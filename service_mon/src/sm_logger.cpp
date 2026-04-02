/* Copyright (C) 2018 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Mukesh Kumar Singh <mukeshkumar.singh@netradyne.com>, Apr 2018
 */

#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <jansson/jansson.h>
#include <algorithm>
#include <fstream> 
#include <string>

#include "nd_file_utils.h"
#include "log.h"
#include "nd_msg_types.h"

using namespace std;

#define TAG "SM"

static const string ND_DEVICE_REL_PATH = "/home/ubuntu/.nddevice";
static const string json_fname = string(ND_DEVICE_REL_PATH) + "/log/sm_critical_events.json";

static const int ONE_MIN_MS         = 60*1000;

static const string KEY_CODE        = "code";
static const string KEY_AUX_CODE    = "code_aux";
static const string SYSTEM_UPTIME = "sys_uptime";
static const string KEY_TS          = "timestamp";
static const string KEY_PNAME       = "process_name";
static const string KEY_DESC        = "desc";
static const string KEY_COUNT       = "count";

//typedef unsigned long long uint64ll_t;

/*!
  This Function traverse through all the json object in root and check for code.
  If found, it increments the count of that object.
  If not found, it append the object "json_obj" into root.
  Finaly all data is dumped into  file "json_fname".

  @root[in] root of json data before updation.
  @code[in] Error Code. It is a Four digit number. First two digit is for Process code and last two digit is for Error Code.
  @json_obj[in]  input for logging into json_fname.
  @json_fname[out]  json file needs to be updated.
 */
static bool update_json(json_t *root, int code, int aux_code, uint64_t ts, uint64_t sysUpTime, string pname, json_t *json_obj) {
    if( root == NULL ) {
        LOG_E(TAG, "update_json: root is null");
        return false;
    }

    if( json_obj == NULL ) {
        LOG_E(TAG, "update_json: json_obj is null");
        return false;
    }

    json_t *value_obj;
    size_t index;
    bool found = false;
    char *data_to_update = NULL;
    int new_count = 0;

    LOG_I(TAG, "Updating array of size: %d \n", json_array_size(root));

    ts = ts / ONE_MIN_MS;

    //Try to find a match where code, sub-code, process name are matching 
    //and timestamp is with in 1 minute of each other
    json_array_foreach(root, index, value_obj) {
        int tmp_code            = json_integer_value(json_object_get(value_obj, KEY_CODE.c_str())) ;
        int tmp_aux_code        = json_integer_value(json_object_get(value_obj, KEY_AUX_CODE.c_str()));
        uint64_t ts_value_obj   = (uint64_t)(json_integer_value(json_object_get(value_obj, KEY_TS.c_str())));
        ts_value_obj            = ts_value_obj / ONE_MIN_MS;

        string pname_value_obj = "";
        const char* pname_value_obj_ptr  = json_string_value(json_object_get(value_obj, KEY_PNAME.c_str()));
        if(pname_value_obj_ptr != NULL) {
            pname_value_obj = pname_value_obj_ptr;
        }

        LOG_I(TAG,"tmp_code: %d, ts_value_obj: %lld, pname_value_obj: %s \n",tmp_code,  ts_value_obj, pname_value_obj.c_str());
        LOG_I(TAG,"code: %d, ts: %lld, pname: %s \n",code, ts, pname.c_str());

        LOG_I(TAG,"code: %d, tmp_code = %d, tmp_aux_code = %d, aux_code = %d, ts_value_obj = %lld, ts = %lld, \
                   pname_value_obj = %s, pname = %s \n", code, tmp_code, tmp_aux_code, aux_code,
                   ts_value_obj, ts, pname_value_obj.c_str(), pname.c_str());
        if(tmp_code == code && tmp_aux_code == aux_code && \
                ts_value_obj == ts && pname_value_obj == pname && code != SM_E_EXTCAM_VIDEO_FILE_NOT_PRESENT) {
            LOG_I(TAG, "code match found" ); 
            new_count = json_integer_value(json_object_get(value_obj, KEY_COUNT.c_str())) + 1;
            json_object_set(value_obj, KEY_COUNT.c_str(), json_integer(new_count) );

            found = true;
            break;
        }
    }

    //No match found, append new entry
    if(found == false) {
        LOG_I(TAG, "No code match found");   
        json_array_append(root, json_obj);      
    }

    //Dump the array to char array
    data_to_update = json_dumps(root, 0);
    if(data_to_update == NULL) {
        LOG_E(TAG,"JSON dumps failed before writing to json_fname");
        return false;
    }

    //Write this dumped json string into file
    string data_to_updatestr = data_to_update ;
    free(data_to_update);
    data_to_update = NULL;

    std::ofstream write_to_json (json_fname, ofstream::trunc );
    if( write_to_json.is_open() == false ) {
        LOG_E(TAG, "Cannot open files: %s", json_fname.c_str());
        return false;
    }

    write_to_json << data_to_updatestr ;
    write_to_json.close();
    file_fd_sync(json_fname);
    LOG_I(TAG, "returning true");
    return true;
}

