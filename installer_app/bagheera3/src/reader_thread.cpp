// this file contains changes  related to reader thread

#include "installer_app.h"

#include <bits/stdc++.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/file.h>
#include <sys/time.h>
#include <sys/stat.h>
#include "nd_msgq.h"
#include "nd_msg_types.h"
#include "nd_msg_utils.h"
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <queue>
#include "config_parser.h"
#include <string>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <jansson/jansson.h>
#include <stdexcept>
#include <linux/errno.h>
#include <sdcard_utils.h>
#include <glob.h>
#include <log.h>
#include "nd_time.h"
#include <nd_task.h>
#include <system_utils.h>

using namespace std;

#define TAG "INSTLR"
extern int connfd;

extern char readbuff[4096];

reader_thread_message t1;
int status, sequence_no, argument_no=0;
string socket_command;

static const string queue_name = "reader_queue";
static int msg_id=0;

json_t *command, *mdvr_ssid, *mdvr_pwd, *mdvr_config, *app_ver, *generic_command;
int len_generic_cmd=0;

static const int TIMEOUT_FOR_KEEPALIVE = 15; //15 sec timeout
static const int UNIT_TIME = 1;
static const int TEN_SECONDS = 10;
static const int VALID_CMD_IDLE_TIMEOUT = 360000; // 6min in ms
static int64_t prev_valid_cmd_time = -1;

pthread_mutex_t in_lock = PTHREAD_MUTEX_INITIALIZER;
extern const string command_file;

bool generic_cmd_flag = false;
int keep_alive_cmd_timeout = TIMEOUT_FOR_KEEPALIVE;

string config_json = "";

static const list<string> pair_data_keys = {"data", "accessory_type", "persistent"};

void read_config(){

   string keep_alive_timeout_str;
   bool get_override_val = true;
   bool is_val_overridden = false;
   static const string bagheera_config_path = "/home/ubuntu/.nddevice/latest/bagheera_config.ini";

    Config_parser installer_config_parser (bagheera_config_path);
    if (!installer_config_parser.getParseStatus()){
        LOG_E(TAG, "Can not parse bagheera_config_path inside read_config");
        return;
    }

    if ("true" == installer_config_parser.getConfig("INSTALLER_APP","generic_cmd_feature","false", get_override_val, is_val_overridden)) {
        generic_cmd_flag = true;
    }

    keep_alive_timeout_str = installer_config_parser.getConfig("INSTALLER_APP", "keep_alive_timeout_s", std::to_string(TIMEOUT_FOR_KEEPALIVE));

    if (!string_to_integer(keep_alive_timeout_str, keep_alive_cmd_timeout)) {
        keep_alive_cmd_timeout = TIMEOUT_FOR_KEEPALIVE;
        LOG_E(TAG, "string_to_integer, failed to get keep_alive_cmd_timeout");
    }

    LOG_I(TAG, "inside read_config function, generic_cmd_feature: %d, keep_alive_cmd_timeout: %d\n",
          generic_cmd_flag, keep_alive_cmd_timeout);
}

