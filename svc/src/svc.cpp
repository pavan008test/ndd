/* Copyright (C) 2017 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Arun V <arun.vj@netradyne.com>, May 2017
 */

#include "log.h"
#include "nd_time.h"
#include "nd_msgq.h"
#include "nd_msg_types.h"
#include "nd_msg_utils.h"
#include <nd_task.h>
#include <svc.h>
#include <svc_internal.h>
#include <svc_common.h>
#include <config_parser.h>
#include <system_utils.h>
#include "nd_msp_utils.h"
#include <nd_prop_utils.h>
#include <jansson.h>
#include <led_utils.h>
#include <nd_factory.h>
#include <data_recording.h>
#include <future>
#include <sys/ipc.h>
#ifdef KRAIT
#ifdef __cplusplus
extern "C" {
#endif

#include <button_api.h>
#include <gpio_api.h>
#ifdef __cplusplus
}
#endif

#endif

#ifdef BAGHEERA2
#include <atomic>
#include <button_api_bagheera2.h>

#ifdef __cplusplus
extern "C" {
#endif


#include "nd_gpio.h"

#ifdef __cplusplus
}
#endif


#endif


#include <string>
#include <sstream>
#include <list>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "service_utils.h"
#include "nd_file_utils.h"

#include "nd_factory.h"
#include <future>

#define TAG "SVC"
#define BLINKING_LED_GREEN SYS_LEFT_GREEN
#define BLINKING_LED_RED SYS_LEFT_RED
#define INST_BLINKING_LED SYS_LEFT_BLUE
#define GB_TO_BYTE (1024*1024*1024)  // To convert GB to bytes
#define LOG_CLEANUP_MIN_LIMIT 2      // 2 GB
#define LOG_CLEANUP_MAX_LIMIT 4      // 4 GB
NDService *nd_service_obj = NULL; //nd service object, to detect critical Errors which will be send to Health stats and cloud
ND_DeviceFactory *nd_device_obj = NULL;  // nd device object based on deviceType


using namespace std;

#define ROUTE_LOGS
static const string ND_CENTRAL_Q = "q_nd_central";
static const string ND_ANALYTICS_Q = "q_analytics"; // Adding q_name for handling in rental flow
const int LED_BLINK_DURATION = 5;
static const string ND_BTFV_Q = "BTFV";
static const string ND_AWSIOT_Q = "AWSIOT";
static const string log_dir = "/home/ubuntu/.nddevice/log/svc";
static const string config_file = "/home/ubuntu/.nddevice/latest/bagheera_config.ini";
bool is_led_blinking = false;
static nd_msgq_t *server_q=NULL;
static system_mode_t system_mode = IDLE_MODE;
static int lcount = 0;
static int split_logs = 0;
static int msg_idx = 0;
static bool recovery_enabled = true;
static int config_recovery_poll = 15*60; //15 minutes default
static bool diskmon_enabled = true;
static int diskmon_poll = 15*60; //15 mins default
int64_t CLEANUP_TRIGGER_DATA = 1 * GB_TO_BYTE; //1 GB
int64_t CLEANUP_TRIGGER_LOG = 4 * GB_TO_BYTE; //4 GB
static const int64_t default_cleanup_trigger_max = LOG_CLEANUP_MAX_LIMIT; //4 GB
static bool svc_pacify_recvd = false;
static bool qcs_alive_enable = true;
static bool qcs_alive_shutdown_reboot_indicate = true;
static unsigned int qcs_alive_timer = 61440;   // 512*2*60 = 61440 = 0xF000, Value signifies 2mins
static const string qcs_alive_enable_default_str = "true";
static const string qcs_alive_shdn_rbt_indicate_default_str = "true";
static int const default_service_timeout = 120; // default timeout of critical servicecs in secs
static bool should_kick_watchdog = true;

static int inst_long_press_duration_ms = 10 * 1000;
static int long_press_duration_ms = 5 * 1000;
static bool record_data_data = true;
static int watchdog_timeout = WATCHDOG_TIMEOUT; // Default watchdog timeout in seconds
static int svc_watchdog_boot_timeout_sec = SVC_WATCHDOG_BOOT_TIMEOUT_SEC;
static bool wdog_init_failed = false;
static bool svc_util_init_failed = false;
static bool msgq_init_failed = false;
static const int pmic_wdog_timeout = 120;
static int pmic_wdog_kick_counter = 0; // Counter to keep track of PMIC watchdog kicks, in case watchdog timeout is greater than pmic_wdog_timeout
static int max_pmic_wdog_kick_counter = 0; // Maximum number of times PMIC watchdog can be kicked, if watchdog timeout is greater than pmic_wdog_timeout, every 5 seconds because in normal scenario watchdog getting kicked every 5 secs
constexpr int MIN_WDOG_TIMEOUT = 120; // Minimum watchdog timeout in seconds
constexpr int MAX_WDOG_TIMEOUT = 65535; // Maximum watchdog timeout in seconds (max timeout for software watchdog)
constexpr int MIN_SVC_BOOT_TIME_TIMEOUT = 120;
constexpr int MAX_SVC_BOOT_TIME_TIMEOUT = 900;
int use_printf_log = false;
//int (*button1_call_back)(void);
//int (*button2_call_back)(void);


#define max_long_press_duration 8 // max duration for long press in secs
#define min_long_press_duration 2 // min duration for long press in secs
#define inst_max_long_press_duration 12 // max duration for long press in secs
#define inst_min_long_press_duration 9 // min duration for long press in secs
#define SERVICE_RESTART_TIMEOUT_SECS 10 // Timeout for service restart (in seconds)

// To store disabled services for rental mode according to device type
std::vector<std::string> disabled_services_for_rental;

bool handle_data_record_status(data_record_metrics_t);
static bool update_service_stats( string service, unsigned int thread_status);
static void enable_keep_alive_monitoring(string service, bool enable);
static string get_awsiot_msgq_name() {
    return ND_AWSIOT_Q;
}
void led_blinking_timer(int time_in_second)
{
    LOG_I (TAG," led_blinking_timer(): time_in_second::  %d ", time_in_second);
    std::this_thread::sleep_for(std::chrono::seconds(time_in_second)); //sleep(time_in_second);
    if(is_led_blinking)
    {
        is_led_blinking = false;
        nd_device_obj->nd_clear_led( BLINKING_LED_GREEN );
        nd_device_obj->nd_clear_led( BLINKING_LED_RED );
        nd_device_obj->nd_clear_led( INST_BLINKING_LED );
        nd_device_obj->nd_set_led_on(RED, POWER_LED, true );

        LOG_I (TAG," led_blinking_timer(): led_blinking stopped... ");
    }
    else
    {
        LOG_I (TAG,"Error:  Not BLINKING ");
    }
    return ;
}
bool update_watchdog(unsigned int timeout) ;

static string get_msgq_name() {
    return QNAME_SVC;
}

static string get_nd_msgq_name() {
    return ND_CENTRAL_Q;
}
static string get_btfv_msgq_name() {
    return ND_BTFV_Q;
}

