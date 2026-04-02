/* Copyright (C) 2022 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, October 2022
 */

#ifndef INC_ND_SAM_CONSTANTS_H_
#define INC_ND_SAM_CONSTANTS_H_

namespace nd {

namespace constants {

    static const char *const kCloudConfigFile  = "/home/ubuntu/.nddevice/latest/cloudconfig.ini";
    static const char *const kDeviceConfigFile = "/home/ubuntu/config/deviceconfig.ini";

#ifdef KRAIT
    static const char *const kSamDbFolderPath  = "/data/nd_files/db/sam_db/";
#else
    static const char *const kSamDbFolderPath  = "/home/ubuntu/.nddevice/sam_db/";
#endif

}

}

#endif // INC_ND_SAM_CONSTANTS_H_
