#include <bits/stdc++.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/file.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/ipc.h>
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
#include <sdcard_utils.h>
#include <glob.h>
#include "installer_app.h"
#include "nd_time.h"
#include <gst/gst.h>
#include "installer_app.h"
#include "nd_time.h"
#include <nd_task.h>
#include "nd_file_utils.h"
#include "nd_gpio.h"
#include <system_utils.h>
#include <string>
#include <nd_net_utils.h>
#include <algorithm>
#include <vector>
#include <nd_ext_cam_utils.h>
#include <cstdlib>
#include <ctime>
#include <cmath>
#include <netinet/tcp.h>
#include <openssl/sha.h>
#include <memory>
#include <nd_factory.h>
#include "nd_accessory_db.h"
#include "ndmb/nd_msg_interface.h"
#include "ndmb/nd_mbclient.h"
#include "nd_accessory_db.h"
#include "nd_ext_cam_utils.h"
#include <nd_vbus_info.h>

constexpr const char* EXT_CAM_Q = "EXT_CAM";
using namespace std;

#define ROUTE_LOGS
#define TAG "INSTLR"

#define ERROR -1

struct CmdResp {
    std::string command_;
    std::string response_;
};

//nd_device_obj for factory class api
ND_DeviceFactory *nd_device_obj = NULL;

using ATGStatus_map = std::unordered_map <std::string, std::string>;

static const int TIMEOUT_FOR_FIRST_MSG = 5;
static const int MAX_CLIENT_ACCEPT_RETRIES = 3;

static const int WAIT_TIME_SECOND_RUN = 10;
static const int WAIT_TIME_HOTSPOT = 100;
static const int HUNDRED_MS = 100000;
static const int BUFFER_SIZE =  4096;
static const int MAX_NO_OF_CLIENT =1;
static const int UNIT_TIME=1;
static const int NVCAMERA_RESTART_TIME =4;
static const int CAMERA_STRING_SIZE =10;
static const int BUFF_SIZE = 4096;
int currently_streaming = -1;

string ip_address, port_address, socket_timeout;
static int time_zone = -800;   // Default for PST time

string log_dir = "/home/ubuntu/.nddevice/log/installer_app";
const string device_ip  = "10.42.0.1";
const string port   = "5000";
static const string default_socket_timeout ="90";
static bool ext_cam_mdvr_conf_feat_enable = false;
static bool ext_cam_feature_cloud_override = false;
static bool ext_cam_feature_cloud_override_value = false;
static string ext_cam_mdvr_conf_ssid = "";
static string ext_cam_ssid_cloud_override = "";
static const string ext_cam_filename = "/home/iriscli/files/ext_cam_installer_app_rec";
static const int EXT_CAM_STREAM_TIME = 15;
static string mdvr_ssid_global = "";
static const int MAX_NUM_CHANNELS = 4;
static int file_idx[MAX_NUM_CHANNELS] = {0, 0, 0, 0};
static const int MAX_EXT_CAM_FILES = 4;
static int stream_ready_file_idx[MAX_NUM_CHANNELS] = {-1, -1, -1, -1};
pthread_mutex_t ext_cam_data_mutex = PTHREAD_MUTEX_INITIALIZER;
static const int WAIT_TIME_GET_EXT_FNAME = 200;
static string ext_cam_stream_fname = "";
static int curently_stream_ext_file_idx[MAX_NUM_CHANNELS] = {-1, -1, -1, -1};
static bool ext_cam_record[MAX_NUM_CHANNELS] = {false, false, false, false};
static bool mdvr_time_set = false;

string device_ssid;
string device_type;
string app_version;

char str[INET_ADDRSTRLEN];
static const string queue_name = "response_queue";
static const string installer_queue = "installer_queue";

static const string gpio_level_info_file = GPIO_VALUE_FILE(IGNITION_GPIO);
static const int MAX_RETRY_COUNT = 4;
static const string default_wlan_interface = "wlan0";
static const string wlan_ip_address = "10.42.0.";
static string wlan_interface;
static const int DEFAULT_SLEEP_DURATION = 5;

static const string otacheck_state_path = "/dev/shm/nd_files_c/otacheck_state.txt";
static const int system_cmd_timeout =120;
static constexpr int kCommandTimeoutsecs = 15;
response_for_generic_cmd resp;
extern string str_generic_cmd;
static const int max_generic_cmd_len =1000;

static const string vehicle_config_path = "/home/ubuntu/config/vbus_installer.ini";

std::stringstream generic_str;

extern const string response_file;
extern const string command_file;

pthread_mutex_t file_lock = PTHREAD_MUTEX_INITIALIZER;

const char *const kInstallerConnIndicatorFile     = "/dev/shm/nd_files_c/installer_app_connected";

struct ext_cam_file_stream_s {
    int seq_no;
    int cam_no;
    int ch_num;
};

int connfd, transfd, listenfd_trans;
static int msg_id =0;
stringstream ss;

// message queue related

nd_msgq_t::nd_msg_t *msg;
nd_msgq_t *msg_response_q;
nd_msgq_t *msg_reader_q;

GstElement *pipeline = NULL;
GstBus *bus;
GstMessage *msg_1;
GError *error_1 = NULL;
GstClockTime *timeout;

char command_app[100];
char readbuff[BUFFER_SIZE]={0};

pthread_t reader_thread;
pthread_t response_thread;
pthread_t obd_run_thread;

static const int OBD_TIMEOUT = 60;
static const int ELD_TIMEOUT = 60;
bool obd_req_first_time = true;
pthread_mutex_t eld_info_lock = PTHREAD_MUTEX_INITIALIZER;
static const int ELD_NDMB_SUB_RETRY = 5;
ndmbmsg_eld_data_installer_app eld_ndmb_msg;
bool is_eld_populated = false;
std::string NDMB_ELD_INST = "NDMB_ELD_INST";
bool is_eld_subscribed = false;
static const int eld_subscribe_count = 1000;
static const int eld_subscribe_timeout = 3;

// diagnostic gps related data
static gps_msg_t diagnostic_gps_data;
static const string NDMB_GPS_INST = "NDMB_INSTALLER_APP_GPS";
pthread_mutex_t gps_data_lock = PTHREAD_MUTEX_INITIALIZER;
static const  int gps_subscribe_count = 10;
static const int gps_subscribe_timeout = 3;

enum obd_run_status_t {
    OBD_RUN_PROGRESS,
    OBD_RUN_SUCCESS,
    OBD_RUN_FAILURE,
    OBD_RUN_ERROR
};

obd_run_status_t obd_run_status = OBD_RUN_ERROR;
static const string Q_NAME = "installer_queue";
//Server Queue pointer
static nd_msgq_t *server_q;

// DHUBX related

Ext_Cam_Info extCamInfo;
const int REQ_EXT_CAM_INFO_TIMEOUT = 6;
std::mutex mutex_ext_cam_info;
std::condition_variable cv_ext_cam_info;

// LED blinking control

const char* nd_central_mq_name = "q_nd_central";
const char* kInstallerScanIndicatorFile = "/dev/shm/nd_files_c/installer_scan_ongoing";
bool nd_central_led_ack_received = false;
bool led_blinking_active = false;
int led_blinking_duration = 5; // in seconds
std::mutex led_blinking_mutex;
std::thread led_blinking_thread;

#ifdef BAGHEERA2
#define PWR_LED_GREEN ND_GPIO_PWR_LED_G
#define PWR_LED_BLUE ND_GPIO_PWR_LED_B
#define PWR_LED_RED ND_GPIO_PWR_LED_R

#elif KRAIT
#define PWR_LED_GREEN SYS_LEFT_GREEN
#define PWR_LED_BLUE SYS_LEFT_BLUE
#define PWR_LED_RED SYS_LEFT_RED
#endif

app_req_t appReqData; 

NDService *nd_service_obj; //nd service object, to detect crashes

static string get_msgq_name() {
    return Q_NAME;
}

static bool init_msgq() {
    //Create message queue
    server_q = nd_msgq_t::get_msgq( get_msgq_name(), nd_msgq_t::ND_MSGQ_SERVER );

    if( server_q == NULL ) {
        LOG_E(TAG, "Cannot create message queue");
        return false;
    }

    LOG_I(TAG, "Message queue created");
    return true;
}


bool stop_camrec_service(){

    static const string stop_camrec = "systemctl stop cam_rec.service";

    int ret =0;

    ret = system_execute ("stopping cam_rec service", stop_camrec);

    sleep (UNIT_TIME);

    if (ret !=0){
       LOG_E(TAG, "cam_rec service is not stopped after client connected\n");
       return false;
    }
    return true;
}

bool stop_bagheera_service(){

    static const string stop_bagheera = "systemctl stop bagheera.service";

    int ret =0;

    ret = system_execute ("stopping bagheera service", stop_bagheera);

    sleep (UNIT_TIME);

    if (ret !=0){
       LOG_E(TAG, "bagheera service is not stopped after client connected\n");
       return false;
    }
    return true;
}

/* Stop awsiot service to make sure camera override does not happen when
 * connected to installer app
 */
bool stop_awsiot_service(){

    static const string stop_awsiot = "systemctl stop awsiot.service";

    int ret =0;

    ret = system_execute ("stopping awsiot service", stop_awsiot);

    sleep (UNIT_TIME);

    if (ret !=0){
       LOG_E(TAG, "awsiot service is not stopped after client connected\n");
       return false;
    }
    return true;
}

bool start_gps_service(){

    static const string start_gps = "sudo systemctl start gps.service";

    int ret =0;

    ret = system_execute ("starting gps service", start_gps);

    sleep (UNIT_TIME);

    if (ret !=0){
       LOG_E(TAG, "gps service is not started after client connected\n");
       return false;
    }
    return true;
}

bool stop_start_nvcamera_service(bool up=false){

   int ret=0;

#ifndef BAGHEERA2
   static const string stop_nvcamera_daemon = "systemctl stop nvcamera-daemon.service";
   static const string start_nvcamera_daemon = "systemctl start nvcamera-daemon.service";
#else
   static const string stop_nvcamera_daemon = "systemctl stop  nvargus-daemon.service";
   static const string start_nvcamera_daemon = "systemctl start  nvargus-daemon.service";
#endif
   if (up == false)
   {
       ret = system_execute("stop nvcamera-daemon service", stop_nvcamera_daemon);

       if (ret !=0){
           LOG_E(TAG, "nvcamera-daemon service is not stopped\n");
           return false;
       }
   }
   else
   {
       ret = system_execute("start_nvcamera-daemon", start_nvcamera_daemon);
       if (ret !=0){
           LOG_E(TAG, "nvcamera-daemon service is not started\n");
           return false;
       }
   }
   return true;
}

bool timeout_setting_for_socket(int &s1, string timeout){
    fd_set readfds;
    struct timeval tv;
    int rv, n=0;

    n = s1 +1;

    LOG_I(TAG, "the value of n is %d\n", n);

    tv.tv_sec = atoi(timeout.c_str());     // 5 min time default
    // there is issue if we did not give explicit value to tv.usec
    // linux take sometime garbage value and select api fail with
    // error 22 (INVALID ARGUMENT)
    tv.tv_usec = 0;
    LOG_I(TAG, "value of timeout is %d\n", tv.tv_sec);

    while (1){

          FD_ZERO(&readfds);
          FD_SET(s1, &readfds);

          rv = select(n, &readfds, NULL, NULL, &tv);

          if ((rv < 0) && (errno!=EINTR)){
             LOG_E(TAG, "value of rv is %d", rv);
             return false;
          }

          if (rv > 0 && FD_ISSET(s1, &readfds)){

             LOG_I(TAG, "user is connected\n");
             return true;
          }

          if (rv == 0){
             return false;
          }

          LOG_I(TAG, "errno is EINTR so try again");
    }

    LOG_I(TAG, "exiting from timeout_setting_for_socket function\n");
    return true;
}

bool read_configurations(){

    Config_parser bagheera_config_parser (bagheera_config_path);
    bool get_override_val = true;
    bool is_val_overridden = false;

    LOG_I(TAG, "path of config file is %s\n", bagheera_config_path.c_str());

    if (!bagheera_config_parser.getParseStatus()){
        LOG_E(TAG, "Can not parse bagheera config fallback to static ip\n");
        ip_address = device_ip;
        port_address = port;
        socket_timeout = default_socket_timeout;
    }
    else {
        ip_address = bagheera_config_parser.getConfig("INSTALLER_APP","static_ip", device_ip);
        port_address = bagheera_config_parser.getConfig("INSTALLER_APP","static_port",port);
        socket_timeout = bagheera_config_parser.getConfig("INSTALLER_APP","socket_timeout", default_socket_timeout, get_override_val, is_val_overridden);
        wlan_interface = bagheera_config_parser.getConfig("INSTALLER_APP","wlan_interface", default_wlan_interface, get_override_val, is_val_overridden);
    }
    LOG_I(TAG, "following configuration from read_configuration function\n");
    LOG_I(TAG, "ip_address:%s, port_address:%s, socket_timeout: %s, wlan_iface: %s",
              ip_address.c_str(), port_address.c_str(), socket_timeout.c_str(), wlan_interface.c_str());

    // read config related to ext cam feature
    Config_parser mdvr_config (mdvr_config_file_path);
    if (mdvr_config.getParseStatus() != true) {
        LOG_E (TAG, "Error parsing %s", mdvr_config_file_path.c_str());
        return false;
    }

    bool feature_enabled = false;
    if( "true" == mdvr_config.getConfig(EXT_CAM_CONFIG_SECTION, EXT_CAM_FEATURE_CONTROL,"false") ) {
        LOG_I (TAG,"External camera feature is enabled in mdvr_config.ini file");
        feature_enabled = true;
    } else {
        LOG_I (TAG,"External camera feature is not enabled in mdvr_config.ini file");
    }

    ext_cam_mdvr_conf_feat_enable = feature_enabled;

    if( "true" == mdvr_config.getConfig(EXT_CAM_CONFIG_SECTION, EXT_CAM_FEATURE_CONTROL,"false",
                                        get_override_val, is_val_overridden) ) {
        LOG_I (TAG,"External camera feature is enabled in config file");
        ext_cam_feature_enabled = true;
    } else {
        LOG_I (TAG,"External camera feature is not enabled in both mdvr_config.ini and bagheera_override.ini file");
    }

    if(is_val_overridden) {
        ext_cam_feature_cloud_override = true;
        ext_cam_feature_cloud_override_value = ext_cam_feature_enabled;
    }

    string ssid = mdvr_config.getConfig (EXT_CAM_CONFIG_SECTION, EXT_CAM_SSID_NAME, "");
    if (ssid == "") {
        LOG_E (TAG,"wifi ssid name is empty in mdvr_config.ini");
    }

    ext_cam_mdvr_conf_ssid = ssid;

    is_val_overridden = false;
    ssid = mdvr_config.getConfig (EXT_CAM_CONFIG_SECTION, EXT_CAM_SSID_NAME, "", get_override_val, is_val_overridden);
    if (ssid == "") {
        LOG_E (TAG,"wifi ssid name is empty in both mdvr_config.ini and bagheera_override.ini");
        LOG_I (TAG, "External camera feature is considered disabled due to missing ssid in config file");
        ext_cam_feature_enabled = false;
    } else {
        mdvr_ssid_global = ssid;
    }

    if(is_val_overridden) {
        ext_cam_ssid_cloud_override = ssid;
    }

    string password = mdvr_config.getConfig (EXT_CAM_CONFIG_SECTION, EXT_CAM_SSID_PASS, "", get_override_val, is_val_overridden);
    return true;
}

