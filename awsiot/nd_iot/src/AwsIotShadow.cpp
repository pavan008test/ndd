
/* Copyright (C) 2023 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by PS Dutta <pranoy.dutta@netradyne.com>, June 2023
 */

#include "AwsIotShadow.h"

#include <algorithm>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

#define TAG "IOT-SHDW"

#include <aws/crt/Api.h>
#include <aws/crt/JsonObject.h>
#include <aws/crt/UUID.h>
#include <aws/crt/io/HostResolver.h>

#include <aws/iot/MqttClient.h>

#include <aws/iotshadow/ErrorResponse.h>
#include <aws/iotshadow/IotShadowClient.h>
#include <aws/iotshadow/ShadowDeltaUpdatedEvent.h>
#include <aws/iotshadow/ShadowDeltaUpdatedSubscriptionRequest.h>
#include <aws/iotshadow/UpdateShadowRequest.h>
#include <aws/iotshadow/UpdateShadowResponse.h>
#include <aws/iotshadow/UpdateShadowSubscriptionRequest.h>

#include <aws/iotshadow/GetShadowRequest.h>
#include <aws/iotshadow/GetShadowResponse.h>
#include <aws/iotshadow/GetShadowSubscriptionRequest.h>

// named shadow headers
#include <aws/iotshadow/NamedShadowDeltaUpdatedSubscriptionRequest.h>
#include <aws/iotshadow/UpdateNamedShadowRequest.h>
#include <aws/iotshadow/UpdateNamedShadowSubscriptionRequest.h>
#include <aws/iotshadow/GetNamedShadowRequest.h>
#include <aws/iotshadow/GetNamedShadowSubscriptionRequest.h>

using namespace std;
using namespace Aws::Crt;
using namespace Aws::Iotshadow;

bool AwsIotShadow::update_shadow(const string &document)
{
    LOG_I(TAG, "update_shadow::%s <%s>", m_shadowName.c_str(), document.c_str());
    std::promise<bool> publishCompletedPromise;

    const String thingName(iot->get_thing_name().c_str());
    const String shadow_content(document.c_str());
    JsonObject root = JsonObject(shadow_content);
    JsonView shadow_json = root;

    JsonObject st = shadow_json.GetJsonObjectCopy("state");
    JsonView state_json = st;

    JsonObject desired;
    JsonObject reported;
    ShadowState state;

    if (state_json.ValueExists("desired"))
    {
        desired = state_json.GetJsonObjectCopy("desired");
        state.Desired = desired;
    }

    if (state_json.ValueExists("reported"))
    {
        reported = state_json.GetJsonObjectCopy("reported");
        state.Reported = reported;
    }

    Aws::Crt::UUID uuid;

    if (m_shadowType == SHADOW_TYPE_CLASSIC)
    {
        UpdateShadowRequest updateShadowRequest;
        updateShadowRequest.ClientToken = uuid.ToString();
        updateShadowRequest.ThingName = thingName;
        updateShadowRequest.State = state;
        auto publishCompleted = [&](int ioErr)
        {
            if (ioErr != AWS_OP_SUCCESS)
            {
                LOG_E(TAG, "Failed to update shadow  %s\n", ErrorDebugString(ioErr));
                publishCompletedPromise.set_value(false);
            }
            else
            {
                LOG_I(TAG, "Successfully updated shadow state.");
                publishCompletedPromise.set_value(true);
            }
        };

        m_shadowClient->PublishUpdateShadow(updateShadowRequest, AWS_MQTT_QOS_AT_LEAST_ONCE, std::move(publishCompleted));
    }
    else
    {
        const String shadowName = m_shadowName;

        UpdateNamedShadowRequest updateNamedShadowRequest;
        updateNamedShadowRequest.ClientToken = uuid.ToString();
        updateNamedShadowRequest.ThingName = thingName;
        updateNamedShadowRequest.ShadowName = shadowName;

        updateNamedShadowRequest.State = state;

        auto publishCompleted = [&](int ioErr)
        {
            if (ioErr != AWS_OP_SUCCESS)
            {
                LOG_E(TAG, "Failed to update named shadow  %s\n", ErrorDebugString(ioErr));
                publishCompletedPromise.set_value(false);
            }
            else
            {
                LOG_I(TAG, "Successfully updated named shadow state.");
                publishCompletedPromise.set_value(true);
            }
        };

        m_shadowClient->PublishUpdateNamedShadow(updateNamedShadowRequest, AWS_MQTT_QOS_AT_LEAST_ONCE, std::move(publishCompleted));
    }

    if (publishCompletedPromise.get_future().get())
    {
        return true;
    }
    else
    {
        return false;
    }
}

