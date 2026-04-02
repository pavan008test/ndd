/* Copyright (C) 2018 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Anirudh Maringanti <anirudh.maringanti@netradyne.com>
 */

#include "transcode_thread.h"
#include "system_utils.h"
#include <queue>
#include "service_utils.h"
#include "circular_buffer.h"
using namespace std;
extern ND_DeviceFactory *nd_device_obj;
static const char *TAG="TC_T";

// FOR READING THE INI FILES
static const string BAGHEERACONFIG_INI = "/home/ubuntu/.nddevice/latest/bagheera_config.ini";       
static const bool BAGHEERA_OVERRIDE = true;
static const string TRANSCODE_SECTION = "Transcode";
static const string TRANSCODE_ENABLE = "enable";
static const string NUM_HQ_VIDEOS_CONFIG = "num_hq_videos_max";
static const string TRANSCODE_DELAY_STR = "delay";
static const string SMART_TRANSCODE_STR = "enable_smart_transcode";
static const string INWARD_TRANSCODE_STR = "enable_inward_transcode";
static const string dpq_extn=".dp.mp4";
static const string NUM_DMS_VIDEOS_CONFIG = "num_dms_videos_max";
static const string NUM_LQ_DMS_VIDEOS_CONFIG = "num_lq_dms_videos_max";
static const string NUM_DMS_ALERT_VIDEOS_CONFIG = "num_dms_alert_videos_max";

//Start transcode thread loop after the following delay so that db cleanup and 
//other circular_buffer startup functions can complete
static int64_t TRANSCODE_START_DELAY=16;
std::queue<circular_buffer_file_t> tc_and_cp_file_queue;         // files are transcoded and copied to SdCard.
static const int tc_and_cp_file_queue_size = 8;

// Transcode thread parameters
static const bool TC_ENABLE=true; // by default the transcode thread is enabled
#ifdef KRAIT
static int64_t NUM_HQ_VIDEOS_MAX=60; // 1 hr of HQ videos stored in DB
#else
static int64_t NUM_HQ_VIDEOS_MAX=120; // 2 hr of HQ videos stored in DB
#endif
static int64_t NUM_HQ_DMS_VIDEOS_MAX=120; // 120 DMS video stored in storage.
int64_t NUM_DMS_ALERT_VIDEOS_MAX=0; // % of NUM_HQ_DMS_VIDEOS_MAX   used only when store_dms_file == false
static int64_t TRANSCODE_DELAY=10; //delay between subsequent transcodes
static bool INWARD_TC_ENABLE=true; //by default inward transcoding is enabled
//by default smart transcoding is enabled. not static as it is required by
//circular_buffer.cpp
bool SMART_TC_ENABLE=true;
extern bool store_dms_file;
extern bool store_lq_dms_file;
extern bool dms_camera_enabled;
static const int LqDmsDeleteAfterMinSessions = 5;

/* at the start transcoding is stopped. It is started only after sync_directories()
 * function is complete. sync_directories function is called in circular_buffer.cpp
 * by circular_buffer_msg_loop() function. After the function is complete, the
 * following variable is set to true.
*/
bool START_TRANSCODING = false;
/*
 * Use the following variable to set the amount of time transcode thread waits for 
 * transcode process to complete
 * For 1920 x 1080 - transcode takes ~25s on average (set 60s recommended)
 * For 1280 x 720  - transcode takes 15-20s on average (set 45s/60s recommended)
 * For 854 x 480 - transcode takes 8-12s on average (set 30s recommended)
 */
static int64_t TRANSCODE_PROCESS_TIMEOUT=60;

static const string DEF_INI_DEVICE_VERSION = "0.0.0";
static const string DEF_INI_SESSION_ID = "none";
static const string DEF_INI_DEVICE_ID = "none";
static const string DEF_INI_DEVICE_TYPE = "bagheera";
static const string DEF_INI_PERCENT_CIRC_BUFF = "90";
static const string DEF_INI_SERVER = "prod";
static const string DEF_INI_API_VERSION = "v1";
static const string DEF_INI_SERVER_URL = "https://idms.netradyne.com/restserver/api";

static const string ld_extn = ".ld.mp4";

//enum is expected to be one more than the number of cameras
static const int NUM_CAMERAS_MAX = (int) CIRCULAR_BUFFER_CAM_ERROR;

//enum is expected to be one more than the number compression schemes
//one of the schemes is NO COMPRESSION. Subtracting 1
static const int NUM_COMPRESSION_SCHEMES = ((int) CIRCULAR_BUFFER_COMPRESSION_ERROR) - 1;


//static transcode_scheme transcode_scheme_list[NUM_CAMERAS_MAX][NUM_COMPRESSION_SCHEMES];


extern circular_buffer_vid *CIRC_BUFF_ctx;
extern NDMBServer server;
int64_t num_hq_videos_max = NUM_HQ_VIDEOS_MAX;
bool drp_enabled = false;
int delete_n_old_session_drp();
int delete_clock_hours_drp();