bool json_parsing_of_command(const string &json_command){

    json_error_t error;
    json_t *root = json_loads(json_command.c_str(), 0, &error);
    if (!root){
        LOG_E(TAG, "json loads fails\n");
        json_decref(root);
        LOG_E(TAG, "phone app is crashed need to restart device\n");
        memset (readbuff, 0, sizeof(readbuff));
	if(send_powermon_to_reboot( "installer_queue", REQ_POWERMON_INSTALLER_APP_TO_REBOOT) == false) {
            system_reboot();
        }
    }

    command = json_object_get(root, "cmd");
    mdvr_ssid = json_object_get(root, "mdvr_ssid");
    mdvr_pwd = json_object_get(root, "mdvr_pwd");
    mdvr_config = json_object_get(root, "mdvr_config");
    app_ver = json_object_get(root, "app_version");

    sequence_no = json_integer_value(json_object_get(root, "seq_no"));
    argument_no = json_integer_value(json_object_get(root, "argument"));

    strcpy(t1.command, json_string_value(command));

    LOG_I(TAG, "following commands got from phone app\n");
    LOG_I(TAG, "*****************************************\n");
    LOG_I(TAG, "sequence no : %d\n", sequence_no);
    LOG_I(TAG, "argument no : %d\n", argument_no);
    LOG_I(TAG, "command json value %s\n", json_string_value(command));

    if (!strcmp(json_string_value(command), "generic_cmd")){

        if (generic_cmd_flag){
            t1.generic_cmd_enable_flag =true;

            LOG_I(TAG, "got the generic command, fill it");
            generic_command = json_object_get(root, "device_cmd");

            if (strlen == 0){
               LOG_E(TAG, "device cmd is empty it does not have any command");
            }
            FILE *fptr;

            pthread_mutex_lock(&in_lock);
            fptr = fopen(command_file.c_str(), "w");

            if (fptr == NULL) {
               LOG_E(TAG, "Error!");
            }
            fprintf(fptr, "%s", json_string_value(generic_command));

            pthread_mutex_unlock(&in_lock);

            len_generic_cmd = strlen(json_string_value(generic_command));

            LOG_I(TAG, "wrote the command :%s and len :%d", json_string_value(generic_command), len_generic_cmd);

            fclose(fptr);
        }
        else{
            t1.generic_cmd_enable_flag = false;
            LOG_I(TAG, "generic cmd not enabled from cloud");
        }
    }


    config_json.clear();
    config_json = json_command;


    if(mdvr_ssid != NULL) {
        LOG_I(TAG, "mdvr ssid %s\n", json_string_value(mdvr_ssid));
    }
    if(mdvr_pwd != NULL) {
        LOG_I(TAG, "mdvr pwd %s\n", json_string_value(mdvr_pwd));
    }
    if(mdvr_config != NULL) {
        LOG_I(TAG, "mdvr config %s\n", json_string_value(mdvr_config));
    }
    if(app_ver != NULL) {
        LOG_I(TAG, "app_version %s\n", json_string_value(app_ver));
    }
    LOG_I(TAG, "*****************************************\n");

    return true;

}

bool send_msg_for_generic_cmd(){

    if( false == send_msg( (generic_msg_t *)&t1, (msg_type_t)GENERIC_CMD_APP, sizeof(t1), queue_name, queue_name, msg_id++ ) ) {
      LOG_E (TAG,"sending msg to main process failed for generic cmd");
      return false;
   }
}

bool send_msg_for_streaming(){

   if( false == send_msg( (generic_msg_t *)&t1, (msg_type_t)START_STREAMING, sizeof(t1), queue_name, queue_name, msg_id++ ) ) {
      LOG_E (TAG,"sending msg to main process failed");
      return false;
   }


}

bool send_msg_for_stop_streaming(){

   if( false == send_msg( (generic_msg_t *)&t1, (msg_type_t)STOP_STREAMING, sizeof(t1), queue_name, queue_name, msg_id++ ) ) {
      LOG_E (TAG,"sending msg to main process failed");
      return false;
   }

}

bool send_msg_for_diagnostic(){

    diagnostic_msg_t t1;
    t1.sequence_no = sequence_no;
    nd_strncpy(t1.config_json, config_json.c_str(), sizeof(t1.config_json));
   if( false == send_msg( (generic_msg_t *)&t1, (msg_type_t)DIAGNOSTIC, sizeof(t1), queue_name, queue_name, msg_id++ ) ) {
      LOG_E (TAG,"sending msg to main process failed");
      return false;
   }

}

