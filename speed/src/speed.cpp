/* Copyright (C) 2017 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Arun V <arun.vj@netradyne.com>, Mar 2017
 */

#include <config.h>
#include <log.h>
#include "nd_msgq.h"
#include "nd_msg_types.h"
#include "nd_msg_utils.h"
#include <memory>
#include <fstream>
#include <string>
#include <sstream>
#include <list>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <atomic>
#include "service_utils.h"
#include "config_parser.h"
#include "system_utils.h"
#include "nd_time.h"
#include <iomanip>
#include <nd_app_timer.h>

using namespace std;
#include <ndmb/nd_mbclient.h>
//#define UNIT_TEST
#define ROUTE_LOGS
#define TAG "SPD"
#define Q_NAME "SPEED"
#define GPS_SERVICE "q_nd_central"
#define POWER_MON_SERVICE "q_power_monitor"

static int handle_speed_reg(generic_msg_t *msg);
static int handle_speed_unreg(generic_msg_t *msg);
static int handle_idle_reg(generic_msg_t *msg);
static int handle_idle_unreg(generic_msg_t *msg);
static void handle_gps_update(gps_msg_t *m);
static void send_handle_speed_reg_res(generic_msg_t *msg, int res);
static void send_handle_speed_unreg_res(generic_msg_t *msg, int res);
static void send_handle_idle_reg_res(generic_msg_t *msg, int res);
static void send_handle_idle_unreg_res(generic_msg_t *msg, int res);

static void cache_gps_data(gps_msg_t *m);
static void one_second_tick(void);

static string get_msgq_name();



NDService *nd_service_obj; //nd service object, to detect critical Errors which will be send to Health stats and cloud
#ifdef UNIT_TEST
void unit_test();
#endif
static nd_msgq_t *server_q=NULL;
static int msg_idx = 0;
static Timer* speed_timer = nullptr;

static int handles = 0;
static const string log_dir = "/home/ubuntu/.nddevice/log/speed";
static const string speed_info_file = "/dev/shm/speed.info";
static const string prev_speed_info_file = "/home/ubuntu/.nddevice/previous_speed.info";

static int engine_idle_enable_speed = 0;
static int engine_idle_enable_duration = 60;// 1 Minute,,

static volatile int64_t latest_gps_time = 0;
std::atomic<bool> g_privacy_ndc(true); // latest privacy for ndcentral is stored in this variable
static const string BAGHEERACONFIG_INI = "/home/ubuntu/.nddevice/latest/bagheera_config.ini";
static const float invalid_lat = 91.0f;
static const float invalid_long = 181.0f;
const string gps_cache_path = "/home/ubuntu/.nddevice/gps_cache.json";
int lat_long_retention_duration = 5;

// Time interval to send the privacy information to NDC
static const int PRIVACY_SEND_INTERVAL = 1; // in seconds

static double g_gps_lat = invalid_lat;
static double g_gps_lon = invalid_long;
static double g_gps_alt = 0;
static double g_gps_bear = 0;
static double g_gps_speed = 0;
bool dta_enabled = false;
static int out_of_idle_soak_timeout_default = 5;
static float invalid_speed_threshold = 5.0;
static float invalid_accuracy_threshold = 10.0;
std::atomic<bool> ignition_status(false);
static std::atomic<int64_t> vbus_speed{0};
static std::atomic<int64_t> vbus_speed_timestamp{0};

// Cached GPS data with monotonic timestamp
struct cached_gps_data_t {
    float speed;
    float latitude;
    float longitude;
    float altitude;
    float bearing;
    float accuracy;
    bool valid;
    int64_t gps_timestamp;
    int64_t monotonic_timestamp;
};

static cached_gps_data_t cached_gps = {0, invalid_lat, invalid_long, 0, 0, 0, false, 0, 0};
static std::mutex gps_cache_mutex;

struct speed_reg_t {
	string client;
	int handle;

	int speed;
	int contig_secs;

	int cnt;
    speed_reg_type_t type;    
};


struct idle_reg_t {
	string client;
	int handle;
    int speed;

	int idle_secs;

	int cnt;
    int out_of_idle_cnt;
    bool idle_sent;
    bool periodic_idle_events;
    idle_reg_type_t type;    
    int out_of_idle_soak_timeout = 5;
    bool idle_on = false;


};
bool read_lat_long_retention_duration()
{
    bool get_override_val = true;
    bool is_val_overridden = false;
    Config_parser c(BAGHEERACONFIG_INI);

    if (c.getParseStatus() != true)
    {
        LOG_E (TAG,"Can't parse bagheera config");
        return false;
    }

    if(false == string_to_integer(c.getConfig("gps","lat_long_retention_duration","5", get_override_val, is_val_overridden),lat_long_retention_duration) ) {
            LOG_E (TAG, "Failed to convert lat_long_retention_duration to int");
            return false;
    }
    else {
        LOG_I (TAG, "lat_long_retention_duration is %d", lat_long_retention_duration);
    }

    return true;

}

