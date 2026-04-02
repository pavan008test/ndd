/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Arun V <arun.vj@netradyne.com>, February 2016
 */

#include <algorithm>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>

#include <aws_iot_internal.h>
//#include <nd_paths.h>
#include <nd_factory.h>
#include <nd_file_utils.h>
#include "AwsIotShadow.h"
#include <nd_prop_utils.h>
#include <data_recording.h>
#include <svc.h>
#include <system_utils.h>
#include <nd_defs.h>
#include <ndmb/nd_mbclient.h>
#include <accessoryhandler.h>

#include <nd_auth_openssl.h>
#define TAG "IOT"

using namespace std;
using namespace Aws::Crt;
using namespace Aws::Iotshadow;

// nd_device_obj for factory class api
ND_DeviceFactory *nd_device_obj = NULL;


const uint8_t READ_TIMEOUT = 255;
static const int base_retry_secs = 5; // Base value for exponential backoff with jitter algorithm
static const int MAX_NUM_RETRIES = 7; // Max value of retries to get cap value for exponential backoff with jitter algorithm
static const int CONNECTIVITY_CHECK_SLEEP = 10;
static const int KEY_CORRUPTION_CHECK_INTERVAL = 3; // mins
static const int MAX_SHADOW_SYNC_RETRY = 6;
const unsigned int AWS_IOT_YIELD_FAILURE_MAX = 15;

static const int MAX_PARAMS = 6;
static const int HOST_ADDR = 5;
static const int CERT_PATH = 4;
static const int DEVICE_ID = 1;
static const int SESSION_ID = 2;
static const int PROD_STAGE = 3;

static const string cloud_config_ini = "/home/ubuntu/.nddevice/latest/cloudconfig.ini";
static const string device_config_ini = "/home/ubuntu/config/deviceconfig.ini";
static const string nddevice_ini = "/home/ubuntu/.nddevice/nddevice.ini";
static const string bagheera_conf_ini = "/home/ubuntu/.nddevice/latest/bagheera_config.ini";

#if defined(KRAIT) || defined(KRAIT2)
static const string cam_override_ini = "/data/nd_files/config/cam_override.ini";
#else
static const string cam_override_ini = "/home/ubuntu/config/cam_override.ini";
#endif

static const string PUB_KEY_REGISTRATION_RESPONSE_FILE = DEV_SHM + "aws_pub_key_reg_res.txt";
static const string KEY_KA_CERTIFICATE_CHECK = "certificate-check-disabled-on-keep-alive-api";
static const string KEY_CONFIG_VALIDATION = "config_validation";

static string server_address = "";
static string end_point = "upload/pingdata";
static string pub_key_registration_url = "device/public-key";

static const string log_dir = "/home/ubuntu/.nddevice/log/awsiot";

static const string q_internal_buffer_str = "q_internal_buffer";
static const string q_circular_buffer_str = "q_circular_buffer";
static const string q_nd_central_str = "q_nd_central";

#define ROUTE_LOGS

static string certificate_folder = "/home/ubuntu/.nddevice/certificate/";
static string priv_jwt_key_path = certificate_folder + "ed25519key.pem";
static string cert_awsiot_key_path = certificate_folder + "certificate.pem.crt";
static string priv_awsiot_key_path = certificate_folder + "private.pem.key";

static list<string> send_recv;
static list<request_t> request_list;
static list<misc_req_t> misc_request_list;
static list<upload_req_t> uploader_list;
static list<livestream_req_t> livestream_list;

static bool uploader_awake = false;
static bool register_pub_key = true;
static bool retry_sync = false;
volatile bool msg_loop_active = false;

static const string Q_NAME = "AWSIOT";
static const string Q_NAME_SVC = "Q_SVC";
static nd_msgq_t *server_q = NULL;
static nd_msgq_t *server_q_nd_iot_shadow = NULL;
static int msg_idx = 0;
static int key_corruption_check_counter = 0;
static int shadow_sync_failure_count = 0; //tracks the consecutive failure count of shadow sync
static bool last_connectivity_status = true;

static const char *const kSAM_Server_MQ_Name = "MQ_SAM";

#if defined(KRAIT) || defined(KRAIT2) || defined(BAGHEERA2)
extern bool eld_enabled;
extern bool ft_enabled;
extern bool vh_enabled;
#endif

AwsIot *AwsIot::obj = NULL;
AwsIot *iot = NULL;
NDService *nd_service_obj = NULL;

pthread_mutex_t aws_api_mutex = PTHREAD_MUTEX_INITIALIZER;

std::mutex shadowSyncMutex;

enum shadow_cb_t
{
    SHADOW_UPDATE_READ,
    SHADOW_UPDATE_DELTA
};

struct shadow_document_t
{
    shadow_cb_t cb_type;
    string state;
    shadow_type_t shadow_type;
};

static list<shadow_document_t> shadow_update_list;

AwsIot *AwsIot::get_object()
{
    if (obj != NULL)
    {
        return obj;
    }

    obj = new AwsIot();
    return obj;
}

string data_recording_string = "";

static string get_msgq_name();
static string get_uploaderq_name();
bool get_send_gps_updates_to_awsiot();


bool send_keepalive(string device_id, int ping_id);
bool reboot(string device_id, int ping_id);
void do_public_key_registration(string reg_jwt);
static bool msg_loop();
static bool create_msg_q();
bool poll();
bool do_requests();
static bool do_ping();
static bool do_vod();
static bool do_livestream();
static bool do_dual_livestream();
bool send_cloud_msg(request_t &msg, aws_status_t status);
bool send_internal_msg(awsmsg_type_t type);
bool send_cloud_recv();
bool send_done();
bool do_delete();
bool mark_misc_reqs_recv();
bool send_misc_reqs_resp();
bool handle_misc_reqs();
void publish_gps(pub_gps_info_msg_t *data);
bool update_device_vod_count(int counter);
void init_public_key_registration();
void check_for_key_corruption();
void handle_misc_req_auth_method(misc_req_t &req);
void handle_misc_req_private_key_status(misc_req_t &req);
bool handle_livestream_res(res_livestreaming_data_t *msg);
bool ndmb_gps_cb(ndmb_generic_msg_t *msg);
void read_aws_gps_publish_config();
static int secs_count = 1;

void *handle_poll(void *arg);
void *handle_publish(void *arg);
bool handle_log_or_vod_res(res_upload_msg_t *msg);
void *eld_message(void *arg);
void *eld_power_status_main(void *arg);

static AwsIotShadow *s_classic_shadow = NULL;
static AwsIotShadow *s_named_shadow_ls = NULL;
static AwsIotShadow *s_named_shadow_vod = NULL;

static const char *TAG_THREAD_HANDLE_POLL ="TH_HANDLE_POLL";

static bool truncate_file(string fname)
{
    int fd = -1;
    if (truncate(fname.c_str(), 0) == 0)
    {
        return true;
    }
    else
    {
        return false;
    }
}


bool delete_certificate_files()
{
    bool b_delete_certificate = file_delete(certificate_folder + "/certificate.pem.crt");
    LOG_E(TAG, " delete_certificate_files(). certificate.pem.crt, Status: %d", b_delete_certificate);
    bool b_delete_private_key = file_delete(certificate_folder + "/private.pem.key");
    LOG_E(TAG, " delete_certificate_files(). private.pem.key, Status: %d", b_delete_private_key);

    b_delete_certificate = b_delete_certificate && b_delete_private_key;
    nd_service_obj->send_err_msg(SM_E_AWS_INVALID_OR_CORROUPT_CERT, b_delete_certificate, "AWS Connection Error, Certificate issue");
    return b_delete_certificate;
}

void AwsIot::report_awsiot_auth_error() {
    delete_certificate_files();
    LOG_E(TAG, "AWSIoT cert error, Exiting...");
    exit(1);

}

bool AwsIot::connect(conn_param_t &c)
{
    connectionCompletedPromise = std::promise<bool>();

    conn = c;

    // Create ByteCursor from in-memory certificate and key buffers
    Aws::Crt::ByteCursor certCursor = Aws::Crt::ByteCursorFromArray(
        reinterpret_cast<const uint8_t *>(conn.clientCRT_buffer), conn.clientCRT_buffer_len);
    Aws::Crt::ByteCursor keyCursor = Aws::Crt::ByteCursorFromArray(
        reinterpret_cast<const uint8_t *>(conn.clientKey_buffer), conn.clientKey_buffer_len);

    // Use the in-memory buffer constructor
    auto clientConfigBuilder =
        Aws::Iot::MqttClientConnectionConfigBuilder(certCursor, keyCursor);
    clientConfigBuilder.WithEndpoint(c.host.c_str());
    clientConfigBuilder.WithCertificateAuthority(c.rootCA.c_str());
    clientConfigBuilder.WithPortOverride(static_cast<uint16_t>(c.port));

    auto clientConfig = clientConfigBuilder.Build();
    if (!clientConfig)
    {
        LOG_E(
            TAG,
            "Client Configuration initialization failed with error %s\n",
            Aws::Crt::ErrorDebugString(clientConfig.LastError()));

        report_awsiot_auth_error();

        return false;
    }
    Aws::Iot::MqttClient client = Aws::Iot::MqttClient();
    connection = client.NewConnection(clientConfig);
    if (!*connection)
    {
        LOG_E(
            TAG,
            "MQTT Connection Creation failed with error %s\n",
            Aws::Crt::ErrorDebugString(connection->LastError()));
        return false;
    }

    // Assign callbacks
    connection->OnConnectionCompleted = std::move(onConnectionCompleted);
    connection->OnDisconnect = std::move(onDisconnect);
    connection->OnConnectionInterrupted = std::move(onInterrupted);
    connection->OnConnectionResumed = std::move(onResumed);

    conn.thingName = (c.endpoint + "-" + c.thingName);
    conn.client_id = (c.endpoint + "-" + c.client_id);
    thing_name = conn.thingName;
    device_id = c.thingName;

    LOG_I(TAG, "Connecting... Thing: %s", conn.thingName.c_str());

    if (!connection->Connect(conn.client_id.c_str(), false, 60/* KA time duration */))
    {
        LOG_E(TAG, "MQTT Connection failed with error %s\n", ErrorDebugString(connection->LastError()));
        return false;
    }

    if (connectionCompletedPromise.get_future().get())
    {
        return true;
    }
    else
    {
        return false;
    }
}

bool AwsIot::disconnect()
{
    LOG_I(TAG, "Disconnecting");

    if (!connected)
    {
        return true;
    }
    return iot->connection->Disconnect();
}

void AwsIot::disconnect_and_exit() {
    bool is_disconnected = disconnect();
    LOG_C(TAG, "Exiting AWSIoT! Disconnection status: %d", is_disconnected);
    exit(-1);
}

void AwsIot::raise_shadow_full_alert(shadow_type_t shadow_type)
{
    std::string shadow_type_str;
    switch (shadow_type) {
        case SHADOW_TYPE_CLASSIC:
            shadow_type_str = "CLASSIC";
            break;
        case SHADOW_TYPE_NAMED_LS:
            shadow_type_str = "NAMED_LS";
            break;
        case SHADOW_TYPE_NAMED_VOD:
            shadow_type_str = "NAMED_VOD";
            break;
        default:
            shadow_type_str = "UNKNOWN";
            break;
    }
    LOG_E(TAG, "Raising critical alert: Shadow full detected: %s (%d)",
            shadow_type_str.c_str(), shadow_type);
    nd_service_obj->send_err_msg(
        SM_E_AWS_SHADOW_FULL,
        static_cast<int>(shadow_type),
        "Shadow full detected: " + shadow_type_str);
}