#ifdef PULL_MVVR_FILES_FROM_IA
static void *receive_ext_cam_file(void *args) {

    int ch_num = *((int *)args);
    stream_file_resp_t res;

    if(ch_num <= 0 || ch_num > MAX_NUM_CHANNELS) {
        LOG_E(TAG, "ch_num invalid");
        sleep(UNIT_TIME);
        pthread_exit(NULL);
    }

    while (1) {

        bool file_retrieved = false;
        string ext_cam_record_filename = ext_cam_filename + to_string(ch_num) + to_string(file_idx[ch_num-1]) + ".mp4";
        if(file_is_present(ext_cam_record_filename)) {
            if(file_delete(ext_cam_record_filename) == false) {
                LOG_I(TAG, "file delete failed");
            }
        }
        // set time zone before trying to pull file from mDVR
        set_time_zone(time_zone);
        LOG_I(TAG, "stream_live_file ch_num: %d", ch_num);
        res = stream_live_file(ext_cam_record_filename, EXT_CAM_STREAM_TIME, ch_num, 30, true);

        LOG_I(TAG, "stream saved file resp: %d, ch_num: %d", res, ch_num);

        if(res != FILE_QUERY_SUCCESS) {
            sleep(UNIT_TIME);
            continue;
        }

        LOG_I(TAG, "setting stream ready file index for channel %d to %d", ch_num, file_idx[ch_num-1]);
        stream_ready_file_idx[ch_num-1] = file_idx[ch_num-1];
        sleep(UNIT_TIME);

        // If the file we are going to newly write is streaming to app, better jump to next file
        do {
            pthread_mutex_lock(&ext_cam_data_mutex);
            if(file_idx[ch_num-1] == MAX_EXT_CAM_FILES-1) {
                file_idx[ch_num-1] = 0;
            } else {
                file_idx[ch_num-1]++;
            }
            pthread_mutex_unlock(&ext_cam_data_mutex);
        } while(curently_stream_ext_file_idx[ch_num-1] == file_idx[ch_num-1]);
    }

    pthread_exit(NULL);
}
#endif

bool create_server_socket(int &fd, string port) {
    struct sockaddr_in serv_addr;
    int status;
    bool res=false;
    int port_number=0;

    LOG_I(TAG, "ip address is %s\n", ip_address.c_str());

    fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd < 0){
        LOG_E(TAG, "socket creation failed\n");
        return false;
    }

    LOG_I(TAG, "socket created and value of sock des is %d\n", fd);

    memset(&serv_addr, '0', sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;

    serv_addr.sin_addr.s_addr = inet_addr(ip_address.c_str());

    port_number = atoi(port.c_str());
    LOG_I(TAG, "port number is %d\n", port_number);

    serv_addr.sin_port = htons(port_number);

    const int optval = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int)) < 0) {
        LOG_E(TAG, "setsockopt(SO_REUSEADDR) failed, errno:%d", errno);
    }

    if (setsockopt(fd, SOL_SOCKET, TCP_NODELAY, &optval, sizeof(int)) < 0) {
        LOG_E(TAG, "setsockopt(TCP_NODELAY) failed, errno:%d", errno);
    }

    if (bind(fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr))<0) {

       LOG_E(TAG, "binding the socket failed: %s", strerror(errno));
       close(fd);
       return false;
    }

    LOG_I(TAG, "bind the socket\n");

    if (listen(fd, MAX_NO_OF_CLIENT)< 0){
        LOG_E(TAG, "listen call failed\n");
        close(fd);
        return false;
    }

    return true;
}

bool accept_client_connection(int &listenfd, int &fd, string timeout) {
    struct sockaddr_in client_address;
    int client_len = sizeof(client_address);

    LOG_I(TAG, "waiting for client to connect: fd: %d", listenfd);

    // using non blocking select api
    bool res = timeout_setting_for_socket(listenfd, timeout);

    if (res){

       fd = accept(listenfd, (struct sockaddr*)&client_address, (socklen_t*)&client_len);

       LOG_I(TAG, "socket created successful and value of fd is %d\n", fd);

       if (fd < 0){
           LOG_E(TAG, "accept call failed exit and kill the thread\n");
           fd = -1;
           return false;
        }
        else{
             LOG_I(TAG, "client connected\n");
        }

        inet_ntop(AF_INET, &(client_address.sin_addr), str, INET_ADDRSTRLEN);

        LOG_I(TAG, "IP address of client is %s\n", str);
        return true;
    }

    close(listenfd);
    listenfd = -1;

    LOG_E(TAG, "exiting the accept connection function\n");
    return false;
}


static bool get_stream_ext_cam_fname(void *args) {
    int ch_num = *((int *)args);
    stringstream ss;
    LOG_I(TAG, "received stream request for channel %d", ch_num);

    // set mdvr time if not already set by this service
    if(!mdvr_time_set) {
        mdvr_time_set = set_mdvr_time();
    }

    while(stream_ready_file_idx[ch_num-1] == -1) {
        if(!ext_cam_record[ch_num-1]) {
            LOG_I(TAG, "got stop stream request for ch %d", ch_num-1);
            return false;
        }
        usleep(HUNDRED_MS);
    }

    LOG_I(TAG, "stream file now ready");

    curently_stream_ext_file_idx[ch_num-1] = stream_ready_file_idx[ch_num-1];

    LOG_I(TAG, "stream_file_idx for ch no %d: %d", ch_num, stream_ready_file_idx[ch_num-1]);
    ext_cam_stream_fname = ext_cam_filename + to_string(ch_num) + to_string(stream_ready_file_idx[ch_num-1]) + ".mp4";
    if(file_is_present(ext_cam_stream_fname) == false) {
        LOG_E(TAG, "file %s to stream absent", ext_cam_stream_fname.c_str());
        return false;
    }

    return true;
}

bool response_streaming(int camera_no, int sequence, bool status){

   response_for_streaming t2;
   t2.response = status;
   t2.argument = camera_no;
   t2.sequence_no = sequence;
   t2.ext_cam_enabled = ext_cam_feature_enabled;

   if( false == send_msg( (generic_msg_t *)&t2, (msg_type_t)RESPONSE_STREAMING, sizeof(t2), queue_name, queue_name, msg_id++ ) ) {
      LOG_E (TAG,"sending msg to response thread failed\n");
      return false;
   }

   LOG_I(TAG, "response for streaming successful from main\n");

   return true;
}

static bool run_obd_binary(void *args) {
    bool resp = stop_service((const string)"obd.service");
    if (resp){
        LOG_I(TAG, "stopped obd service from IA");
    }
    string cmd = "/home/ubuntu/.nddevice/latest/service/obd/obd_app1 installer_app";
    LOG_I(TAG, "running obd binary; cmd: %s", cmd.c_str());

    obd_run_status = OBD_RUN_PROGRESS;
    int res = system_execute("RUN_OBD_BINARY", cmd);
    if(res != 0) {
        LOG_E(TAG, "failed to run obd binary with installer app argument");
        return false;
    }

    return true;
}

void *obd_run_thread_main(void *args) {
    //run obd binary with "installer_app" argument
    task_result_t task_result = nd_timed_task (run_obd_binary, WAIT_TIME_HOTSPOT, NULL, "run obd binary");

    if(task_result == TASK_SUCCESS) {
        LOG_I(TAG, "obd binary run successfully");
        obd_run_status = OBD_RUN_SUCCESS;
    } else {
        LOG_E(TAG, "running obd binary timed-out or failed");
        obd_run_status = OBD_RUN_FAILURE;
    }

    pthread_exit(NULL);
}

static void *transfer_ext_cam_file(void *args) {
    ext_cam_file_stream_s file_stream_info;
    memcpy(&file_stream_info, args, sizeof(ext_cam_file_stream_s));
    bool file_sent = 0;

    ext_cam_stream_fname = "";
    task_result_t task_result = nd_timed_task (get_stream_ext_cam_fname, WAIT_TIME_GET_EXT_FNAME, (void*)&(file_stream_info.ch_num), "get ext cam fname task");
    if (task_result != TASK_SUCCESS || ext_cam_stream_fname == "") {
        response_streaming(file_stream_info.cam_no, file_stream_info.seq_no, false);
        pthread_exit(NULL);
    }

    if(ext_cam_record[file_stream_info.ch_num-1] == false) {
        pthread_exit(NULL);
    }

    int listenfd_trans = -1;
    bool res = create_server_socket(listenfd_trans, "5001");
    if (!res){
        LOG_E(TAG, "something went wrong in server soket creation function");
        response_streaming(file_stream_info.cam_no, file_stream_info.seq_no, false);
        pthread_exit(NULL);
    }

    LOG_I(TAG, "streaming %s file", ext_cam_stream_fname.c_str());
    response_streaming(file_stream_info.cam_no, file_stream_info.seq_no, true);

    do {

        bool res = accept_client_connection(listenfd_trans, transfd, "100");
        if (!res){
            LOG_E(TAG, "something went wrong in accept connect function");
            break;
        }

        long filesize = file_size(ext_cam_stream_fname);
        long bytes_sent = 0;
        char buffer[BUFF_SIZE];

        if(filesize <= 0) {
            LOG_E(TAG, "size of file to transfer empty or bad");
            break;
        }

        FILE *fp = fopen(ext_cam_stream_fname.c_str(), "r");
        if(fp == NULL) {
            LOG_E(TAG, "failed to open file for transfer");
            break;
        }

        if(ext_cam_record[file_stream_info.ch_num-1] == false) {
            LOG_E(TAG, "got stop stream request");
            break;
        }
        LOG_I(TAG, "starting file transfer to installer app of size %ld", filesize);
        // stream file with name ext_cam_stream_fname
        while(bytes_sent < filesize){
	    int read = fread(buffer, sizeof(char), BUFF_SIZE, fp);
	    int sent = send(transfd, buffer, read, 0);
            if(sent < 0 || ext_cam_record[file_stream_info.ch_num-1] == false) {
                LOG_I(TAG, "failed to send data to socket");
                break;
            }
            LOG_D(TAG, "read %d bytes, sent %d bytes", read, sent);
	    bytes_sent += read;
        }

        file_sent = 1;
    } while (false);

    pthread_mutex_lock(&ext_cam_data_mutex);
    curently_stream_ext_file_idx[currently_streaming-3] = -1;
    pthread_mutex_unlock(&ext_cam_data_mutex);

    if(transfd != -1) {
        close(transfd);
        transfd = -1;
    }

    pthread_exit(NULL);
}

bool start_streaming(int argument, int sequence_no){

    char camera_no_string[CAMERA_STRING_SIZE];

    currently_streaming =argument;

    // need to restart the nvcamera-daemon service and wait for sometime

    sprintf(camera_no_string, "%d", argument);

    LOG_I(TAG, "IP address of client is %s\n", str);

    ss.str(std::string());

    LOG_I(TAG, "******camera string is %s ********\n", camera_no_string);

    if (argument < BACK_CAM || argument > EXT_CAM_4) {

        LOG_E(TAG, "app has sent wrong cam id %d\n", argument);
        return response_streaming(argument, sequence_no, false);
    } else if (argument == OUT_CAM) {

        ss
        << "v4l2src device=/dev/video0 ! video/x-raw , width=(int)1920 , "
        << "height=(int)1080 , format=(string)UYVY ! nvvidconv ! "
        << "video/x-raw(memory:NVMM) , width=(int)640 , height=(int)352 , "
        << "format=(string)NV12 ! nvv4l2h265enc bitrate=300000 insert-sps-pps=true "
        << "iframeinterval=30 idrinterval=30 ! "
        << "h265parse ! video/x-h265,stream-format=(string)byte-stream ! "
        << "rtph265pay pt=96 ! udpsink host=" << str << " port=4000";

    } else if (argument >= EXT_CAM_1) {
        int ch_num = argument-3;
        if(ext_cam_feature_enabled == false) {
            return response_streaming(argument, sequence_no, false);
        }

        ext_cam_file_stream_s file_stream_info;
        file_stream_info.seq_no = sequence_no;
        file_stream_info.cam_no = argument;
        file_stream_info.ch_num = ch_num;

        ext_cam_record[ch_num-1] = true;
        pthread_t file_transfer_th;
        // Thread for file transfer
        if ( pthread_create (&file_transfer_th, NULL, transfer_ext_cam_file, (void *)&file_stream_info) != 0 ) {
            LOG_E(TAG, "Can't create extrenal camera file transfer thread");
            return response_streaming(argument, sequence_no, false);
        }

        usleep(HUNDRED_MS);
        return true;
    } else {

        ss
        << "nvarguscamerasrc sensor-id=" << camera_no_string
        << " ! video/x-raw(memory:NVMM),width=640, height=352, framerate=30/1, format=NV12 ! "
        << "nvv4l2h265enc bitrate=300000 insert-sps-pps=true "
        << "iframeinterval=30 idrinterval=30 ! "
        << "h265parse ! queue ! "
        << "video/x-h265,stream-format=(string)byte-stream ! rtph265pay pt=96 ! "
        << "udpsink host=" << str  <<  " port=4000";
    }

    LOG_I(TAG, "command for streaming\n");
    LOG_I(TAG, "%s\n", ss.str().c_str());

    GError* err = NULL;
    pipeline = gst_parse_launch ((ss.str().c_str()), &err);

    if( err != NULL ) {
        LOG_E(TAG, "gstreamer failed to create pipeline\n");
        LOG_E(TAG, "%s\n", err->message);
        g_error_free(err);
        return response_streaming(argument, sequence_no, false);
    }

    if (pipeline == NULL){
        LOG_E(TAG, "gst parse launch failed\n");
        gst_object_unref (pipeline);
        //send error message to app
        return response_streaming(argument, sequence_no, false);
    }

    sleep(UNIT_TIME);
    auto ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        LOG_E(TAG, "Unable to set the pipeline to the playing state.\n");
        gst_object_unref(pipeline);
        pipeline = NULL;
        return response_streaming(argument, sequence_no, false);
    } else {
        LOG_I(TAG, "gst_element_set_state: %d\n", ret);
    }

    return response_streaming(argument, sequence_no, true);
}

bool clear_pipeline() {
    sleep(UNIT_TIME * 2); // delay to avoid failure cases in frequent stream stop start
    GstState pending, state;
    if (NULL != pipeline) {
        gboolean eventStatus = gst_element_send_event (pipeline, gst_event_new_eos());
        LOG_I(TAG, "gst_element_send_event eventStatus: %d", eventStatus);

        GstStateChangeReturn retValue = gst_element_set_state(pipeline, GST_STATE_PAUSED);
        LOG_I(TAG, "gst_element_set_state retValue: %d", retValue);

        retValue = gst_element_get_state(pipeline, &state, &pending, GST_CLOCK_TIME_NONE);
        LOG_I(TAG, "gst_element_get_state state: %d, pending: %d, retValue: %d", state, pending, retValue);

        if ((GST_STATE_CHANGE_SUCCESS == retValue) && (GST_STATE_PAUSED == state)) {
            LOG_I(TAG, "success and paused");
        }

        gst_element_set_state(pipeline, GST_STATE_READY);
        retValue = gst_element_get_state(pipeline, &state, &pending, GST_CLOCK_TIME_NONE);

        if ((GST_STATE_CHANGE_SUCCESS == retValue) && (GST_STATE_READY == state)) {
            LOG_I(TAG, "success and Ready");
        }

        gst_element_set_state(pipeline, GST_STATE_NULL);
        LOG_I(TAG, "pipe state changed to NULL");

        usleep(100*1000);
        LOG_I(TAG, "gst_element_get_state state: %d, pending: %d, retValue: %d", state, pending, retValue);

        gst_object_unref(GST_OBJECT(pipeline));
        pipeline = NULL;
    } else {
        ss.str("");
        LOG_E(TAG, "pipeline NULL here: %s:%d\n", __func__, __LINE__);
        return false;
    }
    ss.str("");
    sleep(UNIT_TIME);
    LOG_I(TAG, "Exiting %s", __func__);
    return true;
}

bool stop_streaming(int cam_num){

        bool status = false;

        LOG_I(TAG, "Enter %s", __func__);
        if (cam_num == currently_streaming){
           status = clear_pipeline();
           if(currently_streaming >= EXT_CAM_1) {
               pthread_mutex_lock(&ext_cam_data_mutex);
               curently_stream_ext_file_idx[currently_streaming-3] = -1;
               pthread_mutex_unlock(&ext_cam_data_mutex);
               close(transfd);
               transfd = -1;
           }
           currently_streaming =-1;
           return status;
        }
        else {
             LOG_E(TAG, "app has sent wrong cam id to stop\n");
             return status;
        }
}