void AwsIotShadow::initNamedShadowCallbacks()
{
    const String iotThingName(iot->get_thing_name().c_str());

    /********************** Shadow Delta Updates ********************/
    // This section is for when a Shadow document updates/changes, whether it is on the server side or client side.

    subscribeDeltaCompletedPromise = std::promise<void>();
    subscribeDeltaAcceptedCompletedPromise = std::promise<void>();
    subscribeDeltaRejectedCompletedPromise = std::promise<void>();

    NamedShadowDeltaUpdatedSubscriptionRequest namedShadowDeltaUpdatedRequest;
    namedShadowDeltaUpdatedRequest.ThingName = iotThingName;
    namedShadowDeltaUpdatedRequest.ShadowName = m_shadowName;

    m_shadowClient->SubscribeToNamedShadowDeltaUpdatedEvents(
        namedShadowDeltaUpdatedRequest, AWS_MQTT_QOS_AT_LEAST_ONCE, onDeltaUpdatedNamed, onDeltaUpdatedSubAck);

    UpdateNamedShadowSubscriptionRequest updateNamedShadowSubscriptionRequest;
    updateNamedShadowSubscriptionRequest.ThingName = iotThingName;
    updateNamedShadowSubscriptionRequest.ShadowName = m_shadowName;

    m_shadowClient->SubscribeToUpdateNamedShadowAccepted(
        updateNamedShadowSubscriptionRequest,
        AWS_MQTT_QOS_AT_LEAST_ONCE,
        onUpdateShadowAcceptedNamed,
        onDeltaUpdatedAcceptedSubAck);

    m_shadowClient->SubscribeToUpdateNamedShadowRejected(
        updateNamedShadowSubscriptionRequest,
        AWS_MQTT_QOS_AT_LEAST_ONCE,
        onUpdateShadowRejectedNamed,
        onDeltaUpdatedRejectedSubAck);

    subscribeDeltaCompletedPromise.get_future().wait();
    subscribeDeltaAcceptedCompletedPromise.get_future().wait();
    subscribeDeltaRejectedCompletedPromise.get_future().wait();

    /********************** Shadow Value Get ********************/
    // This section is to get the initial value of the Shadow document.

    subscribeGetShadowAcceptedCompletedPromise = std::promise<void>();
    subscribeGetShadowRejectedCompletedPromise = std::promise<void>();
    onGetShadowRequestCompletedPromise = std::promise<void>();
    gotInitialShadowPromise = std::promise<void>();

    GetNamedShadowSubscriptionRequest namedShadowSubscriptionRequest;
    namedShadowSubscriptionRequest.ThingName = iotThingName;
    namedShadowSubscriptionRequest.ShadowName = m_shadowName;

    m_shadowClient->SubscribeToGetNamedShadowAccepted(
        namedShadowSubscriptionRequest,
        AWS_MQTT_QOS_AT_LEAST_ONCE,
        onGetShadowAcceptedNamed,
        onGetShadowUpdatedAcceptedSubAck);

    m_shadowClient->SubscribeToGetNamedShadowRejected(
        namedShadowSubscriptionRequest,
        AWS_MQTT_QOS_AT_LEAST_ONCE,
        onGetShadowRejectedNamed,
        onGetShadowUpdatedRejectedSubAck);

    subscribeGetShadowAcceptedCompletedPromise.get_future().wait();
    subscribeGetShadowRejectedCompletedPromise.get_future().wait();

    GetNamedShadowRequest namedShadowGetRequest;
    namedShadowGetRequest.ThingName = iotThingName;
    namedShadowGetRequest.ShadowName = m_shadowName;

    // Get the current shadow document so we start with the correct value
    m_shadowClient->PublishGetNamedShadow(namedShadowGetRequest, AWS_MQTT_QOS_AT_LEAST_ONCE, onGetShadowRequestSubAck);
    onGetShadowRequestCompletedPromise.get_future().wait();
    gotInitialShadowPromise.get_future().wait();
}