#define SIGN_CROP_DRP_CYCLE 12*60
void initialize_sign_crop_files(vector<circular_buffer_drp_t> &signCropFiles, const string& SIGN_CROP_OUTPUT_PATH);
extern vector<circular_buffer_drp_t> signCropFiles;
std::shared_ptr<ndmbmsg_oldest_uploadable_file> drp_oldest_uploadable_file = nullptr;
void update_file_status(circular_buffer_file_t inp_fileinfo, bool transcode_status)
{
    int rc;
    int indexid = inp_fileinfo.index_id;
    circular_buffer_transcode_status_t status;
    LOG_D(TAG, "Trying to update post transcode status of file %s, %d", inp_fileinfo.base_file_name, inp_fileinfo.index_id);
    if (transcode_status == false){
        //Transcoding failed
        //Filetype kept the same
        status = CIRCULAR_BUFFER_TC_STATUS_TRANSCODE_FAILED;
        rc = update_tc_status_DB(indexid, status);
        if (rc == false){
            LOG_E(TAG, "Failed to update file tc status to TRANSCODE_FAILED post unsuccessful transcoding");
        }
        return;
    }
    
    //Transcode successful

    //It is possible the transcode status for a file is changed during the transcoding.
    //Verify if the original file has to be kept before copying the transcoded file

    //if original file was requested for upload, keep original file
    //transcode status does not need to be changed
    if (check_file_transcode_status(inp_fileinfo.index_id, CIRCULAR_BUFFER_TC_STATUS_VOD_REQ)){
        LOG_I(TAG, "File %s has been requested by VOD service. Keeping the original file and discarding transcoded file",inp_fileinfo.base_file_name);

#if 0        
        string file_name_ld = inp_fileinfo.base_file_name + ld_extn;
	if(hard_delete_entry_from_db(file_name_ld)) {
		string orig_file_path_ld = SDCARD_MOUNT_PATH + inp_fileinfo.base_file_name + ld_extn;
		bool update_status = file_delete(orig_file_path_ld);
		if (!update_status){
			//setting the original file to transcode failed
			status = CIRCULAR_BUFFER_TC_STATUS_TRANSCODE_FAILED;
			rc = update_tc_status_DB(indexid, status);
			if (rc == false){
				LOG_E(TAG, "Failed to update file tc status to TRANSCODE_FAILED post failure to delete LD file");
			}
		}

	} else {
		LOG_E(TAG, "Could not delete DB entry for %s",  inp_fileinfo.base_file_name + ld_extn);
		status = CIRCULAR_BUFFER_TC_STATUS_TRANSCODE_FAILED;
		rc = update_tc_status_DB(indexid, status);
		if (rc == false){
			LOG_E(TAG, "Failed to update file tc status to TRANSCODE_FAILED");
		}

	}
#endif
        return;
    }
    //if original file was requested to have no compression, keep original file
    if (check_file_compression_type(inp_fileinfo.index_id, CIRCULAR_BUFFER_NO_COMPRESSION)){
        LOG_I(TAG, "File %s has been requested by analytics to not be transcoded (NO COMPRESSION). Keeping the original file and discarding transcoded file",inp_fileinfo.base_file_name);

#if 0
        string file_name_ld = inp_fileinfo.base_file_name + ld_extn;
        if(hard_delete_entry_from_db(file_name_ld)) {
            string orig_file_path_ld = SDCARD_MOUNT_PATH + inp_fileinfo.base_file_name + ld_extn;
            bool update_status = file_delete(orig_file_path_ld);
            if (!update_status){
                //setting the original file to transcode failed
                status = CIRCULAR_BUFFER_TC_STATUS_TRANSCODE_FAILED;
                rc = update_tc_status_DB(indexid, status);
                if (rc == false){
                    LOG_E(TAG, "Failed to update file tc status to TRANSCODE_FAILED post failure to delete LD file");
                }
            }

        } else {
            LOG_E(TAG, "Could not delete DB entry for %s",  inp_fileinfo.base_file_name + ld_extn);
            status = CIRCULAR_BUFFER_TC_STATUS_TRANSCODE_FAILED;
            rc = update_tc_status_DB(indexid, status);
            if (rc == false){
                LOG_E(TAG, "Failed to update file tc status to TRANSCODE_FAILED");
            }

        }
#endif
        if(inp_fileinfo.base_file_name[0] != '8')
            return;
        else 
            LOG_E(TAG, "dms alert file , not returning %s ", inp_fileinfo.base_file_name );
    }
    if (check_file_compression_type(inp_fileinfo.index_id, CIRCULAR_BUFFER_NO_COMPRESSION_NO_LQ)){
        LOG_I(TAG, "File %s has been requested by analytics to not be transcoded (NO COMPRESSION). No LQ file ",inp_fileinfo.base_file_name);
        return;
    }

    //move file
    //check if sdcard is mounted before moving transcoded file
    mount_status_t card_status = get_mount_status(nd_device_obj->get_external_eMMC_phy_mount_path(), CIRC_BUFF_ctx->buffer_mount_src);
    if(MOUNTED != card_status){
        LOG_E(TAG,"SDCARD UNMOUNTED: Unable to move transcoded file %s. card status = %d", inp_fileinfo.base_file_name, card_status);
        status = CIRCULAR_BUFFER_TC_STATUS_WAITING;
        LOG_I(TAG,"Setting TC STATUS for %s back to WAITING as the transcoded file was unable to be moved due to sdcard unmounted", inp_fileinfo.base_file_name);
        rc = update_tc_status_DB(indexid, status);
        if (rc == false){
            LOG_E(TAG, "Failed to update %s tc status to WAITING", inp_fileinfo.base_file_name);
        }
        return;
    }

    string orig_file_path = nd_device_obj->get_external_eMMC_mount_path() + inp_fileinfo.base_file_name;
    //copy successful
    string hd_file_name = inp_fileinfo.base_file_name;
    string ld_file_name = hd_file_name + ld_extn;
        // proceed to delete HD file only if corresponding LD file existed in SD card or IB
    string cb_ld_file_path = nd_device_obj->get_external_eMMC_mount_path() + ld_file_name;
    if(hd_file_name.find("8_trip") != string::npos){
        delete_entry_from_db(inp_fileinfo.index_id, inp_fileinfo.file_type);
        bool del_status = delete_file_from_sdcard(hd_file_name);
        if(file_is_present(cb_ld_file_path)) {
            if(del_status){
                hard_delete_entry_from_db(ld_file_name);  
            }
            else {  // It should never go inside else
                // delete_entry_from_db(ld_file_name, inp_fileinfo.file_type);
	            LOG_I(TAG, "update_file_status() file %s deletion status: %d", inp_fileinfo.base_file_name, del_status);
            }
	        LOG_D(TAG, "DMS Camera HD file %s deletion status: %d", inp_fileinfo.base_file_name, del_status);
            del_status = delete_file_from_sdcard(ld_file_name);
	        LOG_D(TAG, "DMS Camera LD file %s deletion status: %d", inp_fileinfo.base_file_name, del_status);
        }
    }
    else {
        // do not delete original file, instead update its status as transcoded
        if(hd_file_name.find(dpq_extn) == string::npos) { // // let dpq file get deleted.
            if(!file_is_present(cb_ld_file_path)) {
                    LOG_E(TAG, "LD file corresponding to %s not found. keeping HD file itself as transcoded", inp_fileinfo.base_file_name);
                    rc = update_tc_status_DB(indexid, CIRCULAR_BUFFER_TC_STATUS_TRANSCODED);
                    if (rc == false){
                        LOG_E(TAG, "Failed to update file tc status to TRANSCODED post failure to find LD file");
                    }
                    return;
            }
        }

        string file_path = "";
        file_path = get_file_path(inp_fileinfo.base_file_name);
        if(file_path == "") {
            LOG_E(TAG, "File is not available");
        }
        string file_to_delete = file_path + inp_fileinfo.base_file_name;
        //delete original file
        bool update_status = file_delete(file_to_delete);
        if (!update_status){
            //File delete failed
            //Keep original file
            LOG_E(TAG, "Failed to delete original file %s", file_to_delete.c_str());

            //setting the original file to transcode failed
            status = CIRCULAR_BUFFER_TC_STATUS_TRANSCODE_FAILED;
            rc = update_tc_status_DB(indexid, status);
            if (rc == false){
                LOG_E(TAG, "Failed to update file tc status to TRANSCODE_FAILED post failure to delete original file");
            }
            return;
        }
        else { // Original file delete successful case
            string file_name = inp_fileinfo.base_file_name;
            if(!(hard_delete_entry_from_db(file_name))) { // hard_delete entry failed
                LOG_E(TAG, "Could not delete DB entry for %s",  inp_fileinfo.base_file_name);
                status = CIRCULAR_BUFFER_TC_STATUS_TRANSCODE_FAILED;
                rc = update_tc_status_DB(indexid, status);
                if (rc == false){
                    LOG_E(TAG, "Failed to update file tc status to TRANSCODE_FAILED");
                }
            }
        }
    }
}

