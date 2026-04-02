/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
  * Unauthorized copying of this file, via any medium is strictly prohibited
  * Proprietary and confidential
  * Written by Karthik Dumpala <karthik.dumpala@netradyne.com>, June 2018
*/

#include <aws_iot_internal.h>
#include "nd_factory.h"

#if defined(KRAIT) || defined(KRAIT2) || defined(BAGHEERA2)
/*imports related to eld functionalitis. B1 does not have this feature*/
#include <nd_prop_utils.h>
#include <nd_pq.h>
#include <iomanip>
#endif
#include <ndmb/nd_msg_interface.h>

#define TAG "PUB_TH"

extern AwsIot *iot;
static const string cloud_config_ini = "/home/ubuntu/.nddevice/latest/cloudconfig.ini";

static const string bagheera_conf_ini = "/home/ubuntu/.nddevice/latest/bagheera_config.ini";
static string server_type = "prod";
static string topic_name = "";
static string topic_name_eld = "";
static const string PUB_Q_NAME = "AWSIOT_PUB";
static const string AWS_Q_NAME = "AWSIOT";
static const int SECS_TO_MILLISECS = 1000;
static nd_msgq_t *server_q_pub_th =NULL;
static int PUBLISH_FREQ_IN_SECS = 30;
static const int MAX_BUFFER_SIZE = 50;
static int msg_idx = 0;
bool ndmb_gps_cb(ndmb_generic_msg_t *msg);
// By default this is kep disabled. Need to enable it from bagheera_config
static bool aws_publish_enabled = false;

#if defined(KRAIT) || defined(KRAIT2) || defined(BAGHEERA2) 
pub_gps_info_msg_t gps_info;
char obd_vin[MAX_SIZE_OF_VIN]={0};
bool vbus_bt_availability = false;
string rtc_jump_from_string= "0";
string rtc_jump_to_string = "0";
extern bool eld_enabled;
extern bool ft_enabled;
extern int qos_level;
#endif

extern string dest_db;
extern volatile bool msg_loop_active;
extern pthread_mutex_t aws_api_mutex;
extern NDService *nd_service_obj;
extern ND_DeviceFactory *nd_device_obj;
static const int GPS_SPEED_THRESHOLD = 5;
static const int POWER_COMPLIANCE_TIMEOUT =60;

bool send_gps_updates_to_awsiot = false;

string get_pub_th_msgq_name() {
    return PUB_Q_NAME;
}

string get_aws_msgq_name() {
    return AWS_Q_NAME;
}
bool get_send_gps_updates_to_awsiot() {
    return send_gps_updates_to_awsiot;
}
/*
void AwsIot::iot_subscribe_callback_handler(AWS_IoT_Client *pClient, char *topicName, uint16_t topicNameLen,
						IoT_Publish_Message_Params *params, void *pData) {
	LOG_I(TAG, "Subscribe callback");
	LOG_I(TAG, "Topic len: %d, Topic name:%s, payload len: %d, payload: %s", topicNameLen, topicName, (int) params->payloadLen, params->payload);
}

bool AwsIot::subscribe( ) {

    uint32_t idx;
    for(idx = 0; idx < AWS_IOT_MQTT_NUM_SUBSCRIBE_HANDLERS; idx++) {
        if((&mqttClient)->clientData.messageHandlers[idx].topicName == NULL) {
            break;
        }
    }

    if(AWS_IOT_MQTT_NUM_SUBSCRIBE_HANDLERS <= idx) {
        LOG_E(TAG, "Max subscriptions reached. Returning from here..");
        return false;
    }

    if(server_type == "staging") {
        topic_name = "staging/periodic/staging-" + iot->get_device_id() + "/";
    }
    else {
        topic_name = "production/periodic/production-" + iot->get_device_id() + "/";
    }

    LOG_I(TAG, "Subscribing to topic %s, topic_len: %d", topic_name.c_str(), strlen(topic_name.c_str()));
    int rc = aws_iot_mqtt_subscribe(&mqttClient, topic_name.c_str(), (uint16_t)(strlen(topic_name.c_str())), QOS0, iot_subscribe_callback_handler, NULL);
    if(SUCCESS != rc) {
        LOG_I(TAG, "Failed to subscribe to given topic. Errcode: %d", rc);
	return false;
    }

    return true;
}


bool AwsIot::publish(string payload) {

    IoT_Publish_Message_Params paramsQOS0;

    if(server_type == "staging") {
        topic_name = "staging/periodic/staging-" + iot->get_device_id() + "/";
    }
    else {
        topic_name = "production/periodic/production-" + iot->get_device_id() + "/";
    }
    LOG_I(TAG, "Payload: %s", payload.c_str());

    paramsQOS0.qos = QOS0;
    paramsQOS0.payload = (void *) payload.c_str();
    paramsQOS0.isRetained = 0;
    paramsQOS0.payloadLen = strlen(payload.c_str());

    pthread_mutex_lock(&aws_api_mutex);
    int rc = aws_iot_shadow_yield( &mqttClient, YIELD_DELAY_MSEC );
    pthread_mutex_unlock(&aws_api_mutex);

    if(NETWORK_RECONNECTED == rc || SUCCESS == rc) {
        rc = aws_iot_mqtt_publish(&mqttClient, topic_name.c_str(), strlen(topic_name.c_str()), &paramsQOS0);

        if(SUCCESS == rc) {
            LOG_I(TAG, "Published data successfully to topic %s", topic_name.c_str());
            return true;
        }
    }

    LOG_I(TAG, "Failed to publish to given topic. Errcode: %d", rc);
    return false;
}
*/

