//response_thread_main
//this file contains functionality related to response from device to app
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
#include <log.h>
#include "config_parser.h"
#include <string>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <jansson/jansson.h>
#include <stdexcept>
#include <linux/errno.h>
#include <storage_utils.h>
#include <glob.h>
#ifndef KRAIT
#include <gst/gst.h>
#endif
#include <system_utils.h>
#include "installer_app.h"
#include "nd_accessory_db.h"
#include "nd_ext_cam_utils.h"

nd_msgq_t::nd_msg_t *msg_response;
extern int connfd;
pthread_mutex_t write_mutex = PTHREAD_MUTEX_INITIALIZER;
extern pthread_mutex_t log_handle_mutex;

static const string queue_name = "q_ins_read";
extern const string response_file;
extern const string command_file;
extern string stream_port;
extern const string obd_info_file;

static const int max_app_strlen = 800;

//generic_msg_t *g_msg;

#define TAG "INSTLR"
pthread_mutex_t res_lock = PTHREAD_MUTEX_INITIALIZER;

void send_msg_for_app_exit(){
   generic_msg_t t1;
   if( false == send_msg( (generic_msg_t *)&t1, (msg_type_t)INSTALLER_EXIT, sizeof(t1), queue_name, queue_name, 0 ) ) {
      LOG_E (TAG,"sending msg to main process failed");
   }
}

void write_socket(char *res_json, int len) {
    pthread_mutex_lock(&write_mutex);
    int ret = write(connfd, res_json, len);
    pthread_mutex_unlock(&write_mutex);
    if(ret < 0) {
        LOG_I(TAG, "failed to write to socket");
        send_msg_for_app_exit();
    }
}

bool testconn_response_to_app(){

    char *res_json = NULL;
    int sequence_response = 0;
    bool response = false;

    response_for_testconn *g_msg = (response_for_testconn *)msg_response->get_buffer();
    sequence_response = g_msg->sequence_no;
    response = g_msg->response;

    // create json string and write to socket

    json_t *json_1 = json_object();
    json_object_set_new(json_1, "cmd", json_string("res_test_conn"));
    if (response) {
        json_object_set_new(json_1, "res_status", json_boolean(1));
    } else {
        json_object_set_new(json_1, "res_status", json_boolean(0));
    }

    json_object_set_new(json_1, "seq", json_integer(sequence_response));
    json_object_set_new(json_1, "appVer", json_string(app_version.c_str()));
    json_object_set_new(json_1, "device_type", json_string(device_type.c_str()));
    json_object_set_new(json_1, "stream_port", json_string(stream_port.c_str()));

    json_t * capabilities_json = json_load_file(device_capabilities_file.c_str(), 0, NULL);
    if (capabilities_json == NULL) {
        LOG_E(TAG, "Failed to load json file %s\n", device_capabilities_file.c_str());
        capabilities_json = json_object();
    }
    json_object_set_new(json_1, "caps", capabilities_json);

    res_json = json_dumps(json_1, 0);

    if (res_json == NULL) {
        LOG_E(TAG, "JSON creation failed for stream cam1 command\n");
        return false;
    }

    LOG_I(TAG, "json dump for response is\n");
    LOG_I(TAG, "%s\n", res_json);

    // push the json to app

    write_socket(res_json, strlen(res_json));

    free(res_json);
    json_decref(json_1);
    return true;
}

bool command_ack_response_to_app(){

    char *res_json = NULL;
    int sequence_response =0;

    response_for_ack *g_msg = (response_for_ack *)msg_response->get_buffer();
    sequence_response = g_msg->sequence_no;

    // create json string and write to socket

    json_t *json_1 =json_object();
    json_object_set_new( json_1, "cmd", json_string("ack"));
    json_object_set_new( json_1, "seq", json_integer(sequence_response));
    res_json = json_dumps(json_1,0);

    if(res_json == NULL){
        LOG_E(TAG,"JSON creation failed for keep alive command\n");
        return false;
    }

    LOG_I(TAG, "json dump for response is\n");
    LOG_I(TAG, "%s\n", res_json);

    // push the json to app
    write_socket(res_json, strlen(res_json));

    free(res_json);
    json_decref(json_1);
    return true;
}

bool streaming_response_to_app(){

    char *res_json = NULL;
    int sequence_response, argument_response =0;
    bool response =false, ext_cam_enabled = false;

    response_for_streaming *g_msg = (response_for_streaming *)msg_response->get_buffer();
    sequence_response = g_msg->sequence_no;
    response = g_msg->response;
    argument_response = g_msg->argument;
    ext_cam_enabled = g_msg->ext_cam_enabled;

   json_t *json_1 =json_object();
   json_object_set_new( json_1, "cmd", json_string("res_streaming"));
   if (response) {
       json_object_set_new( json_1, "res_status", json_boolean(1));
   }
   else {
       json_object_set_new( json_1, "res_status", json_boolean(0));
   }

   json_object_set_new( json_1, "seq", json_integer(sequence_response));
   if(!ext_cam_enabled) {
       json_object_set_new( json_1, "ext_cam_resp", json_string("mdvr_feature_disabled"));
   } else {
       json_object_set_new( json_1, "ext_cam_resp", json_string("mdvr_feature_enabled"));
   }
   res_json = json_dumps(json_1,0);

   if(res_json == NULL){
      LOG_E(TAG,"JSON creation failed for streaming response\n");
      return false;
   }


   LOG_I(TAG, "json dump for response is\n");
   LOG_I(TAG, "%s\n", res_json);

   // push the json to app

   write_socket(res_json, strlen(res_json));

   free(res_json);
   json_decref(json_1);
   return true;
}

bool stop_streaming_response_to_app(){


    char *res_json = NULL;
    int sequence_response=0;
    bool response =false;

    response_for_stop_streaming *g_msg = (response_for_stop_streaming *)msg_response->get_buffer();
    sequence_response = g_msg->sequence_no;
    response = g_msg->response;

   json_t *json_1 =json_object();
   json_object_set_new( json_1, "cmd", json_string("res_stop_streaming"));
   if (response) {
       json_object_set_new( json_1, "res_status", json_boolean(1));
   }
   else {
       json_object_set_new( json_1, "res_status", json_boolean(0));
   }

   json_object_set_new( json_1, "seq", json_integer(sequence_response));
   res_json = json_dumps(json_1,0);

   if(res_json == NULL){
      LOG_E(TAG,"JSON creation failed for streaming response\n");
      return false;
   }


   LOG_I(TAG, "json dump for response is\n");
   LOG_I(TAG, "%s\n", res_json);

   // push the json to app

   write_socket(res_json, strlen(res_json));

   free(res_json);
   json_decref(json_1);
   return true;
}

