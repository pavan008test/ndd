/*
 * nd_log_trim.cpp
 * 
 * Standalone implementation of logging functions for doop binary
 * This file contains copied logging function definitions from nd_core_utils/cpp/src/nd_log.cpp
 * to make doop binary standalone  as much as possible.
 * // The TAG used at the time is "6.12"
 * 
 * There is no corresponding header file, because doop.cpp gets logging function declarations 
 * through the log.h header (via nd_auth_openssl.h), but the actual function implementations 
 * are provided by nd_log_trim.cpp during linking
 * 
 */

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <inttypes.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <pwd.h>
#include <stdarg.h>

#include <string>
#include <sstream>
#include <iostream>
#include <unordered_map>

using namespace std;

// Log level enumeration - copied from nd_core_utils/cpp/inc/log.h
typedef enum log_level_{
    LOG_LEVEL_D = 0,
    LOG_LEVEL_I,
    LOG_LEVEL_W,
    LOG_LEVEL_E,
    LOG_LEVEL_C
}log_level_t;

#define LOGS_PER_FILE 3000
#define DURATION_OF_ROUTING_LOG 30*60*1000  // 30 minutes in milliseconds

static const char* non_privilaged_user =  "ubuntu";
static log_level_t logger_level_setting;
static bool log_analysis_enabled = false;


static int64_t previous_route_log_time = 0;
static int count_for_routing = 0;
static int const max_duration_route_logs = DURATION_OF_ROUTING_LOG;
static int const max_log_count_per_file = LOGS_PER_FILE;
static int const call_route_every_nth_log = 100 ;

static string log_dir_str ="";
typedef pair<uint32_t, uint32_t> log_freq_t;
static int max_log_code_per_unit_time  = 500;
static uint32_t const unit_time_for_log_freq_in_sec  = 1;


/*
 * Helper function to get current thread ID
 * Copied from: nd_core_utils/cpp/src/nd_log.cpp (line 131)
 */
static int gettid() {
    return syscall(SYS_gettid);
}



pthread_mutex_t log_handle_mutex = PTHREAD_MUTEX_INITIALIZER;

static int64_t logger_start_time;

// Forward declaration
bool route_logs(const char* log_dir);

/*
 * nd_route_logs - Automatically route logs based on time or count
 * Copied from: nd_core_utils/cpp/src/nd_log.cpp (line 440)
 * Simplified version for single logger without critical log support
 */
void nd_route_logs(int64_t curr_time)
{
    if(log_dir_str == "")
    {
	return ;
    }
    if( curr_time - previous_route_log_time  > max_duration_route_logs)
    {
	cout<< "***  Automatic Routing  *** curr_time: " << curr_time << " previous_route_log_time: "<< previous_route_log_time << " max_duration_route_logs: "<< max_duration_route_logs << endl;
	route_logs(log_dir_str.c_str());
    }
    else if(count_for_routing >  max_log_count_per_file)
    {
	cout<< "***  Automatic Routing  *** count_for_routing: " <<  count_for_routing<< " max_log_count_per_file: "<< max_log_count_per_file << endl;
	route_logs(log_dir_str.c_str());
    }

}

/*
 * route_logs - Route stdout to a timestamped log file
 * Copied from: nd_core_utils/cpp/src/nd_log.cpp (line 387)
 * Simplified version without critical log support and NdLogger class
 */
