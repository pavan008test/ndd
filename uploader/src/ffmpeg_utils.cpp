#include <cstdio>
#include <string>
#include <log.h>
#include <sstream>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <nd_time.h>
#include <iomanip>
#include <fstream>
#include <system_utils.h>
#include <nd_file_utils.h>
#include <nd_task.h>
#include "ffmpeg_utils.h"

#define TAG "FFMP"

using namespace std;

#if defined(KRAIT)
static const string FFMPEG_VERSION = "3.3.3";
#endif

#ifdef BAGHEERA2
static const string FFMPEG_VERSION = "3.4.8-0ubuntu0.2";
#endif

#ifdef BAGHEERA
static const string FFMPEG_VERSION = "2.8.11-0ubuntu0.16.04.1";
#endif

static const string ffmpeg_version_cmd = "ffmpeg -version | head -1 | awk '{print $3}'";

static const int ffmpeg_convert_fail_retry_sleep_time = 30;

static bool check_version_support (string ver)
{
    char buffer [256] = {'\0'};
    bool version_support = false;

    FILE *fp = popen (ffmpeg_version_cmd.c_str(), "r");
    if (!fp)
    {
        LOG_E(TAG, "Failed to execute %s", ffmpeg_version_cmd.c_str());
        return false;
    }
    while (fgets (buffer, sizeof (buffer), fp))
    {
        string s = string (buffer);
        if (!s.empty() && s[s.size() - 1] == '\n')
            s.erase(s.size() - 1);
        if (s == ver)
        {
            version_support = true;
        }
        else
        {
            LOG_I (TAG, "Expected version:%s, Available version:%s", ver.c_str(), s.c_str());
        }
    }
    pclose (fp);
    return version_support;
}

static bool get_int_from_string (string s, int &num)
{
    stringstream ss;
    num = -1;

    ss.clear();
    ss.str("");
    //TODO remove unwanted characters

    ss << s;
    ss >> num;

    if (ss.fail())
    {
        return false;
    }

    return true;
}

static bool get_double_from_string (string s, double &d)
{
    stringstream ss;
    ss << s;
    ss >> d;

    if (ss.fail())
    {
        return false;
    }
    return true;
}

static bool get_offset_and_duration (string fname, int &offset, int &duration)
{
    stringstream ss;
    char buffer [100] = {'\0'};

    string cmd = "ffmpeg -i " + fname;
    cmd += " 2>&1 | grep Duration | cut -d, -f1-2 | awk '{print $2$4}'";
    FILE *fp = popen (cmd.c_str(), "r");
    if (!fp)
    {
        LOG_E (TAG, "Failed to execute %s", cmd.c_str());
        return false;
    }

    if (fgets (buffer, sizeof(buffer), fp) == NULL)
    {
        LOG_E (TAG, "No output for command %s", cmd.c_str());
        pclose (fp);
        return false;
    }
    pclose (fp);

    ss << string (buffer);

    //Will be of form 00:0x:xx.xx,xx.xxxx
    string dur;
    string starttime;

    if (!getline (ss, dur, ','))
    {
        LOG_E (TAG, "Couldn't get duration from %s", buffer);
        return false;
    }
    if (!getline (ss, starttime, ','))
    {
        LOG_E (TAG, "Couldn't get stat time from %s", buffer);
        return false;
    }

    ss.clear();
    ss.str("");
    ss << dur;

    int loop = 0;
    int val = 0;
    int int_from_string = 0;
    //Calculate duration from xx:xx:xx.xx
    LOG_I (TAG, "Extracting duration from %s", ss.str().c_str());
    while (getline (ss, dur, ':'))
    {
        if (loop == 0)
        {
            loop++;
            int_from_string = 0;
            if (!get_int_from_string (dur, int_from_string))
            {
                LOG_E (TAG, "get_int_from_string failed for %s", dur.c_str());
                return false;
            }
            val = 3600 * int_from_string;
        }
        else if (loop == 1)
        {
            loop++;
            int_from_string = 0;
            if (!get_int_from_string (dur, int_from_string))
            {
                LOG_E (TAG, "get_int_from_string failed for %s", dur.c_str());
                return false;
            }
            val += 60 * int_from_string;

        }
        else if (loop ==2)
        {
            loop++;
            double d;
            if (!get_double_from_string (dur, d))
            {
                LOG_E (TAG, "Failed to extract double value from %s", dur.c_str());
                return false;
            }
            val += floor (d);
        }
        else
        {
            LOG_E (TAG, "Unexpected token");
            return false;
        }
    }
    duration = val;
    LOG_I (TAG, "Duration is %d", duration);

    LOG_I (TAG, "Extracting offset from %s", starttime.c_str());
    double d;
    if (!get_double_from_string (starttime, d))
    {
        LOG_E (TAG, "Failed to extract double value from %s", starttime.c_str());
        return false;
    }
    offset = round (d);
    LOG_I (TAG, "Offset is %d", offset);
}

