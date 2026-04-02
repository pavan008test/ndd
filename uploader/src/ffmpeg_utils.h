#ifndef UPLOADER_TYPES_H
#define UPLOADER_TYPES_H

#include <string>

static const int DEVICE_CAMERA_POSITION_FRONT = 0;
static const int DEVICE_CAMERA_POSITION_BACK = 1;
static const int DEVICE_CAMERA_POSITION_LEFT = 2;
static const int DEVICE_CAMERA_POSITION_RIGHT = 3;
static const int DEVICE_CAMERA_POSITION_DMS = 8;

int get_camera_id_from_filename(string filename);

struct convertfile_args {
    std::string src;
    std::string dest;
    int cam_num;
    int framerate;
};
#endif