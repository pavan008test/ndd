#include <gps.h>
#include <nd_msgq.h>
#include <nd_msg_types.h>
#include <nd_msg_utils.h>
#include <service_utils.h>
#include <log.h>
#include <nd_app_timer.h>
#include "ndmb/nd_msg_interface.h"
#include "ndmb/nd_mbclient.h"
#include "ndmb/nd_mbserver.h"
#include "ndmb/nd_msg_interface.h"
#include "ndmb/nd_mbclient.h"
#include "ndmb/nd_mbserver.h"
#include <jansson/jansson.h>
#include <nd_messenger.h>
#include <system_utils.h>
#include <nd_time.h>
#include "gps_dev.h"
#include <nd_config_read_utils.h>
#include <functional>
#include <fstream>
#include "nd_factory.h"
#include <nd_server.h>
#include <deque>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

#define myQ "Q_GPS"
#define dts_gps_q_test "Q_DTS_GPS_MSG"
#define TAG "ND_GPS"
#define nd_central_q "q_nd_central"
#define ROUTE_LOGS
#define ND_SOCKET_INI "/home/ubuntu/.nddevice/latest/nd_core_common.ini"
#define BAGHEERACONFIG_INI "/home/ubuntu/.nddevice/latest/bagheera_config.ini"
#define AUTOMATION_CONFIG "/home/ubuntu/config/automation_config.ini"
static const int64_t ONE_MICRO_IN_NANO = 1000;

extern pthread_t gps_thread;
extern pthread_t gps_pps_thread;
extern atomic<uint64_t> gps_index;
bool is_gps_fix_acquired = false;
extern NDService *nd_service_obj;
constexpr int module_health_check_interval =  120;
constexpr int min_gps_interval = 800;
NDMBServer server("GPS");

mutex gps_healthstatus_mutex;
gps_healthstats_msg_t gps_health;
std::atomic<int64_t> atomic_hs_timestamp_sync(0);
static bool gps_lost = false;

static const float invalid_lat = 91.0f;
static const float invalid_long = 181.0f;

extern double latest_cached_latitude;
extern double latest_cached_longitude;
extern int64_t service_start_time;

namespace DriveSimulation
{
    bool drive_simulation_enabled = false;
#ifdef AUTOMATION
    struct gps_data_dts_t
    {
        Gps::gps_data_t gps_data;
        Gps::gps_extended_data_t gps_extended_data;
    };
    static bool dummy_gps_callback();
    int receiveData_GPS();
    void handle_client(int clientSocket);
    void start_dts_gps_msg_loop();
    std::deque<gps_data_dts_t> gps_data_queue;
    std::atomic<bool> stop_simulation{true}; // Start as true (stopped)
    std::mutex client_mutex;
    std::thread current_client_thread;
#endif
}

bool send_gps_healthstats()
{  
    std::lock_guard<std::mutex> lock(gps_healthstatus_mutex);
    json_t* gps_data = json_object();
    json_t* root = json_object();
    char* gps_health_stats = NULL;
    bool ret = true;

    json_object_set_new( gps_data, "ts", json_integer(gps_health.ts));
    json_object_set_new( gps_data, "lat", json_real(gps_health.lat));
    json_object_set_new( gps_data, "lon", json_real(gps_health.lon));
    json_object_set_new( gps_data, "acc", json_real(gps_health.acc));
    json_object_set_new( gps_data, "spd", json_integer(gps_health.speed));
    json_object_set_new( gps_data, "val", json_integer(gps_health.valid));
    json_object_set_new( gps_data, "fq", json_integer(gps_health.fq));
    json_object_set_new( gps_data, "gs", json_integer(gps_health.gs));
    //json_object_set_new( gps_data, "imuacc", json_string(gps_health.imu_acc.c_str()));
    json_object_set_new( gps_data, "gps", json_integer(gps_health.gps_port_status)); 
    json_object_set_new( gps_data, "gps_ts", json_integer(gps_health.gps_port_status_ts));
    json_object_set_new( gps_data, "ttff", json_integer(gps_health.ttff_ts));
    json_object_set_new( root, "isArray" , json_string("true"));
    json_object_set_new( root, "health_info:gps_info", gps_data);


    if(gps_data != NULL && root != NULL)
    {
        gps_health_stats = json_dumps(root, JSON_REAL_PRECISION(9));
        LOG_D(TAG, "GPS Health Stats: %s", gps_health_stats);
        int length = strlen(gps_health_stats);
        LOG_D(TAG,"Sending GPS Health Metrics To HealthStats");
        ret =  nd_service_obj->send_msg_healthstats(gps_health_stats, length);
        free(gps_health_stats);
        json_decref(root);

    }
    else
    {
        LOG_E(TAG, "GPS Data Is NULL, Unable To Send GPS Health Metrics To HealthStats");
        ret = false;
    }
    return ret;
}