bool AwsIot::publish(string payload)
{
    // TODO: Need to optimize without using check_internet_exist() every time.
    if (!check_internet_exist())
    {
        LOG_E(TAG, "No internet connection, skipping publish");
        return false;
    }

    if (server_type == "staging")
    {
        topic_name = "staging/periodic/staging-" + iot->get_device_id() + "/";
    }
    else
    {
        topic_name = "production/periodic/production-" + iot->get_device_id() + "/";
    }
    LOG_I(TAG, "Payload: %s", payload.c_str());

    std::promise<bool> publishPromise;
    auto future = publishPromise.get_future();

    auto onPublishComplete = [&](Mqtt::MqttConnection &, uint16_t packetId, int errorCode)
    {
        if (errorCode)
        {
            LOG_I(TAG, "Failed to publish to given topic. Errcode: %s", aws_error_debug_str(errorCode));
            publishPromise.set_value(false);
        }
        else
        {
            LOG_I(TAG, "Published data successfully to topic %s", topic_name.c_str());
            publishPromise.set_value(true);
        }
    };

    ByteBuf Payload = ByteBufFromArray((const uint8_t *)payload.data(), payload.length());
    if (!connection->Publish(
            topic_name.c_str(), AWS_MQTT_QOS_AT_MOST_ONCE, false, Payload, onPublishComplete))
    {
        LOG_I(TAG, "Failed to initiate publish.");
        return false; // Indicate failure in initiating publish
    }

    return future.get();
}


#if defined(KRAIT) || defined(KRAIT2) || defined(BAGHEERA2) 

