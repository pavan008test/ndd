/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Arun V <arun.vj@netradyne.com>, February 2016
 */

#include <iostream>
#include <sstream>
#include <cstring>
#include <sys/time.h>

#include <jansson/jansson.h>
#include <nd_msg_types.h>

#include "aws_iot_parser.h"
#include "log.h"
#include "service_utils.h"

#define TAG "IOT-PRSR"

extern NDService *nd_service_obj;
static const int VIDEO_FILE_DURATION_SEC = 60;
static const int VOD_PART_ID_MAX = 99;
static const string KEY_KA_CERTIFICATE_CHECK = "certificate-check-disabled-on-keep-alive-api";
static const string KEY_CONFIG_VALIDATION = "config_validation";

using namespace std;

void add_camera(json_t* cameras, string stream_name, int bitRate, int fps, int camera_index, int error, string resolution) {
    // Create the camera object and set its properties
    json_t* camera = json_object();
    json_object_set_new(camera, "stream_name", json_string(stream_name.c_str()));
    json_object_set_new(camera, "bitRate", json_integer(bitRate));
    json_object_set_new(camera, "fps", json_integer(fps));
    json_object_set_new(camera, "camera", json_integer(camera_index));
    json_object_set_new(camera, "error_code", json_integer(error));
    json_object_set_new(camera, "resolution", json_string(resolution.c_str()));
    // Add the camera object to the cameras array
    json_array_append_new(cameras, camera);
}