bool send_msg_for_test_conn(){

    app_req_t t1;
    t1.dhubx.toggle_dhub_mode = true;
    t1.sequence_no = sequence_no;

    json_error_t error;
    json_t *root = json_loads(config_json.c_str(), 0, &error);
    if (root) {
        json_t *app_caps = json_object_get(root, "app_caps");
        if (app_caps) {
            json_t *accessories = json_object_get(app_caps, "accessories");
            bool dhubx = false;
            if (accessories) {
                size_t array_size = json_array_size(accessories);
                for (size_t i = 0; i < array_size; i++) {
                    json_t *accessory = json_array_get(accessories, i);
                    string accessory_type = json_string_value(accessory);
                    LOG_I(TAG, "accessory type: %s", accessory_type);
                    if (accessory_type == "DHUBX") {
                        LOG_I(TAG, "DHUBX found in accessories");
                        dhubx = true;
                        break;
                    }
                }
            }
            if (dhubx) {
                json_t *features = json_object_get(app_caps, "features");
                if (features) {
                    json_t *dhub_caps = json_object_get(features, "DHUBX");
                    if (dhub_caps) {
                        if (false == json_is_boolean(json_object_get(dhub_caps, "toggle_mode"))) {
                            LOG_E(TAG, "toggle_mode not present in json or not a boolean");
                        } else {
                            t1.dhubx.toggle_dhub_mode = json_boolean_value(json_object_get(dhub_caps, "toggle_mode"));
                        }
                    }
                }
            }
        }
        json_decref(root);
    }

   LOG_I(TAG, "Inside send_msg_for_test_conn case\n");

   if( false == send_msg( (generic_msg_t *)&t1, (msg_type_t)TEST_CONNECTION, sizeof(t1), queue_name, queue_name, msg_id++ ) ) {
      LOG_E (TAG,"sending msg to main process failed");
      return false;
   }

}

bool send_msg_for_exit(){

   LOG_I(TAG, "sending message for installer exit");
   if( false == send_msg( (generic_msg_t *)&t1, (msg_type_t)INSTALLER_EXIT, sizeof(t1), queue_name, queue_name, msg_id++ ) ) {
      LOG_E (TAG,"sending msg to main process failed");
      return false;
   }
}

void send_msg_for_ignition_status(){

   if( false == send_msg( (generic_msg_t *)&t1, (msg_type_t)IGNITION_STATUS_APP, sizeof(t1), queue_name, queue_name, msg_id++ ) ) {
      LOG_E (TAG,"sending msg to main process failed");
   }
}

void send_msg_for_mdvr_config() {
   mdvr_config_msg_t mdvr_conf;
  
   if(mdvr_ssid == NULL) {
       nd_strncpy(mdvr_conf.ssid, "invalid", sizeof(mdvr_conf.ssid));
   } else {
       nd_strncpy(mdvr_conf.ssid, json_string_value(mdvr_ssid), sizeof(mdvr_conf.ssid));
   }

   if(mdvr_pwd == NULL) {
       nd_strncpy(mdvr_conf.pwd, "invalid", sizeof(mdvr_conf.pwd));
   } else {
       nd_strncpy(mdvr_conf.pwd, json_string_value(mdvr_pwd), sizeof(mdvr_conf.pwd));
   }

   mdvr_conf.sequence_no = sequence_no;

   if( false == send_msg( (generic_msg_t *)&mdvr_conf, (msg_type_t)MDVR_CONFIG, sizeof(mdvr_conf), queue_name, queue_name, msg_id++ ) ) {
      LOG_E (TAG,"sending msg to main process failed");
   }
}

void send_msg_for_mdvr_config_file() {
   mdvr_config_file_msg_t mdvr_conf;

   if(mdvr_config == NULL) {
       nd_strncpy(mdvr_conf.config, "[ext_cam_config]\n", sizeof(mdvr_conf.config));
   } else {
       nd_strncpy(mdvr_conf.config, json_string_value(mdvr_config), sizeof(mdvr_conf.config));
   }

   if(app_ver == NULL) {
       nd_strncpy(mdvr_conf.app_version, "none", sizeof(mdvr_conf.app_version));
   } else {
       nd_strncpy(mdvr_conf.app_version, json_string_value(app_ver), sizeof(mdvr_conf.app_version));
   }

   mdvr_conf.sequence_no = sequence_no;

   if( false == send_msg( (generic_msg_t *)&mdvr_conf, (msg_type_t)MDVR_CONFIG_FILE, sizeof(mdvr_conf), queue_name, queue_name, msg_id++ ) ) {
      LOG_E (TAG,"sending msg to main process failed");
   }
}

void send_msg_for_mdvr_check() {
    if( false == send_msg( (generic_msg_t *)&t1, (msg_type_t)MDVR_FEATURE_CHECK, sizeof(t1), queue_name, queue_name, msg_id++ ) ) {
        LOG_E (TAG,"sending msg to main process failed");
    }
}

