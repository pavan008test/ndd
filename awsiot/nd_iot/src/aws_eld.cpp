#include "aws_eld.h"
#include <aws_iot_internal.h>
#include <nd_time.h>
#include <nd_pq.h>
#include <nd_prop_utils.h>
#include <config_parser.h>
#include <system_utils.h>
#include <log.h>
#include <sstream>
#include <iomanip>
#include <sys/sysinfo.h>
#include "aws_iot_msg.h"
#include <svc.h>
#include <nd_vbus_info.h>
#include <nd_factory.h>
#include "wake_up_reason.h"
#include <nd_msg_types.h>

using namespace std;

#define TAG "ELD"

static const string bagheera_config_path = "/home/ubuntu/.nddevice/latest/bagheera_config.ini";
#if defined(KRAIT) || defined(KRAIT2)
static const string db_file_path = "/data/nd_files/db";
#else
static const string db_file_path = "/home/ubuntu/.nddevice/";
#endif
string dest_db="obd_property.db";
static const int PUB_RETRY_INTERVAL_SEC = 1;
bool eld_enabled = false;
bool iosix_enabled = false;
bool vh_enabled = false;
bool ft_enabled = false;
int offline_storage = 240;
int qos_level = 1;
int OneKbInBytes = 1024;


extern NDService *nd_service_obj;
extern AwsIot *iot;
extern NDService *nd_service_obj;
extern volatile bool msg_loop_active;

bool read_eld_config(){
   bool get_override_val = true;
   bool is_val_overridden = false;
   Config_parser config_parser (bagheera_config_path);
    if (!config_parser.getParseStatus()){
        LOG_E(TAG, "Can not parse config\n");
           return false;
    }
    string temp = config_parser.getConfig("driveri_one","eld_enabled","false", get_override_val, is_val_overridden);
    if (temp == "true"){
        eld_enabled = true;
    }
    temp="";
    temp = config_parser.getConfig("driveri_one","vehicle_health","false", get_override_val, is_val_overridden);
    if (temp == "true"){
        LOG_I(TAG, "vehicle health enabled");
        vh_enabled = true;
    }
    string ft_temp = config_parser.getConfig("driveri_one","gps_tracking_enabled","false", get_override_val, is_val_overridden);
    if (ft_temp == "true"){
        ft_enabled = true;
        LOG_I(TAG, "fleet tracking enabled");
    }
    string qos_temp = config_parser.getConfig("driveri_one","mqtt_qos_level","1", get_override_val, is_val_overridden);
    if(!string_to_integer(qos_temp, qos_level)) {
        LOG_E(TAG, "failed to convert qos_level to int");
    }
    else{
        LOG_I(TAG, "MQTT QoS level configured for ELD is %d", qos_level);
    }

    if(eld_enabled == true || ft_enabled == true){
        string offline_storage_str = config_parser.getConfig("driveri_one","offline_storage","240", get_override_val, is_val_overridden);
        if(!string_to_integer(offline_storage_str, offline_storage)) {
            LOG_E(TAG, "failed to convert offline_storage_str to int");
        }
    }
    return true;
}

int calculate_pq_size(){

    int min_freq=10;

    int msgs_hr = 3600/min_freq;
    int msgs_stored = msgs_hr * offline_storage;
    return msgs_stored;
}

std::string compress_and_base64(const std::string& data) {
    // Use single quotes in shell command to avoid manual escaping of double quotes
    std::string command = "echo -n '" + data + "' | gzip | base64 -w 0";

    std::string response;
    if (!system_execute_with_resp("GZIP_BASE64", command, response)) {
        LOG_I(TAG, "Failed to get zipped and encoded string");
        return "";
    }
    return response;
}

void *eld_publish_thread(void* arg ){

    int pq_size = calculate_pq_size();
    nd_pq *pq = new nd_pq("eld", db_file_path, pq_size);
    if (!pq){
        LOG_E (TAG,"ELD DB creation failed, retrying one more time");
        pq = new nd_pq("eld", db_file_path, pq_size);
        if (!pq){
            nd_service_obj->send_err_msg (SM_E_OBD_ELD_DB_CREATE_FAIL, 0, "ELD DB creation failed");
            pthread_exit(NULL);
        }
    }
    string msg;
    static int err_count = 600;
    int ret = -1;
    int64_t count = 0;
    string count_str = "";
    uint64_t seq = 0;
    unsigned int wait_counter =0;
    string eld_payload;

    while(1){
        if(!pq->is_empty() && msg_loop_active){
            if(pq->nd_pq_peek(msg,seq)){
                do{
                    int length = msg.length();
                    LOG_I(TAG,"ELD payload size %d",length);
                    printf("ELD/VH Payload: %s\n", msg.c_str());
                    if(length > (0.8*OneKbInBytes))
                    {
                        LOG_I(TAG,"ELD payload size is more than 800 bytes, compressing the payload");
                        string zipped_and_enc_payload = compress_and_base64(msg);
                        LOG_I(TAG,"compressed and base64 encoded string %s with size %zu",zipped_and_enc_payload.c_str(),zipped_and_enc_payload.size());
                        if(zipped_and_enc_payload.empty()){
                            LOG_E(TAG,"Failed to compress and encode the payload");
                            break;
                        }
                        eld_payload = "{\"data\": \"" + zipped_and_enc_payload + "\", \"isCmp\": 1}";
                     } else {
                      // Publish original payload with added "isCmp" key
                        eld_payload = msg.substr(0, msg.size() - 1) + ", \"isCmp\": 0}";  
                     }
                    if(eld_payload.length() > 5 * OneKbInBytes){
                        LOG_E(TAG,"ELD payload size is more than 5KB, sending critical info");
                        string critical_info = "ELD payload size is more than 5KB, size: " + to_string(eld_payload.length());
                        nd_service_obj->send_err_msg(SM_E_OBD_ELD_PAYLOAD_SIZE_EXCEEDED, 0, critical_info);
                    }
                    ret = iot->publish_eld(eld_payload);
                    //ret = iot->publish_eld(msg);
                    if(ret != 0){
                        err_count++;
			if ((err_count % 10) == 0){
				wait_counter++;
			}
                        if(err_count > 600){
                            pq->nd_get_count_pq(count);
                            count_str = to_string(count);
                            string err_msg = "Failed to publish ELD payloads, available rows in DB " + count_str;
                            nd_service_obj->send_err_msg (SM_E_AWS_ELD_PUBLISH_FAILURE, ret, err_msg);
                            err_count = 0;
                        }
                    }
                    else{
                        err_count = 0;
			wait_counter =0;
                    }
		    if (wait_counter > 30){ /* should not wait more then 30 sec */
			    wait_counter =0;
		    }
		    sleep(PUB_RETRY_INTERVAL_SEC + wait_counter);
                }while(ret != 0);
                pq->nd_pq_pop(msg,seq);
            }
        }
        else{
            sleep(1);
        }
    }
}
