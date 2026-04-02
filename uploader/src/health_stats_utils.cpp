#include "health_stats_utils.h"
#include "nd_msgq.h"
#include "nd_msg_types.h"
#include "nd_msg_utils.h"
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string>
#include <sstream>
#include <algorithm>
#include <jansson.h>
#include <log.h>
#include <nd_file_utils.h>
#include "service_utils.h"
#include <nd_net_utils.h>
#include <nd_time.h>
#include <system_utils.h>
#include <nd_task.h>
#include <condition_variable>
#include <mutex>


#define Q_NAME_UPLOADER_CM "UniUploadCM"
#define Q_NAME_CONN_MGR "CONN_MGR"

static const char *TAG="UPL_HS";
static const int HS_INFO_POLL_INTERVAL_ACTIVE = 15;
static const int HS_INFO_POLL_INTERVAL_IDLE = 500000; //138 hrs, ~ forever, considering device likely to reboot before this time

static int HS_INFO_POLL_INTERVAL = HS_INFO_POLL_INTERVAL_IDLE;
static bool triggered = false;
static bool shutdown_hs_thread_flag = false; // Flag to signal thread shutdown
static pthread_t hs_thread = 0; // 0 means not created

std::mutex upl_hs_mtx_sig_info;
std::condition_variable cv_collect_upl_hs;


// Structure to store the active sessions for HS collection
//In health stats, each info is uniquely identified as <filename>_<rc>_<sample_count>
struct SessionInfo {
    std::string filename;
    int rc; //retry count
    bool poll; //Used to identify if the info needs to be collected one-time or recurrently
    int sig_info_sample_count; //Used to keep track of the number of sig_info samples collected for a [session & rc]

    SessionInfo(std::string f, int r, bool p) :
        filename(f), rc(r), poll(p), sig_info_sample_count(0) {}
};

// List to store active sessions
std::vector<SessionInfo> session_list;

nd_msgq_t *msg_q;
extern NDService *nd_service_obj; //nd service object, to detect crashes


struct timed_task_args {
   string cmd;
   string result;
};



bool is_modem_up() {
    bool modem_state = false;
    try{
        int is_modem_up = system("ls /dev/ | grep qcqmi0");
        if(is_modem_up == 0){
            modem_state = true;
        }
        return modem_state;
    } catch(...) {
        LOG_E(TAG,"Exception in getting modem state");
        return modem_state;

    }
}


/**
 * @brief Removes a one-time session from the active session list.
 *
 * This function searches for a session in the active session list that matches the given
 * session's filename and has the 'poll' flag set to false (indicating a one-time session).
 * If such a session is found, it is removed from the list. The function is thread-safe and
 * acquires a lock on the session list mutex during the operation.
 *
 * @param session The SessionInfo object representing the session to be removed.
 */
void remove_one_time_session_from_active_list(const SessionInfo& session) {
    LOG_I(TAG, "Removing from active list: session %s, poll %d ", session.filename.c_str(), session.poll);

    std::lock_guard<std::mutex> lck(upl_hs_mtx_sig_info);

    auto it = std::find_if(session_list.begin(), session_list.end(),
            [&session](const SessionInfo& s) { return (s.filename == session.filename && s.poll == false); });

    if (it != session_list.end()) {
        session_list.erase(it);
        LOG_I(TAG, "Removed one-time session from active list: %s, active sessions for HS: %d",
                session.filename.c_str(), session_list.size());
    } else {
        LOG_W(TAG, "Session not found in active list: %s", session.filename.c_str());
    }
}

