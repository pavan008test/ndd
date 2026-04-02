/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Y Suresh Kumar <suresh.kumar@netradyne.com>, April 2017
 */

#ifndef POWER_MONITOR_H
#define POWER_MONITOR_H

#include <sstream> 
#include <cstring>
#include <inttypes.h>
#include <component.h>
#include <error.h>

#include <log.h>
#include <config_parser.h>
#include <nd_msgq.h>
#include <nd_msg_types.h>
#include <nd_auth_utils.h>
#include <sqlite3.h> 
#include <nd_factory.h>
#include <nd_base.h>
#include <atomic>
#include <cpu_scheduler_utils.h>

#define STARTING_DELAY 5 // starting delay to makesure system crontab completes its job
#define ND_DEVICE_REL_PATH "/home/ubuntu/.nddevice"

static const string log_dir = "/home/ubuntu/.nddevice/log/power_mon";
static const string prev_volt_file = "/home/ubuntu/.nddevice/prev_volt.info";
static const string prev_valid_speed_file = "/home/ubuntu/.nddevice/prev_valid_speed.info"; 

//took from krait
static const string power_monitor_log = "/home/ubuntu/.nddevice/log/power_monitor.log"; 
static const string login_fname = "/dev/shm/driveri_login_successfull";

//time in mins to keep the device on after crank voltage goes down
static const string default_crank_shutdown_duration="3" ;
static const string default_master_shutdown_enable="false" ;

static const string default_lowpowermode="on";
static const string default_freq_low_power_wakeup="disable";
static const string default_max_lowpower_wakeups="240";
static const string default_lowpower_wakeup_long_cycle_threshold="32";
static const string default_lowpower_wakeup_long_cycle_duration="1440";
static const string default_fsck_lowpower_wakeup="2";
//duration of cyclic reboot in mins
//static const string default_lowpowermode_duration="60" ;                   
static const string default_enable_cyclic_reboot="true";
//cyclic reboot duration in mins
static const string default_cyclic_reboot_duration="60" ;
static const string default_powermodule_enable="false";
// low power mode cycle duration
static const string default_lowpower_wakeup_cycle_duration="180"; 
static const string default_lowpower_wakeup_duration="5"; 
static const int default_safety_wakeup = 10; // wakeup after 10 mins incase of unplaneed shutdown

static const string BATTERY_VOLTAGE_FILE = "/home/ubuntu/.nddevice/battery_voltage";
static const float BATTERY_VOLTAGE_12V = 12.01; // 12V Battery.
static const float BATTERY_VOLTAGE_24V = 24.01; // 24V Battery.
static const string default_min_voltage_limit="10.02";
static const string default_max_voltage_limit_12V="15.02";
static const string default_min_voltage_limit_24V="20.02";
static const string default_max_voltage_limit="32.02";// New Power Adapter Support both 12V and 24V Battery Inputs.
static const string default_min_speed_for_battery_config_init= "5";// default minimum speed(mph) to mark battery_config_init true.
static const string default_abnormal_voltage_wait_duration="6";

static const string default_suspend_mode = "off";
static float min_voltage_limit_read_from_config_12V = 10.02;

static const int default_allow_sdcard_reboot_freq = 24*60*60; // allow only once reboot in every 24hrs
static int min_valid_cyclic_duration_val = 1800;
static const int max_valid_cyclic_duration_val_for_freq_lpw = 3600;
static const string default_extended_post_ignition_off_and_lpw_timer = "false";
#ifdef IGNITION_AUDIO_ALERT

#define DEFAULT_IGNITION_ALERT_INTERVAL 30
#define DEFAULT_IGNITION_ALERT_TIME     -1
static const string default_ignition_on_audio_alert="false";
static const string default_ignition_on_audio_alert_interval="30"; // seconds