static bool trim_and_get_frame_idx (string cmd , int &start_frame_idx, int &end_frame_idx)
{
    int start_idx = -1, end_idx = -1, num_packets_muxed = 0;

    char buffer [256] = {'\0'};

    //Profiling
    int64_t start_time, end_time;

    start_time = get_system_monotonic_time();
    FILE *fp = popen (cmd.c_str(), "r");
    if (!fp)
    {
        LOG_E(TAG, "Failed to execute %s", cmd.c_str());
        return false;
    }

    //For identifying failures
    int line_count = 0;
    while (fgets (buffer, sizeof (buffer), fp))
    {
        line_count++;
        string s = string (buffer);
        if (start_idx < 0)
        {
            if (!get_int_from_string (s, start_idx))
            {
                LOG_E (TAG, "get_int_from_string - start_idx failed for %s", s.c_str());
                pclose (fp);
                return false;
            }
        }
        else if (end_idx < 0)
        {
            if (!get_int_from_string (s, num_packets_muxed))
            {
                LOG_E (TAG, "get_int_from_string - num_packets_muxed failed for %s", s.c_str());
                pclose (fp);
                return false;
            }
            end_idx = start_idx + num_packets_muxed - 1;
        }
        else
        {
            LOG_E(TAG, "Unexpected line: %s", s.c_str());
        }
    }
    pclose (fp);
    end_time = get_system_monotonic_time();
    LOG_I (TAG, "Trimming finished in %lld milliseconds", (end_time - start_time));

    if (line_count == 0)
    {
        LOG_E (TAG, "No output from trim command");
        //Didnt print frame indices. Something went wrong
        return false;
    }
    start_frame_idx = start_idx;
    end_frame_idx = end_idx;

    LOG_I (TAG, "trim_and_get_frame_idx: start_frame_idx=%d, end_frame_idx=%d", start_frame_idx, end_frame_idx);

    if (start_frame_idx < 0 || end_frame_idx < 0)
    {
        LOG_E (TAG, "start_frame_idx and end_frame_idx are negative values. Trimming might have failed");
        return false;
    }
    if (end_frame_idx == 0)
    {
        LOG_E (TAG, "end_frame_idx is zero");
        return false;
    }
    if (start_frame_idx == end_frame_idx)
    {
        LOG_E (TAG, "Strange!! start_frame_idx is same as end_frame_idx");
        return false;
    }
    if (start_frame_idx > end_frame_idx)
    {
        LOG_E (TAG, "Something went wrong. start_frame_idx>end_frame_idx");
        return false;
    }

    return true;
}

static void find_error_in_trimming (string infile, string outfile, int start, int end)
{
    char buffer[256] = {'\0'};

   stringstream cmd;
   cmd <<  "ffmpeg -y -ss " << start << " -t " << end << " -i " << infile << " -c copy -copyts " << outfile;
   cmd << " 2>&1";

   FILE *fp = popen (cmd.str().c_str(), "r");
   while (fgets (buffer, sizeof (buffer), fp))
   {
       LOG_I(TAG, "%s", buffer);
   }
   pclose (fp);
}

bool ffmpeg_trim_video (string inpfname, string outfname, int start_time, int end_time, int &start_frame_idx, int &end_frame_idx, int &offset, int &actual_duration)
{
    if (!check_version_support (FFMPEG_VERSION))
    {
        LOG_E(TAG, "Not a supported version of ffmpeg. Trimming videos might not work");
    }

    int duration = end_time - start_time;
    if (duration < 0)
    {
        LOG_E(TAG, "duration is negative");
        return false;
    }

    struct stat s;
    if (stat (inpfname.c_str(), &s))
    {
        LOG_E (TAG, "%s doesn't exist. Trimming can't be done", inpfname.c_str());
        return false;
    }
    if (s.st_size == 0)
    {
        LOG_E (TAG, "File %s has zero bytes", inpfname.c_str());
        return false;
    }

    stringstream trim_cmd;

    //We use verbose level as trace for getting starting and ending frame information
    //-y option is for over writing existing output file
    trim_cmd << "ffmpeg -y -v trace -ss " << start_time << " -t " << duration << " -i " << inpfname;
    trim_cmd << " -c copy -copyts " << outfname << " 2>&1";

    //filter out the frame info part
    trim_cmd << " | sed -n -e '/Stream mapping/,$p' | grep 'sample\\|packets muxed' | sed -n '1p;$p' | xargs | awk '{print $7\"\\n\"$14}'";
    LOG_I (TAG, "Trim command is %s", trim_cmd.str().c_str());

    if (trim_and_get_frame_idx (trim_cmd.str(), start_frame_idx, end_frame_idx))
    {
        LOG_I(TAG, "Video trimmed successfully. Start Frame Index: %d End Frame Index: %d", start_frame_idx, end_frame_idx);
        if (get_offset_and_duration (outfname, offset, actual_duration))
        {
            LOG_I (TAG, "Offset: %d Duration:%d", offset,actual_duration);
            return true;
        }
        else
        {
            LOG_E (TAG, "Failed to get offset and duration");
            return false;
        }
    }
    else
    {
        LOG_E(TAG, "Failed to trim video");
        find_error_in_trimming (inpfname, outfname, start_time, duration);
        return false;
    }
}

