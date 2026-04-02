/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Y Suresh Kumar <suresh.kumar@netradyne.com>, April 2017
 */

#include <cstdlib>
#include <unistd.h>
#include <chrono>             // std::chrono::seconds
#include <mutex>              // std::mutex, std::unique_lock
#include <condition_variable> // std::condition_variable, std::cv_status
#include <sys/sysinfo.h> 
#include <nd_msg_utils.h> 
#include <nd_msg_types.h>
#include <sys/time.h>
#include <errno.h>
#include <iomanip>

#include <stdlib.h>
#include <fcntl.h>    /* For O_RDWR */
#include <unistd.h>   /* For open(), creat() */
#include <sys/sysinfo.h>
#include <data_recording.h>
#include "power_monitor.h"
#include "nd_time.h"
#include <nd_task.h>
#include <svc.h>
#include <system_utils.h>
#include <nd_file_utils.h>
#include <nd_ext_cam_utils.h>
#include <syslog.h>
#include "service_utils.h"
#include <ndmb/nd_msg_interface.h>
#include <ndmb/nd_mbserver.h>

#include <condition_variable>
#include <nd_factory.h>
#include "nd_auth_utils.h"
#include "wake_up_reason.h"
#include <sys/resource.h>
#include <sys/syscall.h>
#include "nd_tinyalsa.h"

#include "power_monitor_obd.h"

#ifdef KRAIT
#include "nd_msp_utils.h"
#endif

#ifdef IGNITION_AUDIO_ALERT
#include "audio.pb.h"
#include <zmq.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef KRAIT
#include "msp_api.h"
#endif

#ifdef BAGHEERA2
#include "adc_api.h"
#include "sys_info.h"
#endif

#ifdef __cplusplus
}
#endif
#include <ndmb/nd_mbclient.h>
//nd service object, to detect critical Errors which will be send to Health stats and cloud
NDService *nd_service_obj = NULL; 
//nd_device_obj for factory class api
ND_DeviceFactory *nd_device_obj = NULL;

#define ROUTE_LOGS

#ifdef IGNITION_BROADCAST
       NDMBServer server(SERVICE_PM);
#endif


using namespace std;
static string PM_DB_PATH_NAME = "";

bool get_data_record_status_db(data_record_status_db &data_db);
static const char *TAG="PWR";
static const string Q_BTFV = "BTFV";
static const string Q_NDCENTRAL = "q_nd_central";
static const string Q_POWERMON = "q_power_monitor";
static const string Q_EXT_CAM = "EXT_CAM";
static const string Q_UPL = "UniUpload";
static const string Q_WIFI = "WIFI_MGR";
static const string Q_OBD = "OBD_PUB";

static const string Q_DIAG = "DIAGNOSTIC";
static const string Q_SPD = "SPEED";
static const string IGNITION_CLIENTS[] = {Q_NDCENTRAL, Q_BTFV, Q_EXT_CAM, Q_UPL, Q_WIFI, Q_OBD, Q_SPD};
static const int NUM_IGNITION_CLIENTS = sizeof (IGNITION_CLIENTS)/sizeof (std::string);
volatile unsigned int all_thread_keepalive_status = 0x0;  
// lsb to msb bit => bit 1 - main thread, bit 2 - direct_poling_thread_fn, bit 3 - shutdown_poling_thread_fn 
pthread_mutex_t keepalive_status_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t supercap_status_mutex = PTHREAD_MUTEX_INITIALIZER;
power_healthstats_msg_t power_health;

static const int ONE_YEAR_IN_MINUTES = 525600; // (365*24*60 = 525600 minutes)
static const int ONE_DAY_IN_SECONDS = 86400;   // (24*60*60 = 86400 seconds)
static const int MAX_ka_retry_cnt = 6;
int ignition_status = 0;
bool is_ignition_status_update = false;
bool ignore_SVC_reboot = false;
static int delay_reboot_time = 0;
static const int default_delay_reboot_time = 900; // seconds
static int max_B2B_reboot_allowed = 0;
static const int default_max_B2B_reboot_allowed = 10; // count
bool vehicle_data_enabled = false;

// default value for safety time to sync driveri dhub to wakeup before DHUB for sync
static const int default_safety_time_to_sync_driveri_dhub_secs = 60; // seconds, Setting default value to 60 sec, So atleast 1 time dhub status check will be done

// default value for safety time to wakeup when bad voltage shutdown occurs
static const int default_safety_wakeup_time_for_bad_voltage_shutdown = ONE_YEAR_IN_MINUTES;

const unsigned int DEFAULT_RTC_TIME_MAX_LPW = (ONE_YEAR_IN_MINUTES * SECS_IN_A_MIN); ; // seconds. to make sure we don't wakeup in low power due to RTC in case of max lpw count

// default value for wakeup duration when device come up before actual LPW
static const int default_misc_wakeup_duration = 3; // minutes

// To store updated wakeup time of dhub.
int64_t g_dhub_wakeup_time = 0;

// To check apm motion detection is enabled or not
static bool apm_motion_detection = false;

// default value for record on crank low wakeup duration
static int default_non_lpm_crank_low_wakeup_duration = 6; // minutes
static const string default_wake_on_motion_imu_non_lpm = "false";
static const string default_wake_on_motion_aon_non_lpm = "false";
static const string default_wake_on_ign_non_lpm = "false";
static const string default_wake_on_misc_non_lpm = "false";

pthread_mutex_t m_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t m_cond = PTHREAD_COND_INITIALIZER;
pthread_condattr_t m_attr;

static const int DEF_VEHICLE_IDLE_TIME_IN_SEC =  180;// from apm config for motion detection
static const int VEHICLE_STATIONARY_DETECT_DELAY = 200;//NUM_SAD_CHUNKS * motion_detect_interval * 1000; // 200 seconds delay
static const int NUM_AVG_SPEED_SAMPLES = 10; // last 10 samples for average speed calculation
static const double MIN_GPS_ACCURACY = 10.0;

constexpr int SHUTDOWN_TIME_FOR_BAD_BATTERY_VOLTAGE = 60; // seconds

int freq_lpw_cycles[12];

string deviceid = "";
string devicetype = "";
string otaversion = "";
string cloud_server = DEF_INI_SERVER; 
string server_url = DEF_INI_SERVER_URL; 
string version = DEF_INI_API_VERSION;

#ifdef IGNITION_AUDIO_ALERT

#define MAX_PUBLISHER_Q_SZ 16

const string ignition_audio_check = "/dev/shm/ignition_audio_played";
const string ignition_audio_fail = "/home/ubuntu/ignition_audio_failed";
static int64_t previous_ignition_on_idle_update_timestamp;

#endif

const string prev_shutdown_speed_file = "/home/ubuntu/.nddevice/previous_speed.info";
float prev_speed;
bool reported_possible_delay_in_shutdown = false;
int default_max_postpone_shutdown_time_uploader_activity = 45; // minutes
res_pend_uploader_data_upload_msg_t* uploader_data_upload_pend;
gps_health_data_t gps_health_updates[NUM_GPS_UPDATES_PER_MINUTE];  
int gps_health_index = 0; 

#define ND_MAX(A,B) (A) > (B) ? (A) : (B)
#define ND_MIN(A,B) (A) < (B) ? (A) : (B)

#define CRANK_LEVEL_HIGH '1'
#define CRANK_LEVEL_LOW  '0'
power_monitor_ctx *POWER_MONITOR_ctx = power_monitor_ctx::get_power_monitor( "__power_monitor__" );
bool ndmb_gps_cb(ndmb_generic_msg_t *msg);
float get_last_average_speed(int num_samples);
int64_t rtc_wakeup_time = 0;
int dhub_status_check_count = 0;
// As we noticed that on each boot up DHUB time is increased by 20-25 seconds.
const int DEFAULT_TIME_DHUB_WAKEUP_SYNC = 26; // seconds
// As we noticed that DHUB is taking extra 0 sec to shutdown compare to device
const int DEFAULT_TIME_DHUB_SHUTDOWN_SYNC = 0; // seconds
bool manual_ignition_cb_done = false;
int64_t default_bootup_time_by_device = 0;
int dhub_wakeup_sync_extra = DEFAULT_TIME_DHUB_WAKEUP_SYNC;
int dhub_shutdown_sync_extra = DEFAULT_TIME_DHUB_SHUTDOWN_SYNC;
// This flag is used to monitor wakeup time of dhub in case ign_gpio:OFF, imu_ign:ON and gps_ign:ON.
// So power monitor is still in crank high state.
// if device pass wakeup time of dhub then using this flag we can monitor the wakeup time of dhub and update next wakeup time of dhub in DB.
bool monitor_dhub_wakeup_time = false;
// Reset reason file will truncated after 2 mins of boot time
static const string reset_reason_file_path = "/home/ubuntu/.nddevice/reset_reason.txt";
static const int TIME_TO_TRUCATE_BAGHEERA2_RESET_REASON_FILE = 120; // seconds
static bool is_suspend_reset_reason_file_truncated = false;


// This flag is used to indicate during monitoring dhub wakeup time, if dhub wakeup time is updated or not.
bool update_dhub_wakeup_time_for_extended_shutdown = false;

// max retry count for setting RTC
#define RTC_SET_MAX_RETRY 3
// Params to change nice value for KA minified thread
constexpr int DECREASE_SUPERCAP_THREAD_NICE_VALUE_BY = 5;
constexpr int MIN_NICE_VALUE_THREAD = -15;
// Voltage limit for KA minified range check
static const float KA_MINIFIED_MIN_VOLTAGE = MIN_VALID_VOLTAGE; // 07.50 volts
static const float KA_MINIFIED_MAX_VOLTAGE = MAX_VALID_VOLTAGE; // 35.0 volts

constexpr int64_t AUDIO_PLAY_THRESHOLD = 25; // seconds

power_monitor_lpw_data_t lpw_data = {}; // low power wakeup data

constexpr float INVALID_VOLT_THRESHOLD = 100.0; // volts, which indicate voltage read failure from OBD

constexpr int MIN_ALLOWED_CRANK_CHANGES = 1;
constexpr int MAX_ALLOWED_CRANK_CHANGES = 300;
constexpr int MIN_CRANK_EVENTS_TO_DEBOUNCE = 1;
constexpr int MAX_CRANK_EVENTS_TO_DEBOUNCE = 15;
constexpr int DEF_ALLOWED_CRANK_CHANGES = 30; // Count diff b/w prev high/low crank count to current high/low crank count
constexpr int DEF_CRANK_EVENTS_TO_DEBOUNCE = 12; // Max crank events to consider for debounce/toggling check. 1 -> 30 sec * 6 = 180 sec total debounce time.

static int max_allowed_crank_changes = DEF_ALLOWED_CRANK_CHANGES;
static int max_crank_events_to_debounce = DEF_CRANK_EVENTS_TO_DEBOUNCE;
power_monitor_ctx::power_monitor_ctx( string name )
{
    this->name = name;
}

power_monitor_ctx::~power_monitor_ctx( )
{
}

// #define UNIT_TEST

#ifdef UNIT_TEST
void test_driveri_audio();
#endif


void monitor_engine_status(int ignition_status, float adc_value);
bool check_volt_mon_configuration();
int64_t time_taken_to_bootup_device();

static void set_rtc_time_limited_system_reboot();
float get_prev_voltage();

void applyPowerState(PowerStateEvent event, bool status) {
    const DevicePowerState cpusched_obj(event, status);
    POWER_MONITOR_ctx->ps_obj.setPowerState(cpusched_obj);
}

power_monitor_ctx* power_monitor_ctx::get_power_monitor( string name )
{
    power_monitor_ctx* power_monitor = new power_monitor_ctx( name );

    if(power_monitor == NULL)
        return NULL;
    return power_monitor;
}

power_crank_levels_t power_monitor_ctx::crank_level()
{
    return nd_device_obj->get_crank_level();
}

bool power_monitor_ctx::create_table_db(db_handle_t* db_handle)
{
    if(db_handle == NULL){
        LOG_E(TAG, "create_table_db: db_handle == NULL; returning");
        return false;
    }
    char *zErrMsg = 0;
    int  rc;

    pthread_mutex_lock(&db_handle_mutex);
    rc = sqlite3_exec(db_handle, table_formatter.c_str(), NULL, 0, &zErrMsg);
    pthread_mutex_unlock(&db_handle_mutex);
    //// parse the error message
    if( rc != SQLITE_OK ){
        if(strstr(zErrMsg, "already exists") != NULL){
            LOG_I(TAG, "SQL error@ %s", zErrMsg);
            LOG_I(TAG, "Ignoring error since table already exists");
            sqlite3_free(zErrMsg);
            return true;
        }
        LOG_E(TAG, "SQL error@ %s", zErrMsg);
        sqlite3_free(zErrMsg);
        return false;
    }else{
      LOG_I(TAG, "Success in create_table_db");
      return true;
    }
}

bool power_monitor_ctx::exec_cmd_db(db_handle_t* db_handle, const string command, 
        int (*callback)(void*,int,char**,char**), void* cb_data)
{
    if(db_handle == NULL){
        LOG_E(TAG, "exec_cmd_db: db_handle == NULL; returning");
        return false;
    }
    int rc;
    LOG_I(TAG, "command ::%s::", command.c_str());
    pthread_mutex_lock(&db_handle_mutex);
    char *errmsgs = 0;
    rc = sqlite3_exec(db_handle, command.c_str(), callback, cb_data, &errmsgs);
    pthread_mutex_unlock(&db_handle_mutex);
    if( rc != SQLITE_OK ){
      LOG_E(TAG, "SQL error: %s", errmsgs);
      sqlite3_free(errmsgs);
      return false;
    } else {
      LOG_I(TAG, "Success in exec_cmd_db");
    }
    return true;
}

bool power_monitor_ctx::open_db(string db_file, db_handle_t** db_handle)
{
   char *zErrMsg = 0;
   int rc;

   rc = sqlite3_open(db_file.c_str(), db_handle);
   if( rc ){
      LOG_E(TAG, "Can't open database: %s", sqlite3_errmsg(*db_handle));
      *db_handle = NULL;
      return false;
   }

   rc = sqlite3_busy_timeout((*db_handle), SQLITE3_BUSY_TIMEOUT); //setting timeout
   if (rc){
       LOG_E(TAG, "Unable to set timeout for db. Timeout is set to default value = 0");
   }else{
       LOG_I(TAG, "Set db timeout to %dms", SQLITE3_BUSY_TIMEOUT);
   }
   return true;
}

static bool read_boot_time_tt(void *args) {
    read_boot_time(POWER_MONITOR_ctx->boot_time);
    return true;
}

#ifdef IGNITION_AUDIO_ALERT

bool send_audio_req_to_ndcentral(string audio_fname, AudioEventType audio_type) {

    // Check data_record_disable_file present or not
    if(true == file_is_present(data_record_disable_file)) {
        LOG_I(TAG, "Data recording is disabled, Ignition audio alert is disabled");
        return true;
    }

    driver_login_audio_notify_msg_t audio_alert_msg = {};
    ssize_t audio_file_length = (audio_fname.length() > FNAME_LEN + 1) ? audio_fname.length() + 1 : FNAME_LEN;
    nd_strncpy(audio_alert_msg.file, audio_fname.c_str(), audio_file_length);
    audio_alert_msg.alert_type = static_cast<uint32_t>(audio_type);

    const unsigned int MAX_SEND_COUNT = 3; // max retry count
    unsigned int send_count = 0;
    bool status = false;

    LOG_I(TAG, "send_audio_req_to_ndcentral for file %s", audio_alert_msg.file);

    // Store the audio request in the vector
    add_audio_request(audio_alert_msg.file, audio_type);

    do {
        if (false == send_msg ((generic_msg_t*)&audio_alert_msg, DRIVER_LOGIN_AUDIO_NOTIFY, sizeof (driver_login_audio_notify_msg_t), Q_POWERMON, Q_NDCENTRAL, 0)) {
            LOG_E(TAG, "send_audio_req_to_ndcentral failed for file %s", audio_alert_msg.file);
            status = false;
        }
        else {
            status = true;
            LOG_I(TAG, "send_audio_req_to_ndcentral success for file %s", audio_alert_msg.file);
        }

    } while((status != true) && (++send_count < MAX_SEND_COUNT));

    if( (false == status) && (send_count >= MAX_SEND_COUNT) ) {
        update_audio_request_played_status(audio_alert_msg.file, audio_type, false);
        status = true; // Set status to true, as we are playing audio directly.
    }
    return status;
}

int ignition_on_audio_alert() {
    LOG_D(TAG, "lastIgnitionOnAudioAlertTime %lld", POWER_MONITOR_ctx->lastIgnitionOnAudioAlertTime);
    if( (POWER_MONITOR_ctx->crank_level() == CRANK_HIGH) && (true == POWER_MONITOR_ctx->ignitionOnAudioAlert)) {
        int64_t currentTime = get_system_monotonic_time()/1000; // in seconds.
         LOG_I(TAG, "ignition_on_audio_alert - Current Time = %lld lastIgnitionOnAudioAlertTime = %lld", currentTime, POWER_MONITOR_ctx->lastIgnitionOnAudioAlertTime);
        /* During Start Up of Service the value of lastIgnitionOnAudioAlertTime will be -1. We shouldn't drop the first audio alert message, so just send it without any further checks. */
        /* If the interval between lastIgnitionOnAlertTime and the currentTime is greater than the ignitionOnAudioAlertInterval(seconds): to handle toggling/faulty ignition,
           then send a message to play audio and update the lastIgnitionOnAlertTime to the currentTime,
           else drop the event and defer updation of lastIgnitionOnAlertTime */
        LOG_I(TAG, "currentTime %lld", currentTime);
        if((DEFAULT_IGNITION_ALERT_TIME == POWER_MONITOR_ctx->lastIgnitionOnAudioAlertTime) ||
            ((currentTime - POWER_MONITOR_ctx->lastIgnitionOnAudioAlertTime) >= POWER_MONITOR_ctx->ignitionOnAudioAlertInterval)) {

            bool ignition_audio_played = file_is_present(ignition_audio_check);
            bool ignition_audio_for_non_ignition_events = file_is_present(ignition_audio_fail);

            if(!ignition_audio_played)
            {
                if(!ignition_audio_for_non_ignition_events)
                {
                    if(true == send_audio_req_to_ndcentral(POWER_MONITOR_ctx->ignitionOnAudioAlertFile, AudioEventType::IgnAl)) {
                        POWER_MONITOR_ctx->lastIgnitionOnAudioAlertTime = get_system_monotonic_time()/1000; // in seconds
                        LOG_I(TAG, " IGNITION_ON_AUDIO_ALERT  DONE!!!!");
                        LOG_I(TAG, "lastIgnitionOnAudioAlertTime %lld", POWER_MONITOR_ctx->lastIgnitionOnAudioAlertTime);

                        // Creating a file to indicate that the audio alert has been played
                        file_touch(ignition_audio_check);
                    }
                    else {
                        LOG_E(TAG, " IGNITION_ON_AUDIO_ALERT  !!!!FAILED!!!!");
                    }
                }
                else {
                    LOG_I(TAG, "IGNITION_ON_AUDIO_ALERT Failed Because Of Non-Ignition Event");
                    file_delete(ignition_audio_fail);
                }
            }
            else {
                LOG_D(TAG, " IGNITION_ON_AUDIO_ALERT  DROPPED!!!!");
            }
        }
    }
    else {
	    LOG_D(TAG, " IGNITION_ON_AUDIO_ALERT  !!!!DISABLED!!!!");
    }
    return 1;
}

int ignition_on_idle_audio_alert()
{
    bool playIdleAudioAlert = true;
    int64_t current_ignition_on_idle_update_timestamp = get_system_monotonic_time();
    int64_t time_diff = current_ignition_on_idle_update_timestamp - previous_ignition_on_idle_update_timestamp;

    /*Stopping recursive idle audio play calls which are less than 1 Minute duration.
      Fine tuning the interval as we are getting ignition_on_idle_audio_alert once in 28-33 seconds range
    */
    if( time_diff < 40*1000) {
        LOG_I(TAG, "Time diff b/w two ignition_on_idle_audio_alert %d is less than 40 seconds, so dropping the event", time_diff);
        return 1;
    }

    LOG_I(TAG, "Time Diff = %lld", time_diff);

    previous_ignition_on_idle_update_timestamp = current_ignition_on_idle_update_timestamp;
    POWER_MONITOR_ctx->ignitionOnIdleAudioAlertDuration++;
    LOG_I(TAG, "ignition_on_idle_audio_alert count = %d, Time Diff = %lld", POWER_MONITOR_ctx->ignitionOnIdleAudioAlertDuration, time_diff);


    if (POWER_MONITOR_ctx->ignitionOnIdleAudioAlertDuration >= POWER_MONITOR_ctx->ignitionOnIdleAudioAlertThresholdDuration)
    {
	// Reset the Count.
	POWER_MONITOR_ctx->ignitionOnIdleAudioAlertDuration = 0;


        if( (POWER_MONITOR_ctx->ignitionOnIdleAudioAlertThresholdFrequency == DEFAULT_IGNITION_IDLE_ALERT_FREQUENCY ) ||
                (POWER_MONITOR_ctx->ignitionOnIdleAudioAlertFrequency < POWER_MONITOR_ctx->ignitionOnIdleAudioAlertThresholdFrequency) )
        {
            if(playIdleAudioAlert)
            {
                int64_t currentTime = get_system_monotonic_time()/1000; // in seconds.
                LOG_I(TAG, "Play Idle Audio Alert = %d, currentTime = %lld", playIdleAudioAlert, currentTime);

                if(true == send_audio_req_to_ndcentral(POWER_MONITOR_ctx->ignitionOnIdleAudioAlertFile, AudioEventType::IgnIdleAl))
                {
                    LOG_I(TAG, " IGNITION_ON_IDLE_AUDIO_ALERT  DONE!!!!");
                }
                else
                {
                    LOG_E(TAG, " IGNITION_ON_IDLE_AUDIO_ALERT  !!!!FAILED!!!!");
                }
            }
         }
		// Increment the frequency count
		POWER_MONITOR_ctx->ignitionOnIdleAudioAlertFrequency++;
    }

	return 1;
}

// bool driveri_app_login_audio_play (string fname) {

//     if (file_is_present(fname)) {

//         if (send_audio_play(fname))
//         {
//             LOG_I(TAG, "%s : PLAYED", fname.c_str());
//         }
//         else
//         {
//             LOG_E(TAG, "%s : FAILED", fname.c_str());
//             return false;
//         }
//     } else {
//         LOG_E(TAG, "%s : NOT PRESENT", fname.c_str());
//         return false;
//     }
//     return true;
// }

#endif

#ifdef BAGHEERA2
bool writetogpio_edgefile(string edges)
{
    string command;
    FILE *fp;
    char buffer[128] = {0};

    command = "echo " + edges + " > " + nd_device_obj->gpio_crank_edge_info_file() + " 2>&1";

    LOG_I(TAG, "command ::%s::", command.c_str());
    fp = popen(command.c_str(), "r");
    if (fp == NULL) {
        LOG_E(TAG, "Failed to execute command in  writetogpio_edgefile :: %s" ,
                 command.c_str() );
        return false;
    }
    while (fgets(buffer, sizeof(buffer)-1, fp) != NULL) {
        if(strlen(buffer) <= 1)
            break;
        buffer[strlen(buffer)-1] = 0;
        string message(buffer);
        LOG_E(TAG, "message :: %s",message.c_str());
        pclose(fp);
        return false;
    }

    pclose(fp);
    return true;
}
#endif

// returns true if crossed specified limits of uptime
// flase else
bool check_uptime(int &time_diff, int fold_val)
{
    LOG_I(TAG, "Inside check_uptime");
    // if crank is low we are deactivating check_uptime/cyclic reboot
    if(POWER_MONITOR_ctx->crank_level() != CRANK_HIGH) {
        LOG_I(TAG, "low crank level; check_uptime is deactivated");
        return false;
    }
    // Dont activate cyclic reboot for fisrt 15 seconds
    // every powermonitor restart will get a crank high message
    if( (get_system_monotonic_time() - POWER_MONITOR_ctx->service_start_time_mono) < 15*1000) {
        LOG_I(TAG, "Disabling check_uptime because PM started a while back %lld",
             (get_system_monotonic_time() - POWER_MONITOR_ctx->service_start_time_mono) );
        return false;
    }

    if(!nd_device_obj->is_64_bit()) {
        struct sysinfo uptime;
        if(sysinfo(&uptime)) {
            LOG_E(TAG, "sysinfo failed");
            return false;
        }
        int uptime_secs = uptime.uptime;
        LOG_I(TAG, "check_uptime max_uptime %d uptime_secs %d", POWER_MONITOR_ctx->max_uptime_secs*fold_val, uptime_secs);
        if(uptime_secs > POWER_MONITOR_ctx->max_uptime_secs*fold_val){
            LOG_I(TAG, "reached max time; time to reboot");
            time_diff = (int)uptime_secs - POWER_MONITOR_ctx->max_uptime_secs;
            return true;
        }
    } else {
        int64_t uptime_secs = (get_system_monotonic_time() ) / 1000;
        LOG_I(TAG, "check_uptime max_uptime %d uptime_secs %lld", POWER_MONITOR_ctx->max_uptime_secs*fold_val, uptime_secs);
        if(uptime_secs > POWER_MONITOR_ctx->max_uptime_secs*fold_val){
            LOG_I(TAG, "reached max time; time to reboot");
            time_diff = (int64_t)uptime_secs - POWER_MONITOR_ctx->max_uptime_secs;
            return true;
        }

    }
    LOG_I(TAG, "not yet reached max time");
    return false;
}

bool read_device_connected_battery_voltage(float &battery_volt, float &curr_volt)
{
    if(true == nd_factory_utils::is_obd_volt_supported()) {
        float adc_val = 0.0;
        int adc_state = INVALID_ADC_STATE;
        static bool ndmb_read_failed = false;

        // read voltage
        if(false == read_adc_status_and_channel_two_data(adc_val, adc_state)) {

            float voltage = nd_device_obj->get_voltage_value(eCRANK_VOLT);

            if(false == ndmb_read_failed) {
                std::ostringstream ems;
                ems << std::fixed << std::setprecision(2);
                ems << "OBD_NDMB_FAIL: ADC_STAT:" << adc_state << ", ADC_VAL:" << adc_val << ", SYSFS_VOLT:" << voltage;
                LOG_E(TAG, "%s", ems.str().c_str());
                int aux_code = static_cast<int>(voltage);
                nd_service_obj->send_err_msg(SM_E_PM_VOLTAGE_READ_FAIL, aux_code, ems.str() );
                ndmb_read_failed = true;
            }

            adc_val = voltage;
        }
        else {
            // reset the flag.
            ndmb_read_failed = false;
        }

        battery_volt = adc_val;
    }
    else {
        // read voltage
        battery_volt = nd_device_obj->get_voltage_value(eCRANK_VOLT);
    }

    curr_volt = battery_volt;
    static bool invalid_volt_reported = false;
    if((battery_volt < nd_factory_utils::get_min_valid_voltage()) || (battery_volt >= nd_factory_utils::get_max_valid_voltage()))
    {

        if(false == invalid_volt_reported) {

            static unsigned char low_battery_volt_read_cnt = 0;
            low_battery_volt_read_cnt++;

            if( low_battery_volt_read_cnt > POWER_MONITOR_ctx->abnormal_voltage_wait_duration ) { 
                string str_msg = "Battery Voltage Read Failed:: " + std::to_string(battery_volt);
                nd_service_obj->send_err_msg(SM_E_PM_VOLTAGE_READ_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );

                LOG_E(TAG, "read_device_connected_battery_voltage - %s", str_msg.c_str());

                low_battery_volt_read_cnt = 0;

                // Set flag for invalid voltage reported.
                invalid_volt_reported = true;
            }
            battery_volt = 0;
        }

        return false;
    }
    else {
        // Reset flag for invalid voltage read.
        invalid_volt_reported = false;
    }

    LOG_I(TAG, "****Current Battery Voltage = %f - Read Count = %d ****",
            battery_volt, POWER_MONITOR_ctx->bad_battery_voltage_read_count);

    if(POWER_MONITOR_ctx->bad_battery_voltage_read_count < POWER_MONITOR_ctx->abnormal_voltage_wait_duration)
    {
        if( battery_volt < POWER_MONITOR_ctx->min_voltage_limit)
        {
            LOG_C(TAG, "Alert %d!!! Bad Battery Voltage Detected", POWER_MONITOR_ctx->bad_battery_voltage_read_count);
            POWER_MONITOR_ctx->bad_battery_voltage_read_count++;
            POWER_MONITOR_ctx->bad_battery_detected = true;
        }
        else if(battery_volt > POWER_MONITOR_ctx->max_voltage_limit )
        {
            LOG_C(TAG, "Alert %d!!! High battery Volt is detected, h/w should take action", POWER_MONITOR_ctx->bad_battery_voltage_high_read_count);
            POWER_MONITOR_ctx->bad_battery_voltage_high_read_count++;  // Not reseting bad_battery_voltage_read_count to 0
        }
        else
        {
            LOG_D(TAG, "Good Battery Voltage Detected On Vehicle");
            POWER_MONITOR_ctx->bad_battery_voltage_read_count = 0;

            if( (true == POWER_MONITOR_ctx->bad_battery_detected)
                && (SHUTDOWN_FOR_BAD_VOLTAGE == POWER_MONITOR_ctx->reason) ) {

                POWER_MONITOR_ctx->bad_battery_detected = false;

                power_monitor_battery_voltage_t battery_msg;
                battery_msg.type        = POWERMON_BAD_BATTERY_CLEAR;
                battery_msg.len         = sizeof(power_monitor_battery_voltage_t);
                battery_msg.battery_volt= battery_volt;

                nd_msgq_t::nd_msg_t msg((char *)&battery_msg, sizeof(battery_msg), false);
                POWER_MONITOR_ctx->powmon_msg_q->send(msg, nd_msgq_t::ND_MSG_MED);
            }
        }
    }
    return true;
}

void handleBatteryStatus(float battery_volt) {
    LOG_D(TAG, "Entering handleBatteryStatus Check");
    std::string bad_battery_sysfs_file = get_sysfs_path_from_enum(PowermonParam::eBAD_BATTERY_CNT);
    int battery_state = 0;
    read_from_sysfs_entry(bad_battery_sysfs_file, battery_state);
    bool is_valid_state = ((battery_state >= BatteryState::eBatteryNormal) &&
                           (battery_state <= BatteryState::eBatteryEventSent)) ? true : false;
    if((true == is_valid_state) && (battery_state != BatteryState::eBatteryEventSent)) {
        string str_msg = "Battery Voltage: " + std::to_string(battery_volt) + " " +  BatteryState::toString(battery_state);
        LOG_C(TAG, "Battery State Change Detected: %s", str_msg.c_str());
        nd_service_obj->send_err_msg(SM_E_PM_VOLTAGE_FAIL, battery_state, str_msg );
        bool is_apply = (battery_state == BatteryState::eBatteryNormal) ? false : true;
        applyPowerState(PowerStateEvent::eBadBatteryEvent, is_apply);
        write_into_sysfs_entry(bad_battery_sysfs_file, BatteryState::eBatteryEventSent);
    }
}

bool battery_voltage_crossing_limits(float &battery_volt, float &curr_volt)
{
    LOG_I(TAG, "Entering battery_voltage_crossing_limits Check");

    if( battery_volt < POWER_MONITOR_ctx->min_voltage_limit) {
        POWER_MONITOR_ctx->disableLowPowerwakeUp = true;
    }
    else{
        POWER_MONITOR_ctx->disableLowPowerwakeUp = false;
    }
    
    if(!read_device_connected_battery_voltage(battery_volt, curr_volt)) {
        LOG_E(TAG,"Battery_voltage is %f", battery_volt);
        return false;
    }
    curr_volt = battery_volt;

    handleBatteryStatus(battery_volt);

    // THIS IS A ONETIME VOLTAGE THRESHOLD CONFIGURATION TO SUPPORT 12/24V BATTERY VEHICLES.
    // THIS CODE is Intended for new Power Adapter which have support for 12/24V.
    // 12V Only Power Adapter doesn't power up the device when connected to a 24V BATTERY.
    if ( POWER_MONITOR_ctx->is_battery_cfg_init == false )
    {
        //In case of LPM use the volatge cfg from file
        if(POWER_MONITOR_ctx->crank_level() != CRANK_HIGH) {

            ifstream file;
            float battery_volt_file = 0.00;
            //check if file is available or not.
            if(file_is_present(BATTERY_VOLTAGE_FILE)) {
                file.open(BATTERY_VOLTAGE_FILE);
                file >> battery_volt_file;
                file.close();
                LOG_I(TAG, "battery_volt reading from file : %f", battery_volt_file);

            }

        }

        ofstream file;
        file.open (BATTERY_VOLTAGE_FILE);
        // If read battery voltage is greater than min_voltage_limit_24V then it is a 24V Battery. Update the min/max voltage threshold.
        if( (battery_volt >= POWER_MONITOR_ctx->min_voltage_limit_24V ) )
        {
            POWER_MONITOR_ctx->min_voltage_limit =  POWER_MONITOR_ctx->min_voltage_limit_24V;
            POWER_MONITOR_ctx->max_voltage_limit =  POWER_MONITOR_ctx->max_voltage_limit_24V;
            file << BATTERY_VOLTAGE_24V;    // TBD why it is updating in LPM
        }
        else
        {
            POWER_MONITOR_ctx->min_voltage_limit =  min_voltage_limit_read_from_config_12V;
            POWER_MONITOR_ctx->max_voltage_limit =  POWER_MONITOR_ctx->min_voltage_limit_24V;
            
            LOG_I(TAG, "Setting Max Voltage Limit For 12 Volt Battery to Minimum Voltage Limit Of 24 Volt Battery");
            LOG_I(TAG, "Setting Min Voltage Limit For 12 Volt Battery to User Configured Minimum Voltage Limit Of 12 Volt Battery");
            file << BATTERY_VOLTAGE_12V;  // TBD why it is updating in LPM
        }
        file.close();

        LOG_C(TAG, "Configured Voltage Range Thresholds(min,max) = (%f, %f)",
                POWER_MONITOR_ctx->min_voltage_limit, POWER_MONITOR_ctx->max_voltage_limit);
        {
            ifstream file_read;
            float battery_cfg_file = 0.0;
            file_read.open(BATTERY_VOLTAGE_FILE);
            file_read >> battery_cfg_file;
            LOG_C(TAG, " Battery Config File Contents %f", battery_cfg_file);
            file_read.close();
        }

        // speed should be configurable
        if((int32_t)POWER_MONITOR_ctx->gps_pos.speed >= POWER_MONITOR_ctx->min_speed_for_battery_cfg_init /* || imu update */) {
            LOG_I(TAG, "Battery config init done, speed : %f", POWER_MONITOR_ctx->gps_pos.speed);
            POWER_MONITOR_ctx->is_battery_cfg_init = true;
        }

         // If battery voltage is less than min threshold, reboot after
         // configured read retries irrespective of battery voltage
         // configuration
        if( !(battery_volt <  POWER_MONITOR_ctx->min_voltage_limit) ) {
            battery_volt = 0;
            return false;
        }
    }

    if(POWER_MONITOR_ctx->bad_battery_voltage_read_count >= POWER_MONITOR_ctx->abnormal_voltage_wait_duration) {
        LOG_C(TAG, "Current Battery Voltage = %f, Out Of Range Min %f Max %f", battery_volt,
                POWER_MONITOR_ctx->min_voltage_limit, POWER_MONITOR_ctx->max_voltage_limit);
        string str_msg = "Battery Voltage Thresholds Crossed " + std::to_string(battery_volt);
        nd_service_obj->send_err_msg(SM_E_PM_VOLTAGE_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );

        POWER_MONITOR_ctx->bad_battery_voltage_read_count = 0;
        return true;
    }

    battery_volt = 0;

    LOG_I(TAG, "Exiting battery_voltage_crossing_limits Check");

    return false;
}

