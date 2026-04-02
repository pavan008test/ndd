/* Copyright (C) 2017 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Mukesh <mukeshkumar.singh@netradyne.com>, May 2021
 */

#include <log.h>
#include "nd_msgq.h"
#include "nd_msg_types.h"
#include "nd_msg_utils.h"

#include <string>
#include <sstream>
#include <list>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <system_utils.h>
#include <service_utils.h>
#include "sdcard_utils.h"
#include "nd_task.h"
#include "svc.h"
#include "config_parser.h"
#include "sdcard.h"
#include <mutex>
#include <condition_variable>
#include <nd_prop_utils.h>
#include <nd_factory.h>
#include "nd_file_utils.h"
#include "fsck_recovery.h"

extern NDService *nd_service_obj;
extern ND_DeviceFactory *nd_device_obj;
#define TAG "FSCK_RECOVERY"
bool get_resize_number(char *total_data) {

    string total = "df -k | grep '/media/SdCard' | awk '{print $2}'";

    uint32_t data_total = 0;
    string response = "";

    system_execute_with_resp("CHECK_TOTAL_MEMORY", total, response);
    LOG_D(TAG,"response total  = %s", response.c_str());
    if(stringstream(response) >> data_total) {
        LOG_I(TAG, "Before resize - data total : %dK",data_total);
    }
    sprintf(total_data, "%d", data_total);
    response.clear();

    return true;

}

bool update_wdog_timeout(unsigned int timeout) {

    std::stringstream str1;
    str1 << timeout;
    string cmd_1 = "rm /dev/shm/svc.timeout";
    string cmd = "echo " + str1.str() + " >> /dev/shm/svc.timeout", resp = "";
    string restart_service = "systemctl restart svc.service";

    system_execute_with_resp("REMOVE_TIMEOUT_FILE", cmd_1, resp);
    bool ret = system_execute_with_resp("UPDATE_TIMEOUT", cmd, resp);
    if(!ret) {
        LOG_E(TAG, "failed to write svc.timeout file");
        return false;
    }
    resp.clear();
    ret = system_execute_with_resp("RESTART_SVC", restart_service, resp);
    if(!ret) {
        LOG_E(TAG, "failed to write svc.timeout file");
        return false;
    }
    return true;
}

bool start_services(unsigned int timeout) {

    bool ret;
    string start_service = "systemctl start circular_buffer uploader", resp = "";
    LOG_I(TAG,"Executing %s", start_service.c_str());
    ret = system_execute_with_resp("START_SERVICE", start_service, resp);
    if(!ret) {
        LOG_E(TAG, "failed to start bagheera and uploader service");
        return false;
    }
    LOG_I(TAG, "command response : %s", resp.c_str());
    update_wdog_timeout(timeout);
    return true;
}

bool e2fsck_sdcard(void *args) {
    string cmd = "", response = "";
    cmd = "e2fsck -yf " + nd_device_obj->get_external_eMMC_loop_mount_path();
    sleep(1);
    LOG_I(TAG,"Executing %s", __func__);
    if(!system_execute_with_resp("E2FSCK_SDCARD", cmd, response)) {
        LOG_E(TAG, "Failed to execute e2fsck_sdcard");
        return false;
    }
    LOG_I(TAG,"e2fsck cmd response : %s", response.c_str());
    LOG_I(TAG,"Out %s", __func__);
    return true;
}

bool resize_sdcard(void *args) {
    string cmd = "", response = "";
    
    cmd = "resize2fs -M " + nd_device_obj->get_external_eMMC_loop_mount_path();
    
    sleep(2);
    LOG_I(TAG,"Executing %s : %s", __func__, cmd.c_str());
    if(!system_execute_with_resp("resize_SDCARD", cmd, response)) {
        LOG_E(TAG, "Failed to execute resize_sdcard");
        return false;
    }
    LOG_I(TAG,"resize cmd response : %s", response.c_str());
    LOG_I(TAG,"Out %s", __func__);
    return true;
}

bool fsck_correction(void *args) {

    string resp = "";
    string fsck_cmd = "fsck -y " + nd_device_obj->get_external_eMMC_loop_mount_path();
    LOG_I(TAG,"Executing %s", fsck_cmd.c_str());
    bool ret = system_execute_with_resp("RUN_FSCK_RECOVERY", fsck_cmd, resp);
    if(!ret) {
        LOG_E(TAG, "failed to execute fsck recovery command");
        return false;
    }
    LOG_I(TAG, "command response : %s", resp.c_str());
    return true;
}

