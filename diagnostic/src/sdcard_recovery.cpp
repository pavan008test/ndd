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
//#include "circular_buffer.h"
#include "sdcard_utils.h"
#include "system_utils.h"
#include "svc.h"
#include <nd_task.h>
#include <log.h>
#include <nd_time.h>
#include <nd_factory.h>
static const char *TAG="SD_REC";

extern NDService *nd_service_obj;
extern ND_DeviceFactory *nd_device_obj;
static const int sdcard_operation_sleep_time = 10;
static const int sdcard_recovery_thread_sleep_time = 30;
static const int sdcard_max_outage_time = 20*60*1000; // 20 minuites of outage shall trigger reboot.

static const int sdcard_recovery_max_count = 20;
#ifdef BAGHEERA
static const string SDCARD_MOUNT_PATH = "/media/SdCard/";
#endif
static const int thread_wait_timeout = 30;
string          power_mon_q_name = "q_power_monitor";

//Global functions
string get_msgq_name();
bool sdcard_readonly_fs();
bool send_sdcard_ro_reboot_powermon (){
    power_monitor_sdcard_ro_reboot_t msg;
    msg.reboot_after_secs = 0;
        
    return (send_msg ((generic_msg_t*)&msg, REQ_POWERMON_SDCARD_RO_REBOOT, sizeof (msg), get_msgq_name() , power_mon_q_name, 0));   // TBD

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
    int result = system_execute("SdCard", "umount /dev/mmcblk1p1 -f -a"); 
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
    sdcard_status_t card_status = sdcard_get_mount_status(nd_device_obj->get_external_eMMC_mount_path());  
    LOG_D(TAG, "sdcard_get_mount_status %d", card_status);

    if( card_status ==  SDCARD_MOUNTED ) {
        LOG_I(TAG, "check_sdcard_status passed.");
        //Reseting the recovery stage
        this->clear_sdcard_recovery_stage();
        return 0;
    }
    

    //Retaining legacy code
    LOG_E(TAG, "check_sdcard_status entered into remount path");
    if( TASK_TIMEOUT == sdcard_unmount(nd_device_obj->get_external_eMMC_dev_node()) || TASK_TIMEOUT == sdcard_mount(nd_device_obj->get_external_eMMC_mount_path()) || TASK_TIMEOUT == sdcard_remount(nd_device_obj->get_external_eMMC_mount_path())  )
    {
        LOG_I(TAG, "SdCard: umount or mount or remount hanged, Should reboot the device... ");
        return -1 ;
    } 
    // to make sure SDCARD_MOUNT_PATH is non root with permissions
    file_mkdir(nd_device_obj->get_external_eMMC_mount_path(), 0777, true);

    error_count++;
    card_status = sdcard_get_mount_status(nd_device_obj->get_external_eMMC_mount_path());
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
            if(is_card_inserted() == true ){
                LOG_E(TAG, "Card is physically Present but there is no dev node entry");
            }
            LOG_E(TAG, str_msg.c_str());
            nd_service_obj->send_err_msg(SM_E_CB_SD_CARD_MOUNT_FAIL, this->get_sdcard_recovery_stage(), str_msg );
            
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
    card_status = sdcard_get_mount_status(nd_device_obj->get_external_eMMC_mount_path());
    this->card_status = card_status ;
    if ( SDCARD_RECOVERY_ERROR == this->get_sdcard_recovery_stage()) {
        LOG_E( TAG, "Exiting check_sdcard_status, sdcard_recovery(): SDCARD_RECOVERY_ERROR");
    }
    return card_status;
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
    }

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
    recovery_result = SDCARD_RECOVERY_ctx->check_sdcard_status_attribute();
    if (recovery_result) {
        last_sdcard_recovery_monotonic_time = get_system_monotonic_time();
    }

    LOG_I(TAG, "Exited notify_sdcard_recovery recovery_result %d", recovery_result);
}


int sdcard_recovery_ctx::get_card_status(){
    return card_status ;
}


int sdcard_recovery_native(){

    int res = -1;
   //Initialize health monitoring
    SDCARD_RECOVERY_ctx = sdcard_recovery_ctx::get_sdcard_recovery( "__sdcard_recovery__", nd_device_obj->get_external_eMMC_mount_path() );
    if(NULL == SDCARD_RECOVERY_ctx){
        LOG_I(TAG, "SDCARD_RECOVERY_ctx is NULL");
        return res;
    }

    //Start with directory.
    file_mkdir(nd_device_obj->get_external_eMMC_mount_path(), 0777, true);

    //Assume when thread starts the sdcard is functional.
    last_sdcard_recovery_monotonic_time = get_system_monotonic_time();
    LOG_I(TAG,"sdcard_recovery_native().");
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
     
    return   SDCARD_RECOVERY_ctx->get_card_status() ; //sdcard_recovery_ctx::card_status;
}
