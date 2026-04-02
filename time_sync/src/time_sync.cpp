/* Copyright (C) 2018 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Karthik Dumpala <karthik.dumpala@netradyne.com>, August 2018
 */

#include "time_sync.h"
#include <system_utils.h>
#include <time.h>
#include <jansson/jansson.h>
#include <ndmb/nd_mbclient.h>
#define TAG "TIME_SYNC"
#define ROUTE_LOGS
#define ATI_COMMAND_RETRY_COUNT 4
#define ATI_COMMAND_RETRY_INTERVAL 5

NDService *nd_service_obj = NULL; //nd service object, to detect critical Errors which will be send to Health stats and cloud
pthread_mutex_t time_sync_update_mutex = PTHREAD_MUTEX_INITIALIZER;
bool system_time_updated = false;
int64_t system_time_updated_from = 0;
int64_t system_time_updated_to = 0;

pthread_mutex_t gps_update_mutex = PTHREAD_MUTEX_INITIALIZER;
bool gps_valid = false;
double gps_lat = 0;
double gps_long = 0;
int64_t gps_time = 0;
int low_power_wakeup_cnt = -1;

//Log directory
static const string log_dir = "/home/ubuntu/.nddevice/log/time_sync";
static const string BAGHEERACONFIG_INI = "/home/ubuntu/.nddevice/latest/bagheera_config.ini";
static const string BAGH_CONF_TIME_SYNC_SECTION = "time_sync";
static const string BAGH_CONF_TIME_SYNC_FEATURE_CONTROL = "enabled";
static const string BAGH_CONF_TIME_SYNC_GPS_FEATURE_CONTROL = "gps_time_sync_enable";
static const string BAGH_CONF_TIME_SYNC_NETWORK_FEATURE_CONTROL = "network_time_sync_enable";
static const string DATA_BEARER_SUBSTR = "CGACT: 1,1";
static bool time_sync_enabled = true;
static bool gps_time_sync_enabled = true;
static bool network_time_sync_enabled = true;
static int valid_gps_count = 0;
// this is not mutex protected, should we add ?
static bool system_time_set = false;
static const int PROCESS_EXIT_ERROR = -1;
static const int PROCESS_EXIT_SUCCESS = 0;
static const int TENTH_VALUE = 10;
static const int ONE_MINUTE = 60;
static const int SECS_TO_MILLISECS = 1000;
static const int AT_COMMAND_RUN_TIMEOUT = 30;
static const int CURRENT_YEAR = 2019;

//Recent GPS and NETWORK timestamp stored in these variables
int64_t boot_cycle_gps_ts=0;
int64_t boot_cycle_network_ts=0;
int64_t system_mono_ts_gps_lock=0;
int64_t system_mono_ts_network_latch=0;
bool time_sync_set = false;
bool network_time_sync_done = false;
bool gps_time_sync_done = false;

std::mutex set_driveri_system_time_mutex;

using namespace std;


enum regex_match_idx_t {
    REGEX_FULL,
    REGEX_YEAR_IDX,
    REGEX_MONTH_IDX,
    REGEX_DATE_IDX,
    REGEX_HOUR_IDX,
    REGEX_MIN_IDX,
    REGEX_SEC_IDX,
    REGEX_SIZE
};

//Server Queue pointer
static nd_msgq_t *server_q;
//Server Queue Name
static const string Q_NAME = "TIME_SYNC";
//Ndcentral Queue name
static const string Q_NDCENTRAL = "q_nd_central";
static const string Q_POWERMON  = "q_power_monitor";

static string get_msgq_name() {
    return Q_NAME;
}

static string get_ndcentral_q() {
    return Q_NDCENTRAL;
}

static string get_powermon_q() {
    return Q_POWERMON;
}

static bool string_to_integer (string s, int &num) {
    stringstream ss;

    ss.str("");
    ss.clear();

    ss << s;
    ss >> num;
    if (!ss.fail ())
    {
        return true;
    }
    else
    {
        return false;
    }
}