bool confirm_resize(char *total_size) {
    string total = "df -k | grep '/media/SdCard' | awk '{print $2}'", response = "";

    uint32_t prev_data_total = atoi(total_size), data_total = 0;

    if(!system_execute_with_resp("CHECK_TOTAL_MEMORY", total, response)) {
        LOG_E(TAG,"Failed to execute check total memory command");
        return false;
    }
    LOG_D(TAG,"response total  = %s", response.c_str());
    if(stringstream(response) >> data_total) {
        LOG_I(TAG, "After resize - data total : %dK",data_total);
    }
    if(prev_data_total == data_total) {
        LOG_E(TAG, "Fail to resize after fsck");
        return false;
    } else {
        LOG_I(TAG, "nd_sdcard resize is succesfull, new size : %d", data_total);
        return true;
    }
}


void run_fsck_correction_command() {

    string stop_service = "systemctl stop circular_buffer uploader";
    string umount_sdcard = "umount -f " + nd_device_obj->get_external_eMMC_old_mount_path();
    string mount_sdcard = "mount -o loop " + nd_device_obj->get_external_eMMC_loop_mount_path() + " " + nd_device_obj->get_external_eMMC_old_mount_path();
    string check_mount = "losetup -a | grep nd_sdcard.img";
    string resp = "";
    bool ret = false;
    unsigned int timeout = 1020;
    task_result_t task_result;

    if(!update_wdog_timeout(timeout)) {
        LOG_E(TAG, "Failed to update svc timeout, not executing fsck command");
        _exit(1);
    } else {
        LOG_I(TAG, "svc timeout changed successfully, timeout : %u", timeout);
    }

    LOG_I(TAG,"Executing %s", stop_service.c_str());
    ret = system_execute_with_resp("STOP_SERVICE", stop_service, resp);
    if(!ret) {
        LOG_E(TAG, "failed to stop bagheera and uploader service, not executing fsck recovery command");
        timeout = 120;
        update_wdog_timeout(timeout);
        _exit(1);
    }
    LOG_I(TAG, "command response : %s", resp.c_str());
    resp.clear();

    LOG_I(TAG,"Executing %s", umount_sdcard.c_str());
    if(!system_execute_with_resp("FORCE_UNMOUNT_SDCARD", umount_sdcard, resp)) {
        LOG_E(TAG, "Failed to execute force_unmount_sdcard");
        timeout = 120;
        if(!start_services(timeout)) {
            LOG_E(TAG, "fail to start services");
        }
        _exit(1);
    }
    LOG_I(TAG, "command response : %s", resp.c_str());
    resp.clear();


    LOG_I(TAG,"Executing fsck recovery");
    task_result = nd_timed_task(fsck_correction, FSCK_TIMEOUT, NULL, "fsck");
    if (task_result != TASK_SUCCESS) {
        LOG_E (TAG, "nd_timed_task for fsck failed");
        timeout = 120;
        if(!start_services(timeout)) {
            LOG_E(TAG, "fail to start services");
        }
        _exit(1);
    }
    resp.clear();

    LOG_I(TAG,"Executing %s", mount_sdcard.c_str());
    if(!system_execute_with_resp("REMOUNT_SDCARD", mount_sdcard, resp)) {
        LOG_E(TAG, "Failed to execute remount_sdcard");
    } 
    LOG_I(TAG, "command response : %s", resp.c_str());
    resp.clear();

    if(!system_execute_with_resp("CHECK_MOUNT_STATUS", check_mount, resp)) {
        LOG_E(TAG,"Failed to execute mount check command : %s", check_mount.c_str());
    } else {
        if(resp.find(nd_device_obj->get_external_eMMC_loop_mount_path()) != string::npos) {
            LOG_I(TAG,"/media/Sdcard is mounted succesfully: %s", resp.c_str());
        }
    }
    resp.clear();

    timeout = 120;
    if(!start_services(timeout)) {
        LOG_E(TAG, "fail to start services");
    }
    _exit(0);
}

