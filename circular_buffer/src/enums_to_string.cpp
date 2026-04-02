

#include "circular_buffer.h"


string cam_type_to_string (circular_buffer_camtype_t cam_type)
{

    switch (cam_type){
        case CIRCULAR_BUFFER_CAM_FRONT:
            return "OUTWARD CAMERA";
            break;
        case CIRCULAR_BUFFER_CAM_DRIVER:
            return "INWARD CAMERA";
            break;
        case CIRCULAR_BUFFER_CAM_LEFT:
            return "LEFT CAMERA";
            break;
        case CIRCULAR_BUFFER_CAM_RIGHT:
            return "RIGHT CAMERA";
            break;
        case CIRCULAR_BUFFER_CAM_EXT1:
            return "EXT CAMERA_1";
            break;
        case CIRCULAR_BUFFER_CAM_EXT2:
            return "EXT CAMERA_2";
            break;
        case CIRCULAR_BUFFER_CAM_EXT3:
            return "EXT CAMERA_3";
            break;
        case CIRCULAR_BUFFER_CAM_EXT4:
            return "EXT CAMERA_4";
            break;
        case CIRCULAR_BUFFER_CAM_ERROR:
            return "ERROR";
            break;
        default :
            return "UNKNOWN VALUE";
            break;

    }
}

string transcode_status_to_string(circular_buffer_transcode_status_t tc_status)
{

    switch(tc_status){
        case CIRCULAR_BUFFER_TC_STATUS_WAITING:
            return "TRANSCODE WAITING";
            break;
        case CIRCULAR_BUFFER_TC_STATUS_TRANSCODING:
            return "TRANSCODING";
            break;
        case CIRCULAR_BUFFER_TC_STATUS_TRANSCODED:
            return "TRANSCODED";
            break;
        case CIRCULAR_BUFFER_TC_STATUS_TRANSCODE_FAILED:
            return "TRANSCODE FAILED";
            break;
        case CIRCULAR_BUFFER_TC_STATUS_IGNORE:
            return "IGNORE";
            break;
        case CIRCULAR_BUFFER_TC_STATUS_ERROR:
            return "TRANSCODE ERROR";
            break;
        case CIRCULAR_BUFFER_TC_STATUS_VOD_REQ:
            return "VOD REQUESTS";
            break;
        default : 
            return "UNKNOWN TRANSCODE STATUS";
            break;
    }
}

string file_compression_to_string(circular_buffer_file_compression_t fc){
    switch(fc){
        case CIRCULAR_BUFFER_MEDIUM_COMPRESSION:
            return "MEDIUM COMPRESSION";
            break;
        case CIRCULAR_BUFFER_HIGH_COMPRESSION:
            return "HIGH_COMPRESSION";
            break;
        case CIRCULAR_BUFFER_NO_COMPRESSION:
            return "NO_COMPRESSION";
            break;
        case CIRCULAR_BUFFER_COMPRESSION_ERROR:
            return "COMPRESSION_ERROR";
            break;
        default:
            return "UNKNOWN COMPRESSION VALUE";
            break;
    }
}

