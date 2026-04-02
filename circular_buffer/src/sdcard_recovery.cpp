/* Copyright (C) 2019 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sri Hari Haran Seenivasan <hari.seenivasan@netradyne.com>, November 2019
 */

#include <stdio.h>
#include <cstdlib>
#include <unistd.h>
#include <nd_msg_utils.h> 
#include <nd_file_utils.h>
#include "sdcard_recovery.h"
#include "service_utils.h"
#include "system_utils.h"
#include "nd_msg_types.h"
#include "circular_buffer.h"
#include <sdcard_utils.h>
#include "system_utils.h"
#include "svc.h"
#include <nd_task.h>

static const char *TAG="SD_REC";
extern ND_DeviceFactory *nd_device_obj;
extern NDService *nd_service_obj;
extern circular_buffer_vid *CIRC_BUFF_ctx;

static const int sdcard_operation_sleep_time = 10;
static const int sdcard_recovery_thread_sleep_time = 30;
static const int sdcard_max_outage_time = 20*60*1000; // 20 minuites of outage shall trigger reboot.

static const int sdcard_recovery_max_count = 20;


static const int thread_wait_timeout = 30;

//Global functions
bool sdcard_readonly_fs();
bool send_sdcard_ro_reboot_powermon (){
    power_monitor_sdcard_ro_reboot_t msg;
    msg.reboot_after_secs = 0;
        
    return (send_msg ((generic_msg_t*)&msg, REQ_POWERMON_SDCARD_RO_REBOOT, sizeof (msg),
                CIRC_BUFF_ctx->circular_buffer_q_name, CIRC_BUFF_ctx->power_mon_q_name, 0));

}
sdcard_recovery_ctx::sdcard_recovery_ctx( string name, string mount_path )
{
    this->name = name;
    this->mount_path = mount_path;
}

sdcard_recovery_ctx::~sdcard_recovery_ctx( )
{
}

bool sdcard_recovery_ctx::umount_sdcard(void*)
{ 
    LOG_I(TAG, "umount_sdcard() Entered");  
    int result = system_execute("SdCard", "umount " + nd_device_obj->get_external_eMMC_dev_node() + " -f -a"); 
    LOG_I(TAG, "umount_sdcard() Returning");
    return (0 == result)? true : false ; 
}
sdcard_recovery_ctx* sdcard_recovery_ctx::get_sdcard_recovery( string name, string mount_path )
{
    sdcard_recovery_ctx* sdcard_recovery = new sdcard_recovery_ctx( name, mount_path );

    if(sdcard_recovery == NULL)
        return NULL;
    
    sdcard_recovery->clear_sdcard_recovery_stage();
    return sdcard_recovery;
}

bool sdcard_recovery_ctx::check_set_attributes( )
{
    file_attr_t attr = file_get_i_attribute(mount_path);

    if( attr == FILE_ATTR_MUTABLE ) {
        LOG_D(TAG, "file_get_i_attribute(SDCARD_MOUNT_PATH) == FILE_ATTR_MUTABLE");
        return true;
    }
    if(attr == FILE_ATTR_FAILED) {
        LOG_E(TAG, "file_get_i_attribute(SDCARD_MOUNT_PATH) == FILE_ATTR_FAILED");
        LOG_E(TAG, "have to handle this case");
        return false;
    }

    if (attr == FILE_ATTR_IMMUTABLE) {
        LOG_E(TAG, "file_get_i_attribute(SDCARD_MOUNT_PATH) == FILE_ATTR_IMMUTABLE");
        LOG_E(TAG, "setting back to FILE_ATTR_MUTABLE");
        if(!file_clear_i_attribute(mount_path)) {
            LOG_E(TAG, "failed in file_clear_i_attribute(SDCARD_MOUNT_PATH)");
            return false;
        }
        return true;        
    }
}