/*
int AwsIot::publish_eld(string payload) {

    IoT_Publish_Message_Params paramsQOS0;

    if(server_type == "staging") {
        topic_name_eld = "staging/candata/staging-" + iot->get_device_id() + "/";
    }
    else {
        topic_name_eld = "production/candata/production-" + iot->get_device_id() + "/";
    }

    printf("Payload: %s\n", payload.c_str());

    if(qos_level == 1){
        paramsQOS0.qos = QOS1;
    }
    else{
        paramsQOS0.qos = QOS0;
    }
    LOG_I(TAG, "MQTT QoS level configured for ELD is %d", qos_level);
    paramsQOS0.payload = (void *) payload.c_str();
    paramsQOS0.isRetained = 0;
    paramsQOS0.payloadLen = strlen(payload.c_str());

    pthread_mutex_lock(&aws_api_mutex);
    int rc = aws_iot_shadow_yield( &mqttClient, YIELD_DELAY_MSEC );

    if(NETWORK_RECONNECTED == rc || SUCCESS == rc) {
        rc = aws_iot_mqtt_publish(&mqttClient, topic_name_eld.c_str(), strlen(topic_name_eld.c_str()), &paramsQOS0);
        if(SUCCESS == rc) {
            LOG_I(TAG, "Published data successfully to topic %s", topic_name_eld.c_str());
            pthread_mutex_unlock(&aws_api_mutex);
            return 0;
        }
        if(MQTT_TX_BUFFER_TOO_SHORT_ERROR == rc) {
            string str_msg = "ELD Payload size more than MQTT TX buffer size, discarding it";
            nd_service_obj->send_err_msg (SM_E_AWS_MQTT_TX_BUFFER_SHORT, NDService::UNUSED_ERR_AUX_CODE, str_msg);
            LOG_E(TAG, str_msg.c_str());
            pthread_mutex_unlock(&aws_api_mutex);
            return 0;
        }
    }

    LOG_I(TAG, "Failed to publish to eld topic. Errcode: %d", rc);
    pthread_mutex_unlock(&aws_api_mutex);
    return rc;
}
*/
int AwsIot::publish_eld(string payload)
{
    if (server_type == "staging")
    {
        topic_name_eld = "staging/candata/staging-" + iot->get_device_id() + "/";
    }
    else
    {
        topic_name_eld = "production/candata/production-" + iot->get_device_id() + "/";
    }
    LOG_I(TAG, "Payload: %s", payload.c_str());
    LOG_I(TAG, "MQTT QoS level configured for ELD is %d", qos_level);

    std::promise<int> publishPromise;
    auto future = publishPromise.get_future();

    auto onPublishComplete = [&](Mqtt::MqttConnection &, uint16_t packetId, int errorCode)
    {
        if (errorCode)
        {
            LOG_I(TAG, "Failed to publish to given topic. Errcode: %s", aws_error_debug_str(errorCode));
            publishPromise.set_value(errorCode);
        }
        else
        {
            LOG_I(TAG, "Published data successfully to topic %s", topic_name_eld.c_str());
            publishPromise.set_value(0);
        }
    };

    ByteBuf Payload = ByteBufFromArray((const uint8_t *)payload.data(), payload.length());
    if (!connection->Publish(
            topic_name_eld.c_str(),
            (qos_level == 1) ? AWS_MQTT_QOS_AT_LEAST_ONCE : AWS_MQTT_QOS_AT_MOST_ONCE,
            false,
            Payload,
            onPublishComplete))
    {
        LOG_I(TAG, "Failed to initiate publish.");
        return -1; // Indicate failure in initiating publish
    }

    return future.get();
}


#endif

bool create_pub_th_msg_q() {
    //Create message queue
    if (server_q_pub_th) {
        LOG_I (TAG, "MSGQ already present");
        return true;
    }

    server_q_pub_th = nd_msgq_t::get_msgq( get_pub_th_msgq_name(), nd_msgq_t::ND_MSGQ_SERVER );

    if( server_q_pub_th == NULL ) {
        LOG_E(TAG, "Cannot create message queue");
        return false;
    }

    LOG_I(TAG, "Message queue created");

    return true;
}

void publish_gps(pub_gps_info_msg_t *data)
{
    if(data == NULL) {
        LOG_E(TAG, "GPS data null. Returning from here..");
        return;
    }

    string payload = "";
    char buffer[MAX_BUFFER_SIZE];

    snprintf (buffer, MAX_BUFFER_SIZE, "%lf", data->latitude);
    payload += "{\"lat\": " + string(buffer) + ", ";
    snprintf (buffer, MAX_BUFFER_SIZE, "%lf", data->longitude);
    payload += "\"long\": " + string(buffer) + ", ";
    snprintf (buffer, MAX_BUFFER_SIZE, "%lf", data->accuracy);
    payload += "\"acc\": " + string(buffer) + ", ";
    snprintf (buffer, MAX_BUFFER_SIZE, "%.2f", data->speed);
    payload += "\"sp\": " + string(buffer) + ", ";
    snprintf (buffer, MAX_BUFFER_SIZE, "%.2lf", data->bearing);
    payload += "\"bear\": " + string(buffer) + ", ";
    snprintf (buffer, MAX_BUFFER_SIZE, "%d", data->ignition);
    payload += "\"ig\": " + string(buffer) + ", ";
    snprintf (buffer, MAX_BUFFER_SIZE, "%lld", data->timestamp);
    payload += "\"ts\": " + string(buffer) + ", ";
    snprintf (buffer, MAX_BUFFER_SIZE, "%d", data->valid);
    payload += "\"valid\": " + string(buffer) + ", ";
    payload += "\"prog\": \"1\", ";
    payload += "\"ver\": \"v1\"}";
    iot->publish(payload);
}