#define DEFAULT_IGNITION_IDLE_ALERT_DURATION  5 // 5 Minutes
#define DEFAULT_IGNITION_IDLE_ALERT_FREQUENCY 0 // Unlimited Alerts. Alert on every ignition idle threshold duration mark.
static const string default_ignition_on_idle_audio_alert="false";
static const string default_ignition_on_idle_audio_alert_duration ="5"; //minutes
static const string default_ignition_on_idle_audio_alert_frequency ="0"; //unlimited alerts

#endif

static const string default_driveri_app_login = "false";

static const string QNAME_TIME_SYNC = "TIME_SYNC";

#define DEVICE_CONFIG_INI "/home/ubuntu/config/deviceconfig.ini"
#define ND_DEVICE_INI "/home/ubuntu/.nddevice/nddevice.ini"
#define BAGHEERA_CONFIG_INI "/home/ubuntu/.nddevice/latest/bagheera_config.ini"
#define CLOUD_CONFIG_INI "/home/ubuntu/.nddevice/latest/cloudconfig.ini"

static const string DEF_INI_SERVER = "prod";
static const string DEF_INI_API_VERSION = "v1";
static const string DEF_INI_SERVER_URL = "https://idms.netradyne.com/restserver/api";

static const string KEEPALIVE_URL = "keep-alive" ;
#ifdef KRAIT
static const string KEEPALIVE_RESPONSE_FILE = "/data/nd_files/cloud_response/power_keepaliveresponse.txt";
#else
static const string KEEPALIVE_RESPONSE_FILE = "/home/ubuntu/.nddevice/power_keepaliveresponse.txt";
#endif
#define     SECS_IN_A_MIN           (60)
#define     SLEEP_CYCLE_DURATION    (1*SECS_IN_A_MIN/2) // sleep cycle duration 1/2 min
#define     SHUTDOWN_TH_SLEEP_CYCLE_DURATION    (3)
#define     MIN_SHUTDOWN_DELAY_SECS (5) // 5 secs min delay before initiating shutting down
#define     POSTPONE_SHUTDOWN_DURATION (6*SECS_IN_A_MIN) // postpone shutdown to 6 mins

//TODO Seconds... or milliseconds
#define     KEEPALIVE_SLEEP_DURATION  (20)
#define     NUM_GPS_UPDATES_PER_MINUTE 60  

static const int REBOOT_TASK_TIMEOUT = 30;
static const int POR_REBOOT_TIME = 20;
static const int MAX_SYNC_TIME = 120;

constexpr int DEF_IGN_CHECK_DURATION = 5; // seconds

#define ND_MAX(A,B) (A) > (B) ? (A) : (B)
#define ND_MIN(A,B) (A) < (B) ? (A) : (B)

#define SELECT_POWERSTATES                  "SELECT * from POWERSTATES"
#define STATE_VEC_SIZE_ZERO                 0
#define SELECT_POWERSTATES_EVENT_EQ(event)  "SELECT * from POWERSTATES WHERE EVENT == \'" << event << "\'"
#define ORDER_BY_INDEXID_DESC_LIMIT_1       " ORDER BY INDEXID DESC LIMIT 1"
#define ORDER_BY_INDEXID_ASC_LIMIT_1        " ORDER BY INDEXID ASC LIMIT 1"

#define MIN_TO_SEC(min) (min * 60)

// using DHUB_STATUS_CHECK_COUNT to check the dhub status for 3 times
#define DHUB_STATUS_CHECK_COUNT             3
// using DHUB_STATUS_CHECK_INTERVAL to postpone the shutdown for 5 sec to check the dhub status
#define DELAY_SHUT_FOR_DHUB_STATUS_CHECK    60 // seconds
#define DELAY_SHUT_FOR_UPLOADER_ACTIVITY    300 // seconds

static const string default_dhub_status_check_enabled = "false";

static const string table_formatter = "CREATE TABLE POWERSTATES(" \
                        "INDEXID INTEGER PRIMARY KEY  AUTOINCREMENT," \
                        "BOOTTIME       INT     NOT NULL," \
                        "PIDNUM         INT     NOT NULL," \
                        "EVENTTIME      INT     NOT NULL," \
                        "EVENT          TEXT     NOT NULL," \
                        "ACTION         TEXT     DEFAULT NA);" ;

