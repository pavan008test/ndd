#include "rental_utils.h"
#include <data_recording.h>
#include <log.h>

#define TAG "renatl_utils"



bool RentalUtils::check_valid_vod(req_upload_msg_t *msg_req){
    bool ret = false;
    do {
        string video = msg_req->fname;
        string dir_path_from_fname = "", file_name_from_fname = "";
        if(get_folder_file_names(video, dir_path_from_fname, file_name_from_fname) == false) {
            LOG_E(TAG, "failed to split folder and file names");
            ret = true;
            break;
        }

        // get the timestamp from the file name
        string ts_str;
        stringstream ss(file_name_from_fname); 
        int count = 0;   
        // Use while loop to check the getline() function condition.
        while (getline(ss, ts_str, '_') && count < 6) {
            count = count + 1;
        }

        int64_t ts;
        bool converted = string_to_int64(ts_str, ts);
        if(!converted) {
            LOG_E(TAG, "failed to convert string to int64");
            ret = true;
            break;
        }

        LOG_I(TAG, "ts of the file name %lld", ts);
        // get the data recording status from DB
        data_record_status_db data_recording_status;

        if(!get_data_record_status_db(data_recording_status)) {
            LOG_E(TAG, "handle_data_record_status: get_data_record_status failed for DB value");
        }
        else {
            LOG_I(TAG, "data_recording_status: enabled_ts %lld, disabled_ts %lld, enabled %d", data_recording_status.enabled_ts, data_recording_status.disabled_ts, data_recording_status.enabled);
            
            if (data_recording_status.enabled) {
                LOG_I(TAG, "Data recording is enabled");
                if(data_recording_status.disabled_ts < ts && ts < data_recording_status.enabled_ts && data_recording_status.disabled_ts != 0 && data_recording_status.enabled_ts != 0) {
                    LOG_I(TAG, "VOD requested in the data recording disabled period");
                    break;
                }
            } else {
                LOG_I(TAG, "Data recording is disabled");
                if (ts > data_recording_status.disabled_ts && data_recording_status.disabled_ts != 0) {
                    LOG_I(TAG, "VOD requested in the data recording disabled period");
                    break;
                }
            }
        }

        ret = true;
    } while(false);
    return ret;
}