bool ignition_status_response_to_app() {

    char *res_str = NULL;
    int sequence_response=0;
    bool response =false;

    response_for_ignition_status *g_msg = (response_for_ignition_status *)msg_response->get_buffer();
    sequence_response = g_msg->sequence_no;
    response = g_msg->response;

   json_t *json_1 =json_object();
   json_object_set_new( json_1, "cmd", json_string("res_ignition_status"));
   if (response) {
       json_object_set_new( json_1, "ignition_status", json_boolean(1));
   } else {
       json_object_set_new( json_1, "ignition_status", json_boolean(0));
   }

   json_object_set_new( json_1, "seq", json_integer(sequence_response));
   res_str = json_dumps(json_1,0);

   if(res_str == NULL){
      LOG_E(TAG,"JSON creation failed for ignition status response\n");
      return false;
   }

   LOG_I(TAG, "json dump for response is\n");
   LOG_I(TAG, "%s\n", res_str);

   // push the json to app

   write_socket(res_str, strlen(res_str));

   free(res_str);
   json_decref(json_1);
   return true;
}

bool mdvr_config_response_to_app() {
    char *res_json = NULL;
    int sequence_response;
    bool response = false;

    response_for_mdvr_config *g_msg = (response_for_mdvr_config *)msg_response->get_buffer();
    sequence_response = g_msg->sequence_no;
    response = g_msg->response;


    json_t *json_1 =json_object();
    json_object_set_new( json_1, "cmd", json_string("res_mdvr_config"));
    if (response) {
        json_object_set_new( json_1, "res_status", json_boolean(1));
    }
    else {
        json_object_set_new( json_1, "res_status", json_boolean(0));
    }

    json_object_set_new( json_1, "seq", json_integer(sequence_response));
    res_json = json_dumps(json_1,0);

    if(res_json == NULL){
       LOG_E(TAG,"JSON creation failed for mdvr config response\n");
       return false;
    }


    LOG_I(TAG, "json dump for response is\n");
    LOG_I(TAG, "%s\n", res_json);

    // push the json to app

    write_socket(res_json, strlen(res_json));

    free(res_json);
    json_decref(json_1);
    return true;
}

bool mdvr_feature_response_to_app() {
    char *res_json = NULL;
    int sequence_response;
    bool response = false, cloud_override = false, cloud_override_value;
    char mdvr_id[MAX_MDVR_ID_LENGTH], cloud_mdvr_id[MAX_MDVR_ID_LENGTH];

    response_for_mdvr_feature_check *g_msg = (response_for_mdvr_feature_check *)msg_response->get_buffer();
    sequence_response = g_msg->sequence_no;
    response = g_msg->response;
    cloud_override = g_msg->cloud_override;
    cloud_override_value = g_msg->cloud_override_value;

    nd_strncpy(mdvr_id, g_msg->mdvr_id, g_msg->mdvr_id_length+1);
    nd_strncpy(cloud_mdvr_id, g_msg->cloud_mdvr_id, g_msg->cloud_mdvr_id_length+1);

    LOG_I(TAG, "mdvr id in response thread: %s, length: %d", mdvr_id, g_msg->mdvr_id_length);
    LOG_I(TAG, "cloud_mdvr id in response thread: %s, length: %d", cloud_mdvr_id, g_msg->cloud_mdvr_id_length);

    json_t *json_1 =json_object();
    json_object_set_new( json_1, "cmd", json_string("res_mdvr_check"));
    if (response) {
        json_object_set_new( json_1, "feature_enabled", json_boolean(1));
    }
    else {
        json_object_set_new( json_1, "feature_enabled", json_boolean(0));
    }

    if (cloud_override) {
        json_object_set_new( json_1, "cloud_override", json_boolean(1));
    }
    else {
        json_object_set_new( json_1, "cloud_override", json_boolean(0));
    }

    if (cloud_override_value) {
        json_object_set_new( json_1, "cloud_override_value", json_boolean(1));
    }
    else {
        json_object_set_new( json_1, "cloud_override_value", json_boolean(0));
    }
    json_object_set_new( json_1, "mdvr_id", json_string(mdvr_id));
    json_object_set_new( json_1, "cloud_mdvr_id", json_string(cloud_mdvr_id));

    json_object_set_new( json_1, "seq", json_integer(sequence_response));
    res_json = json_dumps(json_1,0);

    if(res_json == NULL){
       LOG_E(TAG,"JSON creation failed for mdvr wifi strength response\n");
       return false;
    }

    LOG_I(TAG, "json dump for response is\n");
    LOG_I(TAG, "%s\n", res_json);

    // push the json to app

    write_socket(res_json, strlen(res_json));

    free(res_json);
    json_decref(json_1);
    return true;
}

bool mdvr_sdcard_response_to_app() {
    char *res_json = NULL;
    int sequence_response, sdcard_size;
    bool response = false;

    response_for_mdvr_sdcard_check *g_msg = (response_for_mdvr_sdcard_check *)msg_response->get_buffer();
    sequence_response = g_msg->sequence_no;
    response = g_msg->response;
    sdcard_size = g_msg->sdcard_size_kb;

    json_t *json_1 =json_object();
    json_object_set_new( json_1, "cmd", json_string("res_mdvr_sdcard_check"));
    if (response) {
        json_object_set_new( json_1, "status", json_boolean(1));
    }
    else {
        json_object_set_new( json_1, "status", json_boolean(0));
    }

    json_object_set_new( json_1, "sdcard_size", json_integer(sdcard_size));
    json_object_set_new( json_1, "seq", json_integer(sequence_response));
    res_json = json_dumps(json_1,0);

    if(res_json == NULL){
       LOG_E(TAG,"JSON creation failed for mdvr wifi strength response\n");
       return false;
    }

    LOG_I(TAG, "json dump for response is\n");
    LOG_I(TAG, "%s\n", res_json);

    // push the json to app

    write_socket(res_json, strlen(res_json));

    free(res_json);
    json_decref(json_1);
    return true;
}

