#ifndef AWS_IOT_PARSER_H
#define AWS_IOT_PARSER_H

#include <string>
#include <vector>
#include <list>
#include <stdint.h>

#include <jansson/jansson.h>

using namespace std;

static const string PING_KEEP_ALIVE = "keep-alive";
static const string PING_REBOOT = "reboot-phone";
static const string PING_UPLOAD_LOGS_DETAILED = "upload-logs-detailed";
static const string PING_UPLOAD_LOGS_SYSTEM = "upload-logs-system";
static const string PING_PAIR_ACCESSORY = "pair";
static const string PING_UNPAIR_ACCESSORY = "unpair";
static const string VOD = "vod";

/**
 * Default priority is given to the VOD requests that are received from the Cloud
 * without the priority field. This is possible in the transition period when the
 * Cloud updated the shadow with older VOD format when device was in older OTA,
 * but the Device received it after updating to the new (VOD-v2 supported) OTA.
 */
static const int DEFAULT_VOD_PRIORITY = 8;

//Lowest priority
static const int MAX_VOD_PRIORITY = 20;

//Highest priority
static const int MIN_VOD_PRIORITY = 1;

//The cancelled field will be present only when cancelled = 1. Otherwise Device will fill it with default value.
static const int DEFAULT_VOD_CANCELLED = 0;

enum type_t
{
    TYPE_VOD,
    TYPE_PING,
    TYPE_LIVESTREAM,
    TYPE_DUAL_LIVESTREAM,
    TYPE_MISC,
    TYPE_ERROR
};

enum shadow_type_t
{
    SHADOW_TYPE_CLASSIC,
    SHADOW_TYPE_NAMED_LS,
    SHADOW_TYPE_NAMED_VOD
};

enum aws_status_t
{
    STATUS_NEW,
    STATUS_RECV,
    STATUS_DO, // Internal state
    STATUS_ACK,
    STATUS_DONE,
    STATUS_ERR,
    STATUS_DEL
};

struct dual_livestream_camera_t {
    string stream_name;
    int bitrate;
    int fps;
    int camera;
    live_stream_error_t error;
    string resolution;
};

struct request_t
{
    uint64_t id;
    string catalog_id;
    type_t type;
    aws_status_t cstatus;
    vector<string> command_list;
    int count; // External request ID, to keep track of responses from say uploader
    bool trim;
    int start_sec;
    int end_sec;
    string vod_id;
    int priority;
    int cancelled;
    int part_id;
    int upload_observation;
    int upload_audio;
    int kinesis_duration;
    int kinesis_fps;
    string kinesis_resolution;
    int kinesis_camera;
    int kinesis_bitrate;
    string kinesis_endpoint;
    long long int kinesis_requestTS;
    uint64_t alert_id;
    string quality;
    string accessory_data;
    bool is_pair;
    live_stream_error_t error;
    struct dual_livestream_camera_t dual_livestream_cameras[2];
    shadow_type_t shadow_type;
};

struct misc_req_t
{
    type_t type;
    aws_status_t cstatus;
    string key;
    string value;
    shadow_type_t shadow_type;
};

struct upload_req_t
{
    int idx;
    string catalog_id;
    uint64_t req_id;
    int part_id;
};

struct livestream_req_t
{
    int idx;
    string catalog_id;
    uint64_t req_id;
    int part_id;
};

bool parse_shadow(const string &input, list<request_t> &req, list<misc_req_t> &misc_req, string section, shadow_type_t shadow_type);
bool parse_shadow_delta(const string &input, list<request_t> &req, list<misc_req_t> &misc_req, shadow_type_t shadow_type);

string get_response(request_t req, aws_status_t new_status);
bool send_hs_vod_status(const request_t req,const bool status);

#endif
