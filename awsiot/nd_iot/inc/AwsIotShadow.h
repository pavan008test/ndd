/* Copyright (C) 2023 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by PS Dutta <pranoy.dutta@netradyne.com>, June 2023
 */

#include <aws_iot_internal.h>

using namespace std;
using namespace Aws::Crt;
using namespace Aws::Iotshadow;

static const String SHADOW_NAME_CLASSIC = "Classic Shadow";
static const String SHADOW_NAME_LS = "priority-shadow-ls";
static const String SHADOW_NAME_VOD = "priority-shadow-vod";

class AwsIotShadow
{
    shadow_type_t m_shadowType;
    String m_shadowName;
    bool m_reportShadowFullError = true;
    AwsIot *iot = AwsIot::get_object();
    IotShadowClient *m_shadowClient = new IotShadowClient(iot->connection);
    void initClassicShadowCallbacks();
    void initNamedShadowCallbacks();
    void logInfo(const string &msg);
    void logError(const string &msg);
    void report_shadow_sync_err(int error_code);

public:
    std::promise<void> subscribeGetShadowAcceptedCompletedPromise;
    std::promise<void> subscribeGetShadowRejectedCompletedPromise;
    std::promise<void> onGetShadowRequestCompletedPromise;
    std::promise<void> gotInitialShadowPromise;
    std::promise<void> subscribeDeltaCompletedPromise;
    std::promise<void> subscribeDeltaAcceptedCompletedPromise;
    std::promise<void> subscribeDeltaRejectedCompletedPromise;

    AwsIotShadow()
    {
        m_shadowName = SHADOW_NAME_CLASSIC;
        m_shadowType = SHADOW_TYPE_CLASSIC;
    }

    AwsIotShadow(const shadow_type_t &shadowType)
    {
        m_shadowType = shadowType;
        switch (shadowType)
        {
        case SHADOW_TYPE_NAMED_LS:
            m_shadowName = SHADOW_NAME_LS;
            break;
        case SHADOW_TYPE_NAMED_VOD:
            m_shadowName = SHADOW_NAME_VOD;
            break;
        case SHADOW_TYPE_CLASSIC:
            m_shadowName = SHADOW_NAME_CLASSIC;
            break;
        default:
            break;
        }
    }

    bool update_shadow(const string &document);

    void changeShadowValue(
        const String &shadowProperty,
        const String &value);

    void initShadowCallbacks();

    void delta_update(String &deltaDoc);
    void read_update(String &shadowDoc);

    const OnSubscribeComplete onDeltaUpdatedSubAck = [&](int ioErr)
    {
        if (ioErr != AWS_OP_SUCCESS)
        {
            string errorMsg = ErrorDebugString(ioErr);
            logError("Error subscribing to shadow delta: " + errorMsg);
            iot->disconnect_and_exit();
        }
        try {
            subscribeDeltaCompletedPromise.set_value();
        } catch (const std::future_error& e) {
            logError(std::string("onDeltaUpdatedSubAck :: Exception while setting promise, ignoring :") + e.what());
        }
    };

    const OnSubscribeComplete onDeltaUpdatedAcceptedSubAck = [&](int ioErr)
    {
        if (ioErr != AWS_OP_SUCCESS)
        {
            string errorMsg = ErrorDebugString(ioErr);
            logError("Error subscribing to shadow delta accepted: " + errorMsg);
            iot->disconnect_and_exit();
        }
        try {
            subscribeDeltaAcceptedCompletedPromise.set_value();
        } catch (const std::future_error& e) {
            logError(std::string("onDeltaUpdatedAcceptedSubAck :: Exception while setting promise, ignoring :") + e.what());
        }
    };

    const OnSubscribeComplete onDeltaUpdatedRejectedSubAck = [&](int ioErr)
    {
        if (ioErr != AWS_OP_SUCCESS)
        {
            string errorMsg = ErrorDebugString(ioErr);
            logError("Error subscribing to shadow delta rejected: " + errorMsg);
            iot->disconnect_and_exit();
        }
        try {
            subscribeDeltaRejectedCompletedPromise.set_value();
        } catch (const std::future_error& e) {
            logError(std::string("onDeltaUpdatedRejectedSubAck :: Exception while setting promise, ignoring :") + e.what());
        }
    };

    const OnSubscribeToShadowDeltaUpdatedEventsResponse onDeltaUpdated = [&](ShadowDeltaUpdatedEvent *event, int ioErr)
    {
        if (ioErr)
        {
            string errorMsg = ErrorDebugString(ioErr);
            logError("Error processing shadow delta: " + errorMsg);
            iot->disconnect_and_exit();
        }

        if (event && event->State && !event->State->View().IsNull())
        {
            String delta_content = event->State->View().WriteCompact();
            delta_update(delta_content);
        }
    };

