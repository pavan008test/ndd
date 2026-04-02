#include <log.h>
#include "vod_health.h"
#include <jansson.h>
#include <service_utils.h>
#include <nd_time.h>
#include <sstream>
#include <string>

#define TAG "vod_health"

extern NDService *nd_service_obj;

namespace vod_health {

bool VodHealth::send_vod_health(const req_upload_msg_t *vod_req,const vod_health_data_type type, void *data) {
    bool ret = false;
    json_t *root = nullptr;
    json_t *individual_data = nullptr;
    json_error_t error;
    do {
        string individual_data_str;
        if (vod_req == nullptr || data == nullptr) {
            LOG_E(TAG, "Invalid input");
            break;
        }
        switch (type) {
        case REQ_ADD_UPLOAD_Q: {
            LOG_I(TAG, "REQ_ADD_UPLOAD_Q");
            // json format: 
            add_req_db_t *add_req = static_cast<add_req_db_t *>(data);
            // json format: {"ts":ts, "rank":rank}
            individual_data = json_pack_ex(&error, 0, "{s:I,s:i}", "ts", add_req->ts, "rank", add_req->rank);
            if (individual_data == nullptr) {
                LOG_E(TAG, "json_pack_ex failed: %s", error.text);
                break;
            }
            individual_data_str = "req_add_upload_q";
            break;
        }
        case UPLOAD_ATTEMPT: {
            LOG_I(TAG, "UPLOAD_ATTEMPT");
            upload_attempt_t *upload_attempt = static_cast<upload_attempt_t *>(data);
            string retry_count = to_string(upload_attempt->retry_count + 1);
            // json format: {"retry_count":{"start_ts":start_ts, "status":status, "retry_count":retry_count, "video_available":video_available, "api_resp_txt":api_resp_txt, "end_ts":end_ts}}
            individual_data = json_pack_ex(&error, 0, "{s:{s:I,s:b,s:i,s:b,s:s,s:I}}", 
                                            retry_count.c_str(), 
                                                "start_ts", upload_attempt->start_ts, 
                                                "status", upload_attempt->status, 
                                                "retry_count", upload_attempt->retry_count, 
                                                "video_available", upload_attempt->video_available, 
                                                "api_resp_txt", upload_attempt->api_resp_txt.c_str(), 
                                                "end_ts", upload_attempt->end_ts);
            individual_data_str = "upload_attempt";
            break;
        }
        case EXT_VOD_REQ: {
            LOG_I(TAG, "REQ_EXT_VOD");
            ext_vod_req_t *ext_vod_req = static_cast<ext_vod_req_t *>(data);
            // json format: {"ts":ts}
            individual_data = json_pack_ex(&error, 0, "{s:I}", "ts", ext_vod_req->ts);
            if (individual_data == nullptr) {
                LOG_E(TAG, "json_pack_ex failed: %s", error.text);
                break;
            }
            individual_data_str = "ext_vod_req";
            break;
        }
        case EXT_VOD_ACK: {
            LOG_I(TAG, "EXT_VOD_ACK");
            ext_vod_ack_t *ext_vod_ack = static_cast<ext_vod_ack_t *>(data);
            // json format: {"ts":ts}
            individual_data = json_pack_ex(&error, 0, "{s:I}", "ts", ext_vod_ack->ts);
            if (individual_data == nullptr) {
                LOG_E(TAG, "json_pack_ex failed: %s", error.text);
                break;
            }
            individual_data_str = "ext_vod_ack";
            break;
        }
        case EXT_VOD_RESP: {
            LOG_I(TAG, "EXT_VOD_RESP");
            ext_vod_resp_t *ext_vod_resp = static_cast<ext_vod_resp_t *>(data);
            // json format: {"ts":ts, "status":status, "reason":reason, "rgb_analysis":rgb_analysis, "is_vod_available":is_vod_available}
            individual_data = json_pack_ex(&error, 0, "{s:I,s:b,s:s,s:s,s:i}", 
                                            "ts", ext_vod_resp->ts, 
                                            "status", ext_vod_resp->status, 
                                            "reason", ext_vod_resp->reason.c_str(), 
                                            "rgb_analysis", ext_vod_resp->rgb_analysis.c_str(), 
                                            "is_vod_available", ext_vod_resp->is_vod_available);
            if (individual_data == nullptr) {
                LOG_E(TAG, "json_pack_ex failed: %s", error.text);
                break;
            }
            individual_data_str = "ext_vod_resp";
            break;
        }

        case VOD_FAILURE: {
            LOG_I(TAG, "VOD_FAILURE");
            vod_failure_t *vod_failure = static_cast<vod_failure_t *>(data);
            // json format: {"ts":ts, "reason":reason}
            individual_data = json_pack_ex(&error, 0, "{s:I,s:s}", "ts", vod_failure->ts, "reason", vod_failure->reason.c_str());
            if (individual_data == nullptr) {
                LOG_E(TAG, "json_pack_ex failed: %s", error.text);
                break;
            }
            individual_data_str = "vod_failure";
            break;
        }
        case VOD_SUCCESS: {
            LOG_I(TAG, "VOD_SUCCESS");
            vod_success_t *vod_success = static_cast<vod_success_t *>(data);
            // json format: {"ts":ts, "retry_count":retry_count}
            individual_data = json_pack_ex(&error, 0, "{s:I,s:i}", "ts", vod_success->ts, "retry_count", vod_success->retry_count);
            if (individual_data == nullptr) {
                LOG_E(TAG, "json_pack_ex failed: %s", error.text);
                break;
            }
            individual_data_str = "vod_success";
            break;
        }

        default:
            LOG_E(TAG, "Invalid type %d", type);
            break;
        }

        if(individual_data_str.empty()) {
            LOG_E(TAG, "individual_data_str is empty");
            break;
        }

        //print individual data
        LOG_I(TAG, "individual_data_str: %s", individual_data_str.c_str());
        char *individual_data_str_c = json_dumps(individual_data, JSON_COMPACT);
        if (individual_data_str_c == nullptr) {
            LOG_E(TAG, "json_dumps failed");
            break;
        }
        LOG_I(TAG, "individual_data: %s", individual_data_str_c);
        free(individual_data_str_c);
        // request_priority_cancelled

        stringstream req_priority_cancelled_ss;
        req_priority_cancelled_ss << "request_" << vod_req->req_priority << "_" << vod_req->cancelled;
        string request_priority_cancelled = req_priority_cancelled_ss.str();
        LOG_I(TAG, "request_priority_cancelled: %s", request_priority_cancelled.c_str());

        // ts        
        int64_t ts = get_system_time();

        // session
        stringstream session;
        session << "vod:requests:" << vod_req->vod_id;

        // file name 
        // path to the file "a/b/c/d.mp4" -> "d.mp4"
        string file_name = vod_req->fname;
        size_t found = file_name.find_last_of("/\\");
        if (found != string::npos) {
            file_name = file_name.substr(found + 1);
        }
        LOG_I(TAG, "file_name: %s", file_name.c_str());

        /* json format: {"session":"request_priority_cancelled// string", "vod_id":vod_id //string, file_name:file_name //string,"ts":ts // int64_t,"request_priority_is_cancel": {"priority":1 //int, "iscancel":0 //int, "individual_data_str":individual_data //json object} } }*/
        root = json_pack_ex(&error, 0, "{s:s,s:s,s:s,s:I,s:{s:i,s:i,s:o}}", 
                            "session",session.str().c_str(),
                            "vod_id", vod_req->vod_id,
                            "file_name", file_name.c_str(),
                            "ts", ts,
                            request_priority_cancelled.c_str(), "priority", vod_req->req_priority, "iscancel", vod_req->cancelled, individual_data_str.c_str(), individual_data);
        if (root == nullptr) {
            LOG_E(TAG, "json_pack_ex failed: %s", error.text);
            break;
        }

        // send data to hs //
        char *data_str = json_dumps(root, JSON_COMPACT);
        if (data_str == nullptr) {
            LOG_E(TAG, "json_dumps failed");
            break;
        }

        LOG_I(TAG, "vod hs data: %s", data_str);
        nd_service_obj->send_msg_healthstats(data_str, strlen(data_str));

        free(data_str);
        ret = true;
                      
    } while (false);
    if (root != nullptr) {
        json_decref(root);
    }
    if (individual_data != nullptr) {
        json_decref(individual_data);
    }
    return ret; 
}

bool VodHealth::send_vod_count(const pending_vod_count_t *pending_vod_count) {
    bool ret = false;
    json_t *root = nullptr;
    json_error_t error;
    do {
        if (pending_vod_count == nullptr) {
            LOG_E(TAG, "Invalid input");
            break;
        }

        // json format: {"vod:pending_vod_count":{"ts":ts, "count":count}, "isArray":"true"}
        root = json_pack_ex(&error, 0, "{s:{s:I,s:i},s:s}", "vod:pending_vod_count", "ts", pending_vod_count->ts, "count", pending_vod_count->count, "isArray", "true");

        if (root == nullptr) {
            LOG_E(TAG, "json_pack_ex failed: %s", error.text);
            break;
        }

        // send data to hs
        char *data_str = json_dumps(root, JSON_COMPACT);
        if (data_str == nullptr) {
            LOG_E(TAG, "json_dumps failed");
            break;
        }

        LOG_I(TAG, "vod hs data: %s", data_str);
        nd_service_obj->send_msg_healthstats(data_str, strlen(data_str));

        free(data_str);
        ret = true;
    } while (false);

    if (root != nullptr) {
        json_decref(root);
    }
    return ret;
}

} // namespace vod_health