void print_timestamp(long long ts, time_source_t source) {   
    time_t ts_seconds = (time_t)(ts / 1000);  
    struct tm *timeinfo = localtime(&ts_seconds);  
    char time_str[100];  
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", timeinfo);  
    LOG_I(TAG, "Recent %s timestamp: %s\n ", source == eGPS ? "GPS" : "NETWORK", time_str);
}
int get_year_from_network()
{
    int network_year = 0;
    string curl_response;
    string curl_cmd = "curl -sI www.google.com | grep \"Date:\" | awk '{print $5}'";
    if(!system_execute_with_resp("GET_YEAR_BY_CURL", curl_cmd, curl_response)) {
        LOG_E(TAG,"Failed to execute curl get date command : %s",  curl_cmd.c_str());
    }
    LOG_I(TAG, "Curl Get Year response : %s", curl_response.c_str());
    try {  
        network_year = atoi(curl_response.c_str());  
        } 
        catch (const std::invalid_argument& e) 
        {    
        LOG_E(TAG, "Invalid argument : %s", e.what());     
        network_year = 0;  
        }  
        return network_year;
}
bool send_time_sync_health_payload(int64_t new_time_source_ts)
{
    //Time Difference between GPS and Network timestamp
    int64_t abs_time_diff = 0;
    int64_t current_system_time = get_system_time();
    abs_time_diff = abs(new_time_source_ts - current_system_time);

    LOG_I(TAG, "Time difference between new time source and current system time: %ld ms", abs_time_diff);
    string abs_time_str = to_string(abs_time_diff);
    json_t *root = json_object();
    json_t *Lte_Network_Time_Sync = json_object();
    json_object_set_new(Lte_Network_Time_Sync, "time_sync", json_string(abs_time_str.c_str()));
    json_object_set_new( root, "health_info:lte_network_time_sync" , Lte_Network_Time_Sync);
    json_object_set_new( root, "isArray" , json_string("true"));
    char *req_params = json_dumps(root, JSON_COMPACT);
    int length = strlen(req_params);
    nd_service_obj->send_msg_healthstats(req_params, length);
    LOG_I(TAG, "Health payload sent  \n");
    free(req_params);
    if(root!=NULL)
    {
       json_decref(root);
    }
    return true;
}
static void set_driveri_system_time(int64_t ts, int64_t prev_time ,time_source_t time_source)
{
    std::unique_lock<std::mutex> lock(set_driveri_system_time_mutex);

    if(time_sync_set == true) {
        return;
    }

    if( network_time_sync_done && (eNETWORK == time_source) ) 
    {
        LOG_I(TAG, "Network time already synced, ignoring this update");
        return;
    }
    LOG_I(TAG,"Timestamp ts : %lld, prev_time : %lld, time_source : %d", ts, prev_time, time_source);
     if (time_source == eGPS) {  
        boot_cycle_gps_ts = ts;  
        system_mono_ts_gps_lock = get_system_monotonic_time();  
        print_timestamp(ts,time_source);  
    } else {  
        boot_cycle_network_ts = ts;  
        system_mono_ts_network_latch = get_system_monotonic_time();  
      print_timestamp(ts,time_source);   
    }  
    LOG_I(TAG, "Recent GPS timestamp: %lld, Recent Network timestamp: %lld", boot_cycle_gps_ts, boot_cycle_network_ts);
    if(boot_cycle_gps_ts!=0 && boot_cycle_network_ts!=0)
    { 
        time_sync_set = send_time_sync_health_payload(ts);
    } 
    if(system_time_set == true) {
        return;
    }
    int64_t curr_time = get_system_time();
    int64_t proc_delay = curr_time - prev_time;
    LOG_I(TAG, "system time %lld, prev system time: %lld, curr system time: %lld, proc_delay: %lld",
                                ts, prev_time, curr_time, proc_delay);
    
    // Update the systime to account for proc delay
    if(proc_delay > 0) {
        ts = ts + proc_delay;
    }

    // Check if date is older than 2018 (current year) before setting
    time_t timestamp = (time_t)(ts/1000);
    struct tm * timeinfo = gmtime(&timestamp);
    if(timeinfo == NULL) {
        LOG_E(TAG, "Failed to get GMT time");
        return;
    }

    if((timeinfo->tm_year + 1900) < CURRENT_YEAR) {
        LOG_E(TAG, "Got request for time update with old year. ignoring");
        return;
    }

    LOG_I(TAG, "Setting system time %lld", ts);
    bool ret = set_system_time(ts);
    if(ret == false) {
        LOG_E(TAG, "Failed to set system time");
        return;
    }
    /* If GPS time is set first, ignore LTE time. 
     * If LTE time is set first, set GPS time again when GPS time becomes available.
     */
    if( (eGPS == time_source) && (false == gps_time_sync_done) ) 
    {
        gps_time_sync_done = true;
        system_time_set = true;
    } 
    else if( (eNETWORK == time_source) && (false == network_time_sync_done) ) 
    {
        network_time_sync_done = true;
    }

    LOG_I(TAG, "System time set by %s", time_source == eGPS ? "GPS" : "NETWORK");
    int64_t sys_monotonic_time = get_system_monotonic_time();
    nd_service_obj->send_err_msg(time_source == eGPS ? SM_I_TIMESYNC_DONE_BY_GPS : SM_I_TIMESYNC_DONE_BY_LTE, sys_monotonic_time, "Time sync Done");


    ret = set_hw_time(ts); 
    if(ret == false) {
        LOG_E(TAG, "Failed to set HW time");
    }

    pthread_mutex_lock ( &time_sync_update_mutex );
    system_time_updated_from = prev_time;
    system_time_updated_to = ts;
    system_time_updated = true;
    pthread_mutex_unlock ( &time_sync_update_mutex );
}