void send_msg_for_mdvr_wifi_strength() {
    if( false == send_msg( (generic_msg_t *)&t1, (msg_type_t)MDVR_WIFI_STRENGTH, sizeof(t1), queue_name, queue_name, msg_id++ ) ) {
        LOG_E (TAG,"sending msg to main process failed");
    }
}

void send_msg_for_mdvr_sdcard_check() {
    if( false == send_msg( (generic_msg_t *)&t1, (msg_type_t)MDVR_SDCARD_CHECK, sizeof(t1), queue_name, queue_name, msg_id++ ) ) {
        LOG_E (TAG,"sending msg to main process failed");
    }
}

bool send_msg_for_obd_diagnostic(){

   if( false == send_msg( (generic_msg_t *)&t1, (msg_type_t)OBD_PROTOCOL_APP, sizeof(t1), queue_name, queue_name, msg_id++ ) ) {
      LOG_E (TAG,"sending msg to main process failed");
      return false;
   }
}

bool send_msg_for_obd_diagnostic_extended(){

   if( false == send_msg( (generic_msg_t *)&t1, (msg_type_t)OBD_EXTENDED_CHECK, sizeof(t1), queue_name, queue_name, msg_id++ ) ) {
      LOG_E (TAG,"sending msg to main process failed");
      return false;
   }
}

bool send_msg_for_eld_diagnostic(){

   if( false == send_msg( (generic_msg_t *)&t1, (msg_type_t)ELD_INFO, sizeof(t1), queue_name, queue_name, msg_id++ ) ) {
      LOG_E (TAG,"sending msg to main process failed");
      return false;
   }
}

void send_msg_for_vehicle_conf() {
    vehicle_config_msg_t vehicle_conf;
    vehicle_conf.sequence_no = sequence_no;
    nd_strncpy(vehicle_conf.config_json, config_json.c_str(), config_json.length() + 1);
    if( false == send_msg( (generic_msg_t *)&vehicle_conf, (msg_type_t)VEHICLE_CONFIG, sizeof(vehicle_conf), queue_name, queue_name, msg_id++ ) ) {
        LOG_E (TAG,"sending msg to main process failed");
    }
    config_json.clear();
}

void send_msg_pair_unpair_accessory(string pair_data_str, bool pair) {

    // add in data to struct
    pair_unpair_data_msg_t pair_data;
    pair_data.sequence_no = sequence_no;
    LOG_I(TAG, "pair_data_str len: %d", pair_data_str.length());
    nd_strncpy(pair_data.data, pair_data_str.c_str(), MAX_PAIR_UNPAIR_DATA_LEN);
    pair_data.pair = pair;

    // send msg to main process
    if( false == send_msg( (generic_msg_t *)&pair_data, (msg_type_t)PAIR_UNPAIR_DATA, sizeof(pair_data), queue_name, queue_name, msg_id++ ) ) {
        LOG_E (TAG,"sending msg to main process failed");
    }
    config_json.clear();
}

void send_msg_fetch_paired_accessories() {
    if( false == send_msg( (generic_msg_t *)&t1, (msg_type_t)FETCH_PAIRED_ACCESSORIES, sizeof(t1), queue_name, queue_name, msg_id++ ) ) {
        LOG_E (TAG,"sending msg to main process failed");
    }
}

void send_msg_validate_dms(string &sn,string &pn) {
    validate_dms_msg_t validate_dms_msg;
    validate_dms_msg.sequence_no = sequence_no;
    nd_strncpy(validate_dms_msg.sn, sn.c_str(), sn.length() + 1);
    nd_strncpy(validate_dms_msg.pn, pn.c_str(), pn.length() + 1);

    if (false == send_msg((generic_msg_t *)&validate_dms_msg, (msg_type_t)VALIDATE_DMS, sizeof(validate_dms_msg), queue_name, queue_name, msg_id++)) {
        LOG_E(TAG, "sending msg to main process failed");
    }
}