bool AwsIot::send_response(string res, shadow_type_t shadow_type = SHADOW_TYPE_CLASSIC)
{
    LOG_I(TAG, "shadow_type: %d, send_response: %s", shadow_type, res.c_str());

    AwsIotShadow *shadow;
    switch (shadow_type)
    {
        case SHADOW_TYPE_CLASSIC:
            shadow = s_classic_shadow;
            break;
        case SHADOW_TYPE_NAMED_LS:
            shadow = s_named_shadow_ls;
            break;
        case SHADOW_TYPE_NAMED_VOD:
            shadow = s_named_shadow_vod;
            break;
        default:
            LOG_E(TAG, "send_response :: Unknown shadow type");
            return false;
    }

    return shadow->update_shadow(res);
}

bool AwsIot::register_read_cb(read_cb_t *cb)
{
    if (cb == NULL)
    {
        return false;
    }

    this->rcb = cb;
    return true;
}

bool AwsIot::register_delta(delta_cb *cb)
{
    if (cb == NULL)
    {
        return false;
    }

    this->cb = cb;
    return true;
}

void AwsIot::publish_read_update(string state, shadow_type_t shadow_type)
{
    this->rcb(state, shadow_type);
}

void AwsIot::publish_delta_update(string state, shadow_type_t shadow_type)
{
    this->cb(state, shadow_type);
}

void process_shadow_update(const shadow_document_t &update)
{
    LOG_I(TAG, "process_shadow_update :: shadow_type: %d, update type: %d",
            update.shadow_type, update.cb_type);
    if (update.cb_type == SHADOW_UPDATE_DELTA)
    {
        bool parsed = parse_shadow_delta(update.state, request_list, misc_request_list, update.shadow_type);
        if (!parsed)
        {
            LOG_E(TAG, "Error parsing delta-update");
        }
    }
    else if (update.cb_type == SHADOW_UPDATE_READ)
    {
        bool parsed = parse_shadow(update.state, request_list, misc_request_list, "desired", update.shadow_type);
        if (!parsed)
        {
            LOG_E(TAG, "Error parsing read-update");
        }
    }
    else
    {
        LOG_E(TAG, "Unknown shadow update type %d", update.cb_type);
    }
}

void delta_cb(string delta, shadow_type_t shadow_type)
{

    LOG_I(TAG, "delta_cb :: shadow_type: %d", shadow_type);
    LOG_I(TAG, "Document: %s", delta.c_str());

    shadow_document_t update;
    update.cb_type = SHADOW_UPDATE_DELTA;
    update.state = delta;
    update.shadow_type = shadow_type;
    {
        std::lock_guard<std::mutex> lock(shadowSyncMutex);
        //add the delta-update to the list
        shadow_update_list.push_back(update);
    }

    // process the delta
    LOG_I(TAG, "delta_cb :: sending internal msg to process delta_cb");
    send_internal_msg(INTERNAL_AWS_PROCESS_SHADOW_UPDATE);
}

void read_cb(string state, shadow_type_t shadow_type)
{
    LOG_I(TAG, "read_cb :: shadow_type: %d", shadow_type);
    LOG_I(TAG, "Document: %s", state.c_str());
    shadow_document_t update;
    update.cb_type = SHADOW_UPDATE_READ;
    update.state = state;
    update.shadow_type = shadow_type;
    {
        std::lock_guard<std::mutex> lock(shadowSyncMutex);
        //add the read-update to the list
        shadow_update_list.push_back(update);
    }

    // process the shadow update
    LOG_I(TAG, "read_cb :: sending internal msg to process read_cb");
    send_internal_msg(INTERNAL_AWS_PROCESS_SHADOW_UPDATE);
}

static bool get_params()
{
    Config_parser c(cloud_config_ini);
    if (c.getParseStatus() != true)
    {
        LOG_E(TAG, "Error in parsing cloudconfig file");
        return false;
    }

    string server;
    string inj_ver;

    if (c.isPresent("cloud", "server"))
    {
        server = c.getConfig("cloud", "server", "");
        if (server == "")
        {
            LOG_E(TAG, "cloud:server returned empty");
            return false;
        }
    }
    else
    {
        LOG_E(TAG, "cloud:Server not found in config file");
        return false;
    }

    if (c.isPresent("cloud", "injection-version"))
    {
        inj_ver = c.getConfig("cloud", "injection-version", "");
        if (inj_ver == "")
        {
            LOG_E(TAG, "cloud:injection-version returned empty");
            return false;
        }
    }
    else
    {
        LOG_E(TAG, "cloud:injection-version not found in config file");
        return false;
    }

    if (c.isPresent(server, "injestion"))
    {
        server_address = c.getConfig(server, "injestion", "");
        if (server_address == "")
        {
            LOG_E(TAG, "server:injestion returned empty");
            return false;
        }
        server_address = server_address + "/" + inj_ver;

        pub_key_registration_url = server_address + "/" + pub_key_registration_url;

        server_address = server_address + "/" + end_point;
    }
    else
    {
        LOG_E(TAG, "%s:injestion not found in config file", server.c_str());
        return false;
    }

    LOG_I(TAG, "get_params: server_address - %s", server_address.c_str());
    LOG_I(TAG, "get_params: pub_key_registration_url - %s", pub_key_registration_url.c_str());
    return true;
}

bool send_hs_vod_status(const request_t req,const bool status) {
    bool ret = false;
    do {

        // filename is first element in command_list , command_list is vector<string> , check if it is not empty
        if(req.command_list.empty()) {
            LOG_E(TAG, "send_hs_vod_status: command_list is empty");
            break;
        }

        string filename = req.command_list[0];
        LOG_I(TAG, "send_hs_vod_status: filename: %s", filename.c_str());

        if (filename.empty()) {
            LOG_E(TAG, "send_hs_vod_status: filename is empty");
            break;
        }


        string type;
        switch (req.cstatus)
        {
        case STATUS_NEW:
            type = "req_new";
            break;
        case STATUS_RECV:
            type = "req_recv";
            break;
        case STATUS_ACK:
            type = "req_ack";
            break;
        case STATUS_DONE:
            type = "req_done";
            break;
        case STATUS_ERR:
            type = "req_err";
            break;
        default:
            LOG_E(TAG, "send_hs_vod_status: Invalid status %d", req.cstatus);
            break;
        }
        if (type.empty()) {
            LOG_E(TAG, "send_hs_vod_status: type is empty");
            break;
        }

        json_t *root = nullptr;
        json_error_t jerror;
        do {
            int64_t ts = get_system_time();

            // request_priority_cancelled
            stringstream request_priority_cancelled;
            request_priority_cancelled << "request_" << req.priority << "_" << req.cancelled;

            LOG_I(TAG, "send_hs_vod_status: request_priority_cancelled: %s", request_priority_cancelled.str().c_str());

            // session = "hierarchy in hs payload" + request_priority_cancelled + vod_id

            string session = "vod:requests:" + req.vod_id;
            // format {"session":"session", "request_priority_cancelled:{type:{ts:timestamp,status:false}}, "ts":123, "filename":"filename", "vod_id":"vod_id" }

            json_error_t error;

            root = json_pack_ex(&error, 0,"{s:s,s:{s:{s:I,s:b}},s:I,s:s,s:s}",
                "session", session.c_str(),
                request_priority_cancelled.str().c_str(),
                type.c_str(),
                "ts", ts,
                "status", status,
                "ts", ts,
                "file_name", filename.c_str(),
                "vod_id", req.vod_id.c_str()
            );

            if (root == NULL) {
                LOG_E(TAG, "send_hs_vod_status: json_pack_ex failed: %s", error.text);
                break;
            }

            char *data_str = json_dumps(root, JSON_COMPACT);
            if (data_str == NULL) {
                LOG_E(TAG, "send_hs_vod_status: json_dumps failed");
                break;
            }

            LOG_I(TAG, "sending vod data to healthstats: %s", data_str);
            if(!nd_service_obj->send_msg_healthstats(data_str, strlen(data_str))) {
                LOG_E(TAG, "send_hs_vod_status: send_msg_healthstats failed FOR %s", data_str);
                break;
            }

            free(data_str);
            ret = true;
        } while(false);

        if(root) {
            json_decref(root);
        }

    } while (false);
    return ret;
}


//  Function to get the data recording status from the json string
bool get_data_recording_status_cloud(string json_string, data_record_metrics_t &data_recording_status) {
    bool ret = false;
    json_error_t jerror;
    json_t *jobj;
    do {

        jobj = json_loads(json_string.c_str(), 0, &jerror);
        if(!jobj) {
            LOG_E(TAG,"get_data_recording_status: Failed to load request json string");
            break;
        }
        if( !( json_is_boolean(json_object_get(jobj, "enabled")) && json_is_integer(json_object_get(jobj, "timestamp")) ) ){
            LOG_E(TAG, "get_data_recording_status: failed to get status and timestamp from request json string");
            LOG_E(TAG, "get_data_recording_status: Both status and timestamp should be present in request json string");
            break;
        }
        data_recording_status.status = json_is_true(json_object_get(jobj, "enabled"));
        data_recording_status.timestamp = json_integer_value(json_object_get(jobj, "timestamp"));
        ret = true;
    } while(false);
    if(jobj) {
        json_decref(jobj);
    }
    return ret;
}

bool handle_data_recording_status(misc_req_t &req) {
    bool ret = false;
    LOG_I (TAG,"data_recording_status");
    do {
        data_record_metrics_t data_recording_status;
        // get data_recording_status from request
        if(!get_data_recording_status_cloud(req.value,data_recording_status)) {
            LOG_E(TAG, "get_data_recording_status failed for request value");
            break;
        }
        LOG_I(TAG, "data_recording_status: %d, %lld", data_recording_status.status, data_recording_status.timestamp);
        // send data_recording_status to svcnnn
        if(! send_msg( (generic_msg_t *)&data_recording_status, (msg_type_t)RES_DATA_RECORD_STATUS, sizeof(data_recording_status), get_msgq_name(), Q_NAME_SVC, msg_idx++ ) ) {
            LOG_E(TAG, "Cannot send message to nd-central to update data recording status\n");
            break;
        }

    ret = true;

    } while(false);

    if(ret) {
        req.cstatus =  STATUS_DONE;
    } else {
        req.cstatus =  STATUS_ERR;
    }
    return ret;
}

bool load_cert_key_to_buffer(AwsIot::conn_param_t &param, string& cert_awsiot_key_path){
    std::ifstream cert_file(cert_awsiot_key_path, std::ios::binary);
    bool status = false;
    do{
        if (!cert_file.is_open()) {
            LOG_E(TAG, "Certificate file could not be opened: %s", cert_awsiot_key_path.c_str());
            break; 
        } else {
            // Move to end to get size
            cert_file.seekg(0, std::ios::end);
            std::streamsize size = cert_file.tellg();
            cert_file.seekg(0, std::ios::beg);

            if (size <= 0) {
                LOG_E(TAG, "Certificate file is empty: %s", cert_awsiot_key_path.c_str());
                break;
            } else {
                // Allocate memory for buffer
                param.clientCRT_buffer = static_cast<unsigned char*>(malloc(size));
                if (!param.clientCRT_buffer) {
                    LOG_E(TAG, "Memory allocation failed for certificate buffer");
                    break;
                } else {
                    // Read file content into buffer
                    if (!cert_file.read(reinterpret_cast<char*>(param.clientCRT_buffer), size)) {
                        LOG_E(TAG, "Failed to read cert file into buffer");
                        free(param.clientCRT_buffer);
                        param.clientCRT_buffer = NULL;
                        break;
                    } else {
                        param.clientCRT_buffer_len = static_cast<size_t>(size);
                        status = true;
                        LOG_I(TAG, "AwsIoT Certificate file read successfully: %zu bytes", param.clientCRT_buffer_len);
                    }
                }
            }
        }
    }while(false);
    
    return status; 
}

