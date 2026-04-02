#include <log.h>
#include <sdcard.h>
#include <svc.h>
#include <unistd.h>
#include <string.h>
#include "sdcard_utils.h"
#include <system_utils.h>
#include "sdcard_recovery.h"
#include "nd_msg_types.h"
#include "service_utils.h"
#include <sstream>
#include <fstream>
#include <nd_file_utils.h>
#include <nd_msg_utils.h>
#include "nd_factory.h"
#include "nd_mmc_cmds.h"
#include "nd_device_storage_utils.h"

#define TAG "SDCARD"
#define MOUNT_SDCARD_WAIT_TIME 60
#define UNMOUNT_SDCARD_WAIT_TIME 60
#define MLC_REPLACEMENT_PERCENTAGE 95


#define DIAG_Q_NAME "DIAGNOSTIC"
#define CB_Q_NAME "q_circular_buffer"

bool sdcard_readonly_fs();
static const int UNIT_TIME = 1;
extern bool induce_ro_mode ;
static const string log_file_path = "/home/ubuntu/.nddevice/log/diagnostic/";
static const int oem_tool_run_frq = 240; // run oem tool every 240 calls of diagnosis
static const int check_avg_copy_delete_time_frq = 60; // get average copy and delete time after every 60 call of diagnosis
static const int slow_sdcard_avg_copy_delete_time_threshold = 5000; //It is more than the double of normal sdcard average copy time + avgerage delete time.
#ifdef BAGHEERA
static const string SDCARD_MOUNT_PATH = "/media/SdCard/";
static const string sdcard_devnode = "/dev/mmcblk1p1" ;
#endif
string delete_time_csv_file = "cb_delete_time.csv";
string copy_time_csv_file = "cb_copy_time.csv";
string dev_shm_path = "/dev/shm/";
string diagnostic_log_path = " /home/ubuntu/.nddevice/log/diagnostic/";

bool svc_util_send_update_watchdogtimeout(unsigned int timeout) ;
extern NDService *nd_service_obj;
extern ND_DeviceFactory *nd_device_obj;
bool SdCard::umount_sdcard(void*)
{ 
    LOG_I(TAG, "umount_sdcard() Entered");  
    int result = system_execute("SdCard", "umount /dev/mmcblk1p1 -f -a"); 
    LOG_I(TAG, "umount_sdcard() Returning");
    return (0 == result)? true : false ; 
}
bool SdCard::mount_sdcard(void*)
{ 
    LOG_I(TAG, "mount_sdcard() Entered");  
    int result = system_execute("SdCard", "mount -t ext4 -o noatime,rw /dev/mmcblk1p1 /media/SdCard"); 
    LOG_I(TAG, "mount_sdcard() Returning");
    return (0 == result)? true : false ; 
}

#if 0
int get_file_size(string filename)
{
    struct stat stat_buf;
    int rc = stat(filename.c_str(), &stat_buf);
    return rc == 0 ? stat_buf.st_size : -1;
}
#endif

string SdCard::check_last_recovery_method() {  
    LOG_D(TAG, "last_recovery_method: %s, at time: %lld",last_recovery_method.c_str(), last_recovery_method_epochTime );
    
    return last_recovery_method ; 
}

int SdCard::get_average_copy_time() {  
    return avgCopyTime ;
}

int SdCard::get_average_delete_time() {  
    return avgDeleteTime ;
}