void run_fsck_correction_and_resize_command() {

    string fsck_cmd = "fsck -y " + nd_device_obj->get_external_eMMC_loop_mount_path();
    string stop_service = "systemctl stop circular_buffer uploader";
    string start_service = "systemctl start circular_buffer uploader";
    string umount_sdcard = "umount -f " + nd_device_obj->get_external_eMMC_old_mount_path();
    string mount_sdcard = "mount -o loop " + nd_device_obj->get_external_eMMC_loop_mount_path() + " " + nd_device_obj->get_external_eMMC_old_mount_path();
    string check_mount = "losetup -a | grep nd_sdcard.img";
    string resp = "";
    bool ret = false;
    unsigned int timeout = 1020;
    char total_size[10];
    task_result_t task_result;


    if(!update_wdog_timeout(timeout)) {
        LOG_E(TAG, "Failed to update svc timeout, not executing fsck command");
        _exit(1);
    } else {
        LOG_I(TAG, "svc timeout changed successfully, timeout : %u", timeout);
    }
    LOG_I(TAG,"Executing %s", stop_service.c_str());
    ret = system_execute_with_resp("STOP_SERVICE", stop_service, resp);
    if(!ret) {
        LOG_E(TAG, "failed to stop bagheera and uploader service, not executing fsck recovery command");
        timeout = 120;
        update_wdog_timeout(timeout);
        _exit(1);
    }
    LOG_I(TAG, "command response : %s", resp.c_str());
    resp.clear();

    LOG_I(TAG,"Executing get_resize_number");
    ret = get_resize_number(total_size);
    LOG_I(TAG,"total memory before resize : %s", total_size);
    

    LOG_I(TAG,"Executing %s", umount_sdcard.c_str());
    if(!system_execute_with_resp("FORCE_UNMOUNT_SDCARD", umount_sdcard, resp)) {
        LOG_E(TAG, "Failed to execute force_unmount_sdcard");
        timeout = 120;
        if(!start_services(timeout)) {
            LOG_E(TAG, "fail to start services");
        }
        _exit(1);
    }
    LOG_I(TAG, "command response : %s", resp.c_str());
    resp.clear();



    LOG_I(TAG,"Executing fsck recovery");
    task_result = nd_timed_task(fsck_correction, FSCK_TIMEOUT, NULL, "fsck");
    if (task_result != TASK_SUCCESS) {
        LOG_E (TAG, "nd_timed_task for fsck failed");
        timeout = 120;
        if(!start_services(timeout)) {
            LOG_E(TAG, "fail to start services");
        }
        _exit(1);
    }

    sleep(5);

    LOG_I(TAG,"Executing %s", umount_sdcard.c_str());
    if(!system_execute_with_resp("FORCE_UNMOUNT_SDCARD", umount_sdcard, resp)) {
        LOG_E(TAG, "Failed to execute force_unmount_sdcard");
        timeout = 120;
        if(!start_services(timeout)) {
            LOG_E(TAG, "fail to start services");
        }
        _exit(1);
    }
    LOG_I(TAG, "command response : %s", resp.c_str());
    resp.clear();
    sleep(1);

    task_result = nd_timed_task(e2fsck_sdcard, E2FSCK_TIMEOUT, NULL, "e2fsck");
    if (task_result != TASK_SUCCESS) {
        LOG_E (TAG, "nd_timed_task for e2fsck failed");
    }

    task_result = nd_timed_task(resize_sdcard, RESIZE_TIMEOUT, NULL, "resize_sdcard");
    if (task_result != TASK_SUCCESS) {
        LOG_E (TAG, "nd_timed_task for resize_sdcard failed");
    }


    LOG_I(TAG,"Executing %s", mount_sdcard.c_str());
    if(!system_execute_with_resp("REMOUNT_SDCARD", mount_sdcard, resp)) {
        LOG_E(TAG, "Failed to execute remount_sdcard");
    } 
    LOG_I(TAG, "command response : %s", resp.c_str());
    resp.clear();

    if(!system_execute_with_resp("CHECK_MOUNT_STATUS", check_mount, resp)) {
        LOG_E(TAG,"Failed to execute mount check command : %s", check_mount.c_str());
    } else {
        if(resp.find(nd_device_obj->get_external_eMMC_loop_mount_path()) != string::npos) {
            LOG_I(TAG,"/media/Sdcard is mounted succesfully: %s", resp.c_str());
        }
    }
    resp.clear();


    timeout = 120;
    if(!start_services(timeout)) {
        LOG_E(TAG, "fail to start services");
    }

    if(!confirm_resize(total_size)) {
        LOG_E(TAG, "nd_sdcard resize is failed");
    }

    _exit(0);
}