bool mdvr_wifi_strength_response_to_app() {
    char *res_json = NULL;
    int sequence_response, wifi_quality, wifi_strength;
    bool response = false, ext_cam_enabled = false;

    response_for_mdvr_wifi_strength *g_msg = (response_for_mdvr_wifi_strength *)msg_response->get_buffer();
    sequence_response = g_msg->sequence_no;
    response = g_msg->response;
    wifi_quality = g_msg->wifi_quality;
    wifi_strength = g_msg->wifi_strength;
    ext_cam_enabled = g_msg->feature_enabled;

    json_t *json_1 =json_object();
    json_object_set_new( json_1, "cmd", json_string("res_mdvr_wifi_strength"));
    if (response) {
        json_object_set_new( json_1, "res_status", json_boolean(1));
    }
    else {
        json_object_set_new( json_1, "res_status", json_boolean(0));
    }

    json_object_set_new( json_1, "seq", json_integer(sequence_response));
    json_object_set_new( json_1, "quality", json_integer(wifi_quality));
    json_object_set_new( json_1, "strength", json_integer(wifi_strength));
    if(!ext_cam_enabled) {
        json_object_set_new( json_1, "feature_resp", json_string("mdvr_feature_disabled"));
    } else {
        json_object_set_new( json_1, "feature_resp", json_string("mdvr_feature_enabled"));
    }
    res_json = json_dumps(json_1,0);

    if(res_json == NULL){
       LOG_E(TAG,"JSON creation failed for mdvr wifi strength response\n");
       return false;
    }


    LOG_I(TAG, "json dump for response is\n");
    LOG_I(TAG, "%s\n", res_json);

    // push the json to app

    write_socket(res_json, strlen(res_json));

    free(res_json);
    json_decref(json_1);
    return true;
}

bool obd_extended_check_response_to_app() {
    char *res_str = NULL;
    int sequence_response = 0;
    bool msg_status = false;
    bool status = false;

    response_for_obd_extended_check *g_msg = (response_for_obd_extended_check *)msg_response->get_buffer();
    sequence_response = g_msg->sequence_no;
    msg_status = g_msg->status;
    json_error_t error;

    json_t *json_resp = json_object();
    json_object_set_new(json_resp, "cmd", json_string("res_obd_extended_check"));
    json_object_set_new(json_resp, "seq", json_integer(sequence_response));

    if (msg_status) {
        LOG_I(TAG, "obd extended response msg_status is true");
        json_t *root = json_load_file(obd_info_file.c_str(), 0, &error);

        if (nullptr != root) {
            json_t *obd_info               = json_object_get(root, "obd_info");

            if (nullptr != obd_info) {
                json_t *error_info_array   = json_object_get(obd_info, "error");

                if (json_is_array(error_info_array)) {
                    LOG_I(TAG, "obd_info error_info_array size is : %ld", json_array_size(error_info_array));

                    json_t *error_info_array_out = json_array();
                    for(auto itr = 0; itr < json_array_size(error_info_array); ++itr) {
                        json_t *error_info  = json_array_get(error_info_array, itr);

                        if (nullptr != error_info) {
                            json_t *type        = json_object_get(error_info, "type");
                            json_t *codes_array = json_object_get(error_info, "codes");

                            if (json_is_string(type)) {
                                LOG_I(TAG, "obd_info error type: %s", json_string_value(type));
                            } else {
                                LOG_E(TAG, "obd_info error type is not a string value");
                                continue; // continuing since the type cannot be decoded
                            }

                            if (json_is_array(codes_array)) {
                                LOG_I(TAG, "obd_info codes_array size is : %ld", json_array_size(codes_array));
                                for(auto err_itr = 0; err_itr < json_array_size(error_info_array); ++err_itr) {
                                    json_t *code  = json_array_get(codes_array, err_itr);
                                    if (json_is_string(code)) {
                                        LOG_I(TAG, "obd_info error code: %s", json_string_value(code));
                                    } else {
                                        LOG_E(TAG, "obd_info error code is not a string value");
                                    }
                                }
                                json_t *error_info_out = json_object();
                                json_object_set(error_info_out, "type", type);
                                json_object_set(error_info_out, "codes", codes_array);
                                json_array_append(error_info_array_out, error_info_out);
                            } else {
                                LOG_E(TAG, "obd_info codes_array is not array");
                            }
                        }
                    }
                    json_object_set_new(json_resp, "error", error_info_array_out);
                } else {
                    LOG_E(TAG, "obd_info error_info is not array");
                    json_object_set_new(json_resp, "error", json_array());
                }
            }
            json_decref(root);
        } else {
            LOG_E(TAG, "json_load_file error: %s", error.text);
        }
    } else {
        LOG_I(TAG, "obd extended response msg_status is false");
        json_object_set_new(json_resp, "error", json_array());
    }

    res_str = json_dumps(json_resp, 0);

    if (NULL != res_str) {
        LOG_I(TAG, "json dump for response is:\n");
        LOG_I(TAG, " %s\n", res_str);

        // push the json to app
        write(connfd, res_str, strlen(res_str));
        free(res_str);
        status = true;
    } else {
        LOG_E(TAG, "JSON creation failed for obd extended response\n");
    }

    json_decref(json_resp);
    return status;
}