int gracefull_exit(int connfd) {
    if (NULL != pipeline) {
        GstState pending, state;
        auto retValue = gst_element_get_state(pipeline, &state, &pending, GST_CLOCK_TIME_NONE);
        LOG_I(TAG, "gst_element_get_state state: %d, pending: %d, retValue: %d", state, pending, retValue);

        if (state == GST_STATE_PLAYING) {
            LOG_E(TAG, "streaming is going on and and app closed\n");
        }

        LOG_I(TAG, "closing pipeline\n");

        retValue = gst_element_set_state(pipeline, GST_STATE_PAUSED);
        LOG_I(TAG, "gst_element_set_state ret: %d", retValue);

        retValue = gst_element_set_state(pipeline, GST_STATE_NULL);
        LOG_I(TAG, "gst_element_set_state ret: %d", retValue);

        gst_object_unref(pipeline);
        pipeline = NULL;
    } else {
        LOG_E(TAG, "pipeline NULL here: %s:%d\n", __func__, __LINE__);
    }

    close(connfd);
    // reboot the device to exit from debug mode
    LOG_I(TAG, "going to reboot system\n");
    if(send_powermon_to_reboot( Q_NAME, REQ_POWERMON_INSTALLER_APP_TO_REBOOT) == false) {
        system_reboot();
    }
}

bool timed_system_execute_with_response_wrapper(void *args) {
    bool status = false;
    std::shared_ptr<CmdResp> cmd_resp_ptr = *(static_cast<std::shared_ptr<CmdResp> *>(args));
    if (cmd_resp_ptr) {
        if (system_execute_with_resp("Timed Execute", cmd_resp_ptr->command_, cmd_resp_ptr->response_)) {
            status = true;
        } else {
            LOG_E(TAG, "failed to run at command to get gstatus");
        }
    } else {
        LOG_E(TAG, "cmd_resp_ptr null in timed_system_execute_with_response_wrapper");
    }
    return status;
}

bool timed_system_execute_with_response(const std::string &command, std::string &response) {
    bool status = false;
    std::shared_ptr<CmdResp> cmd_resp_ptr (new (std::nothrow) CmdResp());

    if (cmd_resp_ptr) {
        cmd_resp_ptr->command_ = command;
        task_result_t task_result = nd_timed_task(timed_system_execute_with_response_wrapper,
                                                  kCommandTimeoutsecs, (&cmd_resp_ptr), "timed_system_execute_with_response");
        if(TASK_SUCCESS == task_result) {
            status = true;
            response = cmd_resp_ptr->response_;
            size_t start_pos = 0;
            size_t find_itr = 0;
            LOG_I(TAG, "timed_system_execute_with_response output for %s", command.c_str());
            while((find_itr = response.find("\n", start_pos)) != std::string::npos) {
                LOG_I(TAG, "%s", response.substr(start_pos, find_itr - start_pos).c_str());
                start_pos = find_itr + 1;
            }
        } else {
            LOG_E(TAG, "timed_system_execute_with_response failed: %d", task_result);
        }
    } else {
        LOG_E(TAG, "timed_system_execute_with_response, no memory");
    }

    return status;
}

bool check_sim_status() {
    bool status (false);
    const char * const cmd = "lte_gps_sample_app 'AT!ICCID?'";
    std::string response;

    do {
        if (!timed_system_execute_with_response(cmd, response)) {
            LOG_E(TAG, "timed_system_execute_with_response for sim test failed");
            break;
        }

        if (std::string::npos == response.find("OK")) {
            LOG_E(TAG, "Sim status error");
            break;
        }

        status = true;
    } while (false);

    return status;
}

#ifndef BAGHEERA2
bool check_sdcard_status(){

   int ret =0;
   char str1[1000];

   static const string sdcard_test = "ls /dev/ | grep " + sdcard_devname + " > sdcard.txt";

   ret = system_execute("checking sdcard status", sdcard_test);
   if (ret !=0){
      LOG_I(TAG, "system call failed\n");
      return false;
   }

    FILE* in_file = fopen("sdcard.txt", "r");
    if (!in_file){
       LOG_E(TAG, "can not read sdcard file\n");
       return false;
    }

   while (fscanf(in_file, "%s", str1)!=EOF){
          LOG_I(TAG, "*********** %s *********\n", str1);
          if(strcmp(str1, "mmcblk1p1") == 0){
                fclose (in_file);
                system_execute("remove_sdcard.txt", "rm sdcard.txt");

                LOG_I(TAG, "first test for sd card done and sucessful\n");
                return true;

           }
           else {
                fclose (in_file);
                system_execute("remove_sdcard.txt", "rm sdcard.txt");
                LOG_I(TAG, "first test for sd card done and failed\n");
                return false;

           }
    }

   return false;

}
#endif
bool check_fan_status(){

    int fan_rpm =0;
    fan_rpm = nd_device_obj->get_fan_status();
    if (fan_rpm > 0)
        return true;
    else
        return false;

}

bool get_ignition_status(bool &ignition_status)
{
    std::ifstream gpio_val(nd_device_obj->gpio_ignition_level_info_file().c_str());
    char value;

    if(!gpio_val.is_open()) {
        LOG_E(TAG, "unable to open gpio value info file");
        return false;
    }

    // gpio_val >> value will return 0 incase it fail to read 1byte of data
    if(!(gpio_val >> value)) {
        LOG_E(TAG, "unable to read gpio value info file");
        gpio_val.close();
        return false;
    }

    gpio_val.close();

    if(value == '1') {
        ignition_status = true;
    } else {
        ignition_status = false;
    }

    LOG_I(TAG, "ignition status: %d", ignition_status);
    return true;
}

std::string& ltrim(std::string &s) {
    auto it = std::find_if(s.begin(), s.end(),
            [](char c) {
            return !std::isspace<char>(c, std::locale::classic());
            });
    s.erase(s.begin(), it);
    return s;
}

std::string& rtrim(std::string &s) {
    auto it = std::find_if(s.rbegin(), s.rend(),
            [](char c) {
            return !std::isspace<char>(c, std::locale::classic());
            });
    s.erase(it.base(), s.end());
    return s;
}

std::string& trim(std::string& s) {
    return ltrim(rtrim(s));
}

bool extract_first_int_from_string(const std::string &input, int &out_int) {
    if (input.empty()) {
        LOG_E(TAG, "input is empty @ %s:%d", __FUNCTION__, __LINE__);
        return false;
    }

    bool status = false;
    std::stringstream input_stream;
    input_stream << input;

    while (!input_stream.eof()) {
        std::string value;
        input_stream >> value;

        if (std::stringstream(value) >> out_int) {
            status = true;
            break;
        }
    }
    return status;
}

bool extract_first_float_from_string(const std::string &input, float &out_float) {
    if (input.empty()) {
        LOG_E(TAG, "input is empty @ %s:%d", __FUNCTION__, __LINE__);
        return false;
    }

    bool status = false;
    std::stringstream input_stream;
    input_stream << input;

    while (!input_stream.eof()) {
        std::string value;
        input_stream >> value;

        if (std::stringstream(value) >> out_float) {
            status = true;
            break;
        }
    }
    return status;
}

ATGStatus_map get_parsed_atgstatus_map(const std::string &cmd_resp) {
    // Add all line outputs that are possible
    // Refer examples below
    const std::vector<std::pair<std::string, std::string>> parseKeys{
        /* Reset Counter: 1 Mode: ONLINE*/
        {"Reset Counter", "Mode"},

        /* Bootup Time: 1 Mode: ONLINE */
        {"Bootup Time", "Mode"},

        {"System mode", "PS state"},

        /* LTE band: B5 LTE bw: 5 MHz */
        {"LTE band", "LTE bw"},
        {"LTE Rx chan", "LTE Tx chan"},
        {"PCC RxM RSSI", "RSRP (dBm)"},

        /* RSSI (dBm): 0 Tx Power: 0*/
        {"RSSI (dBm)", "Tx Power"},
        {"RSRP (dBm)", "TAC"},
        {"RSRQ (dB)", "Cell ID"},

        /* SINR (dB): 0 */
        {"SINR (dB)", ""},

        /* WCDMA band: WCDMA 800 */
        {"WCDMA band", ""},

        /* WCDMA channel: 4436 */
        {"WCDMA channel", ""},

        {"RxM RSSI C0", "RxD RSSI C0"},
        {"RxM RSSI C1", "RxD RSSI C1"}
    };

    /* indicates where to start the next search */
    size_t nextStartItr = 0;

    ATGStatus_map status_map;

    for (auto &keyPair: parseKeys) {
        nextStartItr = cmd_resp.find(keyPair.first, nextStartItr);

        if (std::string::npos == nextStartItr) {
            // if key not found from current position then try from beginning

            auto l_nextStartItr = cmd_resp.find(keyPair.first);

            if (std::string::npos == l_nextStartItr) {
                // key not found
                LOG_E(TAG, "\t====>error key:\"%s\" not found", keyPair.first.c_str());
                nextStartItr = 0;
                continue;
            }

            nextStartItr = l_nextStartItr;
        }

        if (!((nextStartItr == 0 || ('\n' == cmd_resp[nextStartItr - 1])))) {
            LOG_E(TAG, "\terror: does not begin with new line");
            continue;
        }

        string currentLine = cmd_resp.substr(nextStartItr, cmd_resp.find('\n', nextStartItr) - nextStartItr);

        // cout<<currentLine<<"\n";

        size_t itrKeyFirst = currentLine.find(keyPair.first);
        size_t tokenItrFirst = currentLine.find(':', itrKeyFirst);

        ++tokenItrFirst;

        size_t itrKeySecond = currentLine.find(keyPair.second, tokenItrFirst);
        size_t tokenItrSecond = currentLine.find(':', tokenItrFirst);

        // There is another key which is not given for parsing, hence continuing without extraction
        if (std::string::npos == itrKeySecond &&
            !keyPair.second.empty() &&
            std::string::npos != tokenItrSecond) {
            // invalid combination given
            // cannot extract keyPair.first, keyPair.second
            LOG_I(TAG, "\tskipping line: another key which is not given for parsing");
            continue;
        }

        if (std::string::npos == tokenItrSecond) {
            // only one key:value present
            // push back to map
            auto valueFrist = currentLine.substr(tokenItrFirst, currentLine.length() - tokenItrFirst);
            trim(valueFrist);
            valueFrist.shrink_to_fit();
            status_map[keyPair.first] = std::move(valueFrist);
            continue;
        }

        ++tokenItrSecond;

        // At this stage we have both the keys present in the given line
        auto valueFrist = currentLine.substr(tokenItrFirst, itrKeySecond - tokenItrFirst);
        auto valueSecond = currentLine.substr(tokenItrSecond);

        trim(valueFrist);
        trim(valueSecond);
        valueFrist.shrink_to_fit();
        valueSecond.shrink_to_fit();
        status_map[keyPair.first] = std::move(valueFrist);
        status_map[keyPair.second] = std::move(valueSecond);
    }

    return status_map;
}

void assign_signal_data(const ATGStatus_map &status_map, signal_data_s &signal_data) {
    auto statusMapItr = status_map.find("System mode");

    if (status_map.end() != statusMapItr) {
        if (std::string::npos != statusMapItr->second.find("LTE")) {
            signal_data.is_lte = true;
        }
    }

    statusMapItr = status_map.find("Mode");
    if (status_map.end() != statusMapItr) {
        if (std::string::npos != statusMapItr->second.find("ONLINE")) {
            signal_data.is_online = true;
        }
    }

    statusMapItr = status_map.find("PS state");
    if (status_map.end() != statusMapItr) {
        if (std::string::npos != statusMapItr->second.find("Attached")) {
            signal_data.is_attached = true;
        }
    }

    statusMapItr = status_map.find("LTE band");
    if (status_map.end() != statusMapItr) {
        std::string band_info = statusMapItr->second;
        auto num_itr = band_info.find_first_of("0123456789");
        if (std::string::npos != num_itr) {
            band_info.erase(band_info.begin(), band_info.begin() + num_itr);
            if (!extract_first_int_from_string(band_info, signal_data.band)) {
                LOG_E(TAG, "failed to get LTE band");
            }
        }
    } else {
        statusMapItr = status_map.find("WCDMA band");
        if (status_map.end() != statusMapItr) {
            std::string band_info = statusMapItr->second;
            auto num_itr = band_info.find_first_of("0123456789");
            if (std::string::npos != num_itr) {
                band_info.erase(band_info.begin(), band_info.begin() + num_itr);
                if (!extract_first_int_from_string(band_info, signal_data.band)) {
                    LOG_E(TAG, "failed to get WCDMA band");
                }
            }
        }
    }

    statusMapItr = status_map.find("LTE bw");
    if (status_map.end() != statusMapItr) {
        if (!extract_first_float_from_string(statusMapItr->second, signal_data.bw)) {
            LOG_E(TAG, "failed to get LTE bw");
        }
    }

    statusMapItr = status_map.find("LTE Rx chan");
    if (status_map.end() != statusMapItr) {
        if (!extract_first_int_from_string(statusMapItr->second, signal_data.channel)) {
            LOG_E(TAG, "failed to get LTE Rx chan");
        }
    } else {
        statusMapItr = status_map.find("WCDMA channel");
        if(status_map.end() != statusMapItr) {
            if (!extract_first_int_from_string(statusMapItr->second, signal_data.channel)) {
                LOG_E(TAG, "failed to get WCDMA channel");
            }
        }
    }

    statusMapItr = status_map.find("PCC RxM RSSI");
    if (status_map.end() != statusMapItr) {
        if (!extract_first_float_from_string(statusMapItr->second, signal_data.rssi)) {
            LOG_E(TAG, "failed to get PCC RxM RSSI");
        }
    } else {
        statusMapItr = status_map.find("RSSI (dBm)");
        if(status_map.end() != statusMapItr) {
            if (!extract_first_float_from_string(statusMapItr->second, signal_data.rssi)) {
                LOG_E(TAG, "failed to get RSSI (dBm)");
            }
        }
    }

    statusMapItr = status_map.find("RSRP (dBm)");
    if (status_map.end() != statusMapItr) {
        if (!extract_first_float_from_string(statusMapItr->second, signal_data.rsrp)) {
            LOG_E(TAG, "failed to get RSRP (dBm)");
        }
    }

    statusMapItr = status_map.find("RSRQ (dB)");
    if (status_map.end() != statusMapItr) {
        if (!extract_first_float_from_string(statusMapItr->second, signal_data.rsrq)) {
            LOG_E(TAG, "failed to get RSRQ (dB)");
        }
    }

    statusMapItr = status_map.find("SINR (dB)");
    if (status_map.end() != statusMapItr) {
        if (!extract_first_float_from_string(statusMapItr->second, signal_data.sinr)) {
            LOG_E(TAG, "failed to get SINR (dB)");
        }
    }

    statusMapItr = status_map.find("RxM RSSI C0");
    if (status_map.end() != statusMapItr) {
        if (!extract_first_float_from_string(statusMapItr->second, signal_data.rxm_rssi0)) {
            LOG_E(TAG, "failed to get RxMRSSI C0");
        }
    }

    statusMapItr = status_map.find("RxD RSSI C0");
    if (status_map.end() != statusMapItr) {
        if (!extract_first_float_from_string(statusMapItr->second, signal_data.rxd_rssi0)) {
            LOG_E(TAG, "failed to get RxDRSSI C0");
        }
    }

    statusMapItr = status_map.find("RxM RSSI C1");
    if (status_map.end() != statusMapItr) {
        if (!extract_first_float_from_string(statusMapItr->second, signal_data.rxm_rssi1)) {
            LOG_E(TAG, "failed to get RxMRSSI C1");
        }
    }

    statusMapItr = status_map.find("RxD RSSI C1");
    if (status_map.end() != statusMapItr) {
        if (!extract_first_float_from_string(statusMapItr->second, signal_data.rxd_rssi1)) {
            LOG_E(TAG, "failed to get RxDRSSI C1");
        }
    }
}