static void send_int_msg_lte_conn() {
    lte_conn_check_update_msg_t lte_check_msg;

    //Send internal message
    if( false == send_msg( (generic_msg_t *)&lte_check_msg, (msg_type_t)LTE_CONNECTIVITY_UPDATE,
                   sizeof(lte_conn_check_update_msg_t), get_msgq_name(), get_msgq_name(), 0) ) {
        LOG_E (TAG,"sending LTE_CONNECTIVITY_UPDATE internal msg failed");
    }
}
void run_lte_sample_app_command(string cmd, string &output)
{
    int retry = 0;
    while(retry < ATI_COMMAND_RETRY_COUNT)
    {
        output = "";
        if(!system_execute_with_resp("RUN LTE SAMPLE APP",cmd, output)) {
            LOG_E(TAG,"Failed to execute LTE sample app command : %s",  cmd.c_str());
        }
        if((output == "") || (output.find("Err,Sierra modem not detected") != string::npos) || (output.find("ERROR") != string::npos))
        {
            LOG_I(TAG, "Modem Output Doesn't Contain required Info, retrying %d",retry+1);
            retry++;
            sleep(ATI_COMMAND_RETRY_INTERVAL);
        }
        else
        {
            LOG_I(TAG, "Modem Output : %s", output.c_str());
            break;
        }
    }
}

static void send_req_msg_pwr_mon() {
    time_sync_low_power_wakeup_cnt_msg_t req_lpm_wake_count;
    //Send message to power monitor
    if( false == send_msg( (generic_msg_t *)&req_lpm_wake_count, (msg_type_t)LOW_POWER_WAKEUP_CNT_UPDATE,
                   sizeof(time_sync_low_power_wakeup_cnt_msg_t), get_msgq_name(), get_powermon_q(), 0) ) {
        LOG_E (TAG,"sending LOW_POWER_WAKEUP_CNT_UPDATE msg failed");
    }
}

static void run_at_command_check_lte_conn()
{
    char buffer [256];
    string cmd = "lte_gps_sample_app 'at+cgact?'";

    /* at+cgact? response will be as shown below
    at+cgact?
    +CGACT: 1,1
    OK
    */

    LOG_I(TAG, "AT command to get network time process launched");
    string output = "";
    run_lte_sample_app_command(cmd, output);
    // Process the command output
    if(output == "") {
        _exit(PROCESS_EXIT_ERROR);
    }
    // Checking if data bearer established 
    if(output.find(DATA_BEARER_SUBSTR) == string::npos) {
        LOG_I(TAG, "LTE connectivity do not exist");
        _exit(PROCESS_EXIT_ERROR);
    }

    send_int_msg_lte_conn();

    _exit(PROCESS_EXIT_SUCCESS);
}