void handle_gps_data_publish(generic_msg_t *msg){

    // Check for null
    if(msg == NULL) {
        LOG_E(TAG, "gps update msg null.. return from here");
        return;       
    }

    res_gps_update_msg_t *m = (res_gps_update_msg_t *)msg;
    pub_gps_info_msg_t gps_info_msg;

    gps_info_msg.speed = m->speed;
    gps_info_msg.latitude = m->lat;
    gps_info_msg.longitude = m->lon;
    gps_info_msg.accuracy = m->accuracy;

    gps_info_msg.bearing = m->bear;
    gps_info_msg.timestamp = m->timestamp;
    gps_info_msg.valid = m->valid;

    std::ifstream gpio_val(nd_device_obj->gpio_crank_level_info_file().c_str());
    char value = '0';

    if(!gpio_val.is_open()) {
        LOG_E(TAG, "unable to open file in power_monitor_ctx::crank_level; return error");
    }
    if(!(gpio_val >> value)) {
        LOG_E(TAG, "unable to read file in power_monitor_ctx::crank_level; return error");
    }
    gpio_val.close();

    if(value == '1') {
        gps_info_msg.ignition = 1;
    }
    else {
        gps_info_msg.ignition = 0;
    }
    //Send internal message to publish this gps data
    if(! send_msg( (generic_msg_t *)&gps_info_msg, (msg_type_t)INTERNAL_AWS_PUBLISH_GPS, sizeof(gps_info_msg), get_pub_th_msgq_name(), get_aws_msgq_name(), msg_idx++ ) ) {
        LOG_E(TAG, "Cannot send message");
    }
}

static bool string_to_number (string str, int &num)
{
    stringstream ss;
    ss.clear();
    ss.str("");

    ss.str(str);
    ss >> num;
    if (ss.fail() || ss.bad())
    {
        LOG_E (TAG,"string_to_number: extraction failed");
        return false;
    }
    return true;
}

bool read_config()
{
    bool get_override_val = true;
    bool is_val_overridden = false;

    Config_parser c(cloud_config_ini);
    if (c.getParseStatus() != true)
    {
        LOG_E (TAG,"Error in parsing cloudconfig file");
    }

    if (c.isPresent ("cloud","server"))
    {
        string server = c.getConfig ("cloud","server","");
        if (server == "")
        {
            LOG_E (TAG,"cloud:server returned empty");
        }
        else
        {
            server_type = server;
        }
    }
    else
    {
        LOG_E (TAG,"cloud:Server not found in config file");
    }

    Config_parser c2(bagheera_conf_ini);

    if (c2.getParseStatus() != true)
    {
        LOG_E (TAG,"Can't parse %s",bagheera_conf_ini.c_str());
        return false;
    }

    if(c2.isPresent("aws_iot_publish","enabled") ) {
        if( "true" == c2.getConfig("aws_iot_publish","enabled", "false", get_override_val, is_val_overridden) ) {
            LOG_I (TAG,"aws iot publish feature is enabled");
            aws_publish_enabled = true;
        }
        else {
            LOG_I (TAG,"aws iot publish is not enabled in config file");
            aws_publish_enabled = false;
        }
    }
    else {
        LOG_E (TAG,"aws_iot_publish:enabled not present in config file");
        aws_publish_enabled = false;
    }

    if (aws_publish_enabled)
    {
        if(c2.isPresent("aws_iot_publish","publish_freq_in_secs") )
        {
            if (string_to_number (c2.getConfig("aws_iot_publish","publish_freq_in_secs", "30", get_override_val, is_val_overridden), PUBLISH_FREQ_IN_SECS))
            {
                LOG_I (TAG, "AWS IOT gps data publish frequency is %d secs", PUBLISH_FREQ_IN_SECS);
            }
            else
            {
                LOG_E (TAG,"String to number conversion failed for aws_iot_publish:publish_freq_in_secs. Using default: %d secs", PUBLISH_FREQ_IN_SECS);
            }
        }
        else
        {
            LOG_E (TAG,"aws_iot_publish:publish_freq_in_secs not present in config file. Setting as %d secs", PUBLISH_FREQ_IN_SECS);
        }
    }

    return true;
}

