/* Copyright (C) 2019 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Suresh Kumar <suresh.kumar@netradyne.com>, Spet 2019
 */

#include "time_sync.h"
#include "nd_db_utils.h"
#include "nd_file_utils.h"
#include "system_utils.h"
#include <jansson/jansson.h>
#include <nd_prop_utils.h>

static const string udid_db_fullpath = "/home/ubuntu/.nddevice/db/udid.db";

#ifdef BAGHEERA2
static const string time_sync_token_file = "/dev/shm/nd_files_c/time_sync_token_file.bin";
#else
static const string time_sync_token_file = "/dev/shm/time_sync_token_file.bin";
#endif

void *udid_thread (void *arg);
extern bool system_time_updated;
extern int64_t system_time_updated_from;
extern int64_t system_time_updated_to;

extern bool gps_valid;
extern double gps_lat;
extern double gps_long;
extern int64_t gps_time;
bool gps_start_entry = false;


extern NDService *nd_service_obj;
extern pthread_mutex_t time_sync_update_mutex;
extern pthread_mutex_t gps_update_mutex;
extern int low_power_wakeup_cnt ;

#define TAG "UDID"

using namespace std;

// DB handler for health data
db_handle_t* udid_db_handle = NULL;
static int64_t udid_global = -1;
static int64_t udid_boot_time;
static int64_t udid_present_time;

struct db_udid_info_t{
    int64_t index;
    int64_t udid;
    string udid_json;
    int status;
    int64_t boot_time;
};

//Server Queue Name
static const string Q_NAME = "TIME_SYNC";
//Ndcentral Queue name
static const string Q_NDCENTRAL = "q_nd_central";
//AWSIOT Queue Name
static const string Q_NAME_AWS_PUB = "AWSIOT_PUB";

static string get_msgq_name() {
    return Q_NAME;
}

static string get_ndcentral_q() {
    return Q_NDCENTRAL;
}

static string get_awsiot_q() {
    return Q_NAME_AWS_PUB;
}

static int fill_node_udid(void *state_vec, int argc, char **argv, char **azColName){

    db_udid_info_t* node = (db_udid_info_t *)state_vec;
    if( argc != 5 ) {
        node->index = -1;
        node->udid = -1;
        node->udid_json = "";
        node->status = -1;
        node->boot_time = -1;
        LOG_E(TAG, "Something wrong here in count_lowpower_wakeups_cb argc %d", argc);
        return -1;
    }
    string_to_int64(argv[0], node->index);
    string_to_int64(argv[1], node->udid);
    node->udid_json = argv[2];
    string_to_integer(argv[3], node->status);
    string_to_int64(argv[4], node->boot_time);

    return 0;
}

bool db_limit_rows(db_handle_t *db_handle) {
    int rc;
    std::stringstream val_stream;
    val_stream << "DELETE FROM UDID_TABLE WHERE ID IN (SELECT ID FROM " \
        " UDID_TABLE ORDER BY ID DESC LIMIT -1 OFFSET 1000)";
    rc = nd_exec_cmd_db(db_handle, val_stream.str(), NULL, NULL);
    if( rc == false ){
      LOG_E(TAG, "SQL error");
      return false;
    }
    return true;
}