static bool create_empty_array_json(string fname) {
    //Create a json array object
    json_t *root_json_data = json_array();
    if( root_json_data == NULL ) {
        LOG_E(TAG, "create_empty_array_json: root_json_data null");
        return false;
    }

    //Create a char array equivalent of an empty json array
    char *data_to_update = json_dumps(root_json_data, 0);
    json_decref(root_json_data);

    if( data_to_update == NULL ) {
        LOG_E(TAG, "create_empty_array_json: data_to_update is null");
        return false;
    }

    //Write this object to json file
    string data_to_updatestr = data_to_update ;
    free(data_to_update);
    data_to_update = NULL;

    std::ofstream write_to_json (fname, ofstream::trunc);
    if( write_to_json.is_open() == false ) {
        LOG_E(TAG, "Failed to open file");
        return false;
    }

    write_to_json << data_to_updatestr ;
    write_to_json.close();
    file_fd_sync(fname);
    LOG_I(TAG, "JSON file created  %s", fname.c_str()  );

    return true;
}

/*!

  @timestamp[in] current time.
  @process_name[in] Process name
  @code[in]  Error Code.
  @desc[in]  Reason of Failure/Line number/file name.
 */
bool add_err_log(uint64_t timestamp, string process_name, int code, int code_aux, string desc, uint64_t sysUpTime) {

    json_t *root_json_data;
    json_error_t error;

    LOG_I (TAG,"timestamp: %llu process: %s code: %d, desc: %s",timestamp, process_name.c_str(), code, desc.c_str()) ;

    if( file_is_present(json_fname ) == false ) { //File not present, create one
        LOG_I(TAG, "File not present, create one");
        if( file_touch(json_fname) == false ) {
            LOG_E(TAG, "Cannot create json file");
            return false;
        }
    }

    LOG_I(TAG, "File exists/created");

    root_json_data = json_load_file(json_fname.c_str(), 0, &error);

    if (root_json_data == NULL) {  //File not present or corrupted

        LOG_I(TAG, "JSON file  %s is not present or corrupt", json_fname.c_str()  );
        if ( create_empty_array_json(json_fname) == false ) {
            LOG_E(TAG, "Empty array json file could not be created");
            return false;
        }

        LOG_I(TAG, "Created an empty array json file");

        //Now file is created and assumed to have empty array

        LOG_I(TAG, "Loading json file for editing");

        //Load the json file
        root_json_data = json_load_file(json_fname.c_str(), 0, &error);
        if( root_json_data == NULL ) {
            LOG_E(TAG, "Error loading json");
            return false;
        }
    }
    LOG_I(TAG, "Loaded json file");

    // create a new object with data in parameter.
    json_t *json_obj = json_object();
    if(json_obj == NULL) {
        LOG_E(TAG, "add_err_log: json_obj is NULL" );
        return false;
    }

    LOG_I(TAG, "Create new json object");
    json_object_set_new( json_obj, KEY_TS.c_str(), json_integer( json_int_t( timestamp ) )) ; 
    json_object_set_new( json_obj, KEY_PNAME.c_str(), json_string( process_name.c_str() )) ; 
    json_object_set_new( json_obj, KEY_CODE.c_str(), json_integer( code ) ) ; 
    json_object_set_new( json_obj, KEY_AUX_CODE.c_str(), json_integer( code_aux ) ) ; 
    json_object_set_new( json_obj, SYSTEM_UPTIME.c_str(), json_integer(json_int_t( sysUpTime ) )) ;
    json_object_set_new( json_obj, KEY_DESC.c_str(), json_string( desc.c_str()) ) ; 
    json_object_set_new( json_obj, KEY_COUNT.c_str(), json_integer( 1 ) ) ; 

    //Now update json file    
    bool ret = update_json(root_json_data, code, code_aux, timestamp, sysUpTime, process_name, json_obj);
    if( ret == false ) {
        LOG_E(TAG, "update_json: returned false");
    } else {
        LOG_I(TAG, "update_json: returned true");
    }

    json_decref(json_obj);
    json_decref(root_json_data);

    return ret;
}

