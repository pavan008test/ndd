/* Copyright (C) 2020 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Shravan Kumar M <shravan.kumar@netradyne.com>, October 2020
*/

#include <fstream>
#include <sys/time.h>
#include <sstream>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <nd_time.h>
#include <nd_file_utils.h>
#include <nd_msgq.h>
#include <nd_msg_utils.h>
#include <nd_msg_types.h>
#include <log.h>
#include "service_utils.h"
#include <sys/ioctl.h>
#include "config_parser.h"
#include <system_utils.h>
//#include <nd_paths.h>

using namespace std;

#define    fan_state_0  0
#define    fan_state_1  1
#define    fan_state_2  2
#define    fan_state_3  3
#define    fan_state_4  4

#define ROUTE_LOGS
#define TAG "FAN"

NDService *nd_service_obj; //nd service object, to detect crashes

static const string log_dir = "/home/ubuntu/.nddevice/log/fan";

#define BAGHEERA_CONFIG_INI "/home/ubuntu/.nddevice/latest/bagheera_config.ini"
#define CPU_TEMP "/sys/class/hwmon/hwmon1/../../die_temp"

#define FAN "/sys/class/leds/fan_control/brightness"
static string fan_enable_str = "true";

static int def_up_thr[4] = {51, 61, 71, 82};
static int def_down_thr[4] = {36, 52, 62, 72};
static int def_fan_pwm[5] = {0, 80, 120, 160, 255};

static int up_thr[4] = {51, 61, 71, 82};
static int down_thr[4] = {36, 52, 62, 72};
static int fan_pwm[5] = {0, 80, 120, 160, 255};

// Interval in seconds to check the CPU temperature
static int interval = 5;

void read_fan_config() {
    LOG_I(TAG, "Reading config file...");
    Config_parser c(BAGHEERA_CONFIG_INI);
    if (true != c.getParseStatus()) {
        LOG_E(TAG, "Config_parser failed");
        return;
    }

    bool get_override_val = true, val_overridden = false;

    fan_enable_str = c.getConfig("fan","enable", "true", get_override_val, val_overridden);
    LOG_I(TAG, "fan_enable = %s", fan_enable_str.c_str());


    if(fan_enable_str == "true") {

        string interval_str = c.getConfig("fan","interval", "5", get_override_val, val_overridden);
        if (false == string_to_integer(interval_str, interval)) {
            LOG_E(TAG, "string_to_integer api failed, line no  : %d", __LINE__);
        }

        string up_thr1_str = c.getConfig("fan","up_thr1", std::to_string(def_up_thr[0]), get_override_val, val_overridden);
        if (false == string_to_integer(up_thr1_str, up_thr[0])) {
            LOG_E(TAG, "string_to_integer api failed, line no  : %d", __LINE__);
        }

        string up_thr2_str = c.getConfig("fan","up_thr2", std::to_string(def_up_thr[1]), get_override_val, val_overridden);
        if (false == string_to_integer(up_thr2_str, up_thr[1])) {
            LOG_E(TAG, "string_to_integer api failed, line no  : %d", __LINE__);
        }

        string up_thr3_str = c.getConfig("fan","up_thr3", std::to_string(def_up_thr[2]), get_override_val, val_overridden);
        if (false == string_to_integer(up_thr3_str, up_thr[2])) {
            LOG_E(TAG, "string_to_integer api failed, line no  : %d", __LINE__);
        }

        string up_thr4_str = c.getConfig("fan","up_thr4", std::to_string(def_up_thr[3]), get_override_val, val_overridden);
        if (false == string_to_integer(up_thr4_str, up_thr[3])) {
            LOG_E(TAG, "string_to_integer api failed, line no  : %d", __LINE__);
        }

        string down_thr1_str = c.getConfig("fan","down_thr1", std::to_string(def_down_thr[0]), get_override_val, val_overridden);
        if (false == string_to_integer(down_thr1_str, down_thr[0])) {
            LOG_E(TAG, "string_to_integer api failed, line no  : %d", __LINE__);
        }

        string down_thr2_str = c.getConfig("fan","down_thr2", std::to_string(def_down_thr[1]), get_override_val, val_overridden);
        if (false == string_to_integer(down_thr2_str, down_thr[1])) {
            LOG_E(TAG, "string_to_integer api failed, line no  : %d", __LINE__);
        }

        string down_thr3_str = c.getConfig("fan","down_thr3", std::to_string(def_down_thr[2]), get_override_val, val_overridden);
        if (false == string_to_integer(down_thr3_str, down_thr[2])) {
            LOG_E(TAG, "string_to_integer api failed, line no  : %d", __LINE__);
        }

        string down_thr4_str = c.getConfig("fan","down_thr4", std::to_string(def_down_thr[3]), get_override_val, val_overridden);
        if (false == string_to_integer(down_thr4_str, down_thr[3])) {
            LOG_E(TAG, "string_to_integer api failed, line no  : %d", __LINE__);
        }

        string fan_pwm0_str = c.getConfig("fan","fan_pwm0", std::to_string(def_fan_pwm[0]), get_override_val, val_overridden);
        if (false == string_to_integer(fan_pwm0_str, fan_pwm[0])) {
            LOG_E(TAG, "string_to_integer api failed, line no  : %d", __LINE__);
        }

        string fan_pwm1_str = c.getConfig("fan","fan_pwm1", std::to_string(def_fan_pwm[1]), get_override_val, val_overridden);
        if (false == string_to_integer(fan_pwm1_str, fan_pwm[1])) {
            LOG_E(TAG, "string_to_integer api failed, line no  : %d", __LINE__);
        }

        string fan_pwm2_str = c.getConfig("fan","fan_pwm2", std::to_string(def_fan_pwm[2]), get_override_val, val_overridden);
        if (false == string_to_integer(fan_pwm2_str, fan_pwm[2])) {
            LOG_E(TAG, "string_to_integer api failed, line no  : %d", __LINE__);
        }

        string fan_pwm3_str = c.getConfig("fan","fan_pwm3", std::to_string(def_fan_pwm[3]), get_override_val, val_overridden);
        if (false == string_to_integer(fan_pwm3_str, fan_pwm[3])) {
            LOG_E(TAG, "string_to_integer api failed, line no  : %d", __LINE__);
        }

        string fan_pwm4_str = c.getConfig("fan","fan_pwm4", std::to_string(def_fan_pwm[4]), get_override_val, val_overridden);
        if (false == string_to_integer(fan_pwm4_str, fan_pwm[4])) {
            LOG_E(TAG, "string_to_integer api failed, line no  : %d", __LINE__);
        }

        LOG_I(TAG, "Up thresholds: %d, %d, %d, %d", up_thr[0], up_thr[1], up_thr[2], up_thr[3]);
        LOG_I(TAG, "Down thresholds: %d, %d, %d, %d", down_thr[0], down_thr[1], down_thr[2], down_thr[3]);
        LOG_I(TAG, "Fan PWM: %d, %d, %d, %d", fan_pwm[1], fan_pwm[2], fan_pwm[3], fan_pwm[4]);
    }
    return;
}