// Decrypt AWS IoT private key and certificate to buffers and stores them in connection parameter
void load_awsiot_keys_to_buffer(AwsIot::conn_param_t &param)
{
    LOG_I(TAG, "Loading AwsIoT keys to buffer...");
    bool crt_success = true, priv_success = true;
    
    // Load certificate file directly into buffer (not decrypted)
    crt_success = load_cert_key_to_buffer(param, cert_awsiot_key_path);

    // Load private key file using existing decryption function
    param.clientKey_buffer_len = nd_file_reoperate_to_buffer(priv_awsiot_key_path.c_str(), &param.clientKey_buffer);
    if(param.clientKey_buffer_len <=0 || param.clientKey_buffer == NULL) {
        priv_success = false;
    }

    if (!crt_success || !priv_success)
    {
        string str_msg = "Failed to read AwsIoT certificate or private key";
        LOG_E(TAG, str_msg.c_str());
        nd_service_obj->send_err_msg(SM_E_AWS_INVALID_OR_CORROUPT_CERT, NDService::UNUSED_ERR_AUX_CODE, str_msg);
    }else{
        LOG_I(TAG, "Successfully loaded AwsIoT keys to memory buffers");
    }
}

static bool init_shadows()
{
    if(s_classic_shadow == NULL)
    {
        s_classic_shadow = new AwsIotShadow();
        s_classic_shadow->initShadowCallbacks();
    }


    if(s_named_shadow_ls == NULL)
    {
        s_named_shadow_ls = new AwsIotShadow(SHADOW_TYPE_NAMED_LS);
        s_named_shadow_ls->initShadowCallbacks();
    }

    if(s_named_shadow_vod == NULL)
    {
        s_named_shadow_vod = new AwsIotShadow(SHADOW_TYPE_NAMED_VOD);
        s_named_shadow_vod->initShadowCallbacks();
    }

    return true;
}

int main(int argc, char **argv)
{
    //apiHandle : A singleton object representing the init/cleanup state of the entire AWSIOT-CRT.
    //It's invalid to use AWSIOT-CRT functionality without one active.
    ApiHandle apiHandle;

    int attempt = 0, CONNECTION_RETRY_DELAY = base_retry_secs;
    bool conn_status = false;

    printf("initilizing logger\n");
    pthread_t publish_th;

    bool status_log = nd_log_init(log_dir.c_str());
    if (status_log == false)
    {
        printf("unable to init logger :: Exiting from main");
    }
#ifdef ROUTE_LOGS
    route_logs(log_dir.c_str());
#endif

    nd_service_obj = NDService::get_service_obj(TAG);
    nd_device_obj_init();

    if (argc < MAX_PARAMS)
    {
        LOG_E(TAG, "Usage: AwsIotClient <device_id> <session_id>  <production/staging> <cert_path> <awsiot_server>");
        return 1;
    }

    LOG_I(TAG, "##### Starting AWSIOT #####");

    if (get_params() != true)
    {
        LOG_E(TAG, "Can't get params from config file");
        return 1;
    }

    AwsIot::conn_param_t param;
    param.host = argv[HOST_ADDR];
    param.port = 8883;
    certificate_folder = argv[CERT_PATH];
    stringstream ss;

    ss.str("");
    ss.clear();
    ss << argv[CERT_PATH] << "/root-CA.crt";
    param.rootCA = ss.str().c_str();

    param.thingName = argv[DEVICE_ID];
    param.client_id = argv[DEVICE_ID];
    param.endpoint = argv[PROD_STAGE];
    if (param.endpoint == "prod")
    {
        LOG_I(TAG, "endpoint \"prod\" is being changed to \"production\"");
        param.endpoint = "production";
    }

    iot = AwsIot::get_object();
    if (iot == NULL)
    {
        LOG_E(TAG, "Got a NULL AwsIot object");
        return 1;
    }

    read_eld_config();
    if (eld_enabled == true || ft_enabled == true || vh_enabled == true)
    {
        pthread_t eld_data;
        if ((pthread_create(&eld_data, NULL, &eld_publish_thread, NULL)) != 0)
        {
            LOG_E(TAG, "ELD publish thread creation failed, exiting");
            return 1;
        }
    }

    // Spawn thread to handle data publish
    if ((pthread_create(&publish_th, NULL, handle_publish, NULL)) != 0)
    {
        LOG_E(TAG, "Can't create publish thread, exiting");
        return 1;
    }

    bool res = create_msg_q();
    if (!res)
    {
        LOG_E(TAG, "Cannnot create msg q");
        return 1;
    }

    read_aws_gps_publish_config();
    std::string ndmb_gps_client = "NDMB_AWSIOT_SERVICE";
    NDMBClient msg_client_gps(ndmb_gps_client);
    LOG_I(TAG, "subscribe for GPS data");
    if(get_send_gps_updates_to_awsiot())
    {    
        msg_client_gps.subscribe(TOPIC_GPS_DATA, ndmb_gps_cb, 300, 100);
        LOG_I(TAG, "subscribed for GPS data");

    }
    else
    {
        LOG_I(TAG, "send_gps_updates_to_awsiot is false");
    }

    bool msg_loop_ret = true;
    do
    {
        /* Use exponential backoff algo only when connectivity exist */
        if (check_internet_exist() == true)
        {
            LOG_I(TAG, "Internet exists");
            CONNECTION_RETRY_DELAY = get_exp_backoff_wait(attempt, base_retry_secs);
        }
        else
        {
            LOG_I(TAG, "No internet, sleep %d sec", CONNECTIVITY_CHECK_SLEEP);
            sleep(CONNECTIVITY_CHECK_SLEEP);
            continue;
        }

        load_awsiot_keys_to_buffer(param);

        LOG_I(TAG, "Trying to connect to AWS IOT server... Retry %d", attempt);
        conn_status = iot->connect(param);

        if (!conn_status)
        {
            LOG_E(TAG, "Unable to connect to Aws IoT server");
            LOG_I(TAG, "CONNECTION_RETRY_DELAY: %d", CONNECTION_RETRY_DELAY);
            sleep(CONNECTION_RETRY_DELAY);
        }
        attempt++;
        sleep(1);

    } while (!conn_status);

    if (!iot->register_delta(delta_cb))
    {
        LOG_E(TAG, "Unable to register delta callbacks");
        return 1;
    }

    if (!iot->register_read_cb(read_cb))
    {
        LOG_E(TAG, "Unable to register read callbacks");
        return 1;
    }

    // init all the shadows
    init_shadows();

    //get polling started
    pthread_t handle_poll_th;
    pthread_create(&handle_poll_th, NULL, handle_poll, NULL);
    pthread_setname_np(handle_poll_th, TAG_THREAD_HANDLE_POLL);

    msg_loop_ret = msg_loop();

    iot->disconnect();
    LOG_I(TAG, "Exiting AWSIOT, msg_loop_ret: %d", msg_loop_ret);

    return 1;
}

bool create_msg_q()
{
    // Create message queue
    if (server_q)
    {
        LOG_I(TAG, "MSGQ already present");
        return true;
    }

    server_q = nd_msgq_t::get_msgq(get_msgq_name(), nd_msgq_t::ND_MSGQ_SERVER);

    if (server_q == NULL)
    {
        LOG_E(TAG, "Cannot create message queue");
        return false;
    }

    LOG_I(TAG, "Message queue created");

    return true;
}

bool sync_counter_with_shadow(uint64_t counter)
{

    bool status(false);
    const char *const json_data_key = "counter";
    json_t *root = json_object();

    LOG_I(TAG, "Entered %s", __func__);

    do
    {
        if (NULL == root)
        {
            LOG_E(TAG, "%s: root NULL", __func__);
            break;
        }

        json_t *jkey_val = json_object();

        if (NULL == jkey_val)
        {
            LOG_E(TAG, "%s: jkey_val NULL", __func__);
            break;
        }

        json_object_set_new(jkey_val, json_data_key, json_integer(counter));

        json_t *jstate = json_object();
        json_object_set(jstate, "reported", jkey_val);
        json_object_set(root, "state", jstate);
        char *str_ptr = json_dumps(root, 0);
        const std::string str = std::string(str_ptr);
        free(str_ptr);

        LOG_I(TAG, "Updating shadow with pass counter: %s", str.c_str());

        if (!iot->send_response(str))
        {
            LOG_E(TAG, "Unable to sync counter");
            break;
        }

        LOG_I(TAG, "Counter synced with shadow");

        status = true;
    } while (false);

    if (NULL != root)
    {
        json_decref(root);
    }
    return status;
}

void notify_counter_sync_response(bool sync_status)
{
    sam_pass_counter_sync_resp_msg_t sync_response{};
    sync_response.status_ = sync_status;

    if (!send_msg((generic_msg_t *)&sync_response, SYNC_COUNTER_RESPONSE, sizeof(sam_pass_counter_sync_resp_msg_t),
                  get_msgq_name(), kSAM_Server_MQ_Name, msg_idx++))
    {
        LOG_E(TAG, "send_msg for counter sync response failed");
    }
}

void handle_device_vod_count(device_vod_count_msg_t *msg) {
    update_device_vod_count(msg->count);
}

void handle_sam_counter_data(sam_pass_counter_sync_msg_t *msg)
{
    const bool status = sync_counter_with_shadow(msg->counter_);
    notify_counter_sync_response(status);
}

bool msg_loop()
{
    nd_msgq_t::nd_msg_t *msg;
    // Set flag to true when we are going to receive msgs.
    // This flag helps senders who can drop messages which are not critical
    msg_loop_active = true;
    LOG_I(TAG, "setting msg_loop_active flag");

    while (1)
    {
        if ((msg = server_q->receive()) == NULL)
        {
            LOG_E(TAG, "Receive message failed");
            continue;
        }

        msg_type_t type = get_msg_type(msg->get_buffer());
        generic_msg_t *m = (generic_msg_t *)msg->get_buffer();

        if (m == NULL)
        {
            LOG_E(TAG, "Received NULL message");
            continue;
        }

        LOG_D(TAG, "%d received", m->msg_type);
        switch (type)
        {
        case INTERNAL_AWS_POLL:
            if (!poll())
            {
                LOG_E(TAG, "Exiting from msg_loop");
                msg_loop_active = false;
                LOG_I(TAG, "clearing msg_loop_active flag");
                return false;
            }
            break;
        case INTERNAL_AWS_PROCESS_SHADOW_UPDATE:
        {
            {
                std::lock_guard<std::mutex> lock(shadowSyncMutex);
                int shadow_update_count = shadow_update_list.size();
                if (shadow_update_count == 0)
                {
                    LOG_I(TAG, "No shadow updates pending to process");
                    break;
                }

                LOG_I(TAG, "Processing shadow update(s): %d", shadow_update_count);
                for (list<shadow_document_t>::iterator iter = shadow_update_list.begin(), end = shadow_update_list.end(); iter != end; iter++)
                {
                    process_shadow_update(*iter);
                }
                shadow_update_list.clear();
            }
            LOG_I(TAG, "Shadow update(s) processed");
            send_internal_msg(INTERNAL_AWS_RECV);
            send_internal_msg(INTERNAL_AWS_DO);
            LOG_I(TAG, "Internal msg sent for RECV and DO");
            break;
        }
        case INTERNAL_AWS_RECV:
            send_cloud_recv();
            mark_misc_reqs_recv();
            break;
        case INTERNAL_AWS_DO:
            do_requests();
            break;
        case INTERNAL_AWS_DONE:
            send_done();
            send_misc_reqs_resp();
            break;
        case INTERNAL_AWS_DELETE:
            do_delete();
            break;
        case RES_UPLOAD_VOD:
            handle_log_or_vod_res((res_upload_msg_t *)m);
            break;
        case UPDATE_DEVICE_VOD_COUNT:
                LOG_I(TAG, "Received UPDATE_DEVICE_VOD_COUNT");
                handle_device_vod_count(reinterpret_cast<device_vod_count_msg_t *>(m));
                break;
        case RES_UPLOAD_NON_CRITICAL_LOG:
            handle_log_or_vod_res((res_upload_msg_t *)m);
            break;
        case RES_LIVE_STREAM:
            handle_livestream_res((res_livestreaming_data_t *)m);
            break;
        case INTERNAL_AWS_PUBLISH_GPS:
            LOG_D(TAG, "publishing gps data");
            publish_gps((pub_gps_info_msg_t *)m);
            break;
        case SYNC_COUNTER_WITH_IOT:
        {
            LOG_I(TAG, "Received SYNC_COUNTER_WITH_IOT");
            handle_sam_counter_data(reinterpret_cast<sam_pass_counter_sync_msg_t *>(m));
            break;
        }
        case REQ_DATA_RECORD_STATUS:
        {
            misc_req_t misc_req;
            misc_req.value = data_recording_string;
            handle_data_recording_status(misc_req);
            break;
        }
        default:
            LOG_E(TAG, "Unknown message: %d", type);
        }

        delete msg;
    }
    return true;
}

