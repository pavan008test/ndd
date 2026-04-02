#include <opencv2/opencv.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/core.hpp>
#include <stdlib.h>
#include <string>
#include <nd_ext_cam_utils.h>
#include <log.h>
#include <nd_time.h>
#include <config_parser.h>
#include <jansson/jansson.h>
#include <regex>
#include <system_utils.h>
#include <nd_msg_types.h>
#include <cstdlib>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <mutex>

// DHUB Video Chipset Status Constants
constexpr int DHUB_STATUS_BLACK_VIDEO = 0;
constexpr int DHUB_STATUS_NORMAL_VIDEO = 1;

void get_rgb_status(const std::string& file_name, int ch_num, rgb_err& rgb_status, bool update_rgb_health);
rgb_err video_rgb_analyser(string , int);