bool SdCard::get_sdcard_hs_from_oem_tool_output(storage_type st_type){
    string file_name, oem_tool_output_str, ctime, get_time = "date +%s";
    bool res = false;
    if(st_type == SDCARD) {
        file_name = log_file_path + "health_sdcard.txt";

    } 
    else if(st_type == EMMC) {
        file_name = log_file_path + ctime.erase(ctime.length()-1) + "_health_emmc.txt";    

    }

    ifstream fin;
    fin.open(file_name);
    if(fin.is_open()) {
        int health_status_in_percentage = 0  ;
        while(getline(fin, oem_tool_output_str)){
            if(oem_tool_output_str.find("Health Status in % Used") != string::npos){  
                LOG_I(TAG, "oem_tool_succeed => %s",  oem_tool_output_str.c_str() );

                oem_tool_output_str = oem_tool_output_str.substr (oem_tool_output_str.find(":") );
                oem_tool_info = oem_tool_output_str.substr (1,  oem_tool_output_str.find("%") - 1);
                string_to_integer(oem_tool_info, health_status_in_percentage);
                res = true;
            }
            if(oem_tool_output_str.find("Used for SLC Area") != string::npos){
                LOG_I(TAG, "%s",  oem_tool_output_str.c_str() );

                oem_tool_output_str = oem_tool_output_str.substr (oem_tool_output_str.find(":") );
                string slc_percentage_str = oem_tool_output_str.substr (1,  oem_tool_output_str.find("%") - 1);
                string_to_integer(slc_percentage_str, slc_percentage);
                LOG_I(TAG, "slc_percentage: %d",  slc_percentage );

            }
            if(oem_tool_output_str.find("Used for MLC Area") != string::npos){
                LOG_I(TAG, "%s",  oem_tool_output_str.c_str() );

                oem_tool_output_str = oem_tool_output_str.substr (oem_tool_output_str.find(":") );
                string mlc_percentage_str = oem_tool_output_str.substr (1,  oem_tool_output_str.find("%") - 1);
                string_to_integer(mlc_percentage_str, mlc_percentage);
                LOG_I(TAG, "mlc_percentage: %d",  mlc_percentage );

            }
            if(oem_tool_output_str.find("Avg Erase Count MLC") != string::npos) {
                LOG_I(TAG, "%s", oem_tool_output_str.c_str());
            }
            if(oem_tool_output_str.find("Cumulative Write Data Size In 100MB") != string::npos) {
                LOG_I(TAG, "%s", oem_tool_output_str.c_str());
            }

        }
        fin.close() ;
        if(slc_percentage > mlc_percentage && slc_percentage > 5){
           slc_percentage = (slc_percentage + 10 )/3 ;
           health_status_in_percentage = max( slc_percentage , mlc_percentage ) ; 
           oem_tool_info = to_string( health_status_in_percentage );  // As per sandisk person, Correction is required to handle a bug in the tool. 
                LOG_I(TAG, "Corrected oem_tool_info: %s   slc_percentage: %d, mlc_percentage: %d  ",  oem_tool_info.c_str(), slc_percentage, mlc_percentage );
        }
        if(mlc_percentage > MLC_REPLACEMENT_PERCENTAGE ){ 
            send_sdcard_hs_to_critical_info(SM_E_DIAG_REPLACE_SD_CARD_HEALTH, "SD Card Health info ");
            
        } 
    }
    else
    {
        LOG_E(TAG,"unable to open file: %s ", file_name.c_str() );
    }
    return res;
}
void SdCard::send_sdcard_hs_to_critical_info(enum err_code_t err_code, string msg){
    LOG_E(TAG,"send_sdcard_hs_to_critical_info() " );
    if(manufacturer_sdcard == micron) {
        int health_status_in_percentage = std::max(ppeu.TLC_percent_utilization, ppeu.SLC_percent_utilization);
        string err_msg = msg + " micron  SLC:" + to_string(ppeu.SLC_percent_utilization) + "% TLC: " + to_string(ppeu.TLC_percent_utilization) + "%";
        nd_service_obj->send_err_msg(err_code, health_status_in_percentage, err_msg);
    } else {
        int health_status_in_percentage = max( slc_percentage , mlc_percentage ) ; 
        string err_msg = msg + " Sandisk SLC:" + to_string(slc_percentage) + "% MLC: " + to_string(mlc_percentage) + "%";
        nd_service_obj->send_err_msg(err_code, health_status_in_percentage, err_msg);
    }
}
void SdCard::recover(){
    static int sameState = 0;
    static enum component_state_t prev_state = COMPONENT_NORMAL_STATE; 
#ifdef BAGHEERA
    sdcard_status_t substate = sdcard_get_mount_status(SDCARD_MOUNT_PATH);
    last_recovery_method = "mount sdcard";
    last_recovery_method_epochTime = get_system_time();
    LOG_I(TAG,"****  recover() SdCard **** state: %d  substate: %d ", state, substate );
    if(COMPONENT_REDUCED_ACCESSIBILITY == state || COMPONENT_REDUCED_PERFORMANCE == state ){
        if(state == prev_state){
            sameState++; 
            LOG_E(TAG,"Components State remain same after %d trial, recovery_method: %d", sameState, recovery_method );
            if(sameState > 3){
                if(NATIVE_RECOVERY == recovery_method){
                    recovery_method = FSCK;
                    last_recovery_method = "FSCK";
                    last_recovery_method_epochTime = get_system_time();
                    recovered = 0 ;

                    struct stat st;
                    if (stat (sdcard_devnode.c_str(), &st))
                    {
                        recovery_method = CARD_REMOVE_INSERT;
                        last_recovery_method = "CARD_REMOVE_INSERT";
                        last_recovery_method_epochTime = get_system_time();
                        LOG_E (TAG, "recover() No device node present %s, Not running fsck, recovery_method: %d", sdcard_devnode.c_str(), recovery_method);
                    }
                    else {                // TBD                    
                        string file_cmd =  "file -s /dev/mmcblk1p1" ;
                        string fsck_cmd =  "fsck -t ext4 -ly /dev/mmcblk1p1" ;
                        FILE* fp = popen(file_cmd.c_str(), "r");
                        if (fp == NULL) {
                            LOG_E(TAG, "Failed to run command: %s", file_cmd.c_str() );
                            return ;
                        }
                        char file_cmd_ret [100] ;
                        fgets(file_cmd_ret, sizeof(file_cmd_ret), fp);
                        LOG_E(TAG,"recover() file_cmd_ret:  %s ", file_cmd_ret );
                        char search_string[] = "ext4 filesystem data" ;
                        if(strstr(file_cmd_ret, search_string ) == NULL){
                            fsck_cmd =  "e2fsck -y -b 32768 /dev/mmcblk1p1" ;
                            recovery_method = E2FSCK;
                            last_recovery_method = "E2FSCK";
                            last_recovery_method_epochTime = get_system_time();
                        }

                        fp = popen(fsck_cmd.c_str(), "r");
                        if (fp == NULL) {
                            LOG_E(TAG, "Failed to run command: %s", fsck_cmd.c_str() );
                            return ;
                        }

                    }
                    return;
                }
                if(recovery_method < NO_RECOVERY){
                    static int removeInsertCount = 0 ; 
                    if(removeInsertCount > 2){
                        send_sdcard_hs_to_critical_info(SM_E_DIAG_REPLACE_SD_CARD_RECOVERY, "SD Card couldn't be recovered. ");
                        LOG_E(TAG,"card remove and insert also couldn't recover after %d trial, no more recovery will be applied", removeInsertCount );
                        recovery_method = NO_RECOVERY ;
                        last_recovery_method = "All recovery method applied";
                        last_recovery_method_epochTime = get_system_time();
                        return ;
                    }
                    sdcard_remove();
                    sleep(5);
                    sdcard_insert();
                    sleep(5);
                    removeInsertCount++;
                    LOG_E(TAG,"card remove and insert done %d times", removeInsertCount );
                    return;
                }
            }
            if(sdcard_get_mount_status(SDCARD_MOUNT_PATH) == SDCARD_REMOVED){
                LOG_I(TAG, "Not running oem_tool as Sdcard dev node entry is not present, SDCARD_REMOVED");  
                
                prev_state = state ;
                return;
            }
            if(manufacturer_sdcard == micron) {
                int health_status_in_percentage = std::max(ppeu.TLC_percent_utilization, ppeu.SLC_percent_utilization);
                if(health_status_in_percentage > 90) {
                    LOG_E(TAG,"TCL utilization: %d%% SLC utilization: %d%% are above threashold", ppeu.TLC_percent_utilization, ppeu.SLC_percent_utilization);
                    send_sdcard_hs_to_critical_info(SM_E_DIAG_REPLACE_SD_CARD_HEALTH, "SD Card Health info ");
                }
            } else {
                get_sdcard_hs_from_oem_tool_output(SDCARD );
            }
        }   
        else {
            LOG_I(TAG,"Components State changed after %d trial", sameState );
            sameState = 0 ;
        } 
    }
    prev_state = state ;
    task_result_t task_result = TASK_FAIL ;
    string check_sdcard_inserted_cmd =  "gpio_test -n 201 -g" ;
    char check_sdcard_inserted_cmd_ret[100] = {0, };
    FILE *fp;
    fp = popen(check_sdcard_inserted_cmd.c_str(), "r");
    if (fp == NULL) {
        LOG_E(TAG, "Failed to run command: %s", check_sdcard_inserted_cmd.c_str() );
        return ;
    }
    fgets(check_sdcard_inserted_cmd_ret, sizeof(check_sdcard_inserted_cmd_ret), fp);
    fgets(check_sdcard_inserted_cmd_ret, sizeof(check_sdcard_inserted_cmd_ret), fp);
    
    char *ptr_colon = strstr(check_sdcard_inserted_cmd_ret, "value:")  ; 
    LOG_E(TAG, "check_sdcard_inserted_cmd_ret: %s ", check_sdcard_inserted_cmd_ret  );
 
    if( ptr_colon == NULL || ptr_colon[ strlen("value:") ] != '0') {
        LOG_E(TAG, "sdcard is not properly inserted, It could be mechanical issue. return. %s %c", check_sdcard_inserted_cmd_ret, check_sdcard_inserted_cmd_ret[strlen(check_sdcard_inserted_cmd_ret)-1] );
        return;
    }
    LOG_E(TAG, "sdcard is properly inserted, Go for recovery. %s", check_sdcard_inserted_cmd_ret );
    int ret =0;
    string stop_service = "systemctl stop circular_buffer.service";
    ret = system_execute ("stopping circular_buffer service", stop_service);
    if (ret !=0){
       LOG_E(TAG, "circular_buffer service is not stopped after RO mode");
    }

    stop_service = "systemctl stop uploader.service";
    ret = system_execute ("stopping uploader service", stop_service);
    if (ret !=0){
       LOG_E(TAG, "uploader service is not stopped after RO mode");
    }

    sleep (10 * UNIT_TIME);
    while(task_result != TASK_SUCCESS ) {
        task_result = nd_timed_task (this->umount_sdcard, UNMOUNT_SDCARD_WAIT_TIME, NULL, "unmount SdCard");
        if (task_result == TASK_TIMEOUT ) {
            LOG_E(TAG, "unmount SdCard Timeout ... ");
            sleep(60);    
        }
    }
                
    sleep (10 * UNIT_TIME);
    task_result = TASK_FAIL ;
    
    while(task_result != TASK_SUCCESS ) {
        task_result = nd_timed_task (mount_sdcard, MOUNT_SDCARD_WAIT_TIME, NULL, "mount SdCard");
        if (task_result == TASK_TIMEOUT ) {
            LOG_E(TAG, "mount SdCard Timeout ... ");
            sleep(60);    
        }
    }
    sleep (10 * UNIT_TIME);
                
    string start_service = "systemctl start circular_buffer.service";
    ret = system_execute ("starting circular_buffer service", start_service);
    if (ret !=0){
       LOG_E(TAG, "circular_buffer service is not started after RO mode");
    }

    start_service = "systemctl start uploader.service";
    ret = system_execute ("starting uploader service", start_service);
    if (ret !=0){
       LOG_E(TAG, "uploader service is not started after RO mode");
    }
    check_last_recovery_method() ;
#endif

}