static void check_lte_connectivity_exist() {
    pid_t pid = fork();

    if (pid < 0) {
        LOG_E(TAG, "Failed to fork process for conn check at command run");
        return;
    }

    if (pid == 0){
        run_at_command_check_lte_conn();
    }

    LOG_I(TAG, "AT command run child process for lte conn check launched with pid = %d", pid);
    task_status_t tc_process_status = nd_set_timeout_for_task(pid, AT_COMMAND_RUN_TIMEOUT);

    switch (tc_process_status){
        case TASK_STATUS_SUCCESS:
            LOG_I(TAG, "AT command run child process for lte conn check %d successfully completed", pid);
            break;
        case TASK_STATUS_FAILED:
            LOG_E(TAG, "AT command run child process %d for lte conn check failed", pid);
            break;
        case TASK_STATUS_KILLED:
            LOG_E(TAG, "AT command run child process %d for lte_conn_check killed", pid);
            break;
        default:
            LOG_E(TAG, "AT command run child process %d for lte_conn_check unexpected return", pid);
            break;
    }
}

static void send_int_msg_network_time(int64_t ts, int64_t curr_time) {
    network_time_update_msg_t network_time_msg;
    network_time_msg.time = ts;
    network_time_msg.curr_time = curr_time;

    //Send internal message
    if( false == send_msg( (generic_msg_t *)&network_time_msg, (msg_type_t)NETWORK_TIME_UPDATE,
                   sizeof(network_time_update_msg_t), get_msgq_name(), get_msgq_name(), 0) ) {
        LOG_E (TAG,"sending NETWORK_TIME_UPDATE internal msg failed");
    }
}
static void run_at_command_process()
{
    int64_t tc_time_start=0, tc_time_end=0, ts;
    char buffer [256];
    string cmd = "lte_gps_sample_app 'at+cclk?'";
    /*output of "at+cclk?"
    +cclk: "22/02/21,06:47:35+22"
    */
    int year_from_network = 0;
    year_from_network = get_year_from_network();
    struct tm timeinfo;
    LOG_I(TAG, "AT command to get network time process launched");
    string output = "";   
    // Process the command output  
    int year    = 0;
    int month   = 0;
    int date    = 0;
    int hours   = 0;
    int minutes = 0;
    int seconds = 0;

    size_t pos = 0;
    tc_time_start = get_system_monotonic_time();
    run_lte_sample_app_command(cmd, output);
    uint64_t curr_time = get_system_time();
    LOG_I(TAG, "Time taken to run AT command %lld", (curr_time - tc_time_start));
    if(output == "") {
        _exit(PROCESS_EXIT_ERROR);
    }
    if(output.find("+cclk:") != string::npos)
    {
        pos = output.find("+cclk:");
        string date_time = output.substr (pos);
        sscanf(date_time.c_str(), "+cclk: \"%d/%d/%d,%d:%d:%d+", &year, &month, &date, &hours, &minutes, &seconds);
    }
    else
    {
        LOG_E(TAG, " Modem Out Put Doesn't Contain Time Info");
        _exit(PROCESS_EXIT_ERROR);
    }
    timeinfo.tm_year = (2000 + year - 1900);
    LOG_I(TAG,"Curl Year : %d,  cclk Year : %d ",year_from_network,year+2000);
    if((2000+year)!= year_from_network)
    {
        LOG_E(TAG,"Mismatch in curl year :%d  and cclk year:%d ",year_from_network,timeinfo.tm_year);
         _exit(PROCESS_EXIT_ERROR);
    }
    timeinfo.tm_mon = month - 1;
    timeinfo.tm_mday = date;
    timeinfo.tm_hour = hours;
    timeinfo.tm_min = minutes;
    timeinfo.tm_sec = seconds;
    timeinfo.tm_isdst = -1;
    LOG_I(TAG, "Year: %d, Month: %d, Date: %d, Hour: %d, Minute: %d, Sec: %d",
               year, month, date, hours, minutes, seconds);
    
    time_t time_out = mktime( &timeinfo );
    if(time_out == -1)
    {
        LOG_E(TAG, "Failed to make time");
        _exit(PROCESS_EXIT_ERROR);
    }
    ts = ((int64_t)time_out)*SECS_TO_MILLISECS;
    LOG_I(TAG, "EPOCH time corresponding to the obtained network time: %lld", ts);
    tc_time_end = get_system_monotonic_time();
    LOG_I(TAG, "Time taken to run at command = %lld ms", (tc_time_end - tc_time_start));

    send_int_msg_network_time(ts, curr_time);
    
    _exit(PROCESS_EXIT_SUCCESS);
}