void* send_gps_health (void* args)
{
    constexpr const char* hs_tag = "NDC_HS";
   // LOG_I(hs_tag, "### Starting NDC Health Stats Thread ###"); --- change this log
    json_t *root = json_object();
    json_t* gps_data_array = json_array();
    Config_parser sock_addr_config(ND_SOCKET_INI);
    if( sock_addr_config.getParseStatus() != true) {
        LOG_E(TAG, "Cannot allocate ND_CONFIG_ANALYTICS");
        return NULL;
    }

    bool get_override_val = true;
    bool is_val_overridden = false;

    string messenger_addr_analytics = sock_addr_config.getConfig("messenger_sockets","analytics", "ipc:///dev/shm/MSGQ/8355",
                                get_override_val, is_val_overridden);

    string messenger_topic_health_analytics = sock_addr_config.getConfig("messenger_topics","health_analytics", "ha",
                                get_override_val, is_val_overridden);

    if( messenger_topic_health_analytics == "" || messenger_addr_analytics == "" )
    {
        return NULL;
    }


    NDMessenger::ServerBuilder health_analytics_publisher;
    health_analytics_publisher.setServer(messenger_addr_analytics);
    health_analytics_publisher.setTopic(messenger_topic_health_analytics);


    int i = 0;
    int64_t prev_ts = 0;
    char* gps_health_one_minute = NULL;
    bool health_analytics = true;
    read_health_analytics_config(health_analytics);
    while(1)
    {
        json_t* gps_data = json_object();
        if(gps_health.ts == prev_ts)
        {

            json_object_set_new(gps_data, "timestamp", json_integer(get_system_time()));
            gps_health.lat = invalid_lat;
            gps_health.lon = invalid_long;
            gps_health.acc = 0.0;
            gps_health.speed = 0;
            gps_health.valid = false;
            gps_health.fq = 0;
            gps_health.gs = 0;

            gps_health.gps_port_status = false;
            gps_health.gps_port_status_ts = 0;
            gps_health.ttff_ts = 0;
        }
        else
        {
            json_object_set_new( gps_data, "timestamp", json_integer(gps_health.ts));
        }
        json_object_set_new( gps_data, "lat", json_real(gps_health.lat));
        json_object_set_new( gps_data, "long", json_real(gps_health.lon));
        json_object_set_new( gps_data, "accuracy", json_real(gps_health.acc));
        json_object_set_new( gps_data, "speed", json_integer(gps_health.speed));
        json_object_set_new( gps_data, "valid", json_integer(gps_health.valid));
        json_object_set_new( gps_data, "fq", json_integer(gps_health.fq));
        json_object_set_new( gps_data, "gs", json_integer(gps_health.gs));
        json_object_set_new( gps_data, "imu_acc", json_string(gps_health.imu_acc.c_str()));
        //json_object_set_new( gps_data, "gps", json_integer(gps_health.gps_port_status));
        //json_object_set_new( gps_data, "gps_ts", json_integer(gps_health.gps_port_status_ts));
        //json_object_set_new( gps_data, "ttff", json_integer(gps_health.ttff_ts));

        json_array_append_new(gps_data_array, gps_data);

        prev_ts = gps_health.ts;
        i++;
        LOG_D(TAG, "Gentime Triggered, %lld", atomic_hs_timestamp_sync.load());
        if(atomic_hs_timestamp_sync.load() != 0 || i >= 70)// 60th entry or 1 minute of gps data
        {
            i=0;
            atomic_hs_timestamp_sync = 0;
            if (send_gps_healthstats())
            {
                LOG_D(TAG,"Successfully Sent GPS Health Metrics To HealthStats");
            }
            else
            {
                LOG_E(TAG,"Failed to Send GPS Health Metrics To HealthStats");
            }
            json_object_set_new(root, "gps", gps_data_array);
            gps_health_one_minute = json_dumps(root, JSON_REAL_PRECISION(9));

            json_array_clear(gps_data_array);
            json_object_clear(root);
            root = json_object();
            gps_data_array = json_array();

            if(health_analytics)
            {

                if (gps_health_one_minute == NULL) {
                    LOG_E(TAG, "JSON creation failed for gps health message");
                    json_decref(root);
                }
                else
                {
                    health_analytics_publisher.setMessage(string(gps_health_one_minute));
                    bool status = health_analytics_publisher.publish();
                    if (!status)
                    {
                        LOG_E(TAG, "Failed to send message trying to bind again");
                        health_analytics_publisher.setServer(messenger_addr_analytics);
                        health_analytics_publisher.setTopic(messenger_topic_health_analytics);
                        status = health_analytics_publisher.publish();
                        if(!status)
                        {
                            LOG_E(TAG, "Failed to send message even after retrying to bind");
                        }
                    }
                    if(isAutomationEnabled())
                    {
                        ofstream gps_file("/dev/shm/gps.txt",ios::out | ios::app);
                        gps_file << string(gps_health_one_minute) << " , " << get_system_time();
                        gps_file << "\n";
                    }
                }
                      }
                        else
                        {
                LOG_I(TAG,"Health analytics is disabled");
            }

                if (gps_health_one_minute != NULL)
                {
                    free(gps_health_one_minute);
                    gps_health_one_minute = NULL;
                }
        }
        sleep(1);

    }
}
gps_msg_t prev_gps_msg;
static bool gps_callback(Gps::gps_data_t data, Gps::gps_extended_data_t data_ext)
{
    gps_msg_t msg;

    static gps_msg_t last_known_gps;  //used to cache last know gps information in case of lost fix


    msg.valid      = data.valid;
    msg.latitude   = data.latitude;
    msg.longitude   = data.longitude;
    msg.altitude   = data.altitude;
    msg.speed      = data.speed;
    msg.bearing    = data.bearing;
    msg.accuracy   = data.accuracy;
    msg.timestamp  = data.timestamp;
    msg.system_timestamp = data.system_timestamp;
    msg.good_sattelites = data_ext.good_satellites;
    msg.fix_quality = data_ext.fix_quality;
    msg.gps_index = data_ext.gps_index;
    msg.raw_time_ns = data_ext.raw_time_micro;
    msg.gps_port_status = data_ext.gps_port_status;
    msg.gps_port_status_ts = data_ext.gps_port_status_ts;
    msg.ttff_ts = data_ext.ttff_ts;

    LOG_D(TAG, "GPS Callback: lat %lf, lon %lf, acc %f, speed %f, bearing %f, valid %d, gps_index %d, raw_time_ns %lld, status %d, good_sattelites %d , timstamp %lld, system_timestamp %lld",
          msg.latitude, msg.longitude, msg.accuracy, msg.speed, msg.bearing, msg.valid, msg.gps_index, msg.raw_time_ns, msg.gps_port_status, msg.good_sattelites, msg.timestamp, msg.system_timestamp);

    //Initialize the TS so that it can be used in case of no GPS fix
   
    
    if(msg.raw_time_ns <= 0)
    {
        msg.raw_time_ns = get_system_monotonic_time_ns()/ONE_MICRO_IN_NANO;
        LOG_E(TAG, "Raw time ns is zero, setting to current monotonic time: %lld", last_known_gps.raw_time_ns);
    }

    if(msg.timestamp <= 0)
    {
        msg.timestamp = get_system_time();
        LOG_E(TAG, "Timestamp is zero, setting to current system time: %lld", last_known_gps.timestamp);
    }

    if(msg.system_timestamp <= 0)
    {
        msg.system_timestamp = get_system_time();
        LOG_E(TAG, "System timestamp is zero, setting to current system time: %lld", last_known_gps.system_timestamp);
    }

    last_known_gps.raw_time_ns = msg.raw_time_ns;
    last_known_gps.system_timestamp = msg.system_timestamp;
    last_known_gps.timestamp = msg.timestamp;

    if(msg.valid)
    {
        last_known_gps = msg; // Store the last valid GPS data
    }
    else if( (msg.accuracy == 1000) && (msg.valid == false) )
    {    // This condition is to handle the case where GPS data is not valid but accuracy is set to 1000
        // Accuracy is 1000 if the callback is from SW GPS and not from HW GPS.
   
        last_known_gps.valid = false; // If accuracy is 1000, mark as invalid
        last_known_gps.latitude = msg.latitude;
        last_known_gps.longitude = msg.longitude;
        last_known_gps.accuracy = msg.accuracy;
        msg = last_known_gps;

       
        LOG_I(TAG, "Using last valid GPS data: lat %lf, lon %lf", msg.latitude, msg.longitude);
    }
    else if(msg.valid == false) {
        // In this case the callback is from Port but the GPS fix did not happend yet.
        last_known_gps.latitude  = invalid_lat;
        last_known_gps.longitude = invalid_long;

        //we are reading from the file instead of last_known_gps to handle first time GPS fix scenario.
        read_last_known_valid_gps_data(last_known_gps.latitude,last_known_gps.longitude);
        
        
        last_known_gps.valid = false; 
        msg = last_known_gps; // If GPS data is invalid, use the last valid data
        LOG_D(TAG,"Invalid GPS restoring last valid GPS data: lat %lf, lon %lf", msg.latitude, msg.longitude);
    }
   
    if(data_ext.fix_quality > 0 && is_gps_fix_acquired == false)
    {
        is_gps_fix_acquired = true; 
        if(gps_lost){
            string ErrMsg = "GPS Lock Acquired";
            LOG_I(TAG, ErrMsg.c_str());
            nd_service_obj->send_err_msg(SM_E_GPS_FIX_CHANGE_STATE, (int)data_ext.fix_quality, ErrMsg);
            gps_lost = false;
        }
    }
    else if(data_ext.fix_quality <= 0 && is_gps_fix_acquired == true)
    {
        is_gps_fix_acquired = false;
        gps_lost = true;
        string ErrMsg = "GPS Lock Lost";
        LOG_I(TAG, ErrMsg.c_str());
        nd_service_obj->send_err_msg(SM_E_GPS_FIX_CHANGE_STATE, (int)data_ext.fix_quality, ErrMsg);
    }
    else
    {
        LOG_D(TAG, "GPS Fix Quality : %d", data_ext.fix_quality);
    }

    if(msg.latitude == 0)
    {
        msg.latitude = invalid_lat;
    }
    if(msg.longitude == 0)
    {
        msg.longitude = invalid_long;
    }
    std::shared_ptr<gps_msg_t> sharedPtr(new gps_msg_t);
    *sharedPtr = msg;
 
    bool ret = false;
    bool do_publish = true;
    if(!msg.valid)
    {
        if(msg.system_timestamp - prev_gps_msg.system_timestamp < min_gps_interval)
        {
            LOG_I(TAG, "Ignoring invalid GPS data with timestamp: %lld", msg.timestamp);
            do_publish = false;
        }
    }
    prev_gps_msg = msg;
    if(do_publish)
    {
        ret = server.publish(TOPIC_GPS_DATA, sharedPtr);
    }
    if(ret == false)
    {
        LOG_E(TAG, "Failed to publish GPS data");
    }

    std::lock_guard<std::mutex> lock(gps_healthstatus_mutex);
    gps_health.ts = get_system_time();  //sending system_time instead of GPS time wrt DT-856
    gps_health.lat = data_ext.gps_data.latitude;
    gps_health.lon = data_ext.gps_data.longitude;
    gps_health.acc = data_ext.gps_data.accuracy;
    gps_health.speed =data_ext.gps_data.speed;
    gps_health.valid = data_ext.gps_data.valid;
    gps_health.fq = data_ext.fix_quality;
    gps_health.gs = data_ext.good_satellites;
    gps_health.gps_port_status = data_ext.gps_port_status;
    gps_health.gps_port_status_ts = data_ext.gps_port_status_ts;
    gps_health.ttff_ts = data_ext.ttff_ts;
    // gps_health.ignition_status = gps_sensor_data.ignition_status;

    return true;
}
static bool gps_pps_callback(Gps::gps_pps_data_t val, uint64_t gps_index, uint64_t raw_time_ns)
{
    
    std::shared_ptr<ndmb_gps_pps_data_t> sharedPtr(new ndmb_gps_pps_data_t);
    sharedPtr->pps_index = val.pps_index;
    sharedPtr->pps_raw_time = val.pps_raw_time;
    sharedPtr->pps_clock_time = val.pps_clock_time;
    LOG_I(TAG, "==================Publish PPS====================");
    server.publish(TOPIC_GPS_PPS_DATA, sharedPtr);

    return true;
}
#ifdef AUTOMATION
bool DriveSimulation::dummy_gps_callback()
{
    int64_t start = -1, end = -1;
    int64_t elapsed = -1;
    int64_t sleep_time = 0;
    int64_t sleep_time_ms = 0, previous_sleep_time_ms = 0;
    while (!DriveSimulation::stop_simulation.load())
    {
        try
        {
            if (!DriveSimulation::gps_data_queue.empty())
            {
                start = get_system_time();

                gps_data_dts_t data = DriveSimulation::gps_data_queue.front();
                DriveSimulation::gps_data_queue.pop_front();
                Gps::gps_data_t gps_sensor_data;
                Gps::gps_extended_data_t gps_extended_data;

                gps_sensor_data.valid = data.gps_data.valid;
                gps_sensor_data.latitude = data.gps_data.latitude;
                gps_sensor_data.longitude = data.gps_data.longitude;
                gps_sensor_data.altitude = data.gps_data.altitude;
                gps_sensor_data.speed = data.gps_data.speed;
                gps_sensor_data.bearing = data.gps_data.bearing;
                gps_sensor_data.accuracy = data.gps_data.accuracy;
                gps_sensor_data.flags = data.gps_data.flags;

                gps_extended_data.gps_index = data.gps_extended_data.gps_index;
                gps_extended_data.altitudeMSL = data.gps_extended_data.altitudeMSL;
                gps_extended_data.fix_quality = data.gps_extended_data.fix_quality;
                gps_extended_data.good_satellites = data.gps_extended_data.good_satellites;
                gps_extended_data.gps_port_status = data.gps_extended_data.gps_port_status;
                gps_extended_data.gps_port_status_ts = data.gps_extended_data.gps_port_status_ts;
                gps_extended_data.ttff_ts = data.gps_extended_data.ttff_ts;
                gps_extended_data.gnss_temp = data.gps_extended_data.gnss_temp;

                sleep_time = data.gps_data.timestamp; // time diff between each gps data from sample metadata is being sent from automation_framework, 1st entry is 0
                sleep_time_ms = sleep_time * 1000;
                if (elapsed > 0)
                {
                    elapsed = elapsed - previous_sleep_time_ms; // here elapsed time apart from sleeptime is being calulated
                    sleep_time_ms = sleep_time_ms - elapsed;
                    // minimum 0 to prevent negative values, if in anycase processing time is more than sleep_time_ms
                    if (sleep_time_ms < 0) {
                        sleep_time_ms = 0;
                    }
                }
                if (sleep_time_ms > 0)
                {
                    usleep(sleep_time_ms * 1000);
                }
                gps_sensor_data.timestamp = get_system_time();
                gps_sensor_data.system_timestamp = get_system_time();

                gps_extended_data.raw_time_micro = get_system_monotonic_time_ns() / ONE_MICRO_IN_NANO;

                bool gps_callback_result = gps_callback(gps_sensor_data, gps_extended_data);
                end = get_system_time();
                elapsed = end - start; // time taken to process the previous gps data
                previous_sleep_time_ms = sleep_time_ms;
            }
            else
            {
                LOG_I(TAG, "gps_data_queue is empty");
                return false;
            }
        }
        catch (const std::exception &e)
        {
            LOG_E(TAG, "Exception in dummy_gps_callback: %s", e.what());
            break;
        }
    }

    return true;
}