map<size_t, speed_reg_t> speed_regs;
map<size_t, idle_reg_t> idle_regs;
size_t hash_key(const std::string& key)
{ 
    std::hash<std::string> hasher;
    return hasher(key);
}
size_t create_key(const speed_reg_t& reg)
{
    string key =  reg.client + "_" +
        std::to_string(reg.speed) + "_" + std::to_string(reg.contig_secs) + 
        "_" + std::to_string(reg.type);
    LOG_I(TAG, "Spped Reg Key = %s", key.c_str());
    return hash_key(key);
}  

size_t create_key(const idle_reg_t& reg) 
{
    string key =  reg.client + "_" +
        std::to_string(reg.speed) + "_" + std::to_string(reg.idle_secs)  +
         "_" + std::to_string(reg.type);
    LOG_I(TAG, "Idle Reg Key = %s", key.c_str());
    return hash_key(key);
}  
bool init_msgq() {

    //Create message queue
    server_q = nd_msgq_t::get_msgq( get_msgq_name(), nd_msgq_t::ND_MSGQ_SERVER );

    if( server_q == NULL ) {
	LOG_E(TAG, "Cannot create message queue");
	return false;
    }
    
    LOG_I(TAG, "Message queue created");

    return true;
}

bool do_regs() {
	req_gps_reg_msg_t m;

        //Check whether bagheera is alive or not
        if( !is_msg_q_created( GPS_SERVICE ) ) {
                return false;
        }

	if( send_msg( (generic_msg_t *)&m, REQ_GPS_REG, sizeof(m), get_msgq_name(), GPS_SERVICE, msg_idx++ ) == false ) {
		LOG_E(TAG, "Cannot send GPS registration messsage");
		return false;
	}
	
        LOG_I(TAG, "Send GPS registration message");
        return true;
}

void do_unregs() {
	req_gps_reg_msg_t m;

	send_msg( (generic_msg_t *)&m, REQ_GPS_UNREG, sizeof(m), get_msgq_name(), GPS_SERVICE, msg_idx++ );	
}
bool ndmb_gps_cb(ndmb_generic_msg_t *msg)
{
    if(msg == nullptr)
    {
        LOG_E(TAG, "ndmb_gps_cb Invalid message");
        return false;
    }
    std::lock_guard<std::mutex> lock(gps_cache_mutex);
    gps_msg_t gps_msg; // Declare gps_msg
    string topic = msg->topic;
    
    if(topic == TOPIC_GPS_DATA)
    {
        gps_msg = *reinterpret_cast<gps_msg_t *>(msg); // Assign the dereferenced pointer to gps_msg
        LOG_D(TAG, "ndmb_gps_cb Received GPS data, caching with timestamp %lld", gps_msg.timestamp);
        
        // Cache GPS data instead of processing immediately
        cache_gps_data(&gps_msg);

        if(gps_msg.valid == true) {
            latest_gps_time = get_system_monotonic_time();
        

            static int64_t gps_cache_counter = 0;
            gps_cache_counter++;

            if((gps_cache_counter % lat_long_retention_duration) == 0) 
            {
                LOG_D(TAG, "GPS Cache : Latitude: %f Longitude: %f", gps_msg.latitude, gps_msg.longitude);
                std::ofstream gps_cache_out(gps_cache_path);
                if (gps_cache_out.is_open()) {
                    // Create a proper JSON object with latitude and longitude
                    gps_cache_out << "{" << std::endl;
                    gps_cache_out << "  \"latitude\": " << std::fixed << std::setprecision(6) << gps_msg.latitude << "," << std::endl;
                    gps_cache_out << "  \"longitude\": " << std::fixed << std::setprecision(6) << gps_msg.longitude << std::endl;
                    gps_cache_out << "}" << std::endl;
                    gps_cache_out.close();

                    int fd = open(gps_cache_path.c_str(), O_RDWR);
                    if (fd != -1) {
                        fsync(fd);
                        close(fd);
                    }
                    else {
                        LOG_E(TAG, "Failed to open gps_cache.json for fsync");
                    }
                    
                }
                LOG_D(TAG, "GPS Cache updated with %s", gps_cache_path.c_str());
            }
        }
    }
    else
    {
        LOG_E(TAG, "ndmb_gps_cb Invalid topic %s", topic.c_str());
        return false;
    }
    return true;
}

