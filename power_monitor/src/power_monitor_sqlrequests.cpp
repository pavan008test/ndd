/* Copyright (C) 2019 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Y Suresh Kumar <suresh.kumar@netradyne.com>, April 2019
 */

#include <cstdlib>
#include <unistd.h>
#include <nd_msg_utils.h> 
#include <sys/time.h>
#include <errno.h>

#include "power_monitor.h"
#include "service_utils.h"
#include "nd_time.h"
#include "system_utils.h"

static const char *TAG="MP_SQL";

extern ND_DeviceFactory *nd_device_obj;
extern power_monitor_ctx *POWER_MONITOR_ctx;
extern NDService *nd_service_obj;
extern float prev_speed;
extern power_monitor_lpw_data_t lpw_data;

constexpr int64_t INVALID_TIMESTAMP = 0;

bool db_limit_rows(db_handle_t *db_handle) {
    int rc;
    std::stringstream val_stream;
    val_stream << "DELETE FROM POWERSTATES WHERE INDEXID IN (SELECT INDEXID FROM " \
        " POWERSTATES ORDER BY INDEXID DESC LIMIT -1 OFFSET 1000)";
    rc = POWER_MONITOR_ctx->exec_cmd_db(db_handle, val_stream.str(), NULL, NULL);
    if( rc == false ){
      LOG_E(TAG, "SQL error");
      return false;
    }
    return true;
}

bool add_event_db(power_dbstate_enum_t event_code, string action) {
    bool ret = add_event_db(POWER_MONITOR_ctx->db_handle, get_system_time(), POWER_MONITOR_ctx->boot_time,
                     POWER_MONITOR_ctx->pid_num, power_dbstate_enum_t::toString(event_code), action);
    LOG_I(TAG, "add_event_db() exiting, ret:  %d",ret);
    return ret;
}