bool route_logs(const char* log_dir)
{
    //read_log_analysis_from_bagheera_config();
    pthread_mutex_lock(&log_handle_mutex);
    string logfile_name;
    stringstream ss;
    struct timeval tv;
    struct timezone tz;
    gettimeofday(&tv ,&tz);
    uint64_t time = ((uint64_t)tv.tv_sec * 1000) ;

    ss << log_dir << "/log_" << time << ".log" ;

    if(log_dir_str == "")
    {
	log_dir_str = log_dir;
    }
    count_for_routing = 0 ;
    previous_route_log_time = time;

    logfile_name = ss.str();
    printf("Routing logs to file %s \n", logfile_name.c_str());

    FILE *fp = freopen(logfile_name.c_str(), "a", stdout);
    if (fp == NULL)
    {
        printf("freopen of cout failed,errno %d\n",errno);
    }
    else
    {
        struct passwd *pwd = getpwnam (non_privilaged_user);
        if (pwd == NULL)
        {
            printf ("getpwnam failed with errno %d\n",errno);
        }
        else
        {
            uid_t uid = pwd->pw_uid;
            gid_t gid = pwd->pw_gid;
            int fd = fileno(fp);
            if (fchown (fd, uid, gid) <0)
            {
                printf("Could not change owner of %s\n",logfile_name.c_str());
            }
        }
    }
    pthread_mutex_unlock(&log_handle_mutex);
    return false;
}

//#define NEW_TIME_FORMAT

/*
 * common_print - Print formatted log messages with level, timestamp and frequency control
 * Copied from: nd_core_utils/cpp/src/nd_log.cpp (line 465)
 * Simplified version without critical log support and timeout-based mutex locking
 */
extern "C" void common_print(log_level_t level, const char *tag, const char* func, unsigned int line, const char *fmt_string, ...)
{

    static unordered_map<uint32_t, log_freq_t> map_logCode_freq;
    if(level < logger_level_setting)
    {
        return;
    }  
    va_list args;
    va_start(args, fmt_string);
    struct timeval tv;
    struct timezone tz;
    char buffer[256];
    
    gettimeofday(&tv ,&tz);
    
    uint64_t time = ((uint64_t)tv.tv_sec * 1000) + ((uint64_t)tv.tv_usec/1000);
    int64_t uptime = time - logger_start_time;
    static std::hash<std::string> str_hash;
    line = (str_hash(func)%10000) * 100000 + line;
    uint32_t current_time_in_sec = (uint64_t)tv.tv_sec;    
    // acquire lock
    pthread_mutex_lock(&log_handle_mutex);
    if( current_time_in_sec >= map_logCode_freq[line].second + unit_time_for_log_freq_in_sec ){
        map_logCode_freq[line].first = 0 ;
        map_logCode_freq[line].second = current_time_in_sec ;
    }
    map_logCode_freq[line].first++;
    if(max_log_code_per_unit_time < map_logCode_freq[line].first ){
        if(map_logCode_freq[line].first - max_log_code_per_unit_time == 1 ){
            vsnprintf(buffer, 255, fmt_string, args);
            printf("%lld: %lld: E: %s : %d : %d : ", time, uptime, tag, getpid(), gettid());
            printf("max limit of logs exceeded for [%s] \n", buffer);
        }
        pthread_mutex_unlock(&log_handle_mutex);
        return ;
    }

    const char* level_str;

    switch( level ) {
        case LOG_LEVEL_D:
            level_str = "D";
            break;

        case LOG_LEVEL_I:
            level_str = "I";
            break;

        case LOG_LEVEL_W:
            level_str = "W";
            break;

        case LOG_LEVEL_E:
            level_str = "E";
            break;

        case LOG_LEVEL_C:
            level_str = "C";
            break;

        default:
            pthread_mutex_unlock(&log_handle_mutex);
            return;
    }



    printf("%lld: %lld: %s: %s : %d : %d : ", time, uptime, tag, level_str, getpid(), gettid());
    vsnprintf(buffer, 255, fmt_string, args);

    if(log_analysis_enabled)
    {    
        printf("%u: %s", line, buffer);
    }
    else
    {
        printf("%s", buffer);
    }
    printf("\n");
    fflush(stdout);
    // release lock
    pthread_mutex_unlock(&log_handle_mutex);

    va_end(args);
    ++count_for_routing;  
    if(0 == count_for_routing % call_route_every_nth_log )
    {
        nd_route_logs(time);
    }
}