void send_alert_info_signal_data_healthstats(conn_mgr_sig_info_msg_t *sig_info_msg) {

    LOG_I(TAG, "Inside send_alert_info_signal_data_healthstats");

    // Copy session_list under lock for minimal lock scope
    std::vector<SessionInfo> sessions_copy;
    {
        std::lock_guard<std::mutex> lck(upl_hs_mtx_sig_info);
        sessions_copy = session_list;
    }

    for (size_t i = 0; i < sessions_copy.size(); ++i) {
        std::string sig_info_key_str = "signal_info_" +
                                std::to_string(sessions_copy[i].rc) +
                                "_" +
                                std::to_string(sessions_copy[i].sig_info_sample_count);

        const char* sig_info_key = sig_info_key_str.c_str();
        LOG_I(TAG, "sig_info_key = %s", sig_info_key);

        json_t *root = json_object();
        char* req_params = NULL;
        json_t *alert_info = json_object();
        json_t *element = json_object();

        if (!root || !alert_info || !element) {
            LOG_E(TAG, "Failed to allocate JSON objects.");
            if (root) json_decref(root);
            if (alert_info) json_decref(alert_info);
            if (element) json_decref(element);
            return;
        }

        json_object_set_new( root, "session", json_string(sessions_copy[i].filename.c_str()) );

        json_object_set_new( element, "rssi",  json_integer(sig_info_msg->rssi));
        json_object_set_new( element, "roaming",  json_string(sig_info_msg->roaming_status_str));
        json_object_set_new( element, "rat",  json_string(sig_info_msg->radio_interface_str));
        json_object_set_new( element, "rsrq",  json_integer(sig_info_msg->rsrq));
        json_object_set_new( element, "rsrp",  json_integer(sig_info_msg->rsrp));
        json_object_set_new( element, "band",  json_string(sig_info_msg->band_class_str));
        json_object_set_new( element, "regState",  json_string(sig_info_msg->reg_state_str));
        json_object_set_new( element, "homeNetwork",  json_string(sig_info_msg->home_network));
        json_object_set_new( element, "cellId",  json_integer(sig_info_msg->cell_id));
        json_object_set_new( element, "recordedTime",  json_integer(sig_info_msg->time));
        json_object_set_new( element, "sinr",  json_integer(sig_info_msg->sinr));
        json_object_set_new( element, "error_cause",  json_string(sig_info_msg->error_cause));
        json_object_set_new( element, "reg_mnc",  json_string(sig_info_msg->reg_mnc));
        json_object_set_new( element, "reg_mcc",  json_string(sig_info_msg->reg_mcc));
        json_object_set_new( element, "own_num",  json_string(sig_info_msg->own_num));

        json_object_set_new( alert_info, sig_info_key, element );
        json_object_set_new( root, "alert_info", alert_info );
        req_params = json_dumps(root, 0);
        if(req_params == NULL){
            LOG_E(TAG,"JSON creation failed for HS sig info message");
            json_decref(root);
            return;
        }
        LOG_I(TAG, "sending msg[%zu] to hs--: %s", i, req_params);
        int length = strlen(req_params);
        nd_service_obj->send_msg_healthstats(req_params, length);
        json_decref(root);
        free(req_params);

        remove_one_time_session_from_active_list(sessions_copy[i]);

        //sleep for 10 ms (i.e., 10,000 microseconds), for throttling at HS side to process the data
        usleep(10000);
    }

    //Change thread state to IDLE if no active sessions left
    {
        std::lock_guard<std::mutex> lck(upl_hs_mtx_sig_info);
        if (session_list.empty()) {
            HS_INFO_POLL_INTERVAL = HS_INFO_POLL_INTERVAL_IDLE;
            LOG_I(TAG, "No active sessions left, setting HS_INFO_POLL_INTERVAL to IDLE");
        }
    }
}

bool timed_task_cmd(void *arg) {
    struct timed_task_args *s = (timed_task_args*) arg;
    system_execute_with_resp(TAG,(s->cmd).c_str(),s->result);
    return true;
}

string get_cmd_output(string input_cmd) {
    std::string result;
    struct timed_task_args s = {input_cmd, result};

    task_result_t timed_task_result;
    timed_task_result = nd_timed_task(timed_task_cmd, 5, &s, "hs_alert_info");
    if(timed_task_result != TASK_SUCCESS) {
        LOG_E(TAG, "timeout reached in %s" , input_cmd.c_str());
        return "";
    }
    return s.result;
}

void fetch_signal_info_and_send_to_HS() {
    LOG_I (TAG,"start :: fetch_signal_info_and_send_to_HS");

    generic_msg_t t1;
    uint64_t ts_sig_info_collection_start = get_system_time();
    bool send_msg_status = send_msg( (generic_msg_t *)&t1, REQ_UPL_CONN_MGR_SIG_INFO,
                sizeof(t1), Q_NAME_UPLOADER_CM, Q_NAME_CONN_MGR, 0 );
    LOG_I (TAG,"Sent siginfo request msg: %d", send_msg_status);
    if(send_msg_status) {
        //receive msg from connection manager
        nd_msgq_t::nd_msg_t *msg;
        if( (msg = msg_q->receive( )) == NULL ) {
            LOG_E(TAG,"Receive message failed");
            return;
        }
        generic_msg_t *gen_msg = (generic_msg_t *) msg->get_buffer();
        if(gen_msg == NULL) {
            LOG_E(TAG,"Message buffer null. Skipping!!");
            return;
        }

        msg_type_t type = get_msg_type(gen_msg);
        LOG_I(TAG, "Msg type %d received", type);
        if(type == RES_UPL_CONN_MGR_SIG_INFO) {
            uint64_t ts_sig_info_collection_end = get_system_time();
            LOG_I(TAG, "Time taken to collect signal info: %llu ms", (ts_sig_info_collection_end - ts_sig_info_collection_start));
            conn_mgr_sig_info_msg_t *sig_info_msg = (conn_mgr_sig_info_msg_t*) gen_msg;
            send_alert_info_signal_data_healthstats(sig_info_msg);
        } else {
            LOG_I(TAG, "Msg type %d not identified", type);
        }

        delete msg;
    }
    else {
        LOG_E(TAG, "Failed to send siginfo request msg to connection manager");
    }
    LOG_I(TAG, "end :: fetch_signal_info_and_send_to_HS");
}