#endif

#ifdef AUTOMATION
void DriveSimulation::start_dts_gps_msg_loop()
{
    nd_msgq_t *test_msg_q = nd_msgq_t::get_msgq(dts_gps_q_test, nd_msgq_t::ND_MSGQ_SERVER);
    if (test_msg_q == NULL)
    {
        LOG_E(TAG, "Cannot create message queue %s", dts_gps_q_test);
        return;
    }
    while (1)
    {
        nd_msgq_t::nd_msg_t *msg;
        if ((msg = test_msg_q->receive()) == NULL)
        {
            LOG_C(TAG, "Receive message failed");
            continue;
        }

        generic_msg_t *g_msg = (generic_msg_t *)msg->get_buffer();
        if (NULL == g_msg)
        {
            LOG_E(TAG, "msg->get_buffer() returned NULL");
            continue;
        }
        switch (g_msg->msg_type)
        {
        case START_DTS_MSG:
        {
            LOG_I(TAG, "Received START_DTS_MSG");
            nd_service_obj->send_err_msg(SM_E_DTS_START, 0, "Drive simulation GPS started");

            // Only start simulation if a client is connected (stop_simulation is false)
            if (!DriveSimulation::stop_simulation.load())
            {
                std::thread to_call_gps_cb_every_sec(DriveSimulation::dummy_gps_callback);
                to_call_gps_cb_every_sec.detach();
            }
            else
            {
                LOG_E(TAG, "No client connected, simulation not started");
            }
        }
        break;
        default:
            LOG_I(TAG, "Unknown message %d received", g_msg->msg_type);
            break;
        }
    }
}