bool direct_polling_crank_level(power_crank_levels_t &crank_level)
{
    LOG_I(TAG, "inside direct_polling_crank_level");

    {
        int high_count = POWER_MONITOR_ctx->crank_high_count.load();
        int low_count = POWER_MONITOR_ctx->crank_low_count.load();

        uint high_count_diff = high_count - POWER_MONITOR_ctx->prev_crank_high_count.load();
        uint low_count_diff = low_count - POWER_MONITOR_ctx->prev_crank_low_count.load();

        if((high_count_diff >= max_allowed_crank_changes) || (low_count_diff >= max_allowed_crank_changes)) {
            POWER_MONITOR_ctx->crank_event_count.fetch_add(1); // Increment total crank event count
        }
        else {
            POWER_MONITOR_ctx->crank_event_count.store(0); // Reset total crank event count
        }

        // Update previous crank low and high count for debounce/toggling check
        POWER_MONITOR_ctx->prev_crank_low_count.store(low_count);
        POWER_MONITOR_ctx->prev_crank_high_count.store(high_count);
    }

    crank_level = POWER_MONITOR_ctx->crank_level();
    if( crank_level == CRANK_ERROR ) {
        string str_msg = "Crank Error Detected";
        LOG_E(TAG, str_msg.c_str());
        ignition_status_t ign_status = nd_device_obj->get_ignition_status();
        if( IGNITION_ERR == ign_status ) {
            str_msg = "Ignition Error Detected. Setting crank_level to CRANK_LOW";
            LOG_E(TAG, str_msg.c_str());
            nd_service_obj->send_err_msg(SM_E_PM_CRANK_LEVEL_FAIL, crank_level, str_msg);
            crank_level = CRANK_LOW;
            return true;
        }
        crank_level = ( IGNITION_ON == ign_status ) ? CRANK_HIGH : CRANK_LOW;
        LOG_I(TAG, "ignition_status : %d, setting crank_level : %d", static_cast<int>(ign_status), static_cast<int>(crank_level));
    }

    if( POWER_MONITOR_ctx->present_crank_level !=  crank_level) {
        LOG_I(TAG, "direct_polling_crank_level returning true");
        return true;
    }

    LOG_I(TAG, "exiting from direct_polling_crank_level");
    return false;
}

bool send_fsck_run_msg_to_cb() {
    generic_msg_t msg;
    if (send_msg ((generic_msg_t *)&msg, REQ_CIRCULAR_BUFFER_SDCARD_FSCK_CHECK, sizeof(msg),
                                  POWER_MONITOR_ctx->power_monitor_q_name, Q_DIAG, 0)) {
        LOG_I (TAG, "sent message to CB to run fsck check on %d low power wakeup", POWER_MONITOR_ctx->lowpower_wakeups);
        return true;
    }
    
    LOG_E(TAG, "failed to send message to CB to run fsck check on %d low power wakeup", POWER_MONITOR_ctx->lowpower_wakeups);
    return false;
}

#if defined(BAGHEERA2) 
void *adc_voltage_thread_fn(void* args){
    check_volt_mon_configuration();
    float battery_volt;
    while(1) {
        usleep(1000 * 500);
        // read voltage
        battery_volt = read_adc_channel_two_data();
        if(battery_volt <= 0){
            LOG_E(TAG, "failed in read_adc_channel_two_data");
            battery_volt = 0;
            continue;
        }
        LOG_D(TAG, "OBD_INFO :: ****ADC voltage:%f****, ignition_status = %d", battery_volt, ignition_status);
        /* save the adc value here */
        if(is_ignition_status_update == true) //ignition status takes some time to update
            monitor_engine_status(ignition_status, battery_volt);
    }
}
#endif

// This function is to use to send message to main controller to update wakeup time in DB for dhub.
static void send_msg_to_update_dhub_wakeup_time_in_db() {

    LOG_I(TAG, "%s", __func__);
    power_monitor_generic_msg_t gen_msg;
    gen_msg.type = UPDATE_DHUB_WAKEUP_TIME_IN_DB;
    gen_msg.len = sizeof(power_monitor_generic_msg_t);
    nd_msgq_t::nd_msg_t msg((char *)&gen_msg, sizeof(gen_msg), false);
    POWER_MONITOR_ctx->powmon_msg_q->send(msg, nd_msgq_t::ND_MSG_MED);
}

void* direct_poling_thread_fn(void* args)
{
    LOG_I(TAG, "inside direct_poling_thread_fn");

    uint64_t previous_time = get_system_time();
    uint64_t present_time;
    bool msg_send_success = false;

    while(1) {

#if defined(BAGHEERA2) 
        // send message to CB to run fsck check on configured low power wakeup
        if( (POWER_MONITOR_ctx->lowpower_wakeups == POWER_MONITOR_ctx->fsck_lowpower_wakeup) && 
                          POWER_MONITOR_ctx->present_crank_level == CRANK_LOW && msg_send_success == false) {
            msg_send_success = send_fsck_run_msg_to_cb();
        }
#endif

        // 30 seconds
        sleep(SLEEP_CYCLE_DURATION);

        ///// logic to find RTC reset 
        present_time = get_system_time();
        if( llabs(present_time - previous_time) > (SLEEP_CYCLE_DURATION+10)*1000) {
            LOG_C(TAG, "##########POSSIBLE SCENARIO FOR RTC RESET##########");

            std::string previous_time_str = ", RTC_JUMP: " + std::to_string(previous_time);

            // Sending critical info for last wakeup/reset reason and low power wakeup count in case of RTC reset
            int pow_on_off_reason = 0;
            string reason("");
            nd_device_obj->get_reset_wake_reason(pow_on_off_reason, reason);
            reason += previous_time_str;
            nd_service_obj->send_err_msg(SM_E_PM_DB_PREV_SHUTDOWN, pow_on_off_reason, reason);

            time_sync_low_power_wakeup_cnt_msg_t m;
            m.low_power_wakeup_cnt = POWER_MONITOR_ctx->lowpower_wakeups;
            send_msg( (generic_msg_t *)&m, LOW_POWER_WAKEUP_CNT_UPDATE, sizeof(m), POWER_MONITOR_ctx->power_monitor_q_name, QNAME_TIME_SYNC, 0 );

            // sending wakeup stored wakeup time if ignition is off
            if(POWER_MONITOR_ctx->present_crank_level == CRANK_LOW) {
                string msg = "WAKEUP TIME" + std::to_string(rtc_wakeup_time) + "CURRENT TIME" + std::to_string(present_time) + previous_time_str;
                LOG_C(TAG, "%s", msg.c_str());
                nd_service_obj->send_err_msg(SM_E_PM_RTC_WAKEUP_TIME, NDService::UNUSED_ERR_AUX_CODE, msg);

                string err_msg ;
                if(true == POWER_MONITOR_ctx->misc_lowpower_wakeup){
                    err_msg = "Booted up in MISC Low power wakeup mode";
                }else{
                    err_msg = "Booted up in Low power wakeup mode";
                }
                if (POWER_MONITOR_ctx->lowpower_wakeups > POWER_MONITOR_ctx->max_lowpower_wakeups) {
                    err_msg = "Booted up due to NON-RTC Event";
                }

                err_msg += previous_time_str;

                LOG_C(TAG, "%s", err_msg.c_str());
                nd_service_obj->send_err_msg(SM_I_PM_LPW_IS_ACTIVE, POWER_MONITOR_ctx->lowpower_wakeups, err_msg);

                int speed = static_cast<int>(round(POWER_MONITOR_ctx->gps_pos.speed * 10) / 10.0);
                if(POWER_MONITOR_ctx->lowpower_wakeups != 0 ) {
                    std::string msg = "Shutting Down in ";
                    if(true == POWER_MONITOR_ctx->misc_lowpower_wakeup){
                        msg += to_string(POWER_MONITOR_ctx->misc_wakeup_duration/SECS_IN_A_MIN) +"(Min) - MISC";
                    }
                    else {
                        msg += to_string(POWER_MONITOR_ctx->lowpower_wakeup_duration/SECS_IN_A_MIN) +"(Min) - ";
                    }
                    msg += " LPW (Speed: " + to_string(speed)  + ")";

                    if(true == POWER_MONITOR_ctx->freq_low_power_wakeup) {
                        msg += " FLPW Enabled";
                    }

                    msg += previous_time_str;
                    nd_service_obj->send_err_msg(SM_E_PM_LPW_SHUTDOWN, POWER_MONITOR_ctx->lowpower_wakeups, msg);
                }
                else {
                    string msg = "Shutting Down in " + to_string(POWER_MONITOR_ctx->crank_shutdown_duration/SECS_IN_A_MIN) +"(Min) - LPW (Speed: " + to_string(speed)  + ")" + previous_time_str;
                    nd_service_obj->send_err_msg(SM_E_PM_LPW_SHUTDOWN, speed, msg);
                }

                int64_t wakeup_time = 0;
                if(true == POWER_MONITOR_ctx->ext_cam_lpw_enabled) {
                    // In this thread 30 sec sleep is present. So for average time sync we are reducing 15 sec
                    int64_t default_time_sync_rtc_jump = 15;
                    wakeup_time = present_time + (rtc_wakeup_time - previous_time - S_TO_MS(default_time_sync_rtc_jump));
                    string event_name = "DBSTATE_RTC_WAKEUP_TIME_DHUB";
                    // Wakeup time should be positive and previous and rtc_wakeup_time should be in same time range
                    if(wakeup_time > 0) {
                        add_event_db(POWER_MONITOR_ctx->db_handle, wakeup_time, POWER_MONITOR_ctx->boot_time, POWER_MONITOR_ctx->pid_num, event_name, "WAKEUP");
                        rtc_wakeup_time = wakeup_time;
                    }
                    string msg = event_name + ": " + to_string(rtc_wakeup_time) + previous_time_str;
                    LOG_C(TAG, " Updated %s", msg.c_str());
                    int rtc_time = MS_TO_S((int64_t)(rtc_wakeup_time - present_time));
                    nd_service_obj->send_err_msg(SM_E_PM_RTC_WAKEUP_TIME, rtc_time, msg);
                }
            }
        }

        LOG_I(TAG, "previous_time %lld , present_time %lld", previous_time, present_time);
        previous_time = present_time;
        //////

        bool postpone_shutdown_status = true;
        bool post_message_avoid_timeout = true;

        int time_diff; // +ve if we exceed -ve if we have time
        if(check_uptime(time_diff, 3)) {
            LOG_I(TAG, "check_uptime returned true; send message to main controller");
            power_monitor_maxtimeout_t maxtimeout_msg;
            maxtimeout_msg.type        = POWERMON_MAXTIMEOUT;
            maxtimeout_msg.len         = sizeof(power_monitor_maxtimeout_t);
            maxtimeout_msg.time_elapsed= time_diff;

            nd_msgq_t::nd_msg_t msg((char *)&maxtimeout_msg, sizeof(maxtimeout_msg), false);
            POWER_MONITOR_ctx->powmon_msg_q->send(msg, nd_msgq_t::ND_MSG_MED);

            postpone_shutdown_status = false;
            post_message_avoid_timeout = false;
        }

        float battery_volt = 0.0;
        float curr_volt = 0.0;
        if(battery_voltage_crossing_limits(battery_volt, curr_volt))
       {
            LOG_C(TAG, "battery voltage crossed limits");
            if(battery_volt < POWER_MONITOR_ctx->min_voltage_limit)
            {
                POWER_MONITOR_ctx->disableLowPowerwakeUp = true;

                if(ext_cam_feature_enabled)
                {
                    if(isMDVRConnected())
                    {
                        LOG_C(TAG, "Bad Voltage Detected - Notifying Ext Camera Service");
                        if(set_bad_battery_voltage_detect_threshold_in_mdvr())
                        {
                            LOG_I(TAG, "Successfully Set Bad Voltage Threshold In DHUB");
                        }
                        else
                        {
                            LOG_I(TAG, "Failed To Set Bad Voltage Threshold In DHUB");
                        }
                        sleep(10);
                    }
                    else {
                        LOG_I(TAG, "DHUB is Not Reachable");
                    }
                }
            }
            LOG_C(TAG, "initiate shutdown");
            power_monitor_battery_voltage_t battery_msg;
            battery_msg.type        = POWERMON_BAD_BATTERY_VOLTAGE;
            battery_msg.len         = sizeof(power_monitor_battery_voltage_t);
            battery_msg.battery_volt= battery_volt;

            nd_msgq_t::nd_msg_t msg((char *)&battery_msg, sizeof(battery_msg), false);
            POWER_MONITOR_ctx->powmon_msg_q->send(msg, nd_msgq_t::ND_MSG_MED);

            postpone_shutdown_status = false;
            post_message_avoid_timeout = false;
        }
        else {
            LOG_D(TAG, "battery voltage within limits" );
        }

        POWER_MONITOR_ctx->current_voltage = curr_volt;
        LOG_D(TAG,"Curr Voltage is %f",POWER_MONITOR_ctx->current_voltage);

        power_crank_levels_t crank_level;
        if(direct_polling_crank_level(crank_level))
        {
            LOG_C(TAG, "direct_polling_crank_level succeded; voltage changed");
            LOG_C(TAG, "send notification to event loop");
            power_monitor_directpolling_crank_t crank_msg;
            crank_msg.type        = POWERMON_DIRECTPOLL_CRANK_CHANGE;
            crank_msg.len         = sizeof(power_monitor_directpolling_crank_t);
            crank_msg.event_time  = get_system_time();
            crank_msg.crank_level = crank_level;

            nd_msgq_t::nd_msg_t msg((char *)&crank_msg, sizeof(crank_msg), false);
            POWER_MONITOR_ctx->powmon_msg_q->send(msg, nd_msgq_t::ND_MSG_MED);

            postpone_shutdown_status = false;
            post_message_avoid_timeout = false;
        }

        // If delay shutdown is on going for DHUB status check or uploader activity, then postpone shutdown and avoid post pone shutdown for POWERMON_NORMAL_RUN
        if (true == POWER_MONITOR_ctx->shutdown_delayed) {
            LOG_I(TAG, "shutdown_delayed is enabled");
            postpone_shutdown_status = false;
            post_message_avoid_timeout = false;
        }

        //if none of above things happned postpone shutdown to POSTPONE_SHUTDOWN_DURATION
        if(postpone_shutdown_status == true && 
            POWER_MONITOR_ctx->reason >= SHUTDOWN_CANCELLED)
        {
            power_monitor_norman_run_t normal_run;

            LOG_I(TAG, "No special event happned ; postpone shutdown by 5 mins");
            normal_run.type        = POWERMON_NORMAL_RUN;
            normal_run.len         = sizeof(power_monitor_norman_run_t);
            normal_run.event_time  = -1;

            nd_msgq_t::nd_msg_t msg((char *)&normal_run, sizeof(normal_run), false);
            POWER_MONITOR_ctx->powmon_msg_q->send(msg, nd_msgq_t::ND_MSG_MED);
            post_message_avoid_timeout = false;
        }

        if(post_message_avoid_timeout == true ) {
            power_monitor_norman_run_t normal_run;
            LOG_I(TAG, "sending a dummy message to avoid msg_loop timeout");
            normal_run.type        = POWERMON_AVOID_TIMOUT;
            normal_run.len         = sizeof(power_monitor_norman_run_t);
            normal_run.event_time  = -1;

            nd_msgq_t::nd_msg_t msg((char *)&normal_run, sizeof(normal_run), false);
            POWER_MONITOR_ctx->powmon_msg_q->send(msg, nd_msgq_t::ND_MSG_MED);
         }

        // monitor dhub wakeup time and device still up then add next wakeup time in DB
        if ((true == POWER_MONITOR_ctx->ext_cam_lpw_enabled) &&
            (true == monitor_dhub_wakeup_time) &&
            (ignition_status_t::IGNITION_OFF == static_cast<ignition_status_t>(get_status_from_sysfs_source(PowermonParam::eGPIO_IGN)))) {

            int64_t current_time = convert_epoch_format(get_system_time(), DigitsOfEpoch::eDigits_Seconds);
            int64_t dhub_wakeup_time = convert_epoch_format(g_dhub_wakeup_time, DigitsOfEpoch::eDigits_Seconds);
            LOG_I(TAG, "DHUB wakeup time monitoring enabled, current_time: %lld, dhub_wakeup_time: %lld", current_time, dhub_wakeup_time);

            if (abs(current_time - dhub_wakeup_time) <= MIN_TO_SEC(1)) {
                // send message to update dhub wakeup time
                update_dhub_wakeup_time_for_extended_shutdown = true;
                send_msg_to_update_dhub_wakeup_time_in_db();
                monitor_dhub_wakeup_time = false;
            }
        }

        // send keepalive to SVC
        pthread_mutex_lock ( &keepalive_status_mutex );
        LOG_I(TAG, "direct_poling_thread_fn all_thread_keepalive_status:  %u", all_thread_keepalive_status);
        if( !svc_util_send_keepalive(all_thread_keepalive_status) ) {
            LOG_E(TAG, "Something went wrong in sending keepalive to SVC");
        }
        else {
            //LOG_I(TAG, "message sent to SVC");
            all_thread_keepalive_status = 0x05;      // initialize bit to 1 for all thread except this thread- bit 2  
        }
        pthread_mutex_unlock ( &keepalive_status_mutex );

        // Truncate reset_reason.txt file after 2 mins of boot time if present, currently only for bagheera2
        if((true == file_is_present(reset_reason_file_path)) && (false == is_suspend_reset_reason_file_truncated) && (get_system_monotonic_time() > S_TO_MS(TIME_TO_TRUCATE_BAGHEERA2_RESET_REASON_FILE))){
            if(true == file_truncate(reset_reason_file_path)) {
                LOG_I(TAG, "Truncated reset_reason.txt file");
                is_suspend_reset_reason_file_truncated = true;
            }
            else {
                LOG_E(TAG, "Failed to truncate reset_reason.txt file");
            }
        }
    }
    LOG_E(TAG, "exiting from direct_poling_thread_fn");
    return NULL;
}

static bool init_config_device() {

    POWER_MONITOR_ctx->device_config = new Config_parser(DEVICE_CONFIG_INI);
    if( POWER_MONITOR_ctx->device_config->getParseStatus() != true) {
        LOG_E(TAG, "Cannot allocate Config DEVICE_CONFIG_INI");
        return false;
    }
    return true;
}

static bool init_config_bagheera() {

    POWER_MONITOR_ctx->bagheera_config = new Config_parser(BAGHEERA_CONFIG_INI);
    if( POWER_MONITOR_ctx->bagheera_config->getParseStatus() != true) {
        LOG_E(TAG, "Cannot allocate Config BAGHEERA_CONFIG_INI");
        return false;
    }
    return true;
}


static bool init_config_cloud() {

    POWER_MONITOR_ctx->cloud_config = new Config_parser(CLOUD_CONFIG_INI);
    if( POWER_MONITOR_ctx->cloud_config->getParseStatus() != true) {
        LOG_E(TAG, "Cannot allocate Config CLOUD_CONFIG_INI");
        return false;
    }
    return true;
}

static bool init_config_nddevice() {

    POWER_MONITOR_ctx->nddevice = new Config_parser(ND_DEVICE_INI);
    if( POWER_MONITOR_ctx->nddevice->getParseStatus() != true) {
        LOG_E(TAG, "Cannot allocate Config ND_DEVICE_INI");
        return false;
    }
    return true;
}

static bool deinit_config() {

    bool retval1 = false;
    bool retval2 = false;
    bool retval3 = false;
    bool retval4 = false;

    if (POWER_MONITOR_ctx->device_config) {
        delete POWER_MONITOR_ctx->device_config;
        POWER_MONITOR_ctx->device_config = NULL;
        retval1 = true;
    }
    else {
        LOG_E(TAG, "POWER_MONITOR_ctx->device_config == null");
    }

    if (POWER_MONITOR_ctx->bagheera_config) {
        delete POWER_MONITOR_ctx->bagheera_config;
        POWER_MONITOR_ctx->bagheera_config = NULL;
        retval3 = true;
    }
    else {
        LOG_E(TAG, "POWER_MONITOR_ctx->bagheera_config == null");
    }

    if (POWER_MONITOR_ctx->cloud_config) {
        delete POWER_MONITOR_ctx->cloud_config;
        POWER_MONITOR_ctx->cloud_config = NULL;
        retval3 = true;
    }
    else {
        LOG_E(TAG, "POWER_MONITOR_ctx->cloud_config == null");
    }

    if (POWER_MONITOR_ctx->nddevice) {
        delete POWER_MONITOR_ctx->nddevice;
        POWER_MONITOR_ctx->nddevice = NULL;
        retval4 = true;
    }
    else {
        LOG_E(TAG, "POWER_MONITOR_ctx->cloud_config == null");
    }

    return retval1 && retval2 && retval3 && retval4;
}

int ignition_cb_func(void *app)
{
    int value = *((int*)(&app));

    //crank_level = (crank_level == '1') ? CRANK_HIGH : CRANK_LOW ;
    power_crank_levels_t crank_level = CRANK_ERROR;
    if( CRANK_LEVEL_HIGH == value )
        crank_level = CRANK_HIGH;
    else if( CRANK_LEVEL_LOW == value )
        crank_level = CRANK_LOW;

    if(crank_level == CRANK_ERROR)
    {
        LOG_E(TAG, "Something went wrong crank_level == CRANK_ERROR in ignition_cb_func");
        //return -1;
    }
    LOG_D(TAG, "inside the callback value %d crank_level %d",value, crank_level);

    power_monitor_crank_change_t crank_msg;
    crank_msg.type        = POWERMON_CRANK_CHANGE;
    crank_msg.len         = sizeof(power_monitor_crank_change_t);
    crank_msg.crank_change_time = get_system_time() ;
    crank_msg.crank_level  = crank_level;

    nd_msgq_t::nd_msg_t msg((char *)&crank_msg, sizeof(crank_msg), false);
    POWER_MONITOR_ctx->powmon_msg_q->send(msg, nd_msgq_t::ND_MSG_MED);

    return 0;
}