bool get_signal_data(signal_data_s &signal_data) {
    LOG_I(TAG, "trying to get signal data");

    bool status (false);

    const char * const cmd = "lte_gps_sample_app 'AT!GSTATUS?'";
    std::string response;

    do {
        if (!timed_system_execute_with_response(cmd, response)) {
            LOG_E(TAG, "timed_system_execute_with_response for GSTATUS failed");
            break;
        }

        if (std::string::npos == response.find("!GSTATUS:")) {
            // module error
            LOG_E(TAG, "Module error?");
            break;
        }

        ATGStatus_map status_map = get_parsed_atgstatus_map(response);
        assign_signal_data(status_map, signal_data);

        status = true;
    } while (false);

    return status;
}

bool start_services(){

    int ret;

    static const string start_all_service = "systemctl start `find /home/ubuntu/.nddevice/latest/service/ -name *.service ! -name nd_shutdown.service -printf '%f'`";
    ret = system_execute("starting all service", start_all_service);
    if (ret ==0)
       return true;
    else
       return false;
}

bool start_nvcamera_daemon_service (){

   int ret =0;
#ifndef BAGHEERA2
   static const string start_nvcamera_daemon = "systemctl start nvcamera-daemon.service";
#else
    static const string start_nvcamera_daemon = "systemctl start nvargus-daemon.service";
#endif

   ret = system_execute("start nvcamera_daemon", start_nvcamera_daemon);
   sleep (UNIT_TIME);
   if (ret !=0){
      return false;
   }
   return true;
}
bool response_diagnostic(response_for_diagnostic &diagnostic_data) {
    bool ret = false;
    if( false == send_msg( (generic_msg_t *)&diagnostic_data, (msg_type_t)RESPONSE_DIAGNOSTIC, sizeof(diagnostic_data), queue_name, queue_name, msg_id++ ) ) {
      LOG_E (TAG,"sending msg to response thread failed\n");
    } else {
      ret = true;
    }
    return ret;
}

bool check_gps_exist(string &gps_loc) {
    gps_loc.clear();
    bool status (false);
    // "gpsInfo": 'Lat: 36.6389, Lon: 121.607, Acc: 2.828 m, Sat:12',
    stringstream gps_info_stream;
    pthread_mutex_lock(&gps_data_lock);
    gps_info_stream << "Lat: " << diagnostic_gps_data.latitude;
    gps_info_stream << ", Lon: " << diagnostic_gps_data.longitude; 
    gps_info_stream << ", Acc: " << diagnostic_gps_data.accuracy;
    gps_info_stream << " m, Sat: " << diagnostic_gps_data.good_sattelites;
    status = diagnostic_gps_data.valid;
    pthread_mutex_unlock(&gps_data_lock);
    gps_loc = gps_info_stream.str(); 
    LOG_I(TAG, "gps_loc: %s", gps_loc.c_str());

    return status;
}

bool check_gps_satellite_info() {
    bool status (false);
    const char * const cmd = "lte_gps_sample_app 'AT!GPSSATINFO?'";
    std::string response;

    do {
        if (!timed_system_execute_with_response(cmd, response)) {
            LOG_E(TAG, "timed_system_execute_with_response for GPSSATINFO failed");
            break;
        }

        status = true;
    } while (false);

    return status;
}

bool check_imei_no(string &imei_data) {
    bool status (false);
    const char * const cmd = "lte_gps_sample_app 'ATI' | grep 'IMEI:'";
    std::string response;

    do {
        if (!timed_system_execute_with_response(cmd, response)) {
            LOG_E(TAG, "timed_system_execute_with_response for IMEI failed");
            break;
        }

        const size_t start_pos = response.find(':');

        if (std::string::npos == start_pos) {
            // module error
            LOG_E(TAG, "Module error?");
            break;
        }

        imei_data.assign(response, start_pos + 1, response.length() - (start_pos + 1));
        LOG_I (TAG, "imei no is '%s'", imei_data.c_str());

        status = true;
    } while (false);

    return status;
}

bool check_iccid_num(string &iccid_num) {
    bool status (false);
    const char * const cmd = "lte_gps_sample_app 'at!iccid?' | grep '!ICCID:'";
    std::string response;

    do {
        if (!timed_system_execute_with_response(cmd, response)) {
            LOG_E(TAG, "timed_system_execute_with_response for ICCID failed");
            break;
        }

        const size_t start_pos = response.find(':');

        if (std::string::npos == start_pos) {
            // module error
            LOG_E(TAG, "Module error?");
            break;
        }

        iccid_num.assign(response, start_pos + 1, response.length() - (start_pos + 1));
        LOG_I (TAG, "iccid no is '%s'", iccid_num.c_str());

        status = true;
    } while (false);

    return status;
}

bool ndmb_gps_cb(ndmb_generic_msg_t *msg) {
    gps_msg_t *gps_data = nullptr;
    LOG_I(TAG, "GPS Data =========================== Topic %s", msg->topic);
    string topic = msg->topic;
    if(topic == TOPIC_GPS_DATA) {
        gps_data = reinterpret_cast<gps_msg_t *>( msg );
        if (gps_data == nullptr) {
            LOG_E(TAG, "GPS Data is NULL");
            return false;
        }
        pthread_mutex_lock(&gps_data_lock);
        diagnostic_gps_data = *gps_data;
        pthread_mutex_unlock(&gps_data_lock);
        LOG_I(TAG, "GPS Data: %f, %f, %f, %d", diagnostic_gps_data.latitude, diagnostic_gps_data.longitude, diagnostic_gps_data.accuracy, diagnostic_gps_data.good_sattelites);
    } else {
	    LOG_E(TAG, "ndmb_gps_cb Invalid topic %s", topic.c_str());
    }
}

bool diagnostic_device_check(diagnostic_msg_t *diagnostic_msg) {
    bool ret = false;
    response_for_diagnostic diagnostic_data = response_for_diagnostic();

    diagnostic_data.sequence_no = diagnostic_msg->sequence_no;

    std::vector<std::string> diagnostic_params = {"simcard", "internet", "ignition", "gpsInfo", "signal", "imei", "iccid", "sd_card", "fan"};
    std::vector<std::string> updated_diagnostic_params;

    json_t *config_json = json_loads(diagnostic_msg->config_json, 0, NULL);
    if (config_json) {
        json_t *params = json_object_get(config_json, "params");
        if (params) {
            // params is a json_array of strings iterate over it
            size_t index;
            json_t *value;
            json_array_foreach(params, index, value) {
                const char *param = json_string_value(value);
                if (param) {
                    updated_diagnostic_params.push_back(param);
                    LOG_I(TAG, "param: %s", param);
                }
            }
        } else {
            LOG_E(TAG, "failed to get params from config_json");
        }
        json_decref(config_json);
    } else {
        LOG_E(TAG, "failed to load config_json");
    }

    if (updated_diagnostic_params.empty()) {
        updated_diagnostic_params = diagnostic_params;
    }

    LOG_I(TAG, "################## diagnostic device check ##################");
    // iterate over updated_diagnostic_params and check for each param
    for (auto &param: updated_diagnostic_params) {
        if (param == "sdcard") {
            diagnostic_data.sdcard_reqested = true;
#ifndef BAGHEERA2
            diagnostic_data.sdcard_status = check_sdcard_status();
#endif
            LOG_I(TAG, "diagnostic:sdcard_status: %d", diagnostic_data.sdcard_status);

        } else if (param == "simcard") {
            diagnostic_data.simcard_status_requested = true;
            diagnostic_data.simcard_status = check_sim_status();
            LOG_I(TAG, "diagnostic:simcard_status: %d", diagnostic_data.simcard_status);
        
        } else if (param == "fan") {
            diagnostic_data.fan_reqested = true;
            diagnostic_data.fan_status = check_fan_status();
            if (!diagnostic_data.fan_status){
                sleep(UNIT_TIME);
                LOG_E(TAG, "fan diagnostic failed once try one more time");
                diagnostic_data.fan_status = check_fan_status();
            }
            LOG_I(TAG, "diagnostic:fan_status: %d", diagnostic_data.fan_status);
        
        } else if (param == "internet") {
            diagnostic_data.internet_status_requested = true;
            diagnostic_data.internet_status = check_internet_exist();                           // key: internet
            LOG_I(TAG, "diagnostic:internet_status: %d", diagnostic_data.internet_status);
        
        } else if (param == "ignition") {
            diagnostic_data.ignition_requested = true;
            bool status = get_ignition_status(diagnostic_data.ignition_status);                 // key: ignition_status
            LOG_I(TAG, "diagnostic:get_ignition_status: %d", status);
            LOG_I(TAG, "diagnostic:ignition_status: %d", diagnostic_data.ignition_status);
        
        } else if (param == "gpsInfo") {
            diagnostic_data.gps_requested = true;
            string gps_info;
            diagnostic_data.gps_status = check_gps_exist(gps_info);                             // key: gps, gps_info
            nd_strncpy(diagnostic_data.gps_info, gps_info.c_str(), sizeof(diagnostic_data.gps_info));
            LOG_I(TAG, "diagnostic:gps_status: %d", diagnostic_data.gps_status);
            LOG_I(TAG, "diagnostic:gps_info: %s", diagnostic_data.gps_info);

        } else if (param == "signal") {
            diagnostic_data.signal_requested = true;
            memset(&diagnostic_data.signal_data, 0, sizeof(signal_data_s));                     // key: signal_info
            if(!get_signal_data(diagnostic_data.signal_data)) {
                diagnostic_data.signal_data.valid = false;
            } else {
                diagnostic_data.signal_data.valid = true;
            }
        
        } else if (param == "imei") {
            diagnostic_data.imei_requested = true;
            string imei_no;
            diagnostic_data.is_imei = check_imei_no(imei_no);                                   // key: imei_status, imei_no
            nd_strncpy(diagnostic_data.imei_no, imei_no.c_str(), sizeof(diagnostic_data.imei_no));
            LOG_I(TAG, "diagnostic:imei_status: %d", diagnostic_data.is_imei);
            LOG_I(TAG, "diagnostic:imei_no: %s", diagnostic_data.imei_no);
        
        } else if (param == "iccid") {
            diagnostic_data.iccid_requested = true;
            string iccid_no;
            diagnostic_data.is_iccid = check_iccid_num(iccid_no);                               // key: iccid_status, iccid_no
            nd_strncpy(diagnostic_data.iccid_no, iccid_no.c_str(), sizeof(diagnostic_data.iccid_no));
            LOG_I(TAG, "diagnostic:iccid_status: %d", diagnostic_data.is_iccid);
            LOG_I(TAG, "diagnostic:iccid_no: %s", diagnostic_data.iccid_no);
        } else {
            LOG_E(TAG, "unknown param: %s", param.c_str());
        }
    }
    LOG_I(TAG, "#############################################################");
    // There is case if installer run the diagnostic before streaming then
    // nvcamera-daemon service will be in stop state because of stop_services()
    // so atleast need to start nvcamera-daemon service
    start_nvcamera_daemon_service();

    ret = response_diagnostic(diagnostic_data);

    LOG_I(TAG, "diagnostic device check done");
    return ret;
}


void populate_device_id_global(){

   static const string device_config_path = "/home/ubuntu/config/deviceconfig.ini";

   Config_parser device_config_parser (device_config_path);

    if (!device_config_parser.getParseStatus()){
        LOG_E(TAG, "Can not parse device config");
        return;
    }

    device_ssid = device_config_parser.getConfig("identity","deviceid","0");
    LOG_I(TAG, "device id from config file is %s", device_ssid.c_str());

    device_type = device_config_parser.getConfig("identity","devicetype","bagheera");
    LOG_I(TAG, "device type from config file is %s", device_type.c_str());

}

void populate_app_version_global() {

    static const string nd_config_path = "/home/ubuntu/.nddevice/nddevice.ini";

    Config_parser nd_config_parser (nd_config_path);

    if (!nd_config_parser.getParseStatus()){
        LOG_E(TAG, "Can not parse nd config");
        return;
    }

    app_version = nd_config_parser.getConfig("version","ndDevice","");
    LOG_I( TAG, "app version from config file is: %s", app_version.c_str() );
}

bool service_related_activities(){

   bool res =false;

#if 0
   res = stop_start_nvcamera_service();

   if (!res){
       LOG_E(TAG, "nvcamera-daemon service not restarted\n");
       return false;
   }
   sleep (UNIT_TIME*2);
#endif

   res = stop_bagheera_service();
   if (!res){
       LOG_E(TAG, "bagheera service not stopped\n");
       return false;
   }

   res = stop_camrec_service();
   if (!res){
       LOG_E(TAG, "cam_rec service not stopped\n");
       return false;
   }

   sleep (NVCAMERA_RESTART_TIME);

#if 0
   res = stop_start_nvcamera_service(true);

   if (!res){
       LOG_E(TAG, "nvcamera-daemon service not restarted\n");
       return false;
   }
   sleep (UNIT_TIME);
#endif


   res = stop_awsiot_service();
   if (!res){
       LOG_E(TAG, "awsiot service not stopped\n");
       return false;
   }

    //start gps service
    res = start_gps_service();
    if (!res){
        LOG_E(TAG, "gps service not started\n");
        return false;
    }

    if(!stop_service("time_sync.service"))
    {
        sleep(UNIT_TIME);
        stop_service("time_sync.service");
    }

#ifdef BAGHEERA2
    static const std::string stop_fan_service_cmd = "echo 0  > /sys/devices/pwm-fan/temp_control";

    auto ret = system_execute("stop fan service", stop_fan_service_cmd);
    if (ret != 0) {
       LOG_E(TAG, "stop_fan_service_cmd failed\n");
       return false;
    }
#endif

    

   return true;
}

bool queue_server_creation(){

   msg_reader_q = nd_msgq_t::get_msgq( "reader_queue", nd_msgq_t::ND_MSGQ_SERVER);

   if( msg_reader_q == NULL ) {
       LOG_E(TAG,"Could not initialize message reader queue, Exiting");
       return false;
   }

   msg_response_q = nd_msgq_t::get_msgq( "response_queue", nd_msgq_t::ND_MSGQ_SERVER);


   if( msg_response_q == NULL ) {
       LOG_E(TAG,"Could not initialize message response server queue, Exiting");
       return false;
   }

   LOG_I(TAG, "exiting queue server creation func");
   return true;
}

bool ndmb_elddata(ndmb_generic_msg_t *msg){
    bool  ret = false;
    ndmbmsg_eld_data_installer_app *ptr1 = NULL;
    ptr1 = reinterpret_cast<ndmbmsg_eld_data_installer_app *>( msg );
    do {
        if(NULL == ptr1){
            LOG_E(TAG, "eld publisher failed to send data");
            break;
        }

        if( ptr1->topic != TOPIC_ELD_DATA_TO_IA ) {
            LOG_E(TAG, "Unkown topic: ->%s", ptr1->topic);
            break;
        }

        pthread_mutex_lock(&eld_info_lock);
            nd_strncpy(eld_ndmb_msg.eld_data, ptr1->eld_data, sizeof(eld_ndmb_msg.eld_data));
            is_eld_populated = true;
        pthread_mutex_unlock(&eld_info_lock);

        ret = true;
    } while(false);

    return ret;
}

static bool start_obd_from_installer_app() {
    string obd_service_start_cmd = "systemctl start obd.service";
    string obd_service_stop_cmd = "systemctl stop obd.service";
    int sys_exe_resp = 0;
    bool ret = false;
    do {
        // Stopping OBD service
        sys_exe_resp = system_execute("stop obd service from installer app", obd_service_stop_cmd);
        if(sys_exe_resp != 0) {
            LOG_E(TAG, "failed to stop obd service");
            break;
        }
        LOG_E(TAG, "eld_ info stopped obd service");  //will be removed
        
        // Creating file a temp file in /dev/shm/
        if(!file_touch(obd_temp_file)) {
            LOG_E(TAG, "could not create file: %s",obd_temp_file.c_str());
        }

        // Deleting the old obd_info file 
        if(!file_delete(obd_info_file)) {
            LOG_E(TAG, "could not delete file: %s",obd_temp_file.c_str());
        }

        pthread_mutex_lock(&eld_info_lock);
        is_eld_populated = false;
        pthread_mutex_unlock(&eld_info_lock);

        // Starting OBD service
        sys_exe_resp = system_execute("start obd service from installer app", obd_service_start_cmd);
        if(sys_exe_resp != 0) {
            LOG_E(TAG, "Error starting obd service");
            break;
        }
        LOG_I(TAG,"restarted obd service successfully");
        ret = true;
    } while (false);

    return ret;
}