bool ndmb_vbus_cb(ndmb_generic_msg_t *msg)
{
    if(msg == nullptr)
    {
        LOG_E(TAG, "ndmb_vbus_cb Invalid message");
        return false;
    }
    ndmbmsg_vehicle_speed_t vbus_speed_msg;
    string topic = msg->topic;
    if(topic == TOPIC_OBD_VEH_SPEED)
    {
        vbus_speed_msg = *reinterpret_cast<ndmbmsg_vehicle_speed_t *>(msg);
        vbus_speed = vbus_speed_msg.vehicle_speed;
        vbus_speed_timestamp = vbus_speed_msg.timestamp;

    }
    else
    {
        LOG_E(TAG, "ndmb_vbus_cb Invalid topic %s", topic.c_str());
        return false;
    }

    return true;
}

void handle_ignition_status_change(powermon_ignition_msg_t *m)
{
    if (m == nullptr) {
        LOG_E(TAG, "Failed to handle ignition status change: received null powermon message");
        return;
    }
    if (m->status == static_cast<int64_t>(IGNITION_ON)) {
        ignition_status = true;
    }
    else if (m->status == static_cast<int64_t>(IGNITION_OFF)) {
        ignition_status = false;
    }
    else {
        LOG_E(TAG, "Invalid ignition status received: %lld", m->status);
    }
}

void msg_loop() {
    nd_msgq_t::nd_msg_t *msg; 
    int res;
#ifdef UNIT_TEST
    unit_test();
#endif
    while(1) {
        if( (msg = server_q->receive( )) == NULL ) {
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
		case REQ_SPEED_REG:
			res = handle_speed_reg(m);
			send_handle_speed_reg_res(m, res);
			break;

		case REQ_SPEED_UNREG:
			res = handle_speed_unreg(m);
			send_handle_speed_unreg_res(m, res);
			break;

		case REQ_IDLE_REG:
			res = handle_idle_reg(m);
			send_handle_idle_reg_res(m, res);
			break;

		case REQ_IDLE_UNREG:
			res = handle_idle_unreg(m);
			send_handle_idle_unreg_res(m, res);
			break;

		case RES_GPS_REG:
			LOG_I(TAG, "GPS updates registration success");
			break;

		case RES_GPS_UNREG:
			LOG_I(TAG, "GPS updates un-registration success");
			break;

		case RES_GPS_UPDATE:
			
			break;
        case POWERMON_IGNITION:
        {
            powermon_ignition_msg_t *msg = (powermon_ignition_msg_t*)m;
            handle_ignition_status_change(msg);
            break;
        }

		default:
			LOG_E(TAG, "Unknown message: %d", type);
	}
	
	delete msg;
    }
}

static bool send_privacy_mode_to_ndcentral (bool privacy_on)
{
    privacy_mode_update_msg_t msg;
    msg.privacy_on = privacy_on;
    if (false == send_msg( (generic_msg_t *)&msg, (msg_type_t)PRIVACY_MODE_UPDATE, sizeof(privacy_mode_update_msg_t), get_msgq_name(), "q_nd_central", msg_idx++)) {
        LOG_E (TAG,"sending privacy mode to ndcentral failed");
        return false;
    }
    return true;
}