bool obd_protocol_response_to_app() {
    char *res_str = NULL;
    int sequence_response = 0;
    bool msg_status = false;
    bool status = false;

    response_for_obd_protocol *g_msg = (response_for_obd_protocol *)msg_response->get_buffer();
    sequence_response = g_msg->sequence_no;
    msg_status = g_msg->status;
    json_error_t error;

    json_t *json_resp = json_object();
    json_object_set_new(json_resp, "cmd", json_string("res_obd_diagnostic"));
    json_object_set_new(json_resp, "seq", json_integer(sequence_response));

    if (msg_status) {
        LOG_I(TAG, "obd response msg_status is true");
        json_t *root = json_load_file(obd_info_file.c_str(), 0, &error);

        if (nullptr != root) {
            json_t *obd_info               = json_object_get(root, "obd_info");

            if (nullptr != obd_info) {
                json_t *bootloader         = json_object_get(obd_info, "bootloader");
                json_t *firmware           = json_object_get(obd_info, "firmware");
                json_t *protocol           = json_object_get(obd_info, "protocol");
                json_t *vin_info           = json_object_get(obd_info, "vin_info");
                json_t *error_info_array   = json_object_get(obd_info, "error");
                json_t *eld_supported   = json_object_get(obd_info, "eld_supported");

                if (json_is_boolean(bootloader)) {
                    LOG_I(TAG, "obd_info bootloader: %d", json_boolean_value(bootloader));
                    json_object_set(json_resp, "bootloader", bootloader);
                } else {
                    LOG_E(TAG, "obd_info bootloader is not a boolean value");
                    json_object_set_new(json_resp, "bootloader", json_boolean(0));
                }

                if (json_is_string(firmware)) {
                    LOG_I(TAG, "obd_info firmware: %s", json_string_value(firmware));
                    json_object_set(json_resp, "firmware", firmware);
                } else {
                    LOG_E(TAG, "obd_info firmware is not a string value");
                    json_object_set_new(json_resp, "firmware", json_string("None"));
                }

                if (json_is_string(protocol)) {
                    LOG_I(TAG, "obd_info protocol: %s", json_string_value(protocol));
                    json_object_set(json_resp, "obd_protocol", protocol);
                } else {
                    LOG_E(TAG, "obd_info protocol is not a string value");
                    json_object_set_new(json_resp, "obd_protocol", json_string("None"));
                }

                if (json_is_string(vin_info)) {
                    LOG_I(TAG, "obd_info vin_info: %s", json_string_value(vin_info));
                    json_object_set(json_resp, "vin_no", vin_info);
                } else {
                    LOG_E(TAG, "obd_info vin_info is not a string value");
                    json_object_set_new(json_resp, "vin_no", json_string("None"));
                }
                if (json_is_boolean(eld_supported)) {
                    LOG_I(TAG, "obd_info eld_supported: %d", json_boolean_value(eld_supported));
                    json_object_set(json_resp, "eld_supported", eld_supported);
                } else {
                    LOG_E(TAG, "obd_info bootloader is not a boolean value");
                    json_object_set_new(json_resp, "eld_supported", json_boolean(0));
                }

                if (json_is_array(error_info_array)) {
                    LOG_I(TAG, "obd_info error_info_array size is : %ld", json_array_size(error_info_array));

                    json_t *error_info_array_out = json_array();
                    for(auto itr = 0; itr < json_array_size(error_info_array); ++itr) {
                        json_t *error_info  = json_array_get(error_info_array, itr);

                        if (nullptr != error_info) {
                            json_t *type        = json_object_get(error_info, "type");
                            json_t *codes_array = json_object_get(error_info, "codes");

                            if (json_is_string(type)) {
                                LOG_I(TAG, "obd_info error type: %s", json_string_value(type));
                            } else {
                                LOG_E(TAG, "obd_info error type is not a string value");
                                continue; // continuing since the type cannot be decoded
                            }

                            if (json_is_array(codes_array)) {
                                LOG_I(TAG, "obd_info codes_array size is : %ld", json_array_size(codes_array));
                                for(auto err_itr = 0; err_itr < json_array_size(error_info_array); ++err_itr) {
                                    json_t *code  = json_array_get(codes_array, err_itr);
                                    if (json_is_string(code)) {
                                        LOG_I(TAG, "obd_info error code: %s", json_string_value(code));
                                    } else {
                                        LOG_E(TAG, "obd_info error code is not a string value");
                                    }
                                }
                                json_t *error_info_out = json_object();
                                json_object_set(error_info_out, "type", type);
                                json_object_set(error_info_out, "codes", codes_array);
                                json_array_append(error_info_array_out, error_info_out);
                            } else {
                                LOG_E(TAG, "obd_info codes_array is not array");
                            }
                        }
                    }
                    json_object_set_new(json_resp, "error", error_info_array_out);
                } else {
                    LOG_E(TAG, "obd_info error_info is not array");
                    json_object_set_new(json_resp, "error", json_array());
                }
            }
            json_decref(root);
        } else {
            LOG_E(TAG, "json_load_file error: %s", error.text);
        }
    } else {
        LOG_I(TAG, "obd response msg_status is false");
        json_object_set_new(json_resp, "bootloader", json_boolean(0));
        json_object_set_new(json_resp, "firmware", json_string("None"));
        json_object_set_new(json_resp, "obd_protocol", json_string("None"));
        json_object_set_new(json_resp, "vin_no", json_string("None"));
        json_object_set_new(json_resp, "eld_supported", json_boolean(0));
        json_object_set_new(json_resp, "error", json_array());
    }

    res_str = json_dumps(json_resp, 0);

    if (NULL != res_str) {
        LOG_I(TAG, "json dump for response is:\n");
        LOG_I(TAG, " %s\n", res_str);

        // push the json to app
        write(connfd, res_str, strlen(res_str));
        free(res_str);
        status = true;
    } else {
        LOG_E(TAG, "JSON creation failed for obd diagnostic response\n");
    }

    json_decref(json_resp);
    return status;
}

bool eld_protocol_response_to_app() {
    char *res_str = NULL;
    int sequence_response = 0;
    bool status = false;

    response_for_eld_check *g_msg = (response_for_eld_check *)msg_response->get_buffer();
    sequence_response = g_msg->sequence_no;
    json_error_t error;

    json_t *json_resp = nullptr;
    json_t *root = nullptr;

    if(g_msg->status) {
        root = json_load_file(g_msg->eld_file_path, 0, &error);
        if (nullptr != root) {
            LOG_I(TAG, "eld response msg_status is true, loaded eld info from file");
        } else {
            LOG_E(TAG, "json_load_file error: %s", error.text);
        }
    } else {
        LOG_E(TAG, "Status is false, sending error values");
    }
    if (root == nullptr) {
        root = json_object();
    }

    if (json_object_get(root, "eld_info") == NULL) {
        json_resp = json_object();
        json_object_set_new(json_resp, "protocol", json_string("None"));
        json_object_set_new(json_resp, "vin_info", json_string("None"));
        json_object_set_new(json_resp, "speed", json_integer(-1));
        json_object_set_new(json_resp, "rpm", json_integer(-1));
        json_object_set_new(json_resp, "e_hrs", json_integer(-1));
        json_object_set_new(json_resp, "odo", json_integer(-1));
        json_object_set_new(json_resp, "ignition", json_integer(-1));
        json_object_set_new(json_resp, "CAN_bus_state", json_integer(-1000));
        json_object_set_new(root, "eld_info", json_resp);
    }

    json_object_set_new(root, "cmd", json_string("res_eld_diagnostic"));
    json_object_set_new(root, "seq", json_integer(sequence_response));
    json_object_set_new(root, "res_status", json_boolean(g_msg->status));
    json_object_set_new(root, "error_code", json_integer(g_msg->error_code));

    res_str = json_dumps(root, 0);

    if (NULL != res_str) {
        LOG_I(TAG, "json dump for response is:\n");
        LOG_I(TAG, " %s\n", res_str);

        // push the json to app
        write(connfd, res_str, strlen(res_str));
        free(res_str);
        status = true;
    } else {
        LOG_E(TAG, "JSON creation failed for obd diagnostic response\n");
    }

    file_delete(g_msg->eld_file_path);
    if (root != nullptr) {
        json_decref(root);
    }
    return status;
}