void collect_signal_info(string filename, int rc, bool poll) {
    LOG_I(TAG, "collect_signal_info :: session: %s, poll: %d", filename.c_str(), poll);

    {
        std::lock_guard<std::mutex> lock(upl_hs_mtx_sig_info);
        session_list.push_back({filename, rc, poll});
        LOG_I(TAG, "Active sessions for HS: %d", session_list.size());
        HS_INFO_POLL_INTERVAL = HS_INFO_POLL_INTERVAL_ACTIVE;
        triggered = true;
    }
    cv_collect_upl_hs.notify_one();
}


void stop_signal_info_polling(std::string filename) {
    LOG_I(TAG, "Stopping signal info polling for session: %s", filename.c_str());
    std::lock_guard<std::mutex> lock(upl_hs_mtx_sig_info);

    // Remove only the matching session, both one-time and recurrent
    auto it = session_list.begin();
    while (it != session_list.end()) {
        if (it->filename == filename) {
            LOG_I(TAG, "Removed a session with filename %s, poll %d", filename.c_str(), it->poll);
            it = session_list.erase(it);
        } else {
            ++it;
        }
    }
    LOG_I(TAG, "Active sessions for HS after removal: %d", session_list.size());
    // If no active sessions, reset poll interval
    if (session_list.empty()) {
        HS_INFO_POLL_INTERVAL = HS_INFO_POLL_INTERVAL_IDLE;
        LOG_I(TAG, "All sessions removed. Setting HS_INFO_POLL_INTERVAL to idle");
    }
}

void* handle_upl_hs(void* arg)  {
    LOG_I(TAG, "Inside handle_upl_hs");
    if( msg_q == NULL ) {
        LOG_E(TAG, "UPL_CM msg Q was not created. Cant collect signal info, exiting thread");
        return NULL;
    }
    while(true) {
        LOG_I(TAG, "handle_upl_hs:: Waiting for trigger/polling-interval to collect signal info");
        {
            std::unique_lock<std::mutex> lock(upl_hs_mtx_sig_info);
            // Wait for either poll_interval seconds, a trigger, or a shutdown signal
            cv_collect_upl_hs.wait_for(lock, std::chrono::seconds(HS_INFO_POLL_INTERVAL), [] { return triggered || shutdown_hs_thread_flag; });
        }

        if (shutdown_hs_thread_flag) {
            LOG_I(TAG, "handle_upl_hs:: Shutdown signaled. Exiting thread loop.");
            break; // Exit the while loop
        }
        LOG_I(TAG, "handle_upl_hs:: Woke up! Current triggered state: %d", triggered);
        //fetch signal info and send to HS for all the sessions in the list
        fetch_signal_info_and_send_to_HS();
        triggered = false;
    }
    LOG_I(TAG, "handle_upl_hs thread exiting.");
    return NULL;
}


int create_hs_thread() {
    // Check if thread is already created (not 0)
    if (hs_thread != 0) {
        LOG_E(TAG, "hs_thread is already created. Returning...");
        return 0;
    }

    int hs_th_status = pthread_create(&hs_thread, NULL, handle_upl_hs, NULL);
    if (hs_th_status != 0) {
        LOG_E(TAG, "Failed to create hs_thread thread");
        hs_thread = 0; // Reset on failure
    }
    else {
        LOG_I(TAG, "Created hs_thread successfully");
        pthread_setname_np(hs_thread, TAG_THREAD_UPL_HS_TH);
        pthread_detach(hs_thread);
    }
    return hs_th_status;
}

// Function to signal the health stats thread to shut down.
void request_hs_thread_shutdown() {
    LOG_I(TAG, "Requesting health_stats_utils thread shutdown.");
    {
        std::lock_guard<std::mutex> lock(upl_hs_mtx_sig_info);
        shutdown_hs_thread_flag = true;
    }
    cv_collect_upl_hs.notify_one(); // Wake up the thread if it's waiting
}

bool init_conn_mgr_msgq() {
    msg_q = nd_msgq_t::get_msgq( Q_NAME_UPLOADER_CM, nd_msgq_t::ND_MSGQ_SERVER);
    if( msg_q == NULL ) {
        LOG_E(TAG, "Cannot create UPL_CM message queue");
        return false;
    }
    return true;
}