static bool init_msgq() {

    //Create message queue
    server_q = nd_msgq_t::get_msgq( get_msgq_name(), nd_msgq_t::ND_MSGQ_SERVER );

    if( server_q == NULL ) {
        LOG_E(TAG, "Cannot create message queue");
        return false;
    }

    LOG_I(TAG, "Message queue created");

    return true;
}

// Function with no return must have void return type
static void print_stats() {
    //Loop throught all services and print stats
    for( int i=0; i<services_len; i++ ) {
        LOG_I(TAG, "STATS: service: %s, mode: %d, health: %d, last_keep_alive: %lld", \
                services[i].name.c_str(), \
                services[i].curr_service_mode, \
                services[i].healthy, \
                services[i].last_keep_alive);
    }
}
bool update_data_recording_status_in_DB(data_record_status_db data_record_status )
{
    bool ret = false;
    // create json string from data_record_status_db
    json_t *jobj = json_object();
    char *jobj_str = NULL;

    do {
        json_object_set_new(jobj, "enabled", json_boolean(data_record_status.enabled));
        json_object_set_new(jobj, "enabled_ts", json_integer(data_record_status.enabled_ts));
        json_object_set_new(jobj, "disabled_ts", json_integer(data_record_status.disabled_ts));
        if(jobj == NULL) {
            LOG_E(TAG, "handle_data_recording_status: json_object_set_new failed");
            break;
        }
        jobj_str = json_dumps(jobj, JSON_COMPACT);

        // update DB with data_recording_status
        if(!set_property_DB("data_recording_status", jobj_str)) {
            LOG_E(TAG, "handle_data_recording_status: set_property_DB failed to set data_recording_status");
            break;
        }
        ret = true;
    } while(0);
    free(jobj_str);
    json_decref(jobj);
    return ret;
}

bool handle_service_for_rental(bool enabled, bool onReboot)
{
    LOG_I(TAG,"In Side handle service for rental, enabled : [%d] onReboot : [%d]", enabled, onReboot);
    bool ret = true;
    if(!onReboot)
    {
        if(!enabled)
        {
            //send a message to circular buffer to upload the existing videos to cloud
            data_record_metrics_t msg;
            send_msg((generic_msg_t *)&msg, REQ_CIRCULAR_BUFFER_NOTIFY_VIDEO_LIST_TO_CLOUD, sizeof(msg), get_msgq_name(), "q_circular_buffer", 0);
            sleep(5);
        }
    }

    for(int i=0; i < disabled_services_for_rental.size(); i++)
    {

        if(enabled)
        {
            if(!enable_service(disabled_services_for_rental[i], false))
            {
                LOG_E(TAG, "Failed to enable service %s", disabled_services_for_rental[i].c_str());
                ret = false;
            }
            for(int ind = 0;ind < services_len;ind++){ // Whenever enabling service keep alive monitoring is started if keep_alive_at_start is set
                if((services[ind].name ==  disabled_services_for_rental[i]) && (true == services[ind].keep_alive_at_start))
                    enable_keep_alive_monitoring(services[ind].q_name,true);
            }

            // remove data_record_disable file
            if(true == file_delete(data_record_disable_file)) {
                LOG_I(TAG, "data_record_disable file removed");
            }
        }
        else
        {
            if(!disable_service(disabled_services_for_rental[i], onReboot))
            {
                LOG_E(TAG, "Failed to disable service %s", disabled_services_for_rental[i].c_str());
                ret = false;
            }
            for(int ind = 0;ind < services_len;ind++){ // Whenever disabling service keep alive monitoring is stopped
                if(services[ind].name ==  disabled_services_for_rental[i])
                enable_keep_alive_monitoring(services[ind].q_name,false);
            }

            // create data_record_disable file
            if(false == file_touch(data_record_disable_file)) {
                LOG_E(TAG, "Failed to create data_record_disable file");
            }
        }
    }


    record_data_data = enabled;
    return ret;
}
bool handle_data_record_status(data_record_metrics_t req) {
    bool ret = false;
    data_record_status_db db_data;

    do {
        // get data_recording_status from DB
        bool update_db = true;

        if(!get_data_record_status_db(db_data) ) {
            LOG_E(TAG, "get_data_record_status_db failed for DB value");
        } else {
            if (db_data.enabled == req.status || req.status == -1) {
                update_db = false;
            }
        }
        if(update_db && handle_service_for_rental(req.status, false)) {

            // update data_recording_status in DB
            if(req.status == 1)
            {
                string err_msg = "Data Recording Enabled @" + to_string(req.timestamp);
                nd_service_obj->send_err_msg (SM_I_SVC_DATA_RECORD_ENABLED, -1, err_msg);
            }
            else
            {
                string err_msg = "Data Recording Disabled @" + to_string(req.timestamp);
                nd_service_obj->send_err_msg (SM_I_SVC_DATA_RECORD_DISABLED, -1, err_msg);
            }
            db_data.enabled = req.status;
            if(req.status) {
                db_data.enabled_ts = req.timestamp;
            } else {
                db_data.disabled_ts = req.timestamp;
            }

            if(!update_data_recording_status_in_DB(db_data)) {
                LOG_E(TAG, "update_data_recording_status_in_DB failed");
                break;
            }

            SVC_LOG_I(TAG,"Rebooting because of enabling/disabling rental feature");

            if(send_powermon_to_reboot(get_msgq_name(),REQ_POWERMON_SVC_TO_REBOOT)==false)
            {  LOG_I(TAG,"inside false of svc reboot");
                system_reboot();
                exit(0);
            }
        }
        ret = true;

    } while(false);

    return ret;
}

static void do_power_mon_or_system_reboot() {
    // This function is called when power monitor or system reboot is requested
    // It will send a message to power monitor to reboot the system
    // If power monitor is not available, it will directly reboot the system
    if(send_powermon_to_reboot(get_msgq_name(),REQ_POWERMON_SVC_TO_REBOOT) == false) {
        system_reboot();
        SVC_LOG_C (TAG,"System reboot didnot occur, not expected to be here");
        SVC_LOG_I(TAG, "Set gpio for POR OFF");

        nd_device_obj->gpio_por_assert();
        nd_service_obj->send_err_msg(SM_E_PM_POR_GPIO_FAIL, NDService::UNUSED_ERR_AUX_CODE,
                "gpio_por_assert failed in svc timeout; handle this" );
        exit(0);
    }
    return ;
}