void check_for_key_corruption()
{
    LOG_D(TAG, "check_for_key_corruption:: key_corruption_check_counter=%d", key_corruption_check_counter);

    string priv_key_status = get_priv_key_status();
    string auth_method = get_auth_method();

    if ((priv_key_status == PRIV_KEY_STATUS_CORRUPTED || priv_key_status == PRIV_KEY_STATUS_RESET) && auth_method == AUTH_METHOD_PUB_KEY)
    {
        LOG_E(TAG, "Key corruption detected.");

        // Check if request already exists
        bool found = false;
        for (list<misc_req_t>::iterator iter = misc_request_list.begin(), end = misc_request_list.end(); iter != end; iter++)
        {
            if (iter->key == "private_key_status")
            {
                LOG_I(TAG, "Ignoring request, already available");
                found = true;
                break;
            }
        }

        if (!found)
        {
            misc_req_t private_key_status_req;
            private_key_status_req.cstatus = STATUS_RECV;
            private_key_status_req.type = TYPE_MISC;
            private_key_status_req.key = "private_key_status";
            private_key_status_req.value = priv_key_status.c_str();
            private_key_status_req.shadow_type = SHADOW_TYPE_CLASSIC;
            misc_request_list.push_back(private_key_status_req);
        }
        send_internal_msg(INTERNAL_AWS_DO);

        nd_service_obj->send_err_msg(SM_E_AWS_CORRUPT_JWT_AUTH_KEY,
                                        NDService::UNUSED_ERR_AUX_CODE, "Auth key corruption detected");
    }
    else if (priv_key_status == PRIV_KEY_STATUS_NONE && auth_method == AUTH_METHOD_NONE)
    {
        LOG_I(TAG, "Key is not yet registered. Registering...");
        register_pub_key = true;
        send_internal_msg(INTERNAL_AWS_DO);
    }
}


/**
 * @brief Poll for any pending requests, periodic check for key corruption
 */
bool poll()
{
    LOG_D(TAG, "POLL");
    if (!uploader_awake && is_msg_q_created(get_uploaderq_name()))
    {
        // Send an internal message to clear off
        //  any pending requests in DO state
        uploader_awake = true;
        LOG_I(TAG, "Uploader is awake");
        send_internal_msg(INTERNAL_AWS_DO);
    }

    if(last_connectivity_status != iot->is_connected())
    {
        LOG_I(TAG, "AWSIoT connectivity status: %d", iot->is_connected());
        last_connectivity_status = iot->is_connected();
    }

    if (iot->is_connected()){
        if (key_corruption_check_counter++ == KEY_CORRUPTION_CHECK_INTERVAL)
        {
            // check for key corruption every KEY_CORRUPTION_CHECK_INTERVAL secs
            key_corruption_check_counter = 0;
            LOG_I(TAG, "check_for_key_corruption");
            check_for_key_corruption();
        }
        if(retry_sync)
        {
            retry_sync = false;
            LOG_I(TAG, "Retrying shadow sync for the failed requests...");

            send_internal_msg(INTERNAL_AWS_RECV);

            // If any already received messages are present, (eg, abrupt service restart), process them
            send_internal_msg(INTERNAL_AWS_DO);
        }
    }

    return true;
}

void *handle_poll(void *arg)
{
    LOG_D(TAG, "handle_poll");
    while (true)
    {
        send_internal_msg(INTERNAL_AWS_POLL);
        sleep(60);
    }
}

void handle_shadow_sync_retry()
{
    shadow_sync_failure_count++;
    LOG_E(TAG, "Shadow sync failed consecutively for %d time(s)", shadow_sync_failure_count);
    if (shadow_sync_failure_count == MAX_SHADOW_SYNC_RETRY)
    {
        //raise critical alert
        LOG_E(TAG, "Raising critical alert for shadow sync failure");
        nd_service_obj->send_err_msg(SM_E_AWS_SHADOW_SYNC_FAILED,
                NDService::UNUSED_ERR_AUX_CODE,
                "Not retrying shadow sync as failing consecutively");
    }
    else if (shadow_sync_failure_count > MAX_SHADOW_SYNC_RETRY)
    {
        //not retrying shadow sync as failing consecutively
        LOG_E(TAG, "Not retrying shadow sync as failing consecutively");
    }
    else
    {
        //set flag to retry shadow sync
        retry_sync = true;
        LOG_I(TAG, "Will retry shadow sync");
    }
}

/// Handle RECV////
bool send_cloud_recv()
{
    bool send_do = false;
    for (list<request_t>::iterator iter = request_list.begin(), end = request_list.end(); iter != end; iter++)
    {
        request_t &req = *iter;
        if (req.cstatus != STATUS_NEW)
        {
            LOG_D(TAG, "send_cloud_recv: Request: %s:%llu is not in STATUS_NEW, status:%d", req.catalog_id.c_str(), req.id, req.cstatus);
            continue;
        }

        bool ret =  send_cloud_msg(req, STATUS_RECV);
        if(ret) {
            req.cstatus = STATUS_RECV;
            send_do = true;
            LOG_I(TAG, "Sent RECV for request: %s:%llu", req.catalog_id.c_str(), req.id);
            if (req.type == TYPE_VOD) {
                send_hs_vod_status(req, true);
            }
            //reset the sync_failure counter
            shadow_sync_failure_count = 0;
        }
        else {
            LOG_E(TAG, "Failed to send RECV for request: %s:%llu", req.catalog_id.c_str(), req.id);
            handle_shadow_sync_retry();
        }
    }

    if (send_do)
    {
        send_internal_msg(INTERNAL_AWS_DO);
    }

    return true;
}

bool mark_misc_reqs_recv()
{

    for (list<misc_req_t>::iterator iter = misc_request_list.begin(), end = misc_request_list.end(); iter != end; iter++)
    {
        misc_req_t &req = *iter;
        if (req.cstatus != STATUS_NEW)
        {
            continue;
        }
        else
        {
            req.cstatus = STATUS_RECV;
        }
    }
    return true;
}

static bool get_device_info(string &device_type, string &device_ver)
{
    Config_parser c1(device_config_ini);
    Config_parser c2(nddevice_ini);

    if ((c1.getParseStatus() != true) || (c2.getParseStatus() != true))
    {
        LOG_E(TAG, "Error in parsing device/nddevice config file");
        return false;
    }
    device_type = c1.getConfig("identity", "deviceType", "");
    if (device_type == "")
    {
        LOG_E(TAG, "can't get device type");
        return false;
    }
    device_ver = c2.getConfig("version", "nddevice", "");
    if (device_ver == "")
    {
        LOG_E(TAG, "can't get device vesion");
        return false;
    }

    return true;
}

string get_device_id() {
    return iot->get_device_id();
}

string get_server_address() {
    return server_address;
}

string get_device_ota_version() {
    static string device_ver;
    do {
        if (!device_ver.empty()) {
            break;
        }
        Config_parser c2(nddevice_ini);
        if (c2.getParseStatus() != true)
        {
            LOG_E(TAG, "Error in parsing nddevice config file");
            break;
        }
        device_ver = c2.getConfig("version", "nddevice", "");
        if (device_ver == "")
        {
            LOG_E(TAG, "can't get device version");
            break;
        }
    } while (false);
    return device_ver;
}

string get_device_type() {
    static string device_type;
    do {
        if (!device_type.empty()) {
            break;
        }
        Config_parser c1(device_config_ini);
        if (c1.getParseStatus() != true)
        {
            LOG_E(TAG, "Error in parsing device config file");
            break;
        }
        device_type = c1.getConfig("identity", "deviceType", "");
        if (device_type == "")
        {
            LOG_E(TAG, "can't get device type");
            break;
        }
    } while (false);
    return device_type;
}

bool update_request_status(uint64_t req_id, aws_status_t new_status)
{
    bool ret = false;
    for (list<request_t>::iterator iter = request_list.begin(), end = request_list.end(); iter != end; iter++)
    {
        request_t &req = *iter;
        if (req.id == req_id)
        {
            req.cstatus = new_status;
            ret = true;
            break;
        }
    }
    return ret;
}

void do_public_key_registration(string reg_jwt)
{
    int rc;
    string device_type;
    string device_ver;

    if (get_device_info(device_type, device_ver) == false)
    {
        LOG_E(TAG, "Can't get device info");
        return;
    }

    stringstream ss;

    ss << "curl -X POST "
          "-H \"X-DeviceType: "
       << device_type << "\" "
                         "-H \"X-DeviceId: "
       << iot->get_device_id() << "\" "
                                  "-H \"X-Device-JWT: "
       << reg_jwt << "\" "
       << pub_key_registration_url
       << " -o " << PUB_KEY_REGISTRATION_RESPONSE_FILE;

    LOG_I(TAG, ss.str().c_str());
    rc = system(ss.str().c_str());

    if (rc != 0)
    {
        LOG_E(TAG, "Pub key registration: Curl failure %d", rc);
        return;
    }

    ifstream response_file;
    response_file.open(PUB_KEY_REGISTRATION_RESPONSE_FILE.c_str());

    std::stringstream response_buffer;
    if (response_file.is_open())
    {
        response_buffer << response_file.rdbuf();
        response_file.close();
    }

    string response_string = response_buffer.str();
    if (response_string.find("\"response\":true") != string::npos)
    {
        store_key_pair();
    }
    LOG_I(TAG, "do_public_key_registration :: %s", response_string.c_str());
}

////Handle Do////
bool send_keepalive(string device_id, int ping_id)
{

    int rc;
    string device_type;
    string device_ver;

    if (get_device_info(device_type, device_ver) == false)
    {
        LOG_E(TAG, "Can't get device info");
        return false;
    }

    string auth_header = "";
    bool header_status = get_auth_header(auth_header);

    if (!header_status)
    {
        LOG_I(TAG, "Corrupted jwt, Proceeding anyway for KA call...");
    }

    stringstream ss;
    ss << "curl -X POST "
          "-H \"X-DeviceType: "
       << device_type << "\" "
                         "-H \"X-DeviceId: "
       << device_id << "\" "
                       "-H \""
       << auth_header << "\" "
                         "-F data=\" { \\\"device_id\\\": \\\""
       << device_id << "\\\", \\\"keep-alive:\\\" : \\\"true\\\", \\\"deviceversion\\\": \\\""
       << device_ver << "\\\", \\\"devicetype\\\": " << device_type << ", \\\"ping_id\\\": \\\""
       << ping_id << "\\\"}\" " << server_address;

    LOG_I(TAG, ss.str().c_str());
    rc = system(ss.str().c_str());

    if (rc != 0)
    {
        LOG_E(TAG, "Sending ping failed, Curl failure %d", rc);
        return false;
    }

    LOG_I(TAG, "Success sending keep-alive for req: %d", ping_id);
    return true;
}