string get_response(request_t req, aws_status_t new_status)
{

    string key = req.catalog_id;
    string status;
    string type;
    int start_sec, end_sec;

    switch (new_status)
    {
    case STATUS_NEW:
        status = "new"; // hardcoding
        break;
    case STATUS_RECV:
        status = "recv";
        break;
    case STATUS_ACK:
        status = "ack";
        break;
    case STATUS_DONE:
        status = "done";
        break;
    case STATUS_ERR:
        status = "err";
        break;
    default:
        return "";
    }

    switch (req.type)
    {
    case TYPE_VOD:
        type = "vod";
        break;

    case TYPE_PING:
        type = "ping";
        break;

    case TYPE_LIVESTREAM:
        type = "livestream";
        break;
    case TYPE_DUAL_LIVESTREAM:
        type = "dual_livestream";
        break;
    }

    json_t *jstatus = json_object();

    // type
    json_object_set_new(jstatus, "type", json_string(type.c_str()));

    // status
    json_object_set_new(jstatus, "status", json_string(status.c_str()));

    // Case of trimmed video request
    if ((req.type == TYPE_VOD) && req.trim)
    {
        json_object_set_new(jstatus, "startTime", json_integer(req.start_sec));
        json_object_set_new(jstatus, "endTime", json_integer(req.end_sec));
    }

    // command list
    json_t *cmd_arr = json_array();
    for (vector<string>::iterator iter = req.command_list.begin(), end = req.command_list.end(); iter != end; iter++)
    {
        if (strstr((*iter).c_str(), "upload-logs") != NULL)
        {
            json_object_set_new(jstatus, "startTime", json_integer((int64_t)req.start_sec * 1000));
            json_object_set_new(jstatus, "endTime", json_integer((int64_t)req.end_sec * 1000));
        }
        json_array_append(cmd_arr, json_string((*iter).c_str()));
    }

    if (req.type == TYPE_PING)
    {
        json_object_set_new(jstatus, "ping_id", json_integer(req.id));
        json_object_set(jstatus, "commands", cmd_arr);
    }
    else if (req.type == TYPE_VOD)
    {
        json_object_set_new(jstatus, "request_id", json_integer(req.id));
        json_object_set_new(jstatus, "part_id", json_integer(req.part_id));
        json_object_set(jstatus, "videoList", cmd_arr);
        json_object_set(jstatus, "includeObservation", json_integer(req.upload_observation));
        json_object_set(jstatus, "includeAudio", json_integer(req.upload_audio));
        json_object_set( jstatus, "priority", json_integer (req.priority) );
        if(req.cancelled) {
            json_object_set( jstatus, "cancelled", json_integer (req.cancelled) );
        }
        if (req.quality != "NA")
        {
            json_object_set_new(jstatus, "quality", json_string(req.quality.c_str()));
            LOG_I(TAG, "get_response() quality: %s", req.quality.c_str());
        }
    }
    else if (req.type == TYPE_LIVESTREAM)
    {
        json_object_set_new(jstatus, "req", json_integer(req.id));
        json_object_set_new(jstatus, "duration", json_integer(req.kinesis_duration));
        json_object_set_new(jstatus, "endpoint", json_string(req.kinesis_endpoint.c_str()));
        json_object_set_new(jstatus, "requestTs", json_integer(req.kinesis_requestTS));
        json_object_set_new(jstatus, "bitRate", json_integer(req.kinesis_bitrate));
        json_object_set_new(jstatus, "resolution", json_string(req.kinesis_resolution.c_str()));
        json_object_set_new(jstatus, "camera", json_integer(req.kinesis_camera));
        json_object_set_new(jstatus, "error_code", json_integer(req.error));
        json_object_set_new(jstatus, "fps", json_integer(req.kinesis_fps));
    }
    else if(req.type == TYPE_DUAL_LIVESTREAM){
        json_object_set_new( jstatus, "req", json_integer(req.id) );
        json_object_set_new( jstatus, "duration", json_integer(req.kinesis_duration ));
        json_object_set_new( jstatus, "endpoint", json_string(req.kinesis_endpoint.c_str()));
        json_object_set_new( jstatus, "requestTs", json_integer(req.kinesis_requestTS ));
        json_t* cameras = json_array();
        add_camera(cameras, req.dual_livestream_cameras[0].stream_name, req.dual_livestream_cameras[0].bitrate, req.dual_livestream_cameras[0].fps, req.dual_livestream_cameras[0].camera, req.dual_livestream_cameras[0].error, req.dual_livestream_cameras[0].resolution);
        add_camera(cameras, req.dual_livestream_cameras[1].stream_name, req.dual_livestream_cameras[1].bitrate, req.dual_livestream_cameras[1].fps, req.dual_livestream_cameras[1].camera, req.dual_livestream_cameras[1].error, req.dual_livestream_cameras[1].resolution);
        json_object_set_new( jstatus, "cameras", cameras);
    }
    else
    {
        return "";
    }

    json_t *jcmd = json_object();
    json_object_set(jcmd, key.c_str(), jstatus);

    json_t *req_list = json_object();
    json_object_set(req_list, key.c_str(), json_string(key.c_str()));
    json_object_set(jcmd, "requests", req_list);

    json_t *jstate = json_object();
    json_object_set(jstate, "desired", jcmd);
    json_object_set(jstate, "reported", jcmd);

    json_t *root = json_object();
    json_object_set(root, "state", jstate);

    // json_object_set_new( root, "clientToken", json_string(clientToken) );

    string ret = json_dumps(root, 0);
    json_decref(root);

    return ret;
}

void parse_misc_req_for_key(const string &key, json_t *root, list<misc_req_t> &misc_req, const shadow_type_t shadow_type)
{
    json_t *mask = json_object_get(root, key.c_str());
    if (mask == NULL)
    {
        LOG_I(TAG, "parse_misc_req_for_key:: %s not found", key.c_str());
        return;
    }

    LOG_I(TAG, "parse_misc_req_for_key:: %s", key.c_str());

    // Check to see if request already exists. If so, update with new req
    for (list<misc_req_t>::iterator iter = misc_req.begin(), end = misc_req.end(); iter != end; iter++)
    {
        if (iter->key == key)
        {
            LOG_I(TAG, "Already available, replacing with new req");
            misc_req.erase(iter);
            break;
        }
    }

    misc_req_t req;
    req.cstatus = STATUS_NEW;
    req.type = TYPE_MISC;
    req.key = key;
    req.shadow_type = shadow_type;
    const char *cstr_auth_reqs = json_string_value(mask);
    if (json_is_boolean(mask))
    {
        stringstream ss;
        ss.str("");
        ss << json_boolean_value(mask);
        req.value = ss.str();
        LOG_I(TAG, "%s found: %s", key.c_str(), req.value.c_str());
        misc_req.push_back(req);
    }
    else
    {
        const char *cstr_auth_reqs = json_string_value(mask);
        if (NULL == cstr_auth_reqs)
        {
            LOG_E(TAG, "Shadow parse Error, null is comming from cloud.  key:  %s", key.c_str());
        }
        else
        {
            req.value = cstr_auth_reqs;
            LOG_I(TAG, "%s found: %s", key.c_str(), json_string_value(mask));
            misc_req.push_back(req);
        }
    }
}

