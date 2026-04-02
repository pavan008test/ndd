/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Y Suresh Kumar <suresh.kumar@netradyne.com>, October 2016
 */


#ifndef SPEAKER_H
#define SPEAKER_H


#define DEVICE_CONFIG_INI "/home/ubuntu/config/deviceconfig.ini"
#define ND_DEVICE_INI "/home/ubuntu/.nddevice/nddevice.ini"
#define BAGHEERACONFIG_INI "/home/ubuntu/.nddevice/latest/bagheera_config.ini"
#ifdef BAGHEERA2
#define CONFIG_PUSH_STREAMING "/home/ubuntu/.nddevice/nd_config_mod.ini"
#define CAMERA_OVERRIDE_INI "/home/ubuntu/config/cam_override.ini"
#elif KRAIT
#define CONFIG_PUSH_STREAMING "/data/nd_files/config/nd_config_mod.ini"
#define CAMERA_OVERRIDE_INI "/data/nd_files/config/cam_override.ini"
#endif

bool init_speaker();


#endif