#if defined(KRAIT) || defined(KRAIT2) || defined(BAGHEERA2)

bool vin_validation(int length){

      //check if junk char is in last or in between
     //if last char is junk we can take the vin
     //there is possibilty that last char may be *

     int i =0;
     int count = 0;

     unsigned char temp_vin[MAX_SIZE_OF_VIN] = {0};

     LOG_I(TAG, "initial VIN %s", obd_vin);

     for (i =0; i < length; i++){

         if( isalnum(obd_vin[i]) ) {
             temp_vin[count++]= obd_vin[i];
         }
         else if (obd_vin[i] == ' ' || obd_vin[i] == '*' ) {
              LOG_E(TAG, "vin has space/star so discardig them");
         }
         else{
              // discard junk char is in between
              LOG_E(TAG, "vehicle vin has junk char: %d in between so discarding", obd_vin[i]);
              memset(obd_vin, 0, sizeof(obd_vin));
              return false;
         }
     }

     memcpy(obd_vin, temp_vin, sizeof(temp_vin));
     return true;
}

#endif


void *handle_publish (void *arg)
{
    nd_msgq_t::nd_msg_t *msg; 

    if(read_config() == false) {
        LOG_E(TAG, "Read config failed. Exiting from here..");
        pthread_exit(NULL);
    }

#if defined(KRAIT) || defined(KRAIT2) || defined(BAGHEERA2) 
    if(aws_publish_enabled == false && eld_enabled == false && ft_enabled == false)
    {
        LOG_E(TAG, "Feature disabled. Exiting from here..");
        pthread_exit(NULL);
    }
#else
    if(aws_publish_enabled == false)
    {
        LOG_E(TAG, "Publish feature disabled. Exiting from here..");
        pthread_exit(NULL);
    }
#endif


    bool res = create_pub_th_msg_q();
    if( !res ) {
        LOG_E(TAG, "Cannnot create msg q");
        pthread_exit(NULL);
    }

    // Populate global prev_ts before starting publish thread
    int64_t prev_ts = get_system_monotonic_time();
    int64_t curr_ts;
    LOG_I(TAG, "prev_ts: %lld", prev_ts);
    while (1)
    {
        if( (msg = server_q_pub_th->receive( )) == NULL ) {
            LOG_E(TAG, "Receive message failed" );
            continue;
        }

    	msg_type_t type = get_msg_type(msg->get_buffer());
    	generic_msg_t *m = (generic_msg_t *)msg->get_buffer();
    	
    	if( m == NULL ) {
    	    LOG_E(TAG, "Received NULL message");
    	    continue;
    	}

    	LOG_D(TAG, "%d received", m->msg_type);

        switch( type ) {

            case OBD_VIN_DATA:
            {
                prop_data_t vin_entry;
                obd_vin_msg_t *obd_vin_msg = (obd_vin_msg_t*)m;
                memcpy (obd_vin, obd_vin_msg->vin_value, MAX_SIZE_OF_VIN);
                obd_vin[MAX_SIZE_OF_VIN -1] = '\0';
                int vin_len = strlen(obd_vin);
                bool response = vin_validation(vin_len);
                if (response){
                    if(set_property_DB("vin_db", obd_vin,dest_db) == false)
                        LOG_E(TAG, "Failed to store VIN in DB");
                    LOG_I(TAG, "vin data is %s", obd_vin);
                }
                else{
                    if(get_property_DB("vin_db", &vin_entry,dest_db)){
                        memcpy (obd_vin, vin_entry.value.c_str(), MAX_SIZE_OF_VIN);
                    }
                }
                break;
            }
            case TIME_SYNC_RTC_JUMP:
            {
                time_sync_rtc_msg_t *time_sync_rtc_jump_msg = (time_sync_rtc_msg_t *)m;
                LOG_I(TAG, "TIME_SYNC_RTC_JUMP message received:  %lld %lld", time_sync_rtc_jump_msg->rtc_jump_from, time_sync_rtc_jump_msg->rtc_jump_to);
                rtc_jump_from_string = to_string(time_sync_rtc_jump_msg->rtc_jump_from);
                rtc_jump_to_string = to_string(time_sync_rtc_jump_msg->rtc_jump_to);
                break;
            }
            default:
                LOG_I(TAG, "Unknown message received");
        }

        delete msg;
    }
}