bool parse(json_t *root, list<request_t> &req, list<misc_req_t> &misc_req, const shadow_type_t shadow_type)
{
    json_t *reqs = json_null();
    json_t *value;
    const char *key;
    json_error_t error;
    vector<string> requests;
    string catalog_id = "";
    int start_sec = -1, end_sec = -1, part_id = -1;

    if (root == NULL)
    {
        LOG_E(TAG, "root is NULL inside parse func");
        return false;
    }
    reqs = json_object_get(root, "requests");

    if (reqs != NULL)
    {

        json_object_foreach(reqs, key, value)
        {
            const char *cstr_value = json_string_value(value);
            if (NULL == cstr_value)
            {
                LOG_E(TAG, "Shadow parse Error, null is comming from cloud.  key:  %s", key);
                return false;
            }
            requests.push_back(cstr_value);
        }

        for (vector<string>::iterator iter = requests.begin(), end = requests.end(); iter != end; iter++)
        {
            value = json_object_get(root, (*iter).c_str());
            catalog_id = *iter;

            if (value == NULL)
            {
                LOG_E(TAG, "request - %s doesn't have value", catalog_id.c_str());
                continue;
            }

            json_t *type = json_object_get(value, "type");

            json_t *commands = NULL;
            json_t *status = json_object_get(value, "status");

            if (type == NULL || status == NULL)
            {
                goto misc_req;
            }

            const char *cstr_type = json_string_value(type);
            if (NULL == cstr_type)
            {
                LOG_E(TAG, "Shadow parse Error, null is comming from cloud.  key:  %s", key);
                return false;
            }
            string t = cstr_type;

            request_t new_req;
            new_req.count = 0;
            new_req.trim = true;
            new_req.upload_observation = true;
            new_req.upload_audio = false;
            new_req.start_sec = 0;
            new_req.end_sec = 0;
            new_req.part_id = 0;

            json_t *id;
            json_t *req_part_id = NULL;
            json_t *start_time_sec = NULL, *end_time_sec = NULL;
            json_t *priority = NULL, *cancelled = NULL; //vod specific

            if (t == "ping")
            {
                id = json_object_get(value, "ping_id");
                commands = json_object_get(value, "commands");
                new_req.type = TYPE_PING;
            }
            else if (t == "vod")
            {
                id = json_object_get(value, "request_id");
                commands = json_object_get(value, "videoList");
                new_req.type = TYPE_VOD;
                new_req.vod_id = (*iter).c_str();
                start_time_sec = json_object_get(value, "startTime");
                end_time_sec = json_object_get(value, "endTime");
                priority = json_object_get (value, "priority");
                cancelled = json_object_get (value, "cancelled");
                req_part_id = json_object_get(value, "part_id");
                json_t *json_ptr_upload_observation = json_object_get(value, "includeObservation");
                new_req.upload_observation = json_integer_value(json_ptr_upload_observation);
                LOG_I(TAG, "parse(): includeObservation : new_req.upload_observation: %d ", new_req.upload_observation);
                json_t *json_ptr_upload_audio = json_object_get(value, "includeAudio");
                new_req.upload_audio = json_integer_value(json_ptr_upload_audio);
                json_t *json_ptr_upload_alert_id = json_object_get(value, "alert_id");
                if (json_ptr_upload_alert_id == NULL)
                {
                    new_req.alert_id = 0;
                }
                else
                {
                    new_req.alert_id = json_integer_value(json_ptr_upload_alert_id);
                }

                json_t *json_ptr_upload_quality = json_object_get(value, "quality");
                if (json_ptr_upload_quality == NULL)
                {
                    new_req.quality = "NA";
                }
                else
                {
                    new_req.quality = json_string_value(json_ptr_upload_quality);
                }
                LOG_I(TAG, "parse(): includeAudio : new_req.upload_audio: %d alert_id: %llu, new_req.quality: %s", new_req.upload_audio, new_req.alert_id, new_req.quality.c_str());
            }
            else if (t == "livestream")
            {
                LOG_I(TAG, "livestream req is present");
                new_req.type = TYPE_LIVESTREAM;
                json_t *kinesis_duration;
                json_t *kinesis_bitrate;
                json_t *kinesis_resolution;
                json_t *kinesis_endpoint;
                json_t *kinesis_requestTS;
                json_t *kinesis_camera;
                json_t *kinesis_fps;
                json_t *kinesis_error;

                id = json_object_get(value, "req");
                new_req.id = json_integer_value(id);

                kinesis_duration = json_object_get(value, "duration");
                new_req.kinesis_duration = json_integer_value(kinesis_duration);

                kinesis_bitrate = json_object_get(value, "bitRate");
                new_req.kinesis_bitrate = json_integer_value(kinesis_bitrate);

                kinesis_resolution = json_object_get(value, "resolution");
                new_req.kinesis_resolution = json_string_value(kinesis_resolution);

                kinesis_endpoint = json_object_get(value, "endpoint");
                new_req.kinesis_endpoint = json_string_value(kinesis_endpoint);

                kinesis_requestTS = json_object_get(value, "requestTs");
                new_req.kinesis_requestTS = json_integer_value(kinesis_requestTS);

                kinesis_camera = json_object_get(value, "camera");
                new_req.kinesis_camera = json_integer_value(kinesis_camera);

                kinesis_fps = json_object_get(value, "fps");
                new_req.kinesis_fps = json_integer_value(kinesis_fps);

                kinesis_error = json_object_get (value, "error_code");
                if (kinesis_error != NULL && json_is_integer(kinesis_error)) {
                    new_req.error = static_cast<live_stream_error_t>(json_integer_value(kinesis_error));
                } else {
                    new_req.error = LS_ERR_INVALID;
                    nd_service_obj->send_err_msg (SM_E_AWS_STREAMING_PAYLOAD_ERR_NOT_FOUND, 0, "error_code not found in streaming payload");
                }
            }
            else if(t == "dual_livestream"){
                LOG_I (TAG,"dual_livestream req is present");
                new_req.type = TYPE_DUAL_LIVESTREAM;
                json_t *kinesis_duration;
                json_t *kinesis_endpoint;
                json_t *kinesis_requestTS;
                json_t *kinesis_cameras;

                id  = json_object_get(value, "req");
                new_req.id = json_integer_value(id);

                kinesis_duration = json_object_get (value, "duration");
                new_req.kinesis_duration = json_integer_value(kinesis_duration);

                kinesis_endpoint = json_object_get (value, "endpoint");
                new_req.kinesis_endpoint = json_string_value(kinesis_endpoint);

                kinesis_requestTS = json_object_get (value, "requestTs");
                new_req.kinesis_requestTS = json_integer_value(kinesis_requestTS);

                kinesis_cameras = json_object_get(value, "cameras");
                if (json_is_array(kinesis_cameras)) {
                    size_t num_cameras = json_array_size(kinesis_cameras);

                    for (size_t i = 0; i < num_cameras; i++) {
                        dual_livestream_camera_t camera_obj;
                        json_t* camera = json_array_get(kinesis_cameras, i);
                        const char* stream_name = json_string_value(json_object_get(camera, "stream_name"));
                        LOG_I (TAG,"stream_name: %s", stream_name);
                        camera_obj.stream_name = stream_name;
                        const char* resolution = json_string_value(json_object_get(camera, "resolution"));
                        LOG_I (TAG,"resolution: %s", resolution);
                        camera_obj.resolution = resolution;
                        camera_obj.bitrate = json_integer_value(json_object_get(camera, "bitRate"));
                        camera_obj.fps = json_integer_value(json_object_get(camera, "fps"));
                        camera_obj.camera = json_integer_value(json_object_get(camera, "camera"));
                        json_t *kinesis_dual_cam_error = json_object_get(camera, "error_code");
                        if (kinesis_dual_cam_error != NULL && json_is_integer(kinesis_dual_cam_error)) {
                            camera_obj.error = static_cast<live_stream_error_t>(json_integer_value(kinesis_dual_cam_error));
                        } else {
                            camera_obj.error = LS_ERR_INVALID;
                            nd_service_obj->send_err_msg (SM_E_AWS_STREAMING_PAYLOAD_ERR_NOT_FOUND, 0, "error_code not found in streaming payload");
                        }
                        new_req.dual_livestream_cameras[i] = camera_obj;
                    }
                }
            }

            else
            {
                LOG_E(TAG, "Unknown request type");
                goto misc_req;
            }

            if (id == NULL)
            {
                LOG_E(TAG, "ping or vod or livestream req id is not present");
                goto misc_req;
            }

            new_req.id = json_integer_value(id);
            new_req.catalog_id = catalog_id;

            LOG_I(TAG, "Received %s: %llu", new_req.catalog_id.c_str(), new_req.id);

            if (new_req.type == TYPE_VOD)
            {

                if (start_time_sec == NULL)
                {
                    LOG_I(TAG, "startTime is not present");
                    new_req.trim = false;
                }
                else
                {
                    start_sec = json_integer_value(start_time_sec);
                    if (start_sec < 0 || start_sec >= VIDEO_FILE_DURATION_SEC)
                    {
                        LOG_E(TAG, "start_sec is invalid: %d", start_sec);
                        new_req.trim = false;
                    }
                }

                if (end_time_sec == NULL)
                {
                    LOG_I(TAG, "endTime is not present");
                    new_req.trim = false;
                }
                else
                {
                    end_sec = json_integer_value(end_time_sec);
                    if (end_sec < 0 || end_sec > VIDEO_FILE_DURATION_SEC)
                    {
                        LOG_E(TAG, "end_sec is invalid: %d", end_sec);
                        new_req.trim = false;
                    }
                }

                if (priority == NULL) {
                    LOG_E (TAG, "VOD priority is not present - setting to default %d",
                            DEFAULT_VOD_PRIORITY);
                    new_req.priority = DEFAULT_VOD_PRIORITY;
                }
                else {
                    int vod_priority = json_integer_value (priority);
                    if (vod_priority < MIN_VOD_PRIORITY || vod_priority > MAX_VOD_PRIORITY) {
                        LOG_E (TAG, "VOD priority is invalid: %d , setting to default %d",
                                vod_priority, DEFAULT_VOD_PRIORITY);
                        vod_priority = DEFAULT_VOD_PRIORITY;
                    }
                    new_req.priority = vod_priority;
                }

                if (cancelled == NULL) {
                    new_req.cancelled = DEFAULT_VOD_CANCELLED;
                }
                else {
                    int vod_cancelled = json_integer_value (cancelled);
                    if (vod_cancelled != 0 && vod_cancelled != 1) {
                        LOG_E (TAG, "VOD cancelled is invalid: %d , setting to default %d",
                                vod_cancelled, DEFAULT_VOD_CANCELLED);
                        vod_cancelled = DEFAULT_VOD_CANCELLED;
                    }
                    new_req.cancelled = vod_cancelled;
                    LOG_I (TAG, "Request is for cancelling VOD" );

                }

                if (req_part_id == NULL)
                {
                    LOG_I(TAG, "part_id is not present");
                    new_req.trim = false;
                }
                else
                {
                    part_id = json_integer_value(req_part_id);
                    if (part_id < 0 || part_id > VOD_PART_ID_MAX)
                    {
                        LOG_E(TAG, "Part ID is invalid", part_id);
                        new_req.trim = false;
                    }
                }

                if (new_req.trim)
                {

                    if (start_sec < end_sec)
                    {
                        new_req.start_sec = start_sec;
                        new_req.end_sec = end_sec;
                        new_req.part_id = part_id;
                        LOG_I(TAG, "%s:%llu is VOD for trimmed video. Start time:%d End time: %d PartId: %d",
                              new_req.catalog_id.c_str(), new_req.id, new_req.start_sec, new_req.end_sec, new_req.part_id);
                    }
                    else
                    {
                        LOG_E(TAG, "Erron in request: Start frame: %d End frame: %d", start_sec, end_sec);
                        new_req.trim = false;
                    }
                }
                LOG_I(TAG, "VOD REQUEST %s: %llu is %s VOD", new_req.catalog_id.c_str(), new_req.id, (new_req.trim ? "TRIMMED" : "NORMAL"));
            }

            // TODO this is reduntant
            cstr_type = json_string_value(type);
            if (NULL == cstr_type)
            {
                LOG_E(TAG, "Shadow parse Error, null is comming from cloud.  key:  %s", key);
                return false;
            }
            string str_type = cstr_type;
            if (str_type == "vod")
            {
                new_req.type = TYPE_VOD;
            }
            else if (str_type == "livestream")
            {
                LOG_I(TAG, "livestream req is present");
                new_req.type = TYPE_LIVESTREAM;
            }
            else if( str_type == "dual_livestream" ) {
                LOG_I (TAG,"dual_livestream req is present");
                new_req.type = TYPE_DUAL_LIVESTREAM;
            }
            else if (str_type == "ping")
            {
                new_req.type = TYPE_PING;
            }
            else
            {
                goto misc_req;
            }

            const char *cstr_status = json_string_value(status);
            if (NULL == cstr_status)
            {
                LOG_E(TAG, "Shadow parse Error, null is comming from cloud.  key:  %s", key);
                return false;
            }
            string str_status = cstr_status;
            if (str_status == "new")
            {
                LOG_I(TAG, "new req");
                new_req.cstatus = STATUS_NEW;
            }
            else if (str_status == "recv")
            {
                new_req.cstatus = STATUS_RECV;
            }
            else if (str_status == "done")
            {
                new_req.cstatus = STATUS_DONE;
            }
            else if (str_status == "err")
            {
                new_req.cstatus = STATUS_ERR;
            }
            else
            {
                goto misc_req;
            }

            for (int i = 0; i < json_array_size(commands); i++)
            {
                json_t *avalue = json_array_get(commands, i);
                const char *cstr_avalue = json_string_value(avalue);
                if (NULL == cstr_avalue)
                {
                    LOG_E(TAG, "Shadow parse Error, null is comming from cloud.  key:  %s", key);
                    return false;
                }
                string s = cstr_avalue;

                // add additional fields if the ping is for upload detailed/sys logs
                if (s == PING_UPLOAD_LOGS_DETAILED)
                {
                    start_time_sec = json_object_get(value, "startTime");
                    end_time_sec = json_object_get(value, "endTime");
                    start_sec = json_integer_value(start_time_sec) / 1000; // cloud sends ms
                    end_sec = json_integer_value(end_time_sec) / 1000;
                    new_req.start_sec = start_sec;
                    new_req.end_sec = end_sec;
                    new_req.trim = false; // is syslog?
                }
                else if (s == PING_UPLOAD_LOGS_SYSTEM)
                {
                    start_time_sec = json_object_get(value, "startTime");
                    end_time_sec = json_object_get(value, "endTime");
                    start_sec = json_integer_value(start_time_sec) / 1000;
                    end_sec = json_integer_value(end_time_sec) / 1000;
                    new_req.start_sec = start_sec;
                    new_req.end_sec = end_sec;
                    new_req.trim = true; // is syslog?
                }
                else if(s == PING_PAIR_ACCESSORY || s == PING_UNPAIR_ACCESSORY ) {
                    new_req.accessory_data = json_dumps(value, JSON_COMPACT);
                    new_req.is_pair = (s == PING_PAIR_ACCESSORY) ? true : false;
                }

                LOG_I(TAG, "Received command: %s", s.c_str());
                new_req.command_list.push_back(s);
            }

            // Check to see if request already exists, if it exists replace it with latest one
            bool found = false;
            for (list<request_t>::iterator iter = req.begin(), end = req.end(); iter != end; iter++)
            {
                if ((new_req.catalog_id == iter->catalog_id) &&
                    (new_req.id == iter->id) &&
                    (shadow_type == iter->shadow_type))
                {

                    LOG_I(TAG, "Ignoring request: %s: %llu", new_req.catalog_id.c_str(), new_req.id);
                    found = true;
                    break;
                }
            }

            if (found)
            {
                continue;
            }
            new_req.shadow_type = shadow_type;

            LOG_I(TAG, "Adding request: %s : %llu : %d", new_req.catalog_id.c_str(), new_req.id, new_req.shadow_type);
            req.push_back(new_req);
            if(new_req.type == TYPE_VOD) {
                send_hs_vod_status(new_req, true);
            }
        }
    }
misc_req:
    string ret = json_dumps(root, 0);
    LOG_I(TAG, "root:%s", ret.c_str());
    json_t *mask = json_object_get(root, "cameras");
    if (mask == NULL)
    {
        LOG_I(TAG, "No camera masks found");
    }
    else
    {
        LOG_I(TAG, "Cameras");
        misc_req_t cam_req;
        cam_req.type = TYPE_MISC;
        cam_req.cstatus = STATUS_NEW;
        cam_req.key = "cameras";
        stringstream ss;
        ss.str("");
        ss << json_integer_value(mask);
        cam_req.value = ss.str();
        ;
        cam_req.shadow_type = shadow_type;
        misc_req.push_back(cam_req);
    }

    string key_auth_method = "auth_method";
    parse_misc_req_for_key(key_auth_method, root, misc_req, shadow_type);

    string key_private_key_status = "private_key_status";
    parse_misc_req_for_key(key_private_key_status, root, misc_req, shadow_type);
    parse_misc_req_for_key(KEY_KA_CERTIFICATE_CHECK, root, misc_req, shadow_type);
    parse_misc_req_for_key(KEY_CONFIG_VALIDATION, root, misc_req, shadow_type);

    reqs = json_object_get(root, "vehicleClass");
    if (reqs == NULL)
    {
        LOG_I(TAG, "No vehicleClass found");
    }
    else
    {
        LOG_I(TAG, "Vehicle class");
        misc_req_t vehicle_class_req;
        vehicle_class_req.cstatus = STATUS_NEW;
        vehicle_class_req.type = TYPE_MISC;
        vehicle_class_req.key = "vehicleClass";
        const char *cstr_reqs = json_string_value(reqs);
        if (NULL == cstr_reqs)
        {
            LOG_E(TAG, "Shadow parse Error, null is comming from cloud.  key:  %s", key);
            return false;
        }
        vehicle_class_req.value = cstr_reqs;
        LOG_I(TAG, "vehicle Class found: %s", json_string_value(reqs));
        vehicle_class_req.shadow_type = shadow_type;
        misc_req.push_back(vehicle_class_req);
    }
    reqs = json_object_get(root, "vin");
    if (reqs == NULL)
    {
        LOG_I(TAG, "No vin found");
    }
    else
    {
        LOG_I(TAG, "Vehicle VIN class");
        misc_req_t vehicle_vin_req;
        vehicle_vin_req.cstatus = STATUS_NEW;
        vehicle_vin_req.type = TYPE_MISC;
        vehicle_vin_req.key = "vin";
        const char *cstr_reqs = json_string_value(reqs);
        if (NULL == cstr_reqs)
        {
            LOG_E(TAG, "Shadow parse Error, null is comming from cloud.  key:  %s", key);
            return false;
        }
        vehicle_vin_req.value = cstr_reqs;
        LOG_I(TAG, "vehicle VIN found: %s", json_string_value(reqs));
        vehicle_vin_req.shadow_type = shadow_type;
        misc_req.push_back(vehicle_vin_req);
    }

    json_t* req_data_recording = json_object_get (root, "data_recording");
    if( req_data_recording == NULL) {
        LOG_I(TAG, "No data_recording found");
    }
    else
    {
        LOG_I (TAG,"data_recording");
        misc_req_t data_recording;
        data_recording.cstatus = STATUS_NEW;
        data_recording.type = TYPE_MISC;
        data_recording.key = "data_recording";
        char* cstr_reqs = json_dumps(req_data_recording, 0);
        if(NULL == cstr_reqs ){
            LOG_E(TAG, "Shadow parse Error, null is comming from cloud.  key:  %s"  , key );
            return false;
        }
        data_recording.value = cstr_reqs;
        LOG_I (TAG,"data_recording found: %s",data_recording.value.c_str());
        data_recording.shadow_type = shadow_type;
        misc_req.push_back (data_recording);
        json_decref(req_data_recording);
        free(cstr_reqs);
    }

    // parse vehicleDetailHash from shadow for enhanced driver login 2.0 feature
    reqs = json_object_get (root, "vehicleDetailHash");
    if ( NULL == reqs ) {
        LOG_E(TAG, "No vehicleDetailHash found");

    } else {
        LOG_I(TAG, "vehicleDetailHash class");
        misc_req_t vehicle_detail_hash_req;
        vehicle_detail_hash_req.cstatus = STATUS_NEW;
        vehicle_detail_hash_req.type = TYPE_MISC;
        vehicle_detail_hash_req.key = "vehicleDetailHash";
        const char* cstr_reqs = json_string_value(reqs) ;

        if ( NULL == cstr_reqs ) {
            LOG_E(TAG, "Shadow parse Error, null is comming from cloud.  key:  %s"  , key );
            return false;
        }
        vehicle_detail_hash_req.value = cstr_reqs;
        LOG_I (TAG,"vehicleDetailHash found: %s",json_string_value(reqs));
        vehicle_detail_hash_req.shadow_type = shadow_type;
        misc_req.push_back (vehicle_detail_hash_req);
    }
    // json_decref(root);
    return true;

}