int SdCard::diagnosis(void *args){

    static bool oem_tool_succeed_emmc = false;
    static int diagnosis_call_count = 0 ;
    recovered = -1 ;
#if defined(BAGHEERA) || defined(BAGHEERA2)
    static bool oem_tool_succeed_sdcard = false;
    if(oem_tool_succeed_sdcard == false){
        LOG_I(TAG, "diagnosis entered for SDcard");
        manufacturer_sdcard = SDcard; 
        execute_health_check(SDCARD, manufacturer_sdcard) ;
        if(manufacturer_sdcard == micron) {
            oem_tool_succeed_sdcard = true;
            int health_status_in_percentage = std::max(ppeu.TLC_percent_utilization, ppeu.SLC_percent_utilization);
            if(health_status_in_percentage > 90) {
                LOG_E(TAG,"TCL utilization: %d%% SLC utilization: %d%% are above threashold", ppeu.TLC_percent_utilization, ppeu.SLC_percent_utilization);
                send_sdcard_hs_to_critical_info(SM_E_DIAG_REPLACE_SD_CARD_HEALTH, "SD Card Health info ");
            }
            LOG_I(TAG,"TCL utilization: %d%% SLC utilization: %d%% ", ppeu.TLC_percent_utilization, ppeu.SLC_percent_utilization);
        }
        else {
#ifdef BAGHEERA
            oem_tool_succeed_sdcard = get_sdcard_hs_from_oem_tool_output(SDCARD);
            LOG_I(TAG, " manufacturer: %d oem_tool_succeed_sdcard: %d, oem_tool_info: %s", manufacturer_sdcard, oem_tool_succeed_sdcard, oem_tool_info.c_str() );
#endif
        }
    } else { 
        LOG_I(TAG, "Not running oem tool for sd card, It has run successfully. oem_tool_succeed_sdcard: %d, oem_tool_info: %s", oem_tool_succeed_sdcard, oem_tool_info.c_str() );
    }
#endif
    if(oem_tool_succeed_emmc == false){
        LOG_I(TAG, "diagnosis entered for EMMC");
        manufacturer_emmc =  eMMC; 
        oem_tool_succeed_emmc = execute_health_check(EMMC, manufacturer_emmc) ;
        LOG_I(TAG, " manufacturer: %d oem_tool_succeed_emmc: %d", manufacturer_emmc, oem_tool_succeed_emmc );
    } else { 
        LOG_I(TAG, "Not running oem tool for emmc, It has run successfully. oem_tool_succeed_emmc: %d", oem_tool_succeed_emmc);
    }

    diagnosis_call_count++;
    
    if(diagnosis_call_count % oem_tool_run_frq == 0){
#if defined(BAGHEERA) || defined(BAGHEERA2) 
        oem_tool_succeed_sdcard = false ;
#endif
        oem_tool_succeed_emmc = false;
    }

#ifdef BAGHEERA
    component_state_t sdcardState = COMPONENT_NORMAL_STATE; 
    int native_state = sdcard_recovery_native();
    switch(native_state){
        case -1: sdcardState = COMPONENT_REDUCED_ACCESSIBILITY;
        break;
        case 0: sdcardState = COMPONENT_REDUCED_PERFORMANCE;
        break;
        case 1: sdcardState = COMPONENT_NORMAL_STATE;
        break;
        case 2: sdcardState = COMPONENT_REDUCED_ACCESSIBILITY;
        break;
        default: 
        LOG_E(TAG, "Unknown State ....");
        sdcardState = COMPONENT_UNKNOWN_STATE;

    }
    

    LOG_I(TAG, "after diagnosis sdcard's state is %d native_state: %d", sdcardState, native_state);
    if(diagnosis_call_count % check_avg_copy_delete_time_frq == 0){ // 1st check will happen after check_avg_copy_delete_time_frq-1 call of diagnosis.
        get_average_copy_time(dev_shm_path + copy_time_csv_file);    
        get_average_delete_time(dev_shm_path + delete_time_csv_file);    
        if(avgCopyTime + avgDeleteTime > slow_sdcard_avg_copy_delete_time_threshold){
            if(manufacturer_sdcard == sandisk) {
                int hs_sandisk;
                string_to_integer(oem_tool_info, hs_sandisk);
                LOG_E(TAG,"sdcrad looks slow. avgCopyTime: %d, avgDeleteTime: %d, avg_copy_delete_time_threshold: %d health status percentage %d%%", avgCopyTime, avgDeleteTime, slow_sdcard_avg_copy_delete_time_threshold, hs_sandisk);
                string msg = "sdcard is slow. hs:" + oem_tool_info + "%";
                LOG_E(TAG,"send_sdcard is slow to_critical_info() " );
                nd_service_obj->send_err_msg(SM_E_DIAG_SD_CARD_SLOW, avgCopyTime + avgDeleteTime, msg );
            } else if(manufacturer_sdcard == micron) {
                int health_status_in_percentage = std::max(ppeu.TLC_percent_utilization, ppeu.SLC_percent_utilization);
                LOG_E(TAG,"sdcrad looks slow. avgCopyTime: %d, avgDeleteTime: %d, avg_copy_delete_time_threshold: %d health status percentage %d%%", avgCopyTime, avgDeleteTime, slow_sdcard_avg_copy_delete_time_threshold, health_status_in_percentage);
                string msg = "sdcard is slow. hs:" + to_string(health_status_in_percentage) + "%";
                LOG_E(TAG,"send_sdcard is slow to_critical_info() " );
                nd_service_obj->send_err_msg(SM_E_DIAG_SD_CARD_SLOW, avgCopyTime + avgDeleteTime, msg);
            } else {
                LOG_E(TAG,"Unknown SDcard detected. sdcrad looks slow. avgCopyTime: %d, avgDeleteTime: %d, avg_copy_delete_time_threshold: %d", avgCopyTime, avgDeleteTime, slow_sdcard_avg_copy_delete_time_threshold);
                LOG_E(TAG,"send_sdcard is slow to_critical_info() " );
                nd_service_obj->send_err_msg(SM_E_DIAG_SD_CARD_SLOW, avgCopyTime + avgDeleteTime, "unknown sdcard.sdcard is slow" );
            }
        } else {
            LOG_I(TAG,"sdcrad speed looks ok. avgCopyTime: %d, avgDeleteTime: %d, avg_copy_delete_time_threshold: %d ", avgCopyTime, avgDeleteTime, 
                                                                                                        slow_sdcard_avg_copy_delete_time_threshold );
        }

    }
    state = sdcardState;
    if(SDCARD_MOUNTED == native_state) {
        recovered = 1 ;
    }
    return native_state ;
#endif
    return 1;

}