bool init_health_db()
{
    string dest_dir = udid_db_fullpath;
    dest_dir = dest_dir.substr(0, dest_dir.find_last_of('/'));
    DIR *dir_p = opendir(dest_dir.c_str());
    if(dir_p == NULL){
        LOG_I(TAG, "folder not found: %s", dest_dir.c_str() );
        if(file_mkdir( dest_dir, 0777, true) == false) {
            LOG_E(TAG, "Failed to create folder %s", dest_dir.c_str() );
        }
        else {
            LOG_I(TAG, "folder created: %s", dest_dir.c_str() );
        }
    }
    if(dir_p != NULL){
        closedir(dir_p);
    }

    do {

        if (nd_open_db(udid_db_fullpath, &udid_db_handle) == false) {
            udid_db_handle = NULL;
            LOG_E(TAG, "failure in open DB");
            nd_service_obj->send_err_msg(SM_E_TIMESYNC_OPEN_DB_FAIL, 0, "open_DB failed" );
            return false;
        }

        // This will check the status and handle corruption inside nd_handle_db_corruption
        // In case of corruption udid_db_handle is set to NULL inside nd_handle_db_corruption
        // Post that the udid_db_handle will be a dangling pointer here since udid_db_handle is sent by value
        // TODO(handle): For now reopening the db below in case of failure but actually the
        //  nd_db_utils code needs to be modified to handle udid_db_handle
        if (!nd_exec_cmd_db(udid_db_handle, "PRAGMA quick_check;", NULL, 0)) {
            LOG_E(TAG, "failed to execute pragma quick_check");
        } else {
            break;
        }

        // Reopening the db
        if (!nd_open_db(udid_db_fullpath, &udid_db_handle)) {
            udid_db_handle = NULL;
            LOG_E(TAG, "failure in open DB");
            nd_service_obj->send_err_msg(SM_E_TIMESYNC_OPEN_DB_FAIL, 0, "open_DB failed" );
            return false;
        }

    } while (false);

    string schema = "CREATE TABLE IF NOT EXISTS UDID_TABLE(" \
            "ID INTEGER PRIMARY KEY  AUTOINCREMENT," \
            "UDID            INT    NOT NULL," \
            "UDID_JSON       TEXT    DEFAULT '{\"drive_start\": 0, \"drive_end\": 0, \"rtc_jump_from\": 0, \"rtc_jump_to\": 0}'," \
            "STATUS          INT     DEFAULT 1,"\
            "BOOT_TIME       INT     DEFAULT 0);" ;

    if (udid_db_handle != NULL) {
        if(nd_create_table_db(udid_db_handle, schema) == false) {
            LOG_E(TAG, "Failed to create_table_db");
            nd_service_obj->send_err_msg(SM_E_TIMESYNC_CREATE_DB_FAIL, 0, "create_table_DB failed" );
            return false;
        }
    }
    LOG_I(TAG, "success in create_table_db");
    db_limit_rows(udid_db_handle);
    return true;
}

int64_t get_udid() {

    bool first_after_boot;
    if(file_is_present(time_sync_token_file) == true) {
        LOG_E(TAG, "%s is already present", time_sync_token_file.c_str());
        first_after_boot = false;
    }
    else {
        file_touch(time_sync_token_file);
        first_after_boot = true;
    }

    db_udid_info_t udid_node = {0};
    int rc;
    std::stringstream val_stream;
    val_stream << "SELECT * from UDID_TABLE ORDER BY ID DESC LIMIT 1";
    rc = nd_exec_cmd_db(udid_db_handle, val_stream.str(), fill_node_udid, (void*)&udid_node);
    // this is a case where we dont have previous UDID records
    // could be we just started or some loss of data
    // in this case we always start with UDID 1
    if(udid_node.index == 0) {
        LOG_E(TAG, "setting UDID to 1; either DB corruption or first time initilizing udid");
        return 1;
    }
    LOG_I(TAG, "udid_node.index %lld, udid_node.udid %lld udid_json %s",
                 udid_node.index, udid_node.udid, udid_node.udid_json.c_str());
    
    if(first_after_boot == false) {
        LOG_E(TAG, "first_after_boot is false; must be a crash and start");
        return (udid_node.udid);
    }
    LOG_I(TAG, "first_after_boot is true udid is %lld", udid_node.udid+1);
    return ( udid_node.udid+1 );
}

bool add_udid_health_db(int64_t udid_str) {
    // this query inserts a row to DB with given UDID only if it dosent exists with the same udid
    string sql_str = "INSERT INTO UDID_TABLE(UDID,BOOT_TIME) SELECT "+ to_string(udid_str) + "," + to_string(udid_boot_time) + 
                " WHERE NOT EXISTS (SELECT 1 FROM UDID_TABLE WHERE UDID == "+ std::to_string(udid_str) +")";
    LOG_I(TAG, "add_udid_health_db sql_str %s\n", sql_str.c_str());

    bool rc = nd_exec_cmd_db(udid_db_handle, sql_str, NULL, 0);
    if(rc == false) {
        LOG_E(TAG, "failed to execute in add_udid_health_db");
    }
    return rc;
}