static void get_network_time_process() {
    pid_t pid = fork();
    if (pid < 0) {
        LOG_E(TAG, "Failed to fork process for at command run to get network time");
        return;
    }
    if (pid == 0){
        run_at_command_process();
    }
    LOG_I(TAG, "AT command run child process  to get network time launched with pid = %d", pid);
    task_status_t tc_process_status = nd_set_timeout_for_task(pid, AT_COMMAND_RUN_TIMEOUT);
    switch (tc_process_status){
        case TASK_STATUS_SUCCESS:
            LOG_I(TAG, "AT command run child process %d to get network time successfully completed", pid);
            break;
        case TASK_STATUS_FAILED:
            LOG_E(TAG, "AT command run child process %d to get network time failed", pid);
            break;
        case TASK_STATUS_KILLED:
            LOG_E(TAG, "AT command run child process %d to get network time killed", pid);
            break;
        default:
            LOG_E(TAG, "AT command run child process %d to get network time unexpected return", pid);
            break;
    }
}

static void get_network_time() {

    get_network_time_process();
}

static void read_time_sync_config() {
    
    bool get_override_val = true;
    bool is_val_overridden = false;
    
    Config_parser temp (BAGHEERACONFIG_INI);
    if (!temp.getParseStatus()) {
        LOG_E(TAG, "Failed to parse %s", BAGHEERACONFIG_INI);
        return;
    }
    if(temp.isPresent(BAGH_CONF_TIME_SYNC_SECTION, BAGH_CONF_TIME_SYNC_FEATURE_CONTROL) ) {
        if( "false" == temp.getConfig(BAGH_CONF_TIME_SYNC_SECTION, 
                BAGH_CONF_TIME_SYNC_FEATURE_CONTROL,"true", get_override_val, is_val_overridden) ) {
            LOG_I (TAG, "Time sync feature is disabled");
            time_sync_enabled = false;
        }
        else {
            LOG_I (TAG,"Time sync feature is enabled in config file");
        }
    }
    else {
        LOG_E (TAG,"%s:%s not present in config file", BAGH_CONF_TIME_SYNC_SECTION.c_str(), 
                                                       BAGH_CONF_TIME_SYNC_FEATURE_CONTROL.c_str());
    }
    if(temp.isPresent(BAGH_CONF_TIME_SYNC_SECTION, BAGH_CONF_TIME_SYNC_GPS_FEATURE_CONTROL) ) {
        if( "false" == temp.getConfig(BAGH_CONF_TIME_SYNC_SECTION, 
                BAGH_CONF_TIME_SYNC_GPS_FEATURE_CONTROL,"true", get_override_val, is_val_overridden) ) {
            LOG_I (TAG, "Time sync from GPS is disabled");
            gps_time_sync_enabled = false;
        }
        else {
            LOG_I (TAG,"Time sync from GPS is enabled in config file");
        }
    }
    else {
        LOG_E (TAG,"%s:%s not present in config file", BAGH_CONF_TIME_SYNC_SECTION.c_str(), 
                                                       BAGH_CONF_TIME_SYNC_GPS_FEATURE_CONTROL.c_str());
    }
    if(temp.isPresent(BAGH_CONF_TIME_SYNC_SECTION, BAGH_CONF_TIME_SYNC_NETWORK_FEATURE_CONTROL) ) {
        if( "false" == temp.getConfig(BAGH_CONF_TIME_SYNC_SECTION, 
                BAGH_CONF_TIME_SYNC_NETWORK_FEATURE_CONTROL,"true", get_override_val, is_val_overridden) ) {
            LOG_I (TAG, "Time sync from network is disabled");
            network_time_sync_enabled = false;
        }
        else {
            LOG_I (TAG,"Time sync from network is enabled in config file");
        }
    }
    else {
        LOG_E (TAG,"%s:%s not present in config file", BAGH_CONF_TIME_SYNC_SECTION.c_str(), 
                                                       BAGH_CONF_TIME_SYNC_NETWORK_FEATURE_CONTROL.c_str());
    }
}