bool diagnostic_response_to_app(){

    char *res_diag = NULL;

    int sequence_response;
    bool response;

    response_for_diagnostic *g_msg = (response_for_diagnostic *)msg_response->get_buffer();
    sequence_response = g_msg->sequence_no;
    response = g_msg->response;


    json_t *json =json_object();
    json_object_set_new( json, "cmd", json_string("res_diagnostic"));
    json_object_set_new( json, "res_status", json_boolean(JSON_TRUE));

    json_object_set_new( json, "seq", json_integer(sequence_response));


    json_t *res = json_object();

    json_object_set_new( res, "deviceID", json_string(device_ssid.c_str()));
    json_object_set_new( res, "appVer", json_string(app_version.c_str()));

    if (g_msg->sdcard_reqested) {
        LOG_I(TAG, "Adding sdcard status to response");
        json_object_set_new( res, "sdcard", json_boolean(g_msg->sdcard_status));
    }
    if (g_msg->simcard_status_requested) {
        LOG_I(TAG, "Adding simcard status to response");
        json_object_set_new( res, "simcard", json_boolean(g_msg->simcard_status));
    }
    if (g_msg->fan_reqested) {
        LOG_I(TAG, "Adding fan status to response");
        json_object_set_new( res, "fan", json_boolean(g_msg->fan_status));
    }
    if (g_msg->internet_status_requested) {
        LOG_I(TAG, "Adding internet status to response");
        json_object_set_new( res, "internet", json_boolean(g_msg->internet_status));
    }
    if (g_msg->ignition_requested) {
        LOG_I(TAG, "Adding ignition status to response");
        json_object_set_new( res, "ignition", json_boolean(g_msg->ignition_status));
    }
    if (g_msg->gps_requested) {
        LOG_I(TAG, "Adding gps status to response");
        json_object_set_new( res, "gps", json_boolean(g_msg->gps_status));
        json_object_set_new( res, "gpsInfo", json_string(g_msg->gps_info));
    }
    if (g_msg->signal_requested) {
        LOG_I(TAG, "Adding signal status to response");
        json_t *signal_res = json_object();
        json_object_set_new( signal_res, "valid", json_boolean(g_msg->signal_data.valid));
        if(g_msg->signal_data.valid) {
            json_object_set_new( signal_res, "isLTE", json_boolean(g_msg->signal_data.is_lte));
            json_object_set_new( signal_res, "isAttached", json_boolean(g_msg->signal_data.is_attached));
            json_object_set_new( signal_res, "isOnline", json_boolean(g_msg->signal_data.is_online));
            json_object_set_new( signal_res, "band", json_integer(g_msg->signal_data.band));
            json_object_set_new( signal_res, "channel", json_integer(g_msg->signal_data.channel));
            if(g_msg->signal_data.is_lte) {
                json_object_set_new( signal_res, "bandwidth", json_real(g_msg->signal_data.bw));
                json_object_set_new( signal_res, "rssi", json_real(g_msg->signal_data.rssi));
                json_object_set_new( signal_res, "rsrp", json_real(g_msg->signal_data.rsrp));
                json_object_set_new( signal_res, "rsrq", json_real(g_msg->signal_data.rsrq));
                json_object_set_new( signal_res, "sinr", json_real(g_msg->signal_data.sinr));
            } else {
                json_object_set_new( signal_res, "rxmRSSI0", json_real(g_msg->signal_data.rxm_rssi0));
                json_object_set_new( signal_res, "rxdRSSI0", json_real(g_msg->signal_data.rxd_rssi0));
                json_object_set_new( signal_res, "rxmRSSI1", json_real(g_msg->signal_data.rxm_rssi1));
                json_object_set_new( signal_res, "rxdRSSI1", json_real(g_msg->signal_data.rxd_rssi1));
            }
        }
        json_object_set_new( res, "signalInfo", signal_res);
    }

    if (g_msg->imei_requested) {
        LOG_I(TAG, "Adding imei status to response");
        json_object_set_new( res, "is_imei", json_boolean(g_msg->is_imei));
        if (g_msg->is_imei) {
            json_object_set_new( res, "imei_no", json_string(g_msg->imei_no));
        }
    }

    if (g_msg->iccid_requested) {
        LOG_I(TAG, "Adding iccid status to response");
        json_object_set_new( res, "is_iccid", json_boolean(g_msg->is_iccid));
        if (g_msg->is_iccid) {
            json_object_set_new( res, "iccid_no", json_string(g_msg->iccid_no));
        }
    }

    json_object_set_new( json, "res", res);

    res_diag = json_dumps(json,0);

    if(res_diag == NULL){
        LOG_E(TAG,"JSON creation failed for stream cam1 command\n");
        return false;
    }

    LOG_I(TAG, "json dump for response is\n");
    pthread_mutex_lock(&log_handle_mutex);
    printf("%s\n", res_diag);
    pthread_mutex_unlock(&log_handle_mutex);

            // push the json to app

    write_socket(res_diag, strlen(res_diag));

    free(res_diag);
    json_decref(json);
    return true;
}