bool reboot(string device_id, int ping_id)
{
    int pid = fork();

    if (pid == 0)
    {
        LOG_E(TAG, "Rebooting..");
        sleep(30);

        if (send_powermon_to_reboot(Q_NAME, REQ_POWERMON_AWSIOT_TO_REBOOT) == false)
        {
            LOG_E(TAG, "Failed to send powermon to reboot, rebooting from awsiot");
            system_reboot();
            exit(0);
        }
        else {
            LOG_I(TAG, "Reboot request sent to powermon");
        }
    }

    return true;
}

bool do_requests()
{
    bool b1=false, b2=false, b3=false, b4=false, b5=false;
    if (do_ping())
    {
        LOG_D(TAG, "do_ping: Posting INTERNAL_AWS_DONE");
        b1 = send_internal_msg(INTERNAL_AWS_DONE);
    }

    if (do_vod())
    {
        LOG_D(TAG, "do_vod: Posting INTERNAL_AWS_DONE");
        b2 = send_internal_msg(INTERNAL_AWS_DONE);
    }

    if (do_livestream())
    {
        LOG_I(TAG, "do_livestream: Posting INTERNAL_AWS_DONE");
        b3 = send_internal_msg(INTERNAL_AWS_DONE);
    }
    if (do_dual_livestream()){
        LOG_I(TAG, "do_dual_livestream: Posting INTERNAL_AWS_DONE");
        b4 = send_internal_msg(INTERNAL_AWS_DONE);
    }
    if (handle_misc_reqs())
    {
        LOG_D(TAG, "handle_misc_reqs: Posting INTERNAL_AWS_DONE");
        b5 = send_internal_msg(INTERNAL_AWS_DONE);
    }

    return (b1&&b2&&b3&&b4&&b5);
}

bool do_livestream()
{
    bool done = false;

    if (!uploader_awake)
    {
        LOG_E(TAG, "Uploader is not awake yet");
        return false;
    }

    for (list<request_t>::iterator iter = request_list.begin(), end = request_list.end(); iter != end; iter++)
    {
        request_t &req = *iter;
        int errors = 0;

        if (req.type != TYPE_LIVESTREAM)
        {
            LOG_I(TAG, "do_livestream: req %s:%llu is Not livestream", req.catalog_id.c_str(), req.id);
            continue;
        }

        if (req.cstatus != STATUS_RECV)
        {
            LOG_I(TAG, "do_livestream: req %s:%llu is Not STATUS_RECV", req.catalog_id.c_str(), req.id);
            continue;
        }
        int idx = msg_idx++;
        LOG_I(TAG, "do_livestream: Processing livestream request %s:%llu", req.catalog_id.c_str(), req.id);
        req.cstatus = STATUS_DO;
        req_livestreaming_data_t kinesis_req_msg;
        kinesis_req_msg.duration = req.kinesis_duration;
        kinesis_req_msg.bitrate = req.kinesis_bitrate;
        kinesis_req_msg.camera = req.kinesis_camera;
        kinesis_req_msg.fps = req.kinesis_fps;
        kinesis_req_msg.req_id = req.id;
        kinesis_req_msg.id = idx;
        nd_strncpy(kinesis_req_msg.endpoint, req.kinesis_endpoint.c_str(), sizeof(kinesis_req_msg.endpoint));
        nd_strncpy(kinesis_req_msg.resolution, req.kinesis_resolution.c_str(), sizeof(kinesis_req_msg.resolution));

        if (!send_msg((generic_msg_t *)&kinesis_req_msg, (msg_type_t)START_LIVE_STREAMING,
                      sizeof(kinesis_req_msg), get_msgq_name(), q_nd_central_str, idx))
        {
            LOG_E(TAG, "Cannot send message to nd-central to start kinesis streaming\n");
            continue;
        }
        done = true;
        LOG_I(TAG, "pushing back to livestream List: req:%s:%llu idx:%d", req.catalog_id.c_str(), req.id, idx);
        livestream_req_t msg = {idx, req.catalog_id, req.id};
        livestream_list.push_back(msg);
    }
    return done;
}

//TODO: Finish this
bool do_dual_livestream(){
    bool done = false;

    if( !uploader_awake ) {
        LOG_E(TAG, "Uploader is not awake yet");
        return false;
    }

    for( list<request_t>::iterator iter=request_list.begin(), end = request_list.end(); iter!=end; iter++ ) {
        request_t &req = *iter;
        int errors=0;

        if(req.type != TYPE_DUAL_LIVESTREAM) {
            LOG_I(TAG, "do_dual_livestream: req %s:%llu is Not dual_livestream", req.catalog_id.c_str(), req.id);
            continue;
        }

        if(req.cstatus != STATUS_RECV) {
            LOG_I(TAG, "do_dual_livestream: req %s:%llu is Not STATUS_RECV",req.catalog_id.c_str(), req.id);
            continue;
        }
        int idx = msg_idx++;
        LOG_I(TAG, "do_dual_livestream: Processing dual_livestream request %s:%llu", req.catalog_id.c_str(), req.id);
        req.cstatus = STATUS_DO;

        req_dual_livestreaming_data_t kinesis_req_msg;
        kinesis_req_msg.duration = req.kinesis_duration;
        kinesis_req_msg.req_id = req.id;
        kinesis_req_msg.id = idx;
        nd_strncpy(kinesis_req_msg.endpoint, req.kinesis_endpoint.c_str(), sizeof(kinesis_req_msg.endpoint));

        req_streaming_camera_data_t camera0;
        camera0.bitrate = req.dual_livestream_cameras[0].bitrate;
        camera0.camera = req.dual_livestream_cameras[0].camera;
        camera0.fps = req.dual_livestream_cameras[0].fps;
        nd_strncpy(camera0.resolution, req.dual_livestream_cameras[0].resolution.c_str(), sizeof(camera0.resolution));
        nd_strncpy(camera0.stream_name, req.dual_livestream_cameras[0].stream_name.c_str(), sizeof(camera0.stream_name));
        kinesis_req_msg.streaming_cameras[0] = camera0;

        req_streaming_camera_data_t camera1;
        camera1.bitrate = req.dual_livestream_cameras[1].bitrate;
        camera1.camera = req.dual_livestream_cameras[1].camera;
        camera1.fps = req.dual_livestream_cameras[1].fps;
        nd_strncpy(camera1.resolution, req.dual_livestream_cameras[1].resolution.c_str(), sizeof(camera1.resolution));
        nd_strncpy(camera1.stream_name, req.dual_livestream_cameras[1].stream_name.c_str(), sizeof(camera1.stream_name));
        kinesis_req_msg.streaming_cameras[1] = camera1;

	if(! send_msg( (generic_msg_t *)&kinesis_req_msg, (msg_type_t)START_DUAL_LIVE_STREAMING,
           sizeof(kinesis_req_msg), get_msgq_name(), q_nd_central_str, idx ) ) {
            LOG_E(TAG, "Cannot send message to nd-central to start kinesis dual streaming\n");
            continue;
        }
        done = true;
        LOG_I(TAG, "pushing back to livestream List: req:%s:%llu idx:%d", req.catalog_id.c_str(), req.id,idx);
        livestream_req_t msg = {idx , req.catalog_id, req.id };
        livestream_list.push_back(msg);
    }
    return done;
}

void handle_misc_req_auth_method(misc_req_t &req)
{
    LOG_I(TAG, "handle_misc_reqs: auth_method");
    string auth_method = req.value;
    string auth_method_reported = "";
    bool synced = sync_auth_method(auth_method, auth_method_reported);
    if (synced)
    {
        req.value = auth_method_reported;
        req.cstatus = STATUS_DONE;
    }
    else
    {
        req.cstatus = STATUS_ERR;
    }
}

void handle_misc_req_private_key_status(misc_req_t &req)
{
    LOG_I(TAG, "handle_misc_reqs: private_key_status");
    string private_key_status = req.value;

    string private_key_status_reported = "";
    bool synced = sync_private_key_status(private_key_status, private_key_status_reported);
    if (synced)
    {
        req.value = private_key_status_reported;
        req.cstatus = STATUS_DONE;
    }
    else
    {
        req.cstatus = STATUS_ERR;
    }
}

void handle_misc_req_keep_alive_certificate_check(misc_req_t &req)
{
    LOG_I(TAG, "handle_misc_reqs: %s", KEY_KA_CERTIFICATE_CHECK.c_str());
    string disable_ka_certificate_check = req.value;

    bool updated = update_ka_cert_check(disable_ka_certificate_check);
    if (updated)
    {
        req.cstatus = STATUS_DONE;
    }
    else
    {
        req.cstatus = STATUS_ERR;
    }
}

void handle_misc_req_config_validation(misc_req_t &req)
{
    LOG_I(TAG, "handle_misc_reqs: %s", KEY_CONFIG_VALIDATION.c_str());
    string config_validation = req.value;

    //touch this file to disable config-validation
    const char *file_path = "/home/ubuntu/.nddevice/no_override_config_validation";

    if (config_validation == "false" || config_validation == "0")
    {
        file_touch(file_path);
        LOG_I(TAG, "Config validation disabled via shadow");
    }
    else
    {
        //remove the file if exists
        if (file_is_present(file_path))
        {
            file_delete(file_path);
        }
        LOG_I(TAG, "Config validation enabled via shadow");
    }
    req.cstatus = STATUS_DONE;
}