// SdCard Recovery function is used to attempt various recovery process for any given stage.
void sdcard_recovery_ctx::sdcard_recovery() {

    switch( this->get_sdcard_recovery_stage()) {
        case SDCARD_RECOVERY_REMOVE_INSERT:
        {
            if(true == sdcard_readonly_fs() )
            {
                static bool send_RO_reboot_pow_mon = false;
                if(false == send_RO_reboot_pow_mon)
                {
                    string str_msg = "sdcard_in RO mode, Requesting power_mon for Reboot";
                    LOG_E(TAG, str_msg.c_str());
                    ::send_sdcard_ro_reboot_powermon(); 
                    send_RO_reboot_pow_mon = true;
                    nd_service_obj->send_err_msg(SM_E_CB_SD_CARD_UNMOUNT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
                }
                else
                {
                    LOG_E(TAG, "RO mode, Reboot Request is not accepted");
                }
            }
            break;
        }
        case SDCARD_RECOVERY_ERROR:
        {
            LOG_E(TAG, "sdcard_recovery failed");
        }

        case SDCARD_RECOVERY_NOOP:
        default:
            break;          

        }
        LOG_I(TAG, "exiting sdcard_recovery");

}

int sdcard_recovery_ctx::check_sdcard_status()
{
    int error_count = 0;
    sdcard_status_t card_status = sdcard_get_mount_status(nd_device_obj->get_external_eMMC_phy_mount_path());  
    LOG_D(TAG, "sdcard_get_mount_status %d", card_status);

    if( card_status ==  SDCARD_MOUNTED ) {
        LOG_I(TAG, "check_sdcard_status passed.");
        //Reseting the recovery stage
        this->clear_sdcard_recovery_stage();
        return 0;
    }
    

    //Retaining legacy code
    LOG_E(TAG, "check_sdcard_status entered into remount path");

#ifdef NO_SDCARD
    fstab_sdcard_mount();
    sleep (2);
    card_status = sdcard_get_mount_status(nd_device_obj->get_external_eMMC_phy_mount_path());  
    if (card_status ==  SDCARD_MOUNTED ){
        LOG_I(TAG, "fstab_sdcard_mount() succeeded not running fsck");
        return 0;
    }
    else {
        LOG_E(TAG, "fstab_sdcard_mount() Couldn't mount running fsck");
        string file_cmd =  "file -s " + CIRC_BUFF_ctx->buffer_mount_src ;
        string fsck_cmd =  "fsck -t ext4 -ly " + CIRC_BUFF_ctx->buffer_mount_src ;

        string resp = "";
        bool ret = system_execute_with_resp("file_cmd", file_cmd, resp);
        if(!ret) {
            LOG_E(TAG, "failed to execute file command");
        }
        LOG_I(TAG, "file_cmd resp: %s", resp.c_str());

        if(resp.find("ext4 filesystem data")   == string::npos ){
            LOG_E(TAG, "file_cmd resp:  %s", resp.c_str() );
            fsck_cmd =  "e2fsck -y -b 32768 " + CIRC_BUFF_ctx->buffer_mount_src  ;
        }
        resp.clear();  
        ret = system_execute_with_resp("RUN_FSCK_FROM_CB", fsck_cmd, resp);
        if(!ret) {
            LOG_E(TAG, "failed to execute fsck run command");
        }
        LOG_I(TAG, "fsck_cmd: %s", fsck_cmd.c_str());
        LOG_I(TAG, "fsck_cmd resp: %s", resp.c_str());


    }
#else    
    if( TASK_TIMEOUT == sdcard_unmount(nd_device_obj->get_external_eMMC_dev_node()) || TASK_TIMEOUT == sdcard_mount(nd_device_obj->get_external_eMMC_mount_path()) || TASK_TIMEOUT == sdcard_remount(nd_device_obj->get_external_eMMC_mount_path())  )
    {
        LOG_I(TAG, "SdCard: umount or mount or remount hanged, Should reboot the device... ");
        return -1 ;
    } 
#endif    
    // to make sure SDCARD_MOUNT_PATH is non root with permissions
    file_mkdir(nd_device_obj->get_external_eMMC_mount_path(), 0777, true);

    error_count++;
    card_status = sdcard_get_mount_status(nd_device_obj->get_external_eMMC_phy_mount_path());
    switch ( card_status ) {
        case SDCARD_MOUNTED:
        {
            LOG_I(TAG, "check_sdcard_status passed after error count %d", error_count);
            this->clear_sdcard_recovery_stage ();
            return 0;
        }    
        case SDCARD_REMOVED:
        {
            //Send critical info
            string str_msg = "sdcard_get_mount_status returned SDCARD_REMOVED";
            LOG_E(TAG, str_msg.c_str());
            nd_service_obj->send_err_msg(SM_E_CB_SD_CARD_REMOVED, this->get_sdcard_recovery_stage(), str_msg );
            
            // Set recovery error if the current stage not a no-op
            // recovery error sends a command to power monitor to reboot.
            this->set_sdcard_recovery_stage( SDCARD_RECOVERY_REMOVE_INSERT );
            break;
        }
        case SDCARD_UNMOUNTED:
        {
            string str_msg = "sdcard_get_mount_status returns NOT SDCARD_MOUNTED";
            LOG_I(TAG, str_msg.c_str());
            nd_service_obj->send_err_msg(SM_E_CB_SD_CARD_MOUNT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
            this->set_sdcard_recovery_stage( SDCARD_RECOVERY_REMOVE_INSERT );
            break;
        }
        case SDCARD_MOUNTED_READONLY:
        {
            string str_msg = "SdCard is in Read only mode";
            LOG_I(TAG, str_msg.c_str());
            nd_service_obj->send_err_msg(SM_E_CB_SD_CARD_READ_ONLY, true, str_msg );
            this->set_sdcard_recovery_stage( SDCARD_RECOVERY_REMOVE_INSERT );
            break;
        }

        case SDCARD_ERROR:
        default:
            LOG_E(TAG, "check_sdcard_status cannot determine card status");
            return -1;
    } 

    sdcard_recovery();
    if ( SDCARD_RECOVERY_ERROR == this->get_sdcard_recovery_stage()) {
        LOG_E( TAG, "Exiting check_sdcard_status, sdcard_recovery(): SDCARD_RECOVERY_ERROR");
    }
    return -1;
}

bool sdcard_recovery_ctx::check_sdcard_status_attribute()
{
    if(check_sdcard_status()) {
        LOG_C(TAG, "check_sdcard_status() failed");
        return false;
    }
    LOG_D(TAG, "success in check_sdcard_status");

    // This section will be executed if card is mounted and RW mode.
    // Any other card problems check_sdcard_status() is supposed to return false
    if(check_set_attributes() == false) {
        LOG_C(TAG, "check_set_attributes() == false failed");
        return false;
    }
    LOG_D(TAG, "success in check_set_attributes");

    return true;
}

sdcard_recovery_stage sdcard_recovery_ctx::get_sdcard_recovery_stage (){
    return this->current_recovery_stage;
}

void sdcard_recovery_ctx::set_sdcard_recovery_stage (sdcard_recovery_stage recovery_stage) {
    LOG_I(TAG, "set_sdcard_recovery_stage entered with %d stage and current stage %d", recovery_stage, this->current_recovery_stage);
    // If this stage was previously atempted then we mark that recovery was an error.
    if (( recovery_stage == this->current_recovery_stage ) && ( this->sdcard_recovery_retry_count++ > sdcard_recovery_max_count )){
        this->current_recovery_stage = SDCARD_RECOVERY_ERROR;
    }
    else {
        this->current_recovery_stage = recovery_stage;
    }
    LOG_I(TAG, "set_sdcard_recovery_stage set recovery stage to %d retry count %d",this->current_recovery_stage, this->sdcard_recovery_retry_count );

}

void sdcard_recovery_ctx::clear_sdcard_recovery_stage () {
    this->current_recovery_stage = SDCARD_RECOVERY_NOOP;
}


// Thread related declaration and function
std::mutex mu;
std::condition_variable cv;
//recovery_request is message from application to thread.
bool recovery_request = false;
bool recovery_result = false;
long last_sdcard_recovery_monotonic_time;

//Initialize SDCARD recovery object
sdcard_recovery_ctx *SDCARD_RECOVERY_ctx;// = sdcard_recovery_ctx::get_sdcard_recovery( "__sdcard_recovery__", SDCARD_MOUNT_PATH );

void notify_sdcard_recovery() {
    LOG_I(TAG, "Entered notify_sdcard_recovery");
    std::unique_lock<std::mutex> lk(mu);
    cv.wait(lk, []{return recovery_request;});
    recovery_result = SDCARD_RECOVERY_ctx->check_sdcard_status_attribute();
    if (recovery_result) {
        last_sdcard_recovery_monotonic_time = get_system_monotonic_time();
    }
    lk.unlock();

    LOG_D(TAG, "Exited notify_sdcard_recovery recovery_result %d", recovery_result);
}

//This is for the callers.
bool sdcard_recovery_check(){
    if(SDCARD_RECOVERY_ctx == NULL){
        LOG_E(TAG, "SDCARD_RECOVERY_ctx is null, check sdcard_recovery thread");
        return false;
    }
    LOG_I(TAG, "Entered sdcard_recovery_check");
    {
        std::lock_guard<std::mutex> lk(mu);
        recovery_request = true;
    }

    cv.notify_one();
    LOG_I(TAG, "Exited sdcard_recovery_check");
    return recovery_result;

}

void* sdcard_recovery_thread_main(void* args){

   //Initialize health monitoring
    svc_util_init(CIRC_BUFF_ctx->circular_buffer_q_name,0);
    SDCARD_RECOVERY_ctx = sdcard_recovery_ctx::get_sdcard_recovery( "__sdcard_recovery__", nd_device_obj->get_external_eMMC_mount_path() );
    if(NULL == SDCARD_RECOVERY_ctx){
        LOG_I(TAG, "SDCARD_RECOVERY_ctx is NULL");
        return NULL;
    }

    //Start with directory.
    file_mkdir(nd_device_obj->get_external_eMMC_mount_path(), 0777, true);

    //Assume when thread starts the sdcard is functional.
    last_sdcard_recovery_monotonic_time = get_system_monotonic_time();
    LOG_I(TAG,"sdcard_recovery_thread_main initialized.");
    while (1){
        LOG_D(TAG," Running sdcard_recovery_thread_main last_sdcard_recovery_monotonic_time %ld",last_sdcard_recovery_monotonic_time);
        //Wait for message
        notify_sdcard_recovery();
        long current_monotonic_time = get_system_monotonic_time();
        LOG_I(TAG,"recovery_result %d current_monotonic_time %ld last_sdcard_recovery_monotonic_time %ld",recovery_result,current_monotonic_time,last_sdcard_recovery_monotonic_time);
        if (!recovery_result){
            if ( (current_monotonic_time - last_sdcard_recovery_monotonic_time) >= sdcard_max_outage_time ){
                string str_msg = "SdCard not in RW mode, Requesting power_mon for shutdown";
                LOG_E(TAG, str_msg.c_str());
                send_sdcard_ro_reboot_powermon();  
                last_sdcard_recovery_monotonic_time  = current_monotonic_time ;
                nd_service_obj->send_err_msg(SM_E_CB_SD_CARD_UNMOUNT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
            }
        }
        

        sleep (sdcard_recovery_thread_sleep_time);
    }

    return NULL;
}