void DriveSimulation::handle_client(int clientSocket)
{
    nd_server sock;
    string buffer(4000, 0);
    ssize_t recvBytes;

    // Mark simulation as active (client connected)
    DriveSimulation::stop_simulation.store(false);

    string receivedData;
    //  sample data coming from automation
    // {"videoMetaData":[{"accuracy":3.54,"altitude":249.4,"altitudeMSL":0,"bearing":260.3999939,"lat":35.2879448,"long":-80.8828812,
    //   "raw_timestamp":15260916314,"speed":31.761528,"timestamp":1,"valid":1}]}|||
    while (true)
    {
        recvBytes = sock.tcp_receive(clientSocket, &buffer[0], buffer.size(), 0);

        if (recvBytes > 0)
        {
            buffer.resize(recvBytes);
            receivedData += buffer;
            // Check for the delimiter
            size_t delimiterPos = receivedData.find("|||");
            while (delimiterPos != string::npos)
            {
                string dataLine = receivedData.substr(0, delimiterPos);
                receivedData.erase(0, delimiterPos + 3);
                // Parse JSON data for each line
                nlohmann::json json_data = nlohmann::json::parse(dataLine);

                if (json_data.contains("videoMetaData"))
                {
                    int msg_idx = 0;
                    if (json_data["videoMetaData"].is_null())
                    {
                        LOG_E(TAG, "Received null video metadata");
                        continue;
                    }

                    std::vector<json> gps_data = json_data["videoMetaData"];
                    for (auto &video_metadata_point : gps_data)
                    {
                        gps_data_dts_t gps_sensor_data;
                        gps_sensor_data.gps_data.valid = (video_metadata_point.at("valid").get<int>() != 0);
                        gps_sensor_data.gps_data.latitude = video_metadata_point.at("lat");
                        gps_sensor_data.gps_data.longitude = video_metadata_point.at("long");
                        gps_sensor_data.gps_data.altitude = video_metadata_point.at("altitude");
                        gps_sensor_data.gps_data.speed = video_metadata_point.at("speed");
                        gps_sensor_data.gps_data.bearing = video_metadata_point.at("bearing");
                        gps_sensor_data.gps_data.accuracy = video_metadata_point.at("accuracy");
                        gps_sensor_data.gps_data.timestamp = video_metadata_point.at("timestamp");
                        gps_sensor_data.gps_data.system_timestamp = get_system_time();
                        gps_sensor_data.gps_extended_data.gps_index = msg_idx++;
                        gps_sensor_data.gps_extended_data.altitudeMSL = 900;
                        gps_sensor_data.gps_extended_data.fix_quality = 1;
                        gps_sensor_data.gps_extended_data.good_satellites = 1;
                        gps_sensor_data.gps_extended_data.raw_time_micro = video_metadata_point.at("raw_timestamp");
                        gps_sensor_data.gps_extended_data.gps_port_status = true;
                        gps_sensor_data.gps_extended_data.gps_port_status_ts = get_system_time();
                        gps_sensor_data.gps_extended_data.ttff_ts = get_system_time();
                        gps_sensor_data.gps_extended_data.gnss_temp = 35.0f;
                        DriveSimulation::gps_data_queue.push_back(gps_sensor_data);
                    }
                }
                delimiterPos = receivedData.find("|||");
            }
        }
        else
        {
            break;
        }
    }

    // client disconnected, stop simulation
    DriveSimulation::stop_simulation.store(true);
    LOG_I(TAG, "GPS simulation client disconnected, stopping simulation");

    nd_server::closeSocket(clientSocket);
}