bool handle_misc_reqs()
{
    for (list<misc_req_t>::iterator iter = misc_request_list.begin(), end = misc_request_list.end(); iter != end; iter++)
    {
        misc_req_t &req = *iter;
        if (req.cstatus != STATUS_RECV)
        {
            continue;
        }

        req.cstatus = STATUS_DO;

        if (req.key == "cameras")
        {
            string front_cam = "disable", back_cam = "disable", left_cam = "disable", right_cam = "disable";
            unsigned int front_cam_id = 0, back_cam_id = 1, left_cam_id = 2, right_cam_id = 3;
            struct stat s;

            LOG_I(TAG, "handle_misc_reqs: cameras");
            string mask_str = req.value;
            char *ch = NULL;
            const char *mask_str_ch = mask_str.c_str();
            int cam_mask_reported = 0;
            stringstream ss;
            ss.str("");

            long int cam_mask_desired = strtol(mask_str.c_str(), &ch, 10);
            if (ch == mask_str_ch)
            {
                LOG_E(TAG, "strtol failed");
                ss << cam_mask_reported;
                req.value = ss.str();
                req.cstatus = STATUS_ERR;
                continue;
            }
            if (cam_mask_desired & (1 << front_cam_id))
                front_cam = "enable";
            if (cam_mask_desired & (1 << back_cam_id))
                back_cam = "enable";
            if (cam_mask_desired & (1 << left_cam_id))
                left_cam = "enable";
            if (cam_mask_desired & (1 << right_cam_id))
                right_cam = "enable";

            if (file_is_present(cam_override_ini))
            {
                LOG_I(TAG, "Camera over ride file exists");
            }
            else
            {
                if (file_touch(cam_override_ini, true))
                {
                    LOG_I(TAG, "camera override file created.");
                }
                else
                {
                    ss << cam_mask_reported;
                    req.value = ss.str();
                    req.cstatus = STATUS_ERR;
                    continue;
                }
            }

            Config_parser *c = new Config_parser(cam_override_ini);
            if (c == NULL)
            {
                LOG_E(TAG, "Creating object of type Config_parser for %s failed", cam_override_ini.c_str());
                ss << cam_mask_reported;
                req.value = ss.str();
                req.cstatus = STATUS_ERR;
                continue;
            }

            if (c->getParseStatus() == false)
            {
                if (!(c->isFileEmpty()))
                {
                    if (!truncate_file(cam_override_ini))
                    {
                        LOG_E(TAG, "Truncating corrupted ini file failed");
                        ss << cam_mask_reported;
                        req.value = ss.str();
                        req.cstatus = STATUS_ERR;
                        continue;
                    }
                }
            }

            if (c->isPresent("camera", "front"))
            {
                if (c->getConfig("camera", "front", "") == front_cam)
                {
                    LOG_I(TAG, "front cam is already %s", front_cam.c_str());
                }
                else
                {
                    if (c->updateConfig("camera", "front", front_cam))
                    {
                        LOG_I(TAG, "front camera updated to %s", front_cam.c_str());
                    }
                    else
                    {
                        LOG_E(TAG, "Updating front camera to %s failed", front_cam.c_str());
                    }
                }
            }
            else
            {
                if (c->addConfig("camera", "front", front_cam))
                {
                    LOG_I(TAG, "front camera added as %s", front_cam.c_str());
                }
                else
                {
                    LOG_E(TAG, "adding front camera config as %s failed", front_cam.c_str());
                }
            }
            // prepare reported mask
            if (c->getConfig("camera", "front", "") == "enable")
            {
                cam_mask_reported = cam_mask_reported | (1 << front_cam_id);
            }
            else
            {
                cam_mask_reported = cam_mask_reported & ~(1 << front_cam_id);
            }

            if (c->isPresent("camera", "back"))
            {
                if (c->getConfig("camera", "back", "") == back_cam)
                {
                    LOG_I(TAG, "back cam is already %s", back_cam.c_str());
                }
                else
                {
                    if (c->updateConfig("camera", "back", back_cam))
                    {
                        LOG_I(TAG, "back camera updated to %s", back_cam.c_str());
                    }
                    else
                    {
                        LOG_E(TAG, "Updating back camera to %s failed", back_cam.c_str());
                    }
                }
            }
            else
            {
                if (c->addConfig("camera", "back", back_cam))
                {
                    LOG_I(TAG, "back camera added as %s", back_cam.c_str());
                }
                else
                {
                    LOG_E(TAG, "adding back camera config as %s failed", back_cam.c_str());
                }
            }
            if (c->getConfig("camera", "back", "") == "enable")
            {
                cam_mask_reported = cam_mask_reported | (1 << back_cam_id);
            }
            else
            {
                cam_mask_reported = cam_mask_reported & ~(1 << back_cam_id);
            }

            if (c->isPresent("camera", "left"))
            {
                if (c->getConfig("camera", "left", "") == left_cam)
                {
                    LOG_I(TAG, "left cam is already %s", left_cam.c_str());
                }
                else
                {
                    if (c->updateConfig("camera", "left", left_cam))
                    {
                        LOG_I(TAG, "left camera updated to %s", left_cam.c_str());
                    }
                    else
                    {
                        LOG_E(TAG, "Updating left camera to %s failed", left_cam.c_str());
                    }
                }
            }
            else
            {
                if (c->addConfig("camera", "left", left_cam))
                {
                    LOG_I(TAG, "left camera added as %s", left_cam.c_str());
                }
                else
                {
                    LOG_E(TAG, "adding left camera config as %s failed", left_cam.c_str());
                }
            }
            if (c->getConfig("camera", "left", "") == "enable")
            {
                cam_mask_reported = cam_mask_reported | (1 << left_cam_id);
            }
            else
            {
                cam_mask_reported = cam_mask_reported & ~(1 << left_cam_id);
            }

            if (c->isPresent("camera", "right"))
            {
                if (c->getConfig("camera", "right", "") == right_cam)
                {
                    LOG_I(TAG, "right cam is already %s", right_cam.c_str());
                }
                else
                {
                    if (c->updateConfig("camera", "right", right_cam))
                    {
                        LOG_I(TAG, "right camera updated to %s", right_cam.c_str());
                    }
                    else
                    {
                        LOG_E(TAG, "Updating right camera to %s failed", right_cam.c_str());
                    }
                }
            }
            else
            {
                if (c->addConfig("camera", "right", right_cam))
                {
                    LOG_I(TAG, "right camera added as %s", right_cam.c_str());
                }
                else
                {
                    LOG_E(TAG, "adding right camera config as %s failed", right_cam.c_str());
                }
            }
            if (c->getConfig("camera", "right", "") == "enable")
            {
                cam_mask_reported = cam_mask_reported | (1 << right_cam_id);
            }
            else
            {
                cam_mask_reported = cam_mask_reported & ~(1 << right_cam_id);
            }
            ss.str("");
            ss << cam_mask_reported;
            req.value = ss.str();
            if (cam_mask_reported != cam_mask_desired)
            {
                req.cstatus = STATUS_ERR;
            }
            else
            {
                req.cstatus = STATUS_DONE;
            }
            delete c;
            c = NULL;
        }

        else if (req.key == "auth_method")
        {
            handle_misc_req_auth_method(req);
        }

        else if (req.key == "private_key_status")
        {
            handle_misc_req_private_key_status(req);
        }

        else if (req.key == KEY_KA_CERTIFICATE_CHECK)
        {
            handle_misc_req_keep_alive_certificate_check(req);
        }

        else if (req.key == KEY_CONFIG_VALIDATION)
        {
            handle_misc_req_config_validation(req);
        }

        else if (req.key == "vehicleClass")
        {

            LOG_I(TAG, "handle_misc_reqs: vehicleClass");
            string veh_class = req.value;
            string veh_class_reported = "";

            Config_parser *c1 = new Config_parser(device_config_ini);
            if (c1 == NULL)
            {
                LOG_E(TAG, "Creating object of Config_parser for %s failed", device_config_ini.c_str());
                continue;
            }
            if (c1->getParseStatus() == false)
            {
                LOG_E(TAG, "parsing %s failed", device_config_ini.c_str());
                req.value = veh_class_reported;
                req.cstatus = STATUS_ERR;
                delete c1;
                c1 = NULL;
                continue;
            }
            if (c1->isPresent("vehicle", "vehclass"))
            {
                if (c1->getConfig("vehicle", "vehclass", "") == veh_class)
                {
                    LOG_I(TAG, "vehicle class is already %s", veh_class.c_str());
                    veh_class_reported = veh_class;
                }
                else
                {
                    if (c1->updateConfig("vehicle", "vehclass", veh_class))
                    {
                        LOG_I(TAG, "vehicle class updated to %s", veh_class.c_str());
                    }
                    else
                    {
                        LOG_E(TAG, "Updating vehicle class to %s failed", veh_class.c_str());
                    }
                }
            }
            else
            {
                if (c1->addConfig("vehicle", "vehclass", veh_class))
                {
                    LOG_I(TAG, "vehicle class added as %s", veh_class.c_str());
                }
                else
                {
                    LOG_E(TAG, "adding vehicle class as %s failed", veh_class.c_str());
                }
            }

            veh_class_reported = c1->getConfig("vehicle", "vehclass", "");
            req.value = veh_class_reported;
            if (veh_class != veh_class_reported)
            {
                req.cstatus = STATUS_ERR;
            }
            else
            {
                req.cstatus = STATUS_DONE;
            }

            delete c1;
        }
        else if (req.key == "vin")
        {

            LOG_I(TAG, "handle_misc_reqs: vehicle vin");
            string veh_vin = req.value;
            string veh_vin_reported = "";

            Config_parser *c1 = new Config_parser(device_config_ini);
            if (c1 == NULL)
            {
                LOG_E(TAG, "Creating object of Config_parser for %s failed", device_config_ini.c_str());
                continue;
            }
            if (c1->getParseStatus() == false)
            {
                LOG_E(TAG, "parsing %s failed", device_config_ini.c_str());
                req.value = veh_vin_reported;
                req.cstatus = STATUS_ERR;
                delete c1;
                c1 = NULL;
                continue;
            }
            if (c1->isPresent("vehicle", "vin"))
            {
                if (c1->getConfig("vehicle", "vin", "") == veh_vin)
                {
                    LOG_I(TAG, "vehicle vin is already %s", veh_vin.c_str());
                    veh_vin_reported = veh_vin;
                }
                else
                {
                    if (c1->updateConfig("vehicle", "vin", veh_vin))
                    {
                        LOG_I(TAG, "vehicle vin updated to %s", veh_vin.c_str());
                    }
                    else
                    {
                        LOG_E(TAG, "Updating vehicle vin to %s failed", veh_vin.c_str());
                    }
                }
            }
            else
            {
                if (c1->addConfig("vehicle", "vin", veh_vin))
                {
                    LOG_I(TAG, "vehicle vin added as %s", veh_vin.c_str());
                }
                else
                {
                    LOG_E(TAG, "adding vehicle vin as %s failed", veh_vin.c_str());
                }
            }

            veh_vin_reported = c1->getConfig("vehicle", "vin", "");
            req.value = veh_vin_reported;
            if (veh_vin != veh_vin_reported)
            {
                req.cstatus = STATUS_ERR;
            }
            else
            {
                req.cstatus = STATUS_DONE;
            }

            delete c1;
        }
        else if ( "vehicleDetailHash" == req.key ) {
            LOG_I(TAG, "%s: vehicleDetailHash", __func__);
            string veh_detail_hash = req.value;
            string veh_detail_hash_reported = "";

            Config_parser *ptr = new Config_parser(device_config_ini);
            if ( NULL == ptr ) {
                LOG_E(TAG, "%s: Creating object of Config_parser for %s failed", __func__, device_config_ini.c_str());
                continue;
            }
            if ( false == ptr->getParseStatus() ) {
                LOG_E(TAG, "%s: parsing %s failed", __func__, device_config_ini.c_str());
                req.value = veh_detail_hash_reported;
                req.cstatus =  STATUS_ERR;
                delete ptr;
                ptr = NULL;
                continue;
            }
            if ( ptr->isPresent("vehicle", "vhash") ) {

                if ( ptr->getConfig("vehicle", "vhash", "") == veh_detail_hash ) {
                    LOG_I(TAG, "vehicleDetailHash is already %s", veh_detail_hash.c_str());
                    veh_detail_hash_reported = veh_detail_hash;

                } else {

                    if ( ptr->updateConfig("vehicle", "vhash", veh_detail_hash) ) {
                        LOG_I(TAG, "vehicleDetailHash updated to %s", veh_detail_hash.c_str());

                    } else {
                        LOG_E(TAG, "Updating vehicleDetailHash to %s failed", veh_detail_hash.c_str());
                    }
                }
            } else {

                if ( ptr->addConfig("vehicle", "vhash", veh_detail_hash) ) {
                    LOG_I(TAG, "vehicleDetailHash added as %s", veh_detail_hash.c_str());
                } else {
                    LOG_E(TAG, "adding vehicleDetailHash as %s failed", veh_detail_hash.c_str());
                }
            }

            veh_detail_hash_reported = ptr->getConfig("vehicle", "vhash", "");
            req.value = veh_detail_hash_reported;

            if ( veh_detail_hash_reported != veh_detail_hash ) {
                req.cstatus =  STATUS_ERR;
            } else {
                req.cstatus =  STATUS_DONE;
            }
            delete ptr;
        }
        else if (req.key == "data_recording")
        {
            data_recording_string = req.value;
            if(!handle_data_recording_status(req)) {
                LOG_E(TAG, "handle_data_recording_status failed");
            }
        }
    }

    return true;
}