//copy of function implemented in nd_central.cpp
static bool convert_file(string fname) {   
    const string src = fname+ ".mkv";
    const string dest = fname+ ".mp4";
    const string convert_cmd = "ffmpeg -i "+src+" -y -vcodec copy "+dest;
    int64_t time_before = 0, time_after = 0;

    LOG_I(TAG, "Convert cmd: %s", convert_cmd.c_str());
    time_before = get_system_time();
    int res = system(convert_cmd.c_str());
    time_after = get_system_time();
    LOG_I (TAG, "Conversion took %lld ms", (time_after - time_before));

    bool dres = file_delete(src);
    if( dres == false ) {
        LOG_E(TAG, "Failed to remove: %s", src);
    }
    else {
        LOG_I (TAG, "%s file deleted", src.c_str());
    }

    if( res ==0 && dres ) {
        LOG_I(TAG, "Returning true from convert_file function");
        return true;
    }

    LOG_I(TAG, "Returning false from convert_file function");
    return false;
}

/*
 * Following function written for the transcode thread implementation
 * Anirudh Maringanti <anirudh.maringanti@netradyne.com>
 * 22-11-2017
 */


void check_update_config_value(bool& current_value, Config_parser& c, const string config_name, bool override_val, const string config_filename)
{

    bool is_val_overridden = false;
    if (!c.isPresent(TRANSCODE_SECTION, config_name))
    {
        LOG_I(TAG, "Config file %s does not have transcode section / %s key. Using existing value of %s=%s", config_filename.c_str(), config_name.c_str(),
                        config_name.c_str(), ((current_value) ? "true" : "false"));
        return;
    }

    string value_from_config_file = c.getConfig(TRANSCODE_SECTION, config_name, "", override_val, is_val_overridden);
    bool converted_value = true;
    string src_config_file = (is_val_overridden) ? "override config ini" : config_filename;
    LOG_I(TAG, "Obtained %s = %s from %s file", config_name.c_str(), value_from_config_file.c_str(), src_config_file.c_str());
    set<string> truth_values = {"true", "1", "0", "false"};

    set<string>::iterator found = truth_values.find(value_from_config_file);

    if (found == truth_values.end()){
        LOG_E(TAG, "Value for %s=%s in %s is invalid. Using exisitng value of %s",
                config_name.c_str(), value_from_config_file.c_str(),
                src_config_file.c_str(), ((current_value) ? "true" : "false"));
        return;
    }

    if ((value_from_config_file.compare("true") == 0) || (value_from_config_file.compare("1") == 0))
    {
        converted_value = true;
    }else {
        converted_value = false;
    }

    if (current_value == converted_value){
        LOG_I(TAG, "%s value in %s same as current value = %s. Value unchanged", 
            config_name.c_str(), src_config_file.c_str(), ((current_value) ? "true" : "false"));
        return;
    }

    current_value = converted_value;
    LOG_I(TAG, "%s value changed to %s from current value of %s. Config file src = %s",
            config_name.c_str(), ((current_value) ? "true" : "false"),
            ((!current_value) ? "true" : "false"), src_config_file.c_str());
}