void send_msg_start_dms_camera_calibration() {
    
    json_t *data = nullptr;
    do {
        json_error_t error;
        data = json_loads(config_json.c_str(), 0, &error);
        if(data == nullptr) {
            LOG_E(TAG, "json loads fails");
            break;
        }

        // extract data
        // uuid
        string uuid = "";
        if(json_is_string(json_object_get(data, "uuid")) == false) {
            LOG_E(TAG, "uuid not present in json");
        } else {
            uuid = json_string_value(json_object_get(data, "uuid"));
        }

        // timestamp
        int64_t timestamp = 0;
        if(json_is_integer(json_object_get(data, "timestamp")) == false) {
            LOG_E(TAG, "timestamp not present in json");
        } else {
            timestamp = json_integer_value(json_object_get(data, "timestamp"));
        }

        // pn
        string pn = "";
        if(json_is_string(json_object_get(data, "PN")) == false) {
            LOG_E(TAG, "pn not present in json");
        } else {
            pn = json_string_value(json_object_get(data, "PN"));
        }

        // sn
        string sn = "";
        if(json_is_string(json_object_get(data, "SN")) == false) {
            LOG_E(TAG, "sn not present in json");
        } else {
            sn = json_string_value(json_object_get(data, "SN"));
        }

        json_t *calibration_feedback = json_object_get(data, "calibration_feedback");

        string notes_str = "";
        if (calibration_feedback == NULL) {
            LOG_E(TAG, "dms_feedback key not present in json");
        }
        else {
            if(json_is_string(json_object_get(calibration_feedback, "notes")) == false) {
                LOG_E(TAG, "notes not present in json");
            } else {
                notes_str = json_string_value(json_object_get(calibration_feedback, "notes"));
            }
        }
        // send msg to main process
        start_dms_calibration_msg_t start_dms_calibration_msg;
        start_dms_calibration_msg.sequence_no = sequence_no;
        nd_strncpy(start_dms_calibration_msg.sn, sn.c_str(), sn.length() + 1);
        nd_strncpy(start_dms_calibration_msg.uuid, uuid.c_str(), uuid.length() + 1);
        nd_strncpy(start_dms_calibration_msg.pn, pn.c_str(), pn.length() + 1);
        nd_strncpy(start_dms_calibration_msg.feedback, notes_str.c_str(), notes_str.length() + 1);
        start_dms_calibration_msg.timestamp = timestamp;

        if( false == send_msg( (generic_msg_t *)&start_dms_calibration_msg, (msg_type_t)START_DMS_CALIBRATION, sizeof(start_dms_calibration_msg), queue_name, queue_name, msg_id++ ) ) {
            LOG_E (TAG,"sending msg to main process failed");
        }
    } while(false);

    if(data != nullptr) {
        json_decref(data);
    }

}

void send_msg_end_dms_session() {
    if( false == send_msg( (generic_msg_t *)&t1, (msg_type_t)END_DMS_SESSION, sizeof(t1), queue_name, queue_name, msg_id++ ) ) {
        LOG_E (TAG,"sending msg to main process failed");
    }
}

void send_msg_toggle_annotation(int annotation_status) {
    toggle_annotation_msg_t toggle_annotation_msg;
    toggle_annotation_msg.sequence_no = sequence_no;
    toggle_annotation_msg.annotation_status = annotation_status;

    if (false == send_msg((generic_msg_t *)&toggle_annotation_msg, (msg_type_t)TOGGLE_ANNOTATION, sizeof(toggle_annotation_msg), queue_name, queue_name, msg_id++)) {
        LOG_E(TAG, "sending msg to main process failed");
    }
}

void send_msg_rotate_stream() {
    if( false == send_msg( (generic_msg_t *)&t1, (msg_type_t)ROTATE_STREAM, sizeof(t1), queue_name, queue_name, msg_id++ ) ) {
        LOG_E (TAG,"sending msg to main process failed");
    }
}

void send_msg_check_vbus_conn() {
    if( false == send_msg( (generic_msg_t *)&t1, (msg_type_t)CHECK_VBUS_CONN, sizeof(t1), queue_name, queue_name, msg_id++ ) ) {
        LOG_E (TAG,"sending msg to main process failed");
    }
}

void send_msg_for_unknown_command(){

   if( false == send_msg( (generic_msg_t *)&t1, (msg_type_t)UNKNOWN_COMMAND_APP, sizeof(t1), queue_name, queue_name, msg_id++ ) ) {
      LOG_E (TAG,"sending msg to main process failed");
   }
}