static const string insert_str =
        "INSERT INTO POWERSTATES (BOOTTIME,PIDNUM,EVENTTIME,EVENT,ACTION) VALUES ";

nd_enum(power_dbstate_enum_t,
    DBSTATE_CRANKHIGH = 0,
    DBSTATE_CRANKLOW,
    DBSTATE_CRANKERROR,
    DBSTATE_SHUTDOWN_BADVOLTAGE,
    DBSTATE_SHUTDOWN_CYCLIC,
    DBSTATE_SHUTDOWN_OVERTEMP,
    DBSTATE_SHUTDOWN_UPLOADER_UPLOAD_DONE,
    DBSTATE_SHUTDOWN_CRANKOFF,
    DBSTATE_SHUTDOWN_UNKNOWN,               // For smart health payload(for analytics team) we are referring DBSTATE_SHUTDOWN_UNKNOWN as DBSTATE_SHUTDOWN_POWER_DOWN
    DBSTATE_SHUTDOWN_LOWPOWER,
    DBSTATE_SHUTDOWN_SDCARD,
    DBSTATE_SHUTDOWN_AWSIOT,
    DBSTATE_SHUTDOWN_SVC,
    DBSTATE_SHUTDOWN_CAM_CRASH,
    DBSTATE_SHUTDOWN_INSTALLER_APP,
    DBSTATE_SHUTDOWN_INSTALLER_APP_CRASH,
    DBSTATE_SHUTDOWN_MSP_FAIL_REBOOT,
    DBSTATE_SHUTDOWN_MISC_LOWPOWER,
    DBSTATE_RTC_WAKEUP_TIME_DHUB,
    DBSTATE_RTC_WAKEUP_TIME_DEVICE,
    DBSTATE_IGNITION_GPIO_LOW,
    DBSTATE_IGNITION_GPIO_HIGH,
    DBSTATE_LOWPOWER_WAKEUP,
    DBSTATE_DHUB_LAST_CONNECTED_TIME,
    DBSTATE_SHUTDOWN_ANALYTICS,
    DBSTATE_MISC_LOWPOWER_WAKEUP,
    DBSTATE_ERROR);

enum power_monitor_shutdown_delay_reason
{
    DELAY_FOR_UPLOADER_ACTIVITY = 0
};

enum power_monitor_shutdown_reason
{
    //// valid shutdown requests 1st priority
    SHUTDOWN_FOR_BAD_VOLTAGE = 0x00,
    SHUTDOWN_FOR_CAM_CRASH,// 1 //
    SHUTDOWN_FOR_SDCARD_RO_RECOVERY,// 2 //
    SHUTDOWN_FOR_SVC_KEEPALIVE_FAILURE,// 3 //
    SHUTDOWN_FOR_AWSIOT,// 4 //
    SHUTDOWN_FOR_ANALYTICS,// 5 //
    SHUTDOWN_FOR_UPLOADER_DATA_UPLOAD_DONE,// 6 //
    SHUTDOWN_FOR_DHUB_STATUS_CHECK_DONE,// 7 //
    SHUTDOWN_FOR_SVC_REBOOT,// 8 //
    SHUTDOWN_FOR_MSP_FAIL_REBOOT, // 9 //
    SHUTDOWN_FOR_INSTALLER_APP_CRASH,// 10 //
    SHUTDOWN_FOR_INSTALLER_APP,// 11 //

    // 2nd priority
    SHUTDOWN_FOR_IGNITION_OFF,// 12 //
    SHUTDOWN_FOR_CYCLIC_REBOOT,// 13 //
    SHUTDOWN_FOR_OVERTEMPERATURE,// 14 //
    SHUTDOWN_FOR_UNKNOWN_BOOTUP,// 15 //
    SHUTDOWN_FOR_LOWPOWER_WAKEUP,// 16 //
    SHUTDOWN_FOR_MISC_LOWPOWER_WAKEUP,// 17 //

