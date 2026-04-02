#include <utils.h>

extern int curr_speed;
bool rgbLossAlertFlags[4];
static string VIDEO_STREAM_MSG_ID = "1006";
extern int64_t UTC_TO_PST_TZ_CONV_MS;
#define TAG "SYSTEM_UTILS"

mutex mlock;

using namespace std;
using namespace cv;

bool raise_alert( err_code_t err_code,  int err_code_aux, string err_msg );
bool query_mdvr_filename(int64_t start_time, int64_t end_time, int ch_num,
                         vector<string> &vec, int &resp_duration_secs);
string msg_body_file_download(const string& filename);
string get_message(const string& msg_id, const string& msg_body);
bool tcp_client_connect(int &sock);
bool tcp_client_send_data(int sock, string data);
void close_socket(int sock);
void video_properties(string video_filename);
bool header_remove_and_decode(string filename, bool audio_enable);
bool convert_file(const string& video_src, string dest, int64_t start_time, int framerate,
                bool audio_enable, const string& audio_src, int duration_secs, int resp_duration_secs, bool &duration_check);
bool tcp_client_receive(int sock, bool stream, string filename, string &reply);
extern int cam_status_array[4];
static bool report_black_videos[4];

typedef struct {
    int  count = 0;
} rgbData;

struct {
    rgbData black;
    rgbData grey;
    rgbData white;
    rgbData rgbLoss;
    rgbData distorted;
}rbgAlertReportData [4];

Vec3b most_common_used_color(Mat img) {
    int width = img.cols;
    int height = img.rows;

    int r_total = 0;
    int g_total = 0;
    int b_total = 0;
    int count = 0;

    int left_border = 0.25 * width;
    int right_border = 0.75 * width;
    int bottom_border = 0.25 * height;
    int top_border = 0.75 * height;

    LOG_D(TAG,"Starting Pixel Analysis");
    for (int x = left_border; x < right_border; x++) {
        for (int y = bottom_border; y < top_border; y++) {
            Vec3b intensity = img.at<Vec3b>(y, x);
            int b = static_cast<int>(intensity.val[0]);
            int g = static_cast<int>(intensity.val[1]);
            int r = static_cast<int>(intensity.val[2]);

            r_total += r;
            g_total += g;
            b_total += b;

            count++;
        }
    }

    int avg_r = 0;
    int avg_g = 0;
    int avg_b = 0;

    if(count != 0)
    {
        avg_r = r_total / count;
        avg_g = g_total / count;
        avg_b = b_total / count;
    }
    else
    {
        LOG_E(TAG,"Error In Processing RGB Values/No Pixels Found, Pixel Count Is %d",count);
    }

    LOG_D(TAG,"Ending Pixel Analysis At");
    return Vec3b(avg_b, avg_g, avg_r);
}

bool check_for_cam_error(string ext_cam_file_name, int ch_num)
{
    if(get_file_size(ext_cam_file_name) < DHUB_BIN_FILE_MIN_LIMIT)
    {
        stringstream message;
        string timestamp;
        if(! get_timestamp_from_filename(ext_cam_file_name, timestamp))
        {
            LOG_E(TAG, "Failed to get timestamp from filename: %s", ext_cam_file_name.c_str());
            timestamp = "-1";
        }
        int cam_num = ch_num + CAM_NO_TO_CH_NUM;
        message << " Cam " << cam_num << " - Black Video @" << timestamp;
        LOG_E(TAG, "Black Video: %s", message.str().c_str());
        raise_alert(SM_E_EXT_CAM_VIDEO_BLACK, cam_num, message.str());
        return true;
    }
    return false;
}

void get_rgb_status(const std::string& file_name,
                    int ch_num,
                    rgb_err& rgb_status,
                    bool update_rgb_health)
{
    // Skip file validation for new firmware in normal video mode
    if (update_rgb_health && isNewFirmwareSupported())
    {
        LOG_D(TAG,
              "New firmware API is used to get RGB status, skipping file size check: %s",
              file_name.c_str());
        return;
    }
 
    // Common camera error check
    if (check_for_cam_error(file_name, ch_num))
    {
        rgb_status = BLACK_VIDEO;
 
        // Update health array only for normal video mode
        if (update_rgb_health)
        {
            cam_status_array[ch_num - 1] = BLACK_VIDEO;
        }
    }
}