storage_manufacturer SdCard::detect_storage_manufacturer(storage_type type)
{

    string manfid_str("");
    int ret;
    //Updating SanDisk_sd as except D410, non of the platforms uses SDcard, we
    //have eMMC to replace SDcard.
    string SanDisk_sd = "0x000045";
    string Toshiba_sd = "0x000002";
    string Kingston_sd = "0x000070";
    string Micron_sd = "0x000009";
    string SanDisk_emmc = "0x000045";
    string Toshiba_emmc = "0x000011";
    string sdcard_manfid_file = nd_device_obj->get_sdcard_manfid_file_path();
    string emmc_manfid_file = nd_device_obj->get_emmc_manfid_file_path();
    ifstream fin;

    if(type == SDCARD) {
        LOG_D(TAG, "Opening sdcard_manfid_file");
        fin.open(sdcard_manfid_file);
    }
    if(type == EMMC) {
        LOG_D(TAG, "Opening emmc_manfid_file");
        fin.open(emmc_manfid_file);
    }

    if(fin.is_open()) {
        getline(fin, manfid_str);
        fin.close();
    }
    else
    {
        LOG_E(TAG,"unable to open file: %s ", emmc_manfid_file.c_str() );
    }
    switch(type) {
        case SDCARD:
            {
                if (!manfid_str.empty() && manfid_str[manfid_str.length()-1] == '\n') {
                    manfid_str.erase(manfid_str.length()-1);
                }
                if(manfid_str == SanDisk_sd) {
                    LOG_I(TAG, "SanDisk SDcard detected");
                    return sandisk;
                } else if(manfid_str == SanDisk_emmc) {
                    LOG_I(TAG, "SanDisk SDcard detected");
                    return sandisk;
                } else if (manfid_str == Toshiba_sd) {
                    LOG_I(TAG, "Toshiba SDcard detected");
                    return toshiba;
                } else if(manfid_str == Micron_sd) {
                    LOG_I(TAG, "Micron SDcard detected");
                    return micron;
                } else if(manfid_str == Kingston_sd) {
                    LOG_I(TAG, "Kingston SDcard detected");
                    return kingston;
                } else {
                    LOG_E(TAG, "unknown SDcard detected");
                    return not_detected;
                }
            }
            break;
        case EMMC:
            {
                if(manfid_str == SanDisk_emmc) {
                    LOG_I(TAG, "SanDisk emmc detected");
                    return sandisk;
                } else if (manfid_str == Toshiba_emmc) {
                    LOG_I(TAG, "Toshiba emmc detected");
                    return toshiba;
                }
                else {
                    LOG_E(TAG, "unknown emmc detected");
                    return not_detected;
                }
                
            }
            break;
        default:
            {
                LOG_E(TAG, "Invalid Option");
                break;
            }
    }
}
bool save_log_to_file(string file_name, string data)
{
    try {
        // Replace shell command with direct system call
        time_t current_time = time(nullptr);
        string systime = to_string(current_time);
        
        LOG_D(TAG, "Current system time (epoch): %s", systime.c_str());
        if (systime.size() < 1) {
            LOG_E(TAG, "Get system time fail");  // ORIGINAL LOG MESSAGE
            return false;
        }
        
        // Use C++ file streams instead of C file operations for better error handling
        ofstream fptr(file_name);
        if (!fptr.is_open()) {
            LOG_E(TAG, "Error!"); 
            LOG_E(TAG, "Failed to open file: %s", file_name.c_str());
            return false;
        }
        
        // Write timestamp and data
        fptr << systime << "\n" << data;
        
        // File automatically closed when fptr goes out of scope (RAII)
        LOG_D(TAG, "Successfully saved log to file: %s", file_name.c_str());
        return true;
        
    } catch (const std::exception& e) {
        LOG_E(TAG, "Error!"); 
        LOG_E(TAG, "Exception in save_log_to_file: %s", e.what());
        return false;
    }
}