void AwsIotShadow::initClassicShadowCallbacks()
{
    const String iotThingName(iot->get_thing_name().c_str());

    /********************** Shadow Delta Updates ********************/
    // This section is for when a Shadow document updates/changes, whether it is on the server side or client side.

    subscribeDeltaCompletedPromise = std::promise<void>();
    subscribeDeltaAcceptedCompletedPromise = std::promise<void>();
    subscribeDeltaRejectedCompletedPromise = std::promise<void>();

    ShadowDeltaUpdatedSubscriptionRequest shadowDeltaUpdatedRequest;
    shadowDeltaUpdatedRequest.ThingName = iotThingName;

    m_shadowClient->SubscribeToShadowDeltaUpdatedEvents(
        shadowDeltaUpdatedRequest, AWS_MQTT_QOS_AT_LEAST_ONCE, onDeltaUpdated, onDeltaUpdatedSubAck);

    UpdateShadowSubscriptionRequest updateShadowSubscriptionRequest;
    updateShadowSubscriptionRequest.ThingName = iotThingName;

    m_shadowClient->SubscribeToUpdateShadowAccepted(
        updateShadowSubscriptionRequest,
        AWS_MQTT_QOS_AT_LEAST_ONCE,
        onUpdateShadowAccepted,
        onDeltaUpdatedAcceptedSubAck);

    m_shadowClient->SubscribeToUpdateShadowRejected(
        updateShadowSubscriptionRequest,
        AWS_MQTT_QOS_AT_LEAST_ONCE,
        onUpdateShadowRejected,
        onDeltaUpdatedRejectedSubAck);

    subscribeDeltaCompletedPromise.get_future().wait();
    subscribeDeltaAcceptedCompletedPromise.get_future().wait();
    subscribeDeltaRejectedCompletedPromise.get_future().wait();

    /********************** Shadow Value Get ********************/
    // This section is to get the initial value of the Shadow document.

    subscribeGetShadowAcceptedCompletedPromise = std::promise<void>();
    subscribeGetShadowRejectedCompletedPromise = std::promise<void>();
    onGetShadowRequestCompletedPromise = std::promise<void>();
    gotInitialShadowPromise = std::promise<void>();

    GetShadowSubscriptionRequest shadowSubscriptionRequest;
    shadowSubscriptionRequest.ThingName = iotThingName;

    m_shadowClient->SubscribeToGetShadowAccepted(
        shadowSubscriptionRequest,
        AWS_MQTT_QOS_AT_LEAST_ONCE,
        onGetShadowAccepted,
        onGetShadowUpdatedAcceptedSubAck);

    m_shadowClient->SubscribeToGetShadowRejected(
        shadowSubscriptionRequest,
        AWS_MQTT_QOS_AT_LEAST_ONCE,
        onGetShadowRejected,
        onGetShadowUpdatedRejectedSubAck);

    subscribeGetShadowAcceptedCompletedPromise.get_future().wait();
    subscribeGetShadowRejectedCompletedPromise.get_future().wait();

    GetShadowRequest shadowGetRequest;
    shadowGetRequest.ThingName = iotThingName;

    // Get the current shadow document so we start with the correct value
    m_shadowClient->PublishGetShadow(shadowGetRequest, AWS_MQTT_QOS_AT_LEAST_ONCE, onGetShadowRequestSubAck);

    onGetShadowRequestCompletedPromise.get_future().wait();
    gotInitialShadowPromise.get_future().wait();
}

void AwsIotShadow::initShadowCallbacks()
{
    if (m_shadowType == SHADOW_TYPE_CLASSIC)
    {
        initClassicShadowCallbacks();
    }
    else
    {
        initNamedShadowCallbacks();
    }
}

void AwsIotShadow::logInfo(const string &msg)
{
    LOG_I(TAG, "%s : %s", m_shadowName.c_str(), msg.c_str());
}
void AwsIotShadow::logError(const string &msg)
{
    LOG_E(TAG, "%s : %s", m_shadowName.c_str(), msg.c_str());
}

void AwsIotShadow::delta_update(String &deltaDocument)
{
    LOG_I(TAG, "delta_update :: %s : %s", m_shadowName.c_str(), deltaDocument.c_str());
    std::string deltaDocumentStr = deltaDocument.c_str();
    iot->publish_delta_update(deltaDocumentStr, m_shadowType);
}

void AwsIotShadow::read_update(String &updateDocument)
{
    LOG_I(TAG, "read_update :: %s : %s", m_shadowName.c_str(), updateDocument.c_str());
    std::string updateDocumentStr = updateDocument.c_str();
    iot->publish_read_update(updateDocumentStr, m_shadowType);
}


void AwsIotShadow::report_shadow_sync_err(int error_code) {
    LOG_E(TAG, "Failed to sync shadow :: shadow_type: %d, error_code: %d", m_shadowType, error_code);
    if (m_reportShadowFullError && error_code == AWS_HTTP_STATUS_CODE_413_REQUEST_ENTITY_TOO_LARGE)
    {
        m_reportShadowFullError = false;
        iot->raise_shadow_full_alert(m_shadowType);
    }
}