void check_update_config_value(int64_t& current_value, Config_parser& c, const string config_name, bool override_val, const string config_filename)
{
    char  *startptr = NULL, *endptr=NULL; //for strtol
    bool is_val_overridden = false;
    if (!c.isPresent(TRANSCODE_SECTION, config_name))
    {
        LOG_I(TAG, "Config file %s does not have transcode section / %s key", config_filename.c_str(), config_name.c_str());
        LOG_I(TAG, "Using current value of %s = %d", config_name.c_str(), current_value);
        return;
    }

    string value_from_config_file = c.getConfig(TRANSCODE_SECTION, config_name, "", override_val, is_val_overridden);
    string src_config_file = (is_val_overridden) ? "override config ini" : config_filename;

    LOG_I(TAG, "Obtained %s = %s from %s file", config_name.c_str(), value_from_config_file.c_str(), src_config_file.c_str());

    startptr = (char* ) value_from_config_file.c_str();
    int64_t converted_value = (int64_t)strtol(startptr, &endptr, 10);
    if ( startptr && !*endptr)
    {
        if (current_value != converted_value){
            LOG_I(TAG, "%s changed from %lld to %lld after reading %s ", config_name.c_str(), current_value, converted_value, src_config_file.c_str());
            current_value = converted_value;
        }else{
            LOG_I(TAG, "%s value in %s same as current value = %lld. Value unchanged", config_name.c_str(), src_config_file.c_str(), current_value);
        }
    }
    else {
        LOG_E(TAG, "%s value in %s returned %s. INVALID VALUE. Using current value = %lld", config_name.c_str(), src_config_file.c_str(), value_from_config_file.c_str(), current_value);
    }
}


void parse_config(const string config_filename, bool override_val = true)
{
    Config_parser c(config_filename);
    char  *startptr = NULL, *endptr=NULL; //for strtol
    if (c.getParseStatus() != true)
    {
        LOG_E(TAG, "Unable to parse %s", config_filename.c_str());
        return;
    }

    //Get transcode enable value
    bool tc_is_enabled = true;
    check_update_config_value(tc_is_enabled, c, TRANSCODE_ENABLE, override_val, config_filename);
    if(tc_is_enabled != TC_ENABLE){
        LOG_C(TAG, "Transcode is disabled, enabling by default");
    }

    //Get num_hq_videos_max value
    check_update_config_value(NUM_HQ_VIDEOS_MAX, c, NUM_HQ_VIDEOS_CONFIG, override_val, config_filename);

    //Get num_dms_videos_max value
    if(dms_camera_enabled == true ){ 
        check_update_config_value(NUM_HQ_DMS_VIDEOS_MAX, c, NUM_DMS_VIDEOS_CONFIG, override_val, config_filename);
        if(store_dms_file == false){ 
            check_update_config_value(NUM_DMS_ALERT_VIDEOS_MAX, c, NUM_DMS_ALERT_VIDEOS_CONFIG, override_val, config_filename);
            NUM_DMS_ALERT_VIDEOS_MAX = NUM_DMS_ALERT_VIDEOS_MAX*NUM_HQ_DMS_VIDEOS_MAX/100; 
            LOG_I(TAG, "NUM_HQ_DMS_VIDEOS_MAX = %lld , NUM_DMS_ALERT_VIDEOS_MAX: %lld ",  NUM_HQ_DMS_VIDEOS_MAX, NUM_DMS_ALERT_VIDEOS_MAX );
        }
    }
    //Get transcode delay value
    check_update_config_value(TRANSCODE_DELAY, c, TRANSCODE_DELAY_STR, override_val, config_filename);

    //Get smart transcode enable / disable value
    check_update_config_value(SMART_TC_ENABLE, c, SMART_TRANSCODE_STR, override_val, config_filename);

    //Get inward transcode enable / disable value
    check_update_config_value(INWARD_TC_ENABLE, c, INWARD_TRANSCODE_STR, override_val, config_filename);

}

//function to format and log the number of the videos in the db
void print_num_video_file_data(num_video_file_data_t vd_data)
{   
    
    string cam_type_str = cam_type_to_string(vd_data.cam_type);
    string transcode_status_str = transcode_status_to_string(vd_data.tc_status);
    
    std::stringstream val_stream;
    val_stream << cam_type_str << ", " << transcode_status_str << ", NUMFILES = " 
               << vd_data.num_files;

    string val_string = val_stream.str();

    LOG_I(TAG, "%s", val_string.c_str());
}


//top level function to get and log number of videos files in the database
void print_all_num_video_file_data(){

    vector<num_video_file_data_t> num_video_files;
    
    LOG_I(TAG, "Getting info about number of videos files in db");

    if (!get_num_video_audio_files(num_video_files)){
        LOG_E(TAG, "Failed to get video or audio file data in db");
        return;
    }
    
    LOG_I(TAG, "Found %d rows", num_video_files.size());
    int64_t total_front_cam_vids = 0, total_front_cam_lq_vids = 0,
            total_front_cam_failed_vids = 0, total_front_cam;
    //using range-based for loop (available in C++11 onwards)
    for (num_video_file_data_t vd_data : num_video_files){
        print_num_video_file_data(vd_data);
    }

}

void run_transcode_process(circular_buffer_file_t inp_fileinfo, bool tmpdir_enabled)
{

    //string input_file = inp_fileinfo.base_file_name;
    //string input_file_path = SDCARD_MOUNT_PATH + input_file;//SDCARD_MOUNT_PATH should have a trailing /
    //file_delete(input_file_path);

}


bool is_inward_available(circular_buffer_file_t& inward_fileinfo, circular_buffer_file_t& front_fileinfo, circular_buffer_camtype_t camtype)
{
    char inward_file_name[FNAME_LEN] = {0};
    strncpy(inward_file_name, front_fileinfo.base_file_name, sizeof(inward_file_name));
    //Change the first char from 0 to 1 to query for inward video
    inward_file_name[0]='0' + (int) camtype;
    LOG_I(TAG, "Checking availability for transcode for %s", inward_file_name);
    //set inward_fileinfo.base_file_name to NULL
    inward_fileinfo.base_file_name[0] = '\0';

    //Fetch details about inward file

    //Returns false only if sql query fails to run
    if (!(get_fileinfo_from_filename(&inward_fileinfo, inward_file_name))){
        LOG_E(TAG,"Failed to get details about %s", inward_file_name);
        return false;
    }

    //Check if inward_fileinfo is still NULL
    //Query will return successfully even if there is no entry in db
    if (inward_fileinfo.base_file_name[0] == '\0'){
        LOG_I(TAG, "No entry in DB for %s", inward_file_name);
        return false;
    }

    //Check if inward is available for transcoding
    if (inward_fileinfo.tc_status == CIRCULAR_BUFFER_TC_STATUS_WAITING){
        return true;
    }

    LOG_I(TAG,"%s video has transcode status %s. Not transcoding", inward_file_name, (transcode_status_to_string(inward_fileinfo.tc_status)).c_str());
    return false;
}