bool execute_lifetime_kb_write_data(void) {
    try {
        string cmd1 = nd_device_obj->execute_lifetime_kb_write_data_sdcard_cmd();
        string cmd2 = nd_device_obj->execute_lifetime_kb_write_data_emmc_cmd();
        string cmd_resp;
        bool ret = true;
        string sdcard_path, emmc_path;
        const string cat_prefix = "cat ";
        if (cmd1.find(cat_prefix) == 0) {  // Check if starts with "cat "
        sdcard_path = cmd1.substr(cat_prefix.length());
        }

        if (cmd2.find(cat_prefix) == 0) {  // Check if starts with "cat "
            emmc_path = cmd2.substr(cat_prefix.length());
        }

#ifdef BAGHEERA2
        // Read SD card lifetime data
        ifstream sdcard_file(sdcard_path);
        
        if (sdcard_file.is_open()) { 
            getline(sdcard_file, cmd_resp);
            sdcard_file.close();
            
            if (!cmd_resp.empty() && cmd_resp.back() == '\n') {
                cmd_resp.pop_back();
            }
            
            LOG_I(TAG, "cmd : %s, response : %s", cmd1.c_str(), cmd_resp.c_str());
        } else {
            LOG_E(TAG, "Failed to read SD card file: %s", sdcard_path.c_str());
            LOG_I(TAG, "cmd : %s, response : FILE_READ_FAILED", cmd1.c_str());
            ret = false;
        }
        
        cmd_resp.clear();

        // Read eMMC lifetime data
        ifstream emmc_file(emmc_path);
        
        if (emmc_file.is_open()) {
            getline(emmc_file, cmd_resp);
            emmc_file.close();
            
            if (!cmd_resp.empty() && cmd_resp.back() == '\n') {
                cmd_resp.pop_back();
            }
            
            LOG_I(TAG, "cmd : %s, response : %s", cmd2.c_str(), cmd_resp.c_str());
        } else {
            LOG_E(TAG, "Failed to read eMMC file: %s", emmc_path.c_str());
            LOG_I(TAG, "cmd : %s, response : FILE_READ_FAILED", cmd2.c_str());
            ret = false;
        }

#else
        // For KRAIT devices - eMMC only
        ifstream emmc_file(emmc_path);
        
        if (emmc_file.is_open()) {
            getline(emmc_file, cmd_resp);
            emmc_file.close();
            
            if (!cmd_resp.empty() && cmd_resp.back() == '\n') {
                cmd_resp.pop_back();
            }
            
            LOG_I(TAG, "cmd : %s, response : %s", cmd2.c_str(), cmd_resp.c_str());
        } else {
            LOG_E(TAG, "Failed to read eMMC file: %s", emmc_path.c_str());
            LOG_I(TAG, "cmd : %s, response : FILE_READ_FAILED", cmd2.c_str());
            ret = false;
        }
#endif

        LOG_D(TAG, "execute_lifetime_kb_write_data() Completed");
        return ret;
        
    } catch (const std::exception& e) {
        LOG_E(TAG, "Exception in execute_lifetime_kb_write_data: %s", e.what());
        return false;
    }
}