bool extract_data_from_json(string &json_string_cmd, list<string> desired_keys, string &extracted_data) {
    bool ret = false;
    json_error_t error;
    json_t *root;
    json_t * extract_json = json_object();
    do {
        // load json string
        root = json_loads(json_string_cmd.c_str(), 0, &error);
        if(root == NULL) {
            LOG_E(TAG, "json loads fails");
            break;
        }
        int data_count = 0;
        json_t *value;

        LOG_I(TAG, "string cmd: %s",json_string_cmd.c_str());

        // extract desired keys
        for(auto key : desired_keys) {
            value = json_object_get(root, key.c_str());
            if(value == NULL) {
                LOG_E(TAG, "json_object_get fails for key: %s", key.c_str());
                break;
            }
            json_object_set_new(extract_json, key.c_str(), value);
            data_count++;
        }
        // dump json string
        char* json_char = json_dumps(extract_json, JSON_COMPACT);
        if(json_char == NULL) {
            LOG_E(TAG, "json_dumps fails");
            break;
        }
        extracted_data = json_char;
        LOG_I(TAG, "extracted_data: %s", extracted_data.c_str());
        // free memory
        free(json_char);
        ret = true;
    } while(false);

    // free memory
    if(root != NULL) {
        json_decref(root);
    }
    if(extract_json != NULL) {
        json_decref(extract_json);
    }
    if(!ret) {
        extracted_data = "";
    }
    return ret;
}

bool extract_dms_data(string &json_string_cmd, string &sn, string &pn) {
    bool ret = false;
    json_t *root = nullptr;
    json_t *sn_json = nullptr;
    json_t *pn_json = nullptr; 
    do {
        json_error_t error;
        // load json string
        root = json_loads(json_string_cmd.c_str(), 0, &error);
        if(root == nullptr) {
            LOG_E(TAG, "json loads fails");
            break;
        }
        sn_json = json_object_get(root, "SN");
        if(sn_json == nullptr) {
            LOG_E(TAG, "json_object_get fails for key: sn");
            break;
        }
        pn_json = json_object_get(root, "PN");
        if(pn_json == nullptr) {
            LOG_E(TAG, "json_object_get fails for key: pn");
            pn = "";
        } else {
            pn = json_string_value(pn_json);
        }
        sn = json_string_value(sn_json);


        LOG_I(TAG, "sn: %s",sn.c_str());
        LOG_I(TAG, "pn: %s",pn.c_str());
        ret = true;
    } while(false);
    if (root != nullptr) {
        json_decref(root);
    }

    return ret;
}

bool get_annotation_value(string &json_string_cmd, int &annotation_value) {
    bool ret = false;
    json_t *root = nullptr;
    json_t *annotation_json = nullptr;
    do {
        json_error_t error;
        // load json string
        root = json_loads(json_string_cmd.c_str(), 0, &error);
        if(root == nullptr) {
            LOG_E(TAG, "json loads fails");
            break;
        }
        annotation_json = json_object_get(root, "annotation");
        if(annotation_json == nullptr) {
            LOG_E(TAG, "json_object_get fails for key: annotation");
            break;
        }
        annotation_value = json_integer_value(annotation_json);
        LOG_I(TAG, "annotation_value: %d",annotation_value);
        ret = true;
    } while(false);
    if (root != nullptr) {
        json_decref(root);
    }

    return ret;
}