    //// valid postpone requests
    SHUTDOWN_CANCELLED, // 18 //

    // error
    SHUTDOWN_FOR_ERROR, // 19 //

    //When adding new shutdown reasons please make sure the enum values dont overlap with postpone reasons values

    POSTPONE_FOR_IGNITION_ON = 0x20,
    POSTPONE_FOR_NORMAL_RUN, // 0x21 //
    POSTPONE_FOR_UPLOADER_ACTIVITY, // 0x22 //
    POSTPONE_FOR_DHUB_STATUS_CHECK, // 0x23 //
    POSTPONE_FOR_MISC_LOWPOWER_WAKEUP, // 0x24 //
    POSTPONE_FOR_UNKNOWN_REASON, // 0x25 //
    POSTPONE_FOR_BAD_BATTERY_CLEAR, // 0x26 //

    POSTPONE_FOR_ERROR // 0x27 //

};

struct power_shutdown_state_t
{
    int64_t monotonic_time; //System time in milli seconds
    int time_gap; //time gap we want to take in seconds
    //bool restart;
    power_monitor_shutdown_reason reason; // reason for this state
};

typedef struct {
    int  crank_shutdown_duration;
    int  lowpower_wakeup_duration;
    int  lowpower_wakeup_cycle_duration;
    int  lowpower_wakeup_long_cycle_duration;
} power_monitor_lpw_data_t;

typedef sqlite3 db_handle_t;

class power_monitor_ctx
{
public:
    string          power_monitor_q_name = "q_power_monitor"; 
    nd_msgq_t       *powmon_msg_q;

    static power_monitor_ctx* get_power_monitor( string name );
    string name;

    Config_parser   *device_config;
    Config_parser   *bagheera_config;
    Config_parser   *cloud_config;
    Config_parser   *nddevice;

    power_crank_levels_t    crank_level();
    bool            crank_low_registered;
    int64_t         crank_low_registered_time;
    bool            crank_high_registered;
    int64_t         crank_high_registered_time;
    std::atomic<int> crank_low_count{0};
    std::atomic<int> crank_high_count{0};
    std::atomic<uint> prev_crank_low_count{0};
    std::atomic<uint> prev_crank_high_count{0};
    std::atomic<uint> crank_event_count{0};

    power_monitor_shutdown_delay_reason   shutdown_delay_reason;
    power_monitor_shutdown_reason reason;
    int64_t         shutdown_time;

    bool            lowpowermode;
    bool            freq_low_power_wakeup;

    int max_uptime_secs;
    int crank_shutdown_duration;
    int dhub_crank_shutdown_duration;

    pthread_mutex_t powermon_mutex;

    power_crank_levels_t present_crank_level;
    atomic<int64_t> present_crank_change_time;

    float           current_voltage = 0.0;
    float           min_voltage_limit;
    float           max_voltage_limit;
    float           min_voltage_limit_24V;
    float           max_voltage_limit_24V;
    int             abnormal_voltage_wait_duration;
    bool            is_battery_cfg_init;
    int32_t         min_speed_for_battery_cfg_init;