bool SdCard::check_emmc_lifetime() {
    string final_cmd, CmdResp;
    string emmc_lib_path = "";
    string file_name = log_file_path + "health_emmc.txt";
    int ret;
#ifdef BAGHEERA2
    emmc_lib_path = "/home/ubuntu/.nddevice/CLE_tool/EMMC/";
    final_cmd = emmc_lib_path + "cle smart_report_7250 /dev/mmcblk0 -f " + file_name;
    string cmd_grep = "grep \"Avg Erase Count MLC:\" " + file_name ;
#else
    emmc_lib_path = "/home/ubuntu/.nddevice/CLE_tool_krait/emmc/";
    final_cmd = emmc_lib_path + "cle smart_report_EM132_emb /dev/mmcblk0 -f " + file_name;
    string cmd_grep = "grep \"Avg Erase Count POOL3:\" " + file_name ;
#endif
    ret = system_execute_with_resp(TAG, final_cmd, CmdResp);
    if (ret == false) {
        LOG_E(TAG, "Failed to execute EMMC health check command Sandisk");
    }
    ret = system_execute_with_resp(TAG, cmd_grep, CmdResp);
    auto index = CmdResp.find_last_of(' ');
    std::string last_word = CmdResp.substr(++index);
    last_word.pop_back();
    int emmc_run_percent = -1 ;
    if(string_to_integer(last_word, emmc_run_percent )) {
	    emmc_run_percent *= 100 ;
	    emmc_run_percent /= 3000 ;
    }
    LOG_I(TAG, "Internal emmc lifetime: %d %% completed.", emmc_run_percent);

	string str_msg = "EMMC health tool output. MLC cyle: " + last_word ; 

    cmd_grep = "grep \"Cumulative Write Data Size In 100MB\" " + file_name ;
    CmdResp = "";
    ret = system_execute_with_resp(TAG, cmd_grep, CmdResp);
    index = CmdResp.find_last_of(' ');
    last_word = CmdResp.substr(++index);
    last_word.pop_back();
    int cumulative_write_in_GB = -1 ;
    string_to_integer(last_word, cumulative_write_in_GB) ;
    cumulative_write_in_GB *= 100 ;
    cumulative_write_in_GB /= 1024;
	str_msg += " commulative write: " + to_string(cumulative_write_in_GB) + " GB" ; 
    LOG_I(TAG, str_msg.c_str() );
    //nd_service_obj->send_err_msg(SM_E_DIAG_EMMC_RUN_PERCENTAGE, emmc_run_percent,  str_msg);
}
bool SdCard::get_remaining_life_bad_block_spare_block(memory_info &mem_info) {
    try {
        LOG_D(TAG,"START %s:%d", __func__, __LINE__);

        
        // Use class member variables instead of detecting again
        storage_manufacturer manf_sdcard = this->SDcard;
        storage_manufacturer manf_eMMC = this->eMMC;
        string sdcard_lib_path = nd_device_obj->smart_health_report_params_for_memory(CLE_TOOL_PATH_SD);
        string emmc_lib_path = nd_device_obj->smart_health_report_params_for_memory(CLE_TOOL_PATH_EMMC);;
        
        // Initialize local variables for calculations
        int erase_cycle_threshold = 3000;   // Maximum expected erase cycles before device wear-out
        float remaining_life_eMMC = 0.0;    // Calculated remaining life for eMMC
        float remaining_life_SD = 0.0;      // Calculated remaining life for SD card
        int erase_cycle_sd = 0;             // Current erase cycle count for SD card
        int erase_cycle_eMMC = 0;           // Current erase cycle count for eMMC
        
        // Initialize memory_info structure
        mem_info.rem_life_emmc = -1.0;
        mem_info.rem_life_sd = -1.0;
        mem_info.spare_block_emmc = -1.0;
        mem_info.spare_block_sd = -1.0;
        mem_info.bad_block_emmc = -1.0;
        mem_info.bad_block_sd = -1.0;
        StorageHealthMonitor sdcard_health;
        StorageHealthMonitor emmc_health;
    
        if(manf_sdcard == sandisk) {
            LOG_I(TAG, "Executing SD card health check for sandisk");
            string final_cmd = sdcard_lib_path + "/cle smart_report_EM132_emb /dev/" + nd_device_obj->smart_health_report_params_for_memory(EXTERNAL_DEV_NODE);
            LOG_I(TAG, "final_cmd: %s", final_cmd.c_str());  
          
            try {
                
                nd_mmc_ioc_cmd cmd_args;
                memset(&cmd_args, 0, sizeof(cmd_args));
                if (eKrait_1 == nd_device_obj->getDeviceType() || eKrait_2 == nd_device_obj->getDeviceType())
               { nd_strncpy(cmd_args.dev_node, nd_device_obj->get_internal_eMMC_dev_node().c_str(), sizeof(cmd_args.dev_node));}
                else
               { nd_strncpy(cmd_args.dev_node, nd_device_obj->get_external_eMMC_dev_node().c_str(), sizeof(cmd_args.dev_node));}
                const string manfid = nd_device_obj->get_emmc_manfid(nd_device_obj->get_sdcard_manfid_file_path());
                const float emmc_size = nd_device_obj->get_emmc_size(nd_device_obj->external_emmc_size_path());
                const eMMCVendorT vendor_id = static_cast<eMMCVendorT>(nd_device_obj->get_vendor_id(manfid));
                cmd_args.emmc_size = emmc_size;
                cmd_args.type = static_cast<int>(vendor_id);

                sdcard_health = get_mmc_health_stats(&cmd_args);

                // Host write data
                if (sdcard_health.host_write_size != uint64_t(-1)) {
                    uint64_t cumulative_write_100mb = sdcard_health.host_write_size / 100;
                    LOG_I(TAG, "WD : Cumulative Write Data Size In 100MB : %d", (int)cumulative_write_100mb);
                }
                
                // Pool3 erase count data (verified offset 0x08)
                if (sdcard_health.erase_count != uint32_t(-1)) {
                    erase_cycle_sd = sdcard_health.erase_count;
                    remaining_life_SD = (erase_cycle_threshold - erase_cycle_sd) / float(erase_cycle_threshold);
                    mem_info.rem_life_sd = remaining_life_SD;
                    LOG_I(TAG, "WD : Avg Erase Count POOL3 : %d", erase_cycle_sd);
                }
                
                // Pool3 bad block data (verified offset 0x24)
                if (sdcard_health.bad_block_tlc != uint32_t(-1)) {
                    mem_info.bad_block_sd = sdcard_health.bad_block_tlc;
                    LOG_I(TAG, "WD : Bad Block Runtime POOL3 : %d", (int)mem_info.bad_block_sd);
                }
                
            } catch (const std::exception& e) {
                LOG_E(TAG, "Exception in SD card IOCTL health check: %s", e.what());
                LOG_E(TAG, "Failed to execute health check command");
            }
            
        } else if (manf_sdcard == kingston) {
            LOG_I(TAG, "Executing SD card health check for kingston");
            
            string sdcard_lib_path = nd_device_obj->smart_health_report_params_for_memory(CLE_TOOL_PATH_SD);
            string node = nd_device_obj->smart_health_report_params_for_memory(EXTERNAL_DEV_NODE);
            string final_cmd = sdcard_lib_path + "/Kingston/mmc-utils-kingston host_write_byte read /dev/" + node;
            final_cmd += ";" + sdcard_lib_path + "/Kingston/mmc-utils-kingston erase_count read /dev/" + node;
            final_cmd += ";" + sdcard_lib_path + "/Kingston/mmc-utils-kingston bad_block_count read /dev/" + node;
            LOG_I(TAG, "final_cmd: %s", final_cmd.c_str());  // ORIGINAL LOG
            try {
                nd_mmc_cmds_kingston kingston_cmd;
                nd_mmc_ioc_cmd cmd_args;
                string node = nd_device_obj->smart_health_report_params_for_memory(EXTERNAL_DEV_NODE);
                strcpy(cmd_args.dev_node, ("/dev/" + node).c_str());
                cmd_args.device_type = eMMCExternal;
                
                // Host write count
                nd_host_write_data_t host_write_data;
                cmd_args.out_buf = &host_write_data;
                uint64_t host_write = kingston_cmd.get_host_write(&cmd_args);
                if (host_write != uint64_t(-1)) {
                    LOG_I(TAG, "KSI : Host Write Count : %llu", host_write);
                } else {
                    LOG_E(TAG, "KSI : Failed to get host write count");
                }
                
                // Erase count
                nd_erase_count_t erase_data;
                cmd_args.out_buf = &erase_data;
                EraseCounts erase_counts = kingston_cmd.get_erase_count_info(&cmd_args);
                
                if(erase_counts.tlc_mlc > 0) {
                    erase_cycle_sd = erase_counts.tlc_mlc;
                    remaining_life_SD = (erase_cycle_threshold - erase_cycle_sd) / float(erase_cycle_threshold);
                    mem_info.rem_life_sd = remaining_life_SD;
                    LOG_I(TAG, "KSI : Avg Erase Count MLC : %d", erase_cycle_sd);
                } else {
                    LOG_E(TAG, "KSI : Failed to get valid erase count data");
                    mem_info.rem_life_sd = -1.0;
                }
                
                // Bad block count and spare blocks - Kingston supports both
                BadBlockCounts bad_blocks = kingston_cmd.get_bad_block_count_info(&cmd_args);
                
                if (bad_blocks.later != -1) {
                    mem_info.bad_block_sd = bad_blocks.later;
                    LOG_I(TAG, "KSI : Total No. of Later Bad Blocks : %d", (int)mem_info.bad_block_sd);
                } else {
                    LOG_E(TAG, "KSI : Failed to get later bad block count");
                    mem_info.bad_block_sd = -1.0;
                }
                
                // SPARE BLOCKS - Available only for Kingston
                if (bad_blocks.spare_blocks != -1) {
                    mem_info.spare_block_sd = bad_blocks.spare_blocks;
                    LOG_I(TAG, "KSI : Total No. of Spare Blocks : %d", (int)mem_info.spare_block_sd);
                } else {
                    LOG_E(TAG, "KSI : Failed to get spare block count");
                    mem_info.spare_block_sd = -1.0;
                }
                
            } catch (const std::exception& e) {
                LOG_E(TAG, "Exception in Kingston IOCTL health check: %s", e.what());
                LOG_E(TAG, "Failed to execute Kingston IOCTL health check command");
            }
        }
    
        // micron and toshiba SD cards continue using original shell command approach

        // eMMC PROCESSING - Only manufacturers from original code (sandisk, toshiba)
       
        
        if(emmc_lib_path != "") {
            if (manf_eMMC == sandisk) {
                LOG_I(TAG, "Executing EMMC health check for sandisk");
                
                string final_cmd = emmc_lib_path + "cle smart_report_7250 /dev/" + nd_device_obj->smart_health_report_params_for_memory(INTERNAL_DEV_NODE);
                LOG_I(TAG, "final_cmd: %s", final_cmd.c_str());
                try {
                    
                    nd_mmc_ioc_cmd cmd_args;
                    memset(&cmd_args, 0, sizeof(cmd_args));
                    nd_strncpy(cmd_args.dev_node, nd_device_obj->get_internal_eMMC_dev_node().c_str(), sizeof(cmd_args.dev_node));
                    const string manfid = nd_device_obj->get_emmc_manfid(nd_device_obj->get_emmc_manfid_file_path());
                    const float emmc_size = nd_device_obj->get_emmc_size(nd_device_obj->internal_emmc_size_path());
                    const eMMCVendorT vendor_id = static_cast<eMMCVendorT>(nd_device_obj->get_vendor_id(manfid));
                    cmd_args.emmc_size = emmc_size;
                    cmd_args.type = static_cast<int>(vendor_id);

                    emmc_health = get_mmc_health_stats(&cmd_args);     
                    if (emmc_health.host_write_size != uint64_t(-1)) {
                        uint64_t cumulative_write_100mb = emmc_health.host_write_size / 100;
                        LOG_I(TAG, "WD : Cumulative Write Data Size In 100MB : %d", (int)cumulative_write_100mb);
                    }
                    
                    //  Pool3 erase count data for eMMC
                    if (emmc_health.erase_count != uint32_t(-1)) {
                        erase_cycle_eMMC = emmc_health.erase_count;
                        remaining_life_eMMC = (erase_cycle_threshold - erase_cycle_eMMC) / float(erase_cycle_threshold);
                        mem_info.rem_life_emmc = remaining_life_eMMC;
                        LOG_I(TAG, "WD : Avg Erase Count MLC : %d", erase_cycle_eMMC);
                    }
                    
                    //  Pool3 bad block data for eMMC
                    if (emmc_health.bad_block_tlc != uint32_t(-1)) {
                        mem_info.bad_block_emmc = emmc_health.bad_block_tlc;
                        LOG_I(TAG, "WD : Bad Block Runtime MLC : %d", (int)mem_info.bad_block_emmc);
                    }
                    
                } catch (const std::exception& e) {
                    LOG_E(TAG, "Exception in eMMC IOCTL health check: %s", e.what());
                    LOG_E(TAG, "Failed to execute eMMC health check command");
                }
                
            } else if (manf_eMMC == toshiba) {
                LOG_I(TAG, "Executing EMMC health check for toshiba using standard EXT_CSD");

                // Keep original Toshiba implementation exactly as in original code
                LOG_D(TAG, "Get Erase Count");
                static const string emmc_test_cmd = "/home/ubuntu/.nddevice/CLE_tool/EMMC/Toshiba/mmc_utils_toshiba extcsd read /dev/mmcblk0 | grep EXT_CSD_DEVICE_LIFE_TIME_EST_TYP_A";
                string resp = "";
                bool ret = system_execute_with_resp("RUN_mmc-utils", emmc_test_cmd, resp);
                if(!ret) {
                    LOG_E(TAG, "failed to execute emmc liftime check command");
                    return false;
                }
                auto index = resp.find_last_of(' ');
                resp = resp.substr(++index);
                if (resp.back() == '\n') {
                    resp.pop_back();
                }
                int64_t emmc_run_percent = -1;
                if(hexStr_to_int64(resp, emmc_run_percent)) {
                    emmc_run_percent *= 10;
                }
                mem_info.rem_life_emmc = 100 - int(emmc_run_percent);
                LOG_I(TAG, "Internal emmc lifetime: %d completed and remaining : %d", int(emmc_run_percent), int(mem_info.rem_life_emmc));
                
            } else {
                LOG_E(TAG, "Invalid eMMC manufacturer detected: %d", manf_eMMC);
            }
        
        }
        //  FINAL DEBUG OUTPUT - same order and format as original
        LOG_D(TAG, "remaining life emmc: %f", mem_info.rem_life_emmc);
        LOG_D(TAG, "remaining life sd: %f", mem_info.rem_life_sd);
        LOG_D(TAG, "spare block emmc: %f", mem_info.spare_block_emmc);
        LOG_D(TAG, "spare block sd: %f", mem_info.spare_block_sd);
        LOG_D(TAG, "bad block emmc: %f", mem_info.bad_block_emmc);
        LOG_D(TAG, "bad block sd: %f", mem_info.bad_block_sd);
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_E(TAG, "Exception in get_remaining_life_bad_block_spare_block: %s", e.what());
        return false;
    }
}
bool SdCard::execute_health_check(storage_type st_type, storage_manufacturer manufacturer)
{
#if defined(BAGHEERA2)
    string sdcard_lib_path = "/home/ubuntu/.nddevice/CLE_tool/SDcard/";
    string emmc_lib_path = "/home/ubuntu/.nddevice/CLE_tool/EMMC/";
#elif KRAIT
    string sdcard_lib_path = "/home/ubuntu/.nddevice/CLE_tool/SDcard/";
#endif    
    string final_cmd, CmdResp;
    string log_file_path = "/home/ubuntu/.nddevice/log/diagnostic/";
    string file_name;
    bool write_to_file = false;
    int ret;

    execute_lifetime_kb_write_data();
    if(st_type == SDCARD) {
        if(manufacturer == sandisk) {
            LOG_I(TAG, "Executing SD card health check for sandisk");
#ifdef BAGHEERA2
            final_cmd = sdcard_lib_path + "cle smart_report_EM132_emb " + nd_device_obj->get_external_eMMC_dev_node() ;
            LOG_I(TAG, "final_cmd: %s", final_cmd.c_str() );
#endif
            write_to_file = true;
            ret = system_execute_with_resp(TAG, final_cmd, CmdResp);
            if (ret == false) {
                LOG_E(TAG, "Failed to execute health check command");
                write_to_file = false;
            }
        } else if(manufacturer == micron) {
            LOG_I(TAG, "Executing SD card health check for micron");
            char buf[1024];

            memset(&ppeu, 0, sizeof(struct ppeu_data));
            nd_mmc_ioc_cmd cmd_args;
            strcpy(cmd_args.dev_node,nd_device_obj->get_external_eMMC_dev_node().c_str()); 
            cmd_args.out_buf = (void *)&ppeu;
            ret =  do_PPEU(&cmd_args);
            if (ret != 0) {
                LOG_E(TAG, "Failed to execute SDcard health check command for micron");
                write_to_file = false;
            } else {
                write_to_file = true;
                memset(buf, 0, sizeof(buf));

                sprintf(buf,"Fixed Filed Header: %02Xh %02Xh %02Xh %02Xh\nPercentage Step Size: %d\nTLC Percentage Utilization: %d%%\nSLC Percentage Utilization: %d%%\n",ppeu.header[0], ppeu.header[1], ppeu.header[2], ppeu.header[3],
                        ppeu.percent_step_size,
                        ppeu.TLC_percent_utilization,
                        ppeu.SLC_percent_utilization);

                CmdResp += string(buf);
            }
        } else if(manufacturer == toshiba) {
            LOG_D(TAG, "SDcard healthcheck support for Toshiba is not available");
       } else if(manufacturer == kingston) {
            string ext_eMMC_dev_node = nd_device_obj->get_external_eMMC_dev_node();
            LOG_I(TAG, "Executing SD card health check for kingston");
            final_cmd = "cd " + sdcard_lib_path + "Kingston/;" + "./mmc-utils-kingston host_write_byte read " + ext_eMMC_dev_node + " ;./mmc-utils-kingston erase_count  \ 
                                read " + ext_eMMC_dev_node + ";./mmc-utils-kingston bad_block_count  \
                                read " + ext_eMMC_dev_node + ";./mmc-utils-kingston power_loss_count read " + ext_eMMC_dev_node ;
            LOG_I(TAG, "final_cmd: %s", final_cmd.c_str() );
            write_to_file = true;
            ret = system_execute_with_resp(TAG, final_cmd, CmdResp);
            if (ret == false) {
                LOG_E(TAG, "Failed to execute health check command");
                write_to_file = false;
            }
            LOG_I(TAG,"cmd_resp = %s", CmdResp.c_str());
        } else {
            LOG_E(TAG, "Invalid option for SDcard health check");
            return false;
        }
        file_name = log_file_path + "health_sdcard.txt";

        if(write_to_file) {
            save_log_to_file(file_name, CmdResp);
        }

    } else if(st_type == EMMC) {
        file_name = log_file_path + "health_emmc.txt";
        
        if(manufacturer == sandisk) {
            SdCard::check_emmc_lifetime();
        } else if(manufacturer == toshiba) {
            LOG_D(TAG, "Get Erase Count");
            memset(&ecc, 0, sizeof(struct ecc_data));
            memset(&wsz, 0, sizeof(struct wsz_data));
            char buf[1020];
            nd_mmc_ioc_cmd cmd_args;
            strcpy(cmd_args.dev_node, "/dev/mmcblk0");
            cmd_args.out_buf = (void *)&ecc;
            ret =  do_toshiba_ecc(&cmd_args);
            if (ret != 0) {
                LOG_E(TAG, "Failed to execute EMMC health check command Toshiba ecc");
                return false;
            }

            LOG_D(TAG, "Get Cumulative Write Count");
            cmd_args.out_buf = (void *)&wsz;
            ret = do_toshiba_wsz(&cmd_args);
            if (ret != 0) {
                LOG_E(TAG, "Failed to execute EMMC health check command Toshiba wsz");
                return false;
            }
            sprintf(buf, "Status(A0-OK, E0-NG): %02x\nAverage Erase Count MLC: %x\nAverage Erase Count SLC: %x\n", 
                    ecc.status, ecc.aec_MLC, ecc.aec_SLC);
            CmdResp += string(buf);
            memset(buf, 0, sizeof(buf));

            sprintf(buf, "Status(A0-OK, E0-NG): %02x\nCumulative Write Data Size: %x\n", wsz.status, wsz.cwsz);
            LOG_I(TAG, "STATUS : %02x\nAverage Erase Count MLC: %x\nAverage Erase Count SLC: %x", 
                    ecc.status, ecc.aec_MLC, ecc.aec_SLC);
            LOG_I(TAG, "STATUS : %02x\nCumulative Write Data Size: %x", wsz.status, wsz.cwsz);
            CmdResp += string(buf);
            memset(buf, 0, sizeof(buf));

            save_log_to_file(file_name, CmdResp);

        } else {
            LOG_E(TAG, "Invalid option EMMC health check");
            return false;
        }
    }

    return true;
}