int DriveSimulation::receiveData_GPS()
{
    nd_server sock;
    int Port = 12348;

    int Socket = sock.createTcpSocket();
    if (Socket < 0)
    {
        nd_service_obj->send_err_msg(SM_E_DTS_CONN_ERROR, 0, "DTS socket creation error for GPS");
        LOG_E(TAG, "Failed to create TCP socket");
        return 1;
    }
    if (!sock.bindAndListenTcpSocket(Socket, Port))
    {
        nd_service_obj->send_err_msg(SM_E_DTS_CONN_ERROR, 0, "DTS bind/listen error for GPS");
        close(Socket);
        return 1;
    }
    LOG_I(TAG, "TCP server to receive GPS is now listening for incoming connections on port %d", Port);

    // Accept and handle GPS clients
    while (true)
    {
        int ClientSocket = sock.acceptTcpConnection(Socket);
        if (ClientSocket >= 0)
        {
            std::lock_guard<std::mutex> lock(DriveSimulation::client_mutex);

            // Signal any existing simulation to stop
            DriveSimulation::stop_simulation.store(true);

            // If previous client thread is running, wait for it to finish
            if (DriveSimulation::current_client_thread.joinable())
            {
                DriveSimulation::current_client_thread.join();
            }

            // Reset state for new client
            DriveSimulation::stop_simulation.store(true); // Will be set to false when handle_client starts
            DriveSimulation::gps_data_queue.clear();

            // Start new client thread
            DriveSimulation::current_client_thread = std::thread(DriveSimulation::handle_client, ClientSocket);
        }
        else{
            nd_service_obj->send_err_msg(SM_E_DTS_CONN_ERROR, 0, "DTS connection error for GPS");
        }
    }
}
#endif