bool send_data_to_main_process(){

    t1.sequence_no = sequence_no;
    t1.argument = argument_no;
    t1.device_cmd_len = len_generic_cmd;

    LOG_I(TAG, "json string value in send_data func is %s", json_string_value(command));

    // send ack for command irrespective of response
    response_command_ack(sequence_no);

    if(!strcmp(json_string_value(command), "keep-alive")) {
        LOG_I(TAG, "keep-alive command received");
        int64_t curr_time = get_system_monotonic_time();
        int64_t time_diff = curr_time - prev_valid_cmd_time;

        if(time_diff < 0) {
            prev_valid_cmd_time = curr_time;
            return true;
        }

        LOG_I(TAG, "curr_time: %ld, prev_time: %ld, time_diff: %d, timeout: %d",
                curr_time, prev_valid_cmd_time, time_diff, VALID_CMD_IDLE_TIMEOUT);
        // If time diff exhausts max idle timeout exit the app
        if(time_diff > VALID_CMD_IDLE_TIMEOUT) {
            LOG_I(TAG, "no valid command for %d secs. exiting app", VALID_CMD_IDLE_TIMEOUT/1000);
            send_msg_for_exit();
            sleep(TEN_SECONDS);
            pthread_exit(NULL);
        }

        return true;
    }
    
    prev_valid_cmd_time = get_system_monotonic_time();
    if (!strcmp(json_string_value(command), "stream")){
       send_msg_for_streaming();
    }
    else if (!strcmp(json_string_value(command), "stop_stream")){
     send_msg_for_stop_streaming();

    }
    else if (!strcmp(json_string_value(command), "diagnostic")){
      send_msg_for_diagnostic();

    }

    else if (!strcmp(json_string_value(command), "test_con")){

       send_msg_for_test_conn();
    }

    else if (!strcmp(json_string_value(command), "exit")){

     send_msg_for_exit();

    } 
    else if (!strcmp(json_string_value(command), "ignition_status")) {
        send_msg_for_ignition_status();
    }
    else if (!strcmp(json_string_value(command), "mdvr_check")) {
        send_msg_for_mdvr_check();
    }
    else if (!strcmp(json_string_value(command), "mdvr")) {
        send_msg_for_mdvr_config();
    }
    else if (!strcmp(json_string_value(command), "mdvr_file")) {
        send_msg_for_mdvr_config_file();
    }
    else if (!strcmp(json_string_value(command), "mdvr_wifi_strength")) {
        send_msg_for_mdvr_wifi_strength();
    }
    else if (!strcmp(json_string_value(command), "mdvr_sdcard_check")) {
        send_msg_for_mdvr_sdcard_check();
    }
    else if (!strcmp(json_string_value(command), "obd_diagnostic")) {
        send_msg_for_obd_diagnostic();
    }
    else if (!strcmp(json_string_value(command), "obd_diagnostic_ext")) {
        send_msg_for_obd_diagnostic_extended();
    }
    else if (!strcmp(json_string_value(command), "eld_diagnostic")) {
        send_msg_for_eld_diagnostic();
    }
    else if (!strcmp(json_string_value(command), "set_veh_data")) {
        send_msg_for_vehicle_conf();
    }
    else if (!strcmp(json_string_value(command), "pair")) {
        string pair_data;
        if (!extract_data_from_json(config_json,pair_data_keys,pair_data)){
            LOG_E(TAG, "extract_data_from_json failed for pair");
        }
        send_msg_pair_unpair_accessory(pair_data, true);
    }
    else if (!strcmp(json_string_value(command), "unpair")) {
        string pair_data;
        if (!extract_data_from_json(config_json,pair_data_keys,pair_data)){
            LOG_E(TAG, "extract_data_from_json failed for pair");
        }
        send_msg_pair_unpair_accessory(pair_data, false);
    }
    else if (!strcmp(json_string_value(command), "fetch_paired_accessories")) {
        LOG_I(TAG, "fetch_paired_accessories received");
        send_msg_fetch_paired_accessories();
    }
    else if (!strcmp(json_string_value(command), "validate_dms")) {
        LOG_I(TAG, "validate_dms received");

        string sn, pn;
        if (!extract_dms_data(config_json,sn,pn)){
            LOG_E(TAG, "extract_data_from_json failed for pair");
        }
        send_msg_validate_dms(sn, pn);

    }
    else if (!strcmp(json_string_value(command), "start_dms_camera_calibration")) {
        LOG_I(TAG, "start_dms_camera_calibration received");
        send_msg_start_dms_camera_calibration();

    }
    else if (!strcmp(json_string_value(command), "end_dms_session")) {
        LOG_I(TAG, "end_dms_session received");
        send_msg_end_dms_session();

    }
    else if (!strcmp(json_string_value(command), "toggle_annotation")) {
        LOG_I(TAG, "toggle_annotation received");
        int annotation_value = 0;
        if(get_annotation_value(config_json, annotation_value) == false) {
            LOG_E(TAG, "get_annotation_value failed");
            annotation_value = -1;
        }
        send_msg_toggle_annotation(annotation_value);

    } 
    else if (!strcmp(json_string_value(command), "rotate_cam")) {
        send_msg_rotate_stream();
    }

    else if (!strcmp(json_string_value(command), "check_vdr_conn_status")) {
        LOG_I(TAG, "check_vdr_conn_status received");
        send_msg_check_vbus_conn();
    }

    else if (!strcmp(json_string_value(command), "generic_cmd")) {
        send_msg_for_generic_cmd();
    }

   else {
        LOG_E(TAG, "error in command from app\n");
        send_msg_for_unknown_command();
   }
   return true;
}