bool vehicle_config_response_to_app() {
    bool ret = false;
    json_t *res_json =json_object();
    char *resp = NULL;

    do {
        response_vehicle_config_msg_t *g_msg = (response_vehicle_config_msg_t *)msg_response->get_buffer();
        int sequence_response = g_msg->sequence_no;

        json_t *error_json = json_loads(g_msg->error_code, 0, NULL);
        if(error_json == NULL){
            LOG_E(TAG,"failed to load error response\n");
            error_json = json_object();
        }

        json_object_set_new( res_json, "cmd", json_string("res_set_veh_data"));
        if(g_msg->response) {   
            json_object_set_new( res_json, "response", json_boolean(1));
        } else {
            json_object_set_new( res_json, "response", json_boolean(0));
        }
        json_object_set_new( res_json, "seq", json_integer(sequence_response));
        json_object_set_new( res_json, "error_code", error_json);
        resp = json_dumps(res_json,0);

        if(resp == NULL){
        LOG_E(TAG,"JSON creation failed for mdvr config response\n");
        break;
        }

        LOG_I(TAG, "json dump for response is\n");
        LOG_I(TAG, "%s\n", resp);

        // push the json to app
        write_socket(resp, strlen(resp));

        ret = true;
    } while (false);

    free(resp);
    json_decref(res_json);
    return ret;
}

bool pair_unpair_accessory_response_to_app()
{
    bool ret = false;
    json_t *res_json =json_object();
    do {
        response_pair_unpair_accessory_msg_t *g_msg = (response_pair_unpair_accessory_msg_t *)msg_response->get_buffer();
        int sequence_response = g_msg->sequence_no;
        
        // create json for response
        if(g_msg->pair_accessory) {
            json_object_set_new( res_json, "cmd", json_string("res_pair"));
        } else {
            json_object_set_new( res_json, "cmd", json_string("res_unpair"));
        }

        if(g_msg->response) {
            json_object_set_new( res_json, "res_status", json_boolean(1));
        } else {
            json_object_set_new( res_json, "res_status", json_boolean(0));
        }

        json_object_set_new( res_json, "seq", json_integer(sequence_response));

        json_t *error_json = json_loads(g_msg->resp_json_char, 0, NULL);
        if(error_json == NULL){
            LOG_E(TAG,"failed to load error response\n");
            error_json = json_object();
        }
        json_object_update_missing( res_json, error_json);

        char *resp = json_dumps(res_json,0);

        if(resp == NULL){
            LOG_E(TAG,"JSON creation failed for pair unpair accessory response\n");
            break;
        }

        LOG_I(TAG, "json dump for response is\n");
        LOG_I(TAG, "%s\n", resp);

        // push the json to app
        write_socket(resp, strlen(resp));
        
        free(resp);
        
        ret = true;
    } while (false);

    json_decref(res_json);
    return ret;
}

bool fetch_all_accessory_response_to_app(bool response_status, string paired_accessories) {
    bool ret = false;
    json_t *res_json =json_object();
    json_t *error_json = json_object();
    do {
        response_fetch_all_accessory_msg_t *g_msg = (response_fetch_all_accessory_msg_t *)msg_response->get_buffer();
        
        // create json for response
        int sequence_response = g_msg->sequence_no;
        json_object_set_new( res_json, "cmd", json_string("res_fetch_all"));
        json_object_set_new( res_json, "seq", json_integer(sequence_response));

        if (response_status) {
            json_object_set_new( res_json, "res_status", json_boolean(1));
        } else {
            json_object_set_new( res_json, "res_status", json_boolean(0));
            json_object_set_new(error_json, "err", json_integer(1));
            json_object_set_new(error_json, "msg", json_string("unable to fetch accessory"));
        }

        json_t *accessory_data = json_loads(paired_accessories.c_str(), 0, NULL);
        if(accessory_data == NULL){
                LOG_E(TAG,"failed to load paired accessories\n");
                accessory_data = json_array();
        }

        json_object_set_new( res_json, "error", error_json);
        json_object_update_missing( res_json, accessory_data);

        char *resp = json_dumps(res_json,0);
        if(resp == NULL){
            LOG_E(TAG,"JSON creation failed for fetch all accessory response\n");
            break;
        }

        LOG_I(TAG, "json dump for response is\n");
        LOG_I(TAG, "%s\n", resp);

        // push the json to app
        write_socket(resp, strlen(resp));

        // free the memory
        free(resp);
        ret = true;
    } while (false);
    
    json_decref(res_json);
    return ret;
}

bool check_vbus_connection_response_to_app() {
    char *res_json = NULL;
    int sequence_response;
    bool response = false;

    response_check_vbus_conn_msg_t *g_msg = (response_check_vbus_conn_msg_t *)msg_response->get_buffer();
    sequence_response = g_msg->sequence_no;
    response = g_msg->vbus_connected;

    json_t *json_1 =json_object();
    json_object_set_new( json_1, "cmd", json_string("res_check_vdr_conn_status"));
    if (response) {
        json_object_set_new( json_1, "connected", json_boolean(1));
    }
    else {
        json_object_set_new( json_1, "connected", json_boolean(0));
    }

    json_object_set_new( json_1, "seq", json_integer(sequence_response));

    // hardcoding error_code and res_status to maintain consistency with other commands
    json_object_set_new( json_1, "error_code", json_integer(0));
    json_object_set_new( json_1, "res_status", json_boolean(1));

    res_json = json_dumps(json_1,0);

    if(res_json == NULL){
       LOG_E(TAG,"JSON creation failed for vbus connection response\n");
       return false;
    }

    LOG_I(TAG, "json dump for response is\n");
    LOG_I(TAG, "%s\n", res_json);

    // push the json to app

    write_socket(res_json, strlen(res_json));

    free(res_json);
    json_decref(json_1);
    return true;
}

void delete_temp_files(){

     string cmd = "";
     cmd = "rm " +  command_file;
     system(cmd.c_str());
     sleep(1);
     cmd = "";
     cmd = "rm " + response_file;
     system(cmd.c_str());
}