bool request_log(request_t req)
{
    int idx = msg_idx++;
    req_upload_msg_t upload_msg;
    upload_msg.req_id = req.id;
    upload_msg.retry_count = 0;
    upload_msg.payload_size = PAYLOAD_SMALL;
    upload_msg.trim = req.trim;
    upload_msg.start_sec = req.start_sec;
    upload_msg.end_sec = req.end_sec;

    LOG_I(TAG, "request_log: sending REQ_UPLOAD_NON_CRITICAL_LOGS to uploader for start-time: %d and end-time %d . syslog = %d", req.start_sec, req.end_sec, req.trim);

    if (!send_msg((generic_msg_t *)&upload_msg, (msg_type_t)REQ_UPLOAD_NON_CRITICAL_LOGS,
                  sizeof(upload_msg), get_msgq_name(), get_uploaderq_name(), idx))
    {
        LOG_E(TAG, "Cannot send message");
        return false;
    }

    // Push to internal queue
    LOG_I(TAG, "request_log: pushing back to uploaderList: req id = %llu idx:%d ", req.id, idx);
    upload_req_t umsg = {idx, req.catalog_id, req.id};
    uploader_list.push_back(umsg);
    return true;
}

bool do_ping()
{
    bool done = false;
    bool ping_log_upload = false;

    for (list<request_t>::iterator iter = request_list.begin(), end = request_list.end(); iter != end; iter++)
    {
        request_t &req = *iter;
        int errors = 0;
        bool ping_pair_unpair_acc = false;

        if (req.type != TYPE_PING)
        {
            LOG_I(TAG, "do_ping: req %s:%llu not TYPE_PING", req.catalog_id.c_str(), req.id);
            continue;
        }

        if (req.cstatus != STATUS_RECV)
        {
            LOG_I(TAG, "do_ping: req %s:%llu not in STATUS_RECV", req.catalog_id.c_str(), req.id);
            continue;
        }

        req.cstatus = STATUS_DO;

        if (req.command_list.empty())
        {
            LOG_E(TAG, "command list in ping request %s:%llu is empty", req.catalog_id.c_str(), req.id);
            req.cstatus = STATUS_ERR;
            send_internal_msg(INTERNAL_AWS_DONE);
            continue;
        }

        for (vector<string>::iterator iter1 = req.command_list.begin(), end = req.command_list.end(); iter1 != end; iter1++)
        {
            if (*iter1 == PING_KEEP_ALIVE)
            {
                LOG_I(TAG, "Sending keep alive");

                bool done = send_keepalive(iot->get_device_id(), req.id);
                if (!done)
                {
                    errors++;
                }
            }
            else if (*iter1 == PING_REBOOT)
            {
                LOG_I(TAG, "Rebooting...");

                bool done = reboot(iot->get_device_id(), req.id);
                if (!done)
                {
                    errors++;
                }
            }
            else if (*iter1 == PING_UPLOAD_LOGS_DETAILED || *iter1 == PING_UPLOAD_LOGS_SYSTEM)
            {
                LOG_I(TAG, "Requesting log upload...");
                ping_log_upload = true;
                bool done = request_log(req);
                if (!done)
                {
                    errors++;
                }
            }
            else if (*iter1 == PING_PAIR_ACCESSORY || *iter1 == PING_UNPAIR_ACCESSORY) {
                LOG_I(TAG, "Requesting accessory pairing...");
                ping_pair_unpair_acc = true;
                bool pair_unpair_status = AccessoryHandler::GetInstance().HandlePairUnpairRequest(req);
                LOG_I(TAG, "Accessory pairing/unpairing request processed with status: %s", pair_unpair_status ? "SUCCESS" : "FAILURE");
            }
        }
        // status is already updated for pair/unpair accessory request
        //for log_upload ping, update status to done/err only after upload operation is complete
        // so continue to next request
        if(ping_pair_unpair_acc || ping_log_upload) {
            continue;
        }
        if( errors == 0 ) {
            LOG_I(TAG, "Success sending ping: %s:%llu ", req.catalog_id.c_str(), req.id);
            req.cstatus = STATUS_DONE;
        }
        else
        {
            LOG_I(TAG, "Error sending ping: %s:%llu", req.catalog_id.c_str(), req.id);
            req.cstatus = STATUS_ERR;
        }

        done = true;
    }

    return done;
}

bool do_vod()
{
    bool done = false;

    if (!uploader_awake)
    {
        LOG_E(TAG, "Uploader is not awake yet");
        return false;
    }

    for (list<request_t>::iterator iter = request_list.begin(), end = request_list.end(); iter != end; iter++)
    {
        request_t &req = *iter;
        int errors = 0;

        if (req.type != TYPE_VOD)
        {
            LOG_I(TAG, "do_vod: req %s:%llu is Not VOD", req.catalog_id.c_str(), req.id);
            continue;
        }

        if (req.cstatus != STATUS_RECV)
        {
            LOG_D(TAG, "do_vod: req %s:%llu is Not STATUS_RECV", req.catalog_id.c_str(), req.id);
            continue;
        }

        LOG_I(TAG, "do_vod: Processing VOD request %s:%llu", req.catalog_id.c_str(), req.id);
        req.cstatus = STATUS_DO;

        if (req.command_list.empty())
        {
            LOG_E(TAG, "vod request %s:%llu has an empty command list", req.catalog_id.c_str(), req.id);
            req.cstatus = STATUS_ERR;
            send_internal_msg(INTERNAL_AWS_DONE);
            continue;
        }

        for (vector<string>::iterator iter1 = req.command_list.begin(), end = req.command_list.end(); iter1 != end; iter1++)
        {
            int idx = msg_idx++;

            string fname = (*iter1); // do not include path when sending to circular_buffer

            //Send to uploader
            fname = nd_device_obj->get_external_eMMC_mount_path() + fname; //add path
            req_upload_msg_t upload_msg;
            upload_msg.level = (upload_level_t) req.priority;
            upload_msg.req_priority = (upload_level_t) req.priority;
            nd_strncpy(upload_msg.fname, fname.c_str(), sizeof(upload_msg.fname));
            upload_msg.req_id = req.id;
            upload_msg.retry_count = 0;
            upload_msg.payload_size = PAYLOAD_LARGE;
            upload_msg.trim = req.trim;
            upload_msg.upload_observation = req.upload_observation;
            upload_msg.upload_audio = req.upload_audio;
            // upload_msg.upload_audio = false;
            upload_msg.start_sec = req.start_sec;
            upload_msg.end_sec = req.end_sec;
            upload_msg.part_id = req.part_id;
            upload_msg.cancelled = req.cancelled;
            strncpy(upload_msg.vod_id, req.vod_id.c_str(), sizeof(upload_msg.vod_id));
            if (req.quality == "highq")
            {
                upload_msg.quality = HIGHQ;
            }
            else if (req.quality == "lowq")
            {
                upload_msg.quality = LOWQ;
            }
            else if(req.quality == "dpq"){
                upload_msg.quality = DPQ;
            }
            else
            {
                upload_msg.quality = NO_Q;
            }

            upload_msg.alert_id = req.alert_id;
            LOG_I(TAG, "do_vod: sending REQ_UPLOAD_VOD to uploader for file: %s %s req: %s:%llu", upload_msg.fname, ((upload_msg.trim) ? "(trimmed)" : ""),
                  req.catalog_id.c_str(), upload_msg.req_id);
            if (!send_msg((generic_msg_t *)&upload_msg, (msg_type_t)REQ_UPLOAD_VOD, sizeof(upload_msg), get_msgq_name(), get_uploaderq_name(), idx))
            {
                LOG_E(TAG, "Cannot send message");
                continue;
            }

            done = true;
            // Push to internal queue
            LOG_I(TAG, "do_vod: pushing back to uploaderList: req:%s:%llu idx:%d quality: %d, alert_id: %llu", req.catalog_id.c_str(), req.id, idx, upload_msg.quality, upload_msg.alert_id);
            upload_req_t umsg = {idx, req.catalog_id, req.id};
            uploader_list.push_back(umsg);
        }
    }

    return done;
}


// Handle finished requests//
bool send_done()
{
    bool del = false;

    for (list<request_t>::iterator iter = request_list.begin(), end = request_list.end(); iter != end; iter++)
    {
        request_t &req = *iter;
        if(  !( req.cstatus == STATUS_DONE || req.cstatus == STATUS_ERR || req.cstatus == STATUS_ACK)  ){
            LOG_D (TAG,"send_done: req %s:%llu status is not done or error or ack. status:%d", req.catalog_id.c_str(), req.id, req.cstatus);
            continue;
        }

        bool ret = send_cloud_msg(req, req.cstatus);
        if(req.type == TYPE_VOD) {
           send_hs_vod_status(req, ret);
        }
        if(ret) {
            req.cstatus = STATUS_DEL;
            del = true;
            LOG_I(TAG, "send_done: Making request status to STATUS_DEL for req: %s:%llu", req.catalog_id.c_str(), req.id);
            //reset the sync_failure counter
            shadow_sync_failure_count = 0;
        }
        else
        {
            LOG_I(TAG, "send_done: Failed sending DONE/ERR/ACK for req: %s:%llu", req.catalog_id.c_str(), req.id);
            handle_shadow_sync_retry();
        }

        LOG_I(TAG, "send_done: Send status for request: %s:%llu status: %d", req.catalog_id.c_str(), req.id, req.cstatus);
    }

    if (del)
    {
        send_internal_msg(INTERNAL_AWS_DELETE);
    }

    return true;
}

bool handle_livestream_res(res_livestreaming_data_t *msg)
{

    uint64_t req_id = 0;
    string catalog_id = "";
    bool found = false;
    LOG_I(TAG, "received handle_livestream_res %d %d", msg->msg_idx, msg->idx);

    list<livestream_req_t>::iterator iter = livestream_list.begin();
    while (iter != livestream_list.end())
    {
        LOG_I(TAG, "handle_livestream_res: msg_idx:%d\titer_idx:%d", msg->idx, iter->idx);
        if (msg->idx == iter->idx)
        {
            LOG_I(TAG, "handle_livestream_res: msg->idx and iter->idx matches");
            req_id = iter->req_id;
            catalog_id = iter->catalog_id;
            found = true;
            iter = livestream_list.erase(iter);
            break;
        }
        else
        {
            LOG_I(TAG, "handle_livestream_res: msg->idx and iter->idx doesn't match");
            iter++;
        }
    }

    if (!found)
    {
        LOG_I(TAG, "cannot find req_id");
        return false;
    }

    for (list<request_t>::iterator iter = request_list.begin(), end = request_list.end(); iter != end; iter++)
    {
        LOG_I(TAG, "handle_livestream_res: catalog_id: %s:%s\treq_id:%llu:%llu", iter->catalog_id.c_str(), catalog_id.c_str(), iter->id, req_id);
        if ((iter->catalog_id == catalog_id) && (iter->id == req_id))
        {
            LOG_I(TAG, "handle_livestream_res: msg->status=%d", msg->status);
            if (msg->status != LIVE_STREAMING_DONE)
            {
                LOG_E(TAG, "%s: %llu status is not LIVE_STREAMING_DONE", catalog_id.c_str(), req_id);
                iter->cstatus = STATUS_ERR;
                if(msg->dual_streaming){
                    iter->dual_livestream_cameras[0].error = msg->error_cam_one;
                    iter->dual_livestream_cameras[1].error = msg->error_cam_two;
                }
                else {
                    iter->error = msg->error_cam_one;
                }
                send_internal_msg(INTERNAL_AWS_DONE);
            }
            else
            {
                iter->cstatus = STATUS_DONE;
                LOG_I(TAG, "handle_livestream_res: Sending done for req : %s:%llu", (iter->catalog_id.c_str()), (iter->id));
                send_internal_msg(INTERNAL_AWS_DONE);
            }
            break;
        }
    }

    return true;
}

