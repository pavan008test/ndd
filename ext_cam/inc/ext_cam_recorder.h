/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Karthik Dumpala <karthik.dumpala@netradyne.com>, October 2018
 */
#ifndef EXT_CAM_RECORDER_H
#define EXT_CAM_RECORDER_H

#include <nd_ext_cam_utils.h>
#include <fstream>
#include <nlohmann/json.hpp> // Include the JSON library

bool init_ext_camera_module();

bool set_mdvr_time();

void syncTimeToDriveriHub();

stream_file_resp_t query_for_session_info(int64_t start_time, int64_t end_time, int ch_num,
        string filename);


#endif