string check_filetype(string filename)
{
    int64_t time_before = 0, time_after = 0;
    string file_check_cmd = "ffprobe -show_format_entry format_name " + filename;
    LOG_I(TAG, "File check cmd: %s", file_check_cmd.c_str());
    string resp = "";
    time_before = get_system_monotonic_time();
    bool res = system_execute_with_resp("CHECK_FILETYPE", file_check_cmd.c_str(), resp);
    if (res == false) {
        LOG_E(TAG, "ffprobe returned failure. ret code: %d", res);
    }
    time_after = get_system_monotonic_time();
    LOG_I(TAG, "File check took %lld ms", (time_after - time_before));
    LOG_I(TAG, "File check response: ret_value: %d, response_string:\n%s", res, resp.c_str());
    return resp;
}

bool convert_file_format(void *args)
{
    struct convertfile_args *local_args = (struct convertfile_args *)args;
    int64_t time_before = 0, time_after = 0;
    string convert_cmd = "";
    LOG_I(TAG, "Camera ID from local args %s is %d", local_args->src.c_str(), local_args->cam_num);
    if ((local_args->cam_num >= DEVICE_CAMERA_POSITION_FRONT && local_args->cam_num <= DEVICE_CAMERA_POSITION_RIGHT) || local_args->cam_num == DEVICE_CAMERA_POSITION_DMS) {
        // because the stream is elementary so we need to change the conversion command
        convert_cmd = "ffmpeg -y -v warning -r " + std::to_string(local_args->framerate) + " -f hevc -i " + local_args->src + " -c:v copy " + local_args->dest + " 2>&1 ";
    } else {
        // this is to handle external camera as stream is not elementary for it
        convert_cmd = " ffmpeg -y -v warning -i " + local_args->src + " -codec copy " + local_args->dest + " 2>&1 ";
    }
    LOG_I(TAG, "Convert cmd: %s", convert_cmd.c_str());
    time_before = get_system_monotonic_time();

    int res = system_execute("MP4_CONVERT", convert_cmd.c_str());
    time_after = get_system_monotonic_time();

    LOG_I(TAG, "Conversion took %lld ms", (time_after - time_before));
    if (res != 0) {
        LOG_E(TAG, "ffmpeg returned failure. ret code: %d", res);
        return false;
    }

    int src_file_size = get_file_size(local_args->src);
    int dest_file_size = get_file_size(local_args->dest);

    if (dest_file_size < src_file_size) {
        int retry_count = 1;
        while (retry_count > 0 && (dest_file_size < src_file_size)) {
            LOG_E(TAG, "suspect ffmpeg conversion failed, need to retry: src file size:%d. dest file size:%d", src_file_size, dest_file_size);
            sleep(ffmpeg_convert_fail_retry_sleep_time);
            file_fd_sync(local_args->src);
            file_fd_sync(local_args->dest);
            time_before = get_system_monotonic_time();
            res = system_execute("MP4_CONVERT", convert_cmd.c_str());
            if (res != 0) {
                LOG_E(TAG, "ffmpeg returned failure. ret code: %d", res);
                return false;
            }
            time_after = get_system_monotonic_time();
            LOG_I(TAG, "Conversion took %lld ms", (time_after - time_before));
            if (res != 0) {
                LOG_E(TAG, "ffmpeg returned failure. ret code: %d", res);
                return false;
            }
            dest_file_size = get_file_size(local_args->dest);
            retry_count--;
        }
    }

    file_fd_sync(local_args->dest);
    return true;
}