void *obd_restart_thread_func(void *args) {
    if(!start_obd_from_installer_app()) {
        LOG_E(TAG, "Failed to restart obd service");
    }

    pthread_exit(NULL);
}

bool thread_creation_child_process(){

    int ret =0;

    LOG_I(TAG, "creating threads in child process\n");

    ret = pthread_create(&reader_thread, NULL, reader_thread_main, NULL);
    if (ret !=0){
        LOG_E(TAG,  "thread creation failed for reading\n");
        return false;
    }

    LOG_I(TAG, "reader thread created successful\n");

    ret = pthread_create(&response_thread, NULL, response_thread_main, NULL);
    if (ret !=0){
        LOG_E(TAG, "thread creation failed for response\n");
        return false;;
    }

    LOG_I(TAG, "response thread created successful\n");
    return true;
}

bool response_command_ack(int seq_no) {
    response_for_ack t2;
    t2.sequence_no = seq_no;
    if( false == send_msg( (generic_msg_t *)&t2, (msg_type_t)RESPONSE_INST_CMD_ACK, sizeof(t2), queue_name, queue_name, msg_id++ ) ) {
      LOG_E (TAG,"sending ack for command to response thread failed\n");
      return false;
   }
   return true;
}

bool response_testcon(int sequence, bool status){

   response_for_testconn t2;
   t2.response = status;
   t2.sequence_no = sequence;
   if( false == send_msg( (generic_msg_t *)&t2, (msg_type_t)RESPONSE_TESTCONN, sizeof(t2), queue_name, queue_name, msg_id++ ) ) {
      LOG_E (TAG,"sending msg to response thread ailed\n");
      return false;
   }
   LOG_I(TAG, "response for test conn from main is successful\n");

   return true;
}

bool response_stop_streaming(int sequence, bool status){

   response_for_stop_streaming t2;
   t2.response = status;
   t2.sequence_no = sequence;
   if( false == send_msg( (generic_msg_t *)&t2, (msg_type_t)RESPONSE_STOP_STREAMING, sizeof(t2), queue_name, queue_name, msg_id++ ) ) {
      LOG_E (TAG,"sending msg to response thread failed\n");
      return false;
   }
   LOG_I(TAG, "response for stop streaming successful from main\n");

   return true;
}

bool response_ignition_status(int sequence) {
    response_for_ignition_status resp;
    bool ignition_status = false;

    if(get_ignition_status(ignition_status) == false) {
        LOG_E(TAG, "failed to get ignition status");
    }
    resp.response = ignition_status;
    resp.sequence_no = sequence;

    if( false == send_msg( (generic_msg_t *)&resp, (msg_type_t)RESPONSE_IGNITION_STATUS_APP, sizeof(resp), queue_name, queue_name, msg_id++ ) ) {
        LOG_E (TAG,"sending msg to response thread failed\n");
        return false;
    }
    LOG_I(TAG, "response for ignition status successful from main\n");

    return true;
}

bool response_mdvr_wifi_strength(int sequence) {
    response_for_mdvr_wifi_strength resp;
    int wifi_quality = -1, wifi_strength = -1;
    bool response = false;
    string cmd_resp = "";

    if(ext_cam_feature_enabled) {
        string cmd = "iwlist wlan0 scan | grep -B2 " + mdvr_ssid_global + " | grep \"Quality\" | cut -d '=' -f2 | cut -d ' ' -f1 | cut -d '/' -f1";
        LOG_I(TAG, "quality cmd: %s", cmd.c_str());
        if(system_execute_with_resp("MDVR_WIFI_QUAL", cmd, cmd_resp) == false) {
            response = false;
        } else {
            if(string_to_integer(cmd_resp, wifi_quality) == false) {
                response = false;
            }
            response = true;
            LOG_I(TAG, "wifi quality: %d", wifi_quality);
        }

        cmd = "iwlist wlan0 scan | grep -B2 " + mdvr_ssid_global + " | grep \"Quality\" | cut -d '=' -f3 | cut -d ' ' -f1";
        cmd_resp = "";
        LOG_I(TAG, "strength cmd: %s", cmd.c_str());
        if(system_execute_with_resp("MDVR_WIFI_STRENGTH", cmd, cmd_resp) == false) {
            response = false;
        } else {
            if(string_to_integer(cmd_resp, wifi_strength) == false) {
                response = false;
            }
            response = true;
            LOG_I(TAG, "wifi strength: %d dBm", wifi_strength);
        }
    }

    resp.response = response;
    resp.wifi_quality = wifi_quality;
    resp.wifi_strength = wifi_strength;
    resp.sequence_no = sequence;
    resp.feature_enabled = ext_cam_feature_enabled;

    if( false == send_msg( (generic_msg_t *)&resp, (msg_type_t)RESPONSE_MDVR_WIFI_STRENGTH, sizeof(resp), queue_name, queue_name, msg_id++ ) ) {
        LOG_E (TAG,"sending msg to response thread failed\n");
        return false;
    }
    LOG_I(TAG, "response for mdvr wifi strength successful from main\n");
    return true;
}

bool check_obd_data() {
    bool ret = false;
    int count = 0;
    do{
        if(file_is_present(obd_info_file)) {
            ret = true;
            break;
        }
        sleep(UNIT_TIME);
        count = count + 1;
    } while (count <= OBD_TIMEOUT);
    if(!ret) {
        LOG_E(TAG, "obd info file is not present");
    }
    return ret;
}

bool response_obd_protocol(int sequence) {
    response_for_obd_protocol resp;
    resp.status = check_obd_data();
    resp.sequence_no = sequence;

    if (false == send_msg( (generic_msg_t *)&resp, (msg_type_t)RESPONSE_OBD_PROTOCOL_APP, sizeof(resp), queue_name, queue_name, msg_id++ )) {
        LOG_E (TAG,"sending msg to response thread failed\n");
        return false;
    }

    LOG_I(TAG, "response for obd protocol successful from main\n");
    return true;
}

bool response_obd_extended_check(int sequence) {
    response_for_obd_extended_check resp;
    resp.status = check_obd_data();
    resp.sequence_no = sequence;

    if (false == send_msg( (generic_msg_t *)&resp, (msg_type_t)RESPONSE_OBD_EXTENDED_CHECK, sizeof(resp), queue_name, queue_name, msg_id++ )) {
        LOG_E (TAG,"sending msg to response thread failed\n");
        return false;
    }

    LOG_I(TAG, "response for obd protocol successful from main\n");
    return true;
}

bool is_vbus_connected() {
    get_info_for_vbus(nd_service_obj);
    return is_vbus_connected(get_vbus_sn_());
}

bool response_eld_check(int sequence, bool check_eld) {
    response_for_eld_check resp;
    resp.status = false;
    resp.sequence_no = sequence;
    resp.error_code = 0;
    //checking for eld info

    // check if eld connected
    bool eld_connected = is_vbus_connected();
    string eld_info_str;
    if(!eld_connected) {
        LOG_E(TAG, "failed to check eld connection status");
        resp.error_code = -1000;
    } else {
        int count = 0;
        while(count < ELD_TIMEOUT && check_eld) {
            pthread_mutex_lock(&eld_info_lock);
            if(is_eld_populated) {
                LOG_I(TAG,"eld_msg: %s",eld_ndmb_msg.eld_data);
                pthread_mutex_unlock(&eld_info_lock);
                resp.status = true;
                break;
            }
            pthread_mutex_unlock(&eld_info_lock);
            sleep(UNIT_TIME);
            count = count + 1; 
        }
        pthread_mutex_lock(&eld_info_lock);
        eld_info_str = eld_ndmb_msg.eld_data;
        resp.status = is_eld_populated;
        pthread_mutex_unlock(&eld_info_lock);
        if(!resp.status) {
            LOG_E(TAG, "eld info not received within timeout");
            resp.error_code = -2000;
        }
    }

    if(resp.status) {
        printf(TAG, "eld info received from ndmb publisher: %s", eld_info_str.c_str());
        // write eld data to a file so response can read from file 
        // added this due to mq size limitation
        // eld file name = /dev/shm/eld_info_<epoch_time>.txt
        string eld_info_file_path = "/dev/shm/eld_info_" + to_string(get_system_time()) + ".txt";
        ofstream eld_info_file;
        eld_info_file.open (eld_info_file_path, std::ofstream::out | std::ofstream::trunc);
        eld_info_file << eld_info_str;
        eld_info_file.close();
        file_sync();
        LOG_I(TAG, "eld info file written");
        nd_strncpy(resp.eld_file_path, eld_info_file_path.c_str(), sizeof(resp.eld_file_path));
    }

    //sending data to resp thread
    if (false == send_msg( (generic_msg_t *)&resp, (msg_type_t)RESPONSE_ELD_DIAGNOSTIC, sizeof(resp), queue_name, queue_name, msg_id++ )) {
        LOG_E (TAG,"sending msg to response thread failed\n");
        return false;
    }
    LOG_I(TAG, "response for eld protocol successful from main\n");
    return true;
}

bool response_mdvr_config(int command_sequence, string mdvr_ssid, string mdvr_pwd)
{
    response_for_mdvr_config resp;

    ofstream mdvr_config_file;
    mdvr_config_file.open (mdvr_config_file_path, std::ofstream::out | std::ofstream::trunc);
    mdvr_config_file << "[ext_cam_config]\n";
    mdvr_config_file << "enabled=true\n";
    mdvr_config_file << "ssid=";
    mdvr_config_file << mdvr_ssid;
    mdvr_config_file << "\npassword=";
    mdvr_config_file << mdvr_pwd;
    mdvr_config_file.close();

    LOG_I(TAG, "mdvr config file written");
    resp.sequence_no = command_sequence;
    resp.response = true;

    if( false == send_msg( (generic_msg_t *)&resp, (msg_type_t)RESPONSE_MDVR_CONFIG, sizeof(resp), queue_name, queue_name, msg_id++ ) ) {
        LOG_E (TAG,"sending msg to response thread failed\n");
        return false;
    }
    LOG_I(TAG, "response for mdvr config successful from main\n");

    return true;
}

bool response_mdvr_config_file(int command_sequence, string mdvr_config, string app_ver)
{
    response_for_mdvr_config resp;

    bool status = false;
    do {
        // while removing mdvr data with mdvr_file command db content need to be updated
        // if mdvr_config file does not contain ssid then remove mdvr data from db (this is to support old apps to unpair db data)
        if (mdvr_config.find("ssid") == string::npos) {
            LOG_I(TAG, "ssid not found in config file");
            LOG_I(TAG, "Going to remove mdvr data from db");
            nd::device::AccessoryDB accessory_db;
            auto error = accessory_db.RemoveAllOfType("DHUBX");
            if(error.first != 0) {
                LOG_E(TAG, "Failed to remove mdvr data from db %d, %s", error.first, error.second.c_str());
                break;
            } else {
                LOG_I(TAG, "mdvr data removed from db");
            }
        }

        ofstream mdvr_config_file;
        mdvr_config_file.open (mdvr_config_file_path, std::ofstream::out | std::ofstream::trunc);
        mdvr_config_file << mdvr_config;
        mdvr_config_file.close();

        LOG_I(TAG, "mdvr config file written");
    
        // for old apps that do not send app version, force rename ext_cam section to ext_cam_config
        if(app_ver == "none") {
            LOG_I(TAG, "changing the ext_cam section name to ext_cam_config");
            stringstream output;
            output.str("");
            ifstream config_file(mdvr_config_file_path);
            while(!config_file.eof()) {
                string line;
                getline(config_file, line);
                if(line.find("[ext_cam]") != string::npos) {
                    output << "[ext_cam_config]" << endl;
                } else {
                    output << line << endl;
                }
            }
            config_file.close();

            // write modified content to file
            mdvr_config_file.open (mdvr_config_file_path, std::ofstream::out | std::ofstream::trunc);
            mdvr_config_file << output.str();
            mdvr_config_file.close();
        }

        status = true;
    } while (false);

    resp.sequence_no = command_sequence;
    resp.response = status;

    if( false == send_msg( (generic_msg_t *)&resp, (msg_type_t)RESPONSE_MDVR_CONFIG, sizeof(resp), queue_name, queue_name, msg_id++ ) ) {
        LOG_E (TAG,"sending msg to response thread failed\n");
        return false;
    }
    LOG_I(TAG, "response for mdvr config file successful from main\n");

    return true;
}

bool response_mdvr_feature_check(int sequence) {
    response_for_mdvr_feature_check resp;

    resp.response = ext_cam_mdvr_conf_feat_enable;
    resp.sequence_no = sequence;
    resp.cloud_override = ext_cam_feature_cloud_override;
    resp.cloud_override_value = ext_cam_feature_cloud_override_value;
    string mdvr_id = "";

    size_t pos = ext_cam_mdvr_conf_ssid.find("fther");
    if(pos != string::npos) {
        mdvr_id = ext_cam_mdvr_conf_ssid.substr(5, ext_cam_mdvr_conf_ssid.length()-5);
    }

    nd_strncpy(resp.mdvr_id, mdvr_id.c_str(), MAX_MDVR_ID_LENGTH);
    resp.mdvr_id_length = strlen(mdvr_id.c_str());

    pos = ext_cam_ssid_cloud_override.find("fther");
    if(pos != string::npos) {
        mdvr_id = ext_cam_ssid_cloud_override.substr(5, ext_cam_ssid_cloud_override.length()-5);
    }

    nd_strncpy(resp.cloud_mdvr_id, mdvr_id.c_str(), MAX_MDVR_ID_LENGTH);
    resp.cloud_mdvr_id_length = strlen(mdvr_id.c_str());

    LOG_I(TAG, "mdvr_id sent to response thread: %s, length: %d", resp.mdvr_id, resp.mdvr_id_length);

    if( false == send_msg( (generic_msg_t *)&resp, (msg_type_t)RESPONSE_MDVR_FEATURE_CHECK, sizeof(resp), queue_name, queue_name, msg_id++ ) ) {
        LOG_E (TAG,"sending msg to response thread failed\n");
        return false;
    }
    LOG_I(TAG, "response for mdvr feature check successful from main\n");
    return true;
}

bool response_mdvr_sdcard_check(int sequence) {

    // check mdvr sdcard
    int sdcard_size = -1;
    bool sdcard_proper = check_mdvr_sdcard(sdcard_size);
    if(sdcard_proper == false) {
        LOG_I(TAG, "some issue with mdvr sdcard, sdcard_size: %d KB", sdcard_size);
    } else {
        LOG_I(TAG, "mdvr sdcard setting is proper with size: %d KB", sdcard_size);
    }

    response_for_mdvr_sdcard_check resp;

    resp.response = sdcard_proper;
    resp.sequence_no = sequence;
    resp.sdcard_size_kb = sdcard_size;

    if( false == send_msg( (generic_msg_t *)&resp, (msg_type_t)RESPONSE_MDVR_SDCARD_CHECK, sizeof(resp), queue_name, queue_name, msg_id++ ) ) {
        LOG_E (TAG,"sending msg to response thread failed\n");
        return false;
    }
    LOG_I(TAG, "response for mdvr sdcard check successful from main\n");
    return true;
}