    int             lowpower_wakeup_duration;               // in secs
    int             lowpower_wakeup_cycle_duration;         // in secs
    int             safety_time_to_sync_driveri_dhub;       // in secs
    int             safety_wakeup;
    bool            extended_post_ignition_off_and_lpw_timer;
    bool            delay_shutdown_for_dhub_status_check = false;
    bool            delay_shutdown_for_misc_lowpower_wakeup = false;
    bool            dhub_status_check_enabled = false;
    int             max_postpone_shutdown_time_uploader_activity;
    int             misc_wakeup_duration;
    int             possible_lpw_count = 1; // To store the possible low power wakeup count to sync with dhub
    bool            dhub_wakeup_time_event_found = false;
    int64_t         last_dhub_wakeup_time = 0;
    bool            dhub_connected_status = false;
#ifdef IGNITION_AUDIO_ALERT
    /* FEATURE:: AUDIO ALERT MESSAGE ON IGNITION CRANK HIGH(ON) TO INFORM THE DRIVER 
		 THAT DEVICE IS RECORDING BOTH VIDEO AND AUDIO.......................*/
    bool	     ignitionOnAudioAlert = false;
    string           ignitionOnAudioAlertFile = "";
    int              ignitionOnAudioAlertInterval = DEFAULT_IGNITION_ALERT_INTERVAL; // in seconds. In case of toggling or frequent ignition ON events.
    int64_t          lastIgnitionOnAudioAlertTime = DEFAULT_IGNITION_ALERT_TIME;
    bool	     ignitionOnIdleAudioAlert = false;
    string           ignitionOnIdleAudioAlertFile = "";
    int              ignitionOnIdleAudioAlertThresholdDuration = DEFAULT_IGNITION_IDLE_ALERT_DURATION;	
    int              ignitionOnIdleAudioAlertThresholdFrequency = DEFAULT_IGNITION_IDLE_ALERT_FREQUENCY; // number of idle audio alert events when idling 
    int     	     ignitionOnIdleAudioAlertDuration = 0;
    int     	     ignitionOnIdleAudioAlertFrequency = 0;
    void 	     *zmq_context_audio = NULL;
    void	     *zmq_publisher_audio = NULL;
    /*-------------------...........................................................*/
#endif

    bool driveri_app_login = false;
    volatile power_shutdown_state_t power_state;
    pthread_mutex_t power_state_mutex;

    volatile power_crank_levels_t crank_change_keepalive;
    volatile int supercap_status = 1;
    volatile res_gps_update_msg_t gps_pos;
    bool disableLowPowerwakeUp = false ;

    int bad_battery_voltage_read_count = 0 ;
    int bad_battery_voltage_high_read_count = 0 ;
    int safety_wakeup_time_for_bad_voltage_shutdown = 0;

    PowerStateController ps_obj; // Holds configurable frequencies for different states like supercap, low power, normal and boost modes

    //// DB functions/variables
    db_handle_t* db_handle = NULL;
    bool open_db(string bd_file, db_handle_t** db_handle);
    pthread_mutex_t db_handle_mutex;
    bool create_table_db(db_handle_t* db_handle);
    bool exec_cmd_db(db_handle_t* db_handle, const string command, 
                int (*callback)(void*,int,char**,char**), void* cb_data);
    int64_t boot_time;
    int64_t service_start_time_mono;
    int pid_num;
    int lowpower_wakeups;
    int max_lowpower_wakeups;
    int lowpower_wakeup_long_cycle_threshold;
    int lowpower_wakeup_long_cycle_duration;
    int fsck_lowpower_wakeup;
    string previous_shutdown_reason;
    int allow_sdcard_reboot_freq;
    bool RO_registered;
    bool uploader_data_upload_pending;
    bool shutdown_delayed = false;
    bool suspend_mode;
    bool misc_lowpower_wakeup = false;
    int misc_wakeup_count = 0;
    bool misc_lowpower_reboot = false;
    int power_on_off_reason = 0;
    bool ext_cam_lpw_enabled = false;
    int64_t last_rtc_alarm_time = 0;
    bool extend_wakeup_duration_for_misc = false;
    int64_t last_wakeup_time = 0;
    int64_t last_connected_time_dhub = 0;

    // Below variables are related to misc wakeup in low power mode
    bool non_lpm_crank_low_wakeup = false;
    int non_lpm_crank_low_wakeup_duration = 6; // minutes
    bool wake_on_motion_imu = false;
    bool wake_on_motion_aon = false;
    bool wake_on_ign = false;
    bool wake_on_misc = false;
    PowerOnTriggerT wakeup_reason;
    int dhub_ign_status = -1; // DHUB ignition status
    bool keepalive_send_voltage_supercount = false;

    // Ignition Source Status Check Time Before Shutdown
    int ign_check_duration = DEF_IGN_CHECK_DURATION; // seconds

    bool bad_battery_detected = false;

private:
    power_monitor_ctx( string name );
    ~power_monitor_ctx();
};