bool add_udid_props_db(int64_t udid) {
    LOG_I(TAG, "add_udid_props_db() udid: %lld", udid);
    return set_property_DB( "udid", to_string(udid) );
}

bool gps_hs_data(json_t *root) {

    if(gps_valid == false) {
        LOG_I(TAG, "GPS is not valid; returning from gps_hs_data");
        return true;
    }

    pthread_mutex_lock( &gps_update_mutex );
    if(gps_start_entry == false) {
        gps_start_entry = true;
        json_object_set_new( root, "start_gps_lat", json_real(gps_lat));
        json_object_set_new( root, "start_gps_long", json_real(gps_long));
        json_object_set_new( root, "start_gps_time", json_integer(gps_time));
    }
    else {
        json_object_set_new( root, "end_gps_lat", json_real(gps_lat));
        json_object_set_new( root, "end_gps_long", json_real(gps_long));
        json_object_set_new( root, "end_gps_time", json_integer(gps_time));
    }
    pthread_mutex_unlock( &gps_update_mutex );

    return true;
}

bool increment_uptime_health_db(int64_t udid_str, int64_t udid_boot_time, int64_t udid_present_time) {
    add_udid_health_db(udid_str);
    string val_string;
    std::stringstream val_stream;
    val_stream << "SELECT * FROM UDID_TABLE WHERE UDID == "<< udid_str;

    val_string = val_stream.str();
    LOG_I(TAG, "val_stream %s", val_string.c_str());

    db_udid_info_t udid_node = {0};
    bool rc = nd_exec_cmd_db(udid_db_handle, val_string, fill_node_udid, (void*)&udid_node);
    if(rc == false) {
        LOG_E(TAG, "failed to execute in increment_uptime_health_db");
        return rc;
    }
    if(udid_node.index == 0) {
        LOG_E(TAG, "Something wrong here; need to handle this case");
        return false;
    }

    LOG_I(TAG, "udid_node.udid %lld udid_json %s", udid_node.udid, udid_node.udid_json.c_str());
    json_t *root = NULL;
    json_error_t error;
    char *res_json = NULL;

    root = json_loads(udid_node.udid_json.c_str(), 0, &error);
    if(root == NULL){
        LOG_E(TAG,"json_loads failed in increment_uptime_health_db");
        LOG_I(TAG,"error: on line %d: %s", error.line, error.text);
        return false;
    }
    
    json_object_set_new( root, "drive_start", json_integer(udid_boot_time));
    json_object_set_new( root, "drive_end", json_integer(udid_present_time));
    if(low_power_wakeup_cnt != -1){
        json_object_set_new( root, "low_power_wakeup_count", json_integer(low_power_wakeup_cnt));
    }

    // push GPS data part of HS data
    gps_hs_data(root);

    res_json = json_dumps(root,0);
    if(res_json == NULL){
        LOG_E(TAG,"JSON creation failed in increment_uptime_health_db");
        json_decref(root);
        return false;
    }
    LOG_I(TAG, "new json %s", res_json);

    string sql_str = "UPDATE UDID_TABLE SET UDID_JSON = '"+ string(res_json) + "'" 
                + "WHERE ID == " + to_string(udid_node.index);

    rc = nd_exec_cmd_db(udid_db_handle, sql_str, NULL, NULL);
    if(rc == false) {
        LOG_E(TAG, "failed to execute in increment_uptime_health_db");
    }
    free(res_json);
    json_decref(root);
    return rc;
}