bool generic_command_response_app(){

    char *res_json = NULL;
    int sequence_response=0;
    bool response =false;
    int i =0;

    response_for_generic_cmd *g_msg = (response_for_generic_cmd *)msg_response->get_buffer();
    sequence_response = g_msg->sequence_no;
    response = g_msg->response;
    int cmd_len = g_msg->device_cmd_len;

    json_t *json_1 =json_object();

    //read the response from response file generated after command exec

    if (g_msg->generic_cmd_enable_flag){

     std::stringstream buffer;
     pthread_mutex_lock(&res_lock);
     std::ifstream cmd_file(response_file.c_str());
     buffer.str("");

     if ( cmd_file )
     {
            buffer << cmd_file.rdbuf();

            cmd_file.close();
      }

      string buffer_1 = "";
      buffer_1 = buffer.str();

      pthread_mutex_unlock(&res_lock);

       delete_temp_files();

      printf("\n");
      printf("complete response : %s\n", buffer_1.c_str());

       buffer_1.resize(max_app_strlen);

       json_object_set_new( json_1, "response_generic_cmd", json_string(buffer_1.c_str()));

       json_object_set_new( json_1, "seq", json_integer(sequence_response));
       if (g_msg->response)
          json_object_set_new( json_1, "response", json_boolean(1));
       else
          json_object_set_new( json_1, "response", json_boolean(0));
     }

    else {
        LOG_I(TAG, "not supported resp send for generic cmd");

        json_object_set_new( json_1, "response_generic_cmd", json_string("not supported from cloud"));
        json_object_set_new( json_1, "response", json_boolean(0));
        json_object_set_new( json_1, "seq", json_integer(sequence_response));
    }

    res_json = json_dumps(json_1,0);
    if(res_json == NULL){
       LOG_E(TAG,"JSON creation failed generic cmd response");
       json_decref(json_1);
       return false;
    }


    LOG_I(TAG, "json dump for response is\n");
    LOG_I(TAG, "%s\n", res_json);

    // push the json to app

    write_socket(res_json, strlen(res_json));

    free(res_json);
    json_decref(json_1);
    return true;

}

bool handle_fetch_all_accessory(string &fetch_all_data) {
    bool ret = false;
    json_error_t json_error;
    json_t *root, *response_json, *data_array = json_object();
    nd::device::AccessoryDB accessory_db;

    do {
        // loading json
        root = json_load_file(device_capabilities_file.c_str(), 0, &json_error);
        if (root == NULL) {
            LOG_E(TAG, "json parsing failed");
            break;
        }

        // getting accessory type from json
        json_t *accessory_list_json = json_object_get(root, "accessories");
        if (accessory_list_json == NULL) {
            LOG_E(TAG, "data key not present in json");
            break;
        }
        size_t accessory_list_size = json_array_size(accessory_list_json);
        
        // getting data from db
        bool error_occured = false;
        for (size_t i = 0; i < accessory_list_size; i++) {

            // getting accessory type from json
            if(json_is_string(json_array_get(accessory_list_json, i)) == false) {
                LOG_E(TAG, "accessory_type not present in json");
                error_occured = true;
                continue;
            }
            string accessory_type = json_string_value(json_array_get(accessory_list_json, i));
            
            // getting accessory data from db for accessory type
            vector<nd::device::Accessory> accessory_data;
            auto error = accessory_db.GetAllOfType(accessory_type, accessory_data);
            if(error.first != 0) {
                if(error.first == 1 && (error.second.find("no such table: ACCESSORY") != string::npos)) {
                    continue;
                }
                LOG_E(TAG, "accessory data not fetched from db ERROR CODE: %d , %s", error.first, error.second.c_str());
                error_occured = true;
                continue;
            }

            // creating json array for accessory data
            json_t *accessory_json = json_array();
            for(auto const &accessory : accessory_data) {
                json_t *data = json_loads(accessory.data_.c_str(), 0, &json_error);
                if (data == NULL) {
                    LOG_E(TAG, "json parsing failed for accessory type: %s , accessory id: %s", accessory.id_type_.first.c_str(), accessory.id_type_.second.c_str());
                    error_occured = true;
                    continue;
                }
                if(json_array_append(accessory_json, data) != 0) {
                    LOG_E(TAG, "json array append failed");
                    error_occured = true;
                    continue;
                }
            }
            // adding accessory data to data array
            if(json_array_size(accessory_json) > 0) {
                json_object_set(data_array, accessory_type.c_str(), accessory_json); 
            }
        }



		do {
			// all data 
            static constexpr char NaStr[] = "NA";
			string ip = NaStr;
			string serial_number = NaStr;
			string mode = NaStr;
			string ssid = NaStr;
			string password = NaStr;

			Config_parser mdvr_config_parser (mdvr_config_file_path);
			if (mdvr_config_parser.getParseStatus() == false) {
				LOG_E(TAG, "config file parsing failed not adding ssid and password");
			} else {
				ssid = mdvr_config_parser.getConfig("ext_cam_config", "ssid", "NA");
				password = mdvr_config_parser.getConfig("ext_cam_config", "password", "NA");
			}

			response_fetch_all_accessory_msg_t *g_msg = (response_fetch_all_accessory_msg_t *)msg_response->get_buffer();
			serial_number = g_msg->ext_cam_info.serialNumber;
			if (serial_number == "NA") {
				LOG_C(TAG, "MDVR data is not available in db and serial number is NA");
				LOG_C(TAG, "MDVR is not configured till now");
			} else {
				serial_number = g_msg->ext_cam_info.serialNumber;
				ip = g_msg->ext_cam_info.ip;
				mode = g_msg->ext_cam_info.mode;
			}
			LOG_I(TAG, "MDVR serial number: %s, IP: %s, Mode: %s", serial_number.c_str(), ip.c_str(), mode.c_str());

			json_t* dhub_data_array = nullptr;
			dhub_data_array = json_object_get(data_array, "DHUBX");
			if (dhub_data_array == NULL) {
				LOG_I(TAG, "dhub data array is NULL");
				dhub_data_array = json_array();
                if (!dhub_data_array) {
                    LOG_E(TAG, "json array creation failed");
                    break;
                }
			}
			json_t* dhub_data = nullptr;
			if (json_array_size(dhub_data_array) > 0) {
				LOG_I(TAG, "dhub data array size is : %ld", json_array_size(dhub_data_array));
				dhub_data = json_array_get(dhub_data_array, 0);
			}
			if (dhub_data == NULL) {
				LOG_I(TAG, "dhub data is not present in db");
				dhub_data = json_object();
                if (!dhub_data) {
                    LOG_E(TAG, "json object creation failed");
                    break;
                }
			}

			if (serial_number == "NA") {
				LOG_C(TAG, "MDVR data is not available in db and serial number is NA, ssid is NA");
				LOG_I(TAG, "MDVR is not configured till now");
				json_t* sn = nullptr;
                sn = json_object_get(dhub_data, "SN");
                if(sn == NULL) { 
                    json_decref(dhub_data);
                    json_decref(dhub_data_array);
                }
			} else {
                json_t* sn = nullptr;
                sn = json_object_get(dhub_data, "SN");
                if(sn == NULL) {
                    LOG_I(TAG, "MDVR is configured via old flow");
                    json_object_set(data_array, "DHUBX", dhub_data_array);
                    json_array_append(dhub_data_array, dhub_data);
                } else {
                    LOG_I(TAG, "MDVR is configured via new flow");
                    LOG_I(TAG, "updating dhub data for response");
                }
				// check if ssid and password are present in the json
				if (json_object_get(dhub_data, "dhub_ssid") == NULL) {
					LOG_I(TAG, "dhub ssid is NULL");
					json_object_set_new(dhub_data, "dhub_ssid", json_string(ssid.c_str()));
				} else {
					LOG_I(TAG, "dhub ssid is present : %s", json_string_value(json_object_get(dhub_data, "dhub_ssid")));
				}
				if (json_object_get(dhub_data, "dhub_pwd") == NULL) {
					LOG_I(TAG, "dhub password is NULL");
					json_object_set_new(dhub_data, "dhub_pwd", json_string(password.c_str()));
				} else {
					LOG_I(TAG, "dhub password is present");
				}
				if (json_object_get(dhub_data, "accessory_id") == NULL) {
					LOG_I(TAG, "dhub accessory id is NULL");
					json_object_set_new(dhub_data, "accessory_id", json_string(serial_number.c_str()));
				} else {
					LOG_I(TAG, "dhub accessory id is present : %s", json_string_value(json_object_get(dhub_data, "accessory_id")));
				}
				if (json_object_get(dhub_data, "SN") == NULL) {
					LOG_I(TAG, "dhub SN is NULL");
					json_object_set_new(dhub_data, "SN", json_string(serial_number.c_str()));
				} else {
					LOG_I(TAG, "dhub SN is present : %s", json_string_value(json_object_get(dhub_data, "SN")));
				}
				if (json_object_get(dhub_data, "connection_mode") == NULL) {
					LOG_I(TAG, "dhub connection mode is NULL");
					json_object_set_new(dhub_data, "connection_mode", json_string(g_msg->ext_cam_info.mode));
				} else {
					LOG_I(TAG, "dhub connection mode is present : %s", json_string_value(json_object_get(dhub_data, "connection_mode")));
				}
				if (json_object_get(dhub_data, "ip_address") == NULL) {
					LOG_I(TAG, "dhub ip address is NULL");
					json_object_set_new(dhub_data, "ip_address", json_string(g_msg->ext_cam_info.ip));
				} else {
					LOG_I(TAG, "dhub ip address is present : %s", json_string_value(json_object_get(dhub_data, "ip_address")));
				}
			}
		} while (false);



        // creating json object
        response_json = json_object();
        json_object_set(response_json, "data", data_array);
        if (response_json == NULL) {
            LOG_E(TAG, "json object creation failed");
            break;
        }

        // dumping json object
        char* response_char = json_dumps(response_json, JSON_COMPACT);
        if (response_char == NULL) {
            LOG_E(TAG, "json dump failed for response");
            break;
        }
        LOG_I(TAG, "accessory data in db: %s", response_char);
        fetch_all_data = response_char;
        free(response_char);
        // checking if error occured
        if(error_occured) {
            break;
        }
        ret = true;
    } while (false);
    
    // free json objects
    if(root != NULL) {
        json_decref(root);
    }
    if(response_json != NULL) {
        json_decref(response_json);
    }

    return ret;
}