bool add_event_db(db_handle_t *db_handle, int64_t event_time, int64_t boot_time, int pid_num,
                     string event, string action) {

    LOG_C(TAG, "inside add_event_db for %s event", event.c_str());
    int rc;

    std::stringstream val_stream;
    if(action == ""){
        action = "NA";
    }
    val_stream  << insert_str
                << "(" << boot_time << ", "
                << pid_num << ", "
                << event_time << ", "
                << "'" << event << "'" << ", "
                << "'" << action << "'" << " );" ;

    rc = POWER_MONITOR_ctx->exec_cmd_db(db_handle, val_stream.str(), NULL, 0);
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

static int count_lowpower_wakeups_cb(void *state_vec, int argc, char **argv, char **azColName)
{
    db_state_info_t node;
    vector < db_state_info_t >* temp_state_vec = (vector< db_state_info_t >*) state_vec;

    //LOG_I(TAG, "inside count_lowpower_wakeups_cb argc %d", argc);
    if( argc != 6 )
    {
        LOG_E(TAG, "Something wrong here in count_lowpower_wakeups_cb argc %d", argc);
        return -1;
    }
    
    string_to_int64(argv[0], node.index);
    string_to_int64(argv[1], node.boot_time);
    string_to_integer(argv[2], node.pid_num);
    string_to_int64(argv[3], node.event_time);
    node.event = argv[4];
    node.action = argv[5];

    temp_state_vec->push_back(node);
    return 0;
}

bool add_or_update_event_db_event_time(power_dbstate_enum_t event_code, int64_t event_time) {
    stringstream query;
    db_state_info_t crank_high_data_node;
    get_db_node_event(power_dbstate_enum_t::toString(power_dbstate_enum_t::DBSTATE_CRANKHIGH), &crank_high_data_node, POWER_MONITOR_ctx->db_handle);
    db_state_info_t dhub_connected_data_node;
    get_db_node_event(power_dbstate_enum_t::toString(power_dbstate_enum_t::DBSTATE_DHUB_LAST_CONNECTED_TIME), &dhub_connected_data_node, POWER_MONITOR_ctx->db_handle);
    if(crank_high_data_node.index < dhub_connected_data_node.index) {
        query << "UPDATE POWERSTATES SET EVENTTIME = " << event_time << " WHERE INDEXID = " << dhub_connected_data_node.index;
        int rc = POWER_MONITOR_ctx->exec_cmd_db(POWER_MONITOR_ctx->db_handle, query.str(), NULL, 0);
        if(rc == false){
            // Notify health mon
            std::stringstream str_msg;
            str_msg << "failed to execute exec_cmd_db ";
            LOG_E(TAG, str_msg.str().c_str() );
            nd_service_obj->send_err_msg(SM_E_PM_DB_ADD_EVENT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg.str() );
            return false;
        }
    } else {
        if(false == add_event_db(POWER_MONITOR_ctx->db_handle, event_time, POWER_MONITOR_ctx->boot_time,
                    POWER_MONITOR_ctx->pid_num, power_dbstate_enum_t::toString(power_dbstate_enum_t::DBSTATE_DHUB_LAST_CONNECTED_TIME), "WAKEUP")){
            LOG_E(TAG, "Failed to add dhub connected time to db");
            return false;}
    }
    return true;
}

static int fill_node(void *state_vec, int argc, char **argv, char **azColName){

    db_state_info_t* node = (db_state_info_t *)state_vec;
    if( argc != 6 )
    {
        node->index = 0;
        node->boot_time = 0;
        node->pid_num = 0;
        node->event_time = 0;
        node->event = "";
        node->action = "";
        LOG_E(TAG, "Something wrong here in count_lowpower_wakeups_cb argc %d", argc);
        return -1;
    }

    string_to_int64(argv[0], node->index);
    string_to_int64(argv[1], node->boot_time);
    string_to_integer(argv[2], node->pid_num);
    string_to_int64(argv[3], node->event_time);
    node->event = argv[4];
    node->action = argv[5];

    return 0;
}

int support_extended_wakeups(){
    db_state_info_t data_node_high, data_node_low;
    data_node_high.event_time = 0;
    data_node_low.event_time = 0;
    int64_t prev_crank_low_time;

    get_db_node_event(power_dbstate_enum_t::toString(power_dbstate_enum_t::DBSTATE_CRANKHIGH), &data_node_high, POWER_MONITOR_ctx->db_handle);
    if( data_node_high.event_time == 0 ){
        LOG_E(TAG, "failed in get_db_node_event; returning with default crank_shutdown_duration %d", 
            POWER_MONITOR_ctx->lowpower_wakeup_duration);
        return POWER_MONITOR_ctx->lowpower_wakeup_duration;
    }
    int rc;
    std::stringstream val_stream;
    val_stream << "SELECT * from POWERSTATES WHERE EVENT == \'DBSTATE_CRANKLOW\' AND INDEXID > " << data_node_high.index << " ORDER BY INDEXID ASC LIMIT 1";
        rc = POWER_MONITOR_ctx->exec_cmd_db(POWER_MONITOR_ctx->db_handle, val_stream.str(), fill_node, (void*)&data_node_low);
    if(rc == false){
        // Notify health mon
        std::stringstream str_msg;
        str_msg << "failed to execute exec_cmd_db ";
        LOG_E(TAG, str_msg.str().c_str() );
        nd_service_obj->send_err_msg(SM_E_PM_DB_EXECUTE_EVENT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg.str() );
    }

    if(data_node_low.event_time == 0) {
        prev_crank_low_time = get_system_time();
    }
    else {
        prev_crank_low_time = data_node_low.event_time;
    }

    int present_wakeup_time = int((int64_t)POWER_MONITOR_ctx->crank_shutdown_duration -
                                (get_system_time() - prev_crank_low_time)/1000 );
    LOG_I(TAG, "get_system_time %lld prev_crank_low_time %lld present_wakeup_time %lld", 
            get_system_time(), prev_crank_low_time, present_wakeup_time);

    present_wakeup_time = ND_MIN(present_wakeup_time, POWER_MONITOR_ctx->crank_shutdown_duration);
    present_wakeup_time = ND_MAX(present_wakeup_time, POWER_MONITOR_ctx->lowpower_wakeup_duration);
    LOG_I(TAG, "setting low power shutdown to %d time as we need to support extended low power wakeups", 
                    present_wakeup_time);
    return present_wakeup_time;
}

bool get_db_node_action(string action, db_state_info_t* data_node, db_handle_t *db_handle) {

    std::stringstream val_stream;
    // query to get the last shutdown event excluding DBSTATE_SHUTDOWN_BADVOLTAGE
    // normal_shut ----- reboot --------- badvoltage_shut
    // In above case, we consider normal_shut as last shutdown event
    val_stream << "SELECT * from POWERSTATES WHERE ACTION == \'" << action << "\' AND EVENT != \'DBSTATE_SHUTDOWN_BADVOLTAGE\' ORDER BY INDEXID DESC LIMIT 1";

    if(false == POWER_MONITOR_ctx->exec_cmd_db(db_handle, val_stream.str(), fill_node, (void*)data_node)) {
        // Notify health mon
        std::stringstream str_msg;
        str_msg << "failed to execute exec_cmd_db ";
        LOG_E(TAG, str_msg.str().c_str() );
        nd_service_obj->send_err_msg(SM_E_PM_DB_EXECUTE_EVENT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg.str() );
        return false;
    }
    return true;
}

bool get_db_node_event(string event, db_state_info_t* data_node, db_handle_t *db_handle) {
    int rc;
    std::stringstream val_stream;
    val_stream << "SELECT * from POWERSTATES WHERE EVENT == \'" << event << "\' ORDER BY INDEXID DESC LIMIT 1";
        rc = POWER_MONITOR_ctx->exec_cmd_db(db_handle, val_stream.str(), fill_node, (void*)data_node);
    if(rc == false){
        // Notify health mon
        std::stringstream str_msg;
        str_msg << "failed to execute exec_cmd_db ";
        LOG_E(TAG, str_msg.str().c_str() );
        nd_service_obj->send_err_msg(SM_E_PM_DB_EXECUTE_EVENT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg.str() );
        return false;
    }
    return true;
}

void count_lpw() {

    std::stringstream val_stream;
    vector <db_state_info_t> state_vec;
    int lpw_count = 0;

    do {
        val_stream << SELECT_POWERSTATES_EVENT_EQ("DBSTATE_CRANKHIGH") << ORDER_BY_INDEXID_DESC_LIMIT_1;
        if(false == POWER_MONITOR_ctx->exec_cmd_db(POWER_MONITOR_ctx->db_handle, val_stream.str(), count_lowpower_wakeups_cb, (void*)&state_vec)) {
            // Notify health mon
            std::stringstream str_msg;
            str_msg << "failed to execute exec_cmd_db for DBSTATE_CRANKHIGH";
            LOG_E(TAG, str_msg.str().c_str() );
            nd_service_obj->send_err_msg(SM_E_PM_DB_EXECUTE_EVENT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg.str() );
            break;
        }
        LOG_I(TAG, "state_vec.size %d", state_vec.size());
        if(state_vec.size() == 0) {
            break;
        }
        LOG_I(TAG, "state_vec.action %s state_vec.pid_num %d state.index %d",
                        state_vec[0].action.c_str(), state_vec[0].pid_num, state_vec[0].index);

        state_vec.clear();
        LOG_I(TAG, "state_vec.size %d after clearing", state_vec.size());

        // Check For DBSTATE_SHUTDOWN_CRANKOFF
        val_stream.str("");
        val_stream << SELECT_POWERSTATES_EVENT_EQ("DBSTATE_LOWPOWER_WAKEUP") << " AND INDEXID > " << state_vec[0].index;
        if (false == POWER_MONITOR_ctx->exec_cmd_db(POWER_MONITOR_ctx->db_handle, val_stream.str(), count_lowpower_wakeups_cb, (void*)&state_vec)) {
            // Notify health mon
            std::stringstream str_msg;
            str_msg << "failed to execute exec_cmd_db for DBSTATE_LOWPOWER_WAKEUP";
            LOG_E(TAG, str_msg.str().c_str() );
            nd_service_obj->send_err_msg(SM_E_PM_DB_EXECUTE_EVENT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg.str() );
            break;
        }
        lpw_count = state_vec.size();
    }while(false);

    LOG_I(TAG, "DBSTATE_LOWPOWER_WAKEUP: %d", lpw_count);
}

int count_lowpower_wakeups(db_handle_t *db_handle) {

    std::stringstream val_stream;
    vector <db_state_info_t> state_vec;
    int lowpower_wakeup_count = 0;


    do {
        val_stream << SELECT_POWERSTATES_EVENT_EQ("DBSTATE_CRANKHIGH") << ORDER_BY_INDEXID_DESC_LIMIT_1;
        if(false == POWER_MONITOR_ctx->exec_cmd_db(db_handle, val_stream.str(), count_lowpower_wakeups_cb, (void*)&state_vec)) {
            // Notify health mon
            std::stringstream str_msg;
            str_msg << "failed to execute exec_cmd_db for DBSTATE_CRANKHIGH";
            LOG_E(TAG, str_msg.str().c_str() );
            nd_service_obj->send_err_msg(SM_E_PM_DB_EXECUTE_EVENT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg.str() );
            break;
        }
        LOG_I(TAG, "state_vec.size %d", state_vec.size());
        if(state_vec.size() == 0) {
            break;
        }
        LOG_I(TAG, "state_vec.action %s state_vec.pid_num %d state.index %d",
                        state_vec[0].action.c_str(), state_vec[0].pid_num, state_vec[0].index);

        state_vec.clear();
        LOG_I(TAG, "state_vec.size %d after clearing", state_vec.size());

        // Check For DBSTATE_SHUTDOWN_CRANKOFF
        val_stream.str("");
        val_stream << SELECT_POWERSTATES_EVENT_EQ("DBSTATE_SHUTDOWN_CRANKOFF") << " AND INDEXID > " << state_vec[0].index;
        if (false == POWER_MONITOR_ctx->exec_cmd_db(db_handle, val_stream.str(), count_lowpower_wakeups_cb, (void*)&state_vec)) {
            // Notify health mon
            std::stringstream str_msg;
            str_msg << "failed to execute exec_cmd_db for DBSTATE_SHUTDOWN_CRANKOFF";
            LOG_E(TAG, str_msg.str().c_str() );
            nd_service_obj->send_err_msg(SM_E_PM_DB_EXECUTE_EVENT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg.str() );
            break;
        }

        LOG_I(TAG, "state_vec.size %d", state_vec.size());
        if(0 == state_vec.size()) {
            break;
        }

        lowpower_wakeup_count = state_vec.size();
        LOG_I(TAG, "state_vec.action %s state_vec.pid_num %d state.index %d",
                        state_vec[0].action.c_str(), state_vec[0].pid_num, state_vec[0].index);

        state_vec.clear();
        LOG_I(TAG, "state_vec.size %d after clearing", state_vec.size());

        val_stream.str("");
        val_stream << SELECT_POWERSTATES_EVENT_EQ("DBSTATE_SHUTDOWN_LOWPOWER") << " AND INDEXID > " << state_vec[0].index;
        if(false == POWER_MONITOR_ctx->exec_cmd_db(db_handle, val_stream.str(), count_lowpower_wakeups_cb, (void*)&state_vec)) {
            // Notify health mon
            std::stringstream str_msg;
            str_msg << "failed to execute exec_cmd_db for DBSTATE_SHUTDOWN_LOWPOWER";
            LOG_E(TAG, str_msg.str().c_str() );
            nd_service_obj->send_err_msg(SM_E_PM_DB_EXECUTE_EVENT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg.str() );
            break;
        }

        lowpower_wakeup_count += state_vec.size();

        LOG_I(TAG, "state_vec.size %d", state_vec.size());
        if(state_vec.size() == 0) {
            break;
        }
        LOG_I(TAG, "state_vec.action %s state_vec.pid_num %d state.index %d",
                        state_vec[0].action.c_str(), state_vec[0].pid_num, state_vec[0].index);

    } while(false);

    LOG_I(TAG, "lowpower_wakeup_count %d", lowpower_wakeup_count);
    count_lpw();

    // Initialize POWER_MONITOR_ctx->lowpower_wakeups with lowpower_wakeup_count
    POWER_MONITOR_ctx->lowpower_wakeups = lowpower_wakeup_count;

    // after verification if lowpower_wakeup_count is greater than to max_lowpower_wakeups(As per config)
    // In condition where device wake up before max low power wakeups time to handle it as misc handle it in verify_misc_lowpower_wakeup update low power count accordingly
    // Then Mark it as misc_lowpower_wakeup
    // So now Device will go for misc shutdown
    if(lowpower_wakeup_count > POWER_MONITOR_ctx->max_lowpower_wakeups) {
        LOG_C(TAG, "Max lowpower wakeups reached, setting misc_lowpower_wakeup to true");
        POWER_MONITOR_ctx->misc_lowpower_wakeup = true;
        return POWER_MONITOR_ctx->max_lowpower_wakeups;
    }

    // verify misc lowpower wakeup
    verify_misc_lowpower_wakeup(lowpower_wakeup_count);

    return lowpower_wakeup_count;
}

int count_misc_wakeups(db_handle_t *db_handle) {

    LOG_I(TAG,"Entered count_misc_wakeups");
    std::stringstream val_stream;
    vector <db_state_info_t> state_vec;
    int base_index = -1;
    int misc_wakeup_count = 0;

    do {
        val_stream.str("");
        state_vec.clear();
        //Most recent DBSTATE_CRANKHIGH
        val_stream << SELECT_POWERSTATES_EVENT_EQ("DBSTATE_CRANKHIGH") << ORDER_BY_INDEXID_DESC_LIMIT_1;
        if(false == POWER_MONITOR_ctx->exec_cmd_db(db_handle, val_stream.str(), count_lowpower_wakeups_cb, (void*)&state_vec)) {
            // Notify health mon
            std::stringstream str_msg;
            str_msg << "failed to execute exec_cmd_db for DBSTATE_CRANKHIGH";
            LOG_E(TAG, str_msg.str().c_str() );
            nd_service_obj->send_err_msg(SM_E_PM_DB_EXECUTE_EVENT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg.str() );
            break;
        }
        LOG_I(TAG, "DBSTATE_CRANKHIGH : state_vec.size %d", state_vec.size());
        if(state_vec.size() == 0) {//If there is no crankhigh, there is no reset of misc wakeup count, it remains the same
            break;
        }
        base_index = state_vec[0].index;
        LOG_I(TAG, "DBSTATE_CRANKHIGH : state_vec.action %s state_vec.pid_num %d state.index %d", state_vec[0].action.c_str(), state_vec[0].pid_num, state_vec[0].index);

        //Most recent DBSTATE_SHUTDOWN_CRANKOFF after latest CRANKHIGH (if exists)
        val_stream.str("");
        state_vec.clear();
        val_stream << SELECT_POWERSTATES_EVENT_EQ("DBSTATE_SHUTDOWN_CRANKOFF") << " AND INDEXID > " << base_index << ORDER_BY_INDEXID_DESC_LIMIT_1;
        if(false == POWER_MONITOR_ctx->exec_cmd_db(db_handle, val_stream.str(), count_lowpower_wakeups_cb, (void*)&state_vec)) {
            // Notify health mon
            std::stringstream str_msg;
            str_msg << "failed to execute exec_cmd_db for DBSTATE_SHUTDOWN_CRANKOFF";
            LOG_E(TAG, str_msg.str().c_str() );
            nd_service_obj->send_err_msg(SM_E_PM_DB_EXECUTE_EVENT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg.str() );
            break;
        }
        LOG_I(TAG, "DBSTATE_SHUTDOWN_CRANKOFF : state_vec.size %d", state_vec.size());
        if(false == state_vec.empty()) {
            base_index = state_vec[0].index;
        }

        //Most recent DBSTATE_SHUTDOWN_LOWPOWER after latest DBSTATE_SHUTDOWN_CRANKOFF (if exists) else after latest CRANKHIGH
        val_stream.str("");
        state_vec.clear();
        val_stream << SELECT_POWERSTATES_EVENT_EQ("DBSTATE_SHUTDOWN_LOWPOWER") << " AND INDEXID > " << base_index << ORDER_BY_INDEXID_DESC_LIMIT_1;
        if(false == POWER_MONITOR_ctx->exec_cmd_db(db_handle, val_stream.str(), count_lowpower_wakeups_cb, (void*)&state_vec)) {
            // Notify health mon
            std::stringstream str_msg;
            str_msg << "failed to execute exec_cmd_db for DBSTATE_SHUTDOWN_LOWPOWER";
            LOG_E(TAG, str_msg.str().c_str() );
            nd_service_obj->send_err_msg(SM_E_PM_DB_EXECUTE_EVENT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg.str() );
            break;
        }
        LOG_I(TAG, "DBSTATE_SHUTDOWN_LOWPOWER : state_vec.size %d", state_vec.size());
        if(false == state_vec.empty()) {
            base_index = state_vec[0].index;
        }

        //All DBSTATE_MISC_LOWPOWER_WAKEUP after most recent DBSTATE_SHUTDOWN_LOWPOWER / DBSTATE_SHUTDOWN_CRANKOFF if DBSTATE_CRANKHIGH exists
        val_stream.str("");
        state_vec.clear();
        val_stream << SELECT_POWERSTATES_EVENT_EQ("DBSTATE_MISC_LOWPOWER_WAKEUP") << " AND INDEXID > " << base_index;
        if(false == POWER_MONITOR_ctx->exec_cmd_db(db_handle, val_stream.str(), count_lowpower_wakeups_cb, (void*)&state_vec)) {
            // Notify health mon
            std::stringstream str_msg;
            str_msg << "failed to execute exec_cmd_db for DBSTATE_MISC_LOWPOWER_WAKEUP";
            LOG_E(TAG, str_msg.str().c_str() );
            nd_service_obj->send_err_msg(SM_E_PM_DB_EXECUTE_EVENT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg.str() );
            break;
        }
        LOG_I(TAG, "DBSTATE_MISC_LOWPOWER_WAKEUP : state_vec.size %d", state_vec.size());
        misc_wakeup_count = state_vec.size();
    } while(false);

    LOG_I(TAG, "misc LPW wakeup count: %d", misc_wakeup_count);
    return misc_wakeup_count;
}


string previous_shutdown_reason(db_handle_t *db_handle) {
    int rc;
    std::stringstream val_stream;
    vector <db_state_info_t> state_vec;

    val_stream << SELECT_POWERSTATES << ORDER_BY_INDEXID_DESC_LIMIT_1;

    rc = POWER_MONITOR_ctx->exec_cmd_db(db_handle, val_stream.str(), count_lowpower_wakeups_cb, (void*)&state_vec);
    if(rc == false){
        // Notify health mon
        std::stringstream str_msg;
        str_msg << "failed to execute exec_cmd_db";
        LOG_E(TAG, str_msg.str().c_str() );
        nd_service_obj->send_err_msg(SM_E_PM_DB_EXECUTE_EVENT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg.str() );
        return "DBSTATS_EXEC_FAILED";
    }
    LOG_I(TAG, "state_vec.size %d", state_vec.size());
    if(state_vec.size() == 0) {
        return "DBSTATS_EMPTY";
    }

    LOG_C(TAG, "node received : index %lld boot_time %lld pid_num %d event_time %lld event %s actions %s", 
        state_vec[0].index, state_vec[0].boot_time, state_vec[0].pid_num, 
        state_vec[0].event_time, state_vec[0].event.c_str(), state_vec[0].action.c_str() );

    if(state_vec[0].event.find("DBSTATE_SHUTDOWN") != string::npos) {
        return state_vec[0].event + ":" + state_vec[0].action;
    }
    return "NA";
}

bool check_event_in_db(stringstream &val_stream, db_state_info_t &event_node) {

    vector <db_state_info_t> state_vec;

    if (false == POWER_MONITOR_ctx->exec_cmd_db(POWER_MONITOR_ctx->db_handle, val_stream.str(), count_lowpower_wakeups_cb, (void*)&state_vec)) {
        LOG_E(TAG, "failed to execute exec_cmd_db : %s", val_stream.str().c_str());
        return false;
    }

    if(STATE_VEC_SIZE_ZERO == state_vec.size()) {
        LOG_I(TAG, "No event found in power monitor db for query: %s", val_stream.str().c_str());
        return false;
    }

    // store the event node
    event_node = state_vec[0];
    return true;
}

// Below function is used to update the input time if the remain time is less than input time
// To handle time jump issue and to assure that intput time is not negative and not more than config time
bool update_input_time_if_valid(int64_t remain_time_orig, int max_time, int &input_time) {
int64_t remain_time = llabs(remain_time_orig);
    if((remain_time >= 0) && (remain_time <= max_time)) {
        input_time = static_cast<int>(remain_time);
        return true;
    }
    return false;
}

void store_event(vector <db_state_info_t> &state_node, db_state_info_t &event_node, bool &event_found) {
    // store the event node
    if (event_found) {
        state_node.push_back(event_node);
    }
}

void validate_wakeup_time(db_state_info_t &start_node, db_state_info_t &wakeup_time_node, int64_t shutdown_time, int64_t lpw_cycle_duration) {

    int64_t calculated_wakeup_time = start_node.event_time + S_TO_MS(lpw_cycle_duration) + S_TO_MS(shutdown_time);

    int64_t diff = llabs(calculated_wakeup_time - wakeup_time_node.event_time);

    LOG_I(TAG, "config based wakeup_time: %lld, wakeup_time: %lld", calculated_wakeup_time, wakeup_time_node.event_time);

    const unsigned int max_diff_time = 120; // seconds . tolerances of 2 minutes for wakeup time jitter else consider it as time jump or config issue
    if((diff >= 0) && (diff <= S_TO_MS(max_diff_time))) {
        LOG_I(TAG, "Wakeup time is valid, According to the calculated wakeup time based on config values");
    }
    else {

        if(false == POWER_MONITOR_ctx->ext_cam_lpw_enabled) {

            // sending critical info
            string err_msg = "cfg wakeup:" + to_string(calculated_wakeup_time) + " exp:" + to_string(wakeup_time_node.event_time);
            LOG_C(TAG, "%s", err_msg.c_str());
            nd_service_obj->send_err_msg(SM_E_PM_RTC_WAKEUP_TIME, diff, err_msg);

            // Index of the wakeup time node in db which needs to be updated based on time jump or config update
            LOG_C(TAG, "wakeup entry db index: %lld",wakeup_time_node.index);

            // Update last wakeup entry as invalid
            std::stringstream val_stream;
            val_stream << "UPDATE POWERSTATES SET EVENT = \"" << wakeup_time_node.event << "_INVALID\" WHERE INDEXID = " << wakeup_time_node.index;
            std::vector <db_state_info_t> state_vec;

            if (false == POWER_MONITOR_ctx->exec_cmd_db(POWER_MONITOR_ctx->db_handle, val_stream.str(), nullptr, (void*)&state_vec)) {
                LOG_E(TAG, "failed to execute exec_cmd_db : %s", val_stream.str().c_str());
                return;
            }

            // Update new wakeup entry with the calculated wakeup time
            if ( false == add_event_db(POWER_MONITOR_ctx->db_handle, calculated_wakeup_time, POWER_MONITOR_ctx->boot_time,
                POWER_MONITOR_ctx->pid_num, wakeup_time_node.event, "WAKEUP")) {
                LOG_E(TAG, "Failed to add rtc wakeup time for device to db");
                return;
            }

            db_state_info_t new_wakeup_time_node = {};
            val_stream.str("");
            val_stream << SELECT_POWERSTATES_EVENT_EQ(wakeup_time_node.event) << ORDER_BY_INDEXID_DESC_LIMIT_1;
            if (false == check_event_in_db(val_stream, new_wakeup_time_node)) {
                LOG_E(TAG, "Failed to get new wakeup time node from db");
                return;
            }

            // Update the wakeup time node with the new calculated wakeup time db entry
            wakeup_time_node = new_wakeup_time_node;
        }
        else {
            string err_msg = "Exp DHUB Wakeup:" + to_string(wakeup_time_node.event_time);
            LOG_C(TAG, "%s", err_msg.c_str());
            nd_service_obj->send_err_msg(SM_E_PM_RTC_WAKEUP_TIME, diff, err_msg);
        }
    }

}

/* Function to check and handle the record on misc window.
 * This function checks if the last recorded event is DBSTATE_REC_MISC_LOWPOWER_WAKEUP.
 * If it is found, it checks if the event time is between the shutdown time and wakeup time.
 * If it is, it calculates the remaining shutdown time and updates the record_on_misc_duration
 * to the remaining shutdown time if it is greater than 0 and less than or equal to the configured record_on_misc_duration.
 * It also sets the misc_lowpower_reboot flag to true to avoid db entry for the same event again.
 * @param shutdown_time The time when the device is expected to shut down.
 * @param wakeup_time The time when the device is expected to wake up.
 * @param current_time The current system time.
 */
void check_and_handle_misc_window(int64_t shutdown_time, int64_t wakeup_time, int64_t current_time) {

    std::stringstream query;
    query << "SELECT * FROM POWERSTATES WHERE EVENT = 'DBSTATE_SHUTDOWN_MISC_LOWPOWER' "
          << "AND INDEXID > (SELECT MAX(INDEXID) FROM POWERSTATES WHERE EVENT = 'DBSTATE_SHUTDOWN_CRANKOFF') "
          << "ORDER BY INDEXID DESC LIMIT 1";
    db_state_info_t misc_shutdown_node = {}; // last LPW shutdown node
    bool misc_shutdown_found = check_event_in_db(query, misc_shutdown_node);

    query.str("");
    query.clear();
    query << SELECT_POWERSTATES_EVENT_EQ("DBSTATE_MISC_LOWPOWER_WAKEUP") << ORDER_BY_INDEXID_DESC_LIMIT_1;
    db_state_info_t misc_lpw_node = {}; // last Misc LPW node
    bool misc_lpw_found = check_event_in_db(query, misc_lpw_node);

    if((true == misc_shutdown_found) && (misc_lpw_node.event_time < misc_shutdown_node.event_time)) {
        LOG_I(TAG, "Skipping Due to DBSTATE_MISC_LOWPOWER_WAKEUP event time %lld is less than Last DBSTATE_SHUTDOWN_MISC_LOWPOWER event time %lld",
                        misc_lpw_node.event_time, misc_shutdown_node.event_time);
        return;
    }

    int lpw_stat = static_cast<int>(lpw_state_t::eLPW_ON); // For Normal Misc wakeup same as x.6.10.rc.x which LPW ON

    LOG_I(TAG, "misc_lpw_found: %d", misc_lpw_found);
    if((true == misc_lpw_found)) {
        int64_t misc_event_time = convert_epoch_format(misc_lpw_node.event_time, DigitsOfEpoch::eDigits_Seconds);
        LOG_I(TAG, "shutdown_time: %lld, misc_event_time: %lld, wakeup_time: %lld, current_time: %lld",
                        shutdown_time, misc_event_time, wakeup_time, current_time);

        // LPW_SHUT ----------- REC_MISC_LOWPOWER_WAKEUP ----------- LPW_WAKEUP
        // shutdown_time    <     misc_event_time       <        wakeup_time
        if((shutdown_time < misc_event_time) && (wakeup_time > misc_event_time)) {
            int64_t remain_shutdown_time = misc_event_time - current_time;
            LOG_I(TAG, "remain_shutdown_time: %lld", remain_shutdown_time);

            int &wakeup_time = (true == POWER_MONITOR_ctx->non_lpm_crank_low_wakeup) ?
                                  POWER_MONITOR_ctx->non_lpm_crank_low_wakeup_duration :
                                  POWER_MONITOR_ctx->misc_wakeup_duration;

            if((remain_shutdown_time > 0) && (remain_shutdown_time <= wakeup_time)) {
                wakeup_time = remain_shutdown_time;
                POWER_MONITOR_ctx->misc_lowpower_reboot = true;
                lpw_stat = (true == POWER_MONITOR_ctx->non_lpm_crank_low_wakeup) ?
                               static_cast<int>(lpw_state_t::eLPW_OFF) :
                               static_cast<int>(lpw_state_t::eLPW_ON);
            }
        }
    }

    if (true == identify_misc_wakeup_reason(lpw_stat)) { // to set wakeup_reason and update lpw_stat
        lpw_stat = static_cast<int>(lpw_state_t::eLPW_OFF);
    }

    LOG_I(TAG, "Setting lpw_stat sysfs entry to %d", lpw_stat);
    write_into_sysfs_entry(nd_device_obj->get_lpw_stat_sysfs_path(), lpw_stat);
}

void set_device_wakeup_time(db_state_info_t &wakeup_time_node, int64_t lpw_cycle_duration, bool wakeup_before_crank_shutdown_duration = false) {

    // CRANK_LOW -------------- REBOOT ---------- CRANK_OFF_SHUT --------LPW_WAKEUP ----- LPW_SHUT
    //                           (A)
    // CRANK_OFF_SHUT --------- REBOOT ---------- LPW_WAKEUP
    //                           (B)
    // LPW_WAKEUP ------------- REBOOT ---------- LPW_SHUT ------------- LPW_WAKEUP
    //                           (C)
    // LPW_SHUT --------------- REBOOT ---------- LPW_WAKEUP
    //                           (D)

    int64_t current_time = convert_epoch_format(get_system_time(), DigitsOfEpoch::eDigits_Seconds);
    int64_t wakeup_time = convert_epoch_format(wakeup_time_node.event_time, DigitsOfEpoch::eDigits_Seconds);
    int64_t shutdown_time = wakeup_time - lpw_cycle_duration;
    int64_t remain_shutdown_time = shutdown_time - current_time;
    int64_t remain_wakeup_time = wakeup_time - current_time;

    LOG_I(TAG, "current_time: %lld, wakeup_time: %lld, shutdown_time: %lld", current_time, wakeup_time, shutdown_time);

    // (A) and (C)
    if ((current_time < shutdown_time) && ((current_time < wakeup_time) || (true == wakeup_before_crank_shutdown_duration))) {

        // To make sure only one entry in db for DBSTATE_RTC_WAKEUP_TIME_DHUB/DBSTATE_RTC_WAKEUP_TIME_DEVICE
        POWER_MONITOR_ctx->misc_lowpower_reboot = true;
        LOG_I(TAG, "Remain shutdown time: %lld", remain_shutdown_time);

        if ( true == wakeup_before_crank_shutdown_duration) {
            LOG_I(TAG, "Device wakeup before crank_shutdown_duration window");
            if(false == update_input_time_if_valid(remain_shutdown_time, lpw_data.crank_shutdown_duration, POWER_MONITOR_ctx->crank_shutdown_duration)) {
                POWER_MONITOR_ctx->misc_lowpower_reboot = false;
                LOG_C(TAG, "Not updating POWER_MONITOR_ctx->crank_shutdown_duration as remain_shutdown_time is: %lld", remain_shutdown_time);
            }
        }
        else {
            LOG_I(TAG, "Device wakeup before lowpower_wakeup_duration window");
            if(false == update_input_time_if_valid(remain_shutdown_time, lpw_data.lowpower_wakeup_duration, POWER_MONITOR_ctx->lowpower_wakeup_duration)) {
                POWER_MONITOR_ctx->misc_lowpower_reboot = false;
                LOG_C(TAG, "Not updating POWER_MONITOR_ctx->lowpower_wakeup_duration as remain_shutdown_time is: %lld", remain_shutdown_time);
            }
        }
    }
    // (B) and (D)
    else if (current_time < wakeup_time) {

        LOG_I(TAG, "Device wakeup before lowpower_wakeup_cycle_duration window");
        POWER_MONITOR_ctx->misc_lowpower_wakeup = true;

        if((true == POWER_MONITOR_ctx->ext_cam_lpw_enabled) && (POWER_MONITOR_ctx->safety_time_to_sync_driveri_dhub > 0)) {
            remain_wakeup_time -= (int64_t)POWER_MONITOR_ctx->safety_time_to_sync_driveri_dhub;
        }

        if((remain_wakeup_time >= 0) && (remain_wakeup_time <= lpw_data.lowpower_wakeup_duration)) {
            LOG_I(TAG, "Skipping shutdown for misc wakeup, as next wakeup is within lowpower_wakeup_duration, remain_wakeup_time: %lld", remain_wakeup_time);
            POWER_MONITOR_ctx->misc_lowpower_wakeup = false;
            POWER_MONITOR_ctx->extend_wakeup_duration_for_misc = true;
            POWER_MONITOR_ctx->last_wakeup_time = wakeup_time_node.event_time;
            POWER_MONITOR_ctx->lowpower_wakeup_duration += static_cast<int>(remain_wakeup_time);
            return;
        }

        check_and_handle_misc_window(shutdown_time, wakeup_time, current_time);
        LOG_I(TAG, "Remain wakeup time: %lld", remain_wakeup_time);
    }
    else {
        int64_t time_diff = current_time - wakeup_time;
        int64_t missed_wakeup_count = 0;

        if(time_diff > 0) {
            // In case off abrupt shutdown, if device wakeup after max lowpower_wakeups
            // then consider it as misc lowpower wakeup
            if (POWER_MONITOR_ctx->lowpower_wakeups >= POWER_MONITOR_ctx->max_lowpower_wakeups) {
                POWER_MONITOR_ctx->misc_lowpower_wakeup = true;
            }
            else {
                // calculated missed lowpower wakeups
                // freq low power wakeup
                // long low power wakeup
                // normal low power wakeup
                if (true == POWER_MONITOR_ctx->freq_low_power_wakeup) {
                    int64_t cycle_duration = get_lpw_cycle_duration();
                    if (cycle_duration > 0) {
                        missed_wakeup_count = time_diff / cycle_duration;
                    }
                }
                else {
                    int64_t l_cycle_duration = POWER_MONITOR_ctx->lowpower_wakeup_cycle_duration + POWER_MONITOR_ctx->lowpower_wakeup_duration;
                    int64_t l_long_cycle_duration = POWER_MONITOR_ctx->lowpower_wakeup_long_cycle_duration + POWER_MONITOR_ctx->lowpower_wakeup_duration;
                    // total time for 32 wakeups
                    int64_t total_time_cycle_duration_wakeup = (POWER_MONITOR_ctx->lowpower_wakeup_long_cycle_threshold * l_cycle_duration);

                    // time_diff is less than total_time_cycle_duration_wakeup
                    // means device wakeup before lowpower_wakeup_long_cycle_threshold value
                    // In this case we need to check for missed based on l_cycle_duration(lowpower_wakeup_cycle_duration)
                    // So that we can get the missed wakeup count
                    if (time_diff < total_time_cycle_duration_wakeup) {
                        if (l_cycle_duration > 0) {
                            missed_wakeup_count = time_diff / l_cycle_duration;
                        }
                    }
                    else {
                        // time_diff is greater than total_time_cycle_duration_wakeup
                        // means device wakeup after lowpower_wakeup_long_cycle_threshold value
                        // In this case we need to check for missed based on l_long_cycle_duration(lowpower_wakeup_long_cycle_duration)
                        // And we need to add lowpower_wakeup_long_cycle_threshold value
                        // So that we can get the missed wakeup count
                        int64_t remaining_time = time_diff - total_time_cycle_duration_wakeup;
                        if (l_long_cycle_duration > 0) {
                            missed_wakeup_count = (remaining_time / l_long_cycle_duration) + POWER_MONITOR_ctx->lowpower_wakeup_long_cycle_threshold;
                        }
                    }
                }
            }
        }
        // send critical info for this case
        string err_msg = "diff current & wakeup time:" + to_string(time_diff) + " missed_lpw_count:" + to_string(missed_wakeup_count);
        LOG_C(TAG, "%s", err_msg.c_str());
        nd_service_obj->send_err_msg(SM_E_PM_RTC_WAKEUP_TIME, time_diff, err_msg);
    }
}

int64_t get_low_power_wakeup_cycle_duration(int64_t wakeup_node_index, int64_t lpw_shutdown_node_index, int64_t crank_shutdown_node_index) {

    int64_t cycle_duration = 0;

    if((true == POWER_MONITOR_ctx->freq_low_power_wakeup)) {
        cycle_duration = (int64_t)get_lpw_cycle_duration();
    }
    else if(POWER_MONITOR_ctx->lowpower_wakeups == POWER_MONITOR_ctx->lowpower_wakeup_long_cycle_threshold) {

        LOG_I(TAG, "wakeup_node_index %lld lpw_shutdown_node_index %lld crank_shutdown_node_index %lld",
                wakeup_node_index, lpw_shutdown_node_index, crank_shutdown_node_index);

        if((wakeup_node_index < lpw_shutdown_node_index) || (wakeup_node_index < crank_shutdown_node_index)) {
            // If device reach to lowpower_wakeup_long_cycle_threshold
            // And device wakeup for actual lowpower wakeup at this time
            // Or device wakeup wakeup before this time
            // Then lowpower_wakeups count will remain same as threshold
            // And wakeup_node_index will be less than lpw_shutdown_node_index or crank_shutdown_node_index.
            // As we are storing wakeup time in db before shutdown time
            // So, we need to set cycle_duration to lowpower_wakeup_cycle_duration for validation
            cycle_duration = (int64_t)POWER_MONITOR_ctx->lowpower_wakeup_cycle_duration;
        }
        else {
            // If device wakeup at actual lowpower_wakeup_long_cycle_threshold time
            // And stay up for some time then new wakeup entry will be added in db according to lowpower_wakeup_long_cycle_duration
            // So, In this case wakeup_node_index will be greater than lpw_shutdown_node_index or crank_shutdown_node_index.
            // Then we need to set cycle_duration to lowpower_wakeup_long_cycle_duration for validation
            cycle_duration = (int64_t)POWER_MONITOR_ctx->lowpower_wakeup_long_cycle_duration;
        }
    }
    else {
        cycle_duration = (int64_t)get_lpw_cycle_duration();
    }

    LOG_I(TAG, "%s: %lld", __func__, cycle_duration);
    return cycle_duration;
}

bool verify_misc_lowpower_wakeup(int &lowpower_wakeup_count) {

    //Check if device is in CRANK_HIGH state or WAKEonIGNITION is set, If yes, return false as
    {
        power_crank_levels_t crank_level = POWER_MONITOR_ctx->crank_level();
        // If device is in CRANK_HIGH state and wakeup due to ignition toggled, then no need to check for misc lowpower wakeups
        if ((power_crank_levels_t::CRANK_HIGH == crank_level) ^ (POWER_MONITOR_ctx->power_on_off_reason & (1 << PowerOnTriggerT::WAKEonIGNITION))) {
            LOG_C(TAG, "Crank level(%d) XOR Power on off reason(%d) is true, Glitch detected", crank_level, POWER_MONITOR_ctx->power_on_off_reason);
        }

        if((power_crank_levels_t::CRANK_HIGH == crank_level)) {
            LOG_I(TAG, "Device is in CRANK_HIGH state, no need to check for misc lowpower wakeups");
            return false;
        }
    }

    std::stringstream val_stream;
    vector <db_state_info_t> state_vec;

    // Check Last Crank Off Event In Power Monitor DB
    val_stream << SELECT_POWERSTATES_EVENT_EQ("DBSTATE_SHUTDOWN_CRANKOFF") << ORDER_BY_INDEXID_DESC_LIMIT_1;
    db_state_info_t shutdown_crank_off_node = {};
    bool shutdown_crank_off_event_found = check_event_in_db(val_stream, shutdown_crank_off_node);

    // Check Last Crank High Event In Power Monitor DB
    val_stream.str("");
    val_stream << SELECT_POWERSTATES_EVENT_EQ("DBSTATE_CRANKHIGH") << ORDER_BY_INDEXID_DESC_LIMIT_1;
    db_state_info_t shutdown_crank_high_node = {};
    bool shutdown_crank_high_event_found = check_event_in_db(val_stream, shutdown_crank_high_node);

    // Check Last Low Power Wakeup Event In Power Monitor DB
    val_stream.str("");
    val_stream << SELECT_POWERSTATES_EVENT_EQ("DBSTATE_SHUTDOWN_LOWPOWER") << ORDER_BY_INDEXID_DESC_LIMIT_1;
    db_state_info_t shutdown_lowpower_node = {};
    bool shutdown_lowpower_event_found = check_event_in_db(val_stream, shutdown_lowpower_node);

    // Check Last DBSTATE_RTC_WAKEUP_TIME_DHUB Event In Power Monitor DB
    val_stream.str("");
    val_stream << SELECT_POWERSTATES_EVENT_EQ("DBSTATE_RTC_WAKEUP_TIME_DHUB") << ORDER_BY_INDEXID_DESC_LIMIT_1;
    db_state_info_t dhub_wakeup_time_node = {};
    bool dhub_wakeup_time_event_found = check_event_in_db(val_stream, dhub_wakeup_time_node);

    // check Last DBSTATE_RTC_WAKEUP_TIME_DEVICE Event In Power Monitor DB
    val_stream.str("");
    val_stream << SELECT_POWERSTATES_EVENT_EQ("DBSTATE_RTC_WAKEUP_TIME_DEVICE") << ORDER_BY_INDEXID_DESC_LIMIT_1;
    db_state_info_t device_wakeup_time_node = {};
    bool device_wakeup_time_event_found = check_event_in_db(val_stream, device_wakeup_time_node);

    // check Last DBSTATE_LOWPOWER_WAKEUP Event In Power Monitor DB
    val_stream.str("");
    val_stream << SELECT_POWERSTATES_EVENT_EQ("DBSTATE_LOWPOWER_WAKEUP") << ORDER_BY_INDEXID_DESC_LIMIT_1;
    db_state_info_t lowpower_wakeup_node = {};
    bool lowpower_wakeup_event_found = check_event_in_db(val_stream, lowpower_wakeup_node);

    // checkLast DBSTATE_IGNITION_GPIO_LOW Event In Power Monitor DB
    val_stream.str("");
    val_stream << SELECT_POWERSTATES_EVENT_EQ("DBSTATE_IGNITION_GPIO_LOW") << " AND INDEXID > " << shutdown_crank_high_node.index << ORDER_BY_INDEXID_ASC_LIMIT_1;
    db_state_info_t ignition_gpio_low_node = {};
    bool ignition_gpio_low_event_found = check_event_in_db(val_stream, ignition_gpio_low_node);

    // Check DBSTATE_CRANKLOW Event after last DBSTATE_CRANKHIGH Event In Power Monitor DB
    val_stream.str("");
    val_stream << SELECT_POWERSTATES_EVENT_EQ("DBSTATE_CRANKLOW") << " AND INDEXID > " << shutdown_crank_high_node.index << ORDER_BY_INDEXID_ASC_LIMIT_1;
    db_state_info_t crank_low_node = {};
    bool crank_low_event_found = check_event_in_db(val_stream, crank_low_node);

    // use state_vec to store all the events
    state_vec.clear();
    store_event(state_vec, dhub_wakeup_time_node, dhub_wakeup_time_event_found);
    store_event(state_vec, shutdown_crank_off_node, shutdown_crank_off_event_found);
    store_event(state_vec, shutdown_crank_high_node, shutdown_crank_high_event_found);
    store_event(state_vec, shutdown_lowpower_node, shutdown_lowpower_event_found);
    store_event(state_vec, device_wakeup_time_node, device_wakeup_time_event_found);
    store_event(state_vec, lowpower_wakeup_node, lowpower_wakeup_event_found);
    store_event(state_vec, ignition_gpio_low_node, ignition_gpio_low_event_found);
    store_event(state_vec, crank_low_node, crank_low_event_found);

    LOG_I(TAG, "state_vec.size %d", state_vec.size());

    // sorting the events based on indexid in descending order
    // So, that we can find the DBSTATE_CRANKHIGH event and remove all the events after that to get the latest events
    LOG_I(TAG, "Sorting the events based on indexid in descending order");
    std::sort(state_vec.begin(), state_vec.end(), [](const db_state_info_t &a, const db_state_info_t &b) {
        return a.index > b.index;
    });

    // find DBSTATE_CRANKHIGH event
    auto it = std::find_if(state_vec.begin(), state_vec.end(), [](const db_state_info_t &node) {
        return "DBSTATE_CRANKHIGH" == node.event;
    });

    std::unordered_map<string, bool*> event_found_map = {
        {power_dbstate_enum_t::toString(power_dbstate_enum_t::DBSTATE_SHUTDOWN_CRANKOFF), &shutdown_crank_off_event_found},
        {power_dbstate_enum_t::toString(power_dbstate_enum_t::DBSTATE_SHUTDOWN_LOWPOWER), &shutdown_lowpower_event_found},
        {power_dbstate_enum_t::toString(power_dbstate_enum_t::DBSTATE_RTC_WAKEUP_TIME_DHUB), &dhub_wakeup_time_event_found},
        {power_dbstate_enum_t::toString(power_dbstate_enum_t::DBSTATE_RTC_WAKEUP_TIME_DEVICE), &device_wakeup_time_event_found},
        {power_dbstate_enum_t::toString(power_dbstate_enum_t::DBSTATE_LOWPOWER_WAKEUP), &lowpower_wakeup_event_found},
        {power_dbstate_enum_t::toString(power_dbstate_enum_t::DBSTATE_IGNITION_GPIO_LOW), &ignition_gpio_low_event_found},
        {power_dbstate_enum_t::toString(power_dbstate_enum_t::DBSTATE_CRANKLOW), &crank_low_event_found}
    };

    // remove all the events after DBSTATE_CRANKHIGH event and update the event_found flags
    if (it != state_vec.end()) {
        for (auto after_it = std::next(it); after_it != state_vec.end(); ) {
            // store the event name
            auto event = after_it->event;
            // find the event in event_found_map
            auto event_iter = event_found_map.find(event);
            if (event_iter != event_found_map.end()) {
                LOG_I(TAG, "Removing %s event from state_vec, Because it is older than DBSTATE_CRANKHIGH event", event.c_str());
                // updating the event_found flag to false as the event is older than DBSTATE_CRANKHIGH event
                *(event_iter->second) = false;
            }
            // remove the event from state_vec
            after_it = state_vec.erase(after_it);
        }
    }
    POWER_MONITOR_ctx->dhub_wakeup_time_event_found = dhub_wakeup_time_event_found;
    if(true == dhub_wakeup_time_event_found) {
        POWER_MONITOR_ctx->last_dhub_wakeup_time = dhub_wakeup_time_node.event_time;
    }

    if ((false == POWER_MONITOR_ctx->freq_low_power_wakeup) &&
        (true == POWER_MONITOR_ctx->ext_cam_lpw_enabled) &&
        (lowpower_wakeup_count >= POWER_MONITOR_ctx->lowpower_wakeup_long_cycle_threshold)) {

        LOG_I(TAG, "Device is in extended lowpower wakeup state");
        float possible_lpw = (float(POWER_MONITOR_ctx->lowpower_wakeup_long_cycle_duration + POWER_MONITOR_ctx->lowpower_wakeup_duration) / (POWER_MONITOR_ctx->lowpower_wakeup_cycle_duration + POWER_MONITOR_ctx->lowpower_wakeup_duration));
        POWER_MONITOR_ctx->possible_lpw_count = ceil(possible_lpw);
        if(POWER_MONITOR_ctx->possible_lpw_count > 0){
            POWER_MONITOR_ctx->lowpower_wakeup_long_cycle_duration = (POWER_MONITOR_ctx->lowpower_wakeup_cycle_duration * POWER_MONITOR_ctx->possible_lpw_count) + ((POWER_MONITOR_ctx->possible_lpw_count - 1) * POWER_MONITOR_ctx->lowpower_wakeup_duration);
        }
        LOG_I(TAG, "Device will sync up with DHUB at %d LPW cycle", POWER_MONITOR_ctx->possible_lpw_count);
        LOG_I(TAG, "POWER_MONITOR_ctx->lowpower_wakeup_long_cycle_duration: %d", POWER_MONITOR_ctx->lowpower_wakeup_long_cycle_duration);
    }

    //print final status of all event_found flags
    LOG_I(TAG, "shutdown_lowpower_event_found: %d", shutdown_lowpower_event_found);
    LOG_I(TAG, "shutdown_crank_off_event_found: %d", shutdown_crank_off_event_found);
    LOG_I(TAG, "shutdown_crank_high_event_found: %d", shutdown_crank_high_event_found);
    LOG_I(TAG, "dhub_wakeup_time_event_found: %d", dhub_wakeup_time_event_found);
    LOG_I(TAG, "device_wakeup_time_event_found: %d", device_wakeup_time_event_found);
    LOG_I(TAG, "lowpower_wakeup_event_found: %d", lowpower_wakeup_event_found);
    LOG_I(TAG, "ignition_gpio_low_event_found: %d", ignition_gpio_low_event_found);
    LOG_I(TAG, "crank_low_event_found: %d", crank_low_event_found);

    // print all the events in state_vec
    for (auto &node : state_vec) {
        LOG_I(TAG, "node received : index %lld boot_time %lld pid_num %d event_time %lld event %s actions %s",
            node.index, node.boot_time, node.pid_num, node.event_time, node.event.c_str(), node.action.c_str());
    }

    // get last crank_low_event
    val_stream.str("");
    val_stream << SELECT_POWERSTATES_EVENT_EQ("DBSTATE_CRANKLOW") << " AND INDEXID > " << shutdown_crank_high_node.index << ORDER_BY_INDEXID_DESC_LIMIT_1;
    db_state_info_t last_crank_low_node = {};
    bool last_crank_low_event_found = check_event_in_db(val_stream, last_crank_low_node);

    // crank_low_event_found is to indicate first crank low event after last crank high
    // last_crank_low_event_found is to indicate last crank low event after last crank high
    // If mismatch between crank_low_event_found and last_crank_low_event_found send critical info
    if (crank_low_event_found != last_crank_low_event_found) {
        int aux_code = 0; // 0th bit for crank_low_event_found, 1st bit for last_crank_low_event_found
        aux_code = crank_low_event_found | (last_crank_low_event_found << 1);
        string err_msg = "first crank low: " + to_string(crank_low_event_found) + " last crank low: " + to_string(last_crank_low_event_found);
        LOG_C(TAG, "%s", err_msg.c_str());
        nd_service_obj->send_err_msg(SM_E_PM_DB_EXECUTE_EVENT_FAIL, aux_code, err_msg);
    }

    // validate wakeup time comparing with config values
    if (((true == crank_low_event_found) || (true == lowpower_wakeup_event_found) || (true == ignition_gpio_low_event_found)) &&
        ((true == dhub_wakeup_time_event_found) || (true == device_wakeup_time_event_found))) {

        db_state_info_t start_node = {};
        if (true == lowpower_wakeup_event_found) {
            start_node = lowpower_wakeup_node;
            LOG_I(TAG, "lowpower_wakeup_event found, start_node.event_time: %lld", start_node.event_time);
        }
        else if ((true == ignition_gpio_low_event_found) && (true == POWER_MONITOR_ctx->ext_cam_lpw_enabled)) {
            start_node = ignition_gpio_low_node;
            LOG_I(TAG, "ignition_gpio_low_event found, start_node.event_time: %lld", start_node.event_time);
        }
        else if (true == crank_low_event_found) {
            start_node = crank_low_node;
            LOG_I(TAG, "crank_low_event found, start_node.event_time: %lld", start_node.event_time);
        }
        else {
            LOG_C(TAG, "No valid start node found for wakeup time validation");
        }

        if (start_node.event_time != INVALID_TIMESTAMP) {
            // Taking reference of wakeup time node based on the event found in db dhub_wakeup_time or device_wakeup_time
            // To updated in case of time jump or config update in validate_wakeup_time function
            db_state_info_t &wakeup_time_node = (true == dhub_wakeup_time_event_found) ? dhub_wakeup_time_node : device_wakeup_time_node;
            int64_t shutdown_time = (false == lowpower_wakeup_event_found) ? (int64_t)POWER_MONITOR_ctx->crank_shutdown_duration : (int64_t)POWER_MONITOR_ctx->lowpower_wakeup_duration;

            int64_t lpw_cycle_duration = get_low_power_wakeup_cycle_duration(wakeup_time_node.index, shutdown_lowpower_node.index, shutdown_crank_off_node.index);
            validate_wakeup_time(start_node, wakeup_time_node, shutdown_time, lpw_cycle_duration);
        }
    }

    int64_t current_time = convert_epoch_format(get_system_time(), DigitsOfEpoch::eDigits_Seconds);
    bool is_abrupt_wakeup = false;

    if ((true == ignition_gpio_low_event_found) || (true == last_crank_low_event_found) || (true == lowpower_wakeup_event_found)) {

        int64_t time_diff = 0;
        string source_event = "";
        int64_t source_time = 0;

        if(true == lowpower_wakeup_event_found) {
            source_event = "lowpower_wakeup_event";
            source_time = convert_epoch_format(lowpower_wakeup_node.event_time, DigitsOfEpoch::eDigits_Seconds);
            time_diff = llabs(source_time - current_time);

        }
        else if(true == last_crank_low_event_found) {
            source_event = "last crank_low_event";
            source_time = convert_epoch_format(last_crank_low_node.event_time, DigitsOfEpoch::eDigits_Seconds);
            time_diff = llabs(source_time - current_time);
        }
        else if(true == ignition_gpio_low_event_found) {
            source_event = "ignition_gpio_low_event";
            source_time = convert_epoch_format(ignition_gpio_low_node.event_time, DigitsOfEpoch::eDigits_Seconds);
            time_diff = llabs(source_time - current_time);
        }

        LOG_C(TAG, "Verifying Current time with %s, time diff b/w event time and current time: %lld sec", source_event.c_str(), time_diff);

        // If time_diff is greater than ONE_DAY_IN_SECONDS or if RTC reset and no time sync case last_rtc_alarm_time will be epoch time 0
        // It means current time older than source time or current time is in future
        // So, This is possibly time jump or time reset to default scenario
        // Sending critical info and mark it as misc_lowpower_wakeup
        static const int ONE_DAY_IN_SECONDS = 86400; // (24*60*60 = 86400 seconds)

        if ((time_diff > ONE_DAY_IN_SECONDS) || (POWER_MONITOR_ctx->last_rtc_alarm_time == 0)) {
            string err_msg = "Current time" + to_string(current_time) + " Source time:" + to_string(source_time) + " Diff:" + to_string(time_diff);
            LOG_C(TAG, "%s", err_msg.c_str());
            nd_service_obj->send_err_msg(SM_E_PM_RTC_WAKEUP_TIME, time_diff, err_msg);

            if(POWER_MONITOR_ctx->power_on_off_reason & (1 << PowerOnTriggerT::WAKEonRTC)) {
                POWER_MONITOR_ctx->misc_lowpower_wakeup = false;
            }
            else {
                POWER_MONITOR_ctx->misc_lowpower_wakeup = true;
            }
            return false;
        }
    }

    {
        int64_t time_gap_wakeup = 0;
        time_gap_wakeup = llabs(POWER_MONITOR_ctx->last_rtc_alarm_time - current_time);
        const unsigned int MAX_TIME_GAP = 120;  // sec . tolerances of 2 minutes for wakeup time jitter to validate the abrupt wakeup
        bool is_valid_rtc_time = ((time_gap_wakeup >= 0) && (time_gap_wakeup <= MAX_TIME_GAP)) ? true : false;

        if((false == shutdown_crank_off_event_found)) {

            if (true == ignition_gpio_low_event_found) {
                int64_t completed_duration = current_time - convert_epoch_format(ignition_gpio_low_node.event_time, DigitsOfEpoch::eDigits_Seconds);
                int64_t crank_shutdown_duration = 0;

                if(true == POWER_MONITOR_ctx->ext_cam_lpw_enabled) {
                    crank_shutdown_duration = POWER_MONITOR_ctx->dhub_crank_shutdown_duration;
                }
                else {
                    crank_shutdown_duration = POWER_MONITOR_ctx->crank_shutdown_duration;
                }

                LOG_I(TAG, "shutdown_crank_off not found, completed_duration: %lld", completed_duration);
                if((completed_duration > crank_shutdown_duration) && (true == is_valid_rtc_time)) {
                    LOG_I(TAG, "shutdown_crank_off not found, but valid rtc time found, So most probably device wakeup is due to safety time");
                    // ADD DBSTATE_SHUTDOWN_CRANKOFF event
                    is_abrupt_wakeup = true;
                    add_event_db(power_dbstate_enum_t::DBSTATE_SHUTDOWN_CRANKOFF, "SHUTDOWN");
                    shutdown_crank_off_event_found = true;
                }

            }
            else if ((false == ignition_gpio_low_event_found) && (true == is_valid_rtc_time)) {
                is_abrupt_wakeup = true;
                add_event_db(power_dbstate_enum_t::DBSTATE_SHUTDOWN_CRANKOFF, "SHUTDOWN");
                string err_msg = "Wakeup after abrupt shutdown, adding shutdown event to db";
                shutdown_crank_off_event_found = true;
                LOG_C(TAG, "%s", err_msg.c_str());
                nd_service_obj->send_err_msg(SM_E_PM_DB_PREV_SHUTDOWN, NDService::UNUSED_ERR_AUX_CODE, err_msg);
            }

            if((false == is_abrupt_wakeup) && (true == crank_low_event_found)) {
                // If remain_time is greater than crank_shutdown_duration
                // It means device rebooted before actual shutdown time and come up after shutdown time
                // So, we need to add DBSTATE_SHUTDOWN_CRANKOFF event
                int64_t remain_time = current_time - convert_epoch_format(crank_low_node.event_time, DigitsOfEpoch::eDigits_Seconds);
                if (remain_time > POWER_MONITOR_ctx->crank_shutdown_duration) {
                    LOG_C(TAG, "Device rebooted before actual crankoff shutdown time and come up after crankoff shutdown time");
                    is_abrupt_wakeup = true;
                    add_event_db(power_dbstate_enum_t::DBSTATE_SHUTDOWN_CRANKOFF, "SHUTDOWN");
                    shutdown_crank_off_event_found = true;
                }
            }
        }
        else if((true == lowpower_wakeup_event_found) && ((true == shutdown_lowpower_event_found) || (true == shutdown_crank_off_event_found))) {

            int64_t shutdown_time = (false == shutdown_lowpower_event_found) ? shutdown_crank_off_node.event_time : shutdown_lowpower_node.event_time;
            /* condition_1: To verify device wakeup after safety time and before next LPW

                LPW_SHUT --------  LPW ------ ABRUPT_SHUTDOWN ------ DEVICE_WAKEUP_AFTER_SAFTY_TIME ------ LPW
                  T0               T1              T2                        T3(current_time)              T4
                condition_1 = T0 - T1(always negative value)


                condition_2: To verify device wakeup after safety time

                LPW_SHUT --------  LPW ------ ABRUPT_SHUTDOWN ------ DEVICE_WAKEUP_AFTER_SAFTY_TIME  ------ LPW
                  T0               T1              T2                        T3(current_time)               T4
                condition_2 = (T3 - T1) > lowpower_wakeup_duration

             */
            int64_t condition_1 = convert_epoch_format(shutdown_time, DigitsOfEpoch::eDigits_Seconds) - convert_epoch_format(lowpower_wakeup_node.event_time, DigitsOfEpoch::eDigits_Seconds);
            int64_t condition_2 = current_time - convert_epoch_format(lowpower_wakeup_node.event_time, DigitsOfEpoch::eDigits_Seconds);

            LOG_I(TAG, "lowpower_wakeup_event_found, condition_1: %lld, condition_2: %lld", condition_1, condition_2);
            if((condition_1 < 0) && (condition_2 > POWER_MONITOR_ctx->lowpower_wakeup_duration) && (true == is_valid_rtc_time)) {
                LOG_I(TAG, "shutdown_lowpower not found, but valid rtc time found, So most probably device wakeup is due to safety time");
                // ADD DBSTATE_SHUTDOWN_LOWPOWER event
                is_abrupt_wakeup = true;
                add_event_db(power_dbstate_enum_t::DBSTATE_SHUTDOWN_LOWPOWER, "SHUTDOWN");
                shutdown_lowpower_event_found = true;
            }
            else {
                // If lowpower_wakeup_node index is greater than shutdown_lowpower_node index
                // means shutdown_lowpower_node latest event in db is not valid
                // So, we need to check for the time remain_time between current time and lowpower_wakeup_node event time
                // If lowpower_wakeup_duration is less than remain_time
                // It means device rebooted before actual shutdown time and come up after shutdown time
                // So, we need to add DBSTATE_SHUTDOWN_LOWPOWER event
                if(lowpower_wakeup_node.index > shutdown_lowpower_node.index) {
                    int64_t remain_time = current_time - convert_epoch_format(lowpower_wakeup_node.event_time, DigitsOfEpoch::eDigits_Seconds);
                    if (POWER_MONITOR_ctx->lowpower_wakeup_duration < remain_time) {
                        LOG_C(TAG, "Device rebooted before actual lowpower shutdown time and come up after lowpower shutdown time");
                        is_abrupt_wakeup = true;
                        add_event_db(power_dbstate_enum_t::DBSTATE_SHUTDOWN_LOWPOWER, "SHUTDOWN");
                        shutdown_lowpower_event_found = true;
                    }
                }
            }
        }

        if ((true == POWER_MONITOR_ctx->ext_cam_lpw_enabled) &&
            (true == dhub_wakeup_time_event_found) &&
            ((true == shutdown_crank_off_event_found) ||
            (true == shutdown_lowpower_event_found))) {

            int64_t dhub_wakeup_time = convert_epoch_format(dhub_wakeup_time_node.event_time, DigitsOfEpoch::eDigits_Seconds);
            time_gap_wakeup = llabs(dhub_wakeup_time - current_time);
            LOG_I(TAG, "dhub_wakeup_time: %lld in seconds format", dhub_wakeup_time);

            // To verify device wakeup is valid based current time and dhub wakeup time
            if ((time_gap_wakeup >= 0) && (time_gap_wakeup <= MAX_TIME_GAP)) {
                LOG_I(TAG, "Device wakeup is valid based on last dhub wakeup time and current time, time_gap_wakeup: %lld, so no need to check for misc lowpower wakeups", time_gap_wakeup);
                write_into_sysfs_entry(nd_device_obj->get_lpw_stat_sysfs_path(), static_cast<int>(lpw_state_t::eLPW_ON));
                return false;
            }
            else if(POWER_MONITOR_ctx->safety_time_to_sync_driveri_dhub > 0) {
                // To verify device rebooted b/w device wakeup with safety time and dhub wakeup time
                // Check 2nd last DBSTATE_RTC_WAKEUP_TIME_DHUB event
                // Because when device wakeup with safety time, at that time we are adding DBSTATE_RTC_WAKEUP_TIME_DHUB event
                // So for that reason we need to check 2nd last DBSTATE_RTC_WAKEUP_TIME_DHUB event
                val_stream.str("");
                val_stream << SELECT_POWERSTATES_EVENT_EQ("DBSTATE_RTC_WAKEUP_TIME_DHUB") << ORDER_BY_INDEXID_DESC_LIMIT_1 << " OFFSET 1";
                db_state_info_t dhub_wakeup_time_node_2nd_last = {};
                bool dhub_wakeup_time_event_found_2nd_last = check_event_in_db(val_stream, dhub_wakeup_time_node_2nd_last);

                int64_t dhub_wakeup_time_2nd_last = convert_epoch_format(dhub_wakeup_time_node_2nd_last.event_time, DigitsOfEpoch::eDigits_Seconds);
                LOG_I(TAG, "dhub_wakeup_time_2nd_last: %lld in seconds format", dhub_wakeup_time_2nd_last);

                if (current_time < dhub_wakeup_time_2nd_last) {
                    int64_t remain_time = dhub_wakeup_time_2nd_last - current_time;
                    if ((remain_time > 0) && (remain_time < POWER_MONITOR_ctx->safety_time_to_sync_driveri_dhub)) {
                        LOG_I(TAG, "Device rebooted b/w device wakeup with safety time and dhub wakeup time, so no need to check for misc lowpower wakeups");
                        // As already DB entry for DBSTATE_RTC_WAKEUP_TIME_DHUB is present with future time no need to add new entry
                        // For that reason making POWER_MONITOR_ctx->misc_lowpower_reboot to true
                        POWER_MONITOR_ctx->misc_lowpower_reboot = true;
                        POWER_MONITOR_ctx->lowpower_wakeup_duration += remain_time;
                        LOG_I(TAG, "POWER_MONITOR_ctx->lowpower_wakeup_duration: %d", POWER_MONITOR_ctx->lowpower_wakeup_duration);
                        return false;
                    }
                }
            }
        }
        else if ((false == POWER_MONITOR_ctx->ext_cam_lpw_enabled) &&
                 (true == device_wakeup_time_event_found) &&
                 ((true == shutdown_crank_off_event_found) ||
                 (true == shutdown_lowpower_event_found))) {

            int64_t device_wakeup_time = convert_epoch_format(device_wakeup_time_node.event_time, DigitsOfEpoch::eDigits_Seconds);
            time_gap_wakeup = llabs(device_wakeup_time - current_time);

            if ((time_gap_wakeup >= 0) && (time_gap_wakeup <= MAX_TIME_GAP)) {
                LOG_I(TAG, "Device wakeup is valid based on last device wakeup time and current time, time_gap_wakeup: %lld, so no need to check for misc lowpower wakeups", time_gap_wakeup);
                write_into_sysfs_entry(nd_device_obj->get_lpw_stat_sysfs_path(), static_cast<int>(lpw_state_t::eLPW_ON));
                return false;
            }
        }
    }


    bool known_state = false;
    bool wakeup_before_crank_shutdown_duration = false;
    bool is_wakeup_time_present_in_db = (device_wakeup_time_event_found || dhub_wakeup_time_event_found);

    // means device rebooted and wakeup before crank_shutdown_duration
    if ((!(shutdown_lowpower_event_found)) &&
            (!(shutdown_crank_off_event_found)) &&
            (shutdown_crank_high_event_found) &&
            (is_wakeup_time_present_in_db)) {

            // set crank_high_count to 1, to sync with the last crank high event
            POWER_MONITOR_ctx->crank_high_count = 1;
            wakeup_before_crank_shutdown_duration = true;
            LOG_I(TAG, "Device wakeup before crank_shutdown_duration window, set crank_high_count to 1 to sync with the last crank high event");
            known_state = true;
    }
    // means device wakeup after crank_shutdown_duration window and before lowpower_wakeup_cycle_duration window
    else if ((!(shutdown_lowpower_event_found)) &&
            (shutdown_crank_off_event_found) &&
            (shutdown_crank_high_event_found) &&
            (is_wakeup_time_present_in_db)) {

            LOG_I(TAG, "Device wakeup after crank_shutdown_duration window and before lowpower_wakeup_cycle_duration window");
            known_state = true;
    }
    // means device wakeup after lowpower_wakeup_cycle_duration window
    else if ((shutdown_lowpower_event_found) &&
        (shutdown_crank_off_event_found) &&
        (shutdown_crank_high_event_found) &&
        (is_wakeup_time_present_in_db)) {

        LOG_I(TAG, "Device wakeup after lowpower_wakeup_cycle_duration window or Device rebooted before lowpower_wakeup_duration window");
        known_state = true;

    }
    else if((!(shutdown_lowpower_event_found)) &&
        (!(shutdown_crank_off_event_found)) &&
        (shutdown_crank_high_event_found) &&
        (!(is_wakeup_time_present_in_db))) {

        LOG_I(TAG, "Device rebooted before detecting actual crank low event, set crank_high_count to 1 to sync with the last crank high event");
        POWER_MONITOR_ctx->crank_high_count = 1;
    }
    else {
        // Device came up after OTA update or DB entry for wakeup time
        string err_msg = "OTA/DB Event";
        LOG_C(TAG, "%s", err_msg.c_str());
        nd_service_obj->send_err_msg(SM_E_PM_RTC_WAKEUP_TIME, NDService::UNUSED_ERR_AUX_CODE, err_msg);

    }

    // If device is in known state, Then set device wakeup time based on the event
    if(known_state) {

        db_state_info_t wakeup_time_node = (dhub_wakeup_time_event_found) ? dhub_wakeup_time_node : device_wakeup_time_node;
        int64_t lpw_cycle_duration = get_low_power_wakeup_cycle_duration(wakeup_time_node.index, shutdown_lowpower_node.index, shutdown_crank_off_node.index);
        set_device_wakeup_time(wakeup_time_node, lpw_cycle_duration, wakeup_before_crank_shutdown_duration);

        if((true == POWER_MONITOR_ctx->misc_lowpower_wakeup) && (false == is_abrupt_wakeup)) {
            // decrement lowpower_wakeup_count as this is misc lowpower wakeup
            lowpower_wakeup_count--;
            LOG_I(TAG, "Decrementing lowpower_wakeup_count as this is misc lowpower wakeup, lowpower_wakeup_count: %d", lowpower_wakeup_count);
        }
    }
    else if (!is_wakeup_time_present_in_db) {

        // case 1. If device is new or device come up after OTA update, then no entry in power monitor db
        //         Related to device wakeup time or dhub wakeup time
        // case 2. Device rebooted before detecting actual crank low event.
        // In above case, setting wakeup duration based on the last crank low event if present

        LOG_I(TAG, "No DB entry for DBSTATE_RTC_WAKEUP_TIME_DHUB or DBSTATE_RTC_WAKEUP_TIME_DEVICE");

        if(shutdown_crank_high_event_found && (!shutdown_crank_off_event_found)) {
            // Check CRANK_LOW after Last CRANK_HIGH
            val_stream.str("");
            val_stream << SELECT_POWERSTATES_EVENT_EQ("DBSTATE_CRANKLOW") << " AND INDEXID > " << shutdown_crank_high_node.index << ORDER_BY_INDEXID_ASC_LIMIT_1;
            db_state_info_t crank_low_after_crank_high_node = {};
            bool crank_low_after_crank_high_event_found = check_event_in_db(val_stream, crank_low_after_crank_high_node);
            if (true == crank_low_after_crank_high_event_found) {
                LOG_I(TAG, "Crank low event found after last crank high event");
                int64_t shutdown_time = convert_epoch_format(crank_low_after_crank_high_node.event_time, DigitsOfEpoch::eDigits_Seconds) + POWER_MONITOR_ctx->crank_shutdown_duration;
                int64_t remain_time = shutdown_time - current_time;
                if(false == update_input_time_if_valid(remain_time, lpw_data.crank_shutdown_duration, POWER_MONITOR_ctx->crank_shutdown_duration)) {
                    LOG_C(TAG, "Not updating POWER_MONITOR_ctx->crank_shutdown_duration as remain_time is: %lld", remain_time);
                }
            }
        }
        else if (shutdown_crank_high_event_found && shutdown_crank_off_event_found && (!shutdown_lowpower_event_found)) {
            // Check CRANK_LOW after Last CRANK_OFF SHUTDOWN
            val_stream.str("");
            val_stream << SELECT_POWERSTATES_EVENT_EQ("DBSTATE_CRANKLOW") << " AND INDEXID > " << shutdown_crank_off_node.index << ORDER_BY_INDEXID_ASC_LIMIT_1;
            db_state_info_t crank_low_after_crank_off_node = {};
            bool crank_low_after_crank_off_event_found = check_event_in_db(val_stream, crank_low_after_crank_off_node);
            if (true == crank_low_after_crank_off_event_found) {
                LOG_I(TAG, "Crank low event found after last crank off event");
                int64_t shutdown_time = convert_epoch_format(crank_low_after_crank_off_node.event_time, DigitsOfEpoch::eDigits_Seconds) + POWER_MONITOR_ctx->lowpower_wakeup_duration;
                int64_t remain_time = shutdown_time - current_time;
                if(false == update_input_time_if_valid(remain_time, lpw_data.lowpower_wakeup_duration, POWER_MONITOR_ctx->lowpower_wakeup_duration)) {
                    LOG_C(TAG, "Not updating POWER_MONITOR_ctx->lowpower_wakeup_duration as remain_time is: %lld", remain_time);
                }
            }
        }
        else if (shutdown_crank_high_event_found && shutdown_crank_off_event_found && shutdown_lowpower_event_found) {
            // Check CRANK_LOW after Last LOWPOWER SHUTDOWN
            val_stream.str("");
            val_stream << SELECT_POWERSTATES_EVENT_EQ("DBSTATE_CRANKLOW") << " AND INDEXID > " << shutdown_lowpower_node.index << ORDER_BY_INDEXID_ASC_LIMIT_1;
            db_state_info_t crank_low_after_lowpower_node = {};
            bool crank_low_after_lowpower_event_found = check_event_in_db(val_stream, crank_low_after_lowpower_node);
            if (true == crank_low_after_lowpower_event_found) {
                LOG_I(TAG, "Crank low event found after last lowpower shutdown event");
                int64_t shutdown_time = convert_epoch_format(crank_low_after_lowpower_node.event_time, DigitsOfEpoch::eDigits_Seconds) + POWER_MONITOR_ctx->lowpower_wakeup_duration;
                int64_t remain_time = shutdown_time - current_time;
                if(false == update_input_time_if_valid(remain_time, lpw_data.lowpower_wakeup_duration, POWER_MONITOR_ctx->lowpower_wakeup_duration)) {
                    LOG_C(TAG, "Not updating POWER_MONITOR_ctx->lowpower_wakeup_duration as remain_time is: %lld", remain_time);
                }
            }
        }
    }

    LOG_I(TAG, "POWER_MONITOR_ctx->crank_shutdown_duration: %d, POWER_MONITOR_ctx->lowpower_wakeup_duration: %d", POWER_MONITOR_ctx->crank_shutdown_duration, POWER_MONITOR_ctx->lowpower_wakeup_duration);
    LOG_I(TAG, "POWER_MONITOR_ctx->lowpower_wakeup_cycle_duration: %d, POWER_MONITOR_ctx->lowpower_wakeup_long_cycle_duration: %d", POWER_MONITOR_ctx->lowpower_wakeup_cycle_duration, POWER_MONITOR_ctx->lowpower_wakeup_long_cycle_duration);
    LOG_I(TAG, "POWER_MONITOR_ctx->misc_lowpower_wakeup: %d, POWER_MONITOR_ctx->misc_lowpower_reboot: %d", POWER_MONITOR_ctx->misc_lowpower_wakeup, POWER_MONITOR_ctx->misc_lowpower_reboot);

    return true;
}