// Function with no return must have void return type
static void do_house_keeping() {

    long curr_time = get_system_monotonic_time();

    //Loop through all services and update health
    for( int i=0; i<services_len; i++ ) {
        if( services[i].curr_service_mode == STOP_MODE ) {
            continue;
        }

        long diff = curr_time - services[i].last_keep_alive;

        //If keep alive has not come for a longer time than specified,
        //Do not kick the watchdog
         if( diff > services[i].keep_alive*1000 && record_data_data) {
            services[i].healthy = false;
            string str_msg = "Keep alive timeout:  " + services[i].name + " diff: " + std::to_string(diff/1000);
            // removing nd_device_obj->update_wdog( update_watchdogtimeout );
            // added as a part of two commits as below
            // 344893a8c9b7fd6edc1c04e309515ce2f2a0be99
            // 6e1632546c6322feec1d37accdd262bea5a93d57
            // issue : power monitor shutdown thread was crashed and was not
            // able to receive the reboot request, due to update_watchdog
            // timeout was not happening and evantually ended up in no reboot.
            
            // CRITICAL Services - Requires System Reboot
            // NON CRITICAL Services - Does not require system reboot
            if(services[i].type == CRITICAL){
                if(get_system_monotonic_time() > S_TO_MS(svc_watchdog_boot_timeout_sec)){
                    should_kick_watchdog = false; // setting should_kick_watchdog to false only for CRITICAL services
                    str_msg +=  " Triggering system_reboot ";
                    SVC_LOG_E(TAG, "%s" , str_msg.c_str() );
                    nd_service_obj->send_err_msg(SM_E_SVC_KEEP_ALIVE_TIMEOUT, services[i].type, str_msg ); // Sending service type CRITICAL or NON CRITICAL in aux code
                    do_power_mon_or_system_reboot(); // Send message to power monitor to reboot the system
                }
            }else if(services[i].type == NON_CRITICAL){  // Restart the service if type is NON_CRITICAL
                str_msg +=  " Restarting Service ";
                SVC_LOG_E(TAG, "%s" , str_msg.c_str() );
                nd_service_obj->send_err_msg(SM_E_SVC_KEEP_ALIVE_TIMEOUT, services[i].type, str_msg ); // Sending error for keep alive timeout with service type
                // Restarting the servicve stop and start
                service_task_args service_task = {services[i].name,sysctl_action::eSYSCTL_RESTART,false};
                task_result_t time_task_result = nd_timed_task(sysctl_task_tt,SERVICE_RESTART_TIMEOUT_SECS, (void*)&service_task, "restart service", true);
                if(TASK_SUCCESS != time_task_result){
                    LOG_E(TAG,"nd_timed_task failed for sysctl_task_tt with status : %d",(int)time_task_result);
                }
                update_service_stats(services[i].q_name,0); // Updating service stats and wait for keep alive till the next timeout
            }
        }
    }

    //Check that all services are healthy, before kicking watchdog
    if( should_kick_watchdog ) {
        //All services ok, Kick the watchdog to keep system from rebooting
        pmic_wdog_kick_counter = 0; // Reset the kick counter, as services are in healthy state
        nd_device_obj->kick_wdog();

    }
    else{
        // If should_kick_watchdog is false, it means that one of the critical service has not responded
        // If watchdog_timeout is greater than pmic_wdog_timeout, kick the pmic watchdog for the remaining time
        // Then the system will reboot after watchdog_timeout seconds
        // This should be done only for in case pmic wdog is supported
        if( (true == nd_factory_utils::is_pmic_wdog_supported())
            && (pmic_wdog_kick_counter < max_pmic_wdog_kick_counter)) {
            pmic_wdog_kick_counter++;
            nd_device_obj->kick_wdog();
            SVC_LOG_I(TAG, "Kicking watchdog as max pmic watchdog is 120s, pmic_wdog_kick_counter: %d, max_pmic_wdog_kick_counter: %d", pmic_wdog_kick_counter, max_pmic_wdog_kick_counter);
        }else{
            SVC_LOG_I(TAG, "Not kicking watchdog ");
        }
    }
    if( lcount >= PRINT_INTERVAL ){
        //Print the service stats
        print_stats();
        lcount = 0;
    }

#ifdef ROUTE_LOGS
    split_logs+=POLL_INTERVAL;
    if( split_logs >= (SPLIT_LOGS*60) ) {
        route_logs( log_dir.c_str() );
        split_logs =0;
    }
#endif

    lcount+=POLL_INTERVAL;
}

static bool update_service_stats( string service, unsigned int thread_status ) {
    //Find a matching service
    for( int i=0; i<services_len; i++ ) {
        if( services[i].q_name == service ) {
            LOG_I(TAG, "Service:  %s,  status:  %u previous_status: %u    %d", service.c_str(), thread_status, services[i].previous_status, services[i].previous_status & thread_status);
            //update stats if all the threads are running
            // a thread is running if bit is 0 in either current or previous state. example: a) 101 & 010 is 000 => all threads are running. b) 101 & 001 is 001 => all threads are not running.
            // if all five call to this api went to else part, it will reboot the device.(one possible case:  01-01-01-11-10-10 Device will reboot.
            if( 0 == (thread_status & services[i].previous_status) ){
                services[i].last_keep_alive = get_system_monotonic_time();
            }
            else {
                LOG_E(TAG, "All threads are Not running. thread_status: %u", thread_status);
            }
            services[i].previous_status = thread_status ;
            services[i].healthy = true; //Remove this if you donot wish to
                                        //give the service a second chance
            if((false == svc_pacify_recvd) || (services[i].type == NON_CRITICAL)) // If Pacified then only non critical service will be monitored
                services[i].curr_service_mode = RUN_MODE;
            SVC_LOG_I(TAG, "Keep alive received from: %s", service.c_str());
            return true;
        }
    }

    LOG_E(TAG, "Service not registered: %s", service.c_str() );
    return false;
}
// Update monitoring stats in case of svc pacify set or reset
static void update_monitoring_parameters(){
    for(int i = 0;i < services_len;i++){
        if(services[i].type == CRITICAL){
            if(svc_pacify_recvd)
                enable_keep_alive_monitoring(services[i].q_name,false); // Disable monitoring in case of svc pacify true for CRITICAL services
            else
                enable_keep_alive_monitoring(services[i].q_name,true); // Enable monitoring in case of svc pacify false for CRITICAL services
        }
    }
}
// Function with no return must have void return type
static void process_keep_alive(generic_msg_t *msg) {
    string service = string(msg->client_id);

    if (svc_pacify_recvd)
    {
        //Disabling monitoring in case of svc_pacify for CRITICAL services
        update_monitoring_parameters();
        nd_device_obj->kick_wdog();

    }
    // Even if svc_pacify_recvd, monitoring should happen for NON CRITICAL services
    res_keep_alive_msg_t *m = (res_keep_alive_msg_t*)msg;
    if( service == get_msgq_name() ) {
        /*Message from SVC itself, do house keeping*/
        do_house_keeping();
    } else {
        /*Message from other services, update stats*/
        update_service_stats(service, m->status );
    }

}

// captures states of buttons ; start with released state
static button_levels_t button_state[2] = {BUTTON_RELEASED, BUTTON_RELEASED};
// will have fall and raise times of both buttons
static int64_t button_state_time[2][2] = {0,0,0,0};

// this can be implemented with condition_variable without async_max
// will be moving thsi to future task
#define async_max (2*5*max_long_press_duration)
std::atomic<int> async_count ;
static std::future<void> button_longpress_res[async_max];