bool time_sync_to_health_db(int64_t udid_str, int64_t system_time_updated_from, int64_t system_time_updated_to) {
    add_udid_health_db(udid_str);
    string val_string;
    std::stringstream val_stream;
    val_stream << "SELECT * FROM UDID_TABLE WHERE UDID == "<< udid_str;

    val_string = val_stream.str();
    LOG_I(TAG, "val_stream %s", val_string.c_str());

    db_udid_info_t udid_node = {0};
    bool rc = nd_exec_cmd_db(udid_db_handle, val_string, fill_node_udid, (void*)&udid_node);
    if(rc == false) {
        LOG_E(TAG, "failed to execute in time_sync_to_health_db");
        return rc;
    }
    if(udid_node.index == 0) {
        LOG_E(TAG, "Something wrong here; need to handle this case");
        return false;
    }

    LOG_I(TAG, "udid_node.udid %lld udid_json %s", udid_node.udid, udid_node.udid_json.c_str()); 
    json_t *root = NULL;
    json_error_t error;
    char *res_json = NULL;

    root = json_loads(udid_node.udid_json.c_str(), 0, &error);
    if(root == NULL){
        LOG_E(TAG,"json_loads failed in time_sync_to_health_db");
        LOG_I(TAG,"error: on line %d: %s", error.line, error.text);
        return false;
    }
    
    json_object_set_new( root, "rtc_jump_from", json_integer(system_time_updated_from));
    json_object_set_new( root, "rtc_jump_to", json_integer(system_time_updated_to));
    res_json = json_dumps(root,0);
    if(res_json == NULL){
        LOG_E(TAG,"JSON creation failed in time_sync_to_health_db");
        json_decref(root);
        return false;
    }
    LOG_I(TAG, "new json %s", res_json);

    string sql_str = "UPDATE UDID_TABLE SET UDID_JSON = '"+ string(res_json) + "'" 
                + "WHERE ID == " + to_string(udid_node.index);

    rc = nd_exec_cmd_db(udid_db_handle, sql_str, NULL, NULL);
    if(rc == false) {
        LOG_E(TAG, "failed to execute in time_sync_to_health_db");
    }

    free(res_json);
    json_decref(root);
    return rc;
}

void *udid_thread (void *arg) {
    init_health_db();
    read_boot_time(udid_boot_time);

    udid_global = get_udid();
    LOG_I(TAG, "udid_global %lld ", udid_global);
#ifdef AUTOMATION
    if(isAutomationEnabled())
    {
        ofstream time_sync_token(time_sync_token_file,ios::out | ios::app);
        time_sync_token << udid_global;
        time_sync_token << "\n";
        time_sync_token.close();
    }
#endif
    // push udid details for DB
    add_udid_health_db(udid_global);
    if ( !add_udid_props_db(udid_global) ) {
        LOG_E(TAG, "failed to add udid to gen props DB");
    }

    while(1) {
        udid_present_time = get_system_time();
        increment_uptime_health_db(udid_global, udid_boot_time, udid_present_time);
        //record uptime for every 30 secs
        pthread_mutex_lock ( &time_sync_update_mutex );
        if( system_time_updated ) {
            LOG_I(TAG, "Updating rtc jump details, system_time_updated_from: %lld, system_time_updated_to: %lld", system_time_updated_from, system_time_updated_to );
            if( (system_time_updated_to - system_time_updated_from ) > 1000 ){
                time_sync_rtc_msg_t  time_sync_rtc_jump_msg;
                time_sync_rtc_jump_msg.rtc_jump_from = system_time_updated_from ;
                time_sync_rtc_jump_msg.rtc_jump_to = system_time_updated_to ;
                if( false == send_msg( (generic_msg_t *)&time_sync_rtc_jump_msg, (msg_type_t)TIME_SYNC_RTC_JUMP,
                               sizeof(time_sync_rtc_msg_t), get_msgq_name(), get_ndcentral_q(), 0) ) {
                    LOG_E (TAG,"sending TIME_SYNC_RTC_JUMP msg to ndcentral failed");
                }
                if( false == send_msg( (generic_msg_t *)&time_sync_rtc_jump_msg, (msg_type_t)TIME_SYNC_RTC_JUMP,
                               sizeof(time_sync_rtc_msg_t), get_msgq_name(), get_awsiot_q(), 0) ) {
                    LOG_E (TAG,"sending TIME_SYNC_RTC_JUMP msg to awsiot failed");
                }
                LOG_I(TAG, "TIME_SYNC_RTC_JUMP message Send to ndcentral: %lld, %lld", system_time_updated_from, system_time_updated_to );
            }
            time_sync_to_health_db(udid_global ,system_time_updated_from, system_time_updated_to);
            system_time_updated = false;
        }

        pthread_mutex_unlock ( &time_sync_update_mutex );
        sleep(30);
    }

}