//function which  checks whether video is black ,grey,white,rgbloss or normal
rgb_err video_rgb_analyser(string mp4_filename, int ch_num)
{
    int cam_num = ch_num + 3;
    rgb_err rgb_status = UNKNOWN_ERROR_VIDEO;
    //cam_status_array[ch_num - 1] = UNKNOWN_ERROR_VIDEO;  // Unknown Error Video

    LOG_I(TAG,"mp4 filename for rgb detection is %s",mp4_filename.c_str());
	// path to video file
	string video_file = mp4_filename.c_str();

	// path to output directory
#ifdef KRAIT
    string output_dir = "/data/nd_files/data_frames_" + to_string(cam_num) + "/";
#else
	string output_dir = "/home/iriscli/data_frames_" + to_string(cam_num) + "/";
#endif

    mkdir(output_dir.c_str(),0777);

    //To lock the folder by mutex lock
	//ffmpeg command to extract one frame per second from the video
	string extract_command = "ffmpeg -i " + video_file +" -r 1 " + output_dir + "frame%d.jpg";

	int frame_count = 0;

    vector <string> v;
	// execute command using system() functioni
    string response;

    mlock.lock();
	bool status = system_execute_with_resp("EXT_CAM", extract_command, response);
    mlock.unlock();

	// check if extraction command executed successfully
    if (status)
    {
        LOG_D(TAG,"Frames extracted successfully!");

        ifstream infile(output_dir + "frame1.jpg");
        while(infile.good())
        {
            frame_count++;
            infile.close();
            infile.open(output_dir + "frame"+to_string(frame_count+1)+".jpg");
        }
    }
    else
    {
        LOG_E(TAG,"Failed to extract frames");
    }

    LOG_D(TAG,"Total frames :%d",frame_count);
    if(frame_count == 0)
    {
        LOG_E(TAG,"Total Number Of Frames Is 0, Not Analysing RGB");
        cam_status_array[ch_num - 1] = FRAME_COUNT_ERROR;  // Frame Count Error
        return FRAME_COUNT_ERROR;
    }
    srand(time(0));

    //reading the frames

    int final_frames;
    final_frames=(frame_count>15)?15:frame_count;

    bool frame_flag=(frame_count>15)?true:false;
    int i=0;
    while(i<final_frames)
    {
        int random_frame;
        if(frame_flag)
            random_frame=(rand()%frame_count)+1;
        else
            random_frame=i+1;

        string fr = output_dir + "/frame"+to_string(random_frame)+".jpg";
        i++;


        Mat img = imread(fr, IMREAD_COLOR);
        if (img.empty())
        {
            LOG_E(TAG,"Error: Unable to read image: ");
            //Unlocking the mutex lock
            mlock.unlock();

            cam_status_array[ch_num - 1] = IMG_READ_ERROR;  // Image Read Error
            return IMG_READ_ERROR;
        }

        Mat curr_frame = imread(fr, IMREAD_COLOR);

        Vec3b rgb_values=most_common_used_color(curr_frame);

        int r,g,b;
        r = static_cast<int>(rgb_values[2]);
        g = static_cast<int>(rgb_values[1]);
        b = static_cast<int>(rgb_values[0]);
        LOG_I(TAG,"RGB values %d %d %d of %s ",(int)rgb_values[2],(int)rgb_values[1],(int)rgb_values[0],mp4_filename.c_str());


        bool flag=false;
        int black_rgb_val=0;
        int grey_rgb_val=0;
        int white_rgb_val=0;
        int rgb_rgb_val=0;
        bool rgb_flag=false;

        if(r==g &&  g==b)
            rgb_flag=true;

        if(abs(r-g)<4 && abs(g-b)<4)
            flag=true;


        if(r>50 && r<220 && g>50 && g<220 && b>50 && b<220 && rgb_flag)
        {
            v.push_back("RGB_Loss");
        }
        else if(r<40 && g<40 && b<40 && flag)
        {
            LOG_D(TAG,"Black frame : ");
            v.push_back("Black");

        }
        else if(r>225 && g>225 && b>225 && flag)
        {
            LOG_D(TAG,"White frame : ");
            v.push_back("White");
        }

        else if(r>105 && r<220 && g>105 && g<220 && b>105 && b<220 && flag)
        {
            LOG_D(TAG,"Grey frame : ");
            v.push_back("Grey");
        }
        else
        {
            LOG_D(TAG,"RGB frame ");
            v.push_back("RGB");
        }
    }


    int black_frames=0;
    int white_frames=0;
    int grey_frames=0;
    int rgb_frames=0;
    int rgb_loss_frames=0;

    for(auto & it :v)
    {
        if(it=="Black")
            black_frames++;
        else if(it=="White")
            white_frames++;
        else if(it=="Grey")
            grey_frames++;
        else if(it=="RGB_Loss")
            rgb_loss_frames++;
        else
            rgb_frames++;


    }

    string rm_dir = "rm -rf " + output_dir + "*;sync";

    status = system(rm_dir.c_str());

	if (status == 0)
	{
		LOG_I(TAG,"Directory Deleted Successfully!");
    }
    else
    {
        LOG_I(TAG,"Failed To Delete Directory!");
    }

    string msg;
    if(black_frames==final_frames)
    {
        LOG_I(TAG," %s is black",mp4_filename.c_str());
        cam_status_array[ch_num - 1] = BLACK_VIDEO;  // Black video
        rgb_status = BLACK_VIDEO;
    }

    else if(grey_frames==final_frames)
    {
        LOG_I(TAG," %s is Grey",mp4_filename.c_str());
	cam_status_array[ch_num - 1] = GREY_VIDEO;  // Grey video
        rgb_status = GREY_VIDEO;
    }
    else if(rgb_loss_frames>=(0.9*final_frames))
    {
        LOG_I(TAG, " Channel %d has RGB Loss ", ch_num);
	cam_status_array[ch_num - 1] = RGB_LOSS_VIDEO;  // RGB loss video
        rgb_status = RGB_LOSS_VIDEO;
    }

    else if(rgb_frames>=(0.7*final_frames))
    {
        LOG_I(TAG," %s is Normal ",mp4_filename.c_str());
	cam_status_array[ch_num - 1] = NORMAL_VIDEO;  // Normal video
        rgb_status = NORMAL_VIDEO;
    }
    else if(white_frames==final_frames)
    {
        LOG_I(TAG," %s is White",mp4_filename.c_str());
	cam_status_array[ch_num - 1] = WHITE_VIDEO;  // White video
        rgb_status = WHITE_VIDEO;
    }
    else if(black_frames>=1 && grey_frames>=1 && black_frames!=final_frames && rgb_frames!=final_frames)
    {
        LOG_I(TAG," %s is Distorted ",mp4_filename.c_str());
	cam_status_array[ch_num - 1] = DISTORTED_VIDEO;  // Distorted video
        rgb_status = DISTORTED_VIDEO;
    }
    else
    {
        LOG_I(TAG," %s has some issue ",mp4_filename.c_str());
        cam_status_array[ch_num - 1] = CAM_ERROR;  // Default to other issues
        rgb_status = CAM_ERROR;
    }

   return rgb_status;
}
stream_file_resp_t stream_saved_file(const string filename, const int64_t start_time, int64_t end_time,
        const int ch_num, const int framerate, const bool audio_enable, const bool rgb_analysis, long& pull_time, rgb_err& rgb_status) {
    int sock = -1;
    char stream_tag [] = "STREAMING_FILE";

    if(filename == "" || filename.length() < 5) {
        LOG_E(stream_tag, "Filename to record is not specified or short");
        return RESP_ERROR;
    }

    string file_prefix = filename.substr(0, filename.length()-4);
    string bin_filename = file_prefix + ".bin";

    //remove stale bin files if any
    if(file_is_present(bin_filename)) {
        file_delete(bin_filename);
    }

    if(end_time - start_time > 60000)//60000 milliseconds
    {
        LOG_I(stream_tag,"File To Be Pulled From DHUB Has Duration More Than 60 secs, Reducing It to 60 secs");
        end_time = start_time + 60000;
        LOG_I(stream_tag,"End Time = %lld , Start Time = %lld After Reducing Duration to 60 Seconds");
    }

    vector<string> mdvr_filenames;
    int resp_duration_secs = 0;
    // GMT converted to Local time
    if(query_mdvr_filename(start_time + UTC_TO_PST_TZ_CONV_MS, end_time + UTC_TO_PST_TZ_CONV_MS,
                            ch_num, mdvr_filenames, resp_duration_secs) == false) {
        LOG_D(stream_tag, "Failed to query filename from MDVR");
        return TCP_COMM_ERROR;
    }

    if(mdvr_filenames.empty()) {
        LOG_D(stream_tag, "File query returned empty");
        return FILE_QUERY_FAIL;
    }

    string msg_body = "", msg = "";
    int64_t time_start = get_system_time();
    for( vector<string>::iterator iter= mdvr_filenames.begin(), end = mdvr_filenames.end();
            iter!=end; ++iter ) {
        msg_body = msg_body_file_download(*iter);
        string msg =  get_message(VIDEO_STREAM_MSG_ID, msg_body);
        LOG_D(stream_tag, "Message to download file %s", msg.c_str());

        while(1) {
            //connect to host
            if(tcp_client_connect(sock) == false) {
                return TCP_COMM_ERROR;
            }

            //send some data
            if(tcp_client_send_data(sock, msg) == false) {
                close_socket(sock);
                return TCP_COMM_ERROR;
            }

            string resp = "";
            if(tcp_client_receive(sock, true, bin_filename, resp) == false) {
                LOG_E(stream_tag, "failed to receive video stream from mdvr");
                close_socket(sock);
                return TCP_COMM_ERROR;
            }

            int fd = open(bin_filename.c_str(), O_WRONLY);
            fsync(fd);
            close(fd);
            sleep(1);
            long fileSize = file_size(bin_filename);

            if(fileSize <= 0) {
                LOG_E(stream_tag, "Streamed file empty, Filename - %s, Size - %ld",bin_filename.c_str(),fileSize);
                close_socket(sock);
                file_delete(bin_filename);
                return FILE_EMPTY;
            }
            else
            {
                // TO Do
                close_socket(sock);
            }
            break;
        }
    }
    
    if(rgb_analysis)
    {
        video_properties(bin_filename);
    }
    int64_t time_end = get_system_time();
    pull_time = (time_end - time_start)/1000;
    long fileSize = file_size(bin_filename);
    LOG_I(stream_tag, "Time taken to download %s file of size %ld : %ld secs", bin_filename.c_str(), fileSize, pull_time);
    string video_filename = file_prefix + ".264";
    string audio_filename = file_prefix + ".wav";

    bool header_removed = header_remove_and_decode(bin_filename, audio_enable);

    //delete streamed file once header removal is attempted.
    file_delete(bin_filename);

    // In success case, deletion will be taken care by header removal function
    if(header_removed == false) {
        LOG_E(stream_tag, "failed to remove header data");
        // delete stale wav and 264 files
        file_delete(video_filename);
        file_delete(audio_filename);
        return FILE_QUERY_FAIL;
    }

    bool duration_check = true;
    LOG_I(stream_tag, "mp4 filename: %s", filename.c_str());
    if(convert_file(video_filename, filename, start_time, framerate, audio_enable,
                    audio_filename, (end_time-start_time)/1000, resp_duration_secs,
                    duration_check) == false) {
        LOG_E(stream_tag, "failed to convert files to mp4");
        return FILE_QUERY_FAIL;
    }

    int mp4_file_size = get_file_size(filename);
    if( mp4_file_size > DHUB_BIN_FILE_25_MB_LIMIT) 
    {
        LOG_E(stream_tag, "Large Video File Streamed From DHUB, Filename - %s, Size - %ld", filename.c_str(), mp4_file_size);

        stringstream msg;

        msg << " Cam " << (ch_num + 3) << " - Large file @" << start_time << " FileSize - " << round(mp4_file_size / (1024.0 * 1024.0) * 100) / 100 << " MB";

        raise_alert(SM_E_EXT_CAM_LARGE_FILE, curr_speed, msg.str());
        if(mp4_file_size > DHUB_BIN_FILE_MAX_LIMIT )
        {
            return FILE_TOO_BIG;
        }
    }

    // Perform file size check for old firmware (new firmware uses API monitoring)
    get_rgb_status(filename, ch_num, rgb_status, rgb_analysis);

    if(rgb_analysis)
    {
        return RGB_ANALYSIS_DONE;
    }

    if(duration_check == false) {
        LOG_I(stream_tag, "file duration check failed. caller can retry");
        return FILE_DURATION_CHECK_FAIL;
    }

    return FILE_DOWNLOAD_SUCCESS;
}