static bool send_button_alert_msg(int button) {
    user_alert_msg_t msg;

#ifdef KRAIT
    get_system_time_32(&msg.timestamp1, &msg.timestamp2);
#else
    msg.timestamp = get_system_time();
#endif

    msg.button = button;

    std::string source = "Button " + std::to_string(button);
    nd_strncpy(msg.source, source.c_str(), sizeof(msg.source));

    LOG_C(TAG, "Sending message for button falling: %d", button);
    send_msg( (generic_msg_t *)&msg, USER_ALERT, sizeof(msg), get_msgq_name(), get_nd_msgq_name(), msg_idx++ );
    return true;
}

static bool send_button_longpress_msg(int button) {
    user_alert_msg_t msg;

#ifdef KRAIT
    get_system_time_32(&msg.timestamp1, &msg.timestamp2);
#else
    msg.timestamp = get_system_time();
#endif

    msg.button = button;
    LOG_C(TAG, "Sending message for button longpress: %d", button);
    send_msg( (generic_msg_t *)&msg, BUTTON_LONG_PRESS, sizeof(msg), get_msgq_name(), get_nd_msgq_name(), msg_idx++ );
    return true;
}

static bool send_button_longpress_inst_msg(int button) {
    user_alert_msg_t msg;
#ifdef KRAIT
    get_system_time_32(&msg.timestamp1, &msg.timestamp2);
#else
    msg.timestamp = get_system_time();
#endif
    msg.button = button;
    LOG_C(TAG, "Sending message for button longpress for installer: %d", button);
    send_msg( (generic_msg_t *)&msg, BUTTON_LONG_PRESS_INST, sizeof(msg), get_msgq_name(), get_btfv_msgq_name(), msg_idx++ );
    return true;
}

void check_for_longpress(int button_number) {
    LOG_I(TAG, "entered into async check_for_longpress button_number %d", button_number);
    // capture the BUTTON_PRESSED time before going to sleep
    int64_t pressed_time = button_state_time[button_number][BUTTON_PRESSED];
    //usleep(long_press_duration_ms*1000);
    std::this_thread::sleep_for(std::chrono::milliseconds(long_press_duration_ms));
    // button should be high and button event time should be same as pressed_time
    if(button_state[button_number] == BUTTON_PRESSED &&
        pressed_time ==  button_state_time[button_number][BUTTON_PRESSED] ) {
            send_button_longpress_msg(button_number);
    } else {
        LOG_C(TAG, "button_state[%d] is %d for pressed_time %lld is not a long press",
            button_number, button_state[button_number], pressed_time);
    }

    // Sleep for the difference of installer and privacy press duration
    std::this_thread::sleep_for(std::chrono::milliseconds(inst_long_press_duration_ms - long_press_duration_ms));

    if( (button_state[button_number] == BUTTON_PRESSED) &&
        (pressed_time ==  button_state_time[button_number][BUTTON_PRESSED]) ) {
        LOG_C(TAG, "Button %d is pressed since %lld time; must be a long press for installer",
                button_number, pressed_time);
        send_button_longpress_inst_msg(button_number);
    } else {
        LOG_C(TAG, "button_state[%d] is %d for pressed_time %lld is not a long press",
            button_number, button_state[button_number], pressed_time);
    }

}

bool proces_button_event(alert_buttons_t button) {
    button_levels_t temp_state = nd_device_obj->get_button_level(button);

    if(button > nd_device_obj->get_button_number()) {
        LOG_I(TAG, "Button number %d is not supported", button);
    }

    if(temp_state != BUTTON_ERROR) {
        button_state[button] = temp_state;
    }
    else {
        LOG_E(TAG,"error state in proces_button_event; not changing prev state");
        return true;
    }

    // Filling the time for button 0/1 at pressed/released. Hence, 2D array
    button_state_time[button][button_state[button]] = get_system_monotonic_time();
    LOG_C(TAG, "button_state[%d] = %d, button_state_time[%d][%d] = %lld",
        button, button_state[button], button, button_state[button], button_state_time[button][button_state[button]]);
    if(button_state[button] == BUTTON_PRESSED) {
        LOG_I(TAG, "std::async task launching");
        async_count++;
        button_longpress_res[async_count%async_max] = std::async(std::launch::async, check_for_longpress, button );
        LOG_I(TAG, "std::async task launched async_count %d", async_count.load());
    }
    else if( button_state[button] == BUTTON_RELEASED &&
        get_system_monotonic_time() < (button_state_time[button][BUTTON_PRESSED] + long_press_duration_ms) ) {

        LOG_C(TAG, "button_state[%d] == BUTTON_RELEASED and high was at %lld ; sending user gen alert",
                button, button_state_time[button][BUTTON_PRESSED]);
        send_button_alert_msg(button);
    }
    else {
        LOG_C(TAG, "Ignoring release button since its a long press release");
    }
    return true;
}



static int button_cb1() {
    proces_button_event(eButton_1);
    return BUTTON_SUCCESS;
}
static int button_cb2() {
    proces_button_event(eButton_2);

    return BUTTON_SUCCESS;
}