int delete_old_files_out_of_drp(){
    static int sign_crop_cache_counter = 0;
    const string SIGN_CROP_OUTPUT_PATH = nd_device_obj->get_sign_crop_base_path();  

    if(sign_crop_cache_counter%SIGN_CROP_DRP_CYCLE == 0){
        initialize_sign_crop_files(signCropFiles, SIGN_CROP_OUTPUT_PATH); 
        sign_crop_cache_counter = 0;
    }

    sign_crop_cache_counter++;

    // upper level check to delete the files(if any) older than drp_clock_hours*60 sessions.
    int file_deleted_count = delete_n_old_session_drp();
    LOG_I(TAG, "Total video file deleted in delete_n_old_session_drp() : %d", file_deleted_count);

    // Actual file deletion of old files out of drp.
    file_deleted_count = delete_clock_hours_drp();
    LOG_I(TAG, "Total video file deleted in delete_clock_hours_drp() : %d", file_deleted_count);

    return 0;
}

/**
 * @brief Monitors DMS (Driver Monitoring System) video file storage and deletes the oldest file if thresholds are exceeded.
 *
 * This function checks the current count of HQ and LQ DMS video files in the database.
 * - If no specific file name is provided, it determines whether HQ or LQ video count exceeds defined limits,
 *   and retrieves the oldest matching file accordingly.
 * - If a specific file name is provided, it attempts deletion for that file directly, assuming it is LQ.
 *
 * The deletion process includes:
 * - Removing the file from the SD card.
 * - Performing either a soft or hard delete from the database based on storage thresholds.
 *
 * @param dms_file_name Optional file name of the DMS video to delete. If empty, the oldest eligible file is selected based on thresholds.
 *
 * @note The function includes safeguards to avoid deletion when thresholds are not exceeded, or if file information retrieval fails.
 */
void monitor_and_delete_dms_file(string dms_file_name = ""){
	bool rc;
	int64_t num_lq_dms_videos_db = 0;
	int64_t num_hq_dms_videos_db = 0;
	bool isHQ = true;
	if (! dms_file_name.empty()) {
	    isHQ = false;
		if(file_is_present(nd_device_obj->get_external_eMMC_mount_path() + dms_file_name) == false){
                    return ;
		}
		LOG_I(TAG, "Running storage check for dms file: %s isHQ: %d", dms_file_name.c_str(), isHQ );
	}
	else {
		bool alert_dms_video = false;
		circular_buffer_file_t dms_fileinfo;
		rc = get_num_lq_videos_db(&num_lq_dms_videos_db, CIRCULAR_BUFFER_CAM_DMS );
		rc = get_num_hq_videos_db(&num_hq_dms_videos_db, CIRCULAR_BUFFER_CAM_DMS );
		if (num_lq_dms_videos_db > NUM_HQ_VIDEOS_MAX || (store_lq_dms_file == false && num_lq_dms_videos_db > LqDmsDeleteAfterMinSessions) ) { // Keep lq DMS file for 5 minutes.
			LOG_I(TAG, "Num LQ DMS files in db %lld, NUM_HQ_VIDEOS_MAX = %lld, %lld ", num_lq_dms_videos_db, NUM_HQ_VIDEOS_MAX, num_hq_dms_videos_db );
			isHQ = false;
		}
		else if (num_hq_dms_videos_db > NUM_HQ_DMS_VIDEOS_MAX){
			LOG_I(TAG, "Num HQ DMS files in db %lld, NUM_HQ_DMS_VIDEOS_MAX = %lld, %lld ", num_hq_dms_videos_db, NUM_HQ_DMS_VIDEOS_MAX, num_lq_dms_videos_db );
		}
		else {
			LOG_I(TAG, "Not deleting any dms file. num_hq_dms_videos_db: %lld, num_lq_dms_videos_db: %lld", num_hq_dms_videos_db, num_lq_dms_videos_db);
			return;
		}
		get_oldest_dms_file(&dms_fileinfo , isHQ);
		dms_file_name = dms_fileinfo.base_file_name;
		LOG_I(TAG, "Running storage check for dms file: %s isHQ:::::  %d", dms_file_name.c_str(), isHQ );
		if (isHQ && dms_fileinfo.file_compression == CIRCULAR_BUFFER_NO_COMPRESSION) {
			string ld_file_name = dms_fileinfo.base_file_name + ld_extn;
			string cb_ld_file_path = nd_device_obj->get_external_eMMC_mount_path() + ld_file_name;
 
			// It will get come here when num_dms_videos_max < num_hq_videos_max and it is alert session. Delete HQ if LQ is available.
			if (file_is_present(cb_ld_file_path)) {  
				hard_delete_entry_from_db(dms_file_name);
				delete_file_from_sdcard(dms_file_name);
				LOG_I(TAG, "%s delete DMS HQ alert file, LQ is available ", dms_file_name.c_str());
				return;
			} else {
				LOG_I(TAG, "%s Don't delete DMS HQ alert file, LQ missing ", dms_fileinfo.base_file_name);
				if (!update_tc_status_DB(dms_fileinfo.index_id, CIRCULAR_BUFFER_TC_STATUS_TRANSCODED)) {
					LOG_E(TAG, "Failed to update file tc status to TRANSCODED for %s having NO_COMPRESSION type", dms_fileinfo.base_file_name);
				}
				return;
			}
		}

	}
	bool del_status = delete_file_from_sdcard(dms_file_name );
	if (!del_status) {
		LOG_E(TAG, "Failed to delete file: %s", dms_file_name.c_str());
	}
	bool shouldHardDelete = false ; 
	if(isHQ) {
		string ld_file_name = dms_file_name + ld_extn;
		string cb_ld_file_path = nd_device_obj->get_external_eMMC_mount_path() + ld_file_name;

		if (file_is_present(cb_ld_file_path)) {
			shouldHardDelete = true;
		}

	}
	else {  

		string hd_file_name = dms_file_name ;
		hd_file_name = hd_file_name.substr(0, hd_file_name.find(ld_extn) );
		string cb_hd_file_path = nd_device_obj->get_external_eMMC_mount_path() + hd_file_name;

		if (file_is_present(cb_hd_file_path)) {
			shouldHardDelete = true;
		}
	}
	LOG_I(TAG, "shouldHardDelete:  %d  %s %d", shouldHardDelete, dms_file_name.c_str(), isHQ );

	if (shouldHardDelete) {
		hard_delete_entry_from_db(dms_file_name);
	} else {
		circular_buffer_file_t dms_fileinfo;
		if (!get_fileinfo_from_filename(&dms_fileinfo, dms_file_name.c_str())) {
			LOG_E(TAG, "Failed to get details about %s", dms_file_name.c_str());
			return;
		}
		delete_entry_from_db(dms_fileinfo.index_id, dms_fileinfo.file_type);
	}
}