struct db_state_info_t{
    int64_t index;
    int64_t boot_time;
    int pid_num;
    int64_t event_time;
    string event;
    string action;
};

enum power_monitor_msg_type_t {
    POWERMON_CRANK_CHANGE = 1,
    POWERMON_MAXTIMEOUT,
    POWERMON_BAD_BATTERY_VOLTAGE,
    POWERMON_NORMAL_RUN,
    POWERMON_INTRPT_THREAD_CRASH,
    POWERMON_DIRECTPOLL_CRANK_CHANGE,
    POWERMON_SDCARD_RO_RECOVER,
    POWERMON_AVOID_TIMOUT,
    POWERMON_DHUB_STATUS_CHECK,
    UPDATE_DHUB_WAKEUP_TIME_IN_DB,
    POWER_MON_IGNITION_GPIO_STATUS,
    POWERMON_BAD_BATTERY_CLEAR,

    POWERMON_ERROR
};

enum power_monitor_wakeup_reason_t {
     CRANK_HIGH_WAKEUP,
     WOM_WAKEUP,
     UNKNOWN_WAKEUP,
};

struct power_monitor_generic_msg_t {
    power_monitor_msg_type_t type;
    int len;
};

struct power_monitor_crank_change_t {
    power_monitor_msg_type_t type;
    int len;
    power_crank_levels_t crank_level;
    int64_t crank_change_time;
};



struct power_monitor_maxtimeout_t {
    power_monitor_msg_type_t type;
    int len;
    int time_elapsed;
};

struct power_monitor_battery_voltage_t {
    power_monitor_msg_type_t type;
    int len;
    int battery_volt;
};

struct power_monitor_norman_run_t {
    power_monitor_msg_type_t type;
    int len;
    int64_t event_time;
};

struct power_monitor_directpolling_crank_t{
    power_monitor_msg_type_t type;
    int len;
    int64_t event_time;
    power_crank_levels_t crank_level;
};

struct power_monitor_dhub_status_check_t{
    power_monitor_msg_type_t type;
    int len;
    int64_t start_time;
};

struct gps_health_data_t{  
    bool valid;  
    float speed;  
    double lat;  
    double lon;  
    double accuracy;
};

bool check_uptime(int &time_diff, int fold_val);
void delay_shutdown(int delay_shutdown_for_secs, power_monitor_shutdown_delay_reason reason);
bool postpone_shutdown(int postpone_shutdown_for_secs, 
                    power_monitor_shutdown_reason reason);
bool initiate_shutdown(int shutdown_after_secs, 
                    power_monitor_shutdown_reason reason);


bool db_limit_rows(db_handle_t *db_handle);
bool add_event_db(power_dbstate_enum_t event_code, string action="NA");
//bool add_event_db(db_handle_t *db_handle, int64_t event_time, string event, string action="");
bool add_event_db(db_handle_t *db_handle, int64_t event_time, int64_t boot_time, int pid_num,
                     string event, string action="");
int count_misc_wakeups(db_handle_t *db_handle);
int count_lowpower_wakeups(db_handle_t *db_handle);
string previous_shutdown_reason(db_handle_t *db_handle);
bool get_db_node_event(string event, db_state_info_t* data_node, db_handle_t *db_handle);
bool get_db_node_action(string action, db_state_info_t* data_node, db_handle_t *db_handle);
int support_extended_wakeups();
void restart_bluetooth_activities_for_driver_login();
bool verify_misc_lowpower_wakeup(int &lowpower_wakeup_count);
bool check_event_in_db(stringstream &val_stream, db_state_info_t &event_node);
int get_lpw_cycle_duration(bool isValidateDhubRtc = false);
bool add_or_update_event_db_event_time(power_dbstate_enum_t event_code, int64_t event_time);
bool identify_misc_wakeup_reason(lpw_state_t lpw_stat);

// Added for audio playback In case of audio play message send failed to bagheera Service
#ifdef IGNITION_AUDIO_ALERT
void start_stream_playback (const char *filename);
#endif

#endif