void run_fsck_command() {
    string cmd = "fsck -t ext4 -nf " + nd_device_obj->get_external_eMMC_dev_node();
    string resp = "";

    bool ret = system_execute_with_resp("RUN_FSCK", cmd, resp);
    if(!ret) {
        LOG_E(TAG, "failed to execute fsck run command");
        _exit(1);
    }

    LOG_I(TAG, "resp: %s", resp.c_str());

    // parse the fsck response 
    stringstream ss(resp);
    string prev_line = "", line = "";

    string error_msg = "";
    int aux_code = 1;    

    while(std::getline(ss, line, '\n')) {
        if(line == "") {
            continue;
        }

        size_t pos = line.find("Fix?");

        if(pos == string::npos) {
            prev_line = line;
            continue;
        }

        if(pos == 0) {
            error_msg = prev_line;
        } else {
            error_msg = line.substr(0, pos);
        }

        LOG_I(TAG, "FSCK_CMD_OUT : %s", error_msg.c_str());

        // increment aux code for every error message we sent so that sm do not drop back to back messages
        aux_code++; 
    }

    LOG_I(TAG, "sending fsck error: %s to sm", error_msg.c_str());
    nd_service_obj->send_err_msg(SM_E_CB_FSCK_ERRORS, aux_code, error_msg);

    if(aux_code == 1) {
        LOG_I(TAG, "No fsck errors detected");
    }
    _exit(0);
}


void fork_fsck_image_correction_run_process(bool runResize) {

    pid_t pid = fork();

    if (pid < 0) {
        LOG_E(TAG, "Failed to fork process for fsck correction command run");
        return;
    }

    if (pid == 0){
        if(!runResize) {
            LOG_I(TAG, "fork process for fsck correction command run");
            run_fsck_correction_command();
        } else {
            LOG_I(TAG, "fork process for fsck correction and resize command run");
            run_fsck_correction_and_resize_command();
        }
    } else {
        LOG_I(TAG, "fsck correction run child process launched with pid = %d", pid);
        task_status_t tc_process_status;
        if(!runResize) {
            tc_process_status = nd_set_timeout_for_task(pid, FSCK_CORRECTION_COMMAND_TASK_TIMEOUT);
        } else {
            tc_process_status = nd_set_timeout_for_task(pid, FSCK_CORRECTION_AND_RESIZE_COMMAND_TASK_TIMEOUT);
        }
        switch (tc_process_status){
            case TASK_STATUS_SUCCESS:
                LOG_I(TAG, "fsck correction run child process %d successfully completed", pid);
                break;
            case TASK_STATUS_FAILED:
                if(!runResize) {
                    LOG_E(TAG, "fsck correction run child process %d failed", pid);
                    nd_service_obj->send_err_msg(SM_E_CB_FSCK_CORRECTION_FAIL, NDService::UNUSED_ERR_AUX_CODE, "fsck correction command run failed");
                } else {
                    LOG_E(TAG, "fsck correction and resize run child process %d failed", pid);
                    nd_service_obj->send_err_msg(SM_E_CB_FSCK_CORRECTION_AND_RESIZE_FAIL, NDService::UNUSED_ERR_AUX_CODE, "fsck correction and resize run failed"); 
                }
                break;
            case TASK_STATUS_KILLED:
                if(!runResize) {
                    LOG_E(TAG, "fsck correction run child process %d killed", pid);
                    nd_service_obj->send_err_msg(SM_E_CB_FSCK_CORRECTION_FAIL, NDService::UNUSED_ERR_AUX_CODE, "fsck correction run killed");
                } else {
                    LOG_E(TAG, "fsck correction and resize run child process %d killed", pid);
                    nd_service_obj->send_err_msg(SM_E_CB_FSCK_CORRECTION_AND_RESIZE_FAIL, NDService::UNUSED_ERR_AUX_CODE, "fsck correction and resize run killed");
                }
                break;
            default:
                LOG_E(TAG, "fsck correction run child process %d unexpected return. Assuming it failed", pid);
                break;
        }
    }
}

void fork_fsck_run_process() {

    pid_t pid = fork();

    if (pid < 0) {
        LOG_E(TAG, "Failed to fork process for fsck command run");
        return;
    }

    if (pid == 0){
        run_fsck_command();
    } else {
        LOG_I(TAG, "fsck run child process launched with pid = %d", pid);
        task_status_t tc_process_status = nd_set_timeout_for_task(pid, FSCK_COMMAND_TASK_TIMEOUT);
        switch (tc_process_status){
            case TASK_STATUS_SUCCESS:
                LOG_I(TAG, "fsck run child process %d successfully completed", pid);
                break;
            case TASK_STATUS_FAILED:
                LOG_E(TAG, "fsck run child process %d failed", pid);
                break;
            case TASK_STATUS_KILLED:
                LOG_E(TAG, "fsck run child process %d killed", pid);
                break;
            default:
                LOG_E(TAG, "fsck run child process %d unexpected return. Assuming it failed", pid);
                break;
        }
    }
}