static void check_and_sync_gps_time(int64_t timestamp, int64_t system_timestamp)
{
    // Set system time when we get 10th valid GPS update
    valid_gps_count++;
    if(valid_gps_count == TENTH_VALUE) {
        if(time_sync_enabled == true && gps_time_sync_enabled == true) {
            int64_t curr_time = get_system_time();
            LOG_I(TAG, "GPS timestamp: %lld, curr timestamp: %lld",
                       timestamp, curr_time);
            set_driveri_system_time(timestamp, system_timestamp, eGPS);
        }
    }
}

static bool init_msgq() {

    //Create message queue
    server_q = nd_msgq_t::get_msgq( get_msgq_name(), nd_msgq_t::ND_MSGQ_SERVER, true );

    if( server_q == NULL ) {
        LOG_E(TAG, "Cannot create message queue");
        return false;
    }

    LOG_I(TAG, "Message queue created");

    return true;
}

static void msg_loop() {
    
    nd_msgq_t::nd_msg_t *msg;
    network_time_update_msg_t *network_time_msg;
    res_gps_update_msg_t *gps_msg;
    lte_conn_check_update_msg_t *lte_check_msg;

    send_req_msg_pwr_mon();

    while(1) {

        //Block until a new message is received
        if( (msg = server_q->receive( )) == NULL )
        {
            LOG_E(TAG, "Receive message failed" );
            continue;
        }
    
        msg_type_t type = get_msg_type(msg->get_buffer());
        generic_msg_t *m = (generic_msg_t *)msg->get_buffer();
       
        if( m == NULL )
        {
            LOG_E(TAG, "Received NULL message");
            continue;
        }

        LOG_D(TAG, "%d received", m->msg_type);

        // this info will be pushed to HS, so only update gps fields here
        switch( type ) {
           
            case LOW_POWER_WAKEUP_CNT_UPDATE: {
                time_sync_low_power_wakeup_cnt_msg_t *msg_cnt= (time_sync_low_power_wakeup_cnt_msg_t *)m; 
                low_power_wakeup_cnt = msg_cnt->low_power_wakeup_cnt;
                LOG_I(TAG, "Received low_power_wakeup_cnt: %d", low_power_wakeup_cnt);
 
            }
            break;

            default: {
               LOG_D(TAG, "Unknown message %d received", m->msg_type);
            }
            break;
        }
        // Keep dropping messages once time is set
        if(time_sync_set == true) {
            delete msg;
            continue;
        }

        switch( type ) {
           
            case NETWORK_TIME_UPDATE: {
                network_time_msg = (network_time_update_msg_t *)m;
                LOG_I(TAG, "NETWORK_TIME_UPDATE received");
                set_driveri_system_time(network_time_msg->time, network_time_msg->curr_time, eNETWORK);
            }
            break;
            case LTE_CONNECTIVITY_UPDATE: {
                lte_check_msg = (lte_conn_check_update_msg_t *)m;
                LOG_I(TAG, "LTE_CONNECTIVITY_UPDATE received");
                get_network_time();
            }
            break;
            case LOW_POWER_WAKEUP_CNT_UPDATE: 
            break;
            default: {
               LOG_I(TAG, "Unknown message %d received", m->msg_type);
             }
             break;
        }
        delete msg;
    }
}

void *network_time_thread (void *arg)
{
    // Attempt network time sync every minute until it is done
    while(1) {
        sleep(ONE_MINUTE);
       if(time_sync_set == true) {
            break;
        }
        check_lte_connectivity_exist();
    }
    pthread_exit (NULL);
}

bool ndmb_gps_cb(ndmb_generic_msg_t *msg)
{
    if(msg == NULL) {
        LOG_E(TAG, "Invalid message");
        return false;
    }

    gps_msg_t gps_msg; // Declare gps_msg
    string topic = msg->topic;
    if(topic == TOPIC_GPS_DATA)
    {
        gps_msg = *reinterpret_cast<gps_msg_t *>(msg); // Assign the dereferenced pointer to gps_msg
     
        pthread_mutex_lock( &gps_update_mutex );
        if(gps_msg.valid) 
        {
            gps_valid = true;
            gps_lat = gps_msg.latitude;
            gps_long = gps_msg.longitude;
            gps_time = gps_msg.timestamp;

            check_and_sync_gps_time(gps_msg.timestamp, gps_msg.system_timestamp);
        }
        pthread_mutex_unlock( &gps_update_mutex );
    }
    else
    {
        LOG_E(TAG, "ndmb_gps_cb Invalid topic %s", topic.c_str());
        return false;
    }
    return true; // Default return for valid cases
}