void* gpio171_interrupt_thread_fn(void* args)
{
    LOG_I(TAG, "inside gpio171_interrupt_thread_fn");
    sleep(STARTING_DELAY);
    int gpio171_fd;
    gpio171_fd = open(nd_device_obj->gpio_crank_level_info_file().c_str(), O_RDWR);
    if (gpio171_fd < 0) {
        string str_msg = "failed in open " + nd_device_obj->gpio_crank_level_info_file();
        LOG_I(TAG, str_msg.c_str());
        nd_service_obj->send_err_msg(SM_E_PM_GPIO_INT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        // push an event to main guy before exiting
        power_monitor_generic_msg_t gen_msg;
        gen_msg.type = POWERMON_INTRPT_THREAD_CRASH;
        gen_msg.len = sizeof(gen_msg);

        nd_msgq_t::nd_msg_t msg((char *)&gen_msg, sizeof(gen_msg), false);
        POWER_MONITOR_ctx->powmon_msg_q->send(msg, nd_msgq_t::ND_MSG_MED);

        return NULL;
    }

    int ret;
    int a;
    fd_set fds;
    
    while (1) {
        FD_ZERO(&fds);
        FD_SET(gpio171_fd, &fds);

        ret = select(gpio171_fd+1, NULL, NULL, &fds, NULL);
        lseek(gpio171_fd, SEEK_SET, 0);
        int ret = read(gpio171_fd,&a,1);
        if(ret != 1){
            LOG_E(TAG, "failed to read appropriat value; continue");
            continue;
        }
        LOG_C(TAG, "crank voltage state changed %c",a);

        if (ret > 0 && FD_ISSET(gpio171_fd, &fds)) {
            ignition_cb_func((void*)a);
        }else{
            LOG_E(TAG, "failed in select function");
            if(ret == -1 && errno == EINTR){
                LOG_C(TAG, "ret == -1 && errno == EINTR");
                continue;
            }
            else{
                close(gpio171_fd);
                // push an event to main guy before exiting
                LOG_E(TAG, "failed in select returning from gpio171_interrupt_thread_fn");
                power_monitor_generic_msg_t gen_msg;
                gen_msg.type = POWERMON_INTRPT_THREAD_CRASH;
                gen_msg.len = sizeof(gen_msg);

                nd_msgq_t::nd_msg_t msg((char *)&gen_msg, sizeof(gen_msg), false);
                POWER_MONITOR_ctx->powmon_msg_q->send(msg, nd_msgq_t::ND_MSG_MED);
                break;
            }
        }

    }
}

void ignition_gpio_status_cb(void *app) {

    int value = *((int*)(&app));
    power_crank_levels_t crank_level = CRANK_ERROR;

    LOG_I(TAG, "inside the callback value %d", value);

    if( CRANK_LEVEL_HIGH == value ) {
        crank_level = CRANK_HIGH;
    }
    else if( CRANK_LEVEL_LOW == value ) {
        crank_level = CRANK_LOW;
    }

    if(crank_level == CRANK_ERROR) {
        LOG_E(TAG, "Something went wrong crank_level == CRANK_ERROR in ignition_gpio_status_cb");
        return;
    }

    // If manual ignition is done, it means already we have sent the message to main controller
    // So, No need to send the message again to update the ignition gpio status
    if (true == manual_ignition_cb_done) {
        LOG_I(TAG, "manual_ignition_cb_done is true; returning from ignition_gpio_status_cb");
        manual_ignition_cb_done = false;
        return;
    }

    power_monitor_crank_change_t crank_msg = {};
    crank_msg.type              = POWER_MON_IGNITION_GPIO_STATUS;
    crank_msg.len               = sizeof(power_monitor_crank_change_t);
    crank_msg.crank_change_time = get_system_time() ;
    crank_msg.crank_level       = crank_level;

    nd_msgq_t::nd_msg_t msg((char *)&crank_msg, sizeof(crank_msg), false);
    POWER_MONITOR_ctx->powmon_msg_q->send(msg, nd_msgq_t::ND_MSG_MED);
}

void* ign_gpio_status_thread_fn(void*) {

    LOG_I(TAG, "Monitor Ignition GPIO Status Thread Started");
    int gpio_fd = -1;
    int ret = -1;
    string sysfs_path = nd_device_obj->get_ign_gpio_sysfs_path();

    gpio_fd = open(sysfs_path.c_str(), O_RDWR);
    if (gpio_fd < 0) {
        LOG_E(TAG, "Failed to open Ignition GPIO sysfs file");
        return NULL;
    }

    int a = 0;
    fd_set fds;

    while (1) {
        FD_ZERO(&fds);
        FD_SET(gpio_fd, &fds);

        ret = select(gpio_fd+1, NULL, NULL, &fds, NULL);
        lseek(gpio_fd, SEEK_SET, 0);
        int ret = read(gpio_fd, &a, 1);
        if(ret != 1){
            LOG_E(TAG, "Failed to read from %s", sysfs_path.c_str());
            continue;
        }
        LOG_C(TAG, "Ignition GPIO status changed %c", a);

        if (ret > 0 && FD_ISSET(gpio_fd, &fds)) {
            ignition_gpio_status_cb((void*)a);
        }
        else {
            LOG_E(TAG, "Failed in select function");
            if((-1 == ret) && (EINTR == errno)){
                LOG_C(TAG, "ret == -1 && errno == EINTR");
                continue;
            }
            else {
                close(gpio_fd);
                LOG_E(TAG, "Failed in select returning from ign_gpio_status_thread_fn");
                break;
            }
        }
    }
}

bool check_priority(power_monitor_shutdown_reason present_req, 
                    power_monitor_shutdown_reason past_req)
{
    LOG_I(TAG, "check_priority present_req %d, past_req %d", present_req, past_req);
    
    if( past_req == present_req ) {
        return false;
    }
    if( present_req >= SHUTDOWN_FOR_BAD_VOLTAGE &&
        present_req <= SHUTDOWN_FOR_INSTALLER_APP ) {
        return true;
    }
//used for B1 SDCARD RO
#if 0
    if(present_req == SHUTDOWN_FOR_SDCARD_RO_RECOVERY) {
        LOG_I(TAG, "SHUTDOWN_FOR_SDCARD_RO_RECOVERY is highest priority; issuing sutdown immediatley");
        return true;
    }
#endif
    if( past_req > SHUTDOWN_FOR_BAD_VOLTAGE &&
        past_req <= SHUTDOWN_FOR_UNKNOWN_BOOTUP ) {
        return false;
    }
    LOG_I(TAG, "returning true from check_priority");
    return true;
}


bool check_event_frequency(int allow_sdcard_reboot_freq) {

    int last_reboot_happned;
    db_state_info_t data_node;
    data_node.event_time = 0;
    if( get_db_node_event(power_dbstate_enum_t::toString(power_dbstate_enum_t::DBSTATE_SHUTDOWN_SDCARD), &data_node, POWER_MONITOR_ctx->db_handle) == false ){
        LOG_E(TAG, "failed in get_db_node_event");
    }
    LOG_I(TAG, "data_node.event_time %lld , data_node.event %s, data_node.action %s, allow_sdcard_reboot_freq %d", 
            data_node.event_time, data_node.event.c_str(), data_node.action.c_str(), allow_sdcard_reboot_freq );
    // In case of RTC reset, for past time reset POR will not happen. For Future time reset one more POR can happen.
    if( data_node.event_time > 0 ){
        if( data_node.event_time > get_system_time() ) {
            LOG_E(TAG, "data_node.event_time %lld > get_system_time %lld ;must be RTC jump; returning true",
                    data_node.event_time, get_system_time() );
            return true;
        }
        if( ( (get_system_time() - data_node.event_time)/1000 ) < allow_sdcard_reboot_freq ){
            LOG_I(TAG, "system_time %lld data_node.event_time %lld", get_system_time(),data_node.event_time);
            LOG_C(TAG, "Ignoting this event as we recently got the same request in < %d secs", allow_sdcard_reboot_freq);
            return false;
        }
    }
    return true;
}

static int callback_count_rows(void *count, int argc, char **argv, char **azColName){
    int i;
    int64_t *temp_count = (int64_t* )count;

    temp_count[0] = temp_count[0] + argc;
    if (argc != 1){
        temp_count[1] = 0;
    } else {
        string_to_int64(argv[0], temp_count[1]) ;
    }
    return 0;
}

void limited_system_reboot(int maxRebootCnt, int pwr_state) {

    // If reboot triggered for installer app, then no need to check and delay the reboot
    if ((power_dbstate_enum_t::DBSTATE_SHUTDOWN_INSTALLER_APP == (power_dbstate_enum_t)pwr_state) ||
        (power_dbstate_enum_t::DBSTATE_SHUTDOWN_INSTALLER_APP_CRASH == (power_dbstate_enum_t)pwr_state)) {
        LOG_I(TAG, "Device is rebooting for installer app, No need to check and delay the reboot");
        // Add event to DB
        add_event_db((power_dbstate_enum_t)pwr_state , "REBOOT");
        return;
    }

    int rc;
    int totalRebootCnt = 0 ;
    db_state_info_t data_node_high = {0, };
    get_db_node_action("SHUTDOWN", &data_node_high, POWER_MONITOR_ctx->db_handle);
    string val_string ;
    std::stringstream val_stream;
    // find total REBOOT because of CAMERA crash after last device shutdown.
    val_stream <<
                  "SELECT COUNT(*) from POWERSTATES WHERE ACTION == \'REBOOT\' AND INDEXID > " << data_node_high.index << " AND EVENT != \'DBSTATE_SHUTDOWN_INSTALLER_APP\' AND EVENT != \'DBSTATE_SHUTDOWN_INSTALLER_APP_CRASH\'";
    val_string = val_stream.str();
    int64_t count_indx[3] = {0, 0, 0};
    rc = POWER_MONITOR_ctx->exec_cmd_db(POWER_MONITOR_ctx->db_handle, val_string,
                            callback_count_rows, (void*)&count_indx[0]);
    if(rc == false){
        LOG_E(TAG, "failed to execute %s", val_string.c_str());
        return ;
    }
    LOG_I(TAG, "count_indx[1] : %lld, data_node_high.index: %d", count_indx[1], data_node_high.index);
    totalRebootCnt =   count_indx[1] ;
    val_stream.str("");
    val_stream.clear();
    val_stream <<
                "SELECT (EVENTTIME-BOOTTIME) as bootTime from (SELECT * FROM POWERSTATES WHERE INDEXID > " 
                << data_node_high.index << " AND ACTION== \'REBOOT\' AND EVENT != \'DBSTATE_SHUTDOWN_INSTALLER_APP\' AND EVENT != \'DBSTATE_SHUTDOWN_INSTALLER_APP_CRASH\' ORDER BY INDEXID DESC LIMIT " << max_B2B_reboot_allowed << ") ORDER BY bootTime DESC LIMIT 1"; 
    val_string = val_stream.str();
    int64_t testind[3] = {0, 0, 0};
    rc = POWER_MONITOR_ctx->exec_cmd_db(POWER_MONITOR_ctx->db_handle, val_string,
                            callback_count_rows, (void*)&testind[0]);
    if(rc == false){
        LOG_E(TAG, "failed to execute %s", val_string.c_str());
        return ;
    }
    int64_t maxRebootTime = convert_time_unit(nd_time_unit::time_in_milliseconds, nd_time_unit::time_in_seconds, testind[1]);
    LOG_I(TAG, "totalRebootCnt: %d, maxRebootTime: %lld ", totalRebootCnt, maxRebootTime );
    if((totalRebootCnt < maxRebootCnt) || (maxRebootTime > delay_reboot_time)) {
        LOG_I(TAG, "Device is rebooting. totalRebootCnt: %d", totalRebootCnt);
        add_event_db((power_dbstate_enum_t)pwr_state , "REBOOT");
        bool reboot_status = system_reboot();
    }
    else {
        // If db state is SHUTDOWN_MSP_FAIL_REBOOT, delay the reboot
        if(power_dbstate_enum_t::DBSTATE_SHUTDOWN_MSP_FAIL_REBOOT == (power_dbstate_enum_t)pwr_state ) {
            LOG_I(TAG, "Send message to SVC to not do house keeping due to MSP FAIL REBOOT. ");
            static const string server_queue = "Q_SVC";
            int msg_ids =0;
            generic_msg_t m;
            if( false == send_msg( (generic_msg_t *)&m, (msg_type_t)SVC_PACIFY_START, sizeof(m), Q_POWERMON, server_queue,  msg_ids++ ) ) {
               LOG_I(TAG, "Send message SVC_PACIFY_START to svc failed. ");
            }
            add_event_db((power_dbstate_enum_t)pwr_state , "REBOOT");
            ignore_SVC_reboot = true;
            LOG_I(TAG, "Delay the shutdown. totalRebootCnt: %d", totalRebootCnt);

            // To delay for delay_reboot_time and every 1 min set the RTC time
            set_rtc_time_limited_system_reboot();
            if( false == send_msg( (generic_msg_t *)&m, (msg_type_t)SVC_PACIFY_STOP, sizeof(m), Q_POWERMON, server_queue,  msg_ids++ ) ) {
               LOG_I(TAG, "Send message SVC_PACIFY_STOP to svc failed. ");
            }
            bool reboot_status = system_reboot();
        }
        else if(POWER_MONITOR_ctx->present_crank_level == CRANK_HIGH ) {
            LOG_I(TAG, "Send message to SVC to not do house keeping. ");
            static const string server_queue = "Q_SVC";
            int msg_ids =0;
            generic_msg_t m;
            if( false == send_msg( (generic_msg_t *)&m, (msg_type_t)SVC_PACIFY_START, sizeof(m), Q_POWERMON, server_queue,  msg_ids++ ) ) {
               LOG_I(TAG, "Send message SVC_PACIFY_START to svc failed. ");
            }
            add_event_db((power_dbstate_enum_t)pwr_state , "REBOOT");
            ignore_SVC_reboot = true;
            LOG_I(TAG, "Delay the shutdown. totalRebootCnt: %d", totalRebootCnt);

            // To delay for delay_reboot_time and every 1 min set the RTC time
            set_rtc_time_limited_system_reboot();
            if( false == send_msg( (generic_msg_t *)&m, (msg_type_t)SVC_PACIFY_STOP, sizeof(m), Q_POWERMON, server_queue,  msg_ids++ ) ) {
               LOG_I(TAG, "Send message SVC_PACIFY_STOP to svc failed. ");
            }
            bool reboot_status = system_reboot();
        }
        else {
                LOG_I(TAG, "Device is shutting down. totalRebootCnt: %d", totalRebootCnt);
                add_event_db((power_dbstate_enum_t)pwr_state , "SHUTDOWN");
                // try do normal shutdown
                bool to_suspend = true;
                FILE *fp=NULL;
                stringstream command;
                command.str("");

        LOG_I(TAG, " POWER_MONITOR_ctx->disableLowPowerwakeUp %d  POWER_MONITOR_ctx->suspend_mode%d", POWER_MONITOR_ctx->disableLowPowerwakeUp, POWER_MONITOR_ctx->suspend_mode);
        if((false == POWER_MONITOR_ctx->disableLowPowerwakeUp) && 
                (true == POWER_MONITOR_ctx->suspend_mode) && (POWER_MONITOR_ctx->lowpower_wakeups < POWER_MONITOR_ctx->max_lowpower_wakeups)) {
                    to_suspend = true;
                } else
                {
                    command << "shutdown now";
                    to_suspend = false;
                }

                if(to_suspend == false) {
                    LOG_I(TAG, "Device is shutting. totalRebootCnt: %d, lpw cnt: %d, max lpw: %d", totalRebootCnt, POWER_MONITOR_ctx->lowpower_wakeups, POWER_MONITOR_ctx->max_lowpower_wakeups);

                    if(to_suspend == false) {
                        file_fd_sync(PM_DB_PATH_NAME);
                        nd_device_obj->device_shutdown(TAG, command);
                    }

                    // enable_pmic_wdt(TAG);
                    // LOG_I(TAG, "Calling command: %s", command.str().c_str());
                    // fp = popen(command.str().c_str(), "r");
                    // if (fp == NULL) {
                    //     LOG_E(TAG, "Failed to execute command in  shutdown_poling_thread_fn ::%s::" ,
                    //             command.str().c_str() );
                    // }
                    // else {
                    //     pclose(fp);
                    //     LOG_I(TAG, "reboot_status == true; wait for a while to get killed");
                    //     sleep(5);
                    // }
                }
                else  { // suspend
                    LOG_I(TAG, "Device is suspended. totalRebootCnt: %d, lpw cnt: %d, max lpw: %d", totalRebootCnt, POWER_MONITOR_ctx->lowpower_wakeups, POWER_MONITOR_ctx->max_lowpower_wakeups);
                    // system_suspend();
                    nd_device_obj->device_suspend();
                }
        }
    }
}
bool modem_shutdown()
{
    string sim_lpm = "lte_gps_sample_app 'AT+CFUN=0'";
    if ( system_execute("SIERRA_SIM_LPM",sim_lpm.c_str()) != 0) {
        LOG_E(TAG, " FAILED AT+CFUN=0, err: %d", errno);
        return false;
    }

    string pwr_dwn = "lte_gps_sample_app 'AT!POWERDOWN'";
    if( system_execute("SIERRA_PWR_DWN",pwr_dwn.c_str()) != 0) {
        LOG_E(TAG, " FAILED AT+CPWROFF=1, err: %d", errno);
        return false;
    }

    sleep(2);

    return true;
}

bool check_for_delay_shutdown(power_monitor_shutdown_reason reason)
{
    // Maintain the priority
    // First one should be uploader activity
    // DHUB status check should be Least
   if(((true == POWER_MONITOR_ctx->extended_post_ignition_off_and_lpw_timer) && (true == POWER_MONITOR_ctx->uploader_data_upload_pending)) ||
        (true == POWER_MONITOR_ctx->dhub_status_check_enabled))
   {
       if((SHUTDOWN_FOR_IGNITION_OFF == reason) ||
          (SHUTDOWN_FOR_LOWPOWER_WAKEUP == reason) ||
          (POSTPONE_FOR_UPLOADER_ACTIVITY == reason) ||
          (POSTPONE_FOR_DHUB_STATUS_CHECK == reason) ||
          (SHUTDOWN_FOR_UNKNOWN_BOOTUP == reason)) {

            return true;
       }
   }
   return false;
}

void handleShutdownReason(int reason, int& pwr_state) {

    switch (reason) {
        case SHUTDOWN_FOR_CYCLIC_REBOOT:
            LOG_I(TAG, "shutdown for cyclic reboot");
            pwr_state = power_dbstate_enum_t::DBSTATE_SHUTDOWN_CYCLIC;
            break;

        case SHUTDOWN_FOR_SDCARD_RO_RECOVERY:
            LOG_I(TAG, "shutdown for SHUTDOWN_FOR_SDCARD_RO_RECOVERY");
            pwr_state = power_dbstate_enum_t::DBSTATE_SHUTDOWN_SDCARD;
            break;

        case SHUTDOWN_FOR_INSTALLER_APP:
            LOG_I(TAG, "shutdown for SHUTDOWN_FOR_INSTALLER_APP");
            pwr_state = power_dbstate_enum_t::DBSTATE_SHUTDOWN_INSTALLER_APP;
            break;

        case SHUTDOWN_FOR_SVC_REBOOT:
            LOG_I(TAG, "shutdown for SHUTDOWN_FOR_SVC_REBOOT");
            pwr_state = power_dbstate_enum_t::DBSTATE_SHUTDOWN_SVC;
            break;

        case SHUTDOWN_FOR_INSTALLER_APP_CRASH:
            LOG_I(TAG, "shutdown for SHUTDOWN_FOR_INSTALLER_APP_CRASH");
            pwr_state = power_dbstate_enum_t::DBSTATE_SHUTDOWN_INSTALLER_APP_CRASH;
            break;

        case SHUTDOWN_FOR_CAM_CRASH:
            LOG_I(TAG, "shutdown for SHUTDOWN_FOR_CAM_CRASH");
            pwr_state = power_dbstate_enum_t::DBSTATE_SHUTDOWN_CAM_CRASH;
            break;

        case SHUTDOWN_FOR_AWSIOT:
            LOG_I(TAG, "shutdown for SHUTDOWN_FOR_AWSIOT");
            pwr_state = power_dbstate_enum_t::DBSTATE_SHUTDOWN_AWSIOT;
            break;

        case SHUTDOWN_FOR_ANALYTICS:
            LOG_I(TAG, "shutdown for SHUTDOWN_FOR_ANALYTICS");
            pwr_state = power_dbstate_enum_t::DBSTATE_SHUTDOWN_ANALYTICS;
            break;

        case SHUTDOWN_FOR_MSP_FAIL_REBOOT:
            LOG_I(TAG, "shutdown for SHUTDOWN_FOR_MSP_FAIL_REBOOT");
            pwr_state = power_dbstate_enum_t::DBSTATE_SHUTDOWN_MSP_FAIL_REBOOT;
            break;

        default:
            // Unknown reason
            break;
    }
}

void update_voltage_before_shutdown()
{
    ofstream prev_volt_fd(prev_volt_file, std::ofstream::binary);
    if(prev_volt_fd.is_open() == false){
        LOG_E(TAG, "Failed to open file %s", prev_volt_file.c_str());
        return;
    }
    try {
        prev_volt_fd << POWER_MONITOR_ctx->current_voltage << endl;
    }
    catch(...){
        LOG_E(TAG, "Exception in update_voltage_before_shutdown()");
    }
    prev_volt_fd.close();
}

void update_valid_speed_before_shutdown(int speed)
{
    ofstream prev_valid_speed_fd(prev_valid_speed_file, std::ofstream::binary);
    if(prev_valid_speed_fd.is_open() == false){
        LOG_E(TAG, "Failed to open file %s", prev_valid_speed_file.c_str());
        return;
    }
    try{
        prev_valid_speed_fd << speed << endl;
    }
    catch(...){
        LOG_E(TAG, "Exception in update_valid_speed_before_shutdown()");
    }
    prev_valid_speed_fd.close();
}

static bool configure_rtc(int rtc_time, int shutdown_after_secs = 0)
{
    int rtc_status = false;
    int retry = 0;

    do {
        if (true == nd_device_obj->configure_rtc_time(rtc_time, shutdown_after_secs)) {
            rtc_status = true;
            LOG_I(TAG, "configure_rtc_time called with %d secs", rtc_time);
            break;
        }
        LOG_E(TAG, "configure_rtc_time failed to set RTC time, retrying");
    } while( ++retry < RTC_SET_MAX_RETRY);

    return rtc_status;
}

// This function is to set the RTC time for limited system reboot every 1 min while delaying the shutdown
static void set_rtc_time_limited_system_reboot() {

    int count = (delay_reboot_time/MIN_TO_SEC(1));

    for(int i = 0; i < count; i++) {
        if(false == configure_rtc(POWER_MONITOR_ctx->safety_wakeup)) {
            LOG_E(TAG, "Failed to set RTC time for limited system reboot");
        }
        sleep(MIN_TO_SEC(1));
    }
}

// This function ensures that the RTC time is within a valid range according to the shutdown reason based expected rtc time
// If the rtc_time is not in valid range, it sets the rtc_time to the expected rtc time
void validate_rtc_time(power_monitor_shutdown_reason reason, int &rtc_time)
{

    LOG_I(TAG,"%s, reason: %d, rtc_time: %d", __func__, reason, rtc_time);
    int expected_rtc_time = 0;
    bool max_lpw_detected = false;

    switch (reason)
    {
        case SHUTDOWN_FOR_BAD_VOLTAGE:
            if (POWER_MONITOR_ctx->disableLowPowerwakeUp) {
                LOG_D(TAG, "SHUTDOWN_FOR_BAD_VOLTAGE: disableLowPowerwakeUp is enabled, Setting Upper limit of rtc_time to safety wakeup time for bad voltage shutdown");
                expected_rtc_time = POWER_MONITOR_ctx->safety_wakeup_time_for_bad_voltage_shutdown;
            } else {
                LOG_D(TAG, "SHUTDOWN_FOR_BAD_VOLTAGE: disableLowPowerwakeUp is disabled, Setting Upper limit of rtc_time to LPW cycle duration");
                expected_rtc_time = get_lpw_cycle_duration(true);
            }
            break;

        case SHUTDOWN_FOR_CYCLIC_REBOOT:
            LOG_D(TAG, "SHUTDOWN_FOR_CYCLIC_REBOOT: Setting Upper limit of rtc_time to safety wakeup time");
            expected_rtc_time = POWER_MONITOR_ctx->safety_wakeup;
            break;

        case SHUTDOWN_FOR_IGNITION_OFF:
            LOG_D(TAG, "SHUTDOWN_FOR_IGNITION_OFF: Setting Upper limit of rtc_time to LPW cycle duration");
            expected_rtc_time = get_lpw_cycle_duration(true);
            break;

        case SHUTDOWN_FOR_UNKNOWN_BOOTUP:
            LOG_D(TAG, "SHUTDOWN_FOR_UNKNOWN_BOOTUP: Setting Upper limit of rtc_time to LPW cycle duration");
            expected_rtc_time = get_lpw_cycle_duration(true);
            break;

        case SHUTDOWN_FOR_LOWPOWER_WAKEUP:
            if (POWER_MONITOR_ctx->lowpower_wakeups < POWER_MONITOR_ctx->max_lowpower_wakeups) {
                LOG_D(TAG, "SHUTDOWN_FOR_LOWPOWER_WAKEUP: Setting Upper limit of rtc_time to LPW cycle duration");
                expected_rtc_time = get_lpw_cycle_duration(true);
            } else {
                expected_rtc_time = DEFAULT_RTC_TIME_MAX_LPW;
                max_lpw_detected = true;
                LOG_C(TAG, "POWER_MONITOR_ctx->lowpower_wakeups %d = POWER_MONITOR_ctx->max_lowpower_wakeups %d",
                        POWER_MONITOR_ctx->lowpower_wakeups, POWER_MONITOR_ctx->max_lowpower_wakeups);
                LOG_C(TAG, "Reached max low power wakeups; shouldn't wakeup anymore in low power. Setting upper limit of rtc_time to %d", expected_rtc_time);
            }
            break;

        case SHUTDOWN_FOR_UPLOADER_DATA_UPLOAD_DONE:
            LOG_D(TAG, "SHUTDOWN_FOR_UPLOADER_DATA_UPLOAD_DONE: Setting Upper limit of rtc_time to LPW cycle duration");
            expected_rtc_time = get_lpw_cycle_duration(true);
            break;

        case SHUTDOWN_FOR_SDCARD_RO_RECOVERY:
            LOG_D(TAG, "SHUTDOWN_FOR_SDCARD_RO_RECOVERY: Setting Upper limit of rtc_time to safety wakeup time");
            expected_rtc_time = POWER_MONITOR_ctx->safety_wakeup;
            break;

        case SHUTDOWN_FOR_SVC_REBOOT:
            LOG_D(TAG, "SHUTDOWN_FOR_SVC_REBOOT: Setting Upper limit of rtc_time to safety wakeup time");
            expected_rtc_time = POWER_MONITOR_ctx->safety_wakeup;
            break;

        case SHUTDOWN_FOR_INSTALLER_APP:
            LOG_D(TAG, "SHUTDOWN_FOR_INSTALLER_APP: Setting Upper limit of rtc_time to safety wakeup time");
            expected_rtc_time = POWER_MONITOR_ctx->safety_wakeup;
            break;

        case SHUTDOWN_FOR_INSTALLER_APP_CRASH:
            LOG_D(TAG, "SHUTDOWN_FOR_INSTALLER_APP_CRASH: Setting Upper limit of rtc_time to safety wakeup time");
            expected_rtc_time = POWER_MONITOR_ctx->safety_wakeup;
            break;

        case SHUTDOWN_FOR_CAM_CRASH :
            LOG_D(TAG, "SHUTDOWN_FOR_CAM_CRASH: Setting Upper limit of rtc_time to safety wakeup time");
            expected_rtc_time = POWER_MONITOR_ctx->safety_wakeup;
            break;


        case SHUTDOWN_FOR_AWSIOT:
            LOG_D(TAG, "SHUTDOWN_FOR_AWSIOT: Setting Upper limit of rtc_time to safety wakeup time");
            expected_rtc_time = POWER_MONITOR_ctx->safety_wakeup;
            break;

        case SHUTDOWN_FOR_ANALYTICS:
            LOG_D(TAG, "SHUTDOWN_FOR_ANALYTICS: Setting Upper limit of rtc_time to safety wakeup time");
            expected_rtc_time = POWER_MONITOR_ctx->safety_wakeup;
            break;

        case SHUTDOWN_FOR_MSP_FAIL_REBOOT:
            LOG_D(TAG, "SHUTDOWN_FOR_MSP_FAIL_REBOOT: Setting Upper limit of rtc_time to safety wakeup time");
            expected_rtc_time = POWER_MONITOR_ctx->safety_wakeup;
            break;
        case SHUTDOWN_FOR_MISC_LOWPOWER_WAKEUP:
            if (POWER_MONITOR_ctx->lowpower_wakeups < POWER_MONITOR_ctx->max_lowpower_wakeups) {
                LOG_D(TAG, "SHUTDOWN_FOR_MISC_LOWPOWER_WAKEUP (%s): Setting Upper limit of rtc_time to LPW cycle duration", PowerOnTriggerT::toString(POWER_MONITOR_ctx->wakeup_reason).c_str());
                expected_rtc_time = get_lpw_cycle_duration(true);
            } else {
                expected_rtc_time = DEFAULT_RTC_TIME_MAX_LPW;
                max_lpw_detected = true;
                LOG_C(TAG, "POWER_MONITOR_ctx->lowpower_wakeups %d = POWER_MONITOR_ctx->max_lowpower_wakeups %d",
                        POWER_MONITOR_ctx->lowpower_wakeups, POWER_MONITOR_ctx->max_lowpower_wakeups);
                LOG_C(TAG, "Reached max low power wakeups; shouldn't wakeup anymore in low power. Setting upper limit of rtc_time to %d", expected_rtc_time);
            }
            break;
        case SHUTDOWN_FOR_DHUB_STATUS_CHECK_DONE:
            LOG_D(TAG, "SHUTDOWN_FOR_DHUB_STATUS_CHECK_DONE: Setting Upper limit of rtc_time to LPW cycle duration");
            expected_rtc_time = get_lpw_cycle_duration(true);
            break;

        default:
            LOG_C(TAG, "Unknown shutdown/reboot reason %d, Setting upper rtc_time to safety wakeup time", reason);
            expected_rtc_time = POWER_MONITOR_ctx->safety_wakeup;
            break;
    }

    if(POWER_MONITOR_ctx->lowpower_wakeups >= POWER_MONITOR_ctx->max_lowpower_wakeups) {
        expected_rtc_time = DEFAULT_RTC_TIME_MAX_LPW;
        max_lpw_detected = true;
        LOG_C(TAG, "Reached max low power wakeups; shouldn't wakeup anymore in low power. Setting upper limit of rtc_time to %d", expected_rtc_time);
    }

    if ((rtc_time < 0) || (rtc_time > expected_rtc_time) || (true == max_lpw_detected)) {
        LOG_C(TAG, "rtc_time(%d) is not in valid range, Setting it to expected_rtc_time(%d)", rtc_time, expected_rtc_time);
        rtc_time = expected_rtc_time;
    }
    LOG_I(TAG, "expected_rtc_time: %d, rtc_time: %d", expected_rtc_time, rtc_time);
}

/* First checking the shutdown reason again before checking the ignition source status to avoid unnecessary ignition status check if shutdown reason got changed in between
 * Before shutdown command is issued, this function will be called to check whether the ignition source status is changed or not during shutdown flow.
 * If ignition source status is changed, then it will return true which means the shutdown flow should be skip. Otherwise, it will return false which means the shutdown flow can proceed.
 */
bool ignition_on_shutdown_abort(int ign_src_stat, power_monitor_shutdown_reason reason) {

    // If ign_src_stat is 0 means all ignition sensor is in off state.
    // Now while checking the ign_src_stat for ign_check_duration.
    // If it changes to non-zero value, it means ignition source status got changed during shutdown flow, hence we should skip the shutdown/suspend actions.
    // If it remains 0, it means ignition source status is not changed during shutdown flow, hence we can proceed with shutdown/suspend actions.
    // If ign_src_stat is non-zero means shutdown reason is not related to ignition off, so no need to check the ign_src_stat during shutdown flow, just proceed with shutdown/suspend actions.
    constexpr int IGN_SRC_STAT_OFF = 0;
    if(IGN_SRC_STAT_OFF == ign_src_stat) {
        // Check ign_src_stat
        int l_ign_src_stat = ign_src_stat;

        for(int cnt = 0; cnt < POWER_MONITOR_ctx->ign_check_duration; ++cnt) {
            read_from_sysfs_entry(get_sysfs_path_from_enum(PowermonParam::eIGN_SRC_STAT), l_ign_src_stat);
            LOG_I(TAG, "Ignition Src status during shutdown flow check cnt: %d, ign_src_stat: 0x%02x", cnt, l_ign_src_stat);
            sleep(1);
        }

        if(l_ign_src_stat != ign_src_stat) {
            LOG_C(TAG, "Ignition Src status changed during shutdown flow; skip shutdown. Initial ign_src_stat: 0x%02x, Current ign_src_stat: 0x%02x", ign_src_stat, l_ign_src_stat);
            return true;
        }
    }

    pthread_mutex_lock(&POWER_MONITOR_ctx->power_state_mutex);
    power_monitor_shutdown_reason latest_reason = POWER_MONITOR_ctx->power_state.reason;
    pthread_mutex_unlock(&POWER_MONITOR_ctx->power_state_mutex);

    if(latest_reason != reason) {
        LOG_C(TAG, "Shutdown reason got changed from %d to %d, hence not proceeding with shutdown/suspend actions for reason %d", reason, latest_reason, reason);
        return true;
    }

    return false;
}

void* shutdown_poling_thread_fn(void* args)
{

    LOG_I(TAG, "Inside shutdown_poling_thread_fn");
    int64_t present_time, shutdown_time;
    int shutdown_count = -1;

    bool last_reason_set = false;
    power_monitor_shutdown_reason last_reason = SHUTDOWN_CANCELLED;

    int64_t start_time_uploader_activity = 0;
    int64_t next_wakeup_time_sec = 0;

    bool failed_delete_extend_misc_file = false;

    stringstream command;
    FILE *fp=NULL;
    while(1) {
        pthread_mutex_lock ( &keepalive_status_mutex );
        all_thread_keepalive_status &= ~0x4;      // make 3rd bit 0 for this thread  
        pthread_mutex_unlock ( &keepalive_status_mutex );
        present_time = get_system_monotonic_time();
        pthread_mutex_lock(&POWER_MONITOR_ctx->power_state_mutex);
        shutdown_time = POWER_MONITOR_ctx->power_state.monotonic_time + POWER_MONITOR_ctx->power_state.time_gap*1000;
        power_monitor_shutdown_reason reason = POWER_MONITOR_ctx->power_state.reason;
        pthread_mutex_unlock(&POWER_MONITOR_ctx->power_state_mutex);
        command.str("");

        if(shutdown_count%(ND_MAX(1, 60/SHUTDOWN_TH_SLEEP_CYCLE_DURATION)) == 0) {
            LOG_I(TAG, "POWER_MONITOR_ctx->power_state.monotonic_time %lld", POWER_MONITOR_ctx->power_state.monotonic_time);
            LOG_I(TAG, "POWER_MONITOR_ctx->power_state.reason %d", POWER_MONITOR_ctx->power_state.reason);
            LOG_I(TAG, "POWER_MONITOR_ctx->power_state.time_gap %d", POWER_MONITOR_ctx->power_state.time_gap);
            if(!POWER_MONITOR_ctx->ps_obj.checkPowerStateMatch()) { // Monitoring every 60 seconds
                applyPowerState(PowerStateEvent::eNormalOperatingEvent, true); // Applying eNormalOperatingEvent will match the correct power state
            }
        }

        if( present_time > shutdown_time) {
            LOG_I(TAG, " present_time %lld >  shutdown_time %lld", present_time, shutdown_time);

            // This if condition is to handle the shutdown, all the enums
            // mentioned in if condition will be resulting in shutdown.
            if( (reason ==  SHUTDOWN_FOR_BAD_VOLTAGE)  ||
                    (reason ==  SHUTDOWN_FOR_IGNITION_OFF) ||
                    (reason ==  SHUTDOWN_FOR_UNKNOWN_BOOTUP) ||
                    (reason ==  SHUTDOWN_FOR_LOWPOWER_WAKEUP) ||
                    (reason ==  SHUTDOWN_FOR_UPLOADER_DATA_UPLOAD_DONE) ||
                    (reason ==  SHUTDOWN_FOR_MISC_LOWPOWER_WAKEUP) ||
                    (reason ==  SHUTDOWN_FOR_DHUB_STATUS_CHECK_DONE) ||
                    (reason ==  POSTPONE_FOR_UPLOADER_ACTIVITY) ||
                    (reason ==  POSTPONE_FOR_DHUB_STATUS_CHECK) ||
                    (reason == POSTPONE_FOR_MISC_LOWPOWER_WAKEUP)) {

                if(POSTPONE_FOR_MISC_LOWPOWER_WAKEUP == reason) {
                    reason = SHUTDOWN_FOR_MISC_LOWPOWER_WAKEUP; // Change reason to SHUTDOWN_FOR_MISC_LOWPOWER_WAKEUP for further processing after postpone
                }

                int ign_src_stat = 0;
                read_from_sysfs_entry(get_sysfs_path_from_enum(PowermonParam::eIGN_SRC_STAT), ign_src_stat);
                LOG_I(TAG, "ign_src_stat: 0x%02x", ign_src_stat);

                if (reason == SHUTDOWN_FOR_MISC_LOWPOWER_WAKEUP) {
                    LOG_I(TAG, "WOM Trigger set for MISC_WAKEUP reason");
                    write_into_sysfs_entry(nd_factory_utils::get_wom_trigger_sysfs_path(), static_cast<int>(MISC_WAKEUP_TRUE));

                    // Check and delete the extend_misc_file if present
                    if((true == file_is_present(extend_misc_file)) && (false == failed_delete_extend_misc_file)) {

                        failed_delete_extend_misc_file = true;
                        // delete the extend_misc_file
                        if(false == file_delete(extend_misc_file)) {
                            LOG_C(TAG, "Failed to delete file: %s", extend_misc_file.c_str());
                        }
                        else {
                            LOG_C(TAG, "Deleted file: %s", extend_misc_file.c_str());
                        }

                        constexpr int MISC_EXTENTION__DURATION = 180; // 3 mins

                        POWER_MONITOR_ctx->shutdown_delayed = true;
                        // To avoid uploader shutdown postpone due to misc lowpower wakeup
                        POWER_MONITOR_ctx->delay_shutdown_for_misc_lowpower_wakeup = true;
                        postpone_shutdown(MISC_EXTENTION__DURATION, POSTPONE_FOR_MISC_LOWPOWER_WAKEUP);
                        continue;
                    }
                }

                // Shutdown device if the uploader activity reached the limit
                if ((true == POWER_MONITOR_ctx->extended_post_ignition_off_and_lpw_timer) && (true == POWER_MONITOR_ctx->uploader_data_upload_pending) && (POSTPONE_FOR_UPLOADER_ACTIVITY == reason)) {
                    int64_t end_time_uploader_activity = MS_TO_S(get_system_monotonic_time()); // sec
                    int64_t time_diff_uploader_activity = end_time_uploader_activity - start_time_uploader_activity;
                    LOG_I(TAG, "time_diff b/w end_time_uploader_activity and start_time_uploader_activity: %lld", time_diff_uploader_activity);

                    if (time_diff_uploader_activity >= POWER_MONITOR_ctx->max_postpone_shutdown_time_uploader_activity) {
                        LOG_I(TAG, "Uploader activity reached the limit, shutdown the device");
                        reason = SHUTDOWN_FOR_UPLOADER_DATA_UPLOAD_DONE;
                        POWER_MONITOR_ctx->uploader_data_upload_pending = false;
                        if(true == POWER_MONITOR_ctx->ext_cam_lpw_enabled) {
                            // Update rtc_wakeup_time with new wakeup time
                            LOG_I(TAG, "Updating rtc_wakeup_time with g_dhub_wakeup_time: %lld, As uploader activity reached the limit", g_dhub_wakeup_time);
                            rtc_wakeup_time = g_dhub_wakeup_time;
                        }
                    }

                    // It is only for Non DHUB minitoring of LPW cycle in max_postpone_shutdown_time_uploader_activity duration
                    if(false == POWER_MONITOR_ctx->ext_cam_lpw_enabled) {
                        // monitor wakeup time
                        // and if wakeup time is less then current time then update wakeup time in DB
                        // add shutdown event also
                        int64_t time_diff_next_wakeup_time = (time_diff_uploader_activity - next_wakeup_time_sec);
                        LOG_I(TAG, "time_diff_next_wakeup_time: %lld", time_diff_next_wakeup_time);

                        if (time_diff_next_wakeup_time > 0) {

                            // send critical info
                            string msg = "Continuing in Lowpower Mode due to Uploader Activity";
                            int lpw_count = POWER_MONITOR_ctx->lowpower_wakeups + 1;
                            LOG_C(TAG, "%s, lpw_count: %d", msg.c_str(), lpw_count);
                            nd_service_obj->send_err_msg(SM_E_PM_RTC_WAKEUP_TIME, lpw_count, msg);

                            time_sync_low_power_wakeup_cnt_msg_t m;
                            m.low_power_wakeup_cnt = lpw_count;
                            send_msg( (generic_msg_t *)&m, LOW_POWER_WAKEUP_CNT_UPDATE, sizeof(m), POWER_MONITOR_ctx->power_monitor_q_name, QNAME_TIME_SYNC, 0 );

                            // Update POWER_MONITOR_ctx->lowpower_wakeups with lpw_count
                            POWER_MONITOR_ctx->lowpower_wakeups = lpw_count;

                            // Add wakeup time in DB
                            int64_t new_wakeup_time = rtc_wakeup_time + S_TO_MS((int64_t)POWER_MONITOR_ctx->lowpower_wakeup_duration) + S_TO_MS((int64_t)get_lpw_cycle_duration());
                            LOG_I(TAG, "new_wakeup_time: %lld", new_wakeup_time);

                            // WAKEUP ENTRY
                            if ( false == add_event_db(POWER_MONITOR_ctx->db_handle, new_wakeup_time, (POWER_MONITOR_ctx->boot_time + time_diff_next_wakeup_time),
                                    POWER_MONITOR_ctx->pid_num, "DBSTATE_RTC_WAKEUP_TIME_DEVICE", "WAKEUP")) {
                                LOG_E(TAG, "Failed to add rtc wakeup time for device to db");
                            }

                            // If POWER_MONITOR_ctx->lowpower_wakeups is less than 1, No need to update shutdown entry as coming from crank off.
                            if(POWER_MONITOR_ctx->lowpower_wakeups > 1) {
                                int64_t new_shutdown_time = rtc_wakeup_time + S_TO_MS((int64_t)POWER_MONITOR_ctx->lowpower_wakeup_duration);
                                // SHUTDOWN ENTRY
                                if ( false == add_event_db(POWER_MONITOR_ctx->db_handle, new_shutdown_time, (POWER_MONITOR_ctx->boot_time + time_diff_next_wakeup_time),
                                        POWER_MONITOR_ctx->pid_num, "DBSTATE_SHUTDOWN_LOWPOWER", "SHUTDOWN")) {
                                    LOG_E(TAG, "Failed to add rtc shutdown time for device to db");
                                }
                            }

                            // DBSTATE_LOWPOWER_WAKEUP as we are doing config validation based on this entry
                            if(false == add_event_db(POWER_MONITOR_ctx->db_handle, rtc_wakeup_time, (POWER_MONITOR_ctx->boot_time + time_diff_next_wakeup_time),
                                POWER_MONITOR_ctx->pid_num, "DBSTATE_LOWPOWER_WAKEUP", "NA")) {
                                    LOG_E(TAG, "Failed to add DBSTATE_LOWPOWER_WAKEUP in DB");
                            }

                            // Update rtc_wakeup_time with new wakeup time
                            // This is used to set the RTC time for wakeup
                            rtc_wakeup_time = new_wakeup_time;

                            // TODO: Handle multiple LPW in uploader activity
                            next_wakeup_time_sec = MS_TO_S(rtc_wakeup_time - get_system_time());
                            LOG_C(TAG, "After updating new_wakeup_time, next_wakeup_time_sec: %lld", next_wakeup_time_sec);
                        }
                    }
                }

                /*delay shutdown if vods are pending or if vods just finished uploading and device is going to shutdown with prev reason(SHUTDOWN_FOR_IGNITION_OFF or SHUTDOWN_FOR_LOWPOWER_WAKEUP) before shutdown is initiated with reason SHUTDOWN_FOR_UPLOADER_DATA_UPLOAD_DONE */
                if(check_for_delay_shutdown(reason)) {
                    POWER_MONITOR_ctx->shutdown_delayed = true;

                    // Store the last reason for future use, to add proper event in DB
                    if (false == last_reason_set) {
                        last_reason = reason;
                        last_reason_set = true;
                    }

                    // if uploader_data_upload_pending is true and
                    // extended_post_ignition_off_and_lpw_timer is true, post
                    // ignition off, the shutdown will be extended for max_postpone_shutdown_time_uploader_activity

                    if ((true == POWER_MONITOR_ctx->extended_post_ignition_off_and_lpw_timer) && (true == POWER_MONITOR_ctx->uploader_data_upload_pending)) {
                        // store the start time of uploader data upload
                        start_time_uploader_activity = MS_TO_S(get_system_monotonic_time()); // sec
                        postpone_shutdown(POWER_MONITOR_ctx->max_postpone_shutdown_time_uploader_activity, POSTPONE_FOR_UPLOADER_ACTIVITY);
                        int64_t current_time = get_system_time();
                        next_wakeup_time_sec = MS_TO_S(rtc_wakeup_time - current_time);
                        LOG_C(TAG, "start_time_uploader_activity: %lld, next_wakeup_time_sec: %lld, rtc_wakeup_time: %lld, current_time: %lld", start_time_uploader_activity, next_wakeup_time_sec, rtc_wakeup_time, current_time);
                        // If next_wakeup_time_sec is less than lpw cycle duration - 60 then setting next_wakeup_time_sec to default cycle duration.
                        if(next_wakeup_time_sec < (get_lpw_cycle_duration() - 60) ||
                            (next_wakeup_time_sec > (get_lpw_cycle_duration() + 60))) {
                            next_wakeup_time_sec = get_lpw_cycle_duration();
                        }
                        LOG_C(TAG, "after validation next_wakeup_time_sec: %lld", next_wakeup_time_sec);
                        continue;
                    }
                    else if ((true == POWER_MONITOR_ctx->ext_cam_lpw_enabled) && (true == POWER_MONITOR_ctx->dhub_status_check_enabled)) {
                        static int64_t dhub_status_check_start_time = get_system_monotonic_time();
                        postpone_shutdown(DELAY_SHUT_FOR_DHUB_STATUS_CHECK, POSTPONE_FOR_DHUB_STATUS_CHECK);
                        // send message to main controller for DHUB status check
                        power_monitor_dhub_status_check_t dhub_status_msg;
                        dhub_status_msg.type = POWERMON_DHUB_STATUS_CHECK;
                        dhub_status_msg.start_time = dhub_status_check_start_time;
                        dhub_status_msg.len = sizeof(power_monitor_dhub_status_check_t);

                        nd_msgq_t::nd_msg_t msg((char *)&dhub_status_msg, sizeof(dhub_status_msg), false);
                        POWER_MONITOR_ctx->powmon_msg_q->send(msg, nd_msgq_t::ND_MSG_MED);
                        continue;
                    }
                    else {
                        LOG_C(TAG, "WARNING!!!! It Should be not here, POWER_MONITOR_ctx->extended_post_ignition_off_and_lpw_timer: %d, POWER_MONITOR_ctx->uploader_data_upload_pending: %d, POWER_MONITOR_ctx->ext_cam_lpw_enabled: %d, POWER_MONITOR_ctx->dhub_status_check_enabled: %d, reason: %d",
                                POWER_MONITOR_ctx->extended_post_ignition_off_and_lpw_timer,
                                POWER_MONITOR_ctx->uploader_data_upload_pending,
                                POWER_MONITOR_ctx->ext_cam_lpw_enabled,
                                POWER_MONITOR_ctx->dhub_status_check_enabled,
                                reason);
                    }
                }

                if ((SHUTDOWN_FOR_UPLOADER_DATA_UPLOAD_DONE == reason) || (SHUTDOWN_FOR_DHUB_STATUS_CHECK_DONE == reason)) {
                    // To store actual reason for shutdown if delayed shutdown because of uploader activity or dhub status check
                    reason = last_reason;
                }

                // configure RTC time before going for shutdown
                int64_t current_time = convert_epoch_format(get_system_time(), DigitsOfEpoch::eDigits_Seconds);
                int64_t l_rtc_wakeup_time = convert_epoch_format(rtc_wakeup_time, DigitsOfEpoch::eDigits_Seconds);
                int rtc_time = static_cast<int>(l_rtc_wakeup_time - current_time);
                LOG_I(TAG, "current_time: %lld, Device wakeup time: %lld", current_time, l_rtc_wakeup_time);
                validate_rtc_time(reason, rtc_time);
                LOG_I(TAG, "Device wakeup after %d seconds", rtc_time);

                int pwr_state;
                pwr_state = (reason == SHUTDOWN_FOR_BAD_VOLTAGE)*power_dbstate_enum_t::DBSTATE_SHUTDOWN_BADVOLTAGE;
                pwr_state += (reason == SHUTDOWN_FOR_IGNITION_OFF)*power_dbstate_enum_t::DBSTATE_SHUTDOWN_CRANKOFF;
                pwr_state += (reason == SHUTDOWN_FOR_UNKNOWN_BOOTUP)*power_dbstate_enum_t::DBSTATE_SHUTDOWN_UNKNOWN;
                pwr_state += (reason == SHUTDOWN_FOR_LOWPOWER_WAKEUP)*power_dbstate_enum_t::DBSTATE_SHUTDOWN_LOWPOWER;
                pwr_state += (reason == SHUTDOWN_FOR_UPLOADER_DATA_UPLOAD_DONE)*power_dbstate_enum_t::DBSTATE_SHUTDOWN_UPLOADER_UPLOAD_DONE;
                pwr_state += (reason == SHUTDOWN_FOR_MISC_LOWPOWER_WAKEUP)*power_dbstate_enum_t::DBSTATE_SHUTDOWN_MISC_LOWPOWER;

                add_event_db((power_dbstate_enum_t)pwr_state , "SHUTDOWN");
                int64_t time_taken_to_bootup = time_taken_to_bootup_device();
                if(rtc_time > 60){ // Only substracting if rtc_time is greater than 60
                    rtc_time -= time_taken_to_bootup;
                }
                // set RTC for wakeup
                if (false == configure_rtc(rtc_time)) {
                    LOG_E(TAG, "Failed to set RTC for wakeup");
                }

//TODO : FACTORY CLASS IMPLEMENTATION FOR SHUTDOWN AND SUSPEND
                bool to_suspend = true;
                if((true == POWER_MONITOR_ctx->suspend_mode) && (POWER_MONITOR_ctx->lowpower_wakeups < POWER_MONITOR_ctx->max_lowpower_wakeups)) {
                    to_suspend = true;
                } else {
                    command << "shutdown now";
                    to_suspend = false;
                }
                if(modem_shutdown())
                {
                    LOG_I(TAG,"Graceful Modem Shutdown Successful");
                }
                else
                {
                    LOG_I(TAG,"Graceful Modem Shutdown Failure");
                }
                nd_service_obj->send_err_msg(SM_E_PM_SVC_SHUTDOWN, NDService::UNUSED_ERR_AUX_CODE,
                        "Device Shutdown Initiated" );
                // try do normal shutdown
                sync_tt(MAX_SYNC_TIME, !(POWER_MONITOR_ctx->RO_registered) );

                if(true == ignition_on_shutdown_abort(ign_src_stat, reason)) {
                    continue;
                }

                if( !(POWER_MONITOR_ctx->RO_registered) ) {
                    if(to_suspend == false) {
                        file_fd_sync(PM_DB_PATH_NAME);
                        nd_device_obj->device_shutdown(TAG, command);
                    } else { // suspend
                        nd_device_obj->device_suspend();
                    }
                }
                if (!POWER_MONITOR_ctx->suspend_mode){
                    //do forcefull shutdown if needed or if RO happened
                    LOG_C(TAG, "initiating a POR shutdown");
                    file_fd_sync(PM_DB_PATH_NAME);
                    usleep(500*1000);
                    LOG_C(TAG, "Set gpio for POR OFF");

                    if(true == ignition_on_shutdown_abort(ign_src_stat, reason)) {
                        continue;
                    }

                    nd_device_obj->gpio_por_assert();
                    nd_service_obj->send_err_msg(SM_E_PM_POR_GPIO_FAIL, NDService::UNUSED_ERR_AUX_CODE, 
                                "gpio_por_assert failed; handle this" );
                    // wait here indefinetly to trigger sVC timeout
                }
                while(1) {
                    LOG_E(TAG, "waiting for svc timeout and watchdog");
                    sleep(10);
                }
            }
            // with this if condition all reboot related scenarios will be
            // handled.
            else if( reason ==  SHUTDOWN_FOR_CYCLIC_REBOOT || 
                    reason == SHUTDOWN_FOR_SDCARD_RO_RECOVERY ||
                    reason == SHUTDOWN_FOR_SVC_REBOOT ||
                    reason == SHUTDOWN_FOR_INSTALLER_APP ||
                    reason == SHUTDOWN_FOR_INSTALLER_APP_CRASH ||
                    reason == SHUTDOWN_FOR_CAM_CRASH ||
                    reason == SHUTDOWN_FOR_AWSIOT ||
                    reason == SHUTDOWN_FOR_ANALYTICS ||
                    reason == SHUTDOWN_FOR_MSP_FAIL_REBOOT) {

                int pwr_state;
                handleShutdownReason(reason, pwr_state);

                //add_event_db((power_dbstate_enum_t)pwr_state , "REBOOT");
                if( !(POWER_MONITOR_ctx->RO_registered))
                {
                    // configure RTC time before going for reboot
                    int64_t current_time = convert_epoch_format(get_system_time(), DigitsOfEpoch::eDigits_Seconds);
                    int64_t l_rtc_wakeup_time = convert_epoch_format(rtc_wakeup_time, DigitsOfEpoch::eDigits_Seconds);
                    LOG_I(TAG, "current_time: %lld, Device wakeup time: %lld", current_time, l_rtc_wakeup_time);
                    int rtc_time = static_cast<int>(l_rtc_wakeup_time - current_time);
                    validate_rtc_time(reason, rtc_time);

                    LOG_I(TAG, "Device wakeup after %d seconds", rtc_time);
                    if(false == configure_rtc(rtc_time)) {
                        LOG_E(TAG, "Failed to set RTC for wakeup");
                    }

                    limited_system_reboot(max_B2B_reboot_allowed, pwr_state);
                    LOG_I (TAG,"shutdown for reason - %d in crank: %d", reason, POWER_MONITOR_ctx->present_crank_level );

                    bool reboot_status = system_reboot(REBOOT_TASK_TIMEOUT, true);
                    if(reboot_status == true) {
                        LOG_I(TAG, "reboot_status == true; wait for a while to get killed");
                        sleep(5);
                    }
                    string str_msg = "System reboot didnot occur, not expected to be here reboot_status " +
                        std::to_string(reboot_status);
                    LOG_C(TAG, str_msg.c_str());
                    nd_service_obj->send_err_msg(SM_E_PM_SYS_RBT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
                    LOG_C(TAG, "initiate_reboot timed task returned");
                }
                else {
                    add_event_db((power_dbstate_enum_t)pwr_state , "REBOOT");
                }
                sleep(2); // sleep for a while to log health DATA
                sync_tt(REBOOT_TASK_TIMEOUT, false);
                LOG_C(TAG, "initiating a POR shutdown");
                bool rtc_status = configure_rtc(POR_REBOOT_TIME);
#ifdef KRAIT
                if(rtc_status == false) {
                    LOG_C(TAG, "Cannot set RTC timer status: %d ; not rebooting", rtc_status);
                    continue;
                }
#endif
                LOG_I(TAG, "Set RTC timer status: %d", rtc_status);
                file_fd_sync(PM_DB_PATH_NAME);
                usleep(500*1000);
                LOG_C(TAG, "Set gpio for POR OFF");
                nd_device_obj->gpio_por_assert();
                nd_service_obj->send_err_msg(SM_E_PM_POR_GPIO_FAIL, NDService::UNUSED_ERR_AUX_CODE, 
                        "gpio_por_assert failed; handle this" );
                // wait here indefinetly to trigger sVC timeout
                while(1) {
                    LOG_E(TAG, "waiting for svc timeout and watchdog");
                    sleep(10);
                }
            }
            else if( reason == POSTPONE_FOR_IGNITION_ON ||
                    reason == POSTPONE_FOR_NORMAL_RUN ) {
                command << "shutdown --no-wall -c";
            }

            LOG_C(TAG, "########shutdown command ::%s::########", command.str().c_str());
	        fp = popen(command.str().c_str(), "r");
            if (NULL == fp) {
                LOG_E(TAG, "Failed to execute command in  shutdown_poling_thread_fn ::%s::" , command.str().c_str() );
                continue;
            }
            pclose(fp);
        }
        else {
            // reset the last reason set flag
            if ((reason != POSTPONE_FOR_DHUB_STATUS_CHECK) && (reason != POSTPONE_FOR_UPLOADER_ACTIVITY)) {
                last_reason_set = false;
                dhub_status_check_count = 0;
            }

            if(shutdown_count%(ND_MAX(1, 60/SHUTDOWN_TH_SLEEP_CYCLE_DURATION)) == 0) {
                LOG_I(TAG, " present_time %lld <  shutdown_time %lld cancelling shutdown", present_time, shutdown_time);
                LOG_I(TAG, "Setting RTC time for safety wakeup");
                // set RTC time as safety wakeup time
                if ( false == configure_rtc(POWER_MONITOR_ctx->safety_wakeup)) {
                    LOG_E(TAG, "Failed to set RTC time for safety wakeup");
                }
            }
            // initiate shutdown --no-wall -c only once in every 30 seconds 
            if(shutdown_count%(ND_MAX(1, 30/SHUTDOWN_TH_SLEEP_CYCLE_DURATION)) == 0) {
                command << "shutdown --no-wall -c";
                fp = popen(command.str().c_str(), "r");
                if (fp == NULL) {
                    string str_msg = "Failed to execute command in  shutdown_poling_thread_fn ::" + command.str() + "::";
                    LOG_C(TAG, str_msg.c_str());
                    nd_service_obj->send_err_msg(SM_E_PM_SYS_SHDN_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
                    continue;
                }
                pclose(fp);
            }

            if((true == POWER_MONITOR_ctx->ext_cam_lpw_enabled) && (CRANK_LOW == POWER_MONITOR_ctx->present_crank_level)) {
                bool status = isMDVRConnected();
                int64_t first_on_time = 0;
                int64_t dhub_current_time = 0;
                getDHUBRTCTime(first_on_time, dhub_current_time);
                int64_t dhub_uptime = dhub_current_time - first_on_time;

                int dhub_ign = -1;
                getDHUBIgnitionStatus(dhub_ign);
                if(true == status){
                    POWER_MONITOR_ctx->last_connected_time_dhub = get_system_monotonic_time();
                }

                if((status != POWER_MONITOR_ctx->dhub_connected_status) || (dhub_ign != POWER_MONITOR_ctx->dhub_ign_status))
                {
                    POWER_MONITOR_ctx->dhub_connected_status = status;
                    POWER_MONITOR_ctx->dhub_ign_status = dhub_ign;
                    string status_msg = "MDVR Stat: " + to_string(status) +
                                        ", Uptime: " + to_string(dhub_uptime) +
                                        ", Ign: " + to_string(dhub_ign);
                    nd_service_obj->send_err_msg(SM_E_PM_DHUB_STATUS, status, status_msg);
                }

                if(shutdown_count%(ND_MAX(1, 30/SHUTDOWN_TH_SLEEP_CYCLE_DURATION)) == 0){
                    add_or_update_event_db_event_time(power_dbstate_enum_t::DBSTATE_DHUB_LAST_CONNECTED_TIME, POWER_MONITOR_ctx->last_connected_time_dhub);
                }
                LOG_I(TAG,"isMDVRConnected: %d, time: %lld, dhub_uptime: %lld, dhub_ign: %d", status, POWER_MONITOR_ctx->last_connected_time_dhub, dhub_uptime, dhub_ign);
            }

            // monitor audio requests every 10 seconds
            if(shutdown_count%(ND_MAX(1, 10/SHUTDOWN_TH_SLEEP_CYCLE_DURATION)) == 0) {
                // Monitoring Audio Requests
                monitor_audio_requests(AUDIO_PLAY_THRESHOLD, true);
            }
        }
        sleep(SHUTDOWN_TH_SLEEP_CYCLE_DURATION);
        shutdown_count++;
    }
    LOG_E(TAG, "Exiting from shutdown_poling_thread_fn");
    return NULL;
}
#define SUPERCAP_ACTIVE 0
#define BATTERY_ACTIVE 1

#define FAILURE -1

void* keepalive_powerstate_thread_fn(void* args)
{
    LOG_I(TAG, "entered into keepalive_powerstate_thread_fn");

    if(deviceid == ""){
        LOG_E(TAG, "Error in reading deviceId");
        return NULL;
    }
    
    if(devicetype == ""){
        LOG_E(TAG, "Error in reading devicetype; Exiting from keepalive-powermonitor");
        return NULL;
    }

    if(otaversion == ""){
        LOG_E(TAG, "Error in reading otaversion; Exiting from keepalive-powermonitor");
        return NULL;
    }

    // Set the thread to a higher priority so that it can run more frequently
    // This is to ensure that the keepalive thread runs smoothly and can send updates to the cloud without delays.
    // The nice value is set to -5, which is a higher priority
    pid_t tid = syscall(SYS_gettid);
    int old_nice_val = getpriority(PRIO_PROCESS, tid);
    int set_nice_val = old_nice_val - DECREASE_SUPERCAP_THREAD_NICE_VALUE_BY;
    if(set_nice_val < MIN_NICE_VALUE_THREAD) set_nice_val = MIN_NICE_VALUE_THREAD; // limit nice value to -15
    setpriority(PRIO_PROCESS, tid, set_nice_val);
    int new_nice_val = getpriority(PRIO_PROCESS, tid);
    LOG_I(TAG, "keepalive_powerstate_thread_fn with tid %d, nice value changed from %d to %d", tid, old_nice_val, new_nice_val);

    string keepalive_url = server_url + "/" + version + "/" + KEEPALIVE_URL + "/" + devicetype 
                            + "/" + deviceid + "/" + otaversion + "/";

    // sleep for 10 seconds so that good chance we get connectivity and make this call
    sleep(10);
    int ka_retry_cnt = 0;
    volatile power_crank_levels_t previous_crank_change_keepalive =  CRANK_ERROR;

    // Set clock to monotonic
    pthread_condattr_init(&m_attr);
    pthread_condattr_setclock(&m_attr, CLOCK_MONOTONIC);
    pthread_cond_init(&m_cond, &m_attr);

    struct timespec ts = {0};

    while(1){

        pthread_mutex_lock(&m_mutex);

        if ( FAILURE == clock_gettime(CLOCK_MONOTONIC, &ts)) {
            LOG_E(TAG, "clock_gettime failed, setting the wait time to 0 so that pthread_cond_timedwait will unblock immediately");
            // set the time to 0 so pthread_cond_timedwait will unblock immediately
            ts.tv_sec = 0;
            ts.tv_nsec = 0;
        }
        else {
            ts.tv_sec += KEEPALIVE_SLEEP_DURATION;
        }

        if ( ETIMEDOUT == pthread_cond_timedwait(&m_cond, &m_mutex, &ts) ) {
            LOG_D(TAG, "cv Timeout. POWER_MONITOR_ctx->crank_change_keepalive: %d", POWER_MONITOR_ctx->crank_change_keepalive );
        }
        else {
            LOG_C(TAG, "cv finished waiting. POWER_MONITOR_ctx->crank_change_keepalive: %d", POWER_MONITOR_ctx->crank_change_keepalive );
            ka_retry_cnt = 0;
        }
        pthread_mutex_unlock(&m_mutex);

        if(POWER_MONITOR_ctx->crank_change_keepalive != CRANK_ERROR){
            LOG_C(TAG, "Change in power state; notifying to cloud ka_retry_cnt: %d", ka_retry_cnt);

            stringstream gps_stream;
            float avg_speed = get_last_average_speed(NUM_AVG_SPEED_SAMPLES);
            gps_stream << ", \"currentLocation\": { \"lat\": " << POWER_MONITOR_ctx->gps_pos.lat <<
                        ", \"long\":  " << POWER_MONITOR_ctx->gps_pos.lon <<
                        ", \"speed\": " << avg_speed <<
                        " , \"time_stamp\": "<< POWER_MONITOR_ctx->gps_pos.timestamp <<
                        ", \"valid\": " << POWER_MONITOR_ctx->gps_pos.valid <<
                        ", \"accuracy\": " << POWER_MONITOR_ctx->gps_pos.accuracy <<
                        " } " ;

            string gps_string = gps_stream.str();

            string power_state = (POWER_MONITOR_ctx->crank_change_keepalive == CRANK_HIGH) ? "1" : "0";
            string prev_shutdown_reason(POWER_MONITOR_ctx->previous_shutdown_reason);

            LOG_D(TAG, "prev_shutdown_reason: %s", prev_shutdown_reason.c_str() );

            string auth_header = "";
            bool header_status = get_auth_header(auth_header);
            if(!header_status) {
                log_auth_error("Corrupted jwt, Proceeding anyway for KA call...");
            }
            if(CRANK_SUPERCAP == POWER_MONITOR_ctx->crank_change_keepalive) {
                prev_shutdown_reason.append(" : SUPERCAP_ACTIVE");
            } else {
                prev_shutdown_reason.append(" : BATTERY_ACTIVE");
            }
#if 0
            // Read battery voltage
            float battery_voltage = KA_MINIFIED_MIN_VOLTAGE;
            if((POWER_MONITOR_ctx->current_voltage >= KA_MINIFIED_MIN_VOLTAGE) &&
                (POWER_MONITOR_ctx->current_voltage <= KA_MINIFIED_MAX_VOLTAGE)) {
                battery_voltage = POWER_MONITOR_ctx->current_voltage;
            }else{
                LOG_E(TAG, "Battery voltage is out of range %f, send minimum %f", POWER_MONITOR_ctx->current_voltage, battery_voltage);
            }
            if((POWER_MONITOR_ctx->current_voltage >= KA_MINIFIED_MIN_VOLTAGE) &&
                (POWER_MONITOR_ctx->current_voltage <= KA_MINIFIED_MAX_VOLTAGE)){ // Not adding battery voltage to payload if voltage is out of range
                std::ostringstream batt_volt_stream;
                batt_volt_stream << std::fixed << std::setprecision(2) << battery_voltage;
                prev_shutdown_reason += " : " + batt_volt_stream.str() + "v";    // Append battery voltage to the previous shutdown reason
            }
            int supercap_event_count = 0;
            if(false == read_from_sysfs_entry(get_sysfs_path_from_enum(PowermonParam::eSUPERCAP_COUNT), supercap_event_count)) {
                LOG_E(TAG, "Failed to read supercap event count from sysfs");
            }
            prev_shutdown_reason += " : " + to_string(supercap_event_count); // Append supercap event count to the previous shutdown reason
#endif
            string keepalive_command = "";
            // Removed sudo from the keepalive_command as it was causing thread termination issues on the Krait device.
            keepalive_command = "pkill curl; pkill wget; nice --15 wget  -O " + KEEPALIVE_RESPONSE_FILE + 
                " --header=\"Content-Type: application/json\" --header=\"X-DeviceType: " + devicetype + "\" " + 
                " --header=\"" + auth_header + "\" --header=\"X-DeviceId: " + deviceid + "\"" +
                " --post-data=\"{\\\"ignition\\\":" + power_state +
                ",\\\"previousShutdownReason\\\":\\\"" + prev_shutdown_reason + "\\\"" +
                gps_string +
                 "}\" " + keepalive_url ;

            bool ka_cert_check_disabled = is_ka_cert_check_disabled();
            if(ka_cert_check_disabled) {
                keepalive_command += " --no-check-certificate";
            }
            LOG_I(TAG, "is_ka_cert_check_disabled = %d", ka_cert_check_disabled);

            string ka_minified ="\\\"ignition\\\":" + power_state + 
                ",\\\"previousShutdownReason\\\":\\\"" + prev_shutdown_reason + "\\\"" +
                gps_string;
            LOG_C(TAG, "ka_minified %s", ka_minified.c_str());

            printf("keepalive command :: %s \n", keepalive_command.c_str());
            FILE *fp;
            fp = popen(keepalive_command.c_str(), "r");
            if (fp == NULL) {
                LOG_E(TAG, "Failed to execute command in keepalive_command :: %s" ,
                         keepalive_command.c_str() );
            }
            else {
                pclose(fp);
            }
            ifstream response_file;
            response_file.open(KEEPALIVE_RESPONSE_FILE.c_str());
            string response_string = "";
            if(response_file.is_open()){
                response_file >> response_string;
                response_file.close();
            }
            // if success ignore else retry for a max of N times before giveup
            ka_retry_cnt++;
            bool reached_cloud = (response_string.find("\"response\":true") != string::npos);
            //cloud responce for success case response_string :{"response":true,"msg":"upload-logs"}:
            LOG_I(TAG, "response_string :%s: reached_cloud %d", response_string.c_str(), reached_cloud);
            if(reached_cloud || (ka_retry_cnt > MAX_ka_retry_cnt)) {
                POWER_MONITOR_ctx->crank_change_keepalive = CRANK_ERROR;
                ka_retry_cnt = 0;
            }

            if(response_string.find(RESPONSE_JWT_HEADER_MISSING) != string::npos ||
                    response_string.find(RESPONSE_JWT_SIG_INVALID) != string::npos ||
                    response_string.find(RESPONSE_JWT_ALG_INVALID) != string::npos) {
                notify_key_corruption();
            }

        }
    }
    return NULL;
}

void handle_negative_rtc(int &rtc_time)
{
    if(nd_service_obj)
    {
        nd_service_obj->send_err_msg(SM_E_PM_NEGATIVE_RTC, -1 , "RTC Time(-ve) - Next LPW Time Corrected");
    }

    rtc_time = (POWER_MONITOR_ctx->lowpower_wakeup_cycle_duration + POWER_MONITOR_ctx->lowpower_wakeup_duration) - (abs(rtc_time) % (POWER_MONITOR_ctx->lowpower_wakeup_cycle_duration + POWER_MONITOR_ctx->lowpower_wakeup_duration));

}
// isValidateDhubRTC is true, to be used only in validate_rtc_time() strictly
int get_lpw_cycle_duration(bool isValidateDhubRTC)
{
    if(true == POWER_MONITOR_ctx->freq_low_power_wakeup)
    {

       if (POWER_MONITOR_ctx->lowpower_wakeups <= freq_lpw_cycles[1] )
       {
           return POWER_MONITOR_ctx->lowpower_wakeup_cycle_duration;
       }
       else if (POWER_MONITOR_ctx->lowpower_wakeups <= freq_lpw_cycles[2])
       {
           return POWER_MONITOR_ctx->lowpower_wakeup_cycle_duration * 2;
       }
       else if (POWER_MONITOR_ctx->lowpower_wakeups <= freq_lpw_cycles[3])
       {
           return POWER_MONITOR_ctx->lowpower_wakeup_cycle_duration * 4;
       }
       else if (POWER_MONITOR_ctx->lowpower_wakeups <= freq_lpw_cycles[4])
       {
           return POWER_MONITOR_ctx->lowpower_wakeup_cycle_duration * 8;
       }
       else if (POWER_MONITOR_ctx->lowpower_wakeups > freq_lpw_cycles[4] && POWER_MONITOR_ctx->lowpower_wakeups <= freq_lpw_cycles[11])
       {
           return ONE_DAY_IN_SECONDS;
       }
    }
    else
    {
        if( POWER_MONITOR_ctx->lowpower_wakeups >= POWER_MONITOR_ctx->lowpower_wakeup_long_cycle_threshold)
        {
            // Incase of Long Cycle Duration DHUB will wakeup multiple times adding extra time according to possible lpw counts
            // isValidateDhubRTC is true, to be used only in validate_rtc_time() strictly
            if((true == isValidateDhubRTC) && (true == POWER_MONITOR_ctx->ext_cam_lpw_enabled) && (POWER_MONITOR_ctx->possible_lpw_count > 0)){
                return (POWER_MONITOR_ctx->lowpower_wakeup_long_cycle_duration + (POWER_MONITOR_ctx->possible_lpw_count - 1) * ((int64_t)dhub_wakeup_sync_extra + (int64_t)dhub_shutdown_sync_extra));
            }else{
                return POWER_MONITOR_ctx->lowpower_wakeup_long_cycle_duration;
            }
        }
        else
        {
            return POWER_MONITOR_ctx->lowpower_wakeup_cycle_duration;
        }
    }
}

void shutdownReasonContent(int& rtc_time, std::stringstream& content, std::string& msg, power_monitor_shutdown_reason reason, int& shutdown_after_secs)
{
    int speed = static_cast<int>(round(POWER_MONITOR_ctx->gps_pos.speed * 10) / 10.0);
    int shutdown_after_mins = ( shutdown_after_secs + 59 )/SECS_IN_A_MIN;
    msg.clear();
    switch (reason)
    {
        case SHUTDOWN_FOR_BAD_VOLTAGE:
            if (POWER_MONITOR_ctx->disableLowPowerwakeUp) {
                rtc_time = POWER_MONITOR_ctx->safety_wakeup_time_for_bad_voltage_shutdown;
                msg = "Shutdown - Bad Voltage (LPW: 0, Speed: " + to_string(speed)  + ")";
                content << "Shuttingdown, BAD BATTERY: Not entering LPW " << "\t";
            } else {
                rtc_time = get_lpw_cycle_duration();
                msg = "Shutdown - Bad Voltage (LPW: 1, Speed: " + to_string(speed)  + ")";
                content << "Shuttingdown because of SHUTDOWN_FOR_BAD_VOLTAGE " << "\t";
           }
           nd_service_obj->send_err_msg(SM_E_PM_BAD_VOLTAGE_SHDN, POWER_MONITOR_ctx->disableLowPowerwakeUp, msg);
            break;

        case SHUTDOWN_FOR_CYCLIC_REBOOT:
            rtc_time = POWER_MONITOR_ctx->safety_wakeup;
            msg = "Reboot - Cyclic";
            content << "restarting because of SHUTDOWN_FOR_CYCLIC_REBOOT " << "\t";
            nd_service_obj->send_err_msg(SM_E_PM_CYCLIC_REBOOT_SHUTDOWN, speed, msg);
            break;

        case SHUTDOWN_FOR_IGNITION_OFF:
            rtc_time = get_lpw_cycle_duration() + shutdown_after_secs;
            msg = "Shutting Down in " + to_string(shutdown_after_mins) + "(Min) - Ignition Off";
            content << "Shuttingdown because of SHUTDOWN_FOR_IGNITION_OFF " << "\t";

            if(true == POWER_MONITOR_ctx->freq_low_power_wakeup)
            {
                msg += " FLPW Enabled";
            }

            nd_service_obj->send_err_msg(SM_E_PM_IGNITION_OFF_SHUTDOWN, speed, msg);
            break;

        case SHUTDOWN_FOR_UNKNOWN_BOOTUP:
            rtc_time = get_lpw_cycle_duration();
            msg = "Rebooted - Unknown Bootup";
            content << "Shuttingdown because of SHUTDOWN_FOR_UNKNOWN_BOOTUP " << "\t";

            nd_service_obj->send_err_msg(SM_E_PM_UNKNOWN_BOOT_SHDN, speed, msg);
            break;

        case SHUTDOWN_FOR_LOWPOWER_WAKEUP:
            if (POWER_MONITOR_ctx->lowpower_wakeups < POWER_MONITOR_ctx->max_lowpower_wakeups) {
                rtc_time = get_lpw_cycle_duration() + shutdown_after_secs;
                if( (!POWER_MONITOR_ctx->freq_low_power_wakeup) && (POWER_MONITOR_ctx->lowpower_wakeups >= POWER_MONITOR_ctx->lowpower_wakeup_long_cycle_threshold) ) {
                    string str_msg = "Activating long low power wakeup after " + std::to_string(POWER_MONITOR_ctx->lowpower_wakeup_long_cycle_duration) + "seconds";
                    LOG_I(TAG, str_msg.c_str());
                    nd_service_obj->send_err_msg(SM_I_PM_LPW_COUNT_LPW_DURATION, POWER_MONITOR_ctx->lowpower_wakeups, str_msg );
                } else {
                    LOG_I(TAG, "POWER_MONITOR_ctx->lowpower_wakeups %d < POWER_MONITOR_ctx->max_lowpower_wakeups %d",
                            POWER_MONITOR_ctx->lowpower_wakeups, POWER_MONITOR_ctx->max_lowpower_wakeups);
                    LOG_I(TAG, "Activating low power wakeup for next time");

                    msg = "Shutting Down in " + to_string(shutdown_after_mins) +"(Min) - LPW (Speed: " + to_string(speed)  + ")";

                    if(true == POWER_MONITOR_ctx->freq_low_power_wakeup)
                    {
                        msg += " FLPW Enabled";
                    }

                    nd_service_obj->send_err_msg(SM_E_PM_LPW_SHUTDOWN, POWER_MONITOR_ctx->lowpower_wakeups, msg);
                }
            } else {
                rtc_time = DEFAULT_RTC_TIME_MAX_LPW;
                LOG_C(TAG, "POWER_MONITOR_ctx->lowpower_wakeups %d = POWER_MONITOR_ctx->max_lowpower_wakeups %d",
                        POWER_MONITOR_ctx->lowpower_wakeups, POWER_MONITOR_ctx->max_lowpower_wakeups);
                LOG_C(TAG, "Reached max low power wakeups; shouldn't wakeup anymore in low power");
            }
            content << "Shuttingdown because of SHUTDOWN_FOR_LOWPOWER_WAKEUP " << "\t";
            break;

        case SHUTDOWN_FOR_UPLOADER_DATA_UPLOAD_DONE:
            msg = "Data Upload Complete - Initiating Shutdown";
            content << "Initiating shutdown because of SHUTDOWN_FOR_UPLOADER_DATA_UPLOAD_DONE with last shutdown reason" << "\t";
            nd_service_obj->send_err_msg(SM_E_PM_UPLOADER_DATA_UPLOAD_DONE, uploader_data_upload_pend->pend_req, msg);
            break;

        case SHUTDOWN_FOR_SDCARD_RO_RECOVERY:
            rtc_time = POWER_MONITOR_ctx->safety_wakeup + shutdown_after_secs;
            msg = "Reboot Initiated - SD Card Recovery";
            content << "Shuttingdown because of SHUTDOWN_FOR_SDCARD_RO_RECOVERY " << "\t";

            nd_service_obj->send_err_msg(SM_E_PM_SDCARD_RO_RECOVERY_SHUTDOWN, speed, msg);
            break;

        case SHUTDOWN_FOR_SVC_REBOOT:
            rtc_time = POWER_MONITOR_ctx->safety_wakeup + shutdown_after_secs;
            msg = "Reboot Initiated - SVC";
            content << "Shuttingdown because of SHUTDOWN_FOR_SVC_REBOOT " << "\t";

            nd_service_obj->send_err_msg(SM_E_PM_SVC_SHUTDOWN, speed, msg);
            break;

        case SHUTDOWN_FOR_INSTALLER_APP:
            rtc_time = POWER_MONITOR_ctx->safety_wakeup + shutdown_after_secs;
            msg = "Reboot Initiated - Installer App";
            content << "Shuttingdown because of SHUTDOWN_FOR_INSTALLER_APP " << "\t";

            nd_service_obj->send_err_msg(SM_E_PM_INST_APP_SHUTDOWN, speed, msg);
            break;

        case SHUTDOWN_FOR_INSTALLER_APP_CRASH:
            rtc_time = POWER_MONITOR_ctx->safety_wakeup + shutdown_after_secs;
            msg = "Reboot Initiated - Installer App Crash";
            content << "Shuttingdown because of SHUTDOWN_FOR_INSTALLER_APP_CRASH " << "\t";

            nd_service_obj->send_err_msg(SM_E_PM_INST_APP_CRASH_SHUTDOWN, speed, msg);
            break;

        case SHUTDOWN_FOR_CAM_CRASH :
            rtc_time = POWER_MONITOR_ctx->safety_wakeup + shutdown_after_secs;
            msg = "Reboot Initiated - Camera Crash";
            content << "Shuttingdown because of SHUTDOWN_FOR_CAM_CRASH " << "\t";

            nd_service_obj->send_err_msg(SM_E_PM_OUTWARD_CAM_CRASH_SHUTDOWN, speed, msg);
            break;


        case SHUTDOWN_FOR_AWSIOT:
            rtc_time = POWER_MONITOR_ctx->safety_wakeup + shutdown_after_secs;
            msg = "Reboot Initiated - AWS IOT";
            content << "Shuttingdown because of SHUTDOWN_FOR_AWSIOT " << "\t";

            nd_service_obj->send_err_msg(SM_E_PM_AWSIOT_SHUTDOWN, speed, msg);
            break;

        case SHUTDOWN_FOR_ANALYTICS:
            rtc_time = POWER_MONITOR_ctx->safety_wakeup + shutdown_after_secs;
            msg = "Reboot Initiated - ANALYTICS";
            content << "Shuttingdown because of SHUTDOWN_FOR_ANALYTICS " << "\t";

            nd_service_obj->send_err_msg(SM_E_PM_ANALYTICS_SHUTDOWN, speed, msg);
            break;

        case SHUTDOWN_FOR_MSP_FAIL_REBOOT:
            rtc_time = POWER_MONITOR_ctx->safety_wakeup + shutdown_after_secs;
            msg = "Reboot Initiated - MSP Fail";
            content << "Shuttingdown because of SHUTDOWN_FOR_MSP_FAIL_REBOOT " << "\t";

            nd_service_obj->send_err_msg(SM_E_PM_MSP_FAIL_SHUTDOWN, speed, msg);
            break;
        case SHUTDOWN_FOR_MISC_LOWPOWER_WAKEUP:
            if (POWER_MONITOR_ctx->lowpower_wakeups < POWER_MONITOR_ctx->max_lowpower_wakeups) {
                rtc_time = get_lpw_cycle_duration() + shutdown_after_secs;
                msg = "Shutting Down in " + to_string(shutdown_after_mins) +"(Min) - Misc Wakeup(" + PowerOnTriggerT::toString(POWER_MONITOR_ctx->wakeup_reason) + ") LPW (Speed: " + to_string(speed)  + ")";
                content << "Shuttingdown because of SHUTDOWN_FOR_MISC_LOWPOWER_WAKEUP " << "\t";
                nd_service_obj->send_err_msg(SM_E_PM_MISC_LPW_SHUTDOWN, POWER_MONITOR_ctx->lowpower_wakeups, msg);
            }
            else {
                rtc_time = DEFAULT_RTC_TIME_MAX_LPW;
                LOG_C(TAG, "POWER_MONITOR_ctx->lowpower_wakeups %d = POWER_MONITOR_ctx->max_lowpower_wakeups %d",
                        POWER_MONITOR_ctx->lowpower_wakeups, POWER_MONITOR_ctx->max_lowpower_wakeups);
                LOG_C(TAG, "Reached max low power wakeups; shouldn't wakeup anymore in low power");
            }
            break;
        case SHUTDOWN_FOR_DHUB_STATUS_CHECK_DONE:
            msg = "DHUB Status Check Done - Initiating Shutdown";
            content << "Initiating shutdown because of SHUTDOWN_FOR_DHUB_STATUS_CHECK_DONE with last shutdown reason" << "\t";
            nd_service_obj->send_err_msg(SM_E_PM_DHUB_STATUS_CHECK_DONE_SHUTDOWN, speed, msg);
            break;

        default:
            LOG_E(TAG, "initiate_shutdown with unknown reason; TAKE ACTION HERE");
            msg = "Rebooted - Unknown Reason";
            content << "Shuttingdown because of UNKNOWN REASON " << "\t";

            nd_service_obj->send_err_msg(SM_E_PM_UNKNOWN_INIT_SHDN, speed, msg);
    }
}

bool initiate_shutdown(int shutdown_after_secs, power_monitor_shutdown_reason reason)
{
    if( reason >= SHUTDOWN_FOR_ERROR)
    {
        string str_msg = "ERROR :: received reason for shutdown " + reason;
        LOG_C(TAG, str_msg.c_str());
        nd_service_obj->send_err_msg(SM_E_PM_SYS_SHDN_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        return false;
    }

    if (true == POWER_MONITOR_ctx->ext_cam_lpw_enabled ) {
        // Not doing round off for shutdown_after_secs for sync with DHUB
        // Because it will effect on DHUB wakeup
        LOG_I(TAG, "ext_cam_feature_enabled is true, Not doing round off for shutdown_after_secs for sync with DHUB");

        // As we noticed DHUB is taking extra DEFAULT_TIME_DHUB_SHUTDOWN_SYNC seconds to shutdown
        // We are adding this time to shutdown_after_secs for device as well
        // if((SHUTDOWN_FOR_IGNITION_OFF ==  reason) || (SHUTDOWN_FOR_LOWPOWER_WAKEUP == reason)) {
        //     LOG_C(TAG, "ext_cam_feature_enabled is true, Adding %d seconds to shutdown_after_secs for sync with DHUB", DEFAULT_TIME_DHUB_SHUTDOWN_SYNC);
        //     shutdown_after_secs += DEFAULT_TIME_DHUB_SHUTDOWN_SYNC;
        // }
    }
    else {
        int shutdown_after_mins = ( shutdown_after_secs + 59 )/SECS_IN_A_MIN;
        shutdown_after_secs = shutdown_after_mins*SECS_IN_A_MIN;
    }

    LOG_I(TAG, "inside initiate_shutdown time %d secs", shutdown_after_secs);
    LOG_I(TAG, "reason for shutdown %d", reason);

    if(check_priority(reason, POWER_MONITOR_ctx->reason) == false){
        LOG_C(TAG, "cancelling this shutdown since check_priority returned false");
        return false;
    }

    FILE *fp=NULL;
    stringstream command;
    stringstream content;

    command.str("");

    int64_t time = get_system_time();
    content << "@ time " << time << "\t";

    std::string msg;
    int rtc_time = 0;

    shutdownReasonContent(rtc_time, content, msg, reason, shutdown_after_secs);
    pthread_mutex_lock(&POWER_MONITOR_ctx->power_state_mutex);
    POWER_MONITOR_ctx->power_state.monotonic_time = get_system_monotonic_time();
    POWER_MONITOR_ctx->power_state.time_gap = shutdown_after_secs;
    POWER_MONITOR_ctx->power_state.reason = reason;
    pthread_mutex_unlock(&POWER_MONITOR_ctx->power_state_mutex);

    power_health.sd_type = POWER_MONITOR_ctx->power_state.reason;
    power_health.duration = POWER_MONITOR_ctx->power_state.time_gap;

    json_t *root = json_object();
    json_t *power_info = json_object();
    json_t *power_health_analytics = json_object();
    char* req_params = NULL;

    json_object_set_new( power_info, "sd_type", json_integer(power_health.sd_type));
    json_object_set_new( power_info, "duration", json_integer(power_health.duration));
    json_object_set_new( power_health_analytics, "p_ha", power_info);
    json_object_set_new( root, "send_to_analytics", power_health_analytics);

    req_params = json_dumps(root, 0);
    if(req_params == NULL){
        LOG_E(TAG,"JSON creation failed for HS obs gen message");
        json_decref(root);
    }
    int length = strlen(req_params);
    nd_service_obj->send_msg_healthstats(req_params, length);
    json_decref(root);
    free(req_params);

    LOG_C(TAG, "shutdown reason ::%s::", content.str().c_str());

    int64_t time_present = get_system_time();
    int64_t shutdown_time = time_present + shutdown_after_secs*1000;

    POWER_MONITOR_ctx->shutdown_time = shutdown_time;
    POWER_MONITOR_ctx->reason = reason;
    content << "shutdown will happen at " << shutdown_time << " after " 
            << shutdown_after_secs <<" seconds" << "\t";

    if(reason == SHUTDOWN_FOR_IGNITION_OFF || reason == SHUTDOWN_FOR_LOWPOWER_WAKEUP) {
        int64_t diff = time_present - POWER_MONITOR_ctx->crank_low_registered_time;
        // We are storing crank_low_registered_time in process_crank_low function
        // It will take 10 ms - 100 ms to execute this function after processing crank_low event
        // Therefor, we are checking the difference between time_present and crank_low_registered_time
        // And limiting the difference to 1 seconds, to handle time jump issue

        const unsigned int max_diff = 1; // seconds
        if((diff > 0) && (diff < S_TO_MS(max_diff))) {
            LOG_I(TAG, "Reducing the rtc_time as time_present and crank_low_registered_time difference is less than %d sec ", max_diff);
            rtc_time -= MS_TO_S(diff);
        }
        else {
            LOG_C(TAG, "Not reducing the rtc_time as time_present and crank_low_registered_time difference is more than %d sec ", max_diff);
        }
    }

    // If shutdown reason is SHUTDOWN_FOR_UPLOADER_DATA_UPLOAD_DONE or SHUTDOWN_FOR_DHUB_STATUS_CHECK_DONE then don't set the rtc_time
    // as it is already set while last shutdown due to SHUTDOWN_FOR_IGNITION_OFF or SHUTDOWN_FOR_LOWPOWER_WAKEUP
    if ((reason != SHUTDOWN_FOR_UPLOADER_DATA_UPLOAD_DONE) && (reason != SHUTDOWN_FOR_DHUB_STATUS_CHECK_DONE)) {
        // If current session is miscellaneous wakeup or reboot and current sutuation is for ignition off or lowpower wakeup,
        // Then set rtc_wakeup_time based on latest DBSTATE_RTC_WAKEUP_TIME_DHUB/DBSTATE_RTC_WAKEUP_TIME_DEVICE event
        if((POWER_MONITOR_ctx->misc_lowpower_reboot ||
            POWER_MONITOR_ctx->misc_lowpower_wakeup ||
            (reason == SHUTDOWN_FOR_IGNITION_OFF) ||
            (reason == SHUTDOWN_FOR_LOWPOWER_WAKEUP)) &&
            (reason != SHUTDOWN_FOR_BAD_VOLTAGE)) {
            std::stringstream query;
            db_state_info_t node = {};
            string event = (true == POWER_MONITOR_ctx->ext_cam_lpw_enabled) ? "DBSTATE_RTC_WAKEUP_TIME_DHUB" : "DBSTATE_RTC_WAKEUP_TIME_DEVICE";
            query << SELECT_POWERSTATES_EVENT_EQ(event) << ORDER_BY_INDEXID_DESC_LIMIT_1;
            if (!check_event_in_db(query, node)) {
                LOG_E(TAG, "Failed to get last %s event from db", event.c_str());
                // For safety, set rtc_wakeup_time based on last shutdown reason
                rtc_wakeup_time = time_present + S_TO_MS((int64_t)rtc_time);
            } else {
                rtc_wakeup_time = node.event_time;
            }
        }
        else {
            rtc_wakeup_time = time_present + S_TO_MS((int64_t)rtc_time);
        }

        // if override value for safety time to sync driveri_dhub is aviailable, to wakeup device before DHUB
        // Then reducing the safety_time_to_sync_driveri_dhub from rtc_wakeup_time (By default, safety time to sync driveri_dhub is 0)
        // if((true == POWER_MONITOR_ctx->ext_cam_lpw_enabled) && (POWER_MONITOR_ctx->safety_time_to_sync_driveri_dhub > 0)) {
        //     rtc_wakeup_time -= S_TO_MS(POWER_MONITOR_ctx->safety_time_to_sync_driveri_dhub);
        // }
        LOG_I(TAG, "%s: rtc_wakeup_time %lld", __func__, rtc_wakeup_time);
    }

    // set the RTC for default_safety_wakeup
    if( false == configure_rtc(POWER_MONITOR_ctx->safety_wakeup) ) {
        string str_msg = "Something went wrong with configure_rtc_time(safety_wakeup); TAKE ACTION HERE";
        LOG_E(TAG, str_msg.c_str());
        nd_service_obj->send_err_msg(SM_E_PM_SET_RTC_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
    }

    LOG_I(TAG, "shutdown_log :: %s", content.str().c_str());

    return true;
}

bool postpone_shutdown(int postpone_shutdown_for_secs, 
                    power_monitor_shutdown_reason reason)
{    
    FILE *fp=NULL;
    stringstream command;
    stringstream content;

    int64_t time = get_system_time();
    content << "@ time " << time << "\t";
    if (POWER_MONITOR_ctx->power_state.reason == SHUTDOWN_FOR_SDCARD_RO_RECOVERY){
        LOG_I(TAG, "postpone_shutdown cannot be honored power_state reason is SHUTDOWN_FOR_SDCARD_RO_RECOVERY");
        return false;
    }
    if (((POWER_MONITOR_ctx->power_state.reason == SHUTDOWN_FOR_IGNITION_OFF) ||(POWER_MONITOR_ctx->power_state.reason == SHUTDOWN_FOR_UPLOADER_DATA_UPLOAD_DONE) ) && (reason == POSTPONE_FOR_NORMAL_RUN) ){
        LOG_I(TAG, "postpone_shutdown cannot be honored as special event has happened");
        return false;
    }
    int postpone_shutdown_for_mins = ( postpone_shutdown_for_secs + 59 )/SECS_IN_A_MIN;
    postpone_shutdown_for_secs = postpone_shutdown_for_mins*SECS_IN_A_MIN;
    LOG_I(TAG, "inside postpone_shutdown %d secs", postpone_shutdown_for_secs );

    if(POSTPONE_FOR_IGNITION_ON == reason) {
        LOG_C(TAG, "postponing previous shutdown because of POSTPONE_FOR_IGNITION_ON");
        content << "postponing previous shutdown because of POSTPONE_FOR_IGNITION_ON" << "\t"; 
    }
    else if(POSTPONE_FOR_NORMAL_RUN == reason) {
        LOG_I(TAG, "postponing previous shutdown because of POSTPONE_FOR_NORMAL_RUN");
        content << "postponing previous shutdown because of POSTPONE_FOR_NORMAL_RUN" << "\t";
    }
    else if(POSTPONE_FOR_UPLOADER_ACTIVITY == reason) {
        LOG_I(TAG, "delaying shutdown because of POSTPONE_FOR_UPLOADER_ACTIVITY");
        content << "delaying shutdown because of POSTPONE_FOR_UPLOADER_ACTIVITY" << "\t";
    }
    else if (POSTPONE_FOR_DHUB_STATUS_CHECK == reason) {
        LOG_I(TAG, "delaying shutdown because of POSTPONE_FOR_DHUB_STATUS_CHECK");
        content << "delaying shutdown because of POSTPONE_FOR_DHUB_STATUS_CHECK" << "\t";
    }
    else if(POSTPONE_FOR_MISC_LOWPOWER_WAKEUP == reason) {
        LOG_I(TAG, "postponing previous shutdown because of POSTPONE_FOR_MISC_LOWPOWER_WAKEUP");
        content << "postponing previous shutdown because of POSTPONE_FOR_MISC_LOWPOWER_WAKEUP" << "\t";
    }
    else if( POSTPONE_FOR_BAD_BATTERY_CLEAR == reason) {
        LOG_I(TAG, "postponing previous shutdown because of POSTPONE_FOR_BAD_BATTERY_CLEAR");
        content << "postponing previous shutdown because of POSTPONE_FOR_BAD_BATTERY_CLEAR" << "\t";
        reason = POSTPONE_FOR_NORMAL_RUN; // After bad battery clear, we are treating it as normal run for postponing shutdown
    }
    else {
        string str_msg = "postpone_shutdown with unknown reason; TAKE ACTION HERE";
        LOG_E(TAG, str_msg.c_str());
        nd_service_obj->send_err_msg(SM_E_PM_UNKNOWN_SHDN_PSTPNE, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        return false;
    }

    pthread_mutex_lock(&POWER_MONITOR_ctx->power_state_mutex);
    POWER_MONITOR_ctx->power_state.monotonic_time = get_system_monotonic_time();
    POWER_MONITOR_ctx->power_state.time_gap = postpone_shutdown_for_mins*60;
    POWER_MONITOR_ctx->power_state.reason = reason;
    pthread_mutex_unlock(&POWER_MONITOR_ctx->power_state_mutex);

    POWER_MONITOR_ctx->reason = SHUTDOWN_CANCELLED;

    if( false == configure_rtc(POWER_MONITOR_ctx->safety_wakeup) ) {
        string str_msg = "Something went wrong with configure_rtc_time(safety_wakeup); TAKE ACTION HERE";
        LOG_E(TAG, str_msg.c_str());
        nd_service_obj->send_err_msg(SM_E_PM_SET_RTC_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
    }

    // dump the reason for shutdown/postpone
    if(reason != POSTPONE_FOR_NORMAL_RUN) {
        content << command.str() << "\t" << "\t";

        LOG_I(TAG, "shutdown_log :: %s", content.str().c_str());
    }
    return true;
}

void update_lpw_status(powermon_ignition_msg_t &msg) {

    msg.lpw_status = static_cast<int64_t>(((true == POWER_MONITOR_ctx->lowpowermode) && (0 == POWER_MONITOR_ctx->crank_high_count.load())) ? LOW_POWER_WAKEUP_TRUE : LOW_POWER_WAKEUP_FALSE);
    // If msg.lpw_status is LOW_POWER_WAKEUP_TRUE, then we need to read the lpw state from sysfs
    if (true == POWER_MONITOR_ctx->non_lpm_crank_low_wakeup){
        if(msg.lpw_status == LOW_POWER_WAKEUP_TRUE) {

            int status = static_cast<int>(lpw_state_t::eLPW_INIT);
            read_from_sysfs_entry(nd_device_obj->get_lpw_stat_sysfs_path(), status);
            if (lpw_state_t::eLPW_OFF == static_cast<lpw_state_t>(status)) {
                msg.lpw_status = LOW_POWER_WAKEUP_FALSE;
            }
            LOG_I(TAG, "After update_lpw_status, low_power_wakeup = %d", msg.lpw_status);
        }
    }
}

void send_current_ignition_satus_to_client(const std::string &client_id) {

    LOG_I(TAG, "send_current_ignition_satus_to_client called for client_id: %s", client_id.c_str());

    powermon_ignition_msg_t msg {};
    msg.crank_change_time = POWER_MONITOR_ctx->present_crank_change_time.load(); // present_crank_change_time is atomic so using load() to get the value safely.

    if(CRANK_LOW == POWER_MONITOR_ctx->present_crank_level) {
        msg.status = static_cast<int64_t>(ignition_status_t::IGNITION_OFF);
    }
    else if (CRANK_HIGH == POWER_MONITOR_ctx->present_crank_level) {
        msg.status = static_cast<int64_t>(ignition_status_t::IGNITION_ON);
    }
    else {
        msg.status = static_cast<int64_t>(ignition_status_t::IGNITION_ERR);
    }

    update_lpw_status(msg);
    msg.wakeup_reason = POWER_MONITOR_ctx->wakeup_reason;
    LOG_I(TAG, "Ign status = %lld, crank_change_time = %lld, lpw_status = %lld, wakeup_reason = %lld", msg.status, msg.crank_change_time, msg.lpw_status, msg.wakeup_reason);

    if (send_msg ((generic_msg_t *)&msg, POWERMON_IGNITION, sizeof (msg),
                  POWER_MONITOR_ctx->power_monitor_q_name, client_id, 0)) {
        LOG_I (TAG, "Ignition status sent to %s", client_id.c_str());
    } else {
        LOG_E (TAG, "Sending Ignition status : sent to %s failed", client_id.c_str());
    }
}

void send_ignition_status (power_crank_levels_t crank_level, int64_t crank_change_time)
{
    powermon_ignition_msg_t msg {};

    msg.crank_change_time = crank_change_time;
    if(CRANK_LOW == crank_level) {
        msg.status = static_cast<int64_t>(ignition_status_t::IGNITION_OFF);

#ifdef IGNITION_AUDIO_ALERT
        //reset the counts for idle audio alert.
	    POWER_MONITOR_ctx->ignitionOnIdleAudioAlertDuration = 0;
	    POWER_MONITOR_ctx->ignitionOnIdleAudioAlertFrequency = 0;
        LOG_I(TAG, "Ignition status is OFF, reset idle audio alert counts, ignitionOnIdleAudioAlertDuration = %d, ignitionOnIdleAudioAlertFrequency = %d",
                POWER_MONITOR_ctx->ignitionOnIdleAudioAlertDuration, POWER_MONITOR_ctx->ignitionOnIdleAudioAlertFrequency);
#endif
    }
    else if (CRANK_HIGH == crank_level) {
        msg.status = static_cast<int64_t>(ignition_status_t::IGNITION_ON);
    }
    else {
        msg.status = static_cast<int64_t>(ignition_status_t::IGNITION_ERR);
    }

    msg.lpw_status = static_cast<int64_t>(((true == POWER_MONITOR_ctx->lowpowermode) && (0 == POWER_MONITOR_ctx->crank_high_count.load())) ? LOW_POWER_WAKEUP_TRUE : LOW_POWER_WAKEUP_FALSE);
    msg.wakeup_reason = POWER_MONITOR_ctx->wakeup_reason;

#ifdef IGNITION_BROADCAST
          std::shared_ptr<ndmbmsg_apm_ign_status_t>  pm_ign_status(new ndmbmsg_apm_ign_status_t);
          pm_ign_status->ign_status = msg.status;
          bool res = server.publish(TOPIC_POWER_MON_IGN_STATUS, pm_ign_status);
          LOG_I (TAG, "Ignition status = %d, Publishing to OBD", pm_ign_status->ign_status);
          ignition_status = pm_ign_status->ign_status;
          is_ignition_status_update = true;
          if (!res){
                  LOG_E(TAG, "publish error");
          }
#endif

    // Update the low power wakeup status
    update_lpw_status(msg);

    LOG_I(TAG, "Ign status = %lld, crank_change_time = %lld, lpw_status = %lld, wakeup_reason = %lld", msg.status, msg.crank_change_time, msg.lpw_status, msg.wakeup_reason);

    for (int i=0; i<NUM_IGNITION_CLIENTS; i++) {
        if((false == vehicle_data_enabled) && (Q_OBD == IGNITION_CLIENTS[i])) {
            continue;
        }
        if (true == send_msg ((generic_msg_t *)&msg, POWERMON_IGNITION, sizeof (msg), POWER_MONITOR_ctx->power_monitor_q_name, IGNITION_CLIENTS[i], 0)) {
            LOG_I (TAG, "Ignition status sent to %s", IGNITION_CLIENTS[i].c_str());
        }
        else {
            LOG_E (TAG, "Sending Ignition status : sent to %s failed", IGNITION_CLIENTS[i].c_str());
        }
    }
}

/* This function will take shutdown duration and dbstate as input,
 * calculate the rtc wakeup time based on the shutdown duration and sleep cycle duration.
 * Add the rtc wakeup time in db with the dbstate.
 */
static bool add_rtc_wakeup_time_in_db(int shutdown_duration, power_dbstate_enum_t dbstate) {

    LOG_I(TAG, "Inside %s to add rtc wakeup time in db", __func__);

    int64_t current_time = get_system_time();
    int64_t rtc_wakeup_time = current_time + S_TO_MS(shutdown_duration) + S_TO_MS(get_lpw_cycle_duration());

    if(true == POWER_MONITOR_ctx->ext_cam_lpw_enabled){
        if(true == POWER_MONITOR_ctx->dhub_wakeup_time_event_found) {
            LOG_I(TAG, "lat_dhub_wakeup_time: %lld", POWER_MONITOR_ctx->last_dhub_wakeup_time);
            rtc_wakeup_time = POWER_MONITOR_ctx->last_dhub_wakeup_time + S_TO_MS(shutdown_duration) + S_TO_MS(get_lpw_cycle_duration()) + POWER_MONITOR_ctx->possible_lpw_count * (S_TO_MS((int64_t)dhub_wakeup_sync_extra) + S_TO_MS((int64_t)dhub_shutdown_sync_extra));
            LOG_I(TAG, "rtc_wakeup_time based on prev dhub wakeup %lld", rtc_wakeup_time);
        }else{
            rtc_wakeup_time += S_TO_MS((int64_t)dhub_wakeup_sync_extra) + S_TO_MS((int64_t)dhub_shutdown_sync_extra);
        }
    }
    int rtc_time = static_cast<int>(MS_TO_S(rtc_wakeup_time - current_time));
    string event_name = power_dbstate_enum_t::toString(dbstate);

    if ( false == add_event_db(POWER_MONITOR_ctx->db_handle, rtc_wakeup_time, POWER_MONITOR_ctx->boot_time,
                    POWER_MONITOR_ctx->pid_num, event_name, "WAKEUP")) {
        LOG_E(TAG, "Failed to add rtc wakeup time for device to db");
        return false;
    }
    string msg = event_name + ": " + to_string(rtc_wakeup_time);
    LOG_I(TAG, "%s", msg.c_str());
    nd_service_obj->send_err_msg(SM_E_PM_RTC_WAKEUP_TIME, rtc_time, msg);
    return true;
}

void reset_lpw_data() {
    // reset values to original
    POWER_MONITOR_ctx->crank_shutdown_duration = lpw_data.crank_shutdown_duration;
    POWER_MONITOR_ctx->lowpower_wakeup_duration = lpw_data.lowpower_wakeup_duration;
    POWER_MONITOR_ctx->lowpower_wakeup_cycle_duration = lpw_data.lowpower_wakeup_cycle_duration;
    POWER_MONITOR_ctx->lowpower_wakeup_long_cycle_duration = lpw_data.lowpower_wakeup_long_cycle_duration;

    LOG_I(TAG, "crank_shutdown_duration %d, lowpower_wakeup_duration %d, lowpower_wakeup_cycle_duration %d, lowpower_wakeup_long_cycle_duration %d",
            POWER_MONITOR_ctx->crank_shutdown_duration, POWER_MONITOR_ctx->lowpower_wakeup_duration,
            POWER_MONITOR_ctx->lowpower_wakeup_cycle_duration, POWER_MONITOR_ctx->lowpower_wakeup_long_cycle_duration);
}

bool identify_misc_wakeup_reason(lpw_state_t lpw_stat) {

    // verify device misc wakeup reason and store it into POWER_MONITOR_ctx->wakeup_reason
    // So according to the misc wakeup reason, we can handle the device shutdown.
    // And also we can handle record on misc window is required or not
    // Device may wake up due to following reasons
    // 1. wake on ign
    // 2. wake on wom imu
    // 3. wake on wom aon
    // 4. wake on can -> Not applicable for now
    // 5. wake on sms -> Not applicable for now

    bool ret = false;

    if((true == POWER_MONITOR_ctx->wake_on_ign) && (POWER_MONITOR_ctx->power_on_off_reason & WAKE_ON_IGN_MASK)) {
        POWER_MONITOR_ctx->wakeup_reason = PowerOnTriggerT::WAKEonIGNITION;
        ret = true;
    }
    else if((true == POWER_MONITOR_ctx->wake_on_motion_imu) && (POWER_MONITOR_ctx->power_on_off_reason & WAKE_ON_MOT_IMU_MASK)) {
        POWER_MONITOR_ctx->wakeup_reason = PowerOnTriggerT::WAKEonMOTION_IMU;
        ret = true;
    }
    else if((true == POWER_MONITOR_ctx->wake_on_motion_aon) && (POWER_MONITOR_ctx->power_on_off_reason & WAKE_ON_MOT_AON_MASK)) {
        POWER_MONITOR_ctx->wakeup_reason = PowerOnTriggerT::WAKEonMOTION_AON;
        ret = true;
    }
    else if (true == POWER_MONITOR_ctx->wake_on_misc) {
        // If wake_on_misc is true, then we can assume that device woke up due to misc event
        POWER_MONITOR_ctx->wakeup_reason = PowerOnTriggerT::POWER_ON_TRIGGER;
        ret = true;
    }
    else if ((lpw_state_t::eLPW_OFF == static_cast<lpw_state_t>(lpw_stat))) {
        db_state_info_t misc_lowpower_wakeup_node = {};
        stringstream query;
        query << SELECT_POWERSTATES_EVENT_EQ("DBSTATE_MISC_LOWPOWER_WAKEUP") << ORDER_BY_INDEXID_DESC_LIMIT_1;

        // Check is based on action which is updated on last misc wakeup event
        // In case entry not found in db because of crash or reboot, then we consider it as misc and LPW is ON as per x.6.10.rc.x
        if (check_event_in_db(query, misc_lowpower_wakeup_node)) {
            POWER_MONITOR_ctx->wakeup_reason = PowerOnTriggerT::fromString(misc_lowpower_wakeup_node.action);
            ret = true;
        }
        else {
            // If can't find entry then we are doing LPW ON
            ret = false;
        }
    }

    LOG_I(TAG, "wakeup_reason: %d" , static_cast<int>(POWER_MONITOR_ctx->wakeup_reason));
    return ret;
}

int process_crank_low(int64_t crank_change_time)
{
    LOG_I(TAG, "inside process_crank_low");

    LOG_I(TAG, "voltage :: %d", CRANK_LOW);
    LOG_I(TAG, "crank_high_count :: %d", POWER_MONITOR_ctx->crank_high_count.load());

    POWER_MONITOR_ctx->crank_low_registered = true;
    POWER_MONITOR_ctx->crank_low_registered_time = get_system_time();
    POWER_MONITOR_ctx->present_crank_change_time = crank_change_time; // store the crank change time for current crank low event

    if ((true == POWER_MONITOR_ctx->ext_cam_lpw_enabled) && (true == POWER_MONITOR_ctx->dhub_status_check_enabled)) {
        LOG_I(TAG, "in process_crank_low and enabling delay_shutdown_for_dhub_status_check");
        POWER_MONITOR_ctx->delay_shutdown_for_dhub_status_check = true;
    }

    if ((true == POWER_MONITOR_ctx->lowpowermode)  &&
        (0 == POWER_MONITOR_ctx->crank_high_count.load()) &&
        (false == POWER_MONITOR_ctx->misc_lowpower_wakeup)) {

        string err_msg = "Booted up in Low power wakeup mode";
        if (POWER_MONITOR_ctx->lowpower_wakeups > POWER_MONITOR_ctx->max_lowpower_wakeups) {
            err_msg = "Booted up due to NON-RTC Event";
        }

        LOG_I(TAG, "%s", err_msg.c_str());
        LOG_I(TAG, "in process_crank_low and cyclic_reboot is true");
        LOG_I(TAG, "crank_high_count %d is zero; means its a low power wakeup", POWER_MONITOR_ctx->crank_high_count.load());
        nd_service_obj->send_err_msg(SM_I_PM_LPW_IS_ACTIVE, POWER_MONITOR_ctx->lowpower_wakeups, err_msg);

        if(false == POWER_MONITOR_ctx->misc_lowpower_reboot) {
            // ADD NEW ENTRY FOR LOWPOWER WAKEUP
            add_event_db(power_dbstate_enum_t::DBSTATE_LOWPOWER_WAKEUP);
            if(false == POWER_MONITOR_ctx->ext_cam_lpw_enabled){
                add_rtc_wakeup_time_in_db(POWER_MONITOR_ctx->lowpower_wakeup_duration, power_dbstate_enum_t::DBSTATE_RTC_WAKEUP_TIME_DEVICE);
            }
        }

        LOG_I(TAG, "wait for lowpower_wakeup_duration and shutdown");
        initiate_shutdown(POWER_MONITOR_ctx->lowpower_wakeup_duration, SHUTDOWN_FOR_LOWPOWER_WAKEUP);

    }
    else if((true == POWER_MONITOR_ctx->misc_lowpower_wakeup) &&
            (0 == POWER_MONITOR_ctx->crank_high_count.load())) {

        if (POWER_MONITOR_ctx->lowpower_wakeups >= POWER_MONITOR_ctx->max_lowpower_wakeups) {
            string err_msg = "Booted up due to NON-RTC Event";
            LOG_C(TAG, "%s", err_msg.c_str());
            nd_service_obj->send_err_msg(SM_I_PM_LPW_IS_ACTIVE, POWER_MONITOR_ctx->lowpower_wakeups, err_msg);
        }

        LOG_I(TAG, "in process_crank_low and misc_lowpower_wakeup is true");

        int shutdown_duration = (true == POWER_MONITOR_ctx->non_lpm_crank_low_wakeup) ? POWER_MONITOR_ctx->non_lpm_crank_low_wakeup_duration : POWER_MONITOR_ctx->misc_wakeup_duration;

        // add event in db for MISC wakeup
        if(false == POWER_MONITOR_ctx->misc_lowpower_reboot) {
            int64_t shutdown_time = get_system_time() + S_TO_MS((int64_t)shutdown_duration);
            LOG_I(TAG, "record on crank low wakeup duration %d", shutdown_duration);
            string wakeup_reason = PowerOnTriggerT::toString(POWER_MONITOR_ctx->wakeup_reason);
            add_event_db(POWER_MONITOR_ctx->db_handle, shutdown_time, POWER_MONITOR_ctx->boot_time, POWER_MONITOR_ctx->pid_num, "DBSTATE_MISC_LOWPOWER_WAKEUP", wakeup_reason);
        }

        initiate_shutdown(shutdown_duration, SHUTDOWN_FOR_MISC_LOWPOWER_WAKEUP);
    }
    else {
        LOG_I(TAG, "wait for crank_shutdown_duration and shutdown");

        // If current session is valid and not a miscellaneous reboot and ext_cam_lpw_enabled is false, then add the device wakeup time in db
        if((false == POWER_MONITOR_ctx->misc_lowpower_reboot) && (false == POWER_MONITOR_ctx->ext_cam_lpw_enabled)) {
            add_rtc_wakeup_time_in_db(POWER_MONITOR_ctx->crank_shutdown_duration, power_dbstate_enum_t::DBSTATE_RTC_WAKEUP_TIME_DEVICE);
        }

        // Setting the lpw_state to eLPW_OFF for crank off
        write_into_sysfs_entry(nd_device_obj->get_lpw_stat_sysfs_path(), static_cast<int>(lpw_state_t::eLPW_OFF));
        LOG_I(TAG, "%s : crank_shutdown_duration %d", __func__, POWER_MONITOR_ctx->crank_shutdown_duration);
        initiate_shutdown(POWER_MONITOR_ctx->crank_shutdown_duration, SHUTDOWN_FOR_IGNITION_OFF);
    }

    send_ignition_status (CRANK_LOW, crank_change_time);
#ifdef IGNITION_AUDIO_ALERT
    file_delete(ignition_audio_check);
#endif
    return 1;
}

int process_crank_high(int64_t crank_change_time)
{
    LOG_I(TAG, "inside process_crank_high");
    LOG_I(TAG, "voltage :: %d", CRANK_HIGH);

    POWER_MONITOR_ctx->present_crank_change_time = crank_change_time; // store the crank change time for current crank high event

    if(POWER_MONITOR_ctx->crank_low_registered == true) {
        LOG_I(TAG, "in process_crank_high and POWER_MONITOR_ctx->crank_low_registered == true");
        LOG_I(TAG, "cancelling previous shutdown commands; postpone shutdown by 5 mins");
        postpone_shutdown(POSTPONE_SHUTDOWN_DURATION, POSTPONE_FOR_IGNITION_ON);
    }
    // this will come when crank ON during lowpower wakeup  
    // this can also come in cyclic reboot crank on
    else
    {
        LOG_I(TAG, "in process_crank_high and POWER_MONITOR_ctx->crank_low_registered == false");
        LOG_I(TAG, "cancelling previous shutdown commands; postpone shutdown by 5 mins");
        postpone_shutdown(POSTPONE_SHUTDOWN_DURATION, POSTPONE_FOR_IGNITION_ON);
    }

    POWER_MONITOR_ctx->uploader_data_upload_pending = false;
    POWER_MONITOR_ctx->shutdown_delayed = false;
    POWER_MONITOR_ctx->crank_low_registered = false;
    POWER_MONITOR_ctx->crank_low_registered_time = -1;
    POWER_MONITOR_ctx->lowpower_wakeups = 0; // resetting lowpower_wakeups to 0 since we got crank high
    reported_possible_delay_in_shutdown = false;

    // REsetting the misc_lowpower_reboot and misc_lowpower_wakeup to false as crank high is received
    POWER_MONITOR_ctx->misc_lowpower_reboot = false;
    POWER_MONITOR_ctx->misc_lowpower_wakeup = false;
    POWER_MONITOR_ctx->misc_wakeup_count = 0; // resetting misc_wakeup_count to 0 since we got crank high

    // On ignition high resetting dhub_status_check_count
    dhub_status_check_count = 0;

    // resetting the extend_wakeup_duration_for_misc to false;
    POWER_MONITOR_ctx->extend_wakeup_duration_for_misc = false;

    // resetting lpw_state to eLPW_INIT
    lpw_state_t lpw_stat = lpw_state_t::eLPW_INIT;
    LOG_I(TAG, "Setting lpw_stat to eLPW_INIT(%d)", static_cast<int>(lpw_stat));
    write_into_sysfs_entry(nd_device_obj->get_lpw_stat_sysfs_path(), static_cast<int>(lpw_stat));

    // resetting the dhub_wakeup_time_event_found to false
    POWER_MONITOR_ctx->dhub_wakeup_time_event_found = false;

    if ((true == POWER_MONITOR_ctx->ext_cam_lpw_enabled) && (true == POWER_MONITOR_ctx->dhub_status_check_enabled)) {
        LOG_I(TAG, "in process_crank_high and disabling delay_shutdown_for_dhub_status_check");
        POWER_MONITOR_ctx->delay_shutdown_for_dhub_status_check = false;
    }

    POWER_MONITOR_ctx->delay_shutdown_for_misc_lowpower_wakeup = false; // Resetting the flag

    send_ignition_status (CRANK_HIGH, crank_change_time);
#ifdef IGNITION_AUDIO_ALERT
    // To play ignition on audio alert
    ignition_on_audio_alert();
#endif

    // reset all LPW related variables with original values
    // To handle : crankoff ---- reboot --- values reduced --- crank high/low toggle --- to reset to original values
    if (POWER_MONITOR_ctx->crank_high_count.load() >= 1) {
        LOG_I(TAG, "resetting LPW related variables with original values");
        reset_lpw_data();
    }

    return 1;
}

int get_uptime()
{
    struct sysinfo uptime;
    if(sysinfo(&uptime)) {
        LOG_E(TAG, "sysinfo failed");
        return false;
    }

    return int(uptime.uptime/SECS_IN_A_MIN);
}

int get_prev_shutdown_reason()
{

    if (POWER_MONITOR_ctx->previous_shutdown_reason.empty()) {
        LOG_I(TAG, "Previous shutdown reason is not available or set to NA.");
        return -1;
    }    

    if (POWER_MONITOR_ctx->previous_shutdown_reason == "NA") {
        return power_dbstate_enum_t::DBSTATE_SHUTDOWN_UNKNOWN;
    }    

    LOG_I(TAG, "Processing previous shutdown reason: %s", POWER_MONITOR_ctx->previous_shutdown_reason.c_str());

    // Check for keywords like "POWER_DOWN"
    std::vector<std::string> keywords = {"POWER_DOWN", "SYSTEM_RESET", "SUPERCAP_DISCHARGE"};
    for (const auto& keyword : keywords) {
        if (POWER_MONITOR_ctx->previous_shutdown_reason.find(keyword) != std::string::npos) {
            LOG_I(TAG, "Found keyword '%s' in the shutdown reason. Returning DBSTATE_SHUTDOWN_UNKNOWN.", keyword.c_str());
            return power_dbstate_enum_t::DBSTATE_SHUTDOWN_UNKNOWN;
        }
    }    

    // Extract the reason before the first colon
    size_t colon_pos = POWER_MONITOR_ctx->previous_shutdown_reason.find(':');
    std::string prev_shutdown_reason = (colon_pos != std::string::npos)
        ? POWER_MONITOR_ctx->previous_shutdown_reason.substr(0, colon_pos)
        : POWER_MONITOR_ctx->previous_shutdown_reason;

    // Trim whitespace
    prev_shutdown_reason.erase(0, prev_shutdown_reason.find_first_not_of(" \t"));
    prev_shutdown_reason.erase(prev_shutdown_reason.find_last_not_of(" \t") + 1);

    LOG_I(TAG, "Extracted shutdown reason: %s", prev_shutdown_reason.c_str());

    // If empty after trimming, treat as unknown
    if (prev_shutdown_reason.empty()) {
        LOG_E(TAG, "Shutdown reason extracted is empty. Returning DBSTATE_SHUTDOWN_UNKNOWN.");
        return power_dbstate_enum_t::DBSTATE_SHUTDOWN_UNKNOWN;
    }

    // Map the reason to an enum
    int res = -1;
    try {
        res = power_dbstate_enum_t::fromString(prev_shutdown_reason);
    } catch (const std::exception& e) {
        LOG_E(TAG, "Exception in fromString: %s. Returning DBSTATE_SHUTDOWN_UNKNOWN.", e.what());
        return power_dbstate_enum_t::DBSTATE_SHUTDOWN_UNKNOWN;
    } catch (...) {
        LOG_E(TAG, "Unknown exception in fromString. Returning DBSTATE_SHUTDOWN_UNKNOWN.");
        return power_dbstate_enum_t::DBSTATE_SHUTDOWN_UNKNOWN;
    }

    // Validate the enum value
    if (res < 0 || res > static_cast<int>(power_dbstate_enum_t::DBSTATE_ERROR)) {
        LOG_D(TAG, "Invalid shutdown reason: %s. Returning DBSTATE_SHUTDOWN_UNKNOWN.", prev_shutdown_reason.c_str());
        return power_dbstate_enum_t::DBSTATE_SHUTDOWN_UNKNOWN;
    }

    LOG_C(TAG, "Previous shutdown reason: %s (mapped to %d)", prev_shutdown_reason.c_str(), res);
    return res;

}

float get_cpu_temp()
{
    int temp = 0;
#ifndef KRAIT
    ifstream temp_file("/sys/devices/virtual/thermal/thermal_zone1/temp");
#else
    ifstream temp_file("/sys/class/thermal/thermal_zone8/temp");
#endif
    if (!temp_file.is_open()) {
        LOG_E(TAG, "Failed to open temp file: %s", strerror(errno));
        return 0.0;
    }
    // Enable exceptions to be thrown on failbit and badbit
    temp_file.exceptions(std::ifstream::failbit | std::ifstream::badbit);

    try {
        temp_file >> temp;
    }
    catch(const std::ios_base::failure& e){
        LOG_E(TAG, "I/O Exception in reading cpu temperature: %s", e.what());
        temp_file.close();
        return 0.0;
    }
    catch(...){
        LOG_E(TAG, "Exception in reading cpu temperature");
        temp_file.close();
        return 0.0;
    }
    LOG_D(TAG, "temperature is %f, %d",((static_cast<float>(temp))/1000) , temp);
    temp_file.close();
    return ((static_cast<float>(temp))/1000);
}

int get_wakeup_reason(power_metrics_msg_t power_metrics)
{
    if(power_metrics.shutdown_reason >= int(power_dbstate_enum_t::DBSTATE_SHUTDOWN_UPLOADER_UPLOAD_DONE) &&  power_metrics.shutdown_reason <= int(power_dbstate_enum_t::DBSTATE_SHUTDOWN_INSTALLER_APP_CRASH))
    {
        return CRANK_HIGH_WAKEUP;
    }

    else
    {
        return UNKNOWN_WAKEUP;
    }
}

float get_prev_voltage()
{
    float prev_volt = -1.0;
    if(file_is_present(prev_volt_file))
    {    
        ifstream prev_volt_fd;
        prev_volt_fd.open(prev_volt_file, std::ifstream::binary);
        if (false == prev_volt_fd.is_open()) {
            LOG_E(TAG, "Failed to open prev_volt_file: %s", strerror(errno));
        }
        else {
            try {
                prev_volt_fd >> prev_volt;
            }
            catch(...){
                LOG_E(TAG, "Exception in reading previous voltage");
            }
            prev_volt_fd.close();
        }
        // Removing the file_delete operation, in case update fails we still have the previous voltage. When update happens it will overwrite the previous value.
    }
    update_voltage_before_shutdown();

    return prev_volt;
}

int get_prev_valid_speed(int curr_speed)
{
    int prev_valid_speed = -1;
    if(file_is_present(prev_valid_speed_file))
    {    
        ifstream prev_valid_speed_fd;
        prev_valid_speed_fd.open(prev_valid_speed_file);
        if(false == prev_valid_speed_fd.is_open()) {
            LOG_E(TAG, "Failed to open prev_valid_speed_file: %s", strerror(errno));
        }
        else {
            try {
                prev_valid_speed_fd >> prev_valid_speed;
            }
            catch(...){
                LOG_E(TAG, "Exception in reading previous valid speed");
            }
            prev_valid_speed_fd.close();
        }
        file_delete(prev_valid_speed_file);
    }
    else
    {
        prev_valid_speed = -1;
    }
    update_valid_speed_before_shutdown(curr_speed);

    return prev_valid_speed;
}

gps_health_data_t get_latest_gps_health_data() {
    int latest_valid_index = -1;
    for (int i = NUM_GPS_UPDATES_PER_MINUTE - 1; i >= 0; i--) {
        if (gps_health_updates[i].valid) {
            latest_valid_index = i;
            break;
        }
    }
    if (latest_valid_index == -1) {
        return (gps_health_data_t) {
            .valid = false,
            .speed = -1.0,
            .lat = 91.0,
            .lon = 181.0,
            .accuracy = 0.0
        };
    }
    return gps_health_updates[latest_valid_index];
}

void send_power_metrics(power_metrics_msg_t power_metrics)
{
    power_metrics.uptime = get_uptime();
    power_metrics.igni_stat = POWER_MONITOR_ctx->present_crank_level;
    power_metrics.igni_post_off_time = int(POWER_MONITOR_ctx->crank_shutdown_duration/SECS_IN_A_MIN);
    power_metrics.lpw_cnt = POWER_MONITOR_ctx->lowpower_wakeups;
    power_metrics.lpw_cyc_dur = int(POWER_MONITOR_ctx->lowpower_wakeup_cycle_duration/SECS_IN_A_MIN);
    power_metrics.lpw_wakeup_dur = int(POWER_MONITOR_ctx->lowpower_wakeup_duration/SECS_IN_A_MIN);
    power_metrics.shutdown_reason = get_prev_shutdown_reason();
    power_metrics.wakeup_reason = get_wakeup_reason(power_metrics);
    power_metrics.temp = get_cpu_temp();
    power_metrics.batt_curr_volt = int(POWER_MONITOR_ctx->current_voltage);
    power_metrics.batt_min_volt = int(POWER_MONITOR_ctx->min_voltage_limit);
    power_metrics.batt_max_volt = int(POWER_MONITOR_ctx->max_voltage_limit);
    
    gps_health_data_t last_valid_data = get_latest_gps_health_data();
    power_metrics.curr_lat = last_valid_data.lat;
    power_metrics.curr_long = last_valid_data.lon;
    power_metrics.curr_spd = int(last_valid_data.speed);
    power_metrics.prev_spd = get_prev_valid_speed(power_metrics.curr_spd);
    power_metrics.prev_volt = int(get_prev_voltage());
    // Read supercap event count from sysfs
    if(false == read_from_sysfs_entry(get_sysfs_path_from_enum(PowermonParam::eSUPERCAP_COUNT), power_metrics.supercap_toggle_cnt)){
        power_metrics.supercap_toggle_cnt = 0;
        LOG_E(TAG, "Failed to read supercap toggle count from sysfs entry %s", get_sysfs_path_from_enum(PowermonParam::eSUPERCAP_COUNT).c_str());
    }

    // Read ignition source status from sysfs
    if(false == read_from_sysfs_entry(get_sysfs_path_from_enum(PowermonParam::eIGN_SRC_STAT), (uint64_t&)power_metrics.ign_src_stat)){
        power_metrics.ign_src_stat = -1;
        LOG_E(TAG, "Failed to read ignition source status from sysfs entry %s", get_sysfs_path_from_enum(PowermonParam::eIGN_SRC_STAT).c_str());
    }

    send_msg ((generic_msg_t *)&power_metrics, RES_POWERMON_METRICS, sizeof(power_metrics), POWER_MONITOR_ctx->power_monitor_q_name, Q_DIAG, 0);
}

bool send_low_power_wakeup_count_time_sync(unsigned int low_power_wakeup_cnt) {

    static const unsigned int low_power_wakeups = low_power_wakeup_cnt;//Send the Count registered on power up
    time_sync_low_power_wakeup_cnt_msg_t m;
    m.low_power_wakeup_cnt = low_power_wakeups;
    LOG_I(TAG, " send_low_power_wakeup_count_time_sync()  m.low_power_wakeup_cnt: %u", m.low_power_wakeup_cnt);
    return send_msg( (generic_msg_t *)&m, LOW_POWER_WAKEUP_CNT_UPDATE, sizeof(m), POWER_MONITOR_ctx->power_monitor_q_name, QNAME_TIME_SYNC, 0 );
}

int64_t time_taken_to_bootup_device(){
    NDDeviceType device_type = nd_device_obj->getDeviceType();
    if(false == POWER_MONITOR_ctx->ext_cam_lpw_enabled){
        return 0;
    }
    switch(device_type){
        case eBagheera_2: return default_bootup_time_by_device + 30;
        case eBagheera_3: return default_bootup_time_by_device + 0;
        case eKrait_1: return default_bootup_time_by_device + 0;
        case eKrait_2: return default_bootup_time_by_device + 0;
    }
    return 0;
}

// This will update the DB entry for the DBSTATE_RTC_WAKEUP_TIME_DHUB if ext_cam_lpw_enabled is true
static bool update_dhub_wakeup_time_in_db() {

    if((!POWER_MONITOR_ctx->misc_lowpower_wakeup) && (!POWER_MONITOR_ctx->misc_lowpower_reboot)) {
        int crank_shutdown_duration = 0;
        if (true == POWER_MONITOR_ctx->ext_cam_lpw_enabled) {
            crank_shutdown_duration = POWER_MONITOR_ctx->dhub_crank_shutdown_duration;
        }
        else {
            LOG_C(TAG, "ext_cam_lpw_enabled is false; Not updating DHUB wakeup time in DB");
            return false;
        }

        // case 1:
        // After crank off lowpower_wakeups is 0 (0 != 0 is false) and update_dhub_wakeup_time_for_extended_shutdown is false
        // So that dhub wakeup time will be updated in db with crank_shutdown_duration

        // case 2:
        // After first LPW lowpower_wakeups is not 0 (wakeups != 0 is true) and update_dhub_wakeup_time_for_extended_shutdown is false
        // So that dhub wakeup time wiil be updated in db with lowpower_wakeup_duration

        // case 3:
        // Due to IMU/GPS IGN, shutdown will extended and overshoot next wakeup time
        // In monitor dhub wakeup time, will set the flag update_dhub_wakeup_time_for_extended_shutdown to true
        // So that dhub wakeup time in db will be updated with lowpower_wakeup_duration

        // case 4:
        // Due to uploader activity, shutdown will extended and overshoot next wakeup time
        // In monitor dhub wakeup time, will set the flag update_dhub_wakeup_time_for_extended_shutdown to true
        // So that dhub wakeup time in db will be updated with lowpower_wakeup_duration

        int shutdown_duration = ((POWER_MONITOR_ctx->lowpower_wakeups != 0) || (true == update_dhub_wakeup_time_for_extended_shutdown)) ? POWER_MONITOR_ctx->lowpower_wakeup_duration : crank_shutdown_duration;
        add_rtc_wakeup_time_in_db(shutdown_duration, power_dbstate_enum_t::DBSTATE_RTC_WAKEUP_TIME_DHUB);
    }

    stringstream ss;
    ss << SELECT_POWERSTATES_EVENT_EQ("DBSTATE_RTC_WAKEUP_TIME_DHUB") << ORDER_BY_INDEXID_DESC_LIMIT_1;
    db_state_info_t dhub_wakeup_time_node = {};
    bool entry_found = check_event_in_db(ss, dhub_wakeup_time_node);

    if(true == entry_found) {
        //storing the wakeup time in global variable to monitor dhub wakeup time
        g_dhub_wakeup_time = dhub_wakeup_time_node.event_time;
        LOG_I(TAG, "Updated g_dhub_wakeup_time: %lld", g_dhub_wakeup_time);
        monitor_dhub_wakeup_time = true;

        if(true == update_dhub_wakeup_time_for_extended_shutdown) {
            // Add DBSTATE_LOWPOWER_WAKEUP, DBSTATE_IGNITION_GPIO_LOW and DBSTATE_CRANKLOW to handle config validation
            add_event_db(POWER_MONITOR_ctx->db_handle, POWER_MONITOR_ctx->last_dhub_wakeup_time, POWER_MONITOR_ctx->boot_time, POWER_MONITOR_ctx->pid_num, "DBSTATE_LOWPOWER_WAKEUP", "NA");
            add_event_db(POWER_MONITOR_ctx->db_handle, POWER_MONITOR_ctx->last_dhub_wakeup_time, POWER_MONITOR_ctx->boot_time, POWER_MONITOR_ctx->pid_num, "DBSTATE_IGNITION_GPIO_LOW", "NA");
            add_event_db(POWER_MONITOR_ctx->db_handle, POWER_MONITOR_ctx->last_dhub_wakeup_time, POWER_MONITOR_ctx->boot_time, POWER_MONITOR_ctx->pid_num, "DBSTATE_CRANKLOW", "NA");
        }

        // Set the dhub_wakeup_time_event_found to true and store the dhub_wakeup_time_node.event_time in last_dhub_wakeup_time
        // So that after overshooting the dhub wakeup time.
        // We can update the dhub wakeup time based on the last dhub wakeup time
        POWER_MONITOR_ctx->dhub_wakeup_time_event_found = true;
        POWER_MONITOR_ctx->last_dhub_wakeup_time = dhub_wakeup_time_node.event_time;
        LOG_I(TAG, "monitor_dhub_wakeup_time set to true");
    }
    else {
        monitor_dhub_wakeup_time = false;
        LOG_I(TAG, "monitor_dhub_wakeup_time set to false. As no entry found in DB for DBSTATE_RTC_WAKEUP_TIME_DHUB");
    }

    return true;
}
bool update_db_event_time_by_index(int64_t index,int64_t event_time){
    stringstream query;
    query << "UPDATE POWERSTATES SET EVENTTIME=" << event_time << " WHERE INDEXID=" << index;
    bool rc = POWER_MONITOR_ctx->exec_cmd_db(POWER_MONITOR_ctx->db_handle, query.str(), NULL, 0);
    if(rc == false){
        // Notify health mon
        std::stringstream str_msg;
        str_msg << "failed to execute exec_cmd_db ";
        LOG_E(TAG, str_msg.str().c_str() );
        nd_service_obj->send_err_msg(SM_E_PM_DB_ADD_EVENT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg.str() );
        return false;
    }
    return true;
}

/* Check diff b/w prev high/low and current high/low count.
 * If diff is one then only process the crank change.
 * This is to avoid multiple crank change.
 */
static bool is_crank_change_valid() {

    uint high_count_diff = POWER_MONITOR_ctx->crank_high_count.load() - POWER_MONITOR_ctx->prev_crank_high_count.load();
    uint low_count_diff = POWER_MONITOR_ctx->crank_low_count.load() - POWER_MONITOR_ctx->prev_crank_low_count.load();

    LOG_C(TAG, "Valid crank change detected. CH(%d) - PCH(%d) = %d, CL(%d) - PCL(%d) = %d, EC=%d",
            POWER_MONITOR_ctx->crank_high_count.load(), POWER_MONITOR_ctx->prev_crank_high_count.load(), high_count_diff,
            POWER_MONITOR_ctx->crank_low_count.load(), POWER_MONITOR_ctx->prev_crank_low_count.load(), low_count_diff, POWER_MONITOR_ctx->crank_event_count.load());
    if((high_count_diff <= max_allowed_crank_changes) && (low_count_diff <= max_allowed_crank_changes) && (POWER_MONITOR_ctx->crank_event_count.load() < max_crank_events_to_debounce)) {
        return true;
    }

    // Debounce/ toggling detected return crank change as invalid
    return false;
}

int power_monitor_msg_loop()
{
    LOG_I(TAG, "inside power_monitor_msg_loop");
    nd_msgq_t::nd_msg_t *msg;
    power_monitor_crank_change_t *crank_change;
    power_monitor_maxtimeout_t* maxtimeout;
    power_monitor_battery_voltage_t *battery_volt;
    power_monitor_norman_run_t *normal_run;
    power_monitor_directpolling_crank_t *directpoll_crank_msg;
    power_monitor_sdcard_ro_reboot_t *sdcard_ro_reboot;
    power_monitor_svc_reboot_req_t *svc_reboot;
    power_monitor_installer_app_reboot_req_t *installer_app_reboot;
    power_monitor_installer_app_crash_reboot_req_t *installer_app_crash_reboot;
    power_monitor_outward_camera_crash_reboot_req_t *outward_camera_crash_reboot;
    power_monitor_to_reboot_t *s_reboot;

    res_gps_update_msg_t *gps_update;
    res_idle_update_msg_t *idle_update;
    driveri_app_login_update_msg_t *loginStatus;
    driver_login_audio_notify_msg_t *driver_login_audio_notify;
    msp_status_msg_t *msp_status;

    int time_diff;

    previous_ignition_on_idle_update_timestamp = get_system_monotonic_time();

    while(1) {
        pthread_mutex_lock ( &keepalive_status_mutex );
        all_thread_keepalive_status &= ~0x01;      // make 1st bit 0 for main thread  
        pthread_mutex_unlock ( &keepalive_status_mutex );
        if( (msg = POWER_MONITOR_ctx->powmon_msg_q->receive( )) == NULL ) {
            LOG_E(TAG, "Receive message failed");
            continue;
        }
        power_monitor_generic_msg_t *g_msg = (power_monitor_generic_msg_t *)msg->get_buffer();
        if(g_msg == NULL)
            continue;

        switch( g_msg->type ) {
            case SUPERCAP_STATUS:
            {
                supercap_msg_t *msg = (supercap_msg_t *)g_msg;
                LOG_I(TAG, "SUPERCAP_STATUS message received, status = %d", msg->status);
                pthread_mutex_lock(&supercap_status_mutex);
                POWER_MONITOR_ctx->supercap_status = msg->status;
                pthread_mutex_unlock(&supercap_status_mutex);
                if( SUPERCAP_ACTIVE == msg->status) {
                    pthread_mutex_lock(&m_mutex);
                    POWER_MONITOR_ctx->crank_change_keepalive = CRANK_SUPERCAP;
                    pthread_cond_signal(&m_cond);
                    pthread_mutex_unlock(&m_mutex);
                }
                applyPowerState(PowerStateEvent::eSupercapEvent, (SUPERCAP_ACTIVE == msg->status));
                break;
            }
            case POWERMON_CRANK_CHANGE:
                crank_change = (power_monitor_crank_change_t *)g_msg;
                LOG_I(TAG, "POWERMON_CRANK_CHANGE received");
                pthread_mutex_lock(&m_mutex);
                pthread_cond_signal(&m_cond);
                pthread_mutex_unlock(&m_mutex);
                if(crank_change->len != sizeof(power_monitor_crank_change_t))
                {
                    LOG_E(TAG, "crank_change->length != \
                                sizeof(power_monitor_crank_change_t)" );
                    break;
                }

                POWER_MONITOR_ctx->present_crank_level = crank_change->crank_level;
                POWER_MONITOR_ctx->crank_low_count.fetch_add(crank_change->crank_level == CRANK_LOW ? 1 : 0);
                POWER_MONITOR_ctx->crank_high_count.fetch_add(crank_change->crank_level == CRANK_HIGH ? 1 : 0);

                if(false == is_crank_change_valid()) {
                    LOG_I(TAG, "!!!ERROR!!!: Ignoring invalid crank change");
                    break;
                }

                POWER_MONITOR_ctx->crank_change_keepalive = crank_change->crank_level;
                if(crank_change->crank_level == CRANK_LOW){
                    process_crank_low(crank_change->crank_change_time);

                    //don't update in DB if lowpowermode is off
                    if(true == POWER_MONITOR_ctx->lowpowermode) {
                        add_event_db(power_dbstate_enum_t::DBSTATE_CRANKLOW);
                    }
                    LOG_I(TAG, "DBSTATE_CRANKLOW :: Going to shutdown" );
                    if(true == POWER_MONITOR_ctx->extend_wakeup_duration_for_misc)  {
                        LOG_I(TAG, "extend_wakeup_duration_for_misc is true, Updating DB entries");
                        db_state_info_t node = {};
                        int64_t new_event_time = POWER_MONITOR_ctx->last_wakeup_time;
                        if(true == get_db_node_event("DBSTATE_LOWPOWER_WAKEUP",&node,POWER_MONITOR_ctx->db_handle)){
                            update_db_event_time_by_index(node.index,new_event_time);
                        }
                        if(true == get_db_node_event("DBSTATE_IGNITION_GPIO_LOW",&node,POWER_MONITOR_ctx->db_handle)){
                            update_db_event_time_by_index(node.index,new_event_time);
                        }
                        if(true == get_db_node_event("DBSTATE_CRANKLOW",&node,POWER_MONITOR_ctx->db_handle)){
                            update_db_event_time_by_index(node.index,new_event_time);
                        }
                    }
                }
                else if(crank_change->crank_level == CRANK_HIGH){
                    process_crank_high(crank_change->crank_change_time);
                    add_event_db(power_dbstate_enum_t::DBSTATE_CRANKHIGH);
                    if(check_uptime(time_diff, 1)) {
                        LOG_I(TAG, "check_uptime_crankhigh returned true; send message to main controller");
                        power_monitor_maxtimeout_t maxtimeout_msg;
                        maxtimeout_msg.type        = POWERMON_MAXTIMEOUT;
                        maxtimeout_msg.len         = sizeof(power_monitor_maxtimeout_t);
                        maxtimeout_msg.time_elapsed= time_diff;

                        nd_msgq_t::nd_msg_t msg((char *)&maxtimeout_msg, sizeof(maxtimeout_msg), false);
                        POWER_MONITOR_ctx->powmon_msg_q->send(msg, nd_msgq_t::ND_MSG_MED);
                    }
                }
                else {
                    LOG_E(TAG, "SOMETHING WRONG HERE POWERMON_CRANK_CHANGE crank_change->crank_level == CRANK_ERROR ; TAKE ACTION HERE");
                    process_crank_low(crank_change->crank_change_time);
                    add_event_db(power_dbstate_enum_t::DBSTATE_CRANKERROR);
                }
                applyPowerState(PowerStateEvent::eIgnitionOffEvent, (CRANK_LOW == crank_change->crank_level));
            break; //POWERMON_CRANK_CHANGE

            case POWERMON_MAXTIMEOUT:
            {
                maxtimeout = (power_monitor_maxtimeout_t *)g_msg;
                LOG_C(TAG, "POWERMON_MAXTIMEOUT received");
                if(maxtimeout->len != sizeof(power_monitor_maxtimeout_t))
                {
                    LOG_E(TAG, "maxtimeout->length != \
                            sizeof(power_monitor_maxtimeout_t)" );
                    break;
                }

                file_touch(ignition_audio_fail);

                initiate_shutdown(0, SHUTDOWN_FOR_CYCLIC_REBOOT);
            }
            break; // POWERMON_MAXTIMEOUT
            case REQ_POWERMON_SDCARD_RO_REBOOT:
                sdcard_ro_reboot = (power_monitor_sdcard_ro_reboot_t *)g_msg;
                LOG_C(TAG, "REQ_POWERMON_SDCARD_RO_REBOOT recieved");
                if(sdcard_ro_reboot->length != sizeof(power_monitor_sdcard_ro_reboot_t)){
                    LOG_E(TAG, "reboot_timeout != \
                                sizeof(power_monitor_sdcard_ro_reboot_t)" );
                    break;
                }
                POWER_MONITOR_ctx->RO_registered = true;
                if( check_event_frequency(POWER_MONITOR_ctx->allow_sdcard_reboot_freq) == false ) {
                    LOG_I(TAG, "rebooted happened recently; ignoring this request");
                    break;
                }
                initiate_shutdown(sdcard_ro_reboot->reboot_after_secs, SHUTDOWN_FOR_SDCARD_RO_RECOVERY);
            break; // REQ_POWERMON_SDCARD_RO_REBOOT
            case POWERMON_BAD_BATTERY_VOLTAGE:
                battery_volt = (power_monitor_battery_voltage_t *)g_msg;
                LOG_C(TAG, "POWERMON_BAD_BATTERY_VOLTAGE received");
                if(battery_volt->len != sizeof(power_monitor_battery_voltage_t))
                {
                    LOG_E(TAG, "battery_volt->length != \
                                sizeof(power_monitor_battery_voltage_t)" );
                    break;
                }
                // delete check crank high and reboot file, since we want to shutdown irrespective of crank
                file_delete(nd_device_obj->get_engage_shutdown_path());
                // immediate shutdown for low voltage
                initiate_shutdown(SHUTDOWN_TIME_FOR_BAD_BATTERY_VOLTAGE, SHUTDOWN_FOR_BAD_VOLTAGE);
            break; // POWERMON_BAD_BATTERY_VOLTAGE

            case POWERMON_BAD_BATTERY_CLEAR:
                LOG_C(TAG, "POWERMON_BAD_BATTERY_CLEAR received");
                battery_volt = (power_monitor_battery_voltage_t *)g_msg;
                if(battery_volt->len != sizeof(power_monitor_battery_voltage_t))  {
                    LOG_E(TAG, "battery_volt->length != sizeof(power_monitor_battery_voltage_t)" );
                    break;
                }

                file_touch(nd_device_obj->get_engage_shutdown_path());
                postpone_shutdown(POSTPONE_SHUTDOWN_DURATION, POSTPONE_FOR_BAD_BATTERY_CLEAR);

            break; // POWERMON_BAD_BATTERY_CLEAR

            case REQ_POWERMON_SVC_TO_REBOOT:
            svc_reboot = (power_monitor_svc_reboot_req_t *)g_msg;
            LOG_C(TAG, "REQ_POWERMON_TO_SVC_REBOOT recieved");
            if(ignore_SVC_reboot && POWER_MONITOR_ctx->present_crank_level == CRANK_HIGH ) {
                LOG_E(TAG, "Ignore SVC reboot, keep device ON for %d seconds", delay_reboot_time );
                sleep(delay_reboot_time);
                break;
            }
            if(svc_reboot->length != sizeof(power_monitor_svc_reboot_req_t)) {
                LOG_E(TAG, "reboot_timeout != sizeof(power_monitor_svc_reboot_req_t)" );
                break;
            }
            initiate_shutdown(svc_reboot->reboot_after_secs, SHUTDOWN_FOR_SVC_REBOOT);
            break;

            case REQ_POWERMON_INSTALLER_APP_TO_REBOOT:
            installer_app_reboot = (power_monitor_installer_app_reboot_req_t *)g_msg;
            LOG_C(TAG, "REQ_POWERMON_TO_INSTALLER_APP_REBOOT recieved");
            if(installer_app_reboot->length != sizeof(power_monitor_installer_app_reboot_req_t)) {
                LOG_E(TAG, "reboot_timeout != sizeof(power_monitor_installer_app_reboot_req_t)" );
                break;
            }
            initiate_shutdown(installer_app_reboot->reboot_after_secs, SHUTDOWN_FOR_INSTALLER_APP);
            break;

            case REQ_POWERMON_INSTALLER_APP_CRASH_TO_REBOOT:
            installer_app_crash_reboot = (power_monitor_installer_app_crash_reboot_req_t *)g_msg;
            LOG_C(TAG, "REQ_POWERMON_INSTALLER_APP_CRASH_TO_REBOOT recieved");
            if(installer_app_crash_reboot->length != sizeof(power_monitor_installer_app_crash_reboot_req_t)) {
                LOG_E(TAG, "reboot_timeout != sizeof(power_monitor_installer_app_crash_reboot_req_t)" );
                break;
            }
            initiate_shutdown(installer_app_crash_reboot->reboot_after_secs, SHUTDOWN_FOR_INSTALLER_APP_CRASH);
            break;

            case REQ_POWERMON_MSP_FAIL_TO_REBOOT:
            msp_status = (msp_status_msg_t *)g_msg;
            LOG_C(TAG, "REQ_POWERMON_MSP_FAIL_TO_REBOOT recieved");
            if(msp_status->length != sizeof(msp_status_msg_t)) {
                LOG_E(TAG, "reboot_timeout != sizeof(msp_status_msg_t)" );
                break;
            }

            if (true == msp_status->status) {
                LOG_I(TAG, "MSP status is true, No need to reboot");
                break;
            }
            initiate_shutdown(0, SHUTDOWN_FOR_MSP_FAIL_REBOOT);
            break;

            case POWERMON_DIRECTPOLL_CRANK_CHANGE:
                directpoll_crank_msg = (power_monitor_directpolling_crank_t *)g_msg;
                LOG_I(TAG, "POWERMON_DIRECTPOLL_CRANK_CHANGE received %lld", directpoll_crank_msg->event_time );
                if(directpoll_crank_msg->len != sizeof(power_monitor_directpolling_crank_t))
                {
                    LOG_E(TAG, "directpoll_crank_msg->length != \
                                sizeof(power_monitor_directpolling_crank_t)" );
                    break;
                }
                POWER_MONITOR_ctx->present_crank_level = directpoll_crank_msg->crank_level;
                POWER_MONITOR_ctx->crank_low_count.fetch_add(directpoll_crank_msg->crank_level == CRANK_LOW ? 1 : 0);
                POWER_MONITOR_ctx->crank_high_count.fetch_add(directpoll_crank_msg->crank_level == CRANK_HIGH ? 1 : 0);

                if(false == is_crank_change_valid()) {
                    LOG_I(TAG, "!!!ERROR!!!: Ignoring invalid crank change");
                    break;
                }

                POWER_MONITOR_ctx->crank_change_keepalive = directpoll_crank_msg->crank_level; //crank_change->crank_level uses a stale/dangling pointer
                LOG_I(TAG, "POWERMON_DIRECTPOLL_CRANK_CHANGE received %lld", directpoll_crank_msg->event_time );
                if(directpoll_crank_msg->crank_level == CRANK_LOW){
                    process_crank_low(directpoll_crank_msg->event_time);
                    add_event_db(power_dbstate_enum_t::DBSTATE_CRANKLOW);
                }
                else if(directpoll_crank_msg->crank_level == CRANK_HIGH){
                    process_crank_high(directpoll_crank_msg->event_time);
                    add_event_db(power_dbstate_enum_t::DBSTATE_CRANKHIGH);
                    if(check_uptime(time_diff, 1)) {
                        LOG_I(TAG, "check_uptime_crankhigh returned true; send message to main controller");
                        power_monitor_maxtimeout_t maxtimeout_msg;
                        maxtimeout_msg.type        = POWERMON_MAXTIMEOUT;
                        maxtimeout_msg.len         = sizeof(power_monitor_maxtimeout_t);
                        maxtimeout_msg.time_elapsed= time_diff;

                        nd_msgq_t::nd_msg_t msg((char *)&maxtimeout_msg, sizeof(maxtimeout_msg), false);
                        POWER_MONITOR_ctx->powmon_msg_q->send(msg, nd_msgq_t::ND_MSG_MED);
                    }
                }
                else {
                    // other possible value is CRANK_ERROR
                    LOG_E(TAG, "SOMETHING WRONG HERE POWERMON_DIRECTPOLL_CRANK_CHANGE; TAKE ACTION");
                    process_crank_low(directpoll_crank_msg->event_time);
                    add_event_db(power_dbstate_enum_t::DBSTATE_CRANKLOW);
                }
                applyPowerState(PowerStateEvent::eIgnitionOffEvent, (CRANK_LOW == directpoll_crank_msg->crank_level));

            break; //  POWERMON_DIRECTPOLL_CRANK_CHANGE

            case POWERMON_NORMAL_RUN:
                normal_run = (power_monitor_norman_run_t *)g_msg;
                LOG_I(TAG, "POWERMON_NORMAL_RUN received");
                if(normal_run->len != sizeof(power_monitor_norman_run_t))
                { 
                    LOG_E(TAG, "normal_run->length != \
                                sizeof(power_monitor_norman_run_t)" );
                    break;
                }

                postpone_shutdown(POSTPONE_SHUTDOWN_DURATION, POSTPONE_FOR_NORMAL_RUN);
            break; // POWERMON_NORMAL_RUN

            case POWERMON_AVOID_TIMOUT:
                normal_run = (power_monitor_norman_run_t *)g_msg;
                LOG_I(TAG, "POWERMON_AVOID_TIMOUT received");
                if(normal_run->len != sizeof(power_monitor_norman_run_t))
                {
                    LOG_E(TAG, "normal_run->length != \
                                sizeof(power_monitor_norman_run_t)" );
                    break;
                }
            break; // POWERMON_AVOID_TIMOUT

            case POWERMON_INTRPT_THREAD_CRASH:
                LOG_E(TAG, "POWERMON_INTRPT_THREAD_CRASH received");
                LOG_E(TAG, "handel this in future releases");
                nd_service_obj->send_err_msg(SM_E_PM_GPIO_INT_FAIL, NDService::UNUSED_ERR_AUX_CODE, 
                        "POWERMON_INTRPT_THREAD_CRASH received" );
            break;

            case REQ_POWERMON_AWSIOT_TO_REBOOT:
            s_reboot = (power_monitor_to_reboot_t *)g_msg;
            LOG_C(TAG, "REQ_POWERMON_TO_AWSIOT_REBOOT recieved");
            if(s_reboot->length != sizeof(power_monitor_to_reboot_t)) {
                LOG_E(TAG, "reboot_timeout != sizeof(power_monitor_to_reboot_t)" );
                break;
            }

            if(check_priority(SHUTDOWN_FOR_AWSIOT, POWER_MONITOR_ctx->reason) == false){
                LOG_C(TAG, "cancelling this shutdown since check_priority returned false");
                break;
            }


            file_touch(ignition_audio_fail); // Creating this file because crank is already high so after reboot ignition_on_audio should not play.
            initiate_shutdown(s_reboot->reboot_after_secs, SHUTDOWN_FOR_AWSIOT);
            break;
            
            case REQ_POWERMON_ANALYTICS_TO_REBOOT:
            {
                s_reboot = (power_monitor_to_reboot_t *)g_msg;

                LOG_C(TAG, "REQ_POWERMON_ANALYTICS_TO_REBOOT received");

                if(check_priority(SHUTDOWN_FOR_ANALYTICS, POWER_MONITOR_ctx->reason) == false){
                    LOG_C(TAG, "cancelling this shutdown since check_priority returned false");
                    break;
                }


                file_touch(ignition_audio_fail); // Creating this file because crank is already high so after reboot ignition_on_audio should not play.
                initiate_shutdown(0, SHUTDOWN_FOR_ANALYTICS);
            }
            break;
            case REQ_POWERMON_CAM_CRASH_TO_REBOOT:
                s_reboot = (power_monitor_to_reboot_t *)g_msg;
                LOG_C(TAG, "REQ_POWERMON_TO_CAM_CRASH_REBOOT recieved");
                if(s_reboot->length != sizeof(power_monitor_to_reboot_t)) {
                     LOG_E(TAG, "reboot_timeout != sizeof(power_monitor_to_reboot_t)" );
                     break;
                }
                file_touch(ignition_audio_fail); // Creating this file because crank is already high so after reboot ignition_on_audio should not play.
                initiate_shutdown(s_reboot->reboot_after_secs, SHUTDOWN_FOR_CAM_CRASH);
                break;

            case GET_POWERMON_IGNITION_STATUS:
                {
                    generic_msg_t *gen_msg = (generic_msg_t *)g_msg;
                    LOG_I(TAG, "GET_POWERMON_IGNITION_STATUS received from client: %s", gen_msg->client_id);
                    const std::string client_id(gen_msg->client_id);
                    send_current_ignition_satus_to_client(client_id);
                }
                break;
            case RES_IDLE_UPDATE:
            {
                idle_update = (res_idle_update_msg_t *)g_msg;

                LOG_I(TAG, "POWERMON RES_IDLE_UPDATE received");
                if(idle_update->length != sizeof(res_idle_update_msg_t))
                {
                    LOG_E(TAG, "idle_update->length %d != sizeof(res_idle_update_msg_t) %d ", idle_update->length, sizeof(res_idle_update_msg_t)  );
                    break;
                }
#ifdef IGNITION_AUDIO_ALERT
                if( ( true == idle_update->idle_on ) &&
                        ( true == POWER_MONITOR_ctx->ignitionOnIdleAudioAlert ) &&
                        ( POWER_MONITOR_ctx->crank_level() == CRANK_HIGH) )
                {
                    LOG_D(TAG, "POWERMON RES_IDLE_UPDATE CRANK_HIGH ");
                    ignition_on_idle_audio_alert();
                }
                else
                {
                    //reset the counts for idle audio alert.
                    POWER_MONITOR_ctx->ignitionOnIdleAudioAlertDuration = 0;
                    POWER_MONITOR_ctx->ignitionOnIdleAudioAlertFrequency = 0;
                }
#endif
            }
            break;
            case UPLOADER_DATA_UPLOAD_STATUS:
            {
                if(POWER_MONITOR_ctx->crank_level() == CRANK_HIGH) {
                    POWER_MONITOR_ctx->uploader_data_upload_pending = false;
                    POWER_MONITOR_ctx->shutdown_delayed = false;
                    break;
                }

                uploader_data_upload_pend = (res_pend_uploader_data_upload_msg_t*) g_msg;

                LOG_I(TAG, "POWERMON UPLOADER_DATA_UPLOAD_STATUS received");

                LOG_I(TAG, "No of Pending Requests are %d , Failure Count is %d, Internet Status is %d", uploader_data_upload_pend->pend_req, uploader_data_upload_pend->retry_failure_count, uploader_data_upload_pend->internet_status);

                if( uploader_data_upload_pend->dhub_offline_check)
                {
                    LOG_I(TAG,"Only Ext Cam Request Are Pending And MDVR Is Offline So Not Delaying Shutdown For VOD Upload");
                }

                if((uploader_data_upload_pend->pend_req > 0) && (uploader_data_upload_pend->retry_failure_count < 10) && (uploader_data_upload_pend->internet_status) && (!uploader_data_upload_pend->dhub_offline_check))
                {
                    if( !reported_possible_delay_in_shutdown && POWER_MONITOR_ctx->extended_post_ignition_off_and_lpw_timer )
                    {
                        string msg = "Data Upload Pending Shutdown Maybe Delayed";
                        nd_service_obj->send_err_msg(SM_E_PM_UPLOADER_DATA_UPLOAD_PENDING, uploader_data_upload_pend->pend_req, msg);
                        reported_possible_delay_in_shutdown = true;
                    }
                    POWER_MONITOR_ctx->uploader_data_upload_pending = true;
                }
                else
                {
                    LOG_I(TAG, "NO pending requests are available so setting uploader_data_upload_pending to false");
                    POWER_MONITOR_ctx->uploader_data_upload_pending = false;

                    // Checking delay_shutdown_for_dhub_status_check is disable or not before going to shutdown
                    // To avoid shutdown in case of dhub status check is in progress
                    // Or post pone shutdown for misc lowpower wakeup is in progress
                    if((true == POWER_MONITOR_ctx->shutdown_delayed) && (false == POWER_MONITOR_ctx->delay_shutdown_for_dhub_status_check) && (false == POWER_MONITOR_ctx->delay_shutdown_for_misc_lowpower_wakeup))
                    {
                        initiate_shutdown(0, SHUTDOWN_FOR_UPLOADER_DATA_UPLOAD_DONE);
                    }
                }
            }
            break;
            case DRIVER_LOGIN_AUDIO_NOTIFY :
            {
                LOG_I(TAG, "POWERMON DRIVER_LOGIN_AUDIO_NOTIFY received");
                driver_login_audio_notify = (driver_login_audio_notify_msg_t *) g_msg;
#ifdef IGNITION_AUDIO_ALERT
                // check if message send from ndcentral
                LOG_I(TAG, "driver_login_audio_notify.client_id: %s", driver_login_audio_notify->client_id);
                if(Q_NDCENTRAL == driver_login_audio_notify->client_id) {
                    // Removing Audio Request
                    remove_played_audio_request(driver_login_audio_notify->alert_type);
                }
                else {
                    send_audio_req_to_ndcentral(driver_login_audio_notify->file, AudioEventType::DrvLoginAl);
                }
#endif
            }
            break;
            case REQ_POWERMON_METRICS:
            {
                LOG_I(TAG, "REQ_POWERMON_METRICS");
                power_metrics_msg_t* power_metrics = (power_metrics_msg_t*) g_msg;
                send_power_metrics(*power_metrics);
            }
            break;
            case REQ_DHUB_WIFI_MODE_FROM_DB:
            {
                LOG_I(TAG,"RECIEVED GET_DHUB_WIFI_MODE_DB");
                setCurrDHUBWifiModeFromDB();
            }
            break;
            case LOW_POWER_WAKEUP_CNT_UPDATE:
            {
                generic_msg_t *gen_msg = (generic_msg_t *)g_msg;
                LOG_I(TAG, "LOW_POWER_WAKEUP_CNT_UPDATE received from client: %s", gen_msg->client_id);

                send_low_power_wakeup_count_time_sync(0);
            }
            break;
            case POWERMON_DHUB_STATUS_CHECK: // This we will recieve at 6(~10 secons) minutes...which basically handles our boot up time check so effectively we will monitor after 5 minutes of recording MDVR. and from the last disconnected time to present uptime is > than 30(22+8 safety-> as it takes ~22 seconds to boot up and 6 t0 7 seconds to shutdown) seconds we can go ahead and say it has disconnected. If the difference is more than 60 seconds we will update the RTC.
            {
                power_monitor_dhub_status_check_t *dhub_status_check = (power_monitor_dhub_status_check_t *)g_msg;
                LOG_I(TAG, "POWERMON_DHUB_STATUS_CHECK received");
                if(false == reported_possible_delay_in_shutdown){
                    initiate_shutdown(0, SHUTDOWN_FOR_DHUB_STATUS_CHECK_DONE);
                    break;
                }
                if(dhub_status_check->len != sizeof(power_monitor_dhub_status_check_t))
                {
                    LOG_E(TAG, "dhub_status_check->length != sizeof(power_monitor_dhub_status_check_t)" );
                    initiate_shutdown(0, SHUTDOWN_FOR_DHUB_STATUS_CHECK_DONE);
                    break;
                }

                bool status = isMDVRConnected();// get this from the context values state and timestamp from the shutdown polling thread.
                LOG_I(TAG, "isMDVRConnected = %d", status);
                if(POWER_MONITOR_ctx->last_connected_time_dhub == 0){
                    db_state_info_t data_node;
                    if(true == get_db_node_event(power_dbstate_enum_t::toString(power_dbstate_enum_t::DBSTATE_DHUB_LAST_CONNECTED_TIME), &data_node, POWER_MONITOR_ctx->db_handle)){
                        POWER_MONITOR_ctx->last_connected_time_dhub = data_node.event_time;
                        LOG_I(TAG, "POWER_MONITOR_ctx->last_connected_time_dhub = %lld", POWER_MONITOR_ctx->last_connected_time_dhub);
                    }
                    else {
                        LOG_E(TAG, "Failed to get last connected time from DB");
                    }
                }
                if(POWER_MONITOR_ctx->last_connected_time_dhub != 0) {

                    int64_t current_monotonic_time = get_system_monotonic_time();
                    int64_t diff_mdvr_discon_time = current_monotonic_time - POWER_MONITOR_ctx->last_connected_time_dhub;
                    unsigned const int max_diff_mdvr_discon_time = 30000;
                    LOG_I(TAG, "current_monotonic_time = %lld, last_connected_time_dhub = %lld, diff_mdvr_discon_time = %lld", current_monotonic_time, POWER_MONITOR_ctx->last_connected_time_dhub, diff_mdvr_discon_time);

                    if(false == status){
                        if(llabs(diff_mdvr_discon_time) > max_diff_mdvr_discon_time){
                            unsigned const int max_time_to_sync_dhub_secs = 180;
                            // increment/decement DHUB RTC by value subject to max of 3 minutes.
                            // If not connected at all don;t do anything.
                            if(diff_mdvr_discon_time >= S_TO_MS((int64_t)max_time_to_sync_dhub_secs)){
                                rtc_wakeup_time -= S_TO_MS((int64_t)max_time_to_sync_dhub_secs);
                                add_event_db(POWER_MONITOR_ctx->db_handle, rtc_wakeup_time, POWER_MONITOR_ctx->boot_time, POWER_MONITOR_ctx->pid_num, "DBSTATE_RTC_WAKEUP_TIME_DHUB", "WAKEUP");
                            }
                        POWER_MONITOR_ctx->delay_shutdown_for_dhub_status_check = false;
                        initiate_shutdown(0, SHUTDOWN_FOR_DHUB_STATUS_CHECK_DONE);
                    }
                }
                else {
                        POWER_MONITOR_ctx->last_connected_time_dhub = current_monotonic_time;
                        int64_t shutdown_time_in_sec = (POWER_MONITOR_ctx->lowpower_wakeups > 0) ? POWER_MONITOR_ctx->lowpower_wakeup_duration : POWER_MONITOR_ctx->crank_shutdown_duration;
                        // Time Taken Since DHUB Status Check Started
                        int64_t diff =  current_monotonic_time - dhub_status_check->start_time;
                        LOG_I(TAG, "diff = %lld, start_time = %lld", diff, dhub_status_check->start_time);
                        // check the diff b/w current uptime and crank_shutdown/sleep duration.
                        if( diff >= S_TO_MS((int64_t)POWER_MONITOR_ctx->safety_time_to_sync_driveri_dhub))
                        {
                            //increment/decement DHUB RTC by value subject to max of 3 minutes
                            rtc_wakeup_time += S_TO_MS((int64_t)POWER_MONITOR_ctx->safety_time_to_sync_driveri_dhub);
                            POWER_MONITOR_ctx->delay_shutdown_for_dhub_status_check = false;
                            add_event_db(POWER_MONITOR_ctx->db_handle, rtc_wakeup_time, POWER_MONITOR_ctx->boot_time, POWER_MONITOR_ctx->pid_num, "DBSTATE_RTC_WAKEUP_TIME_DHUB", "WAKEUP");
                            initiate_shutdown(0, SHUTDOWN_FOR_DHUB_STATUS_CHECK_DONE);
                        }
                    }
                }else{
                    nd_service_obj->send_err_msg(SM_E_PM_DHUB_STATUS, NDService::UNUSED_ERR_AUX_CODE, "last_connected_time_dhub is 0" );
                    POWER_MONITOR_ctx->delay_shutdown_for_dhub_status_check = false;
                    initiate_shutdown(0, SHUTDOWN_FOR_DHUB_STATUS_CHECK_DONE);
                }
            }
            break;
            case UPDATE_DHUB_WAKEUP_TIME_IN_DB:
            {
                LOG_I(TAG, "UPDATE_DHUB_WAKEUP_TIME_IN_DB received");
                update_dhub_wakeup_time_in_db();
            }
            break;
            case POWER_MON_IGNITION_GPIO_STATUS:
            {
                power_monitor_crank_change_t *ign_gpio_status = (power_monitor_crank_change_t *)g_msg;
                LOG_I(TAG, "POWER_MON_IGNITION_GPIO_STATUS received");

                if (ign_gpio_status->len != sizeof(power_monitor_crank_change_t)) {
                    LOG_E(TAG, "ign_gpio_status->length != sizeof(power_monitor_crank_change_t)");
                    break;
                }

                if (CRANK_LOW == ign_gpio_status->crank_level) {
                    // add IGNITION_GPIO_LOW in DB
                    add_event_db(power_dbstate_enum_t::DBSTATE_IGNITION_GPIO_LOW);
                    // send message to update wakeup time in DB
                    if(true == POWER_MONITOR_ctx->ext_cam_lpw_enabled) {
                        update_dhub_wakeup_time_in_db();
                    }
                }
                else if (CRANK_HIGH == ign_gpio_status->crank_level) {
                    // add IGNITION_GPIO_HIGH in DB
                    update_dhub_wakeup_time_for_extended_shutdown = false;
                    add_event_db(power_dbstate_enum_t::DBSTATE_IGNITION_GPIO_HIGH);
                }
                else {
                    LOG_C(TAG, "Invalid crank level received from IGNITION_GPIO, value: %d", ign_gpio_status->crank_level);
                }
            }
            break;
            default:
                LOG_E(TAG, "Reached Default in power_monitor_msg_loop; breaking");
            break; // default
        }
        delete msg;
    }
    LOG_E(TAG, "Exiting from power_monitor_msg_loop");

    return 1;
}

void set_freq_cycles()
{
    freq_lpw_cycles[0] = 0;
    int i;

    for(i = 0; i < 4; i++)
    {
        freq_lpw_cycles[i+1] = freq_lpw_cycles[i] +  int(ONE_DAY_IN_SECONDS/((POWER_MONITOR_ctx->lowpower_wakeup_cycle_duration << i) + POWER_MONITOR_ctx->lowpower_wakeup_duration));
    }
    for (i = 5; i <= 11; i++)
    {
        freq_lpw_cycles[i] = freq_lpw_cycles[i-1] + 1;
    }
}

#define GET_BAGHEERA_POWER_CONFIG(entry, default_val,get_override_val, is_val_overrriden ) \
              POWER_MONITOR_ctx->bagheera_config->getConfig("power", entry, default_val, get_override_val, is_val_overrriden)

int main(int argc, char *argv[])
{
    nd_service_obj = NDService::get_service_obj(TAG);
    printf("initilizing logger\n");
    bool status_log = nd_log_init( log_dir.c_str() );
    if(status_log == false) {
        printf("unable to init logger ; Exiting from power_monitor\n");
        nd_service_obj->send_err_msg(SM_E_PM_LOG_INIT_FAIL, NDService::UNUSED_ERR_AUX_CODE, "unable to init logger :: Exiting from main" );
        return -1;
    }
    
#ifdef ROUTE_LOGS
    route_logs( log_dir.c_str() );
#endif
    LOG_E(TAG, "#####STARTING POWER MONITOR#####");

    nd_device_obj_init();
    set_nd_service_ext_object(nd_service_obj);
    // This Flag configures the min max voltage for 12/24V Input Battery on power_mon init
    POWER_MONITOR_ctx->is_battery_cfg_init = false;
    POWER_MONITOR_ctx->powmon_msg_q =  nd_msgq_t::get_msgq( 
                                            POWER_MONITOR_ctx->power_monitor_q_name,
                                            nd_msgq_t::ND_MSGQ_SERVER);
    if( POWER_MONITOR_ctx->powmon_msg_q == NULL ) {
        LOG_E(TAG, "failed in power_monitor get_msgq ; Exiting from power_monitor");
        return -1;
    }

#ifdef IGNITION_BROADCAST
    server.create_topic(TOPIC_POWER_MON_IGN_STATUS);
#endif


    if (pthread_mutex_init(&POWER_MONITOR_ctx->power_state_mutex, NULL) != 0){
        string str_msg = " mutex init failed ; Exiting from power_monitor";
        LOG_C(TAG, str_msg.c_str());
        nd_service_obj->send_err_msg(SM_E_PM_MTX_INIT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        return -1;
    }

    file_touch(nd_device_obj->get_engage_shutdown_path());
    if(file_is_present(prev_shutdown_speed_file))
    {
        ifstream prev_shutdown_speed;
        prev_shutdown_speed.open(prev_shutdown_speed_file);
        prev_shutdown_speed >> prev_speed;
        prev_shutdown_speed.close();
    }
    // Logic to identify if the device has booted up or came out of standby
    int64_t standby_uptime = get_standby_monotonic_time();
    if(standby_uptime == 0) {
        LOG_I(TAG, "Device has booted up");
    }
    else {
        LOG_I(TAG, "Device has come out of Standby or service restarted. standby uptime = %lld", standby_uptime);
    }

    //////////////// POWER MONITOR DB SETUP ///////////////////////
    bool ret;
    PM_DB_PATH_NAME = nd_device_obj->get_db_base_path() + "power_monitor.db";

    string db_path_file = PM_DB_PATH_NAME;
    ret = POWER_MONITOR_ctx->open_db(db_path_file.c_str(), &POWER_MONITOR_ctx->db_handle);
    if(ret == false)    {
        // Notify health mon
        string str_msg = "PM Failed to open DB";
        LOG_C(TAG, str_msg.c_str() );
        nd_service_obj->send_err_msg(SM_E_PM_DB_OPEN_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        POWER_MONITOR_ctx->db_handle = NULL;
    }
    LOG_I(TAG, "success in open DB");
    if (pthread_mutex_init(&POWER_MONITOR_ctx->db_handle_mutex, NULL) != 0){
        // Notify health mon
        string str_msg = "db_handle_mutex init failed ; Exiting from circ_buff";
        LOG_C(TAG, str_msg.c_str() );
        nd_service_obj->send_err_msg(SM_E_PM_DB_MUTEX_INIT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        POWER_MONITOR_ctx->db_handle = NULL;
    }
    LOG_I(TAG, "success mutex init");
    ret = POWER_MONITOR_ctx->create_table_db( POWER_MONITOR_ctx->db_handle);
    if(ret == false)    {
        // Notify health mon
        string str_msg = "PM Failed to create_table_db";
        LOG_C(TAG, str_msg.c_str() );
        nd_service_obj->send_err_msg(SM_E_PM_DB_CREATION_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        POWER_MONITOR_ctx->db_handle = NULL;
    }
    LOG_I(TAG, "success in create_table_db");

    db_limit_rows(POWER_MONITOR_ctx->db_handle);

    //////////////// POWER MONITOR DB SETUP ///////////////////////

    all_thread_keepalive_status &= ~0x01;      // make 1st bit 0 for main thread  
    svc_util_init(POWER_MONITOR_ctx->power_monitor_q_name,0);
    svc_util_send_keepalive(all_thread_keepalive_status);

    bool get_override_val = true, val_overridden = false;
    POWER_MONITOR_ctx->allow_sdcard_reboot_freq = default_allow_sdcard_reboot_freq;

    // Check ext_cam feature is enabled or not
    ext_cam_feature_enabled = is_ext_cam_feature_enabled();
    POWER_MONITOR_ctx->ext_cam_lpw_enabled = dhub_lpw_enabled();
    LOG_I(TAG, "ext_cam_lpw_enabled: %d", POWER_MONITOR_ctx->ext_cam_lpw_enabled);

    //  initialize safety wakeup time for bad battery voltage shutdown
    POWER_MONITOR_ctx->safety_wakeup_time_for_bad_voltage_shutdown = (ONE_YEAR_IN_MINUTES * SECS_IN_A_MIN); // 1 year

    if(init_config_bagheera() == false)
    {
	    LOG_E(TAG, "Unable to read config; taking default values");
	    LOG_E(TAG, "Proceeding with default values");

	    POWER_MONITOR_ctx->lowpowermode =  (("on" == default_lowpowermode) ? true : false);
        POWER_MONITOR_ctx->freq_low_power_wakeup = (("enable" == default_freq_low_power_wakeup) ? true : false);
	    POWER_MONITOR_ctx->max_lowpower_wakeups = atoi(default_max_lowpower_wakeups.c_str());
	    POWER_MONITOR_ctx->fsck_lowpower_wakeup = atoi(default_fsck_lowpower_wakeup.c_str());

	    POWER_MONITOR_ctx->max_uptime_secs = 
		    atoi(default_cyclic_reboot_duration.c_str())*SECS_IN_A_MIN;
	    POWER_MONITOR_ctx->crank_shutdown_duration =
	            atoi(default_crank_shutdown_duration.c_str())*SECS_IN_A_MIN;

	    POWER_MONITOR_ctx->lowpower_wakeup_cycle_duration = 
		    atoi(default_lowpower_wakeup_cycle_duration.c_str())*SECS_IN_A_MIN;
	    POWER_MONITOR_ctx->lowpower_wakeup_duration = 
		    atoi(default_lowpower_wakeup_duration.c_str())*SECS_IN_A_MIN;

	POWER_MONITOR_ctx->lowpower_wakeup_long_cycle_threshold = atoi(default_lowpower_wakeup_long_cycle_threshold.c_str());
        POWER_MONITOR_ctx->lowpower_wakeup_long_cycle_duration = atoi(default_lowpower_wakeup_long_cycle_duration.c_str());

	    POWER_MONITOR_ctx->safety_wakeup = default_safety_wakeup * SECS_IN_A_MIN; // seconds

	    POWER_MONITOR_ctx->min_voltage_limit = (float)atof(default_min_voltage_limit.c_str());
	    POWER_MONITOR_ctx->max_voltage_limit = (float)atof(default_min_voltage_limit_24V.c_str());

	    POWER_MONITOR_ctx->min_voltage_limit_24V = (float)atof(default_min_voltage_limit_24V.c_str());
	    POWER_MONITOR_ctx->max_voltage_limit_24V = (float)atof(default_max_voltage_limit.c_str());

	    POWER_MONITOR_ctx->min_speed_for_battery_cfg_init = (int32_t)atoi(default_min_speed_for_battery_config_init.c_str());
	    
        POWER_MONITOR_ctx->abnormal_voltage_wait_duration = 
		    atoi(default_abnormal_voltage_wait_duration.c_str());
        POWER_MONITOR_ctx->extended_post_ignition_off_and_lpw_timer = ((default_extended_post_ignition_off_and_lpw_timer == "true") ? true : false);

        POWER_MONITOR_ctx->suspend_mode = ( ( "on" == default_suspend_mode) ? true : false);

#ifdef IGNITION_AUDIO_ALERT
	    /* FEATURE:: AUDIO ALERT MESSAGE ON IGNITION CRANK HIGH(ON) TO INFORM THE DRIVER 
	       THAT DEVICE IS RECORDING BOTH VIDEO AND AUDIO.......................*/
	    POWER_MONITOR_ctx->ignitionOnAudioAlert 	= ((default_ignition_on_audio_alert == "true") ? true : false); 
	    POWER_MONITOR_ctx->ignitionOnAudioAlertFile.assign( nd_device_obj->get_ignition_on_audio_alert_file());
	    string_to_integer(default_ignition_on_audio_alert_interval, POWER_MONITOR_ctx->ignitionOnAudioAlertInterval );
	    POWER_MONITOR_ctx->lastIgnitionOnAudioAlertTime = DEFAULT_IGNITION_ALERT_TIME; 
	    POWER_MONITOR_ctx->ignitionOnIdleAudioAlert 	= ((default_ignition_on_idle_audio_alert == "true") ? true : false);
	    POWER_MONITOR_ctx->ignitionOnIdleAudioAlertFile.assign( nd_device_obj->get_ignition_on_idle_audio_alert_file());
	    POWER_MONITOR_ctx->ignitionOnIdleAudioAlertThresholdDuration = DEFAULT_IGNITION_IDLE_ALERT_DURATION;
            POWER_MONITOR_ctx->ignitionOnIdleAudioAlertThresholdFrequency = DEFAULT_IGNITION_IDLE_ALERT_FREQUENCY;
#endif

        if (true == POWER_MONITOR_ctx->ext_cam_lpw_enabled) {
            POWER_MONITOR_ctx->safety_time_to_sync_driveri_dhub = (default_safety_time_to_sync_driveri_dhub_secs); // seconds
            LOG_I(TAG, "setting safety_time_to_sync_driveri_dhub to default time: %d", POWER_MONITOR_ctx->safety_time_to_sync_driveri_dhub);

            POWER_MONITOR_ctx->dhub_status_check_enabled = (("true" == default_dhub_status_check_enabled) ? true : false);
            LOG_I(TAG, "dhub_status_check_enabled: %d", POWER_MONITOR_ctx->dhub_status_check_enabled);
        }

        // wakeup duration if device wakeup as miscellanous wakeup
        POWER_MONITOR_ctx->misc_wakeup_duration = (default_misc_wakeup_duration * SECS_IN_A_MIN); // seconds

        // max time to wait for uploader activity before shutdown
        POWER_MONITOR_ctx->max_postpone_shutdown_time_uploader_activity = (default_max_postpone_shutdown_time_uploader_activity * SECS_IN_A_MIN); // seconds

        // time to delay reboot for limited reboot feature
        delay_reboot_time =  default_delay_reboot_time; // seconds

        // max reboot count for B2B reboot
        max_B2B_reboot_allowed = default_max_B2B_reboot_allowed;

        POWER_MONITOR_ctx->non_lpm_crank_low_wakeup_duration = (default_non_lpm_crank_low_wakeup_duration * SECS_IN_A_MIN); // seconds
        POWER_MONITOR_ctx->wake_on_motion_imu = ("false" == default_wake_on_motion_imu_non_lpm) ? false : true;
        POWER_MONITOR_ctx->wake_on_motion_aon = ("false" == default_wake_on_motion_aon_non_lpm) ? false : true;
        POWER_MONITOR_ctx->wake_on_ign = ("false" == default_wake_on_ign_non_lpm) ? false : true;
        POWER_MONITOR_ctx->wake_on_misc = ("false" == default_wake_on_misc_non_lpm) ? false : true;
    }
    else
    {
	    string lowpowermode_string = POWER_MONITOR_ctx->bagheera_config->
		    getConfig("power","enable_lowpowermode",default_lowpowermode, get_override_val, val_overridden);
	    POWER_MONITOR_ctx->lowpowermode = (("on" == lowpowermode_string) ? true : false);

        string freq_low_power_wakeup_string = POWER_MONITOR_ctx->bagheera_config->
		    getConfig("power","frequent_low_power_wakeup",default_freq_low_power_wakeup, get_override_val, val_overridden);
	    POWER_MONITOR_ctx->freq_low_power_wakeup = (( "enable" == freq_low_power_wakeup_string ) ? true : false);

	    string max_lowpower_wakeups_string = POWER_MONITOR_ctx->bagheera_config->
		    getConfig("power","max_lowpower_wakeups",default_max_lowpower_wakeups, get_override_val, val_overridden);
	    POWER_MONITOR_ctx->max_lowpower_wakeups = atoi(max_lowpower_wakeups_string.c_str());

	    string fsck_lowpower_wakeup_string = POWER_MONITOR_ctx->bagheera_config->
		    getConfig("power", "fsck_lowpower_wakeup", default_fsck_lowpower_wakeup, get_override_val, val_overridden);
	    POWER_MONITOR_ctx->fsck_lowpower_wakeup = atoi(fsck_lowpower_wakeup_string.c_str());
	string lowpower_wakeup_long_cycle_threshold_string = POWER_MONITOR_ctx->bagheera_config->
                getConfig("power","lowpower_wakeup_long_cycle_threshold",default_lowpower_wakeup_long_cycle_threshold, get_override_val, val_overridden);
        POWER_MONITOR_ctx->lowpower_wakeup_long_cycle_threshold = atoi(lowpower_wakeup_long_cycle_threshold_string.c_str());

        string lowpower_wakeup_long_cycle_duration_string = POWER_MONITOR_ctx->bagheera_config->
                getConfig("power","lowpower_wakeup_long_cycle_duration",default_lowpower_wakeup_long_cycle_duration, get_override_val, val_overridden);
        POWER_MONITOR_ctx->lowpower_wakeup_long_cycle_duration = atoi(lowpower_wakeup_long_cycle_duration_string.c_str())*SECS_IN_A_MIN;

	    string max_uptime_str = POWER_MONITOR_ctx->bagheera_config->
		    getConfig("power","cyclic_reboot_duration",default_cyclic_reboot_duration, get_override_val, val_overridden);
	    POWER_MONITOR_ctx->max_uptime_secs = atoi(max_uptime_str.c_str())*SECS_IN_A_MIN;

        string crank_shutdown_duration_str = POWER_MONITOR_ctx->bagheera_config->getConfig("power", "crank_shutdown_duration", default_crank_shutdown_duration, get_override_val, val_overridden);
        LOG_I(TAG, "crank_shutdown_duration from config in mins: %s", crank_shutdown_duration_str.c_str());
        POWER_MONITOR_ctx->crank_shutdown_duration = atoi(crank_shutdown_duration_str.c_str()) * SECS_IN_A_MIN;

        // storing the original crank_shutdown_duration value for dhub shutdown
        if (POWER_MONITOR_ctx->ext_cam_lpw_enabled) {
           POWER_MONITOR_ctx->dhub_crank_shutdown_duration = POWER_MONITOR_ctx->crank_shutdown_duration;

           LOG_I(TAG, "dhub_crank_shutdown_duration: %d", POWER_MONITOR_ctx->dhub_crank_shutdown_duration);
        }

        string veh_data = POWER_MONITOR_ctx->bagheera_config->getConfig("vehicle_data", "enabled", "false", get_override_val, val_overridden);
        if(veh_data == "true"){
            vehicle_data_enabled = true;
            LOG_I(TAG, "Vehicle data enabled");
        }
        if (nd_device_obj->is_master_shutdown_supported()) {

            string master_shutdown_enable_str = POWER_MONITOR_ctx->bagheera_config->getConfig("power", "master_shutdown_enable", default_master_shutdown_enable, get_override_val, val_overridden);
            if (master_shutdown_enable_str == "true") {
                POWER_MONITOR_ctx->crank_shutdown_duration = 0;
                LOG_I(TAG, "master_shutdown_enable == true. Setting crank_shutdown_duration = 0");
            }
        }

        if (nd_device_obj->is_wake_on_motion_supported()) {
            string apm_motion_detection_str = POWER_MONITOR_ctx->bagheera_config->getConfig("apm", "apm_motion_detection", "false", get_override_val, val_overridden);
            // If motion detection feature is enabled, we take 3 minutes to detect if device is stationary
            // Hence, reducing this 3 minute delay from the crash_shutdown_suration to keep the device ON for
            // constant time irrespective of feature enabled/disabled
            if ("true" == apm_motion_detection_str) {
                if (POWER_MONITOR_ctx->crank_shutdown_duration > (3 * 60))
                {
                    LOG_I(TAG, "motion detection feature enabled, reducing the crank_shutdown_duration by 3 mins");
                    POWER_MONITOR_ctx->crank_shutdown_duration -= (3 * 60);
                }
                else {
                    LOG_I(TAG, "motion detection feature enabled and crank_shutdown_duration config <= 3 min, forcing to zero");
                    POWER_MONITOR_ctx->crank_shutdown_duration = 0;
                }
                apm_motion_detection = true;
            }
            else {
                LOG_I(TAG, "motion detection feature: %s", apm_motion_detection_str.c_str());
            }
        }

        string lowpower_wakeup_cycle_duration = POWER_MONITOR_ctx->bagheera_config->
		    getConfig("power","lowpower_wakeup_cycle_duration",default_lowpower_wakeup_cycle_duration, get_override_val, val_overridden);
	    POWER_MONITOR_ctx->lowpower_wakeup_cycle_duration = atoi(lowpower_wakeup_cycle_duration.c_str())*SECS_IN_A_MIN;
        bool dta_enabled = false;
        #ifdef AUTOMATION
        dta_enabled = isAutomationEnabled();
        #endif
        if (dta_enabled == true) {
            LOG_I(TAG,"Defaulting min_valid_cyclic_duration_val to 900 because device is in staging environment");
            min_valid_cyclic_duration_val = 900;
        }
        if(POWER_MONITOR_ctx->lowpower_wakeup_cycle_duration < min_valid_cyclic_duration_val)
        {
            LOG_I(TAG,"lowpower_wakeup_cycle_duration is less than 30, setting it to 30");
            POWER_MONITOR_ctx->lowpower_wakeup_cycle_duration = min_valid_cyclic_duration_val; // changing value to 30 mins if lowpower_wakeup_cycle_duration is less than 30 mins
        }

        if( (true == POWER_MONITOR_ctx->freq_low_power_wakeup) && (POWER_MONITOR_ctx->lowpower_wakeup_cycle_duration > max_valid_cyclic_duration_val_for_freq_lpw))
        {
            LOG_I(TAG,"lowpower_wakeup_cycle_duration is greater than 60 when freq lpw is enabled, setting it to 60");
            POWER_MONITOR_ctx->lowpower_wakeup_cycle_duration = max_valid_cyclic_duration_val_for_freq_lpw; // changing value to 60 mins if lowpower_wakeup_cycle_duration is greater than 60 mins when freq lpw is enabled
            nd_service_obj->send_err_msg(SM_E_PM_FREQUENT_LPW_CYCLE_DURATION, NDService::UNUSED_ERR_AUX_CODE, "Frequent LPW Cycle Duration Overridden To 60(min)" );
        }

        string lowpower_wakeup_duration = POWER_MONITOR_ctx->bagheera_config->
		    getConfig("power","lowpower_wakeup_duration",default_lowpower_wakeup_duration, get_override_val, val_overridden);
	    POWER_MONITOR_ctx->lowpower_wakeup_duration = atoi(lowpower_wakeup_duration.c_str())*SECS_IN_A_MIN;

        string safety_wakeup = POWER_MONITOR_ctx->bagheera_config->
            getConfig("power","safety_wakeup", to_string(default_safety_wakeup), get_override_val, val_overridden);
        if(true == string_to_integer(safety_wakeup, POWER_MONITOR_ctx->safety_wakeup)) {
            POWER_MONITOR_ctx->safety_wakeup *= SECS_IN_A_MIN; // seconds
        }
        else {
            LOG_E(TAG, "Failed to convert safety_wakeup string to integer, setting to default value");
            POWER_MONITOR_ctx->safety_wakeup = default_safety_wakeup * SECS_IN_A_MIN; // seconds
        }

	    string min_voltage_limit = POWER_MONITOR_ctx->bagheera_config->
		    getConfig("power","min_voltage_limit_V",default_min_voltage_limit, get_override_val, val_overridden);
	    POWER_MONITOR_ctx->min_voltage_limit = (float)atof(min_voltage_limit.c_str());
	    string max_voltage_limit = POWER_MONITOR_ctx->bagheera_config->
		    getConfig("power","max_voltage_limit_V",default_min_voltage_limit_24V, get_override_val, val_overridden);
	    POWER_MONITOR_ctx->max_voltage_limit = (float)atof(max_voltage_limit.c_str());

	    string min_voltage_limit_24V = POWER_MONITOR_ctx->bagheera_config->
		    getConfig("power","min_voltage_limit_24V",default_min_voltage_limit_24V, get_override_val, val_overridden);
	    POWER_MONITOR_ctx->min_voltage_limit_24V = (float)atof(min_voltage_limit_24V.c_str());
	    string max_voltage_limit_24V = POWER_MONITOR_ctx->bagheera_config->
		    getConfig("power","max_voltage_limit_24V",default_max_voltage_limit, get_override_val, val_overridden);
	    POWER_MONITOR_ctx->max_voltage_limit_24V = (float)atof(max_voltage_limit_24V.c_str());
	    
        string min_speed_for_voltage_config = POWER_MONITOR_ctx->bagheera_config->
		    getConfig("power","min_speed_for_battery_config",default_min_speed_for_battery_config_init , get_override_val, val_overridden);
	    POWER_MONITOR_ctx->min_speed_for_battery_cfg_init = (int32_t)atoi(min_speed_for_voltage_config.c_str());



	    string abnormal_voltage_wait_duration = POWER_MONITOR_ctx->bagheera_config->
		    getConfig("power","abnormal_voltage_wait_duration",default_abnormal_voltage_wait_duration, get_override_val, val_overridden);
	    POWER_MONITOR_ctx->abnormal_voltage_wait_duration = atoi(abnormal_voltage_wait_duration.c_str());
	    string allow_sdcard_reboot_freq = POWER_MONITOR_ctx->bagheera_config->
		    getConfig("power","allow_sdcard_reboot_freq", std::to_string(default_allow_sdcard_reboot_freq), get_override_val, val_overridden);
	    POWER_MONITOR_ctx->allow_sdcard_reboot_freq = atoi(allow_sdcard_reboot_freq.c_str());
        POWER_MONITOR_ctx->extended_post_ignition_off_and_lpw_timer = ((POWER_MONITOR_ctx->bagheera_config->getConfig("power","extended_post_ignition_off_and_lpw_timer", default_extended_post_ignition_off_and_lpw_timer, get_override_val, val_overridden) == "true") ? true : false);
        int bootup_time_by_device = 0;
        if(true == string_to_integer(POWER_MONITOR_ctx->bagheera_config->getConfig("power","default_bootup_time", to_string(default_bootup_time_by_device), get_override_val, val_overridden),bootup_time_by_device))
        {
                default_bootup_time_by_device = (int64_t)bootup_time_by_device;
                LOG_I(TAG, "default_bootup_time_by_device: %lld", default_bootup_time_by_device);
        }else{
                LOG_I(TAG," Failed to convert default_bootup_time string to integer, setting to default value");
        }
#ifdef IGNITION_AUDIO_ALERT
	    /* FEATURE:: AUDIO ALERT MESSAGE ON IGNITION CRANK HIGH(ON) TO INFORM THE DRIVER 
	       THAT DEVICE IS RECORDING BOTH VIDEO AND AUDIO.......................*/

	    POWER_MONITOR_ctx->ignitionOnAudioAlert	= ((GET_BAGHEERA_POWER_CONFIG("ignition_on_audio_alert", default_ignition_on_audio_alert, get_override_val, val_overridden) == "true") ? true : false);
	    POWER_MONITOR_ctx->ignitionOnAudioAlertFile.assign( GET_BAGHEERA_POWER_CONFIG("ignition_on_audio_alert_file", nd_device_obj->get_ignition_on_audio_alert_file(), get_override_val, val_overridden));
	    if( false == string_to_integer(GET_BAGHEERA_POWER_CONFIG("ignition_on_audio_alert_interval", default_ignition_on_audio_alert_interval, get_override_val, val_overridden), POWER_MONITOR_ctx->ignitionOnAudioAlertInterval))
	    {
	            POWER_MONITOR_ctx->ignitionOnAudioAlertInterval = DEFAULT_IGNITION_ALERT_INTERVAL;

	    }

	    POWER_MONITOR_ctx->lastIgnitionOnAudioAlertTime = DEFAULT_IGNITION_ALERT_TIME; 
	    POWER_MONITOR_ctx->ignitionOnIdleAudioAlert = ((GET_BAGHEERA_POWER_CONFIG("ignition_on_idle_audio_alert", default_ignition_on_idle_audio_alert, get_override_val, val_overridden) == "true") ? true : false);
	    POWER_MONITOR_ctx->ignitionOnIdleAudioAlertFile.assign( GET_BAGHEERA_POWER_CONFIG("ignition_on_idle_audio_alert_file", nd_device_obj->get_ignition_on_idle_audio_alert_file(), get_override_val, val_overridden));
	    if( false == string_to_integer(GET_BAGHEERA_POWER_CONFIG("ignition_on_idle_audio_alert_duration", default_ignition_on_idle_audio_alert_duration, get_override_val, val_overridden), POWER_MONITOR_ctx->ignitionOnIdleAudioAlertThresholdDuration))
	    {
	            POWER_MONITOR_ctx->ignitionOnIdleAudioAlertThresholdDuration = DEFAULT_IGNITION_IDLE_ALERT_DURATION;

	    }
	    if( false == string_to_integer(GET_BAGHEERA_POWER_CONFIG("ignition_on_idle_audio_alert_frequency", default_ignition_on_idle_audio_alert_frequency, get_override_val, val_overridden), POWER_MONITOR_ctx->ignitionOnIdleAudioAlertThresholdFrequency))
	    {
	            POWER_MONITOR_ctx->ignitionOnIdleAudioAlertThresholdFrequency = DEFAULT_IGNITION_IDLE_ALERT_FREQUENCY;

	    }
#endif

        // Setting default value for suspend mode
        POWER_MONITOR_ctx->suspend_mode = false;

        string suspend_mode = POWER_MONITOR_ctx->bagheera_config->getConfig("power","suspend_mode",default_suspend_mode, get_override_val, val_overridden);
        if("on" == suspend_mode) {
            if(true == nd_device_obj->is_suspend_mode_supported()) {
                LOG_C(TAG, "suspend mode is supported");
                POWER_MONITOR_ctx->suspend_mode = true;
            }
        }

        // safety time to sync Driveri and DHUB
        if (true == POWER_MONITOR_ctx->ext_cam_lpw_enabled) {
            string safety_time_to_sync_driveri_dhub_str = POWER_MONITOR_ctx->bagheera_config->
                getConfig("power","time_to_sync_driveri_dhub", to_string(default_safety_time_to_sync_driveri_dhub_secs), get_override_val, val_overridden);
            if(false == string_to_integer(safety_time_to_sync_driveri_dhub_str, POWER_MONITOR_ctx->safety_time_to_sync_driveri_dhub)) {
                LOG_E(TAG, "Failed to convert safety_time_to_sync_driveri_dhub string to integer, setting to default value %d", default_safety_time_to_sync_driveri_dhub_secs);
                POWER_MONITOR_ctx->safety_time_to_sync_driveri_dhub = (default_safety_time_to_sync_driveri_dhub_secs); // seconds
            }
            LOG_I(TAG, "safety_time_to_sync_driveri_dhub: %d", POWER_MONITOR_ctx->safety_time_to_sync_driveri_dhub);

            string dhub_status_check_enabled = POWER_MONITOR_ctx->bagheera_config->getConfig("power","dhub_status_check_enabled",default_dhub_status_check_enabled, get_override_val, val_overridden);
            POWER_MONITOR_ctx->dhub_status_check_enabled = (("true" == dhub_status_check_enabled) ? true : false);
            LOG_I(TAG, "dhub_status_check_enabled: %d", POWER_MONITOR_ctx->dhub_status_check_enabled);
        }

        // safety wakeup time for BAD voltage shutdown
        {
            string safety_wakeup_bad_voltage_str = POWER_MONITOR_ctx->bagheera_config->getConfig("power","safety_wakeup_bad_voltage",to_string(default_safety_wakeup_time_for_bad_voltage_shutdown), get_override_val, val_overridden);
            if (true == string_to_integer(safety_wakeup_bad_voltage_str, POWER_MONITOR_ctx->safety_wakeup_time_for_bad_voltage_shutdown)){
                POWER_MONITOR_ctx->safety_wakeup_time_for_bad_voltage_shutdown *= SECS_IN_A_MIN; // seconds
            }
            else {
                POWER_MONITOR_ctx->safety_wakeup_time_for_bad_voltage_shutdown = (ONE_YEAR_IN_MINUTES * SECS_IN_A_MIN); // 1 year
            }
        }

        {
            string misc_wakeup_duration = POWER_MONITOR_ctx->bagheera_config->getConfig("power","misc_wakeup_duration",to_string(default_misc_wakeup_duration), get_override_val, val_overridden);
            if (true == string_to_integer(misc_wakeup_duration, POWER_MONITOR_ctx->misc_wakeup_duration)){
                POWER_MONITOR_ctx->misc_wakeup_duration *= SECS_IN_A_MIN; // seconds
            }
            else {
                POWER_MONITOR_ctx->misc_wakeup_duration = (default_misc_wakeup_duration * SECS_IN_A_MIN); // seconds
            }
        }

        {
            string max_postpone_shutdown_time_uploader_activity = POWER_MONITOR_ctx->bagheera_config->getConfig("power","max_postpone_shutdown_time_uploader_activity",to_string(default_max_postpone_shutdown_time_uploader_activity), get_override_val, val_overridden);
            if (true == string_to_integer(max_postpone_shutdown_time_uploader_activity, POWER_MONITOR_ctx->max_postpone_shutdown_time_uploader_activity)){
                POWER_MONITOR_ctx->max_postpone_shutdown_time_uploader_activity *= SECS_IN_A_MIN; // seconds
            }
            else {
                POWER_MONITOR_ctx->max_postpone_shutdown_time_uploader_activity = (default_max_postpone_shutdown_time_uploader_activity * SECS_IN_A_MIN); // seconds
            }
        }

        {
            string delay_reboot_time_string = POWER_MONITOR_ctx->bagheera_config->
            getConfig("power","delay_reboot_time", to_string(default_delay_reboot_time), get_override_val, val_overridden);
            if ( false == string_to_integer(delay_reboot_time_string, delay_reboot_time)) {
                LOG_E(TAG, "Failed to convert delay_reboot_time  string to integer, setting to default value");
                delay_reboot_time = default_delay_reboot_time;
            }
        }

        {
            string max_B2B_reboot_allowed_str = POWER_MONITOR_ctx->bagheera_config->
            getConfig("power","max_B2B_reboot_allowed", to_string(default_max_B2B_reboot_allowed), get_override_val, val_overridden);
            if(false == string_to_integer(max_B2B_reboot_allowed_str, max_B2B_reboot_allowed)) {
                LOG_E(TAG, "Failed to convert max_B2B_reboot_allowed string to integer, setting to default value");
                max_B2B_reboot_allowed = default_max_B2B_reboot_allowed;
            }
        }

        {
            string non_lpm_crank_low_wakeup_duration_str = POWER_MONITOR_ctx->bagheera_config->
                getConfig("power","non_lpm_wakeup_duration", to_string(default_non_lpm_crank_low_wakeup_duration), get_override_val, val_overridden);
            if (true == string_to_integer(non_lpm_crank_low_wakeup_duration_str, POWER_MONITOR_ctx->non_lpm_crank_low_wakeup_duration)) {
                POWER_MONITOR_ctx->non_lpm_crank_low_wakeup_duration *= SECS_IN_A_MIN; // seconds
            }
            else {
                POWER_MONITOR_ctx->non_lpm_crank_low_wakeup_duration = (default_non_lpm_crank_low_wakeup_duration * SECS_IN_A_MIN); // seconds
                LOG_E(TAG, "Failed to convert non_lpm_crank_low_wakeup_duration string to integer, setting to default value %d", POWER_MONITOR_ctx->non_lpm_crank_low_wakeup_duration);
            }

            string wake_on_motion_imu_str = POWER_MONITOR_ctx->bagheera_config->
                getConfig("power","nlpm_on_imu", default_wake_on_motion_imu_non_lpm, get_override_val, val_overridden);
            POWER_MONITOR_ctx->wake_on_motion_imu = (("true" == wake_on_motion_imu_str) ? true : false);
            LOG_I(TAG, "wake_on_imu: %d", POWER_MONITOR_ctx->wake_on_motion_imu);

            string wake_on_motion_aon_record_str = POWER_MONITOR_ctx->bagheera_config->
                getConfig("power","nlpm_on_por", default_wake_on_motion_aon_non_lpm, get_override_val, val_overridden);
            POWER_MONITOR_ctx->wake_on_motion_aon = (("true" == wake_on_motion_aon_record_str) ? true : false);
            LOG_I(TAG, "wake_on_aon: %d", POWER_MONITOR_ctx->wake_on_motion_aon);

            string wake_on_ign_record_str = POWER_MONITOR_ctx->bagheera_config->
                getConfig("power","nlpm_on_ign", default_wake_on_ign_non_lpm, get_override_val, val_overridden);
            POWER_MONITOR_ctx->wake_on_ign = (("true" == wake_on_ign_record_str) ? true : false);
            LOG_I(TAG, "wake_on_ign: %d", POWER_MONITOR_ctx->wake_on_ign);

            string wake_on_misc_record_str = POWER_MONITOR_ctx->bagheera_config->
                getConfig("power","nlpm_on_misc", default_wake_on_misc_non_lpm, get_override_val, val_overridden);
            POWER_MONITOR_ctx->wake_on_misc = (("true" == wake_on_misc_record_str) ? true : false);
            LOG_I(TAG, "wake_on_misc: %d", POWER_MONITOR_ctx->wake_on_misc);

        }
        {
            string max_allowed_crank_changes_str = POWER_MONITOR_ctx->bagheera_config->
                getConfig("power","max_allowed_crank_changes", to_string(max_allowed_crank_changes), get_override_val, val_overridden);
            if((false == string_to_integer(max_allowed_crank_changes_str, max_allowed_crank_changes)) || (max_allowed_crank_changes < MIN_ALLOWED_CRANK_CHANGES) || (max_allowed_crank_changes > MAX_ALLOWED_CRANK_CHANGES)) {
                LOG_E(TAG, "Failed to convert max_allowed_crank_changes string to integer or value out of range  %d, setting to default value", max_allowed_crank_changes);
                max_allowed_crank_changes = DEF_ALLOWED_CRANK_CHANGES;
            }
            string max_crank_events_to_debounce_str = POWER_MONITOR_ctx->bagheera_config->
                getConfig("power","max_crank_events_to_debounce", to_string(max_crank_events_to_debounce), get_override_val, val_overridden);
            if((false == string_to_integer(max_crank_events_to_debounce_str, max_crank_events_to_debounce)) || (max_crank_events_to_debounce < MIN_CRANK_EVENTS_TO_DEBOUNCE) || (max_crank_events_to_debounce > MAX_CRANK_EVENTS_TO_DEBOUNCE)) {
                LOG_E(TAG, "Failed to convert max_crank_events_to_debounce string to integer or value out of range %d, setting to default value", max_crank_events_to_debounce);
                max_crank_events_to_debounce = DEF_CRANK_EVENTS_TO_DEBOUNCE;
            }
            LOG_I(TAG, "max_allowed_crank_changes: %d, max_crank_events_to_debounce: %d", max_allowed_crank_changes, max_crank_events_to_debounce);
        }

        {
            std::string ign_check_duration_str = POWER_MONITOR_ctx->bagheera_config->getConfig("power","ign_check_duration", std::to_string(DEF_IGN_CHECK_DURATION), get_override_val, val_overridden);
            if(false == string_to_integer(ign_check_duration_str, POWER_MONITOR_ctx->ign_check_duration)) {
                LOG_E(TAG, "Failed to convert ign_check_duration string to integer, setting to default value");
                POWER_MONITOR_ctx->ign_check_duration = DEF_IGN_CHECK_DURATION;
            }
            LOG_I(TAG, "ign_check_duration: %d seconds", POWER_MONITOR_ctx->ign_check_duration);
        }
    }
    if (true == POWER_MONITOR_ctx->ext_cam_lpw_enabled) {
        LOG_I(TAG, "Default values for dhub_wakeup_sync_extra %d and dhub_shutdown_sync_extra %d",dhub_wakeup_sync_extra, dhub_shutdown_sync_extra);
        string dhub_wakeup_sync_extra_str = POWER_MONITOR_ctx->bagheera_config->
            getConfig("power","dhub_wakeup_sync_extra", to_string(DEFAULT_TIME_DHUB_WAKEUP_SYNC), get_override_val, val_overridden);
        if (false == string_to_integer(dhub_wakeup_sync_extra_str, dhub_wakeup_sync_extra)) {
            dhub_wakeup_sync_extra = DEFAULT_TIME_DHUB_WAKEUP_SYNC;
            LOG_E(TAG, "Failed to convert dhub_wakeup_sync_extra string to integer, setting to default value %d", dhub_wakeup_sync_extra);
        }
        string dhub_shutdown_sync_str = POWER_MONITOR_ctx->bagheera_config->
            getConfig("power","dhub_shutdown_sync_extra", to_string(DEFAULT_TIME_DHUB_SHUTDOWN_SYNC), get_override_val, val_overridden);
        if (false == string_to_integer(dhub_shutdown_sync_str, dhub_shutdown_sync_extra)) {
            dhub_shutdown_sync_extra = DEFAULT_TIME_DHUB_SHUTDOWN_SYNC;
            LOG_E(TAG, "Failed to convert dhub_shutdown_sync_extra string to integer, setting to default value %d", dhub_shutdown_sync_extra);
        }
        LOG_I(TAG, "dhub_wakeup_sync_extra: %d dhub_shutdown_sync_extra: %d", dhub_wakeup_sync_extra, dhub_shutdown_sync_extra);
        // POWER_MONITOR_ctx->lowpower_wakeup_cycle_duration += dhub_wakeup_sync_extra + dhub_shutdown_sync_extra;
        // LOG_I(TAG,"Added dhub_wakeup_sync_extra: %d and dhub_shutdown_sync_extra: %d to lowpower_wakeup_cycle_duration %d", dhub_wakeup_sync_extra, dhub_shutdown_sync_extra,
        //     POWER_MONITOR_ctx->lowpower_wakeup_cycle_duration);
    }
    // Check if PowerStateContoller is enabled and load all required configs related to it
    POWER_MONITOR_ctx->ps_obj.loadPowerStateConfig(PowerStateService::ePowerMonitor);

    {
        // Setting non_lpm_crank_low_wakeup based on the wakeup sources set by the user
        power_crank_levels_t crank_level = POWER_MONITOR_ctx->crank_level();
        if((power_crank_levels_t::CRANK_LOW == crank_level)) {
            POWER_MONITOR_ctx->non_lpm_crank_low_wakeup = POWER_MONITOR_ctx->wake_on_motion_imu ||
                                                        POWER_MONITOR_ctx->wake_on_motion_aon ||
                                                        POWER_MONITOR_ctx->wake_on_ign;
            LOG_I(TAG, "non_lpm_crank_low_wakeup: %d", POWER_MONITOR_ctx->non_lpm_crank_low_wakeup);
        }
    }
    // Initializing the current voltage value to prev_voltage to avoid any misinformation
    {
        POWER_MONITOR_ctx->current_voltage = get_prev_voltage();
        LOG_I(TAG, "Previous voltage read -> %.2f", POWER_MONITOR_ctx->current_voltage);
    }

    data_record_status_db db_data;
    if(get_data_record_status_db(db_data))
    {
        LOG_I(TAG,"Read record_data succesfully");
    }
    else
    {
        LOG_E(TAG,"Reading of record_data failed");
    }
    if(false == db_data.enabled)
    {
        POWER_MONITOR_ctx->ignitionOnAudioAlert = false;
    }

    min_voltage_limit_read_from_config_12V = POWER_MONITOR_ctx->min_voltage_limit;
    //read_ext_camera_common_config(ext_cam_feature_enabled);
    if(ext_cam_feature_enabled)
    {
        setCurrDHUBWifiModeFromDB();
    }

    if(init_config_device() == true) {
        deviceid = POWER_MONITOR_ctx->device_config->getConfig("identity","deviceid","");
        devicetype = POWER_MONITOR_ctx->device_config->getConfig("identity","deviceType","");
    }

    if(init_config_nddevice() == true) {
        otaversion = POWER_MONITOR_ctx->nddevice->getConfig("version","nddevice","");
    }

    if(init_config_cloud() == true) {
        cloud_server = POWER_MONITOR_ctx->cloud_config->getConfig("cloud","server",DEF_INI_SERVER);
        server_url = POWER_MONITOR_ctx->cloud_config->getConfig(cloud_server,"injestion",DEF_INI_SERVER_URL);
        version = POWER_MONITOR_ctx->cloud_config->getConfig("cloud","injection-version",DEF_INI_API_VERSION);
    }

    // if lowpowermode is disabled, then log it as critical
    if(false == POWER_MONITOR_ctx->lowpowermode) {
        LOG_C(TAG, "Lowpowermode is disabled");
    }

    LOG_I(TAG, "lowpowermode %d", POWER_MONITOR_ctx->lowpowermode);
    LOG_I(TAG, "max_lowpower_wakeups %d", POWER_MONITOR_ctx->max_lowpower_wakeups);
    LOG_I(TAG, "lowpower_wakeup_long_cycle_threshold %d", POWER_MONITOR_ctx->lowpower_wakeup_long_cycle_threshold);
    LOG_I(TAG, "lowpower_wakeup_long_cycle_duration %d", POWER_MONITOR_ctx->lowpower_wakeup_long_cycle_duration);

    LOG_I(TAG, "max_uptime_secs %d", POWER_MONITOR_ctx->max_uptime_secs);
    LOG_I(TAG, "crank_shutdown_duration %d", POWER_MONITOR_ctx->crank_shutdown_duration);
    LOG_I(TAG, "lowpower_wakeup_cycle_duration %d", POWER_MONITOR_ctx->lowpower_wakeup_cycle_duration);
    LOG_I(TAG, "lowpower_wakeup_duration %d", POWER_MONITOR_ctx->lowpower_wakeup_duration);
    LOG_I(TAG, "frequent lowpower wakeup  %d", POWER_MONITOR_ctx->freq_low_power_wakeup);
    LOG_I(TAG, "extended_post_ignition_off_and_lpw_timer %d",POWER_MONITOR_ctx->extended_post_ignition_off_and_lpw_timer);
    LOG_I(TAG, "safety_wakeup %d", POWER_MONITOR_ctx->safety_wakeup);
    LOG_I(TAG, "min_voltage_limit %f", POWER_MONITOR_ctx->min_voltage_limit);
    LOG_I(TAG, "max_voltage_limit %f", POWER_MONITOR_ctx->max_voltage_limit);
    LOG_I(TAG, "min_voltage_limit_24V %f", POWER_MONITOR_ctx->min_voltage_limit_24V);
    LOG_I(TAG, "max_voltage_limit_24V %f", POWER_MONITOR_ctx->max_voltage_limit_24V);
    LOG_I(TAG, "min_voltage_limit_read_from_config_12V %f", min_voltage_limit_read_from_config_12V);
    LOG_I(TAG, "min_speed_for_battery_cfg_init %d", POWER_MONITOR_ctx->min_speed_for_battery_cfg_init);
    LOG_I(TAG, "abnormal_voltage_wait_duration %d",
                 POWER_MONITOR_ctx->abnormal_voltage_wait_duration);
    LOG_I(TAG, "POWER_MONITOR_ctx->suspend_mode %d", POWER_MONITOR_ctx->suspend_mode);

#ifdef IGNITION_AUDIO_ALERT
    LOG_I(TAG, "ignitionOnAudioAlert %d", POWER_MONITOR_ctx->ignitionOnAudioAlert);
    LOG_I(TAG, "ignitionOnAudioAlertFile %s", POWER_MONITOR_ctx->ignitionOnAudioAlertFile.c_str());
    LOG_I(TAG, "ignitionOnAudioAlertInterval %d", POWER_MONITOR_ctx->ignitionOnAudioAlertInterval);
    LOG_I(TAG, "lastIgnitionOnAudioAlertTime %lld", POWER_MONITOR_ctx->lastIgnitionOnAudioAlertTime);
    LOG_I(TAG, "ignitionOnIdleAudioAlert %d", POWER_MONITOR_ctx->ignitionOnIdleAudioAlert);
    LOG_I(TAG, "ignitionOnIdleAudioAlertFile %s", POWER_MONITOR_ctx->ignitionOnIdleAudioAlertFile.c_str());
    LOG_I(TAG, "ignitionOnIdleAudioAlertThresholdDuration %d", POWER_MONITOR_ctx->ignitionOnIdleAudioAlertThresholdDuration);
    LOG_I(TAG, "ignitionOnIdleAudioAlertThresholdFrequency %d", POWER_MONITOR_ctx->ignitionOnIdleAudioAlertThresholdFrequency);
#endif
#ifdef BAGHEERA2
    if(writetogpio_edgefile("both") == false) {
        LOG_E(TAG, "writetogpio_edgefile both returned false;");
        string str_msg = "gpio171_interrupt_thread may have issues; we will continue to run";
        LOG_E(TAG, str_msg.c_str());
        nd_service_obj->send_err_msg(SM_E_PM_GPIO171_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
    }
#endif

    LOG_I(TAG, "lowpowermode %d", POWER_MONITOR_ctx->lowpowermode);
    LOG_I(TAG, "delay_reboot_time %d", delay_reboot_time);
    LOG_I(TAG, "max_B2B_reboot_allowed %d", max_B2B_reboot_allowed);
    LOG_I(TAG, "safety_wakeup_time_for_bad_voltage_shutdown %d", POWER_MONITOR_ctx->safety_wakeup_time_for_bad_voltage_shutdown);
    LOG_I(TAG, "misc_wakeup_duration %d", POWER_MONITOR_ctx->misc_wakeup_duration);
    LOG_I(TAG, "max_postpone_shutdown_time_uploader_activity %d", POWER_MONITOR_ctx->max_postpone_shutdown_time_uploader_activity);
    LOG_I(TAG, "non_lpm_crank_low_wakeup_duration %d", POWER_MONITOR_ctx->non_lpm_crank_low_wakeup_duration);

    // TODO: save all LPW realted values as backup
    lpw_data.crank_shutdown_duration = POWER_MONITOR_ctx->crank_shutdown_duration;
    lpw_data.lowpower_wakeup_duration = POWER_MONITOR_ctx->lowpower_wakeup_duration;
    lpw_data.lowpower_wakeup_cycle_duration = POWER_MONITOR_ctx->lowpower_wakeup_cycle_duration;
    lpw_data.lowpower_wakeup_long_cycle_duration = POWER_MONITOR_ctx->lowpower_wakeup_long_cycle_duration;

    POWER_MONITOR_ctx->reason = SHUTDOWN_CANCELLED;
    POWER_MONITOR_ctx->present_crank_level = CRANK_ERROR;
    POWER_MONITOR_ctx->crank_low_count.store(0);
    POWER_MONITOR_ctx->crank_high_count.store(0);

    // Setting default value of uploader_data_upload_pending as false
    POWER_MONITOR_ctx->uploader_data_upload_pending = false;

    // initialize to default variables
    pthread_mutex_lock(&POWER_MONITOR_ctx->power_state_mutex);
    POWER_MONITOR_ctx->power_state.monotonic_time = get_system_monotonic_time();
    POWER_MONITOR_ctx->power_state.reason = POSTPONE_FOR_NORMAL_RUN;
    POWER_MONITOR_ctx->power_state.time_gap = POSTPONE_SHUTDOWN_DURATION;
    pthread_mutex_unlock(&POWER_MONITOR_ctx->power_state_mutex);

    POWER_MONITOR_ctx->crank_change_keepalive = CRANK_ERROR;
    
    double last_known_lat = INVALID_LAT, last_known_lon = INVALID_LONG;
    read_last_known_valid_gps_data(last_known_lat, last_known_lon);
    POWER_MONITOR_ctx->gps_pos.lat = last_known_lat;
    POWER_MONITOR_ctx->gps_pos.lon = last_known_lon;
    POWER_MONITOR_ctx->gps_pos.speed = 0;
    POWER_MONITOR_ctx->gps_pos.valid = false;
    POWER_MONITOR_ctx->gps_pos.accuracy = 1000; // Set a high accuracy value
    POWER_MONITOR_ctx->gps_pos.timestamp = get_system_time();
    POWER_MONITOR_ctx->RO_registered = false;

    // initialize POWER_MONITOR_ctx->present_crank_change_time
    POWER_MONITOR_ctx->present_crank_change_time = get_system_time();

    // reading gpio ignition status and updating it in ign_gpio sysfs entry
    // TODO : move to driver in 6.14
    write_into_sysfs_entry(nd_device_obj->get_ign_gpio_sysfs_path() , static_cast<int>(nd_device_obj->get_ignition_status()));

    // initialize the POWER_MONITOR_ctx->wakeup_reason
    POWER_MONITOR_ctx->wakeup_reason = PowerOnTriggerT::POWER_ON_TRIGGER;

    // reading last RTC time
    POWER_MONITOR_ctx->last_rtc_alarm_time = convert_epoch_format(get_rtc_wakeup_time(), DigitsOfEpoch::eDigits_Seconds);

    task_result_t timed_task_result;
    POWER_MONITOR_ctx->boot_time = 0;
    timed_task_result = nd_timed_task(read_boot_time_tt, 1, NULL, "read_boot_time");
    if (timed_task_result != TASK_SUCCESS) {
        LOG_E (TAG, "nd_timed_task for read_boot_time_tt failed");
        POWER_MONITOR_ctx->boot_time = get_system_time();
    }
    LOG_I(TAG, "POWER_MONITOR_ctx->boot_time %lld", POWER_MONITOR_ctx->boot_time);
    POWER_MONITOR_ctx->service_start_time_mono = get_system_monotonic_time();
    LOG_I(TAG, "POWER_MONITOR_ctx->service_start_time_mono %lld", POWER_MONITOR_ctx->service_start_time_mono);
    POWER_MONITOR_ctx->pid_num = (int)getpid();
    LOG_I(TAG, "POWER_MONITOR_ctx->pid_num %d", POWER_MONITOR_ctx->pid_num);

    string reason_str = "";
    if (nd_device_obj->get_reset_wake_reason(POWER_MONITOR_ctx->power_on_off_reason, reason_str) == false) {
        LOG_E(TAG, "get_reset_wake_reason failed");
    }

    if(true == POWER_MONITOR_ctx->freq_low_power_wakeup)
    {
        set_freq_cycles();
        POWER_MONITOR_ctx->max_lowpower_wakeups = freq_lpw_cycles[11];
    }

    // initialize the lowpower_wakeups count
    POWER_MONITOR_ctx->lowpower_wakeups = 0;

    POWER_MONITOR_ctx->lowpower_wakeups = count_lowpower_wakeups(POWER_MONITOR_ctx->db_handle);
    LOG_I(TAG, "POWER_MONITOR_ctx->lowpower_wakeups %d", POWER_MONITOR_ctx->lowpower_wakeups);

    // initialize the misc_wakeup_count
    POWER_MONITOR_ctx->misc_wakeup_count = 0;

    POWER_MONITOR_ctx->misc_wakeup_count = count_misc_wakeups(POWER_MONITOR_ctx->db_handle);
    LOG_I(TAG, "POWER_MONITOR_ctx->misc_wakeup_count %d", POWER_MONITOR_ctx->misc_wakeup_count);
    write_into_sysfs_entry(nd_factory_utils::get_misc_wakeup_count_sysfs_path(), POWER_MONITOR_ctx->misc_wakeup_count);

    POWER_MONITOR_ctx->previous_shutdown_reason = previous_shutdown_reason(POWER_MONITOR_ctx->db_handle);

    power_crank_levels_t power_crank_level = POWER_MONITOR_ctx->crank_level();

    // In starting of power monitor if power_crank_level is CRANK_LOW then calling ignition_gpio_status_cb for initial for POWER_MON_IGNITION_GPIO_STATUS
    if(power_crank_levels_t::CRANK_LOW == power_crank_level) {
        int crank_level = power_crank_level + '0';
        LOG_I(TAG, "calling ignition_gpio_status_cb with CRANK_LOW to update starting DB entry for POWER_MON_IGNITION_GPIO_STATUS");
        ignition_gpio_status_cb((void *)crank_level);
        manual_ignition_cb_done = true;
    }

    if ( power_crank_levels_t::CRANK_HIGH == power_crank_level ) {
        LOG_I(TAG, "reseting to 0 POWER_MONITOR_ctx->lowpower_wakeups %d", POWER_MONITOR_ctx->lowpower_wakeups);
        POWER_MONITOR_ctx->lowpower_wakeups = 0;
        LOG_I(TAG, "resetting to 0 POWER_MONITOR_ctx->misc_wakeup_count %d", POWER_MONITOR_ctx->misc_wakeup_count);
        POWER_MONITOR_ctx->misc_wakeup_count = 0;
    }

    if ( strncmp(POWER_MONITOR_ctx->previous_shutdown_reason.c_str(), "NA", 2) == 0 ) {

        string delimiter = "RESET_REASON:";
        reason_str = reason_str.substr( reason_str.find(delimiter) + delimiter.length(), reason_str.length());
        //reason_str = reason_str + " (Speed: " + to_string(prev_speed)  + ") ";
        POWER_MONITOR_ctx->previous_shutdown_reason = reason_str;
    }
    nd_service_obj->send_err_msg(SM_E_PM_DB_PREV_SHUTDOWN, POWER_MONITOR_ctx->power_on_off_reason,
            "Previous Shutdown: " + POWER_MONITOR_ctx->previous_shutdown_reason + " (Speed: " + to_string(prev_speed)  + ")");


    send_low_power_wakeup_count_time_sync(POWER_MONITOR_ctx->lowpower_wakeups);
    LOG_I(TAG, "POWER_MONITOR_ctx->previous_shutdown_reason %s", POWER_MONITOR_ctx->previous_shutdown_reason.c_str());

    if(POWER_MONITOR_ctx->lowpower_wakeups > POWER_MONITOR_ctx->max_lowpower_wakeups) {
        LOG_E(TAG, "POWER_MONITOR_ctx->lowpower_wakeups %d > POWER_MONITOR_ctx->max_lowpower_wakeups %d", 
                POWER_MONITOR_ctx->lowpower_wakeups, POWER_MONITOR_ctx->max_lowpower_wakeups);
        LOG_E(TAG, "suppoese to wakeup only max_lowpower_wakeups times; should be a quick crank change or a bug ?");
    }

    std::string ndmb_gps_client = "NDMB_POWER_MON_SERVICE";
    NDMBClient msg_client_gps(ndmb_gps_client);
    LOG_I(TAG, "subscribe for GPS data");
    msg_client_gps.subscribe(TOPIC_GPS_DATA, ndmb_gps_cb, 300, 100);
    //ignition_register_cmd( );
    pthread_t gpio171_interrupt_thread; // gpio intrupt thread
    // direct poling thread 
    // this thread will poll for 
    // 1. crank voltage (incase if we miss tha callback)
    // 2. system uptime
    // 3. temperature
    // 4. battery voltage
    pthread_t direct_poling_thread; 
    // seperate thread looks for time out and initiate
    // shoudown or reboot after time out
    // this is to avoid unwanted shutdown because of RTC reset
    pthread_t shutdown_poling_thread;
    // thread for sending keepalive upon crank change
    pthread_t keepalive_powerstate_thread;

    pthread_t ign_gpio_status_thread;

    if(true == nd_factory_utils::is_obd_volt_supported()) {

        if (false == obd_adc_open()) {
            LOG_E(TAG,"obd_adc open failed ");
        }

        obd_adc_thread_info_t *tinfo;
        tinfo = (obd_adc_thread_info_t *)malloc(sizeof(obd_adc_thread_info_t));
        if (tinfo == NULL) {
            LOG_E(TAG,"Memory allcation failed for obc adc thread");
            return false;
        }
        memset(tinfo,0,sizeof(obd_adc_thread_info_t));
        int thread_ret= pthread_create(&tinfo->obd_thread_id, NULL, &obd_adc_subs_funcptr , NULL);
        if(0 != thread_ret) {
            LOG_E(TAG, "pthread create failed by error: %d", thread_ret);
            return false;
        }
    }

    //Check if battery_voltage info file is available if not create and Update with 12V
    float battery_volt = 0.0;
    if( access( BATTERY_VOLTAGE_FILE.c_str() ,F_OK ) == -1 )
    {
        ofstream file;
        file.open (BATTERY_VOLTAGE_FILE);
        file << BATTERY_VOLTAGE_12V;
        file.close();
    }
    {
        ifstream file;
        file.open(BATTERY_VOLTAGE_FILE);
        file >> battery_volt;
        file.close();
    }
    LOG_I(TAG, " BATTERY VOLTAGE FROM %s is: %f volts", BATTERY_VOLTAGE_FILE.c_str(), battery_volt);

    // thread for adc voltage value
    pthread_t adc_voltage_thread;

#ifndef DISABLE_INTERRUPT_THREAD
    pthread_create(&gpio171_interrupt_thread, NULL, gpio171_interrupt_thread_fn, NULL);
    pthread_create(&ign_gpio_status_thread, NULL, ign_gpio_status_thread_fn, NULL);
#endif
    pthread_create(&direct_poling_thread, NULL, direct_poling_thread_fn, NULL);
    pthread_create(&shutdown_poling_thread, NULL, shutdown_poling_thread_fn, NULL);
    pthread_create(&keepalive_powerstate_thread, NULL, keepalive_powerstate_thread_fn, NULL);
#if defined(BAGHEERA2)   
    pthread_create(&adc_voltage_thread, NULL, adc_voltage_thread_fn, NULL);
#endif


#ifdef UNIT_TEST
    LOG_I(TAG, "Creating thread for test_driveri_audio");
    thread t1(test_driveri_audio);
#endif

    audio_monitor_util_init(); // Create audio monitor thread

    power_monitor_msg_loop();

    LOG_I(TAG, "waiting for threads to complete their job");
#ifndef DISABLE_INTERRUPT_THREAD
    pthread_join(gpio171_interrupt_thread, NULL);
    pthread_join(ign_gpio_status_thread, NULL);
#endif
    pthread_join(direct_poling_thread, NULL);
    pthread_join(shutdown_poling_thread, NULL);
    pthread_join(keepalive_powerstate_thread, NULL);
#ifdef UNIT_TEST
    t1.join();
#endif


#if defined(BAGHEERA2)   
    pthread_join(adc_voltage_thread,NULL);
#endif

#if defined(BAGHEERA2)   
    pthread_join(adc_voltage_thread,NULL);
#endif

    audio_monitor_util_deinit(); // Join audio monitor thread

    deinit_config();
#ifdef IGNITION_AUDIO_ALERT
    // Release AUDIO SOCKET Resource.

    if( POWER_MONITOR_ctx->zmq_context_audio && POWER_MONITOR_ctx->zmq_publisher_audio )
    {
	    zmq_close (POWER_MONITOR_ctx->zmq_publisher_audio);
	    zmq_ctx_destroy (POWER_MONITOR_ctx->zmq_context_audio);
    }

    POWER_MONITOR_ctx->zmq_publisher_audio = NULL;
    POWER_MONITOR_ctx->zmq_context_audio = NULL;
#endif

    LOG_E(TAG, "Exiting from POWER MONITOR");
    nd_service_obj->release_service_obj();
    return 0;
}

#ifdef UNIT_TEST

void test_driveri_audio() {
    // send audio alert
    LOG_I(TAG, "test_driveri_audio");
    driver_login_audio_notify_msg_t msg;
    strcpy(msg.file, "/data/nd_files/autocam/audio/nd_debug2/seatbelt.wav");

    int a;

    while (1) {
        cout << "Enter a number: ";
        cin >> a;

        if (a > 1) {
            sleep(1);
            if (false == send_msg((generic_msg_t *)&msg, (msg_type_t)DRIVER_LOGIN_AUDIO_NOTIFY, sizeof(msg), Q_POWERMON, Q_POWERMON, 0))
            {
                LOG_E(TAG, "Sending msg to btfv to restart bluetooth activities failed");
                return;
            }
        }
    }
}
#endif

bool ndmb_gps_cb(ndmb_generic_msg_t *msg)
{
    if (msg == NULL) {
        LOG_E(TAG, "msg is NULL");
        return false;
    }
    gps_msg_t gps_update ;


    string topic = msg->topic;
    if(topic == TOPIC_GPS_DATA)
    { 
        gps_update = *(reinterpret_cast<gps_msg_t *>( msg ));
        LOG_D(TAG, "POWERMON RES_GPS_UPDATE received");
        LOG_D(TAG, "gps_update->valid %d", gps_update.valid);
        LOG_D(TAG, "gps_update->latitude %f", gps_update.latitude);    
        LOG_D(TAG, "gps_update->longitude %f", gps_update.longitude);      
        LOG_D(TAG, "gps_update->speed %f", gps_update.speed);
        LOG_D(TAG, "gps_update->timestamp %lld", gps_update.timestamp);
        LOG_D(TAG, "gps_update->accuracy %f", gps_update.accuracy);
        
        //update POWER_MONITOR_ctx->gps_pos only when crank_change_keepalive is idle (init AND after reached_cloud OR max_retry_cnt reached).
        if( POWER_MONITOR_ctx->crank_change_keepalive == CRANK_ERROR ) {
            POWER_MONITOR_ctx->gps_pos.lat = gps_update.latitude;
            POWER_MONITOR_ctx->gps_pos.lon = gps_update.longitude;
            POWER_MONITOR_ctx->gps_pos.speed = gps_update.speed;
            POWER_MONITOR_ctx->gps_pos.timestamp = gps_update.timestamp;
            POWER_MONITOR_ctx->gps_pos.accuracy = gps_update.accuracy;
            POWER_MONITOR_ctx->gps_pos.valid = gps_update.valid;
        }

        gps_health_updates[gps_health_index].valid = gps_update.valid;
        gps_health_updates[gps_health_index].speed = gps_update.speed;
        gps_health_updates[gps_health_index].lat = gps_update.latitude;
        gps_health_updates[gps_health_index].lon = gps_update.longitude;
        gps_health_updates[gps_health_index].accuracy = gps_update.accuracy;


        // Increment index and wrap around if necessary
        gps_health_index = (gps_health_index + 1) % NUM_GPS_UPDATES_PER_MINUTE;
    }  
    else
    {
        LOG_E(TAG, "Invalid topic received: %s", topic.c_str());
        return false;
    }
    return true;      
}

float get_last_average_speed(int num_samples) {
    if (num_samples <= 0) {
        num_samples = 1;
    }

    if (num_samples > NUM_GPS_UPDATES_PER_MINUTE) {
        num_samples = NUM_GPS_UPDATES_PER_MINUTE;
    }

    float sum = 0.0f;
    int count = 0;

    for (int i = 0; i < num_samples; i++) {
        int idx = (gps_health_index - 1 - i + NUM_GPS_UPDATES_PER_MINUTE) % NUM_GPS_UPDATES_PER_MINUTE;
        if( (true == gps_health_updates[idx].valid) && (gps_health_updates[idx].accuracy < MIN_GPS_ACCURACY) ) {
            sum += gps_health_updates[idx].speed;
            count++;
        }
    }

    return (count == 0) ? 0.0f : (sum / count);
}