bool wrapper_for_read(void *arg){

// since we cannot pass multiple argument in nd_timed_task
// so creating this wrapper function, all arguments are global

    int rc = read(connfd, readbuff, sizeof(readbuff));
    if(rc <= 0) {
        LOG_E(TAG, "socket closed on other end: %d", errno);
        return false;
    }
    return true;
}

std::vector<std::string> get_json_strings() {

    std::vector<std::string> parsed_json_strings;

    int object_boundary = 0;
    size_t object_pos = 0;
    bool object_found = false;

    for(size_t itr = 0; itr < 4096; ++itr) {
        if ('\0' == readbuff[itr]) {
            LOG_I(TAG, "get_json_strings: found end of string");
            break;
        }

        if ('{' == readbuff[itr]) {
            if (0 == object_boundary) {
                object_pos = itr;
                LOG_I(TAG, "get_json_strings: object boundary starts at: %lu",object_pos);
                object_found = true;
            }
            ++object_boundary;
        } else if ('}' == readbuff[itr]) {
            --object_boundary;
            if (0 == object_boundary) {
                LOG_I(TAG, "get_json_strings: object boundary ends at: %lu",itr);
            }
        } else {
        }

        if (0 > object_boundary) {
            LOG_E(TAG, "get_json_strings: invalid json, partial object received?");
            parsed_json_strings.clear();
            break;
        }

        if ((object_found) && (0 == object_boundary)) {
            LOG_I(TAG, "get_json_strings: object_pos: %lu, itr: %lu", object_pos, itr);
            parsed_json_strings.emplace_back(&readbuff[object_pos], (itr - object_pos + 1));
            if (!parsed_json_strings.empty()) {
                LOG_I(TAG, "get_json_strings: parsed_json_strings.back(): %s", parsed_json_strings.back().c_str());
            }
            object_found = false;
        }
    }

    return parsed_json_strings;
}

bool read_message_from_socket() {

    task_result_t  read_result;

    prev_valid_cmd_time = get_system_monotonic_time();

    read_config();

    while (1) {

        if(connfd < 0) {
            sleep(UNIT_TIME);
            continue;
        }

        // If buffer is not empty then read for message. The first message is already contained in readbuff
        if ('\0' == readbuff[0]) {
            LOG_I(TAG, "Buffer empty, reading from socket");
            read_result = nd_timed_task(wrapper_for_read, keep_alive_cmd_timeout, (void*)NULL, "task for read");

            if (read_result != TASK_SUCCESS) {
                LOG_E(TAG, "did not receive any command from app for %d sec or read timed task failed", keep_alive_cmd_timeout);
                send_msg_for_exit();
                sleep(TEN_SECONDS);
                pthread_exit(NULL);
            }
        } else {
            LOG_I(TAG, "Reading current data from buffer");
        }

        LOG_I(TAG, "command from app is %s\n", readbuff);

        // Due to the recent changes in app for keep alive, there are cases where multiple commands
        //  can be received in single read like below:
        // {"cmd":"keep-alive","seq_no":9}{"cmd":"keep-alive","seq_no":9}{"cmd":"keep-alive","seq_no":9}

        const auto parsed_strings = get_json_strings();

        LOG_I(TAG, "Commands received size: %lu\n", parsed_strings.size());

        for(const auto &json_command : parsed_strings) {
            json_parsing_of_command(json_command);
            send_data_to_main_process();
        }

        memset (readbuff, 0, sizeof (readbuff));
    }
    return true;
}

void *reader_thread_main(void *dummy){

    LOG_I(TAG, "socket desc at reader thread main is %d\n", connfd);
    read_message_from_socket();

}
