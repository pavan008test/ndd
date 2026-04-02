#include <jansson.h>
#include "service_utils.h"

//void fill_map_add_file_healthstats(string session, string category, int status, int64_t starttime, int64_t endtime);
void fill_map_del_file_healthstats(string session, string category, int status, int64_t starttime, int64_t endtime);
void fill_map_add_file_healthstats(string session, string category, int status);
void fill_map_transcode_file_healthstats(string session, string category, int status, int64_t starttime, int64_t endtime);
void update_udid_sessionCount_from_filename( circular_buffer_fileinfo_t &file_info);
void update_udid_sessionCount_from_filename( circular_buffer_fileinfo_t &file_info);