// void *internet_led_thread(void *arg)
// {
//     while(1){
//         if(check_internet_exist_wwan0())
//         {
//             LOG_I(TAG,"Internet exists in wwan0");
//             nd_device_obj->nd_set_led_on(RED, LTE_LED , true);
//         }
//         else 
//         {    
//             string cmd_quectel = "lsusb | grep 2c7c";
//             string module_response;
//             if(system_execute_with_resp(TAG,cmd_quectel,module_response))
//             {
//                 if(module_response == "")
//                 {
//                     LOG_I(TAG,"Module type is not Quectel");
//                     nd_device_obj->nd_clear_led(LTE_LED);
//                 }
//                 else
//                 {
//                     LOG_I(TAG,"Default LED case");
//                     nd_device_obj->nd_blink_led(LTE_LED , DUTYCYCLE_50);
//                 }
//             }
//             else
//             {
//                 LOG_E(TAG,"Clearing LED case");
//                 nd_device_obj->nd_clear_led(LTE_LED);
//             }
//         }
//         sleep(120);
//     }
// }

int main( int argc, char **argv )
{
    nd_service_obj = NDService::get_service_obj(TAG);
    pthread_t network_time_check_th;
    pthread_t udid_th;
    printf("initilizing logger\n");
    bool status_log = nd_log_init( log_dir.c_str());
    if(status_log == false)
    {
        printf("unable to init logger :: Exiting from main");
        nd_service_obj->send_err_msg(SM_E_TIMESYNC_INIT_FAIL, NDService::UNUSED_ERR_AUX_CODE, 
                                            "unable to init logger :: Exiting from main" );
    }

#ifdef ROUTE_LOGS
    route_logs( log_dir.c_str() );
#endif

    read_time_sync_config();

    // If time sync feature is disabled in config, make system_time_set true and continue
    if(time_sync_enabled == false || 
       (gps_time_sync_enabled == false && network_time_sync_enabled == false)) {
        LOG_I(TAG, "Time sync feature is disabled in config. Exiting service");
        system_time_set = true;
    }

    //Initialize Message Queue // dont exit even failed to init messageQ
    if( false == init_msgq() )
    {
        string str_msg = "MSG queue init failed";
        LOG_E(TAG, str_msg.c_str());
        nd_service_obj->send_err_msg(SM_E_TIMESYNC_MSGQ_INIT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        return 0;
    }

    if ( (pthread_create (&udid_th, NULL, udid_thread, NULL)) != 0 )
    {
        LOG_E (TAG,"Can't create udid_th thread");
        string str_msg = "udid_th init failed";
        LOG_E(TAG, str_msg.c_str());
        nd_service_obj->send_err_msg(SM_E_TIMESYNC_UUID_THREAD_INIT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        return 0;
    }

    // Thread to check network time every minute if network time sync is enabled
    if(network_time_sync_enabled == true) {
        LOG_I(TAG, "Spawning thread to check network time every minute");
        if ( (pthread_create (&network_time_check_th, NULL, network_time_thread, NULL)) != 0 )
        {
            LOG_E (TAG,"Can't create thread, Will wait in msg loop for gps time");
        }
    }
    std::string ndmb_gps_client = "NDMB_TIMESYNC_SERVICE";
    NDMBClient msg_client_gps(ndmb_gps_client);

    if(gps_time_sync_enabled == true) 
    {
        LOG_I(TAG, "subscribed for GPS data");
        msg_client_gps.subscribe(TOPIC_GPS_DATA, ndmb_gps_cb, 300, 100);
    }
    // Get stuck in msg loop until time sync happen
    msg_loop();   
    
    // Send message to ndcentral indicating time sync done. Then ndcentral 
    // can stop sending gps updates to time sync service
    time_sync_done_msg_t time_sync_done_msg;
    
    if( false == send_msg( (generic_msg_t *)&time_sync_done_msg, (msg_type_t)TIME_SYNC_DONE,
                   sizeof(time_sync_done_msg_t), get_msgq_name(), get_ndcentral_q(), 0) ) {
        LOG_E (TAG,"sending TIME_SYNC_DONE msg to ndcentral failed");
    }
 
    LOG_I(TAG, "Time sync done. Exiting service"); 
}