#ifdef KRAIT
void* gpio24_interrupt_thread_fn(void* args)
{
    LOG_I(TAG, "inside gpio24_interrupt_thread_fn");

    int ret = -1;
    while(1) {
        ret = sysfs_init_interrupt_enable(24, GPIO_EVENT_BOTH);
        if(ret != 0) {
            LOG_E(TAG, "error exporting the super cap gpio");
            sleep(2);
            continue;
        }
        break;
    }

    int gpio24_fd;
    string gpio_level_info_file = "/sys/class/gpio/gpio24/value";
    gpio24_fd = open(gpio_level_info_file.c_str(), O_RDWR | O_NONBLOCK);
    if (gpio24_fd < 0) {
        string str_msg = "failed in open " + gpio_level_info_file;
        LOG_C(TAG, str_msg.c_str());
        nd_service_obj->send_err_msg(SM_E_PM_GPIO_INT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        return NULL;
    }

    char a = 'e';
    fd_set fds;

    ret = read(gpio24_fd,&a,1);
    if(ret != 1){
        LOG_E(TAG, "failed to read appropriate value; %d, %s", ret, strerror(errno));
    }

    LOG_I(TAG, "initial button gpio %c", a);

    while (1) {
        FD_ZERO(&fds);
        FD_SET(gpio24_fd, &fds);

        ret = select(gpio24_fd+1, NULL, NULL, &fds, NULL);
        if (ret > 0 && FD_ISSET(gpio24_fd, &fds)) {
            ret = lseek(gpio24_fd, SEEK_SET, 0);
            if(ret == -1) {
                LOG_E(TAG, "lseek error %d, %s", ret, strerror(errno));
                continue;
            }
            ret = read(gpio24_fd,&a,1);
            if(ret != 1){
                LOG_E(TAG, "failed to read appropriate value; continue, %d, %s", ret, strerror(errno));
                continue;
            }
            // call the button event processing
            proces_button_event( nd_device_obj->get_button_number() ); // button 0
        }
        LOG_I(TAG, "button status %c",a);

    }
    return NULL;
}
#endif
bool set_led_status_for_rental(bool enabled)
{

    if(!enabled)
    {
        //clearing the leds
        nd_device_obj->nd_set_led_on(RED, POWER_LED, false );
        nd_device_obj->nd_set_led_on(GREEN, POWER_LED, false );
        nd_device_obj->nd_set_led_on(RED, PRIVACY_LED, false);
        nd_device_obj->nd_set_led_on(GREEN, PRIVACY_LED, false);

        // setting the power and privacy led to RED
        nd_device_obj->nd_set_led_on(RED, POWER_LED, true );
        nd_device_obj->nd_set_led_on(RED, PRIVACY_LED, true);
        LOG_I(TAG,"Setting Power Led To RED and PrivacyLed to RED");

    }
    else
    {
        LOG_I(TAG," Data Recording Is Disabled, Led behaviour Will be Handled By Bagheera Service.");
    }

    return true;
}


static bool button_init() {
    bool status = false;
//TODO : Move thread to factory class
#ifdef KRAIT
    pthread_t gpio24_interrupt_thread;

    if(pthread_create(&gpio24_interrupt_thread, NULL, gpio24_interrupt_thread_fn, NULL) == 0) {
        LOG_I(TAG,"Button event thread created successfully");
        return true;
    }
    status = false;
#else
    status = nd_device_obj->register_button_interrupt(eButton_1, button_cb1);
    status &= nd_device_obj->register_button_interrupt(eButton_2, button_cb2);
#endif
    return status;

}
// Function to enable/disable monittoring for a service
static void enable_keep_alive_monitoring(string service, bool enable){
    for( int i=0; i<services_len; i++ ) {
        if( services[i].q_name == service ) {
            if(enable){
                LOG_I(TAG,"Starting keep alive monitoring for %s service and updating service stats",services[i].name.c_str());
                update_service_stats(services[i].q_name,0); // Updating service stats as the message recevied from the service
            }else{
                LOG_I(TAG,"Stopping keep alive monitoring for %s service",services[i].name.c_str());
                services[i].healthy = false; // Health should not be monitored
                services[i].curr_service_mode = STOP_MODE; // Setting to STOP MODE to disable monitoring
                services[i].previous_status = 0; // Setting to 0 as intial value
                services[i].last_keep_alive = get_system_monotonic_time(); // Disable monitoring message treated as keep alive
            }
        }
    }
}

static void msg_loop() {
    nd_msgq_t::nd_msg_t *msg;
    bool res;

    while(1) {
        int64_t curr_monotonic_time = get_system_monotonic_time();
        // In case of svc_util_init_failed, self keep alive will not be received
        // hence, svc will not be able to do monitoring and kick watchdog.
        // So, calling do_house_keeping to do monitoring and kick watchdog.
        // This will be done every POLL_INTERVAL (5) seconds, for svc_boot_timeout_sec. (10mins) default
        // And message receive will be done with non blocking call in the next iteration.
        if((true == svc_util_init_failed) || (true == msgq_init_failed)) {
            do_house_keeping(); // Do house keeping to update service stats and kick watchdog
            sleep(POLL_INTERVAL);
        }
        // It is possible to reach till here, if msgq_init_failed is true.
        // In that case, we will try to initialize the message queue.
        // If it is not initialized, we will sleep for POLL_INTERVAL and try again.
        // In this case watchdog kick will be done by calling do_house_keeping for svc_boot_timeout_sec. (10mins) default
        if((true == msgq_init_failed) || (server_q == NULL)) {
            LOG_E(TAG, "Message queue is not initialized");
            if(get_system_monotonic_time() > S_TO_MS(svc_watchdog_boot_timeout_sec)) {
                LOG_E(TAG, "svc_boot_timeout_sec reached, sending message to power_mon to reboot");
                do_power_mon_or_system_reboot(); // Send message to power monitor to reboot the system
            }
            msgq_init_failed = (init_msgq() == false);
            continue;
        }
        if (true == svc_util_init_failed) {
            if ((msg = server_q->receive(IPC_NOWAIT)) == NULL) {
                LOG_E(TAG, "Receive message failed (IPC_NO_WAIT)");
                continue;
            }
        } else {
            if ((msg = server_q->receive()) == NULL) {
                LOG_E(TAG, "Receive message failed");
                continue;
            }
        }


        msg_type_t type = get_msg_type(msg->get_buffer());
        generic_msg_t *m = (generic_msg_t *)msg->get_buffer();

        if( m == NULL ) {
            LOG_E(TAG, "Received NULL message");
            continue;
        }

        //LOG_D(TAG, "%d received", m->msg_type);

        switch( type ) {
            case RES_KEEP_ALIVE:
                process_keep_alive( m );
                break;
            case SVC_PACIFY_START:
                LOG_I (TAG,"START PACIFY received");
                svc_pacify_recvd = true;
                update_monitoring_parameters(); // Disable monitoring for CRITICAL Services
                break;

            case SVC_PACIFY_STOP:
                LOG_I (TAG,"STOP PACIFY received");
                svc_pacify_recvd = false;
                update_monitoring_parameters(); // Enable monitoring for CRITICAL Services
                break;

            case TRIGGER_CLEANUP:
                LOG_I (TAG, "TRIGGER_CLEANUP recieved");
                diskmon_cleanup_process();
#ifdef ROUTE_LOGS
                route_logs( log_dir.c_str() );
#endif
                LOG_E(TAG, "Log rotated post diskmon_cleanup_process");
                break;
            case UPDATE_WATCHDOG_TIMEOUT:
                should_kick_watchdog = false;
                LOG_I(TAG, "UPDATE_WATCHDOG_TIMEOUT received ");

                nd_device_obj->update_wdog( ((svc_update_watchdog_timeout_msg_t*)m)->timeout );

                break;
            case RES_DATA_RECORD_STATUS:
                {
                    LOG_I(TAG, "DATA_RECORD_METRICS received ");
                    data_record_metrics_t *msgr = (data_record_metrics_t*) m;
                    handle_data_record_status(*msgr);
                }
                break;
            case INSTALLER_SCAN_INDICATE:
                {
                    LOG_I(TAG,"INSTALLER_SCAN_INDICATE msg received");
                    if(is_led_blinking == false)
                    {
                        nd_device_obj->nd_clear_led( BLINKING_LED_RED );
                        nd_device_obj->nd_clear_led( BLINKING_LED_GREEN );
                        nd_device_obj->nd_clear_led( INST_BLINKING_LED );
                        nd_device_obj->nd_blink_led( INST_BLINKING_LED, 0);   // netradyne api
                        nd_device_obj->nd_blink_led( INST_BLINKING_LED, DUTYCYCLE_50);   // net
                        is_led_blinking = true;
                       // LOG_I (TAG,"Inst Led blink started by nd_blink_led():: ctx.inst_led_blinking_durarion: %d ",  ctx.inst_led_blinking_durarion);
                        std::async(std::launch::async, led_blinking_timer, LED_BLINK_DURATION);
                        LOG_I (TAG,"led_blinking_timer() is called with async");
                    }
                    else
                    {
                        LOG_I (TAG,"Led is already blinking hence ignore this event for led blink");
                    }
                    break;
                }
                #ifdef AUTOMATION
                case TEST_AUTOMATION:
                    {
                        LOG_I(TAG,"TEST_ALERT msg received");
                        test_automation_msg_t *msgr = (test_automation_msg_t*) m;
                        msg_type_t test_type = (msg_type_t)msgr->internal_msg_type;
                        alert_buttons_t button_number = (alert_buttons_t)msgr->button_no;
                        if (test_type == TEST_USER_ALERT)
                        {
                            send_button_alert_msg(button_number);
                        }
                        else if (test_type == TEST_LONG_PRESS)
                        {
                            send_button_longpress_msg(button_number);
                        }
                        else if (test_type == TEST_LONG_PRESS_INST)
                        {
                            send_button_longpress_inst_msg(button_number);
                        }
                        else {
                            LOG_E(TAG, "Unknown test type: %d", test_type);
                        
                        }
                    }
                    break;
                #endif
            case ENABLE_MONITORING:
                {
                    enable_monitoring_msg_t *msg = (enable_monitoring_msg_t*)m;
                    string service = string(msg->client_id);
                    bool enable = msg->enable;
                    enable_keep_alive_monitoring(service, enable);
                    break;
                }
            default:
                LOG_E(TAG, "Unknown message: %d", type);
        }

        delete msg;
    }
}

bool init_with_default(void) {
    //if bagheera_config.ini is corrupted and config read failed, this init will
    //stop svc to trigger the cleanup due to incorrect config value, or 0.

    LOG_I(TAG, "init CLEANUP_TRIGGER_LOG with default value as config file is corrupted");
#ifdef KRAIT
    CLEANUP_TRIGGER_LOG = default_cleanup_trigger_max * GB_TO_BYTE;
#endif

    return true;
}

bool get_config() {
    Config_parser config(config_file);

    bool val_overridden;

    if( false == config.getParseStatus() ) {
        LOG_E(TAG, "Cannot parse config: %s", config_file.c_str());
        LOG_I(TAG, "Recovery enabled: %d, Diskmon enabled: %d", recovery_enabled, diskmon_enabled);
        init_with_default();
        return false;
    }

    if (config.isPresent ("svc","config_recovery_enable")) {
        if (config.getConfig("svc","config_recovery_enable","") == "false") {
            recovery_enabled = false;
        }
    }

    if (config.isPresent ("svc","config_recovery_poll")) {
        string poll = config.getConfig("svc","config_recovery_poll","");
        stringstream ss(poll);
        int ipoll;

        ss >> ipoll;
        if( false == ss.fail() ) {
            config_recovery_poll = ipoll;
            LOG_I(TAG, "read config_recovery_poll: %d", config_recovery_poll);
        } else {
            LOG_E(TAG, "Error parsing recovery poll");
        }
    }

    LOG_I(TAG, "config_recovery_poll: %d", config_recovery_poll);

    if (config.isPresent ("svc","diskmon_enable")) {
        if (config.getConfig("svc","diskmon_enable","") == "false") {
            diskmon_enabled = false;
        }
    }

    if (config.isPresent ("svc","diskmon_poll")) {
        string poll = config.getConfig("svc","diskmon_poll","");
        stringstream ss(poll);
        int ipoll;

        ss >> ipoll;
        if( false == ss.fail() ) {
            diskmon_poll = ipoll;
            LOG_I(TAG, "read diskmon_poll: %d", diskmon_poll);
        } else {
            LOG_E(TAG, "Error parsing diskmon poll");
        }
    }

    LOG_I(TAG, "diskmon_poll: %d", diskmon_poll);

    {
        string watchdog_timeout_str = config.getConfig("svc","watchdog_timeout", to_string(WATCHDOG_TIMEOUT), true, val_overridden);
        LOG_I(TAG, "read watchdog_timeout from config in secs: %s", watchdog_timeout_str.c_str());
        if(false == string_to_integer(watchdog_timeout_str, watchdog_timeout)) {
            LOG_E(TAG, "Error parsing watchdog_timeout, using default value: %d", WATCHDOG_TIMEOUT);
            watchdog_timeout = WATCHDOG_TIMEOUT;
        }
        LOG_I(TAG, "val_overridden: %d", val_overridden);
        if((watchdog_timeout < MIN_WDOG_TIMEOUT) || (watchdog_timeout > MAX_WDOG_TIMEOUT)){
            LOG_E(TAG,"Watchdog timeout not in range %d setting default %d", watchdog_timeout, WATCHDOG_TIMEOUT);
            watchdog_timeout = WATCHDOG_TIMEOUT;
        }
        LOG_I(TAG, "watchdog_timeout: %d" , watchdog_timeout);
        if( (true == nd_factory_utils::is_pmic_wdog_supported())
            && (watchdog_timeout > pmic_wdog_timeout)){
            max_pmic_wdog_kick_counter = (watchdog_timeout - pmic_wdog_timeout) / POLL_INTERVAL;
        }
        LOG_I(TAG, "max_pmic_wdog_kick_counter: %d", max_pmic_wdog_kick_counter);

        string svc_failure_case_timeout = config.getConfig("svc","svc_failure_case_timeout", to_string(svc_watchdog_boot_timeout_sec), true, val_overridden);
        LOG_I(TAG, "read svc_failure_case_timeout from config in secs: %s", svc_failure_case_timeout.c_str());
        if(false == string_to_integer(svc_failure_case_timeout, svc_watchdog_boot_timeout_sec)) {
            svc_watchdog_boot_timeout_sec = SVC_WATCHDOG_BOOT_TIMEOUT_SEC;
            LOG_E(TAG, "Error parsing svc_failure_case_timeout, using default value: %d", svc_watchdog_boot_timeout_sec);
        }else{
            LOG_I(TAG, "svc_failure_case_timeout: %d" , svc_watchdog_boot_timeout_sec);
        }
        if((svc_watchdog_boot_timeout_sec < MIN_SVC_BOOT_TIME_TIMEOUT) || (svc_watchdog_boot_timeout_sec > MAX_SVC_BOOT_TIME_TIMEOUT)){
            svc_watchdog_boot_timeout_sec = SVC_WATCHDOG_BOOT_TIMEOUT_SEC;
        }
    }

#ifdef KRAIT

    if (config.isPresent ("svc","cleanup_trigger_size")) {
        string min_limit = config.getConfig("svc","cleanup_trigger_size", to_string(default_cleanup_trigger_max), true, val_overridden);

        int64_t limit = atoll(min_limit.c_str());
        if(!( (limit >= LOG_CLEANUP_MIN_LIMIT) && (limit <= LOG_CLEANUP_MAX_LIMIT) ) ) {
            LOG_E(TAG, "/data cleanup threshold is out of limit(2GB < limit < 4GB), setting to default limit : %lld GB", default_cleanup_trigger_max);
            limit = default_cleanup_trigger_max;
        }
        //converting limit from GB to byte as disk_mon will get the free space
        //in bytes
        CLEANUP_TRIGGER_LOG = limit * GB_TO_BYTE;
        LOG_I(TAG, "read cleanup_trigger_size: %lld GB", limit);
    }

    LOG_I(TAG, "cleanup_trigger_size : %lld (byte)", CLEANUP_TRIGGER_LOG);

    if (config.isPresent ("svc","qcs_alive_enable")) {
        if (config.getConfig("svc","qcs_alive_enable",qcs_alive_enable_default_str, true, val_overridden) == "false") {
            qcs_alive_enable = false;
        }
    }

    if (config.isPresent ("svc","qcs_alive_shutdown_reboot_indicate")) {
        if (config.getConfig("svc","qcs_alive_shutdown_reboot_indicate",qcs_alive_shdn_rbt_indicate_default_str, true, val_overridden) == "false") {
            qcs_alive_shutdown_reboot_indicate = false;
        }
    }

#endif
    LOG_I(TAG, "Recovery enabled: %d, Diskmon enabled: %d", recovery_enabled, diskmon_enabled);


   string_to_integer(config.getConfig("INSTALLER_APP",
                                        "inst_long_press_duration_ms","10000" , true, val_overridden),
                                        inst_long_press_duration_ms );

    if( inst_long_press_duration_ms > (inst_max_long_press_duration*1000) ) {
        inst_long_press_duration_ms = inst_max_long_press_duration*1000;
    }
    if( inst_long_press_duration_ms < (inst_min_long_press_duration*1000) ) {
        inst_long_press_duration_ms = inst_min_long_press_duration*1000;
    }

    LOG_I(TAG, "inst_long_press_duration_ms %d", inst_long_press_duration_ms);

    string_to_integer(config.getConfig("privacy_mode_activate",
                                        "long_press_duration_ms","5000" , true, val_overridden),
                                        long_press_duration_ms);

    if( long_press_duration_ms > (max_long_press_duration*1000) ) {
        long_press_duration_ms = max_long_press_duration*1000;
    }
    if( long_press_duration_ms < (min_long_press_duration*1000) ) {
        long_press_duration_ms = min_long_press_duration*1000;
    }

    LOG_I(TAG, "long_press_duration_ms: %d", long_press_duration_ms);
    return true;
}

#ifdef KRAIT
bool send_qcs_alive(){
    if(false == nd_msp_qcs_alive_signal()){
        string str_msg = "MSP qcs alive signal send error, system shall reboot anytime";
        SVC_LOG_E(TAG, "%s", str_msg.c_str());
        nd_service_obj->send_err_msg(SM_E_SVC_KEEP_ALIVE_TIMEOUT, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        return false;
    }
    LOG_I(TAG, "MSP QCS alive signal sent successfully.");
    return true;
}

bool msp_qcs_alive(){
    uint retry_count =0;
    int required_msp_qcs_alive_config_value = 0;
    int curr_qcs_alive_config = 0, curr_qcs_alive_timer_value = 0;
    bool msp_qcs_alive_setup_required = false, status = false;
    const unsigned int max_retry = 3;
    const unsigned int msp_qcs_alive_enable_bit_position = 0;
    const unsigned int msp_qcs_shdn_rbt_indicate_bit_position = 1;

    if (qcs_alive_enable){
        required_msp_qcs_alive_config_value  =  1 << msp_qcs_alive_enable_bit_position ;
    }
    if (qcs_alive_shutdown_reboot_indicate) {
        required_msp_qcs_alive_config_value  |= ( 1 << msp_qcs_shdn_rbt_indicate_bit_position ) ;
    }

    while( (true != status) && (max_retry > retry_count++)) {
        LOG_I(TAG, "Entered to check and send MSP qcs alive with retry_count %d", retry_count);
        if (false == nd_read_msp_qcs_alive_monitor_ctrl_cmd(&curr_qcs_alive_config)){
            string str_msg = "Error reading MSP QCS Alive current configuration";
            SVC_LOG_E(TAG, "%s", str_msg.c_str());
            nd_service_obj->send_err_msg(SM_E_SVC_KEEP_ALIVE_TIMEOUT, NDService::UNUSED_ERR_AUX_CODE, str_msg );
            continue;
        }

        if (false == nd_read_msp_qcs_alive_check_timer_value_cmd(&curr_qcs_alive_timer_value)){
            string str_msg = "Error reading MSP QCS Alive current timer value";
            SVC_LOG_E(TAG, "%s", str_msg.c_str());
            nd_service_obj->send_err_msg(SM_E_SVC_KEEP_ALIVE_TIMEOUT, NDService::UNUSED_ERR_AUX_CODE, str_msg );
            continue;
        }

        msp_qcs_alive_setup_required = (required_msp_qcs_alive_config_value != curr_qcs_alive_config);

        LOG_I (TAG, "required_msp_qcs_alive_config_value Alive enabled in bagheera config is %d", required_msp_qcs_alive_config_value);
        LOG_I (TAG, "Current QCS Alive configuration in MSP is %d ", curr_qcs_alive_config);
        // Bagheera config with qcs_alive disable.
        // Setup MSP only if there is a mismatch between the two variables.
        if( msp_qcs_alive_setup_required) {
            LOG_I(TAG, "MSP setup required for qcs alive configuration");
            if ( false == nd_write_msp_qcs_alive_monitor_ctrl_cmd(required_msp_qcs_alive_config_value) ) {
                string str_msg = "Error encountered setting msp qcs alive setting";
                SVC_LOG_E(TAG, "%s", str_msg.c_str());
                nd_service_obj->send_err_msg(SM_E_SVC_KEEP_ALIVE_TIMEOUT, NDService::UNUSED_ERR_AUX_CODE, str_msg );
                continue;

            }
            LOG_I(TAG, "MSP QCS alive monitor set to %d in msp", required_msp_qcs_alive_config_value);

        }

        if( qcs_alive_timer != curr_qcs_alive_timer_value ) {
            LOG_I(TAG, "MSP setup required for qcs alive timer value register");
            if (false == nd_write_msp_qcs_alive_check_timer_value_cmd(qcs_alive_timer)){
                string str_msg = "Failed while trying to set timer value.";
                SVC_LOG_E(TAG, "%s", str_msg.c_str());
                nd_service_obj->send_err_msg(SM_E_SVC_KEEP_ALIVE_TIMEOUT, NDService::UNUSED_ERR_AUX_CODE, str_msg );
                continue;
            }
        }

        LOG_I (TAG, "MSP setup successfull");
        status = send_qcs_alive();
    }
    return status;

}
#endif  // KRAIT

bool send_default_start_keepalive() {

    for( int i=0; i<services_len; i++ ) {
        if( services[i].keep_alive_at_start == false ) {
            LOG_I(TAG, "Ignoring keep_alive_at_start for service %s", services[i].name.c_str());
            continue;
        }
        // Adding analytics here for handling rental flow. This can also be handled by checking the curr_service_mode == STOP_MODE
        if( (services[i].q_name == ND_CENTRAL_Q || services[i].q_name == ND_ANALYTICS_Q) && (record_data_data == false) ) {  //this change was done to maintain the state of bagheera to stop_mode incase of rental disabled DT-837
            LOG_I(TAG, "Ignoring keep_alive_at_start for service %s as data_recording is disabled", services[i].name.c_str());
            continue;
        }
        LOG_I(TAG, "registering keep_alive_at_start for service %s", services[i].name.c_str());
        res_keep_alive_msg_t ka_m;
        memset(&ka_m, 0, sizeof(ka_m));

        strncpy( ka_m.client_id, services[i].q_name.c_str() , sizeof(ka_m.client_id) );
        ka_m.msg_type = RES_KEEP_ALIVE;
        ka_m.status = 0 ; // 0 is to tell all the required thereds are working fine in the service
        ka_m.length=sizeof(ka_m);

        nd_msgq_t::nd_msg_t msg((char *)&ka_m, sizeof(ka_m), false);
        server_q->send(msg,nd_msgq_t::ND_MSG_MED);
    }

    return true;
}

int main(int argc, char *argv[]) {
    nd_service_obj = NDService::get_service_obj(TAG);
    printf("initilizing logger\n");
    bool res;
    bool status_log = nd_log_init( log_dir.c_str() );
    if(status_log == false) {
        // Incase of logger init failure, svc service will crash and restart, earlier in this watchdog was not kicked
        // To avoid indefinite hang, in case of logger init failure, we will write error and critical logs to serial port
        // and some of the impportant logs will be written to syslog
        printf("unable to init logger");
        use_printf_log = true;
    }

    if(true == status_log){
        #ifdef ROUTE_LOGS
        route_logs( log_dir.c_str() );
        #endif
    }else{
        openlog(TAG, LOG_PID|LOG_CONS|LOG_NDELAY, LOG_USER);
        route_logs_filename("/dev","ttyS0");
        update_log_level(LOG_LEVEL_E);
    }
    SVC_LOG_I(TAG, "######Starting SVC######");

    nd_device_obj_init();
    //Read configuration
    get_config();
//Read data_record_status from DB
    data_record_status_db db_data;
    if(get_data_record_status_db(db_data))
    {
        LOG_I(TAG,"Read record_data succesfully");
    }
    else
    {
        LOG_I(TAG,"Reading of record_data failed");
    }
    record_data_data = db_data.enabled;

    /*Calling this to handle when Ota upgrade happens if the data_recording state is disabled, *
     *since run_as_root is enabling all the services, we are disabling it here */
    LOG_D(TAG,"Rental Service Start Time : ");
    // initiate rental service based on SKU's
    nd_factory_utils::get_rental_disabled_services(disabled_services_for_rental);
    std::future<bool> fut =  std::async(std::launch::async, handle_service_for_rental, record_data_data, true );
    LOG_D(TAG,"Rental Service Stop Time : ");

    set_led_status_for_rental(record_data_data);

    data_record_metrics_t msg;
    send_msg((generic_msg_t *)&msg, REQ_DATA_RECORD_STATUS, sizeof(msg), get_msgq_name(), get_awsiot_msgq_name(), 0);


#ifdef KRAIT
    if (false == msp_qcs_alive()) {
        SVC_LOG_E(TAG, "MSP QCS alive failure");
    }
#endif
    // Regardless of the msg_q_init status, the rest of the initialization will happen
    // as other threads like diskmon, should be able to run to check and clean the disk space
    // in this case system will be up for svc_watchdog_boot_timeout_sec (default) 10 mins
    if( init_msgq() == false ) {
        /*Critical error, return and allow systemd to restart*/
        LOG_C(TAG, "Init msgq failed,running svc for failure_case_timeout %d secs", svc_watchdog_boot_timeout_sec);
        msgq_init_failed = true;
        // return false;
    }

    if( button_init() == false ) {
        SVC_LOG_E(TAG, "Cannot initialize button");
        //Not exiting as most functionality will be unaffected without button
    }

    //retry init_watchdog in a loop
    int wdt_init_count = 6;
    while(wdt_init_count) {

        if( nd_device_obj->init_wdog(watchdog_timeout) == true ) {
            LOG_I(TAG, "init_watchdog success");
            break;
        }
        SVC_LOG_E(TAG, "Watchdog init failed, retrying");
        sleep(5);
        wdt_init_count--;
    }
    // Incase of failure, of watchdog init, service will run to perform the other functionalities
    if(!wdt_init_count) {
        SVC_LOG_C(TAG, "Watchdog init failed");
        nd_service_obj->send_err_msg(SM_E_SVC_WDT_INIT_TIMEOUT, wdt_init_count,
             "Watchdog init failed, exiting from svc" );
        wdog_init_failed = true;
    }

    // Incase of failure, of svc_util_init, service will run to perform the other functionalities
    // and will kick watchdog for svc_watchdog_boot_timeout_sec (default) 10 mins
    // after which system will reboot
    if( svc_util_init(get_msgq_name(), POLL_INTERVAL) == false) {
        /*Critical error, return and allow systemd to restart*/
        LOG_C(TAG, "Init timerthread failed,running svc for failure_case_timeout %d secs", svc_watchdog_boot_timeout_sec);
        svc_util_init_failed = true;
        // return false;

    }

    init_var_diskmon();

    if( diskmon_enabled == true ) {
        res = diskmon_init(diskmon_poll);
        if( res == false ) {
            SVC_LOG_E(TAG, "Diskmon init status: %d", res);
        } else {
            SVC_LOG_I(TAG, "Diskmon init status: %d", res);
        }
    }

    if( recovery_enabled == true ) {
        res = recovery_init(config_recovery_poll);
        if( res == false ) {
            SVC_LOG_E(TAG, "Recovery init status: %d", res);
        } else {
            SVC_LOG_I(TAG, "Recovery init status: %d", res);
        }
    }

    // initilizing timeouts as per need
    int service_timeout = default_service_timeout;
    const string SERVICE_TIMEOUT_FILE  = nd_device_obj->get_service_timeout_file();

    if(file_is_present(SERVICE_TIMEOUT_FILE)) {
        ifstream service_timeout_file;
        service_timeout_file.open(SERVICE_TIMEOUT_FILE.c_str());
        string service_timeout_string;
        if(service_timeout_file.is_open()){
            service_timeout_file >> service_timeout_string;
        }
        service_timeout_file.close();
        if(!string_to_integer(service_timeout_string, service_timeout)) {
            service_timeout = default_service_timeout;
        }
        if(service_timeout < default_service_timeout) {
            service_timeout = default_service_timeout;
        }
        LOG_W(TAG, "changing default timeout of all services to %d", service_timeout);
        for( int i=0; i<services_len; i++ ) {
            services[i].keep_alive = service_timeout;
        }
    }

    for( int i=0; i<services_len; i++ ) {
        LOG_I(TAG, "service %s timeout %d",services[i].name.c_str(), services[i].keep_alive );
    }

    // Only if msgq_init is successful, we will send the default keepalive for required services
    // This is to avoid sending keepalive messages when msgq_init fails, which can lead to unexpected behavior or service crashes.
    // If msgq_init_failed then svc time_thread will kick watchdog (if it is failing to send msg) for svc_watchdog_boot_timeout_sec (default) 10 mins
    // after which system will reboot
    if(false == msgq_init_failed) {
        // send default keepalive for required services, only if msgq_init is successful
        send_default_start_keepalive();
    }

    msg_loop();
    nd_service_obj->release_service_obj();
    return 0;
}