bool response_pair_unpair_accessory(int sequence, string error_code_str, bool response, bool pair_accessory) {
    response_pair_unpair_accessory_msg_t resp;
    resp.sequence_no = sequence;
    resp.response = response;
    nd_strncpy(resp.resp_json_char, error_code_str.c_str(), sizeof(resp.resp_json_char));
    resp.pair_accessory = pair_accessory;

    bool ret = false;

    do {
        if( false == send_msg( (generic_msg_t *)&resp, (msg_type_t)RESPONSE_PAIR_UNPAIR_ACCESSORY, sizeof(resp), queue_name, queue_name, msg_id++ ) ) {
            LOG_E (TAG,"sending msg to response thread failed\n");
            break;
        }
        LOG_I(TAG, "response for pair unpair accessory command successful from main\n");
        ret = true;
    } while (false);
    return ret;
}
string typecast_json_val_to_string(json_t *value) {
     stringstream ss;
    if(json_is_string(value)) {
        ss << json_string_value(value);
    } else if(json_is_integer(value)) {
        ss << json_integer_value(value);
    } else if(json_is_boolean(value)) {
        ss << json_boolean_value(value);
    } else if(json_is_real(value)) {
        ss << json_real_value(value);
    } else {
        LOG_E(TAG, "unknown json type");
    }
    return ss.str();
}
bool write_json_to_config(string config_file_path, char *config_json, json_t *error_code){
    json_error_t json_error;
    json_t *root, *value, *config_value;
    const char *config_section, *config_key;
    int error = 0;
    bool ret = false;
    root = json_loads(config_json, 0, &json_error);

    do {
        // write vehicle data to config file
        if (root == NULL) {
            LOG_E(TAG, "json parsing failed");
            break;
        }

        bool is_file_present = file_is_present(config_file_path);

        //checking if config file is present
        if (is_file_present == false) {
            LOG_E(TAG, "config file not present");
            LOG_I(TAG, "creating config file");
            if(file_touch(config_file_path) == false) {
                LOG_E(TAG, "creating config file failed");
                break;
            }
        }
        Config_parser file_config_parser (config_file_path);

        if(is_file_present) {
             if (file_config_parser.getParseStatus() == false) {
                LOG_E(TAG, "config file parsing failed");
                break;
            }
        }

        json_object_foreach(root, config_section, value) { 

            if(json_typeof(value) == JSON_OBJECT) {

                const char* config_key;
                json_t *config_value;
                json_object_foreach(value, config_key, config_value) {
                    error = 1;
                    //converting json values to string
                    string config_value_str = typecast_json_val_to_string(config_value);

                    //updating config file
                    if(file_config_parser.isPresent(config_section, config_key)) {

                        if(false == file_config_parser.updateConfig(config_section, config_key, config_value_str)) {
                            LOG_E(TAG, "updating vehicle config file failed");
                            break;
                        }
        
                    } else {

                        if(false == file_config_parser.addConfig(config_section, config_key, config_value_str)) {
                            LOG_E(TAG, "adding vehicle config file failed");
                            break;
                        }
                    }
                    error = 0;
                }
                if(error != 0){
                    json_t *error_array = json_array();
                    json_array_append(error_array, json_integer(error));
                    json_object_set_new(error_code, config_section, error_array);
                }
            }
        }
        if (error == 0) {
            ret = true;
        }
    } while (false);
    //free json objects
    json_decref(root);
    return ret;
}

void led_blinking_timer(int time_in_second)
{
    LOG_I (TAG," led_blinking_timer(): time_in_second::  %d ", time_in_second);
    std::this_thread::sleep_for(std::chrono::seconds(time_in_second));
    std::lock_guard<std::mutex> lock(led_blinking_mutex);
    if (led_blinking_active)
    {
        nd_device_obj->nd_clear_led(PWR_LED_GREEN);
        nd_device_obj->nd_clear_led(PWR_LED_BLUE);
        nd_device_obj->nd_clear_led(PWR_LED_RED);
        nd_device_obj->nd_set_led_on(GREEN, POWER_LED, true );
        LOG_I (TAG," led_blinking_timer(): led_blinking stopped... ");
        led_blinking_active = false;
    }
    else
    {
        LOG_I (TAG,"Error: Not BLINKING ");
    }
}

void secondary_msgq() {
    // receive message from server_queue
    nd_msgq_t::nd_msg_t *smsg = NULL;

    while (1) {
        smsg = server_q->receive();
        if (smsg == NULL) {
            LOG_E(TAG, "data receive failed\n");
            continue;
        }

        generic_msg_t *m = (generic_msg_t *)smsg->get_buffer();

        if (NULL == m) {
            LOG_E(TAG, "msg->get_buffer() returned NULL");
            continue;
        }

        switch( m->msg_type ) {
            case RES_DHUB_INFO_INSTALLER_APP: {
                LOG_I(TAG, "fetch all accessory response received");
                //Ext_Cam_Info
                Ext_Cam_Info *ext_cam_info = (Ext_Cam_Info *)m;

                nd_strncpy(extCamInfo.fwVersion, ext_cam_info->fwVersion, sizeof(extCamInfo.fwVersion));
                nd_strncpy(extCamInfo.ip, ext_cam_info->ip, sizeof(extCamInfo.ip));
                nd_strncpy(extCamInfo.mode, ext_cam_info->mode, sizeof(extCamInfo.mode));
                nd_strncpy(extCamInfo.serialNumber, ext_cam_info->serialNumber, sizeof(extCamInfo.serialNumber));
                LOG_I(TAG, "ext cam info received: fwVersion: %s, serialNumber: %s, ip: %s, mode: %s", extCamInfo.fwVersion, extCamInfo.serialNumber, extCamInfo.ip, extCamInfo.mode);
                cv_ext_cam_info.notify_all();
                break;
            } 

            case INSTALLER_SCAN_INDICATE:
            {
                LOG_I(TAG,"INSTALLER_SCAN_INDICATE msg received");
                {
                    std::lock_guard<std::mutex> lock(led_blinking_mutex);
                    if (!led_blinking_active)
                    {
                        nd_device_obj->nd_clear_led(PWR_LED_GREEN);
                        nd_device_obj->nd_clear_led(PWR_LED_BLUE);
                        nd_device_obj->nd_clear_led(PWR_LED_RED);
                        nd_device_obj->nd_blink_led(PWR_LED_BLUE, DUTYCYCLE_50);
                        led_blinking_active = true;
                        LOG_I(TAG,"Inst Led blink started:: led_blinking_duration: %d ", led_blinking_duration);

                        if (led_blinking_thread.joinable()) {
                            led_blinking_thread.join();
                        }
                        led_blinking_thread = std::thread(led_blinking_timer, led_blinking_duration);
                    }
                    else
                    {
                        LOG_I(TAG,"Led is already blinking hence ignore this event for led blink");
                    }
                }
                break;
            }

                
            default:
                LOG_E(TAG, "unknown message type received: %d", m->msg_type);
                break;
        }
    }
    return;
}

bool populate_extcam_config() {
    string ssid = "";
    string sn = "NA";
    bool ret = false;
    do {
        if (false == file_is_present(mdvr_config_file_path)) {
            LOG_E(TAG, "config file not present");
            break;
        }

        Config_parser mdvr_config(mdvr_config_file_path);
        if (mdvr_config.getParseStatus() != true) {
            LOG_E (TAG, "Error parsing %s", mdvr_config_file_path.c_str());
        }

        ssid = mdvr_config.getConfig (EXT_CAM_CONFIG_SECTION, EXT_CAM_SSID_NAME, "");

        if(ssid == "") {
            LOG_E(TAG,"DHUB Serial Number not found");
            break;
        }

        if(ssid.find("fther") == string::npos) {
                 LOG_E(TAG,"Invalid DHUB Serial Number");\
                break;
        } 
        sn = ssid.substr(ssid.find("fther") + 5);
    } while (false);

    nd_strncpy(extCamInfo.fwVersion, "NA", sizeof(extCamInfo.fwVersion));
    nd_strncpy(extCamInfo.ip, "NA", sizeof(extCamInfo.ip));
    nd_strncpy(extCamInfo.mode, "NA", sizeof(extCamInfo.mode));
    nd_strncpy(extCamInfo.serialNumber, sn.c_str(), sizeof(extCamInfo.serialNumber));
    return ret;
}

bool response_fetch_all_accessory(int sequence) {
    response_fetch_all_accessory_msg_t resp;
    resp.sequence_no = sequence;
    bool ret = false;

    do {
        req_ext_cam_info_t req;
        req.toggle_dhub_mode = appReqData.dhubx.toggle_dhub_mode;
        if (false == send_msg( (generic_msg_t *)&req, (msg_type_t)REQ_DHUB_INFO_INSTALLER_APP, sizeof(req), queue_name,EXT_CAM_Q, msg_id++ )) {
            LOG_E (TAG,"sending msg to ext cam failed");
        } else {
            LOG_I(TAG, "fetch all accessory command sent to ext cam");
        }

        {
            std::unique_lock<std::mutex> lock(mutex_ext_cam_info);
            if (cv_ext_cam_info.wait_for(lock, std::chrono::seconds(REQ_EXT_CAM_INFO_TIMEOUT)) == std::cv_status::timeout) {
                LOG_E(TAG, "fetch accessory from ext cam timed out");
                LOG_E(TAG, "fetch accessory from ext cam failed");
                if( false == populate_extcam_config() ) {
                    LOG_E(TAG, "populate_extcam_config failed");
                }
            }
        }
        resp.ext_cam_info = extCamInfo;

        LOG_I(TAG, "ext cam info: fwVersion: %s, serialNumber: %s, ip: %s, mode: %s", resp.ext_cam_info.fwVersion, resp.ext_cam_info.serialNumber, resp.ext_cam_info.ip, resp.ext_cam_info.mode);
        if( false == send_msg( (generic_msg_t *)&resp, (msg_type_t)RESPONSE_FETCH_ALL_ACCESSORY, sizeof(resp), queue_name, queue_name, msg_id++ ) ) {
            LOG_E (TAG,"sending msg to response thread failed\n");
            break;
        }
        LOG_I(TAG, "response for fetch all accessory command successful from main\n");
        ret = true;
    } while (false);
    return ret;
}

bool set_vehicle_data(char *config_json, string &error_code_str) {
    bool response = false;
    json_t *error_code = json_object();

    response = write_json_to_config(vehicle_config_path, config_json, error_code);

    error_code_str = json_dumps(error_code, JSON_COMPACT);
    LOG_I(TAG, "set_vehicle_data error_code_str: %s", error_code_str.c_str());

    if(error_code_str.empty()) {
        error_code_str = "{}";
        LOG_E(TAG, "error code json dump failed");
    }
    json_decref(error_code);
    return response;
}

bool response_set_vehicle_config(int sequence, string error_code_str, bool response) {
    response_vehicle_config_msg_t resp;
    resp.sequence_no = sequence;
    resp.response = response;
    nd_strncpy(resp.error_code, error_code_str.c_str(), sizeof(resp.error_code));

    bool ret = false;

    do {
        if( false == send_msg( (generic_msg_t *)&resp, (msg_type_t)RESPONSE_VEHICLE_CONFIG, sizeof(resp), queue_name, queue_name, msg_id++ ) ) {
            LOG_E (TAG,"sending msg to response thread failed\n");
            break;
        }
        LOG_I(TAG, "response for vehicle config command successful from main\n");
        ret = true;
    } while (false);
    return ret;
}

bool response_check_vbus_conn(int sequence) {
    LOG_I(TAG, "check vbus conn response received");
    response_check_vbus_conn_msg_t resp;
    resp.sequence_no = sequence;
    resp.vbus_connected = is_vbus_connected();
    if( false == send_msg( (generic_msg_t *)&resp, (msg_type_t)RESPONSE_CHECK_VBUS_CONN, sizeof(resp), queue_name, queue_name, msg_id++ ) ) {
        LOG_E (TAG,"sending msg to response thread failed\n");
        return false;
    }
    LOG_I(TAG, "response for check vbus conn command successful from main\n");
    return true;
}

bool handle_dhubx_prequisites(json_t *data) {
    bool ret = false;
    do {
        if(NULL == json_object_get(data, "dhub_ssid") && NULL == json_object_get(data, "dhub_pwd")) {
            LOG_E(TAG, "ssid and password not present in json");
            break;
        }

        if(false == json_is_string(json_object_get(data, "dhub_ssid"))) {
            LOG_E(TAG, "ssid not present in json");
            break;
        }
        string ssid = json_string_value(json_object_get(data, "dhub_ssid"));
        
        if(ssid.empty()) {
            LOG_E(TAG, "ssid is empty");
            break;
        }

        if(false == json_is_string(json_object_get(data, "dhub_pwd"))) {
            LOG_E(TAG, "password not present in json");
            break;
        }

        string password = json_string_value(json_object_get(data, "dhub_pwd"));

        if(password.empty()) {
            LOG_E(TAG, "password is empty");
            break;
        }

        //checking if config file is present
        bool is_file_present = file_is_present(mdvr_config_file_path);
        if (!is_file_present) {
            LOG_W(TAG, "config file not present");
            LOG_I(TAG, "creating config file");
            if(false == file_touch(mdvr_config_file_path)) {
                LOG_E(TAG, "creating config file failed");
                break;
            }
        }
        Config_parser file_config_parser (mdvr_config_file_path);
        if (is_file_present) {
            if (false == file_config_parser.getParseStatus()) {
                LOG_E(TAG, "config file parsing failed");
                break;
            }
        }

        if(file_config_parser.isPresent("ext_cam_config", "ssid")) {
            if(false == file_config_parser.updateConfig("ext_cam_config", "ssid", ssid)) {
                LOG_E(TAG, "updating dhubx ssid failed");
                break;
            }
        } else {
            if(false == file_config_parser.addConfig("ext_cam_config", "ssid", ssid)) {
                LOG_E(TAG, "adding dhubx ssid failed");
                break;
            }
        }

        if(file_config_parser.isPresent("ext_cam_config", "password")) {
            if(false == file_config_parser.updateConfig("ext_cam_config", "password", password))
            {
                LOG_E(TAG, "updating dhubx password failed");
                break;
            }
        } else {
            if(false == file_config_parser.addConfig("ext_cam_config", "password", password)) {
                LOG_E(TAG, "adding dhubx password failed");
                break;
            }
        }

        if (file_config_parser.isPresent("ext_cam_config", "enabled")) {
            if(false == file_config_parser.updateConfig("ext_cam_config", "enabled", "true")) {
                LOG_E(TAG, "updating dhubx enabled failed");
                break;
            }
        } else {
            if(false == file_config_parser.addConfig("ext_cam_config", "enabled", "true")) {
                LOG_E(TAG, "adding dhubx enabled failed");
                break;
            }
        }

        ret = true;
    } while(false);

    if (ret == false) {
        LOG_E(TAG, "dhubx prerequisites failed");
        file_delete(mdvr_config_file_path);
    }

    return ret;
}
void handle_vbus_prerequisites() {
    LOG_I(TAG, "vbus prerequisites handling");
    if(file_delete(vehicle_config_path)) {
        LOG_I(TAG, "vbus config file deleted successfully");
    } else {
        LOG_W(TAG, "vbus config file deletion failed or file not present");
    }
}
bool handle_pair_prerequisites(json_t *data, string &accessory_type) {
    bool ret = false;
    do {

        if (accessory_type == "DHUBX") {
            if(!handle_dhubx_prequisites(data)) {
                break;
            }
        }
        else if (accessory_type == "VBUS") {
            handle_vbus_prerequisites();
        }
        else {
            LOG_I("There is no prerequisites for accessory type: %s", accessory_type.c_str());
        }
        ret = true;
    } while(false);

    if(ret == false) {
        LOG_E(TAG, "prerequisites failed for accessory type: %s", accessory_type.c_str());
    }

    return ret;
}

bool handle_unpair_prerequisites(json_t *data, string &accessory_type) {
    bool ret = false;
    do {
        if (accessory_type == "DHUBX") {
            LOG_I(TAG, "dhubx prerequisites");
            if( file_is_present(mdvr_config_file_path)) {
                if(!file_delete(mdvr_config_file_path)) {
                    break;
                }
            }
        } else {
            LOG_I("There is no prerequisites for accessory type: %s", accessory_type.c_str());
        }
        ret = true;
    } while(false);

    if(!ret) {
        LOG_E(TAG, "prerequisites failed for accessory type: %s", accessory_type.c_str());
    }

    return ret;
}

