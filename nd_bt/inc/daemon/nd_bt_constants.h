/* Copyright (C) 2023 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, January 2023
 */

#ifndef INC_ND_BT_CONSTANTS_H_
#define INC_ND_BT_CONSTANTS_H_

namespace nd {

namespace constants {
    const char *const kBagheeraConfigFile             = "/home/ubuntu/.nddevice/latest/bagheera_config.ini";
    const char *const kCloudConfigFile                = "/home/ubuntu/.nddevice/latest/cloudconfig.ini";
    const char *const kDeviceConfigFile               = "/home/ubuntu/config/deviceconfig.ini";

    const char *const kNDDeviceConfigFile             = "/home/ubuntu/.nddevice/nddevice.ini";
    const char *const kNDConfigFile                   = "/home/ubuntu/.nddevice/latest/nd_config.ini";
    const char *const kEnablingBluetoothIndicatorFile = "/dev/shm/enableBluetooth.txt";
    const char *const kRebootIndicatorFile            = "/dev/shm/rebooting.txt";
    const char *const kDriverAppLoginIndicatorFile    = "/dev/shm/driveri_login_successful";

    const char *const kNDCoreCommonConfigFile         = "/home/ubuntu/.nddevice/latest/nd_core_common.ini";
    const char *const kSpeedInfoFile                  = "/dev/shm/speed.info";

#ifdef KRAIT
    const char *const kBtDbFolderPath                 = "/data/nd_files/db/";
    const char *const kCameraOverrideFile             = "/data/nd_files/config/cam_override.ini";
    const char *const kNDTempPath                     = "/dev/shm/";
#else
    const char *const kBtDbFolderPath                 = "/home/ubuntu/.nddevice/.ble/";
    const char *const kCameraOverrideFile             = "/home/ubuntu/config/cam_override.ini";
    const char *const kNDTempPath                     = "/dev/shm/nd_files_c/";
#endif
    const char *const kNDHomePath                     = "/home/ubuntu/.nddevice/";
}

}  // namespace nd

#endif // INC_ND_BT_CONSTANTS_H_