bool send_misc_reqs_resp()
{
    json_t *jkey_val = NULL;

    bool reset_pub_key = false;

    for (list<misc_req_t>::iterator iter = misc_request_list.begin(), end = misc_request_list.end(); iter != end; iter++)
    {
        misc_req_t &req = *iter;
        if (req.cstatus != STATUS_DONE && req.cstatus != STATUS_ERR)
        {
            LOG_D(TAG, "send_misc_reqs_resp:request %s:%s  not in STATUS_DONE or STATUS_ERR , status:%d", req.key.c_str(), req.value.c_str(), req.cstatus);
            continue;
        }
        else if (req.cstatus != STATUS_DONE)
        {
            LOG_I(TAG, "send_misc_reqs_resp:request %s:%s  not in STATUS_DONE status:%d", req.key.c_str(), req.value.c_str(), req.cstatus);
            continue;
        }
        else
        {

            jkey_val = json_object();

            if (req.key == "cameras")
            {
                json_object_set(jkey_val, req.key.c_str(), json_integer(atoi(req.value.c_str())));
            }
            else if (req.key == "vehicleClass" || req.key == "auth_method" || req.key == "private_key_status")
            {
                json_object_set(jkey_val, req.key.c_str(), json_string(req.value.c_str()));

                if (req.key == "private_key_status" && req.value == "reset")
                {
                    reset_pub_key = true;
                    register_pub_key = true;
                    LOG_I(TAG, "request from cloud: ~~~~~~~~ reset keys ~~~~~~~~");
                }
            }
            else if (req.key == KEY_KA_CERTIFICATE_CHECK)
            {
                int ka_ca_cert_check_disabled;
                if (!string_to_integer(req.value, ka_ca_cert_check_disabled))
                {
                    ka_ca_cert_check_disabled = 0;
                }
                json_object_set(jkey_val, req.key.c_str(), json_boolean(ka_ca_cert_check_disabled));
            }
            else if (req.key == KEY_CONFIG_VALIDATION)
            {
                int config_val_int = 0;
                if (!string_to_integer(req.value, config_val_int))
                {
                    config_val_int = 0;
                }
                json_object_set(jkey_val, req.key.c_str(), json_boolean(config_val_int));
            }
            else if (req.key == "vin")
            {
                json_object_set(jkey_val, req.key.c_str(), json_string(req.value.c_str()));
            }

            else if (req.key == "data_recording") {
                json_error_t error;
                json_t *jdata_rec = json_loads(req.value.c_str(), 0, &error);
                if(jdata_rec == NULL) {
                    LOG_E(TAG,"JSON creation failed.");
                    LOG_E(TAG,"error: on line %d: %s", error.line, error.text);
                    return false;
                }
                json_object_set (jkey_val, req.key.c_str(), jdata_rec);
            }

            else if ( "vehicleDetailHash" == req.key ) {
                json_object_set (jkey_val, req.key.c_str(), json_string(req.value.c_str()));
            }

            json_t *jstate = json_object();
            json_object_set(jstate, "reported", jkey_val);

            json_t *root = json_object();
            json_object_set(root, "state", jstate);
            char *req_params = json_dumps(root, 0);

            if (req_params == NULL)
            {
                LOG_E(TAG, "JSON dumps failed.");
                json_decref(root);
                return false;
            }
            string str(req_params);
            LOG_I(TAG, "sending %s to cloud", str.c_str());

            bool send_status = iot->send_response(str, req.shadow_type);
            if (send_status)
            {
                LOG_I(TAG, "response sent for %s:%s request", req.key.c_str(), req.value.c_str());
                req.cstatus = STATUS_DEL;
                send_internal_msg(INTERNAL_AWS_DELETE);
            }
            else
            {
                LOG_I(TAG, "response sending for %s:%s request failed", req.key.c_str(), req.value.c_str());
            }
            free(req_params);
            json_decref(root);
        }
    }

    // init public_key_registration
    if ((reset_pub_key || (get_priv_key_status() == PRIV_KEY_STATUS_NONE && get_auth_method() == AUTH_METHOD_NONE)) && register_pub_key)
    {
        register_pub_key = false;
        init_public_key_registration();
    }
    return true;
}

bool update_device_vod_count(int counter) {
    bool status(false);
    const char *const json_data_key = "deviceVodCount";
    json_t *root = json_object();

    LOG_I(TAG, "Entered %s", __func__);

    do {
        if (NULL == root) {
            LOG_E(TAG, "%s: root NULL", __func__);
            break;
        }



        json_t *jkey_val = json_object();

        if (NULL == jkey_val) {
            LOG_E(TAG, "%s: jkey_val NULL", __func__);
            break;
        }

        json_object_set_new(jkey_val, json_data_key, json_integer(counter));

        json_t *jstate = json_object();
        json_object_set(jstate, "reported", jkey_val);
        json_object_set(root, "state", jstate);
        char *str_ptr = json_dumps(root, 0);
        const std::string str = std::string(str_ptr);
        free(str_ptr);

        LOG_I(TAG, "Updating shadow with deviceVodCount: %s",str.c_str());

        if (!iot->send_response(str)) {
            LOG_E(TAG, "Unable to update deviceVodCount");
            break;
        }

        LOG_I(TAG, "deviceVodCount %d updated in shadow", counter);

        status = true;
    } while(false);

    if (NULL != root) {
        json_decref(root);
    }
    return status;
}


void init_public_key_registration()
{
    LOG_I(TAG, "init public key registration");

    // share the one-time auth_token
    json_t *jkey_val = NULL;
    string json_key = "auth_token";
    string json_val = get_one_time_auth_token();

    jkey_val = json_object();

    json_object_set(jkey_val, json_key.c_str(), json_string(json_val.c_str()));

    json_t *jstate = json_object();
    json_object_set(jstate, "reported", jkey_val);

    json_t *root = json_object();
    json_object_set(root, "state", jstate);
    char *req_params = json_dumps(root, 0);

    if (req_params == NULL)
    {
        LOG_E(TAG, "JSON creation failed.");
        json_decref(root);
        return;
    }

    string str(req_params);
    LOG_I(TAG, "Updating shadow: auth_token %s", str.c_str());

    bool token_shared = iot->send_response(str);
    if (token_shared)
    {
        LOG_I(TAG, "One time auth token shared");

        // Call API for pub key registration
        string reg_jwt = "";
        string error_msg = "";

        if (get_registration_jwt(reg_jwt, json_val, error_msg))
        {
            do_public_key_registration(reg_jwt);
        }
        else
        {
            // will be retried after KEY_CORRUPTION_CHECK_INTERVAL secs
            log_auth_error(error_msg);
        }
    }
    else
    {
        // will be retried after KEY_CORRUPTION_CHECK_INTERVAL secs
        LOG_I(TAG, "Unable to send one time auth token");
    }
    json_decref(root);
    free(req_params);
}

// Delete requests that were finished and acknowledged by cloud//
bool do_delete()
{
    list<request_t>::iterator iter = request_list.begin();
    while (iter != request_list.end())
    {

        if (iter->cstatus == STATUS_DEL)
        {
            LOG_I(TAG, "Deleting off request: %s:%llu", iter->catalog_id.c_str(), iter->id);
            iter = request_list.erase(iter);
        }
        else
        {
            iter++;
        }
    }

    list<misc_req_t>::iterator iter2 = misc_request_list.begin();
    while (iter2 != misc_request_list.end())
    {
        if (iter2->cstatus == STATUS_DEL)
        {
            iter2 = misc_request_list.erase(iter2);
        }
        else
        {
            iter2++;
        }
    }
    return true;
}


bool send_cloud_msg(request_t &msg, aws_status_t status)
{
    LOG_I(TAG, "send_cloud_msg:: %s:%llu status:%d", msg.catalog_id.c_str(), msg.id, status);

    string res = get_response(msg, status);

    bool send_status = iot->send_response(res, msg.shadow_type);

    return send_status;
}

bool send_internal_msg(awsmsg_type_t type)
{
    awsiot_internal_msg_t do_msg;

    if (!send_msg((generic_msg_t *)&do_msg, (msg_type_t)type, sizeof(do_msg), get_msgq_name(), get_msgq_name(), msg_idx++))
    {
        LOG_E(TAG, "Cannot send message");
        return false;
    }

    return true;
}

bool handle_log_or_vod_res(res_upload_msg_t *msg)
{

    uint64_t req_id = 0;
    string catalog_id = "";
    bool found = false;
    LOG_I(TAG, "received handle_log_or_vod_res %d %d", msg->msg_idx, msg->idx);

    list<upload_req_t>::iterator iter = uploader_list.begin();
    while (iter != uploader_list.end())
    {
        LOG_I(TAG, "handle_log_or_vod_res: msg_idx:%d\titer_idx:%d", msg->idx, iter->idx);
        LOG_I (TAG, "msg_req_id: %llu  iter_req_id: %llu", msg->req_id, iter->req_id);
        if( msg->idx == iter->idx && msg->req_id == iter->req_id) {
            LOG_I (TAG,"handle_log_or_vod_res: idx and req_id matches");
            req_id = iter->req_id;
            catalog_id = iter->catalog_id;
            found = true;
            iter = uploader_list.erase(iter);
            break;
        }
        else
        {
            LOG_I(TAG, "handle_log_or_vod_res: msg->idx and iter->idx doesn't match");
            iter++;
        }
    }

    if (!found)
    {
        LOG_I(TAG, "cannot find req_id");
        return false;
    }

    for (list<request_t>::iterator iter = request_list.begin(), end = request_list.end(); iter != end; iter++)
    {
        LOG_I(TAG, "handle_log_or_vod_res: catalog_id: %s:%s\treq_id:%llu:%llu", iter->catalog_id.c_str(), catalog_id.c_str(), iter->id, req_id);
        if ((iter->catalog_id == catalog_id) && (iter->id == req_id))
        {
            LOG_I(TAG, "handle_log_or_vod_res: msg->status=%d", msg->status);
            if( msg->status == UPLOAD_ACK ) {
                LOG_I (TAG, "%s: %llu status is UPLOAD_ACK", catalog_id.c_str(), req_id);
                iter->cstatus = STATUS_ACK;
                send_internal_msg(INTERNAL_AWS_DONE);
            }
            else if( msg->status != UPLOAD_SUCCESS ) {
                LOG_E(TAG, "%s: %llu status is not UPLOAD_SUCCESS", catalog_id.c_str(), req_id);
                iter->cstatus = STATUS_ERR;
                send_internal_msg(INTERNAL_AWS_DONE);
            }
            else
            {
                iter->count++;
                LOG_I(TAG, "handle_log_or_vod_res: iter->count: %d cmdlistsize:%d", iter->count, iter->command_list.size());
                if (iter->count == iter->command_list.size())
                {
                    iter->cstatus = STATUS_DONE;
                    LOG_I(TAG, "handle_log_or_vod_res: Sending done for req : %s:%llu", (iter->catalog_id.c_str()), (iter->id));
                    send_internal_msg(INTERNAL_AWS_DONE);
                }
            }

            break;
        }
    }
    return true;
}

string get_msgq_name()
{
    return Q_NAME;
}

string get_uploaderq_name()
{
    return "UniUpload";
}
