/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Y Suresh Kumar <suresh.kumar@netradyne.com>, October 2016
 */


#include "nd_file_utils.h"
#include "speaker.h"
#include <log.h>
#include <config_parser.h>
#include <string>       // std::string
#include <iostream>     // std::cout
#include <sstream>      // std::stringstream
#include "nd_factory.h"
#include <system_utils.h>
//TODO: Move this file to Devices folder per device
//
#ifdef BAGHEERA2

#ifdef __cplusplus
extern "C" {
#endif
    #include <aud_api.h>
#ifdef __cplusplus
}
#endif
#endif

#ifdef BAGHEERA
#include <nd_task.h>
#include <service_utils.h>
#include <system_utils.h>
#endif

#include <nd_factory.h>

using namespace std;

#define TAG "SPKR"
#define nd_device_obj (ND_DeviceFactory::Create_NDDevice())
#ifdef BAGHEERA
extern NDService *nd_service_obj; //nd service object, to detect crashes
static int set_speaker_volume_timeout = 10 ;
static int set_speaker_volume_retrial_cnt = 3;
#endif

#ifdef BAGHEERA
// i2c interface instead of direct write YSK
bool set_speaker_volume(void *args)
{
  int volume = *(int*)args;
  if( volume < 0 || volume > 10 ) // print here
  {
    LOG_E(TAG, "use valid set_speaker_volume rance %d", volume);
    return false;
  }

  int reg_val = 128 + (10 - volume)*5;

  FILE *fp=NULL;
  stringstream command;
  command.str("");
  command << "i2cset -f -y 0 0x18 0x00 1";
  LOG_I(TAG, "Command %s", command.str().c_str());
  fp = popen(command.str().c_str(), "r");
  if (fp == NULL) {
      LOG_E(TAG, "Failed to run command :: %s" , command.str().c_str() );
      return false;
  }
  pclose(fp);

  command.str("");
  command << "i2cset -f -y 0 0x18 0x26 " << reg_val;
  LOG_I(TAG, "Command %s", command.str().c_str());
  fp = popen(command.str().c_str(), "r");
  if (fp == NULL) {
      LOG_E(TAG, "Failed to run command :: %s" , command.str().c_str() );
      return false;
  }
  pclose(fp);

  LOG_I(TAG, "returning from set_speaker_volume");
  return true;
}
#elif KRAIT
// i2c interface instead of direct write YSK
bool set_speaker_volume(int volume)
{
  if( volume < 0 || volume > 10 ) // print here
  {
    LOG_E(TAG, "use valid set_speaker_volume rance %d", volume);
    return false;
  }

  int reg_val = 128 + (10 - volume)*5;

  FILE *fp=NULL;
  stringstream command;
  command.str("");
  command << "i2cset -f -y 0 0x18 0x00 1";
  LOG_I(TAG, "Command %s", command.str().c_str());
  fp = popen(command.str().c_str(), "r");
  if (fp == NULL) {
      LOG_E(TAG, "Failed to run command :: %s" , command.str().c_str() );
      return false;
  }
  pclose(fp);

  command.str("");
  command << "i2cset -f -y 0 0x18 0x26 " << reg_val;
  LOG_I(TAG, "Command %s", command.str().c_str());
  fp = popen(command.str().c_str(), "r");
  if (fp == NULL) {
      LOG_E(TAG, "Failed to run command :: %s" , command.str().c_str() );
      return false;
  }
  pclose(fp);

  LOG_I(TAG, "returning from set_speaker_volume");
  return true;
}
#endif

// move to a speaker volume file
bool init_speaker() {
    LOG_I(TAG, "Inside init_speaker");
    nd_speaker_volume_params vol_params;
    get_speaker_volume_config(vol_params);

#ifdef BAGHEERA
    int cnt = 0 ;
    task_result_t task_result ;
    while(cnt++ < set_speaker_volume_retrial_cnt){
        task_result = nd_timed_task (set_speaker_volume, set_speaker_volume_timeout, (void *)&volume, "set_speaker_volume");
        if (task_result != TASK_SUCCESS) {
            LOG_E (TAG, "nd_timed_task for set_speaker_volume() timeout");
            nd_service_obj->send_err_msg(SM_E_NDC_SET_SPEAKER_VOLUME_FAIL, 0, "set_speaker_volume");
            sleep(1);
        }
        else {
            break;
        }
    }
    if (task_result != TASK_SUCCESS) {
        return false;
    }
    LOG_I(TAG, "set_speaker_volume %d", volume);
#endif

    // Speaker initialization
    if(0 == nd_device_obj->speaker_init(vol_params)){
        LOG_I(TAG,"Speaker init successful");
    }else{
        LOG_E(TAG,"Speaker init failed");
    }

    return true;
}