bool handle_pair_unpair_accessory(char *pair_unpair_data, string &error_code_str, bool pair_accessory) {
    bool ret = false;
    json_error_t json_error;
    json_t *root, *data_array, *data, *error_code_json = json_object();;
    json_t *err_code_array = json_array();
    json_t *success_array = json_array();
    string accessory_type;

    do
    {
        root = json_loads(pair_unpair_data, 0, &json_error);
        if (root == NULL) {
            LOG_E(TAG, "json parsing failed");
            break;
        }
        if(! json_is_string(json_object_get(root, "accessory_type"))) {
            LOG_E(TAG, "accessory_type not present in json");
            break;
        }

        accessory_type = json_string_value(json_object_get(root, "accessory_type"));

        if (accessory_type.empty()) {
            LOG_E(TAG, "accessory_type is empty");
            break;
        }

        json_object_set_new(error_code_json, "accessory_type", json_string(accessory_type.c_str()));

        data_array = json_object_get(root, "data");

        if (data_array == NULL) {
            LOG_E(TAG, "data key not present in json");
            break;
        }

        bool persist_accessory = true;
        if(json_is_boolean(json_object_get(root, "persistent"))) {
            persist_accessory = json_boolean_value(json_object_get(root, "persistent"));
        }
        nd::device::DatabaseType db_type = persist_accessory ? nd::device::DatabaseType::kPersistent : nd::device::DatabaseType::kCached;

        size_t data_array_size = json_array_size(data_array);
        for (size_t i = 0; i < data_array_size; i++) {
            string accessory_id;
            int err = 1;
            do {
                // getting data from json array
                data = json_array_get(data_array, i);
                if (data == NULL) {
                    LOG_E(TAG, "config value not present in json");
                    break;
                }

                // getting accessory id from data
                if(json_is_string(json_object_get(data, "accessory_id")) == false) {
                    LOG_E(TAG, "accessory_id not present in json");
                    break;
                }
                accessory_id = json_string_value(json_object_get(data, "accessory_id"));
                if(accessory_id.empty()) {
                    LOG_E(TAG, "accessory_id is empty");
                    break;
                }

                // handles if any prerequisites are required for accessory
                if(pair_accessory) {
                    bool status = handle_pair_prerequisites(data, accessory_type);
                    if(status == false) {
                        LOG_E(TAG, "prerequisites failed for accessory type: %s", accessory_type.c_str());
                        err = 3;
                        break;
                    }
                } else {
                    bool status = handle_unpair_prerequisites(data, accessory_type);
                    if(status == false) {
                        LOG_E(TAG, "prerequisites failed for accessory type: %s", accessory_type.c_str());
                        err = 3;
                        break;
                    }
                }

                // converting json to string
                char* data_char = json_dumps(data, JSON_COMPACT);
                if (data_char == NULL) {
                    LOG_E(TAG, "json dump failed for data");
                    break;
                }

                string data_string = data_char;

                // free json dump
                free(data_char);

                int retry_count = 0;
                bool db_update_status = false;
                while (retry_count < MAX_RETRY_COUNT) {
                    db_update_status = nd::device::update_accessories_db(accessory_id, accessory_type, data_string, pair_accessory, db_type);
                    if (db_update_status) {
                        LOG_I(TAG, "accessory data updated to db for accessory_id: %s",
                                accessory_id.c_str());
                        break;
                    } else {
                        LOG_W(TAG, "accessory data not updated to db for accessory_id: %s, retrying...",
                                accessory_id.c_str());
                        retry_count++;
                        sleep(UNIT_TIME);
                    }
                }
                if (!db_update_status) {
                    LOG_E(TAG, "accessory data not updated to db for accessory_id: %s after max retries",
                            accessory_id.c_str());
                    err = 2;
                    break;
                }
                err = 0;
            } while (false);

            // updating error code if error occured
            if(err != 0) {
                // updating error code
                json_t *error_code_jobj = json_object();
                json_object_set_new(error_code_jobj, "accessory_id", json_string(accessory_id.c_str()));
                json_object_set_new(error_code_jobj, "err", json_integer(err));
                json_array_append(err_code_array, error_code_jobj);
            } else {
                // updating success array
                json_array_append(success_array, json_string(accessory_id.c_str()));
            }
        }

        //  dumping error code json
        json_object_set(error_code_json, "errors", err_code_array);
        if(pair_accessory) {
            json_object_set(error_code_json, "paired_ids", success_array);
        } else {
            json_object_set(error_code_json, "unpaired_ids", success_array);
        }
        error_code_str = json_dumps(error_code_json, JSON_COMPACT);
        if (error_code_str.empty()) {
            LOG_E(TAG, "json dump failed for error code");
            error_code_str = "{}";
        }
        LOG_I(TAG, "error code json: %s", error_code_str.c_str());
        // checking if error occured
        if(json_array_size(err_code_array) > 0) {
            break;
        }

        if(accessory_type == "VBUS") {
            string obd_service_restart_cmd = "systemctl restart obd.service";
            if(system_execute("restart obd service from installer app", obd_service_restart_cmd) != 0) {
                LOG_E(TAG, "obd service restart failed");
            }
        }

        if (pair_accessory && accessory_type == "DHUBX") {
            string dhubx_service_restart_cmd = "sudo systemctl restart ext_cam.service";
            if(system_execute("restart ext_cam service from installer app", dhubx_service_restart_cmd) != 0) {
                LOG_E(TAG, "ext_cam service restart failed");
            }
        }

        ret = true;

    } while (false);
    if(root != NULL) {
        json_decref(root);
    }
    if(error_code_json != NULL) {
        json_decref(error_code_json);
    }
    return ret;
}

void response_unknown_command(int sequence) {
    response_for_unknown_cmd resp;
    resp.sequence_no = sequence;

    if( false == send_msg( (generic_msg_t *)&resp, (msg_type_t)RESPONSE_UNKNOWN_COMMAND_APP,
                                    sizeof(resp), queue_name, queue_name, msg_id++ ) ) {
        LOG_E (TAG,"sending msg to response thread failed\n");
        return;
    }
    LOG_I(TAG, "response for unknown command successful from main\n");

}

bool cmd_execute_with_timeout(void*args){

    bool ret =false;
    int cmd_length =0;
    string generic_cmd_result = "";

    ret = system_execute_with_resp("generic_cmd", generic_str.str(), generic_cmd_result);
    if (!ret){
       LOG_E(TAG, "failed to run generic command");
       resp.response = false;
    }
    else
       resp.response = true;

    FILE *fptr_resp;

    pthread_mutex_lock(&file_lock);
    fptr_resp = fopen(response_file.c_str(), "w");

    if (fptr_resp == NULL) {
        LOG_E(TAG, "Error!");
        return false;
    }

    fprintf(fptr_resp, "%s", generic_cmd_result.c_str());
    pthread_mutex_unlock(&file_lock);

    cmd_length = (int)strlen(generic_cmd_result.c_str());

    resp.device_cmd_len = cmd_length;

    fclose(fptr_resp);
    return true;

}

bool response_generic_cmd(int sequence, int length, bool gen_cmd_flag) {

    int ret =0, cmd_length;
    resp.sequence_no = sequence;
    char c;
    int i =0;

    if (gen_cmd_flag){

        if (length > max_generic_cmd_len){
           LOG_E(TAG, "genric cmd length more then max len");
           resp.response = false;
           return false;
        }

        pthread_mutex_lock(&file_lock);

        std::ifstream cmd_file(command_file.c_str());

        if ( cmd_file )
        {
            generic_str.str("");
            generic_str << cmd_file.rdbuf();

            cmd_file.close();
       }

       pthread_mutex_unlock(&file_lock);

       LOG_I(TAG, "command read from file : %s", generic_str.str().c_str());

       task_result_t task_result = nd_timed_task (cmd_execute_with_timeout, system_cmd_timeout, (void*)NULL, "generic_cmd_run");

       if(task_result != TASK_SUCCESS) {
           LOG_E(TAG, "generic cmd run failed timeout");
           resp.response = false;
       }
       resp.generic_cmd_enable_flag = true;

    }
    else{
        LOG_I(TAG, "did not execute generic command in installer app thread");
        resp.response = false;
        resp.generic_cmd_enable_flag = false;
    }

    if( false == send_msg( (generic_msg_t *)&resp, (msg_type_t)RESPONSE_GENERIC_COMMAND_APP,
                                    sizeof(resp), queue_name, queue_name, msg_id++ ) ) {
        LOG_E (TAG,"sending msg to response thread failed for generic cmd\n");
        return false;
    }
    LOG_I(TAG, "response for generic command successful from main\n");
    return true;

}

int msg_loop(){

    bool res=false;
    int command_sequence, camera_no, len;
    bool gen_cmd_flag =false;

    NDMBClient *eld_client = nullptr;
    NDMBClient *gps_client = nullptr;
    bool is_gps_subscribed = false;


    while(1){
         msg = msg_reader_q ->receive( );
         if (msg == NULL ) {
            LOG_E(TAG, "data receive failed\n");
            continue;

         }

        generic_msg_t *m = (generic_msg_t *)msg->get_buffer();
        reader_thread_message *g_msg = (reader_thread_message *)msg->get_buffer();

        if( NULL == m ) {
                 LOG_E(TAG, "msg->get_buffer() returned NULL");
                 continue;

        }

        switch( m->msg_type ) {

        case TEST_CONNECTION:
        {
           LOG_I(TAG, "test_conn case\n");
           app_req_t *app_req_data = (app_req_t *)m;
           appReqData.dhubx.toggle_dhub_mode = app_req_data->dhubx.toggle_dhub_mode;
           
           response_testcon(app_req_data->sequence_no, true);
           break;
        }
        case START_STREAMING:
           LOG_I(TAG, "start streaming case\n");
           command_sequence= g_msg->sequence_no;
           camera_no = g_msg->argument;
           if (currently_streaming == camera_no && camera_no < EXT_CAM_1){
              LOG_E(TAG, "app has sent streaming command for same camera no\n");
              LOG_E(TAG, "keep on playing for old camera_no\n");
           }
           else {
                res = start_streaming(camera_no, command_sequence);
           }
           break;

        case STOP_STREAMING:
            LOG_I(TAG, "stop streaming case\n");
            command_sequence= g_msg->sequence_no;
            camera_no = g_msg->argument;
            if(camera_no >= EXT_CAM_1) {
                ext_cam_record[g_msg->argument-4] = false;
            }
            res = stop_streaming(camera_no);
            if(res)
               response_stop_streaming(command_sequence, true);
            else
               response_stop_streaming(command_sequence, false);
            break;

       case DIAGNOSTIC:
        {
            LOG_I(TAG, "diagnostic case\n");
            if (!gps_client) {
                gps_client = new NDMBClient(NDMB_GPS_INST);
            }
            if(!is_gps_subscribed) {
                is_gps_subscribed = gps_client->subscribe(TOPIC_GPS_DATA, ndmb_gps_cb, gps_subscribe_count, gps_subscribe_timeout);
                if(!is_gps_subscribed) {
                    LOG_E(TAG, "failed to sub gps ndmb");
                } else {
                    LOG_I(TAG, "subscribed to gps ndmb");
                }
            }
            diagnostic_msg_t *diag = (diagnostic_msg_t *)m;
            diagnostic_device_check(diag);
            break;
        }

       case INSTALLER_EXIT:

          LOG_I(TAG, "exit case\n");
          gracefull_exit(connfd);
          break;

       case IGNITION_STATUS_APP:
          LOG_I(TAG, "ignition status case");
          command_sequence= g_msg->sequence_no;
          response_ignition_status(command_sequence);
          break;

       case MDVR_CONFIG:
        {
          LOG_I(TAG, "mdvr config case");
          mdvr_config_msg_t *mdvr_conf = (mdvr_config_msg_t *)m;
          command_sequence = mdvr_conf->sequence_no;
          char mdvr_ssid[MAX_SSID_LENGTH], mdvr_pwd[MAX_PWD_LENGTH];
          nd_strncpy(mdvr_ssid, mdvr_conf->ssid, sizeof(mdvr_ssid));
          nd_strncpy(mdvr_pwd, mdvr_conf->pwd, sizeof(mdvr_pwd));
          string mdvr_ssid_str = mdvr_ssid;
          string mdvr_pwd_str = mdvr_pwd;
          response_mdvr_config(command_sequence, mdvr_ssid_str, mdvr_pwd_str);
          break;
        }

       case MDVR_CONFIG_FILE:
        {
          LOG_I(TAG, "mdvr config file case");
          mdvr_config_file_msg_t *mdvr_conf = (mdvr_config_file_msg_t *)m;
          command_sequence = mdvr_conf->sequence_no;
          char mdvr_config[MAX_CONFIG_LENGTH];
          nd_strncpy(mdvr_config, mdvr_conf->config, sizeof(mdvr_config));
          string mdvr_config_str = mdvr_config;
          char app_ver[MAX_APP_VERSION_LENGTH];
          nd_strncpy(app_ver, mdvr_conf->app_version, sizeof(app_ver));
          string app_ver_str = app_ver;
          response_mdvr_config_file(command_sequence, mdvr_config_str, app_ver_str);
          pair_unpair_info info;
          info.pair_status = false;
          if (false == send_msg( (generic_msg_t*)&info, (msg_type_t)PAIR_UNPAIR_STATUS, sizeof(info),
                      queue_name, EXT_CAM_Q, msg_id++))
          {

              LOG_E(TAG, "sending msg to ext cam failed\n");
          }
          break;
        }

       case MDVR_FEATURE_CHECK:
          LOG_I(TAG, "mdvr feature check case");
          command_sequence= g_msg->sequence_no;
          response_mdvr_feature_check(command_sequence);
          break;

       case MDVR_WIFI_STRENGTH:
          LOG_I(TAG, "mdvr wifi strength case");
          command_sequence= g_msg->sequence_no;
          response_mdvr_wifi_strength(command_sequence);
          break;

       case MDVR_SDCARD_CHECK:
          LOG_I(TAG, "mdvr sdcard check case");
          command_sequence= g_msg->sequence_no;
          response_mdvr_sdcard_check(command_sequence);
          break;

       case OBD_PROTOCOL_APP:
          LOG_I(TAG, "obd protocol case");
          command_sequence= g_msg->sequence_no;
                if(!obd_req_first_time) {
                    // Restart obd binary service hotspot is activated
                    if(!start_obd_from_installer_app()) {
                        LOG_E(TAG, "Failed to restart obd service");
                    }
                }
          obd_req_first_time = false;
          response_obd_protocol(command_sequence);

          break;
        case OBD_EXTENDED_CHECK:
        {
            LOG_I(TAG, "obd extended check case");
            command_sequence= g_msg->sequence_no;
            if(!obd_req_first_time) {
                // Restart obd binary service hotspot is activated
                if(!start_obd_from_installer_app()) {
                    LOG_E(TAG, "Failed to restart obd service");
                }
            } else {
                LOG_I(TAG, "protocol and VIN detection inprogress avoid creating extra thread");
            }
            obd_req_first_time = false;
            response_obd_extended_check(command_sequence);
            break;
        }

        case ELD_INFO:
        {
            LOG_I(TAG, "eld check case");
            command_sequence= g_msg->sequence_no;

            try {
                if(!is_eld_subscribed) {
                    if (eld_client == nullptr) {
                        eld_client = new NDMBClient(NDMB_ELD_INST);
                    }
                    is_eld_subscribed = eld_client->subscribe(TOPIC_ELD_DATA_TO_IA, ndmb_elddata, eld_subscribe_count, eld_subscribe_timeout);

                    if(!is_eld_subscribed) {
                        LOG_E(TAG, "failed to sub eld ndmb");
                    } else {
                        LOG_I(TAG, "subscribed to eld ndmb");
                    }
                } else {
                    LOG_I(TAG, "already subscribed to eld ndmb");
                    is_eld_subscribed = true;
                }

            } catch (const std::exception& e) {
                LOG_E(TAG, "exception while creating eld client %s", e.what());
            }

            pthread_mutex_lock(&eld_info_lock);
            is_eld_populated = false;
            pthread_mutex_unlock(&eld_info_lock);

            response_eld_check(command_sequence, is_eld_subscribed);
            break;
        }

        case VEHICLE_CONFIG:
        {
            LOG_I(TAG, "vehicle config case");

            string error_code_str;
            bool response = false;

            char vehicle_config[MAX_CONFIG_LENGTH];
            vehicle_config_msg_t *vehicle_conf = (vehicle_config_msg_t *)m;
            command_sequence = vehicle_conf->sequence_no;
            nd_strncpy(vehicle_config, vehicle_conf->config_json, sizeof(vehicle_config)); 
         
            response = set_vehicle_data(vehicle_config, error_code_str);
            response_set_vehicle_config(command_sequence, error_code_str, response);

            break;
        }

       case PAIR_UNPAIR_DATA:
        {
            LOG_I(TAG, "pair unpair data case");
            pair_unpair_data_msg_t *pair_unpair_data = (pair_unpair_data_msg_t *)m;
            string error_code_str;
            bool ret = handle_pair_unpair_accessory(pair_unpair_data->data, error_code_str, pair_unpair_data->pair);
            if(!ret) {
                LOG_E(TAG, "pair unpair data failed errors %s", error_code_str.c_str());
            }
            response_pair_unpair_accessory(pair_unpair_data->sequence_no, error_code_str, ret, pair_unpair_data->pair);
            break;
        }

       case FETCH_PAIRED_ACCESSORIES:
        {    
            LOG_I(TAG, "fetch paired accessories case");
            response_fetch_all_accessory(g_msg->sequence_no);
            break;
        }

       case CHECK_VBUS_CONN:
       {
           LOG_I(TAG, "check vbus conn case");
           response_check_vbus_conn(g_msg->sequence_no);
           break;
       }

       case GENERIC_CMD_APP:
          LOG_I(TAG, "generic cmd msg recv from reader thread");
          command_sequence= g_msg->sequence_no;
          len = g_msg->device_cmd_len;
          gen_cmd_flag = g_msg->generic_cmd_enable_flag;
          response_generic_cmd(command_sequence, len, gen_cmd_flag);
          break;

       default:
          LOG_E(TAG, "error command case\n");
          response_unknown_command(command_sequence);
          break;
     }

     ss.str("");
  }

}