bool ndmb_gps_cb(ndmb_generic_msg_t *msg)
{
    if(msg == nullptr)
    {
        LOG_E(TAG, "ndmb_gps_cb Invalid message");
        return false;
    }
    if(!aws_publish_enabled){
        LOG_I(TAG, "AWS publish feature is disabled. Returning from here..");
        return true;
    }

    gps_msg_t gps_msg; // Declare gps_msg
    string topic = msg->topic;
    int64_t curr_ts = 0;
    static int64_t prev_ts = 0;

    if(topic == TOPIC_GPS_DATA)
    {
        gps_msg = *reinterpret_cast<gps_msg_t *>( msg );
        res_gps_update_msg_t res_gps_update_msg;
        res_gps_update_msg.handle = 0; // Handle is not used in this context
        res_gps_update_msg.valid = gps_msg.valid;
        res_gps_update_msg.lat = gps_msg.latitude;
        res_gps_update_msg.lon = gps_msg.longitude;     
        res_gps_update_msg.accuracy = gps_msg.accuracy;
        res_gps_update_msg.alt = gps_msg.altitude;
        res_gps_update_msg.bear = gps_msg.bearing;
        res_gps_update_msg.speed = gps_msg.speed;
        res_gps_update_msg.timestamp = gps_msg.timestamp;
        res_gps_update_msg.system_timestamp = gps_msg.system_timestamp; 

        // Check if last update was >= 30sec older
        curr_ts = get_system_monotonic_time();
        if(curr_ts - prev_ts >= PUBLISH_FREQ_IN_SECS*SECS_TO_MILLISECS) {
            if(msg_loop_active) {
                LOG_I(TAG, "sending internal message to publish gps data");
                handle_gps_data_publish((generic_msg_t *)&res_gps_update_msg); // Pass gps_msg by reference
            } else {
                LOG_I(TAG, "msg loop not active. so dropping this gps update");
            }
            prev_ts = curr_ts;
        }
        
    }
    else
    {
        LOG_E(TAG, "ndmb_gps_cb Invalid topic %s", topic.c_str());
    }
    return true;

}


void read_aws_gps_publish_config() {

    Config_parser c(bagheera_conf_ini.c_str());

    if (c.getParseStatus() != true)
    {
        LOG_E (TAG,"Can't parse %s", bagheera_conf_ini.c_str());
        return;
    }

    bool get_override_val = true;
    bool is_val_overridden = false;
    if(c.isPresent("aws_iot_publish","enabled") ) {
        if( ("true" == c.getConfig("aws_iot_publish","enabled", "false",
                        get_override_val, is_val_overridden)) ||
                ("true" == c.getConfig("driveri_one","eld_enabled", "false",
                                       get_override_val, is_val_overridden)) ||
                ("true" == c.getConfig("driveri_one","gps_tracking_enabled", "false",
                                       get_override_val, is_val_overridden)) ) {
            LOG_I (TAG,"aws iot publish feature is enabled");
            send_gps_updates_to_awsiot = true;
        }
        else {
            LOG_I (TAG,"aws iot publish is not enabled in config file");
        }
    }
    else {
        LOG_E (TAG,"aws_iot_publish:enabled not present in config file");
    }


}