void* storage_monitor_main(void* args)
{
    int64_t num_hq_videos_db = 0 ;
    int64_t num_hq_dms_videos_db = 0;
    bool rc;

    if (NULL == CIRC_BUFF_ctx){
        LOG_E(TAG, "Circular buffer context is Null");
    }

    //setup db for transcode
    bool db_sanitized = sanitize_db_for_tc();
    if (!db_sanitized){
        LOG_E(TAG, "Failed to sanitize db for transcoding (setting up additional column, cleaning data etc)");
        LOG_E(TAG, "Exiting transcode thread");
        pthread_exit(NULL);
    }
    LOG_I(TAG, "DB sanitized. Ready for transcoding.");
  
    LOG_I(TAG, "Reading %s", BAGHEERACONFIG_INI.c_str());
    parse_config(BAGHEERACONFIG_INI, BAGHEERA_OVERRIDE);

    //wait for some time before starting the process. primarily for the device to initialize
    LOG_I(TAG, "Transcoding will start after %llds", TRANSCODE_START_DELAY);
    sleep(TRANSCODE_START_DELAY);

    //print video file data
    //running after delay so that circular_buffer main process can complete a db cleanup
    print_all_num_video_file_data();

    int wait_on_sync_directories = 30;
    while(!START_TRANSCODING){
        LOG_I(TAG, "TC not started. Waiting on sync_directiories(). Checking after %d seconds", wait_on_sync_directories);
        sleep(wait_on_sync_directories);
    }
    LOG_I(TAG, "sync_directories() completed. Starting TC");
    int64_t tc_time_start=0, tc_time_end=0; 
    bool checked_for_inward_video = false;
    int tc_sleep_duration = TRANSCODE_DELAY;

    while(1){
        if(drp_enabled){
            LOG_I(TAG, "drp is enabled. calling delete_old_files_out_of_drp() ");
            delete_old_files_out_of_drp();
        }
        res_storage_health_msg_t storage_info;
        string udid = "", fileName = "";
        int ret = get_oldest_video_details(storage_info.ovd_ts,storage_info.ovd_lat,storage_info.ovd_lon,udid, fileName);
        if(ret < 0){
            fileName = "";//empty string check handled at uploader
            udid = "-1";
            LOG_E(TAG,"Unable to get oldest uploadable video details");
        }
        if(nullptr == drp_oldest_uploadable_file){
            drp_oldest_uploadable_file = std::make_shared<ndmbmsg_oldest_uploadable_file>();
            if(nullptr == drp_oldest_uploadable_file){
                LOG_C(TAG,"Unable to create shared pointer for broadcast");
            }
        }
        if(nullptr != drp_oldest_uploadable_file){
            nd_strncpy(drp_oldest_uploadable_file->base_file_name,fileName.c_str(),FNAME_LEN);
            drp_oldest_uploadable_file->uptime = get_system_monotonic_time();
            LOG_I(TAG,"publish filename to upl %s ",drp_oldest_uploadable_file->base_file_name);
            bool msg_success = server.publish(TOPIC_OLDEST_UPLOADABLE_FILE,drp_oldest_uploadable_file);
            if(false == msg_success){
                LOG_E(TAG,"Unable to publish drp_last_uploadable_file");
            }else{
                LOG_I(TAG,"Message Published");
            }
        }else{
            LOG_C(TAG,"Unable to broadcast as shared pointer is NULL");
        }
        sleep(tc_sleep_duration);
        // sleep 60 secs in cases we are continuing below
        tc_sleep_duration = 60;
        if (!TC_ENABLE){
            //TC_ENABLE == false
            LOG_I(TAG, "Transcoding disabled. Checking after %d seconds", tc_sleep_duration);
            continue;
        }

        // if sdcard is unmounted do not trigger transcoding process. Check after
        // some time.
        mount_status_t card_status = get_mount_status(nd_device_obj->get_external_eMMC_phy_mount_path(),CIRC_BUFF_ctx->buffer_mount_src);
        if(MOUNTED != card_status){
            LOG_E(TAG,"SDCARD UNMOUNTED: Transcoding stopped. Checking after %d seconds", tc_sleep_duration);
            continue;
        }

        if(MOUNTED_READONLY == card_status){
            LOG_E(TAG,"SDCARD READ_ONLY: Transcoding stopped. Checking after %d seconds", tc_sleep_duration);
            continue;
        }

        int64_t free_space = file_getfreespace(nd_device_obj->get_external_eMMC_mount_path());
        int64_t backup_space = get_total_backup_space();
        LOG_I(TAG, "Free space in eMMC = %lld bytes, total backup space = %lld bytes", free_space, backup_space);
        // check if free space is less 60% than 5 session worth of space.
        if (free_space < backup_space){
            // set free_space less than backup_space to true.
            add_file_normal(CIRC_BUFF_ctx->db_handle, "DUMMY_FILE_FOR_CLEANUP", 0); // Calling add_file_normal to delete files, passing dummy params.
            LOG_E(TAG, "Free space %lld bytes less than %lld backup space %lld bytes. call add_file_normal", free_space, backup_space);

        }

        circular_buffer_camtype_t camtype = CIRCULAR_BUFFER_CAM_FRONT;

        string camtype_str = cam_type_to_string(camtype);

        rc = get_num_hq_videos_db(&num_hq_videos_db, camtype); 

        if (rc == false){
            LOG_E(TAG, "Failed to get number of hq videos in db in transcode thread");
            LOG_E(TAG, "Trying again after some delay");
            continue;
        }
        if(dms_camera_enabled == true) {   // Not required for krait and BGR2
            rc = get_num_hq_videos_db(&num_hq_dms_videos_db, CIRCULAR_BUFFER_CAM_DMS );
        if(store_dms_file == false) {   // DMS file rotation. Deletion of HQ and LQ DMS camera file.
            if (num_hq_dms_videos_db <= NUM_HQ_DMS_VIDEOS_MAX){
                LOG_D(TAG, "Num DMS files in db %lld, NUM_HQ_DMS_VIDEOS_MAX = %lld, %lld ", num_hq_dms_videos_db, NUM_HQ_DMS_VIDEOS_MAX, num_hq_videos_db );
            }
            if (num_hq_dms_videos_db > NUM_HQ_DMS_VIDEOS_MAX){
                LOG_I(TAG, "Num DMS files in db %lld, NUM_HQ_DMS_VIDEOS_MAX = %lld", num_hq_dms_videos_db, NUM_HQ_DMS_VIDEOS_MAX);
                circular_buffer_file_t inp_transcode_fileinfo;
                rc = get_file_to_transcode(&inp_transcode_fileinfo, CIRCULAR_BUFFER_CAM_DMS);
                LOG_I(TAG, "Running transcode for dms file %s", inp_transcode_fileinfo.base_file_name);
                string ld_file_name = inp_transcode_fileinfo.base_file_name + ld_extn;
                string cb_ld_file_path = nd_device_obj->get_external_eMMC_mount_path() + ld_file_name;
                if (inp_transcode_fileinfo.file_compression == CIRCULAR_BUFFER_NO_COMPRESSION)
                {
                    rc = update_tc_status_DB(inp_transcode_fileinfo.index_id, CIRCULAR_BUFFER_TC_STATUS_TRANSCODED);
                    if (rc == false){
                        LOG_E(TAG, "Failed to update file tc status to TRANSCODED for %s having NO_COMPRESSION type", inp_transcode_fileinfo.base_file_name);
                    }
                    if(file_is_present(cb_ld_file_path)) {
                        hard_delete_entry_from_db(ld_file_name);
                        bool del_status = delete_file_from_sdcard(inp_transcode_fileinfo.base_file_name + ld_extn);
                        LOG_I(TAG, "DMS Camera Alert LD file %s deletion status: %d", inp_transcode_fileinfo.base_file_name, del_status);
                    }
                }
                if (inp_transcode_fileinfo.file_compression == CIRCULAR_BUFFER_NO_COMPRESSION_ADJ)
                {
                    rc = update_tc_status_DB(inp_transcode_fileinfo.index_id, CIRCULAR_BUFFER_TC_STATUS_TRANSCODED);
                    if (rc == false){
                        LOG_E(TAG, "Failed to update file tc status to TRANSCODED for %s having NO_COMPRESSION type", inp_transcode_fileinfo.base_file_name);
                    }
                    if(file_is_present(cb_ld_file_path)) {
                        hard_delete_entry_from_db(ld_file_name);
                        bool del_status = delete_file_from_sdcard(inp_transcode_fileinfo.base_file_name + ld_extn);
                        LOG_I(TAG, "DMS Camera adjecent to Alert, LD file %s deletion status: %d", inp_transcode_fileinfo.base_file_name, del_status);
                    }
                }
                bool transcode_status = true;
                update_file_status(inp_transcode_fileinfo, transcode_status);
                string file_compression_str = file_compression_to_string(inp_transcode_fileinfo.file_compression);
                string transcode_status_str = (transcode_status) ? "Success" : "Failure";
                tc_time_end = get_system_time();
                string hs_session(inp_transcode_fileinfo.base_file_name);
                LOG_I(TAG, "Transcode %s : %s, fc = %s, time = %lldms", transcode_status_str.c_str(),
                    inp_transcode_fileinfo.base_file_name, file_compression_str.c_str(), (tc_time_end - tc_time_start));
            }
        }
	else if(store_dms_file == true) { 
    	    monitor_and_delete_dms_file();
    	    if(num_hq_dms_videos_db > NUM_HQ_DMS_VIDEOS_MAX+1) {
		    monitor_and_delete_dms_file();
	    }

	}
        }
        num_hq_videos_max = NUM_HQ_VIDEOS_MAX;
        if (num_hq_videos_db < num_hq_videos_max){
            LOG_I(TAG, "Num HQ %s files in db %ld, NUM_HQ_VIDEOS_MAX = %lld", camtype_str.c_str(), num_hq_videos_db, num_hq_videos_max);
        }

        if (num_hq_videos_db > NUM_HQ_VIDEOS_MAX){
            tc_sleep_duration = TRANSCODE_DELAY;
            LOG_I(TAG, "Num HQ %s files in db %ld, NUM_HQ_VIDEOS_MAX = %lld", camtype_str.c_str(), num_hq_videos_db, num_hq_videos_max);
            if ( (num_hq_videos_db > NUM_HQ_VIDEOS_MAX + tc_and_cp_file_queue_size) || (num_hq_dms_videos_db > NUM_HQ_DMS_VIDEOS_MAX + tc_and_cp_file_queue_size) ){
                tc_sleep_duration = 2;
            }
            tc_time_start = get_system_time();
            //Get fileinfo of the oldest FRONT camera HQ video in db
            circular_buffer_file_t inp_transcode_fileinfo;
            rc = get_file_to_transcode(&inp_transcode_fileinfo, camtype);

            if ( !rc ){
                //rc is false
                LOG_E(TAG, "Failed to get oldest video file to transcode");
                LOG_E(TAG, "Trying again after some delay"); //TODO - thread can be stuck in a loop
                continue; // Try again from the start of the while loop
            }


	    if(strstr(inp_transcode_fileinfo.base_file_name, dpq_extn.c_str() ) == NULL ){
            if (INWARD_TC_ENABLE){
                if (!checked_for_inward_video){
                    //check if corresponding inward video exists for the front camera video obtained
                    circular_buffer_file_t inward_transcode_fileinfo;
                    if (is_inward_available(inward_transcode_fileinfo, inp_transcode_fileinfo, CIRCULAR_BUFFER_CAM_DRIVER)){
                        //Run transcode on inward
                        inp_transcode_fileinfo = inward_transcode_fileinfo;
                        checked_for_inward_video = true;
                    }
                }else{
                    //already checked for inward video. Run transcoding for outward this cycle
                    //and toggle checked_for_inward_video to check for inward video again in the next cycle
                    checked_for_inward_video = false;
                }
	    }
	    }
        if(dms_camera_enabled == true) {   // Not required for krait and BGR2
	    if(store_dms_file == true && store_lq_dms_file == true && inp_transcode_fileinfo.base_file_name[0] == '0') {  
		    circular_buffer_file_t temp_transcode_fileinfo = inp_transcode_fileinfo;
		    temp_transcode_fileinfo.base_file_name[0] = '8';
		    string file_name = temp_transcode_fileinfo.base_file_name;
		    if (inp_transcode_fileinfo.file_compression == CIRCULAR_BUFFER_NO_COMPRESSION){ 
			    LOG_I(TAG,"%s has file_compression_type CIRCULAR_BUFFER_NO_COMPRESSION, not calling monitor_and_delete_dms_file() ", inp_transcode_fileinfo.base_file_name);
		    }
		    else   {   
			    monitor_and_delete_dms_file(file_name + ld_extn);  // Not needed, but keep it as a backup
		    }
	    }
	}
	    LOG_I(TAG, "Running transcode for %s", inp_transcode_fileinfo.base_file_name);

            if (inp_transcode_fileinfo.file_compression == CIRCULAR_BUFFER_NO_COMPRESSION || inp_transcode_fileinfo.file_compression == CIRCULAR_BUFFER_NO_COMPRESSION_NO_LQ)
            {
                LOG_I(TAG,"%s has file_compression_type CIRCULAR_BUFFER_NO_COMPRESSION. Ignore transocding", inp_transcode_fileinfo.base_file_name);
                rc = update_tc_status_DB(inp_transcode_fileinfo.index_id, CIRCULAR_BUFFER_TC_STATUS_TRANSCODED);
                if (rc == false){
                    LOG_E(TAG, "Failed to update file tc status to TRANSCODED for %s having NO_COMPRESSION type", inp_transcode_fileinfo.base_file_name);
                }
                continue;
            }

            rc = update_tc_status_DB(inp_transcode_fileinfo.index_id, CIRCULAR_BUFFER_TC_STATUS_TRANSCODING);
            if ( !rc ){
                //rc == false
                LOG_E(TAG, "Failed to set file %s status to transcoding in DB", inp_transcode_fileinfo.base_file_name);
                continue; // Try again from the start of the while loop
            }

            if ((inp_transcode_fileinfo.camtype >= NUM_CAMERAS_MAX) ||
                    (inp_transcode_fileinfo.file_compression > NUM_COMPRESSION_SCHEMES))
            {
                LOG_E(TAG, " CAMTYPE( %d >= %d ) or FILE_COMPRESSSION (%d >= %d). Invalid info for %s in db", inp_transcode_fileinfo.camtype, NUM_CAMERAS_MAX,
                        inp_transcode_fileinfo.file_compression, NUM_COMPRESSION_SCHEMES, inp_transcode_fileinfo.base_file_name);
                LOG_E(TAG, "Aborting transcode for %s", inp_transcode_fileinfo.base_file_name);
                rc = update_tc_status_DB(inp_transcode_fileinfo.index_id, CIRCULAR_BUFFER_TC_STATUS_TRANSCODE_FAILED);
                if (rc == false){
                    LOG_E(TAG, "Failed to update file tc status to TRANSCODE_FAILED for invalid fileinfo");
                }
                continue;
            }

            // This is the actual place of modifying the DB and deleting files
            int status;
            bool transcode_status = true;
            update_file_status(inp_transcode_fileinfo, transcode_status);
                string file_compression_str = file_compression_to_string(inp_transcode_fileinfo.file_compression);
                string transcode_status_str = (transcode_status) ? "Success" : "Failure";
                tc_time_end = get_system_time();
                string hs_session(inp_transcode_fileinfo.base_file_name);
                LOG_I(TAG, "Transcode %s : %s, fc = %s, time = %lldms", transcode_status_str.c_str(),
                        inp_transcode_fileinfo.base_file_name, file_compression_str.c_str(), (tc_time_end - tc_time_start));
        }
    }

    return 0;
}