bool pacify_svc_from_installer(){

   static const string server_queue = "Q_SVC";
   static const string client_queue = "client_q";
   int msg_ids =0;
   generic_msg_t m;
   if( false == send_msg( (generic_msg_t *)&m, (msg_type_t)SVC_PACIFY_START, sizeof(m), client_queue, server_queue,  msg_ids++ ) ) {
      LOG_E (TAG,"***********sending msg to svc to pacify failed ************\n");
      return false;
   }
   LOG_I(TAG, "pacify svc successful\n");
   return true;
}

static bool enabled_side_camera_clocks(){

   int ret =0;
   static const string cam_audio_clk_enable_cmd = "mw_test 0x70003158 0x00 && mw_test 0x70003180 0x00";
   ret = system_execute("enabling camera clocks", cam_audio_clk_enable_cmd);

   sleep (UNIT_TIME);

   if (ret !=0){
       LOG_E(TAG, "enabling camera clock failed");
       return false;
    }

   LOG_I(TAG, "exiting enabled_side_camera_clocks function");
   LOG_I(TAG, "exiting enabled_side_camera_clocks function");
   return true;
}

bool wait_for_msg_from_wifi_mgr(){

    nd_msgq_t::nd_msg_t *msg_response_ble;

    nd_msgq_t *msg_installer_server = nd_msgq_t::get_msgq( installer_queue, nd_msgq_t::ND_MSGQ_SERVER);

    if( msg_installer_server == NULL ) {
       LOG_E(TAG,"Could not initialize message installer server queue, Exiting");
       return ERROR;
    }
    LOG_I(TAG, "polling in response thread for message from main");

    while(1){

        if( (msg_response_ble = msg_installer_server ->receive( )) == NULL ) {
            LOG_I(TAG, "data receive failed");
            continue;
        }

        LOG_I(TAG, "got the instruction to create socket");
        return true;
    }

    LOG_I(TAG, "exiting wait_for_msg_from_wifi_mgr");
    return false;
}

static void send_hotspot_disc_msg(bool bt_rescan) {
    // Send message to wifi_mgr service to disconnect hotspot
    stop_installer_hotspot_t disc_hotspot_msg;
    disc_hotspot_msg.rescan = bt_rescan;

    if( false == send_msg((generic_msg_t *)&disc_hotspot_msg, (msg_type_t)STOP_INSTALLER_HOTSPOT,
                           sizeof(disc_hotspot_msg), queue_name, "WIFI_MGR", 0 ) ) {
        LOG_E(TAG, "Sending msg to wifi mgr to disconnect wifi hotspot failed");
        return;
    }

    LOG_I(TAG, "Sent message to wifi_mgr to disconnect wifi hotspot");
}

void force_reset_installer_scan_led() {
    /* Reset installer scan LED and power LED to known state (e.g. after nd_central is stopped). */
    if (nd_device_obj) {
        nd_device_obj->nd_clear_led(PWR_LED_GREEN);
        nd_device_obj->nd_clear_led(PWR_LED_BLUE);
        nd_device_obj->nd_clear_led(PWR_LED_RED);
        nd_device_obj->nd_set_led_on(GREEN, POWER_LED, true);
        LOG_I(TAG, "Force reset installer scan LED (nd_central stopped or unreachable).");
    }
}

bool receive_stop_led_ack(void* args) {
    nd_msgq_t::nd_msg_t *msg;
    bool ack_received = false;
    while (true) {
        if( (msg = server_q->receive( )) == NULL ) {
            LOG_E(TAG, "Receive message failed" );
            break;
        }

        generic_msg_t *m = (generic_msg_t *)msg->get_buffer();
        if( m == NULL) {
            LOG_E(TAG, "Received NULL message");
            delete msg;
            continue;
        }

        if (m->msg_type == RESP_INSTALLER_LED_BLINKING) {
            LOG_I(TAG, "Received RESP_INSTALLER_LED_BLINKING from nd_central");
            ack_received = true;
            delete msg;
            break;
        } else {
            LOG_E(TAG, "Received unexpected message type %d while waiting for RESP_INSTALLER_LED_BLINKING",
                 m->msg_type);
            delete msg;
        }
    }
    return ack_received;
}

bool stop_led_blinking() {
    const int TIMEOUT_FOR_RESP_INSTALLER_LED_BLINKING = 3;  // seconds
    bool success = false;
    nd_central_led_ack_received = false;
    do {
        // Overwrite and delete installer scan indicator file if it exists
        if (file_is_present(kInstallerScanIndicatorFile) && file_delete(kInstallerScanIndicatorFile)) {
            // Send message to nd_central to stop led blinking
            led_blink_req_msg_t led_blink_msg;
            led_blink_msg.blink_status = LED_STOP_BLINKING;
            if (false == send_msg((generic_msg_t *)&led_blink_msg, (msg_type_t)INSTALLER_SCAN_INDICATE,
                                sizeof(led_blink_msg), queue_name, nd_central_mq_name, 0)) {
                LOG_E(TAG, "Sending msg to nd_central to stop led blinking failed");
                break;
            }

            // Wait for ack from nd_central to stop led blinking
            task_result_t task_result = nd_timed_task(receive_stop_led_ack, TIMEOUT_FOR_RESP_INSTALLER_LED_BLINKING,
                                                    (void*)NULL, "wait_for_resp_installer_led_blinking");
            if (task_result != TASK_SUCCESS) {
                LOG_E(TAG, "Did not receive ack from nd_central for stopping led blinking");
                break;
            }
            LOG_I(TAG, "Sent message to nd_central to stop led blinking");
        }
        success = true;
        nd_central_led_ack_received = true;
    } while (false);

    return success;
}

bool installer_app_msg_loop() {
    int listenfd = -1;

    while (1) {
        nd_msgq_t::nd_msg_t *msg;
        //Block until a new message is received
        if( (msg = server_q->receive( )) == NULL ) {
            LOG_E(TAG, "Receive message failed" );
            continue;
        }

        msg_type_t type = get_msg_type(msg->get_buffer());
        generic_msg_t *m = (generic_msg_t *)msg->get_buffer();
        if( m == NULL ) {
            LOG_E(TAG, "Received NULL message");
            continue;
        }

         LOG_I(TAG, "%d received", m->msg_type);

        switch(m->msg_type)
        {
            case CREATE_INSTALLER_SOCKET:
            {
                if(listenfd == -1) {
                    // start obd binary once hotspot is activated
                    //int ret = pthread_create(&obd_run_thread, NULL, obd_run_thread_main, NULL);
                    
		    bool ret= start_obd_from_installer_app();
		    if (ret) {
                        LOG_E(TAG, "start_obd_from_installer_app success");
                    } else {
                        LOG_I(TAG, "start_obd_from_installer_app failed");
                    }
		    LOG_I(TAG, "going to create socket first");
                    bool res = create_server_socket(listenfd, port_address);
                    if (!res){
                        LOG_E(TAG, "failed to create socket");
                        listenfd = -1;
                        send_hotspot_disc_msg(true);
                        break;
                    }
                } else {
                    LOG_I(TAG, "socket already created, proceeding to wait for connection");
                }

                unsigned int retry_client = 0;
                bool is_client_connected = false;
                while (retry_client++ < MAX_CLIENT_ACCEPT_RETRIES) {
                    bool res = accept_client_connection(listenfd, connfd, socket_timeout);
                    if (!res) {
                        LOG_E(TAG, "no connection from app for %s secs", socket_timeout.c_str());
                        // wait for next trigger
                        break;
                    }

                    // do the first read here
                    task_result_t read_result = nd_timed_task([](void *arg) {
                                                                bool status = true;
                                                                if (0 >= read(connfd, readbuff, BUFF_SIZE)) {
                                                                    LOG_E(TAG, "No data available on read:%d ", errno);
                                                                    status = false;
                                                                }
                                                                return status;
                                                              },
                                                              TIMEOUT_FOR_FIRST_MSG, (void*)NULL, "first message check");

                    if (TASK_SUCCESS != read_result) {
                        LOG_E(TAG,
                             "Client connected, first message did not arrive in %d sec or read timed task failed: %d",
                             TIMEOUT_FOR_FIRST_MSG, read_result);
                        close(connfd);
                        connfd = -1;
                    } else {
                        is_client_connected = true;
                        LOG_I(TAG, "First message received, handing over the data to child");
                        break;
                    }
                }

                if (!stop_led_blinking()) {
                    LOG_E(TAG, "Failed to stop led blinking by ndcentral, will force reset installer scan LED");
                }

                if (!is_client_connected) {
                    // wait for next trigger
                    LOG_I(TAG, "Waiting for next trigger");
                    send_hotspot_disc_msg(false);

                    // close server socket
                    close(listenfd);
                    listenfd = -1;
                    break;
                }

                return true;
            }
            break;
            default:
            {
                LOG_E(TAG, "unknown message type");
            }
        }
    }
    return false;
}

/* When connected to installer app, make sure to stop ota update
 * To stop ota update, over writing the otacheck_state file with
 * installer app PID and RUN_STATE to make sure OTA check is bypassed
 */

void update_otacheck_state_file() {

    ofstream otacheck_state_file;
    otacheck_state_file.open (otacheck_state_path.c_str(), std::ofstream::out | std::ofstream::trunc);
    stringstream ss;

    otacheck_state_file << getpid();
    ss << getpid();
    otacheck_state_file << " RUN_STATE";
    ss << " RUN_STATE";
    otacheck_state_file.close();

    LOG_I(TAG, "Updated the otacheck_state file");

    LOG_I(TAG, "Contents are: %s", ss.str().c_str());
}

int main (int argc, char **argv)
{
   pthread_t ext_cam_th1, ext_cam_th2, ext_cam_th3, ext_cam_th4;
   pid_t pid;
   bool result =false;

  if (nd_log_init(log_dir.c_str()) == false) {
    printf("unable to init logger :: but continue");
  }
#ifdef ROUTE_LOGS
  route_logs(log_dir.c_str());

#endif

  nd_service_obj = NDService::get_service_obj(TAG);
  nd_device_obj_init();
   // read installer app related config
   bool msgq_init_status = init_msgq();
   if( false == msgq_init_status ) {
       string str_msg = "MSG queue init failed";
       LOG_E(TAG, str_msg.c_str());
       return ERROR;
   }

   // read installer app related config
   read_configurations();

   populate_device_id_global();
   populate_app_version_global();

   LOG_I(TAG, "installer app pid = %d", getpid());

   // start obd binary once hotspot is activated
   result = installer_app_msg_loop();
   if (!result) {
      return ERROR;
   }

   //Keep streaming files for installer app streaming if ext cam feature is enabled
   if(ext_cam_feature_enabled) {

       // set mdvr time zone first so that requests will be in sync with its time
       read_ext_cam_time_zone_hours(time_zone);
       set_time_zone(time_zone);
       mdvr_time_set = set_mdvr_time();

#ifdef PULL_MVVR_FILES_FROM_IA
       int ch_num = 1;
       // Thread for external camera msg handler
       if ( (pthread_create (&ext_cam_th1, NULL, receive_ext_cam_file, (void *)&ch_num)) != 0 ) {
           LOG_E(TAG, "Can't create extrenal camera ch1 file receive thread");
       }

       usleep(HUNDRED_MS);

       ch_num = 2;
       // Thread for external camera msg handler
       if ( (pthread_create (&ext_cam_th2, NULL, receive_ext_cam_file, (void *)&ch_num)) != 0 ) {
           LOG_E(TAG, "Can't create extrenal camera ch2 file receive thread");
       }

       usleep(HUNDRED_MS);

       ch_num = 3;
       // Thread for external camera msg handler
       if ( (pthread_create (&ext_cam_th3, NULL, receive_ext_cam_file, (void *)&ch_num)) != 0 ) {
           LOG_E(TAG, "Can't create extrenal camera ch3 file receive thread");
       }

       usleep(HUNDRED_MS);

       ch_num = 4;
       // Thread for external camera msg handler
       if ( (pthread_create (&ext_cam_th4, NULL, receive_ext_cam_file, (void *)&ch_num)) != 0 ) {
           LOG_E(TAG, "Can't create extrenal camera ch4 file receive thread");
       }

       usleep(HUNDRED_MS);
#endif
   }

   LOG_I(TAG, "value of socket des from create socket fun %d\n", connfd);

   // pacify svc not restart device

   result = pacify_svc_from_installer();
   if (!result){
      LOG_E(TAG, "not able to pacify svc but continue\n");
   }


   // perform service related activities

   bool res = service_related_activities();

   if (!res){
      LOG_E(TAG, "something went wrong in stopping service/n");
      // Need to avoid extra device reboot so lets continue debugging
   }

    if (!nd_central_led_ack_received) {
       LOG_W(TAG, "nd_central LED ack not received, force resetting installer scan LED");
       force_reset_installer_scan_led();
    }

   // now init the gst
   gst_init(NULL, NULL);

   update_otacheck_state_file();

   // creating the queue server here because there are possibility that
   // if we create the server after forking server may not be available
   // but client can start sending the message so server should create
   // before client start sending the message

   res = queue_server_creation();

   if (!res){
      LOG_E(TAG, "something went wrong in queue server creation/n");
      return ERROR;
   }

   // enable the cam clock because there may be the chances that
   // side and inward camera are disabled through cloud. If that is
   // the case nd-central disable the camera and audio clocks

   res = enabled_side_camera_clocks();

   if (!res){
      LOG_E(TAG, "not able to enable clock, but continue");
   }

   pid = fork();

   if (pid <0){

        LOG_E(TAG, "child process creation failed/n");
        return ERROR;
   }

   // this is for parent process, in parent process we need
   // to do all processing

   else if ( pid >0) {
        // File touch for installer connection indicator
        file_touch(kInstallerConnIndicatorFile);
        // start thread for secondary_msgq
        std::thread secondary_msgq_thread(secondary_msgq);
        msg_loop();
        secondary_msgq_thread.join();
    }

   else {

        // from child process thread should be created
        // child processe are for communicating with app

        res = thread_creation_child_process();
        if (!res){
            LOG_E(TAG, "thread creation failed so exit\n");
             return ERROR;
        }
    }

     while (1){
          sleep (UNIT_TIME);
     }

}