static bool send_speed_service_started_to_ndcentral()
{
    generic_msg_t msg;
    if (false == send_msg((generic_msg_t *)&msg, (msg_type_t)SPEED_SERVICE_STARTED, sizeof(generic_msg_t), get_msgq_name(), "q_nd_central", msg_idx++)) {
        LOG_E (TAG,"sending SPEED_SERVICE_STARTED to ndcentral failed");
        return false;
    }
    return true;
}
void read_config () {
    Config_parser bagh_conf(BAGHEERACONFIG_INI);
    bool val_overridden = true;
    string out_of_idle_soak_timeout_default_str;
    string invalid_speed_threshold_str;
    string invalid_accuracy_threshold_str;

    if (bagh_conf.getParseStatus()) {
        out_of_idle_soak_timeout_default_str = bagh_conf.getConfig("speed", "out_of_idle_soak_timeout", "",true, val_overridden);
        if (!out_of_idle_soak_timeout_default_str.empty() && !string_to_integer (out_of_idle_soak_timeout_default_str,out_of_idle_soak_timeout_default)) {
            LOG_E(TAG, "Failed to parse out_of_idle_soak_timeout from config, using default %d", out_of_idle_soak_timeout_default);
        }
        if (out_of_idle_soak_timeout_default < 5) {
            out_of_idle_soak_timeout_default = 5;
            LOG_I(TAG, "out_of_idle_soak_timeout adjusted to minimum value of 5");
        }

        invalid_speed_threshold_str = bagh_conf.getConfig("speed", "invalid_speed_threshold", "", true, val_overridden);
        if (!invalid_speed_threshold_str.empty() && !string_to_float(invalid_speed_threshold_str, invalid_speed_threshold)) {
            LOG_E(TAG, "Failed to parse invalid_speed_threshold from config, using default %f", invalid_speed_threshold);
        }

        invalid_accuracy_threshold_str = bagh_conf.getConfig("speed", "invalid_accuracy_threshold", "", true, val_overridden);
        if (!invalid_accuracy_threshold_str.empty() && !string_to_float(invalid_accuracy_threshold_str, invalid_accuracy_threshold)) {
            LOG_E(TAG, "Failed to parse invalid_accuracy_threshold from config, using default %f", invalid_accuracy_threshold);
        }
    }
}
int main() {
        nd_service_obj = NDService::get_service_obj(TAG);
        printf("initilizing logger\n");
        bool status_log = nd_log_init( log_dir.c_str() );
        if(status_log == false) {
                printf("unable to init logger :: Exiting from main");
                nd_service_obj->send_err_msg(SM_E_SPD_LOG_INIT_FAIL, NDService::UNUSED_ERR_AUX_CODE, 
                    "unable to init logger :: Exiting from main" );
                return 1;
        }

        #ifdef ROUTE_LOGS
        route_logs( log_dir.c_str() );
        #endif

	LOG_I(TAG, "starting...");

    if(read_lat_long_retention_duration() == false) {
        LOG_E(TAG, "Failed to read lat_long_retention_duration");
    }
    else{
        if((lat_long_retention_duration <= 0) || (lat_long_retention_duration > 600)) {
            lat_long_retention_duration = 5;
        LOG_I(TAG,"Out of bound value is found setting to default value %d", lat_long_retention_duration);
        }
    }
	init_msgq();

    /* Send a message to Bagheera service, so that if bagheera service missed registering for
     * speed privacy, because of speed service not started, it will do it at this point */
	send_speed_service_started_to_ndcentral();

    #ifdef AUTOMATION
    dta_enabled = isAutomationEnabled();
    #endif
    read_config();
#ifdef IGNITION_AUDIO_ALERT
    {
	//Locally Registering IDLE Event to SPEED from POWER_MON for 1 minute Intervals
	req_idle_reg_msg_t req;
	LOG_I (TAG,"Engine idle registration for POWER MON");
	req.speed = engine_idle_enable_speed;
	req.idle_secs = engine_idle_enable_duration;
	strncpy( req.client_id, string(POWER_MON_SERVICE).c_str() , sizeof(req.client_id) );
	req.reg_type = IDLE_REG_ENGINE;
	handle_idle_reg((generic_msg_t*)&req);
    }
#endif
    std::string ndmb_gps_client = "NDMB_SPEED_SERVICE";
    std::string ndmb_vbus_client = "NDMB_VBUS_SPEED_SERVICE";

    NDMBClient msg_client_gps(ndmb_gps_client);
    NDMBClient msg_client_vbus(ndmb_vbus_client);
    LOG_I(TAG, "subscribe for GPS data");
    msg_client_gps.subscribe(TOPIC_GPS_DATA, ndmb_gps_cb, 3000, 10);
    msg_client_vbus.subscribe(TOPIC_OBD_VEH_SPEED, ndmb_vbus_cb, 3000, 10);
    
    // Initialize and start timer for periodic speed processing
    speed_timer = new Timer();
    if(speed_timer == nullptr)
    {
        LOG_I(TAG, "Returning and restarting speed because of nullptr");
	return 0;
    }
    int timer_interval_sec = 1;
    LOG_I(TAG, "Registering speed processing timer (interval: %d sec)", timer_interval_sec);
    speed_timer->register_for_timer(one_second_tick, timer_interval_sec);
    speed_timer->startTimer();

    
    
    msg_loop();
    nd_service_obj->release_service_obj();
	return 0;
}

string get_msgq_name() {
	return Q_NAME;
}