    const OnSubscribeToUpdateShadowAcceptedResponse onUpdateShadowAccepted = [&](UpdateShadowResponse *response, int ioErr)
    {
        if (ioErr != AWS_OP_SUCCESS)
        {
            string errorMsg = ErrorDebugString(ioErr);
            logError("Error on subscription: " + errorMsg);
            iot->disconnect_and_exit();
        }
    };

    const OnSubscribeToUpdateShadowRejectedResponse onUpdateShadowRejected = [&](ErrorResponse *error, int ioErr)
    {
        if (ioErr != AWS_OP_SUCCESS)
        {
            string errorMsg = ErrorDebugString(ioErr);
            logError("Error on subscription: " + errorMsg);
            iot->disconnect_and_exit();
        }
        stringstream ss;
        ss << "Update of shadow state failed with message " << error->Message->c_str() << " and code " << *error->Code;
        logError(ss.str());
        report_shadow_sync_err(*error->Code);
    };

    const OnSubscribeComplete onGetShadowUpdatedAcceptedSubAck = [&](int ioErr)
    {
        if (ioErr != AWS_OP_SUCCESS)
        {
            string errorMsg = ErrorDebugString(ioErr);
            logError("Error subscribing to get shadow document accepted: " + errorMsg);
            iot->disconnect_and_exit();
        }
        try {
            subscribeGetShadowAcceptedCompletedPromise.set_value();
        } catch (const std::future_error& e) {
            logError(std::string("onGetShadowUpdatedAcceptedSubAck :: Exception while setting promise, ignoring :") + e.what());
        }
    };

    const OnSubscribeComplete onGetShadowUpdatedRejectedSubAck = [&](int ioErr)
    {
        if (ioErr != AWS_OP_SUCCESS)
        {
            string errorMsg = ErrorDebugString(ioErr);
            logError("Error subscribing to get shadow document rejected: " + errorMsg);
            iot->disconnect_and_exit();
        }
        try {
            subscribeGetShadowRejectedCompletedPromise.set_value();
        } catch (const std::future_error& e) {
            logError(std::string("onGetShadowUpdatedRejectedSubAck :: Exception while setting promise, ignoring :") + e.what());
        }
    };

    const OnPublishComplete onGetShadowRequestSubAck = [&](int ioErr)
    {
        if (ioErr != AWS_OP_SUCCESS)
        {
            string errorMsg = ErrorDebugString(ioErr);
            logError("Error getting shadow document: " + errorMsg);
            iot->disconnect_and_exit();
        }
        try {
            onGetShadowRequestCompletedPromise.set_value();
        } catch (const std::future_error& e) {
            logError(std::string("onGetShadowRequestSubAck :: Exception while setting promise, ignoring :") + e.what());
        }
    };

    const OnSubscribeToGetShadowAcceptedResponse onGetShadowAccepted = [&](GetShadowResponse *response, int ioErr)
    {
        if (ioErr != AWS_OP_SUCCESS)
        {
            string errorMsg = ErrorDebugString(ioErr);
            logError("Error getting shadow value from document: " + errorMsg);
            iot->disconnect_and_exit();
        }
        if (response)
        {
            String state = "\"state\"";
            String metadata = "\"metadata\"";
            String desired = "\"desired\"";
            String reported = "\"reported\"";

            String desired_val, reported_val, metadata_desired_val, metadata_reported_val;

            if (response->State && response->State->Desired && !response->State->Desired->View().IsNull())
            {
                desired_val = response->State->Desired->View().WriteCompact();
            }
            else
            {
                desired_val = "{}";
            }

            if (response->State && response->State->Reported && !response->State->Reported->View().IsNull())
            {
                reported_val = response->State->Reported->View().WriteCompact();
            }
            else
            {
                reported_val = "{}";
            }

            if (response->Metadata && response->Metadata->Desired && !response->Metadata->Desired->View().IsNull())
            {
                metadata_desired_val = response->Metadata->Desired->View().WriteCompact();
            }
            else
            {
                metadata_desired_val = "{}";
            }
            if (response->Metadata && response->Metadata->Reported && !response->Metadata->Reported->View().IsNull())
            {
                metadata_reported_val = response->Metadata->Reported->View().WriteCompact();
            }
            else
            {
                metadata_reported_val = "{}";
            }

            String shadow_content = "{" + state + ":" + "{" + desired + ":" + desired_val + "," + reported + ":" + reported_val + "}" +
                                    "," + metadata + ":" + "{" + desired + ":" + metadata_desired_val + "," + reported + ":" + metadata_reported_val +
                                    "}}";

            read_update(shadow_content);

            try {
                gotInitialShadowPromise.set_value();
            } catch (const std::future_error& e) {
                logError(std::string("OnSubscribeToGetShadowAcceptedResponse :: Exception while setting promise, ignoring :") + e.what());
            }
        }
    };