bool send_message_to_CB(msg_type_t msg_type)
{
    generic_msg_t msg;
    return (send_msg ((generic_msg_t*)&msg, msg_type, sizeof(msg),
                 DIAG_Q_NAME, CB_Q_NAME, 0));

}

bool SdCard::get_average_copy_time(string source_path) {
    ifstream inputFile(source_path);
    std::stringstream str_stream;
    int file_size;
    string line;
    int line_count = 0 , totalTime = 0;
    static int prev_line_count = 0 ; 
    bool valid_file = false;
    getline(inputFile, line, '\n');
    while (getline(inputFile, line, '\n'))
    {
        line_count++;
        if(line_count < prev_line_count){
            continue;
        }
        string tempStr;
        int tempInt = 0;
        str_stream.str("");
        try{
            line = line.substr(line.find(", ")+2);
            stringstream ss(line);
            std::getline(ss, tempStr, ',') ;
            string_to_integer(tempStr, tempInt);
        }
        catch (...) { 
            LOG_E(TAG, "exception is cought moving the file to log" ) ; 
            stringstream command;
            FILE *fp = NULL;
            command.str("");
            string dest_full_name = diagnostic_log_path + copy_time_csv_file + ".log" ;
            command << "mv " <<source_path << dest_full_name;
            LOG_E(TAG, "move copy_time_csv to log, command %s", command.str().c_str());
            fp = popen(command.str().c_str(), "r");
            if (fp == NULL) {
                LOG_E(TAG, "Failed to run mv csv file, command :: %s" , command.str().c_str() );
            }
            else {
                pclose(fp);
            }

        }
        totalTime += tempInt ; 
    }
    if((line_count - prev_line_count) > 0){
        avgCopyTime = totalTime / (line_count - prev_line_count);
    }
    LOG_I(TAG, "avgCopyTime:  %d,  totalTime: %d, line_count: %d, prev_line_count: %d", avgCopyTime,  totalTime, line_count, prev_line_count );
    prev_line_count = line_count ;
    file_size = get_file_size(source_path);
    LOG_I(TAG, "%s file size : %d", source_path.c_str(), file_size);
    if(file_size > 2000000) {
        LOG_I(TAG, "Sending message to CB to delete cb_copy_time.csv as size is more that 2MB : %d", file_size);
        send_message_to_CB(REQ_CIRCULAR_BUFFER_REMOVE_COPY_TIME_CSV); 
    }
    return true;
}
bool SdCard::get_average_delete_time(string source_path) {
    ifstream inputFile(source_path);
    std::stringstream str_stream;
    int file_size;

    string line;
    int line_count = 0 , totalTime = 0;
    static int prev_line_count = 0 ; 
    bool valid_file = false;
    getline(inputFile, line, '\n');
    while (getline(inputFile, line, '\n'))
    {
        line_count++;
        if(line_count < prev_line_count){
            continue;
        }
        string tempStr;
        int tempInt = 0;
        str_stream.str("");
        try{
            line = line.substr(line.find(", ")+2);
            stringstream ss(line);
            std::getline(ss, tempStr, ',') ;
            string_to_integer(tempStr, tempInt);
        }
        catch (...) { 
            LOG_E(TAG, "exception is cought moving the file to log" ) ; 
            stringstream command;
            FILE *fp = NULL;
            command.str("");
            string dest_full_name = diagnostic_log_path + delete_time_csv_file + ".log" ;
            command << "mv " <<source_path << dest_full_name;
            LOG_E(TAG, "move delete_time_csv to log, command %s", command.str().c_str());
            fp = popen(command.str().c_str(), "r");
            if (fp == NULL) {
                LOG_E(TAG, "Failed to run mv csv file, command :: %s" , command.str().c_str() );
            }
            else {
                pclose(fp);
            }

        }
        totalTime += tempInt ; 

    }
    if((line_count - prev_line_count) > 0){
        avgDeleteTime = totalTime / (line_count - prev_line_count);
    }
    LOG_I(TAG, "avgDeleteTime:  %d,  totalTime: %d, line_count: %d, prev_line_count: %d", avgDeleteTime,  totalTime, line_count, prev_line_count );
    prev_line_count = line_count ;
    file_size = get_file_size(source_path);
    LOG_I(TAG, "%s file size : %d", source_path.c_str(), file_size);
    if(file_size > 2000000) {
        LOG_I(TAG, "Sending message to CB to delete cb_delete_time.csv as size is more that 2MB : %d", file_size);
        send_message_to_CB(REQ_CIRCULAR_BUFFER_REMOVE_DELETE_TIME_CSV); 
    }
    return true;
}