int main()
{
    NDService *nd_service_obj = NDService::get_service_obj(TAG);

    LOG_I(TAG,"**********Starting GPS Service**********");
    nd_msgq_t *server_q = nd_msgq_t::get_msgq( myQ, nd_msgq_t::ND_MSGQ_SERVER );

    if( server_q == NULL)
    {
        printf("ERROR can't create server msgq\n");
        return -1;
    }

    Gps *gps = Gps::get_gps("ND_GPS");

    if( NULL == gps ) {
        LOG_C(TAG,"Cannot open GPS");
        return false;
    }
#ifdef AUTOMATION
    if (IsDTSEnabled())
    {
        DriveSimulation::drive_simulation_enabled = true;
        LOG_I(TAG, "DTS is enabled, so starting dts_gps_msg_loop and receiveData_GPS thread");
        thread start_dts_th(DriveSimulation::start_dts_gps_msg_loop);
        thread receiveData_GPS_loop_th(DriveSimulation::receiveData_GPS);
        start_dts_th.detach();
        receiveData_GPS_loop_th.detach();
    }
#endif
    service_start_time = get_system_monotonic_time();
    LOG_I(TAG, "Service Start Time (monotonic): %lld", service_start_time);
    server.create_topic(TOPIC_GPS_DATA);
    server.create_topic(TOPIC_GPS_PPS_DATA);
    if (DriveSimulation ::drive_simulation_enabled == false)
    {
        if (false == gps->register_gps_callback((Gps::gps_callback_t *)gps_callback))
        {
            LOG_C(TAG, "Cannot register GPS callback");
            return false;
        }
    }

    if( false == gps->register_gps_pps_callback( (Gps::gps_pps_callback_t *)gps_pps_callback) ) {
        LOG_C(TAG,"Cannot register GPS PPS callback");
        return false;
    }

    if( false == gps->configure(Gps::config_refresh, "1") ) {
        LOG_C(TAG,"Cannot configure GPS refresh rate");
        nd_service_obj->send_err_msg(SM_E_NDC_GPS_FAIL, 0, "Cannot configure GPS refresh rate");
        return false;
    }

    gps->enable_gps(true);
    LOG_I(TAG, "Initializing Timer");
    
    Timer apptimer;
    apptimer.register_for_timer(std::bind(&Gps::monitor_gps_cb, gps), module_health_check_interval);
    apptimer.startTimer();

    pthread_t gps_health_th;
    if (!pthread_create (&gps_health_th, NULL, send_gps_health, NULL)) {
        LOG_I (TAG, "Thread created for send gps health");
    }
    else {
        LOG_E(TAG, "failed to launch send gps health thread");
    }
    
    // Message queue handling loop
    while(1)
    {
        nd_msgq_t::nd_msg_t *msg = NULL;
        // Block until a new message is received
        if( (msg = server_q->receive( )) == NULL ) {
            LOG_E(TAG, "Receive message failed" );
            continue;
        }

        generic_msg_t *g_msg = (generic_msg_t *)msg->get_buffer();
        if( g_msg == NULL ) {
            LOG_E(TAG, "Received NULL message");
            continue;
        }

        LOG_D(TAG, "Message received: %d", g_msg->msg_type);

        switch( g_msg->msg_type ) {
            case REQ_HEALTH_INFO:
                {
                    hs_gen_time *hs_msg = (hs_gen_time *)g_msg;
                    atomic_hs_timestamp_sync = hs_msg->time;
                    LOG_D(TAG, "REQ_HEALTH_INFO Received, %lld", atomic_hs_timestamp_sync.load());
                }
                break;
                
            default:
                LOG_D(TAG, "Unknown message received: %d", g_msg->msg_type);
                break;
        }
        // Delete the message after processing
        delete msg;
    }
    
    (void) pthread_join(gps_thread, NULL);
    (void) pthread_join(gps_pps_thread, NULL);

}