bool msg_loop_response (){

 while (1){

   LOG_I(TAG, "polling in response thread for message from main\n");
   if( (msg_response = msg_response_q ->receive( )) == NULL ) {
        LOG_I(TAG, "data receive failed\n");
        continue;
   }

   generic_msg_t *g_msg = (generic_msg_t *)msg_response->get_buffer();

   if( NULL == g_msg ) {
         LOG_E(TAG, "msg->get_buffer() returned NULL");
         continue;
    }

   switch (g_msg->msg_type){
       case RESPONSE_INST_CMD_ACK:

            command_ack_response_to_app();
            break;

       case RESPONSE_TESTCONN:

            testconn_response_to_app();
            break;

       case RESPONSE_STREAMING:

            streaming_response_to_app();
            break;

       case RESPONSE_STOP_STREAMING:

            stop_streaming_response_to_app();
            break;

       case RESPONSE_DIAGNOSTIC:

            diagnostic_response_to_app();
            break;
       
       case RESPONSE_IGNITION_STATUS_APP:

            ignition_status_response_to_app();
            break;
       case RESPONSE_MDVR_CONFIG:
  
            mdvr_config_response_to_app();
            break;
       case RESPONSE_MDVR_FEATURE_CHECK:

            mdvr_feature_response_to_app();
            break;
       case RESPONSE_MDVR_SDCARD_CHECK:

            mdvr_sdcard_response_to_app();
            break;
       case RESPONSE_MDVR_WIFI_STRENGTH:

            mdvr_wifi_strength_response_to_app();
            break;
       case RESPONSE_OBD_PROTOCOL_APP:
       
            obd_protocol_response_to_app();
            break;

        case RESPONSE_OBD_EXTENDED_CHECK:
            obd_extended_check_response_to_app();
            break;

        case RESPONSE_ELD_DIAGNOSTIC:
            LOG_I(TAG, "eld response case");
            eld_protocol_response_to_app();
            break;

        case RESPONSE_VEHICLE_CONFIG:
        {
            vehicle_config_response_to_app();
            break;
        }
        case RESPONSE_PAIR_UNPAIR_ACCESSORY:
        {
            pair_unpair_accessory_response_to_app();
            break;
        }
        case RESPONSE_FETCH_ALL_ACCESSORY:
        {
            string paired_accessory_list = "";
            bool ret = handle_fetch_all_accessory(paired_accessory_list);
            fetch_all_accessory_response_to_app(ret, paired_accessory_list);
            break;
        }
        case RESPONSE_CHECK_VBUS_CONN:
        {
            check_vbus_connection_response_to_app();
            break;
        }

       case RESPONSE_GENERIC_COMMAND_APP:

             generic_command_response_app();
             break;

       default:
            LOG_E(TAG, "wromng command\n");

   }
 }

}

void *response_thread_main(void *dummy){

   LOG_I(TAG, "Inside response thread entry function\n");

   msg_loop_response();
 }