bool parse_shadow_delta(const string &input,
                        list<request_t> &req,
                        list<misc_req_t> &misc_req,
                        const shadow_type_t shadow_type)
{

    json_t *reqs;
    json_t *value;
    const char *key;
    json_error_t error;
    vector<string> commands;

    json_t *root = json_loads(input.c_str(), 0, &error);
    if (root == NULL)
    {
        LOG_C(TAG, "json error on line %d, %s", error.line, error.text);

        LOG_I(TAG, "shadow contents: %s", input.c_str());
        nd_service_obj->send_err_msg(SM_E_AWS_INVALID_JSON, 0, "Invalid json in shadow");
        return false;
    }
    if (parse(root, req, misc_req, shadow_type))
    {
        json_decref(root);
        return true;
    }

    LOG_E(TAG, "return false from parse_shadow_delta");
    return false;
}

bool parse_shadow(const string &input,
                  list<request_t> &req,
                  list<misc_req_t> &misc_req,
                  string section,
                  const shadow_type_t shadow_type)
{

    json_t *root = NULL;
    json_t *sec = NULL;
    json_t *value = NULL;
    json_error_t error;

    root = json_loads(input.c_str(), 0, &error);

    if (root == NULL)
    {
        LOG_C(TAG, "json error on line %d, %s", error.line, error.text);
        LOG_I(TAG, "shadow contents: %s", input.c_str());
        nd_service_obj->send_err_msg(SM_E_AWS_INVALID_JSON, 0, "Invalid json in shadow");
        goto err_exit;
    }

    value = json_object_get(root, "state");
    if (value == NULL)
    {

        LOG_C(TAG, "No state found. Shadow has unexpected contents");
        LOG_I(TAG, "shadow contents: %s", input.c_str());
        nd_service_obj->send_err_msg(SM_E_AWS_UNEXPECTED_JSON, 0, "Unexpected contents in shadow");

        goto err_exit;
    }

    sec = json_object_get(value, section.c_str());
    if (sec == NULL)
    {

        LOG_C(TAG, "No desired found. Shadow has unexpected contents");
        LOG_I(TAG, "shadow contents: %s", input.c_str());
        nd_service_obj->send_err_msg(SM_E_AWS_UNEXPECTED_JSON, 0, "Unexpected contents in shadow");
        goto err_exit;
    }

    if (parse(sec, req, misc_req, shadow_type))
    {
        json_decref(root);
        return true;
    }

err_exit:
    json_decref(root);
    LOG_E(TAG, "Returning false from parse_shadow");
    return false;
}