bool read_cpu_temp(int64_t *user_data)
{
    FILE *f = nullptr;
    int64_t temp_data = 0;
    char raw_cpu_temp[30] = {0};
    char *ptr1 = nullptr;
    char *ptr2 = nullptr;

    int ret_get_cpu_temp;
        LOG_D(TAG, "%s Enter...\n", __func__);
        f = fopen(CPU_TEMP, "r");
        if(f == NULL){
                LOG_E(TAG, "CPU temp fd failed");
                return false;
        }
        ret_get_cpu_temp=fscanf(f,"%s",raw_cpu_temp);
    if(ret_get_cpu_temp<1){
        LOG_E(TAG, "CPU temp read size less than 1");
        fclose(f);
        return false;
    }   
    ptr1 = strstr(raw_cpu_temp,":");
    if (ptr1 == NULL){
        LOG_E(TAG, "CPU temp is not in correct format");
        fclose(f);
        return false;
    }   
    ptr1++;
    ptr2 = strtok(ptr1," ");
    if (ptr2 == NULL){
        LOG_E(TAG, "CPU temp strtok failed");
        fclose(f);
        return false;
    }   
    std::string temp_str(ptr2);
    if(false == string_to_int64(temp_str, temp_data)) {
        LOG_E(TAG, "string_to_integer failed in read_cpu_temp");
        temp_data = 0;
    }
    LOG_D(TAG, "CPUTEMP:%lld\n",temp_data/1000);
    *user_data = temp_data/1000;
    fclose(f);
    LOG_D(TAG, "%s Exit...\n", __func__);
    return true;
}

int main(int argc, char *argv[]) {

    int fan_fd;
    int ret;
    int64_t cpu_temp = 0;
    int64_t prev_cpu_temp = 0;
    string pwm = "";
    int high_temp_thr = 0, low_temp_thr = 0;
    int fan_state = fan_state_0;

    nd_service_obj = NDService::get_service_obj(TAG); 
    nd_log_init (log_dir.c_str());
 #ifdef ROUTE_LOGS
    route_logs( log_dir.c_str() );
 #endif

    read_fan_config();

    if(fan_enable_str == "false") {
        LOG_I(TAG, "Feature Disabled, Exiting");
        if(stop_service("fan_control.service"))
        {  /* I will not come here because service is already stopped */
            LOG_I(TAG, " Fan Control Service Is Stopped Successfully");
        }

        return 0;
    }

    fan_fd=open(FAN,O_WRONLY);
    if(fan_fd < 0){
        LOG_E(TAG, "FAN:File not opened\n");
        close(fan_fd);
        nd_service_obj->send_err_msg(SM_E_FAN_SYSFS_ENTRY_FAILED, 0, "Could not open fan sysfs file");
        return -1;
    }

    while(1){
        ret=read_cpu_temp(&cpu_temp);
        if(ret == true){

            switch(fan_state)
            {
                case fan_state_0:
                    low_temp_thr = 0;
                    high_temp_thr = up_thr[0];
                    break;
                case fan_state_1:
                    low_temp_thr = down_thr[0];
                    high_temp_thr = up_thr[1];
                    break;
                case fan_state_2:
                    low_temp_thr = down_thr[1];
                    high_temp_thr = up_thr[2];
                    break;
                case fan_state_3:
                    low_temp_thr = down_thr[2];
                    high_temp_thr = up_thr[3];
                    break;
                case fan_state_4:
                    low_temp_thr = down_thr[3];
                    high_temp_thr = 255;
                    break;
                default:
                    break;
            }

            if(cpu_temp < low_temp_thr) {
                if(fan_state > fan_state_0) {
                    fan_state = fan_state - 1;
                }
            }
            else if(cpu_temp > high_temp_thr) {
                if(fan_state < fan_state_4) {
                    fan_state = fan_state + 1;
                }
            }

            pwm = std::to_string(fan_pwm[fan_state]);
            write(fan_fd, pwm.c_str(), strlen(pwm.c_str()));

        }
        LOG_I(TAG, "temperature = %lld, fan PWM = %s", cpu_temp, pwm.c_str());
        sleep(interval);
    }

    return 0;
}

