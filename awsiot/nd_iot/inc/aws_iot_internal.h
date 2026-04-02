
/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Arun V <arun.vj@netradyne.com>, February 2016
 */

#ifndef AWS_IOT_INTERNAL_H
#define AWS_IOT_INTERNAL_H

#include <aws/crt/Api.h>
#include <aws/crt/JsonObject.h>
#include <aws/crt/UUID.h>
#include <aws/crt/io/HostResolver.h>
#include <aws/crt/mqtt/MqttClient.h>

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

#include <string>
#include <string.h>

#include <algorithm>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <limits.h>

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "log.h"
#include "service_utils.h"
#include "nd_gpio.h"

#include <string>
#include <list>
#include <iostream>
#include <sstream>

#include <config_parser.h>
#include <aws_iot_parser.h>

#include <system_utils.h>
#include <nd_msgq.h>
#include <nd_time.h>
#include <nd_msg_utils.h>
#include <nd_msg_types.h>
#include <aws_iot_msg.h>
#include <jansson/jansson.h>
#include <errno.h>
#include <nd_file_utils.h>
#include <nd_net_utils.h>
#include <nd_auth_utils.h>

#if defined(KRAIT) || defined(KRAIT2) || defined(BAGHEERA2)
#include "ndmb/nd_mbclient.h"
#include "aws_eld.h"
#endif

using namespace std;
using namespace Aws::Crt;
using namespace Aws::Crt::Mqtt;
using namespace Aws::Iotshadow;

const unsigned int YIELD_DELAY_MSEC = 1000; // value in milli secs assumed that this is always in multiple of 1000

class AwsIot
{
public:
    struct conn_param_t
    {
        string host;
        int port;
        unsigned char* clientCRT_buffer = NULL;
        size_t clientCRT_buffer_len = 0;
        unsigned char* clientKey_buffer = NULL;
        size_t clientKey_buffer_len = 0;
        string rootCA;

        string thingName;
        string client_id;
        string endpoint;
    };

    typedef void read_cb_t(string state, shadow_type_t shadow_type);
    typedef void delta_cb(string delta, shadow_type_t shadow_type);

    bool connect(conn_param_t &conn);
    bool disconnect();
    void disconnect_and_exit();
    bool read_shadow();
    bool subscribe();
    bool publish(string data);
    int publish_eld(string data);
    bool register_read_cb(read_cb_t *cb);
    static AwsIot *get_object();
    bool send_response(string res, shadow_type_t shadow_type);
    bool register_delta(delta_cb *cb);
    string get_device_id() { return device_id; }
    bool get_update_status() { return update_status; }
    void set_update_status(bool s) { update_status = s; }
    bool is_connected() { return connected; }
    void set_connected(bool c) { connected = c; }
    string get_thing_name() { return thing_name; }
    std::shared_ptr<Aws::Crt::Mqtt::MqttConnection> connection;
    void publish_read_update(string state, shadow_type_t shadow_type);
    void publish_delta_update(string state, shadow_type_t shadow_type);
    void raise_shadow_full_alert(shadow_type_t shadow_type);
    bool get_send_gps_updates_to_awsiot();


    // Invoked when a MQTT connect has completed or failed
    const OnConnectionCompletedHandler onConnectionCompleted =
        [&](Aws::Crt::Mqtt::MqttConnection &, int errorCode, Aws::Crt::Mqtt::ReturnCode returnCode, bool)
    {

        /*
        Refer for return codes:
        enum aws_mqtt_connect_return_code
        
        Refer for error codes: 
        enum aws_mqtt_error
        */
        LOG_C("IOT", "OnConnectionCompletedHandler :: return code %d", returnCode);

        if (errorCode)
        {
            LOG_E("IOT", "Connection failed with error %s , errorCode %d", aws_error_debug_str(errorCode), errorCode);
            
            /**
             * Error code AWS_ERROR_MQTT_UNEXPECTED_HANGUP is seen when the certificates are valid but
             * not currently associated with the device.
             * Example scenario:
             * Device is changed from staging to prod.
             * Certificates are reset at Cloud end due to some reason.
             **/

            if(returnCode == AWS_MQTT_CONNECT_NOT_AUTHORIZED || errorCode == AWS_ERROR_MQTT_UNEXPECTED_HANGUP) {
                report_awsiot_auth_error();
            }
            connectionCompletedPromise.set_value(false);
        }
        else
        {
            connectionCompletedPromise.set_value(true);
            set_connected(true);
            LOG_C("IOT", "Connected successfully\n");
        }
    };

    // Invoked when a disconnect message has completed.
    const OnDisconnectHandler onDisconnect = [&](Aws::Crt::Mqtt::MqttConnection &)
    {
        LOG_C("IOT", "Disconnected!!");
        set_connected(false);
    };

    // Invoked when a MQTT connection was interrupted/lost
    const OnConnectionInterruptedHandler onInterrupted = [&](Aws::Crt::Mqtt::MqttConnection &, int error)
    {
        LOG_C("IOT", "Connection interrupted: %s\n", Aws::Crt::ErrorDebugString(error));
        set_connected(false);
    };

    // Invoked when a MQTT connection was interrupted/lost, but then reconnected successfully
    const OnConnectionResumedHandler onResumed = [&](Aws::Crt::Mqtt::MqttConnection &, Aws::Crt::Mqtt::ReturnCode, bool)
    {
        LOG_C("IOT", "Connection resumed");
        set_connected(true);
    };

private:
    AwsIot() : connected(false), cb(NULL), update_status(false) {}
    ~AwsIot()
    {
        connected = false;
        cb = NULL;
    }

    static AwsIot *obj;
    bool connected;
    // AWS_IoT_Client mqttClient;
    conn_param_t conn;
    read_cb_t *rcb;
    string thing_name;
    string device_id;
    // char response[SHADOW_MAX_SIZE_OF_RX_BUFFER];
    bool update_status;
    delta_cb *cb;
    void report_awsiot_auth_error();
    std::promise<bool> connectionCompletedPromise;

};

string get_device_type();
string get_device_ota_version();
string get_server_address();
string get_device_id();
bool send_internal_msg(awsmsg_type_t type);
bool update_request_status(uint64_t req_id, aws_status_t new_status);

#endif