int handle_speed_reg(generic_msg_t *msg) {
	req_speed_reg_msg_t *m = (req_speed_reg_msg_t *)msg;


	speed_reg_t reg;
	reg.handle = handles++;
	reg.client = m->client_id;
	reg.speed = m->speed;
	reg.contig_secs = m->contig_secs;
	reg.cnt = 0;
    reg.type = m->reg_type;
    size_t key = create_key(reg);

    auto it = speed_regs.find(key);

	if( reg.client == "" || reg.speed < 0 || reg.contig_secs < 0 ) {
        string str_msg = "Speed registration failed: " + reg.client + 
                    std::to_string(reg.speed) + std::to_string(reg.contig_secs);
        LOG_E(TAG, str_msg.c_str());
        nd_service_obj->send_err_msg(SM_E_SPD_REG_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
		return -1;
	}
    else if( it == speed_regs.end() )
    {
        // Ensure contig_secs meets minimum threshold to account for out-of-idle soak timeout
        reg.contig_secs = max(reg.contig_secs, out_of_idle_soak_timeout_default);
        speed_regs.emplace(key, reg);
        LOG_I(TAG, "Speed Registration With Key: %d", key);
        LOG_I(TAG, "Speed Registration Done: %s %d %d", reg.client.c_str(), reg.speed, reg.contig_secs,reg.type);
    }
    else if( it != speed_regs.end())
    {
        it->second.handle = reg.handle;
        LOG_I(TAG, "Duplicate Speed Registration With Key: %d", key);
        LOG_I(TAG, "Duplicate Speed Registration: %s %d %d", reg.client.c_str(), reg.speed, reg.contig_secs,reg.type);
        
    }
    else
    {
        LOG_E(TAG, "Error in finding String %d", reg.client);
    }
	return reg.handle;
}

int handle_speed_unreg(generic_msg_t *msg) {
	
    req_speed_unreg_msg_t *m = (req_speed_unreg_msg_t *)msg;  
  
    auto iter = speed_regs.begin(), end = speed_regs.end();  
    while (iter != end) {  
        if (iter->second.client == m->client_id && iter->second.handle == m->handle) {  
            // Client and Handle matching, erase item  
            speed_regs.erase(iter);  
            LOG_I(TAG, "Speed unregistration success: %s %d", m->client_id, m->handle);  
            return m->handle;  
        }  
  
        if ((iter->second.client == m->client_id) && (iter->second.client == "EXT_CAM")) {  
            speed_regs.erase(iter);  
            LOG_I(TAG, "Speed unregistration success: %s %d", m->client_id);  
            return m->handle;  
        }  
  
        ++iter;  
    }  
  
    string str_msg = "Speed unregistration failed: " + string(m->client_id) + std::to_string(m->handle);  
    LOG_E(TAG, str_msg.c_str());  
    nd_service_obj->send_err_msg(SM_E_SPD_UNREG_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg);  
    return -1;  

}

int handle_idle_reg(generic_msg_t *msg) {
    req_idle_reg_msg_t *m = (req_idle_reg_msg_t*)msg;


    idle_reg_t reg;
    reg.handle = handles++;
    reg.client = m->client_id;
    reg.speed = m->speed;
    reg.idle_secs = m->idle_secs;
    reg.type = m->reg_type;
    reg.cnt = 0; 
    reg.idle_sent = false;
    reg.periodic_idle_events = false;
    size_t key = create_key(reg);


    if( reg.client == POWER_MON_SERVICE )
    {
       reg.periodic_idle_events = true;
    }

    auto it = idle_regs.find(key);
    if( reg.client == "" || reg.idle_secs < 0 ) {
	    string str_msg = "idle registration failed: " +
		    reg.client + std::to_string(reg.speed) + std::to_string(reg.idle_secs);
	    LOG_E(TAG, str_msg.c_str());
	    nd_service_obj->send_err_msg(SM_E_SPD_IDLE_REG_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
	    return -1;
    }
    else if( it == idle_regs.end() )
    {
        idle_regs.emplace(key, reg);
        LOG_I(TAG, "IDLE Registration With Key: %d", key);
        LOG_I(TAG, "IDLE Registration Done: %s %d", reg.client.c_str(), reg.speed, reg.type);
    }
    else
    {
        // Duplicate registration - resend idle/out-of-idle state if previously sent
        bool idle_state = it->second.idle_on;
        it->second.handle = reg.handle;
        LOG_I(TAG, "Duplicate IDLE Registration With Key: %d", key);
  

        // Only send current idle state if idle or out-of-idle was previously sent
        //sending duplicate ack here to correct the sequence.
        send_handle_idle_reg_res(msg, reg.handle);
        res_idle_update_msg_t res_msg;
        res_msg.handle = reg.handle;
        res_msg.idle_on = idle_state;
        
        LOG_I(TAG, "Resending idle state (idle_on=%d) to duplicate registration for client %s", idle_state, reg.client.c_str());
        send_msg( (generic_msg_t *)&res_msg, RES_IDLE_UPDATE, sizeof(res_msg), 
                    get_msgq_name(), reg.client, msg_idx++ );
       
    }


	LOG_I(TAG, "idle registration done: %s %d mph %d s", reg.client.c_str(), reg.speed, reg.idle_secs);

	return reg.handle;

    

      
       
}

int handle_idle_unreg(generic_msg_t *msg) {

    req_idle_unreg_msg_t *m = (req_idle_unreg_msg_t *)msg;  
    auto iter = idle_regs.begin(), end = idle_regs.end();  

    while (iter != end) {  
        if (iter->second.client == m->client_id && iter->second.handle == m->handle) {  
            // Client and Handle matching, erase item  
            idle_regs.erase(iter);  
            LOG_I(TAG, "Speed unregistration success: %s %d", m->client_id, m->handle);  
            return m->handle;  
        }  

        ++iter;  
    }  
  
    string str_msg = "Speed unregistration failed: " + string(m->client_id) + std::to_string(m->handle);  
    LOG_E(TAG, str_msg.c_str());  
    nd_service_obj->send_err_msg(SM_E_SPD_UNREG_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg);  
    return -1; 

}

void send_handle_speed_reg_res(generic_msg_t *msg, int res) {
	req_speed_reg_msg_t *m = (req_speed_reg_msg_t *)msg;
	string dest = m->client_id;

	res_speed_reg_msg_t res_msg;

	res_msg.handle = res;
	if( res < 0 ) {
		res_msg.res = STATUS_ERR_OTHER;
	}
	else {
		res_msg.res = STATUS_OK;
	}
    res_msg.reg_type = m->reg_type;
    LOG_I (TAG,"send_handle_speed_reg_res type: %d",res_msg.reg_type);
	send_msg( (generic_msg_t *)&res_msg, RES_SPEED_REG, sizeof(res_msg), get_msgq_name(), dest, msg_idx++ );
}

void send_handle_speed_unreg_res(generic_msg_t *msg, int res) {
	req_speed_unreg_msg_t *m = (req_speed_unreg_msg_t *)msg;
	string dest = m->client_id;

	res_speed_unreg_msg_t res_msg;

	res_msg.handle = res;
	if( res >= 0 ) {
		res_msg.res = STATUS_OK;
	}
	else {
		res_msg.res = STATUS_ERR_OTHER;
	}

	send_msg( (generic_msg_t *)&res_msg, RES_SPEED_UNREG, sizeof(res_msg), get_msgq_name(), dest, msg_idx++ );
	
}

void send_handle_idle_reg_res(generic_msg_t *msg, int res) {
    req_idle_reg_msg_t *m = (req_idle_reg_msg_t*)msg; 
    string dest = m->client_id;
    
    res_idle_reg_msg_t res_msg;
    res_msg.handle = res;
    res_msg.reg_type = m->reg_type;
    if (res < 0)
    {
        res_msg.res = STATUS_ERR_OTHER;
    }
    else
    {
        res_msg.res = STATUS_OK;
    }
	send_msg( (generic_msg_t *)&res_msg, RES_IDLE_REG, sizeof(res_msg), get_msgq_name(), dest, msg_idx++ );
}

void send_handle_idle_unreg_res(generic_msg_t *msg, int res) {

	req_idle_unreg_msg_t *m = (req_idle_unreg_msg_t *)msg;
	string dest = m->client_id;

	res_idle_unreg_msg_t res_msg;

	res_msg.handle = res;
	if( res >= 0 ) {
		res_msg.res = STATUS_OK;
	}
	else {
		res_msg.res = STATUS_ERR_OTHER;
	}

	send_msg( (generic_msg_t *)&res_msg, RES_SPEED_UNREG, sizeof(res_msg), get_msgq_name(), dest, msg_idx++ );

}

void update_global_gps_data(gps_msg_t *m) {

    g_gps_lat = m->latitude;
    g_gps_lon = m->longitude;
    g_gps_alt = m->altitude;
    g_gps_bear = m->bearing;
    g_gps_speed = m->speed;

}

// Cache GPS data with monotonic timestamp
static void cache_gps_data(gps_msg_t *m) {
    
    int64_t now = get_system_monotonic_time();
    
    cached_gps.speed = m->speed;
    cached_gps.latitude = m->latitude;
    cached_gps.longitude = m->longitude;
    cached_gps.altitude = m->altitude;
    cached_gps.bearing = m->bearing;
    cached_gps.accuracy = m->accuracy;
    cached_gps.valid = m->valid;
    cached_gps.gps_timestamp = m->timestamp;
    cached_gps.monotonic_timestamp = now;
    
    if (m->valid) {
        latest_gps_time = now;
    }
    
    LOG_D(TAG, "GPS cached: speed=%.2f valid=%d mono_ts=%lld", m->speed, m->valid, now);
}

// One second timer tick callback
static void one_second_tick(void) {
    LOG_D(TAG, "One second tick - processing speed data");
    gps_msg_t gps_msg;

    // Create gps_msg_t from cached GPS data
    {
        std::lock_guard<std::mutex> lock(gps_cache_mutex);

        
            gps_msg.speed = cached_gps.speed;
            gps_msg.latitude = cached_gps.latitude;
            gps_msg.longitude = cached_gps.longitude;
            gps_msg.altitude = cached_gps.altitude;
            gps_msg.bearing = cached_gps.bearing;
            gps_msg.accuracy = cached_gps.accuracy;
            gps_msg.valid = cached_gps.valid;
            gps_msg.timestamp = cached_gps.gps_timestamp;
        
        if(cached_gps.monotonic_timestamp == 0) {
            LOG_D(TAG, "No GPS data cached yet");
            gps_msg.valid = false;
            gps_msg.speed = 0.0;

        }
        else if((get_system_monotonic_time() - cached_gps.monotonic_timestamp) > 2000) {
            LOG_D(TAG, "Cached GPS data is stale");
            gps_msg.valid = false;
            gps_msg.speed = 0.0;
        }
    }
    // Process speed and idle checks
    handle_gps_update(&gps_msg);
}

void handle_gps_update(gps_msg_t *m)
{

    update_global_gps_data(m);
    #ifdef AUTOMATION
    if (dta_enabled) {
        // if file is present over ride m->speed and m->valid with the speed in file.
        // else consider exiting values.
        constexpr const char * filePath = "/dev/shm/SPEED";
        std::ifstream inputFile(filePath);

        if(inputFile.is_open() && inputFile.good()) {
            string line;
            if(getline(inputFile, line)) {
                m->speed = stof(line);
                m->valid = true;
            }
        }
    }
    #endif
    if(!m->valid) {
        int64_t current_time = get_system_monotonic_time();
        if((current_time - vbus_speed_timestamp.load()) < 2000)  // 2 seconds freshness check (milliseconds)
        {
            LOG_I(TAG, "VBUS speed: %lld timestamp: %lld current_time: %lld delta : %lld", vbus_speed.load(), vbus_speed_timestamp.load(), current_time, (current_time - vbus_speed_timestamp.load()));
            m->speed = static_cast<float>(vbus_speed.load());
            m->valid = true; //setting psedu valid flag as below algo wont be triggered incase of invalid gps.
			m->accuracy = 3; //safety-check to setdummy accuracy for the below algo to work incase random value comes from gps service
        }
    }
    float speed = m->speed;
    bool valid = m->valid;
    // Considering speed as 0 when accuracy is bad and speed is low OR ignition is off (regardless of accuracy and speed)
    if((m->accuracy >= invalid_accuracy_threshold && m->speed <= invalid_speed_threshold) || (!ignition_status.load())) {
        LOG_I(TAG, "GPS Update: raw_speed: %f accuracy: %f timestamp: %lld ignition_status: %d", m->speed, m->accuracy, m->timestamp, ignition_status.load());
        speed = 0.0;
    }
    if(valid)
    {
        ofstream speed_info_fd(speed_info_file, ofstream::binary);
        LOG_I(TAG, "Speed = %f" , speed);
        speed_info_fd << speed << endl;
        speed_info_fd.close();
    }
    for( map<size_t, speed_reg_t>::iterator iter=speed_regs.begin(), end = speed_regs.end(); iter != end; iter++ ) {
        if( valid && (speed > iter->second.speed) ) {
            iter->second.cnt++;
            if( iter->second.cnt >= iter->second.contig_secs ) {
                LOG_I(TAG, "Speed Limit hit");
                res_speed_update_msg_t res_msg;
                res_msg.handle = iter->second.handle;
                res_msg.speed = speed;
                g_privacy_ndc = false;
                send_msg( (generic_msg_t *)&res_msg, RES_SPEED_UPDATE, sizeof(res_msg),
                         get_msgq_name(), iter->second.client, msg_idx++ );
                if(iter->second.client == GPS_SERVICE) {
                    send_privacy_mode_to_ndcentral(g_privacy_ndc);
                }
            }
        }
        else {
            iter->second.cnt = 0;
        }
    }

    for( map<size_t, idle_reg_t>::iterator iter=idle_regs.begin(), end = idle_regs.end(); iter != end; iter++ ) {
        if( !valid || (speed <= iter->second.speed) ) {
            iter->second.cnt++;
            iter->second.out_of_idle_cnt = 0;
            iter->second.out_of_idle_soak_timeout = out_of_idle_soak_timeout_default;
	        LOG_D(TAG, "handle_gps_update() speed: %f valid: %d , iter->cnt: %d, iter->client: %s", m->speed, m->valid, iter->second.cnt, iter->second.client.c_str());
            if( ((iter->second.client == GPS_SERVICE) || (iter->second.idle_sent == false) || (iter->second.periodic_idle_events == true)) && (iter->second.cnt >= iter->second.idle_secs) ) {
                LOG_I(TAG, "Vehicle idle detected for client %s", iter->second.client.c_str());
                if("q_nd_central" == iter->second.client){
                    g_privacy_ndc = true;
                }
                iter->second.idle_on = true;
                res_idle_update_msg_t res_msg;
                res_msg.handle = iter->second.handle;
                res_msg.idle_on = true;

                send_msg( (generic_msg_t *)&res_msg, RES_IDLE_UPDATE, sizeof(res_msg), 
                            get_msgq_name(), iter->second.client, msg_idx++ );
                if(iter->second.client == GPS_SERVICE) {
                    send_privacy_mode_to_ndcentral(g_privacy_ndc);
                }
                iter->second.idle_sent = true;
            }
            // If Idling and Idle was sent already, reset the idle count so that we can again report the next idle event if periodic_idle_events is configured.
            if (((iter->second.client == GPS_SERVICE) || (iter->second.periodic_idle_events == true)) && (iter->second.cnt >= iter->second.idle_secs))
            {
	            iter->second.cnt = 0;
	        }
        }
        else {
            iter->second.out_of_idle_cnt++;
            LOG_D(TAG, "handle_gps_update() speed: %f valid: %d , iter->out_of_idle_cnt: %d", m->speed, m->valid, iter->second.out_of_idle_cnt);
            LOG_I(TAG, "Out if Idle Count is %d", iter->second.out_of_idle_soak_timeout);
            if ( (speed > iter->second.speed) && (iter->second.out_of_idle_soak_timeout <= 0) ) {
                iter->second.cnt = 0;
                if (iter->second.idle_sent) {
                    LOG_I(TAG, "Vehicle 'out of idle' detected %s", iter->second.client.c_str());
                    res_idle_update_msg_t res_msg;
                    res_msg.handle = iter->second.handle;
                    res_msg.idle_on = false;
                    iter->second.idle_on = false;

                    send_msg( (generic_msg_t *)&res_msg, RES_IDLE_UPDATE, sizeof(res_msg), 
                             get_msgq_name(), iter->second.client, msg_idx++ );
                    iter->second.idle_sent = false;
                }
                LOG_I(TAG, "Resetting idle_soak_timeout to Default");
                iter->second.out_of_idle_soak_timeout = out_of_idle_soak_timeout_default;
            }
            else
            {
                LOG_I(TAG, "Decrementing Out Of Idle Soak Count");
                iter->second.out_of_idle_soak_timeout--;
            }
        }
    }
}

#ifdef UNIT_TEST
float test_speed = 0;

void send_reg() {
	req_speed_reg_msg_t m;
	m.speed = 50;
	m.contig_secs = 5;

	send_msg( (generic_msg_t *)&m, REQ_SPEED_REG, sizeof(m), get_msgq_name(), get_msgq_name(), msg_idx++ );
}

void send_unreg() {
	req_speed_unreg_msg_t m;
	m.handle = 0;

	send_msg( (generic_msg_t *)&m, REQ_SPEED_UNREG, sizeof(m), get_msgq_name(), get_msgq_name(), msg_idx++ );

}

void *test_thread(void *args) {

    sleep(5);
    while(1) {
        sleep(1);
        res_gps_update_msg_t m;
        m.handle = 0;
        m.lat = 91.0;
        m.lon = 181.0;
        m.alt = 500.0;  
        m.speed = test_speed;
        m.bear = 0.0;
        m.valid = true;
        m.timestamp = get_system_time();
        m.system_timestamp = get_system_time();
        LOG_I(TAG, "sending GPS update with speed %f", test_speed);
        send_msg( (generic_msg_t *)&m, RES_GPS_UPDATE, sizeof(m), "q_nd_central", "SPEED", msg_idx++ );
    }
}

void * input_thread_func(void* arg)
{
    string o;
    cout <<"test thread"<<endl;
    while(1)
    {
        sleep(5);

        test_speed = 50;
        cout <<"enter option:"<<endl;
        cin >> o;

        if (o == "s")
        {
            test_speed = 20;
        }
        else if (o == "i")
        {
            test_speed = 0;
        }
    }
}

void unit_test() {

    pthread_t test_th;
    if (!pthread_create (&test_th, NULL, test_thread, NULL)) {
        LOG_I (TAG, "Test Thread created for  gps data check");
    } else {
        LOG_E(TAG, "cannot create test thread for gps data");
    }

    pthread_t input_th;
    if (!pthread_create (&input_th, NULL, input_thread_func, NULL)) {
        LOG_I (TAG, "Input thread created for test");
    } else {
        LOG_E(TAG, "cannot create input thread");
    }

	//send_reg();
	//send_unreg();
}

#endif