    const OnSubscribeToGetShadowRejectedResponse onGetShadowRejected = [&](ErrorResponse *error, int ioErr)
    {
        if (ioErr != AWS_OP_SUCCESS)
        {
            string errorMsg = ErrorDebugString(ioErr);
            logError("Error on getting shadow document: " + errorMsg);
            iot->disconnect_and_exit();
        }
        stringstream ss;
        ss << "Getting shadow document failed with message " << error->Message->c_str() << " and code " << *error->Code;
        logInfo(ss.str());
        try {
            gotInitialShadowPromise.set_value();
        } catch (const std::future_error& e) {
            logError(std::string("OnSubscribeToGetShadowRejectedResponse :: Exception while setting promise, ignoring :") + e.what());
        }
    };

    // named shadow callbacks

    // named shadow DELTA
    const OnSubscribeToNamedShadowDeltaUpdatedEventsResponse onDeltaUpdatedNamed = [&](ShadowDeltaUpdatedEvent *event, int ioErr)
    {
        if (ioErr)
        {
            string errorMsg = ErrorDebugString(ioErr);
            logError("Error processing shadow delta: " + errorMsg);
            iot->disconnect_and_exit();
        }

        if (event && event->State && !event->State->View().IsNull())
        {
            String delta_content = event->State->View().WriteCompact();
            delta_update(delta_content);
        }
    };

    const OnSubscribeToUpdateNamedShadowAcceptedResponse onUpdateShadowAcceptedNamed = [&](UpdateShadowResponse *response, int ioErr)
    {
        if (ioErr != AWS_OP_SUCCESS)
        {
            string errorMsg = ErrorDebugString(ioErr);
            logError("Error on subscription: " + errorMsg);
            iot->disconnect_and_exit();
        }
    };

    const OnSubscribeToUpdateNamedShadowRejectedResponse onUpdateShadowRejectedNamed = [&](ErrorResponse *error, int ioErr)
    {
        if (ioErr != AWS_OP_SUCCESS)
        {
            string errorMsg = ErrorDebugString(ioErr);
            logError("Error on subscription : " + errorMsg);
            iot->disconnect_and_exit();
        }
        stringstream ss;
        ss << "Update of shadow state failed with message " << error->Message->c_str() << " and code " << *error->Code;
        logInfo(ss.str());
        report_shadow_sync_err(*error->Code);
    };

    // named shadow GET
    const OnSubscribeToGetNamedShadowAcceptedResponse onGetShadowAcceptedNamed = [&](GetShadowResponse *response, int ioErr)
    {
        if (ioErr != AWS_OP_SUCCESS)
        {
            string errorMsg = ErrorDebugString(ioErr);
            logError("Error getting shadow value from document: " + errorMsg);
            iot->disconnect_and_exit();
        }
        if (response)
        {
            String state = "\"state\"";
            String metadata = "\"metadata\"";
            String desired = "\"desired\"";
            String reported = "\"reported\"";

            String desired_val, reported_val, metadata_desired_val, metadata_reported_val;

            if (response->State && response->State->Desired && !response->State->Desired->View().IsNull())
            {
                desired_val = response->State->Desired->View().WriteCompact();
            }
            else
            {
                desired_val = "{}";
            }

            if (response->State && response->State->Reported && !response->State->Reported->View().IsNull())
            {
                reported_val = response->State->Reported->View().WriteCompact();
            }
            else
            {
                reported_val = "{}";
            }

            if (response->Metadata && response->Metadata->Desired && !response->Metadata->Desired->View().IsNull())
            {
                metadata_desired_val = response->Metadata->Desired->View().WriteCompact();
            }
            else
            {
                metadata_desired_val = "{}";
            }
            if (response->Metadata && response->Metadata->Reported && !response->Metadata->Reported->View().IsNull())
            {
                metadata_reported_val = response->Metadata->Reported->View().WriteCompact();
            }
            else
            {
                metadata_reported_val = "{}";
            }

            String shadow_content = "{" + state + ":" + "{" + desired + ":" + desired_val + "," + reported + ":" + reported_val + "}" +
                                    "," + metadata + ":" + "{" + desired + ":" + metadata_desired_val + "," + reported + ":" + metadata_reported_val +
                                    "}}";
            read_update(shadow_content);

            try {
                gotInitialShadowPromise.set_value();
            } catch (const std::future_error& e) {
                logError(std::string("OnSubscribeToGetNamedShadowAcceptedResponse :: Exception while setting promise, ignoring :") + e.what());
            }
        }
    };

    const OnSubscribeToGetNamedShadowRejectedResponse onGetShadowRejectedNamed = [&](ErrorResponse *error, int ioErr)
    {
        if (ioErr != AWS_OP_SUCCESS)
        {
            string errorMsg = ErrorDebugString(ioErr);
            logError("Error on getting shadow document: " + errorMsg);
            iot->disconnect_and_exit();
        }
        stringstream ss;
        ss << "Getting shadow document failed with message " << error->Message->c_str() << " and code " << *error->Code;
        logInfo(ss.str());
        try {
            gotInitialShadowPromise.set_value();
        } catch (const std::future_error& e) {
            logError(std::string("OnSubscribeToGetNamedShadowRejectedResponse :: Exception while setting promise, ignoring :") + e.what());
        }
    };
};
