#include <unistd.h>  /* UNIX standard function definitions */
#include <fcntl.h>   /* File control definitions */
#include <termios.h> /* POSIX terminal control definitions */
#include <cstdio>
#include <string.h>
#include <stdio.h>
#include <cstdlib>
#include <unistd.h>
#include <cctype>    /* For isspace and isdigit functions */
#include "system_utils.h"
#include <gps_dev.h>
#include <log.h>
#include "config_parser.h"
#include "agnss.h"
#include <nd_factory.h>

#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <fstream>
#include <nd_time.h>
#include <sys/prctl.h>
#include <signal.h>
#include <poll.h>

#define DECOUPLE_GPS_CAM
#define MAX_ACCURACY_DATA_LEN 60
#define MAX_ACCURACY_THRESHOLD 10
#define DETECT_SIERRA_MODULE "lsusb | grep 1199"
#define DETECT_QUECTEL_MODULE "lsusb | grep 2c7c"
#define GOOD_SNR_THRESHOLD 30     //Good GNSS signal should be considered only when snr is more than 30
#define STATIC_SPEED_THRESHOLD_L3 0.8
#define BAGHEERACONFIG_INI "/home/ubuntu/.nddevice/latest/bagheera_config.ini"
#define SECONDS_IN_24_HOURS (24 * 60 * 60)  // for agnss epo files expiry check
#define EPO_FILES_EXPIRY_CHECK_INTERVAL (6 * 60 * 60) // for agnss epo files periodic check
#define MILLISECOND_TO_SECOND 1000
#define SECONDS_IN_AN_HOUR 3600 
#define SLEEP_TIME_BEFORE_RETRY 5
#define SLEEP_AFTER_WRITEPORT_FAIL 5
#define INVALID_LAT 91.0
#define INVALID_LONG 181.0
#define SERVICE_UPTIME_THRESHOLD 2*60 // 2 minutes

static Gps::gps_callback_t *cb=NULL;
static Gps::gps_pps_callback_t *pps_cb=NULL;
static int handle = 0;

static bool open_port();
static bool close_port();
static bool config_port();
static bool create_thread();
static void *read_thread( void *p );
static void *pps_record_thread (void *);
static void parse(char *buf);
static void parse_lumia3(char *buf);
static bool at_config();
static void *agnss_epo_files_expiry_check(void *); 

int fd = -1;
static int enable_mask = 0;
pthread_t gps_thread;
pthread_t gps_pps_thread;
pthread_t epo_expiry_check_thread; //thread for periodic check of epo files expiry
pthread_t gps_cold_start_thread; //thread for agnss cold start
pthread_mutex_t gps_port_write_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile bool thread_alive=false;

static const int gps_interface = 2;
static const int fnamelen = 128;

double latest_cached_latitude = INVALID_LAT;
double latest_cached_longitude = INVALID_LONG;

static string gps_port = "";
static const int NMEA_LEN = 1024;
static const string gps_restart_log = "/home/ubuntu/.nddevice/log/gps_restart.log";

static bool first_fix = false;
static bool fix_quality_flag = false;
static bool critical_info_fix_quality_flag = false;

//when module gps_pps_interrupt.ko is inserted (just before starting bagheera service), below file is created.
static const string GPS_PPS_FILE = "/sys/gps_pps_sysfs/gps_pps_timestamps";

static bool hdmaps_mode;

char nmea_log_filename[100];

FILE *nmea_log_file = NULL;

string nmea_log_status ="false";

bool nmea_log_val = false;

using namespace std;

extern NDService *nd_service_obj; //nd service object, to detect crashes

#define TAG "GPSD"
#define TAG2 "AGNSS"

static int modem_config = -1;
int64_t gps_first_fix_time = 0;
int64_t gps_port_enumerate_time = 0;
#define MILLISECOND_TO_SECOND 1000
static int module_ttff = -1; 
static bool gps_cold_start_fail = false;
string is_agnss_enabled = "true";
string is_cold_start_enabled = "false";
int no_of_epofiles = 4;
int cold_start_thread_sleep_time = 600; // 600secs = 10 minutes
const string epo_file_download_time_path = "/home/ubuntu/.nddevice/.epo_file_download_time";
static bool epo_files_erase = false;
const string epo_erase_cmd="$PAIR472*3B\r\n";
const string gps_cold_start_cmd = "$PQTMCOLD*1C\r\n";
const string gps_subsys_cold_start_cmd = "$PAIR006*3C\r\n";
const string gps_off_cmd = "/bin/vendor/gpio_test -n 312 -o 1 > /dev/null";
const string gps_on_cmd = "/bin/vendor/gpio_test -n 312 -o 0 > /dev/null";
extern time_t epo_download_time;
extern bool raise_ack_fail_ce();
int64_t service_start_time = 0;
bool is_gps_port_disconnected = false;
int64_t gps_port_disconnected_time = 0;

ND_DeviceFactory *nd_device_obj = NULL;  // nd device object based on deviceType


bool is_sierra_modem()
{
    if (modem_config == 1)
        return true;
    else if (modem_config != -1)
        return false;
    string sierra_module = "1199";
    string resp;
    bool res = system_execute_with_resp(TAG, DETECT_SIERRA_MODULE, resp);
    if (res && strstr (resp.c_str(), sierra_module.c_str()))
    {
        modem_config = 1;
        return true;
    }
    else
        return false;
}

bool is_quectel_modem()
{
       if (modem_config == 2)
           return true;
       else if (modem_config != -1)
           return false;

    string quectel_module = "2c7c";
    string resp;
    bool res = system_execute_with_resp(TAG, DETECT_QUECTEL_MODULE, resp);
    if (res && strstr (resp.c_str(), quectel_module.c_str()))
    {
        modem_config = 2;
        return true;
    }
    else
        return false;
}

int get_gps_interface()
{
    if(is_sierra_modem())
        return 2;
    else if(is_quectel_modem())
        return 0;

    return 2;
}

void log_file(string msg) {
    fstream log_file (gps_restart_log.c_str(), fstream::app|fstream::out);
    if (log_file)
    {
        log_file << get_system_time() << " : " << msg << endl;
    }
    else
    {
        LOG_E (TAG, "Cannot log GPS message");
    }
    log_file.close();

}

vector<string> get_ports() {

        vector<string> ports;
        string resp = "";
        string cmd = "ls /dev/nd-gnss";
        bool res = system_execute_with_resp(TAG, cmd, resp);
        if(!res){
            LOG_I(TAG, "Cannot list /dev/nd-gnss, falling back to /dev/ttyUSB*");
            cmd = "ls /dev/ttyUSB*";
            res = system_execute_with_resp(TAG, cmd, resp);
            if(!res){
                LOG_I(TAG, "Cannot list devices");
                return ports;
            }
        }

        // Split response string by newlines
        stringstream ss(resp);
        string port;
        while (getline(ss, port)) {
            if (!port.empty()) {
                LOG_I(TAG, "Detected port: %s", port.c_str());
                ports.push_back(port);
            }
        }

        return ports;
}

int get_interface(string port) {
        int interface = -1;

        stringstream ss;
        ss << "udevadm info -a -p  $(udevadm info -q path -n " << port << " 2> /dev/null) 2> /dev/null | grep bInterfaceNumber | cut -d'=' -f3 | cut -d'\"' -f2";

        FILE *pipe;
        string cmd = ss.str();
        if( NULL == (pipe = popen(cmd.c_str(),"r") ) ) {
                LOG_E(TAG, "Cannot execute udevadm");
                return -1;
        }

        char buff[fnamelen];
        if (fgets(buff, fnamelen, pipe) == NULL) {
                LOG_E(TAG, "Cannot get interface");
                pclose(pipe);
                return -1;
        }
        pclose(pipe);

        string s(buff);
        s.erase( std::remove(s.begin(), s.end(), '\n'), s.end() );
        char *temp;
        interface = strtol(s.c_str(), &temp, 10);
        if( *temp != '\0' ) {
                LOG_E(TAG, "Cannot get interface, unable to convert to int");
                return -1;
        }

        LOG_I(TAG, "Inteface for port: %s is %d", port.c_str(), interface);

        return interface;

 }

string get_gps_port() {

        //List all ttyUSB port
        vector<string> ports = get_ports();
        int interface = -1;
        
        if (ports.size() == 1 && ports[0] == "/dev/nd-gnss") {
            LOG_I(TAG, "PORT: %s", ports[0].c_str());
            return ports[0];
        }
        else {
            //Go through the list and check for gps_interface
            for( vector<string>::iterator iter = ports.begin(), end = ports.end(); iter!=end; iter++ ) {
            interface = get_interface(*iter);
            LOG_I(TAG, "PORT: %s interface: %d", (*iter).c_str(), interface  );
            if ( get_gps_interface() ==  interface ) {
                    LOG_I(TAG, "Found GPS interface");
                    return *iter;
                }
            }
        }
     
        LOG_E(TAG, "Cannot find GPS interface");
        return "";
}

int gps_dev_open() {
#ifndef DECOUPLE_GPS_CAM
	if( !open_port() || !config_port() ) {
		return -1;
	}
#endif
    LOG_I(TAG, "Opening GPS port");
	return handle++;
}

bool gps_dev_close(int handle) {
    handle--;
    if(thread_alive == true) {
        LOG_I(TAG, "Before gps thread cancel");
        pthread_cancel(gps_thread);
        LOG_I(TAG, "after gps thread cancel");
        thread_alive = false;
        usleep(100 * 1000);
    }
    LOG_I(TAG, "Closing GPS port");
    if (nmea_log_file != NULL) {
        fclose(nmea_log_file);
        nmea_log_file = NULL;
    }
    return close_port();
}

bool gps_dev_config( int handle, string key, string value ) {
	return true;
}

bool gps_dev_reg_cb( int handle, Gps::gps_callback_t *cb ) {
	if( cb == NULL ) {
		return false;
	}

	::cb = cb;
	return true;
}

bool gps_dev_reg_pps_cb( int handle, Gps::gps_pps_callback_t *cb ) {
    if (cb == NULL) {
        return false;
    }
    ::pps_cb = cb;
    return true;
}

bool gps_dev_enable( int handle, bool hdmap_mode_enable ) {
	hdmaps_mode = hdmap_mode_enable;
	enable_mask = 1;
	create_thread();
	return true;
}

bool gps_dev_disable( int handle ){
	enable_mask = 0;
	return true;
}

bool open_port( ) {
        gps_port = get_gps_port();
        LOG_I(TAG, "GPS PORT: %s", gps_port.c_str() );

	fd = open(gps_port.c_str(), O_RDWR);

	if (fd < 0) {
		return false;
	}

	return true;
}

static bool close_port() {
	close(fd);

	return true;
}

static bool config_port() {
	struct termios tty;
	tcgetattr( fd, &tty );

	/* SEt Baud Rate */

    LOG_I(TAG,"entered config_port");
    if(is_sierra_modem())
    {
        cfsetospeed( &tty, B9600 );
        cfsetispeed( &tty, B9600 );
    }
    else if(is_quectel_modem())
    {
        cfsetospeed( &tty, B115200);
        cfsetispeed( &tty, B115200);
    }
 	tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;

	tty.c_iflag =  IGNBRK;

	tty.c_lflag = 0;
	tty.c_oflag = 0;

	tty.c_cflag |= CLOCAL | CREAD;
	//tty.c_cc[VMIN] = 1;
	//tty.c_cc[VTIME] = 5;

	tty.c_iflag &= ~(IXON|IXOFF|IXANY);

	tty.c_cflag &= ~(PARENB | PARODD);

	tcsetattr(fd, TCSANOW, &tty);

 	struct termios sgg;

	tcgetattr(fd, &sgg);
	sgg.c_cflag |= CLOCAL;
	tcsetattr(fd, TCSANOW, &sgg);
	{
	struct termios sgg;

	tcgetattr(fd, &sgg);
	sgg.c_cflag |= HUPCL;
	tcsetattr(fd, TCSANOW, &sgg);
	}
	return true;
}

static bool create_thread() {
	if( thread_alive ) {
		return false;
	}

	if(pthread_create(&gps_thread, NULL, read_thread, NULL)) {
		return false;
	}

#ifdef BAGHEERA2
    if(pthread_create(&gps_pps_thread, NULL, pps_record_thread, NULL)) {
        return false;
    }
#endif
    return true;
}

//Test Data//
//buf = "$GPRMC,181407.000,A,1259.7011,N,07743.5383,E,0.50,153.53,101016,,,A*64";
//buf = "$GPRMC,181407.000,A,1259.7011,N,07743.5383,E,,,101016,,,A*64";
//buf = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";
//buf = "$GPRMC,000000.000,A,1259.7011,N,07743.5383,E,0.50,153.53,101016,,,A*64";
//buf = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";

bool get_checksum( char *buf, int len, int &checksum )
{
    char c;
    int csum = 0;
    int i;
    bool found = false;

    for (i=0;i<len;i++)  {
        c = buf[i];
        switch(c)
        {
            case '$':
                break;
            case '*':
                found = true;
                i = len;
                continue;
            default:
                if (csum == 0) {
                    csum = c;
                }
                else {
                    csum = csum ^ c;
                }
                break;
        }
    }

    checksum = csum;
    return found;
}

bool read_checksum( char *buf, int len, int &checksum ) {
    int i=0;
    bool found=false;
    int csum = 0;

    for( i=0; i<len; i++ ) {
        if( buf[i] == '*' ) {
                found = true;
                break;
        }
    }

    if( found ) {
        char ccsum[3];
        ccsum[0] = buf[i+1];
        ccsum[1] = buf[i+2];
        ccsum[2] = '\0';

        csum = strtol(ccsum, NULL, 16);
    }

    checksum = csum;
    return found;
}

void log_gps_ttf(int64_t gps_port_enumerate_time, int64_t gps_first_fix_time) {

    pid_t pid = fork();
    if (pid == 0)
    {
        LOG_I(TAG, "Got first fix");       
        //Setting child process kill on parent death
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        FILE *pipe;
        vector<string> ports;
        static const int buff_len = 1024;
        
        LOG_I(TAG, "Starting lte_gps_sample_app");
        string cmd = "lte_gps_sample_app \'at!gpsstatus\?\'";
        if( NULL == (pipe = popen(cmd.c_str(),"r") ) ) {
            LOG_E(TAG, "Cannot get gpsstatus");
        }

        char buff[buff_len];
        string result = "";
        while (fgets(buff, buff_len, pipe) != NULL) {
            LOG_I(TAG, "%s", buff);
            result += buff;
        }
        pclose(pipe);
        
        string ttff;
        size_t pos = result.find("TTFF (sec) =");
        if (pos != std::string::npos) {
            pos += 12; // Move past "TTFF (sec) ="
            // Skip any whitespace characters
            while (pos < result.size() && std::isspace(static_cast<unsigned char>(result[pos]))) {
                ++pos;
            }
            // Extract digits
            size_t start = pos;
            while (pos < result.size() && std::isdigit(static_cast<unsigned char>(result[pos]))) {
                ++pos;
            }
            if (start != pos) {
                ttff = result.substr(start, pos - start);
            } else {
                LOG_I(TAG, "TTFF response is Unknown");
                ttff = "Not found";
            }
        } else {
            LOG_I(TAG, "TTFF is not locked");
            ttff = "Not found";
        }

        if(ttff == "Not found")
        {
            LOG_I(TAG, "TTFF is not found. Setting to -1");
            module_ttff = -1;
        }
        else{
            module_ttff = atoi(ttff.c_str());
        } 
        LOG_I(TAG, "Module TTFF: %d", module_ttff);
        string err_msg = "TTFF:" + std::to_string(gps_first_fix_time/MILLISECOND_TO_SECOND) + ", TTFF(excluding enumeration):" + std::to_string((gps_first_fix_time - gps_port_enumerate_time)/MILLISECOND_TO_SECOND)+ " TTFF(Module):" + std::to_string(module_ttff)+ " in sec";
        nd_service_obj->send_err_msg(SM_E_GPS_FIRST_FIX, 0, err_msg );
        LOG_I(TAG, "Finished lte_gps_sample_app");
        _exit (0);
    }
    else if (pid < 0)
    {
        LOG_E (TAG,"Creating child process for logging first fix failed");
    }
}


Gps::gps_data_t data;
Gps::gps_extended_data_t extended_gps;
bool gprmc_status = false;
bool gpgga_status = false;
bool pqxfi_status = false;
bool gpvtg_status = false;
bool pqtmsenmsg_status = false;
int32_t goodSatellites = 0;
float accuracy_list[MAX_ACCURACY_DATA_LEN];
int accuracy_index = 0;
float avg_accuracy_list_in_hour[MAX_ACCURACY_DATA_LEN];
int avg_accuracy_index = 0;
int64_t accuracy_start_timestamp = 0;
atomic<uint64_t> gps_index;
bool parsing_gns_sat_msg = false;

static void update_accuracy_list (float current_accuracy)
{
    float sum = 0, avg = 0;
    if(nmea_log_val)
    {
        LOG_I(TAG,"update_accuracy_list: Accuracy = %lf", current_accuracy);
        LOG_I(TAG,"update_accuracy_list: accuracy_index = %d", accuracy_index);
    }
    if (avg_accuracy_index == 0)
    {
        accuracy_start_timestamp = get_system_time();
    }

    accuracy_list[accuracy_index++] = current_accuracy;

    if (accuracy_index >= MAX_ACCURACY_DATA_LEN)
    {
        for (int i = 0; i < accuracy_index; i++)
            sum += accuracy_list[i];
        avg = sum/accuracy_index;
        avg_accuracy_list_in_hour[avg_accuracy_index++] = avg;
        accuracy_index = 0;
    }
    sum = 0;
    avg = 0;
    if (avg_accuracy_index >= MAX_ACCURACY_DATA_LEN)
    {
        for (int i = 0; i < avg_accuracy_index; i++)
            sum += avg_accuracy_list_in_hour[i];
        avg = sum/avg_accuracy_index;
        if (avg > MAX_ACCURACY_THRESHOLD)
        {
            string err_msg = "Poor GPS Accuracy from " + std::to_string(accuracy_start_timestamp);
            nd_service_obj->send_err_msg(SM_E_NDC_GPS_FAIL, (int)avg, err_msg );
        }
        accuracy_index = 0;
        avg_accuracy_index = 0;
        accuracy_start_timestamp = 0;
    }
}

//bool gpgsa_status = false;
static const int64_t ONE_MICRO_IN_NANO = 1000;

static bool first_pps_pushed = false;

static void do_gps_cb (Gps::gps_data_t data, Gps::gps_extended_data_t extended_gps ) {
    if (!cb) {
        return;
    }

    static bool first_gps_data_pushed = false;
    static Gps::gps_data_t first_gps_to_push;

#if 0
    if(hdmaps_mode == true)
    {
        if (!first_pps_pushed) {
            first_gps_to_push = data;
            LOG_I (TAG, "First PPS is not yet pushed. Return");
            return;
        }
    }

    if (!first_gps_data_pushed) {
        LOG_I (TAG, "Pushing first GPS data. Index: %llu", gps_index);
        cb (first_gps_to_push, gps_index, raw_time_ns);
        first_gps_data_pushed = true;
    }
    //monotonic time is in nanos
    //data.system_monotonic_time = data.system_monotonic_time/ONE_MICRO_IN_NANO;
#endif

    gps_index.fetch_add(1);
    extended_gps.gps_index = gps_index.load();
    extended_gps.gps_data = data;
    cb (data, extended_gps);
}

void do_pps_cb (Gps::gps_pps_data_t pps_data) {

    if (!pps_cb) {
        return;
    }

    static uint64_t pps_index = 0;
    static Gps::gps_pps_data_t first_pps_to_push;
    static int64_t last_valid_pps_ts = 0;
    static bool first = true;

    if (pps_data.pps_raw_time == 0) {
        LOG_E (TAG, "val.pps_raw_time is zero. Returning");
        return;
    }
    if (pps_data.pps_raw_time != last_valid_pps_ts) {
        last_valid_pps_ts = pps_data.pps_raw_time;
        if (first) {
            LOG_I (TAG, "Buffering the first PPS entry");
            first_pps_to_push = pps_data;
            first = false;
            return;
        }
    }
    else {
        if (!first_pps_pushed) {
            LOG_E (TAG, "Same pps timestamp. Reassigning first_pps_to_push");
            first_pps_to_push = pps_data;
            return;
        }
        else {
            LOG_E (TAG, "Same pps timestamp. Setting to zero");
            pps_data.pps_raw_time = 0;
        }
    }
    //pps_data.pps_raw_time is in ns.
    //For HDMAPS, we take timestamps in microseconds
    pps_data.pps_raw_time = pps_data.pps_raw_time/ONE_MICRO_IN_NANO;
    pps_data.pps_clock_time =  pps_data.pps_clock_time/ONE_MICRO_IN_NANO;
    if (!first_pps_pushed) {
        LOG_I (TAG, "Pushing first PPS data. Index: %llu", pps_index);
        first_pps_to_push.pps_index = pps_index;
        first_pps_to_push.pps_raw_time = first_pps_to_push.pps_raw_time / ONE_MICRO_IN_NANO;
        first_pps_to_push.pps_clock_time = first_pps_to_push.pps_clock_time/ONE_MICRO_IN_NANO;
        pps_cb (first_pps_to_push);
        first_pps_pushed = true;
    }
    pps_data.pps_index = ++pps_index;
    pps_cb (pps_data);
}

static void parse(char *buf) {
        if ( strlen(buf) == 0) {
           LOG_D(TAG, "buff len is 0");
           return;
        }

        string input(buf);
        //   Debug Log Utility
        if(nmea_log_val)
        {

            if (nmea_log_file != NULL){

                int nmea_log_ret = fprintf(nmea_log_file, "NMEA: %s\n", input.c_str());
                if (nmea_log_ret < 0) {
                    LOG_E(TAG, "Failed to write to nmea log file");
                }
            } 
            else {
                LOG_I(TAG, "NMEA log file not available (file handle is NULL)\n");
            }
        }
        stringstream ss(input);
        string header;
        string token;
        bool b1=false, b2=false, cstatus = false;
        int csum=0, read_csum = 0;

        std::getline(ss, header, ',');
        static bool log_gps_rollover = true;

    	LOG_D(TAG, "buff :: %s",buf);
        LOG_D(TAG, "HEADER: %s", header.c_str());
        b1 = get_checksum(buf, strlen(buf), csum);
        b2 = read_checksum(buf, strlen(buf), read_csum);

        if( b1 && b2 && (csum == read_csum) ) {
                cstatus = true;
        } else {
                cstatus = false;
                LOG_E(TAG, "Checksum failed for HEADER: %s", header.c_str());
        }

	if(cstatus && (header.find("GPRMC") != string::npos)) {
                int dec;
                float frac;
                double lat = 0;
                double lon = 0;
                char ns = 'I';
                char ew = 'I';
                char av='V';
		double speed=0;
                int date = 0;
                uint64_t raw_time_ns = get_system_monotonic_time_ns();
                goodSatellites = 0;
                gprmc_status = cstatus;

                //System time
       		data.system_timestamp = get_system_time();

                data.valid = false;

                //Time
                double time=0;
                std::getline(ss,token, ',');

                if( token != "" ) {
                        time = atof(token.c_str());
                }
                LOG_D(TAG, "Time: %lf", time);

                //Valid
                std::getline(ss,token, ',');

                if( token != "" ) {
                        av = token[0];
                }
                LOG_D(TAG, "Valid: %c", av);

                //If GPS fix is not valid, set flag accordingly
                if( av == 'A' ) {
                        data.valid = true;
                }
                else {
                        data.valid = false;
                }

                //Latitude
                std::getline(ss,token, ',');

                if( token != "" ) {
                    lat = atof(token.c_str());
                    dec = (int)lat / 100;
                    frac = lat - (dec *100);
                    frac /= 60.0;
                    data.latitude = dec + frac;
                    LOG_D(TAG, "Lat: %lf", data.latitude);
                }
                else
                {
                    LOG_D (TAG,"Latitude field is blank, Assuming invalid GPS");
                    data.valid = false;
                }

                //North South
                std::getline(ss,token, ',');

                if( token != "" ) {
                    ns = token[0];
                    LOG_D(TAG, "NorthSouth: %c", ns);
                    if ( ns == 'S' ) {
                        data.latitude *= -1;
                    }
                }
                else
                {
                    LOG_D (TAG,"NorthSouth field is blank, Assuming invalid GPS");
                    data.valid = false;
                }

                //Longitude
                std::getline(ss,token, ',');

                if( token != "" ) {
                    lon = atof(token.c_str());
                    dec = (int)lon / 100;
                    frac = lon - (dec *100);
                    frac /= 60.0;
                    data.longitude = dec + frac;
                    LOG_D(TAG, "Lon: %lf", data.longitude);
                }
                else
                {
                    LOG_D (TAG,"Longitude field is blank, Assuming invalid GPS");
                    data.valid = false;
                }

                //East West
                std::getline(ss,token, ',');

                if( token != "" ) {
                    ew = token[0];
                    LOG_D(TAG, "EastWest: %c", ew);

                    if( ew == 'W' ) {
                        data.longitude *= -1;
                    }
                }
                else
                {
                    LOG_D (TAG,"EastWest field is blank, Assuming invalid GPS");
                    data.valid = false;
                }

                //Speed
                std::getline(ss,token, ',');

                if( token != "" ) {
                    speed = atof(token.c_str());
                    LOG_D(TAG, "Speed: %lf", speed);
                    data.speed = speed*1.15078;
                }
                else
                {
                    LOG_D (TAG,"Speed field is blank. Assuming invalid GPS");
                    data.valid = false;
                }
                //True Course - skip
                std::getline(ss,token, ',');

                //Date
                std::getline(ss,token, ',');

                if( token != "" ) {
                        date = atof(token.c_str());
                } else {
                        LOG_D (TAG,"Date field is blank. Assuming invalid GPS");
                        data.valid = false;
                }

                LOG_D(TAG, "Date: %lf", date);

                data.timestamp = make_epoch_time(time, date);
                // hot fix for handling time rollout issue, time rolledoud by 1024 weeks from 3rd Nov 2019
                // https://www.sierrawireless.com/iot-blog/iot-blog/2019/10/gps-rollover/
                // 1572600000 is Friday, November 1, 2019 9:20:00 AM
                if( data.timestamp < 1572600000 ) {
                    data.timestamp = data.timestamp + (1024*7*24*3600);
                    if(log_gps_rollover) {
                        log_gps_rollover = false;
                        LOG_E(TAG, "entered into GPS-rollover case");
                        LOG_I(TAG, "HEADER: %s", header.c_str());
                    }
                }
                data.timestamp = data.timestamp * 1000;
                if( gprmc_status && gpgga_status && pqxfi_status && gpvtg_status && data.valid ) {
                        if( first_fix == false ) {
                            first_fix = true;
                            gps_first_fix_time = get_system_monotonic_time();
                            extended_gps.ttff_ts = get_system_monotonic_time();
                            if(is_gps_port_disconnected) gps_first_fix_time = gps_first_fix_time - gps_port_disconnected_time;
                            LOG_I(TAG, "GPS First Fix time: %lld sec", gps_first_fix_time/MILLISECOND_TO_SECOND);
                            if(((service_start_time/MILLISECOND_TO_SECOND) <= SERVICE_UPTIME_THRESHOLD) || is_gps_port_disconnected ){
                                log_gps_ttf(gps_port_enumerate_time, gps_first_fix_time);
                                is_gps_port_disconnected = false;
                            }
                        }

                        data.valid = true;
                } else {
                        data.valid = false;
                }

                extended_gps.raw_time_micro = raw_time_ns/ONE_MICRO_IN_NANO;
                do_gps_cb(data, extended_gps);

                gprmc_status = false;
                gpgga_status = false;
                pqxfi_status = false;
                gpvtg_status = false;
        	//system( "lte_gps_sample_app \'at!gpsloc?\'" );
        }

    if(cstatus && (header.find("GPGGA") != string::npos)) {

                gpgga_status = cstatus;

                //Skip 5 fields
                for(int i=0; i<5; i++) {
                        std::getline(ss,token, ',');
                }

                std::getline(ss,token, ',');
                LOG_D(TAG, "GPS FIX %s", token.c_str());
                extended_gps.fix_quality = atoi(token.c_str());
		LOG_D(TAG, "GPS ALT %s", token.c_str());
                //If fix is not valid, set altitude to 0
                if( token == "" || token == "0" ) {
                    LOG_D (TAG,"Altitude field is blank");
                        data.altitude = 0;
                        return;
                }

                //Skip 2 fields
                for(int i=0; i<2; i++) {
                        std::getline(ss,token, ',');
                }

                //Altitude
                std::getline(ss,token, ',');

                if( token != "" ) {
                        data.altitude = atof(token.c_str());
                } else {
                        LOG_D(TAG, "Altitude field is NULL");
                }
                LOG_D(TAG,"Altitude: %lf", data.altitude);
        }

    if(cstatus && (header.find("PQXFI") != string::npos)) {

                pqxfi_status = cstatus;

                //Skip 6 fields
                for(int i=0; i<6; i++) {
                        std::getline(ss,token, ',');
                }

                std::getline(ss,token, ',');

                //If fix is not valid, set altitude to 0
                if( token == "" ) {
                        data.accuracy = 0;
                        LOG_D(TAG,"Accuracy field is null, Assuming invalid GPS");
                        data.valid = false;
                } else {
                        data.accuracy = atof(token.c_str());
                        update_accuracy_list(data.accuracy);
                }
                LOG_D(TAG,"Accuracy: %lf", data.accuracy);
        }

    if(cstatus && (header.find("GPVTG") != string::npos)) {

                gpvtg_status = cstatus;

                //Read true heading
                std::getline(ss, token, ',');

                //If Bearing is not valid, set it to 0
                if( token == "" ) {
                        data.bearing = 0;
                        LOG_D(TAG,"Bearing field is null");
                } else {
                        data.bearing = atof(token.c_str());
                }
                LOG_D(TAG,"Bearing: %lf", data.bearing);
                //GPVTG always comes after all GPGSV messages with sat info
                extended_gps.good_satellites = goodSatellites;
                LOG_D(TAG,"good satellites value is %d",goodSatellites);
       }
    if (cstatus && (header.find("GSV") != string::npos)) {

        LOG_D(TAG,"GSV message header = %s", header.c_str());
        int totalMsgNo = 0, curMsgNo = 0, totalSatNo = 0, maxSatInMsg = 4;

        std::getline(ss, token, ',');

        if( token == "" ) {
            LOG_D(TAG,"Corrupt GSV info");
            return;
        } else {
            totalMsgNo = atoi(token.c_str());
        }
        LOG_D(TAG,"total msg No. : %d", totalMsgNo);

        std::getline(ss, token, ',');

        if( token == "" ) {
            LOG_D(TAG,"Corrupt GSV info");
            return;
        } else {
            curMsgNo = atoi(token.c_str());
        }
        LOG_D(TAG,"total current msg number : %d", curMsgNo);

        std::getline(ss, token, ',');

        if( token == "" ) {
            LOG_D(TAG,"Corrupt GSV info");
            return;
        } else {
            totalSatNo = atoi(token.c_str());
        }
        LOG_D(TAG,"total sattelite : %d", totalSatNo);

        if (totalMsgNo == 1)
            maxSatInMsg = totalSatNo;
        else if ((totalMsgNo == curMsgNo) && (totalSatNo%totalMsgNo != 0))
            maxSatInMsg = totalSatNo%maxSatInMsg;

        LOG_D(TAG,"sat info for this line : %d", maxSatInMsg);
        for (int i = 0; i < maxSatInMsg; i++)
        {
            int satId = 0; int snr = -1;
            std::getline(ss, token, ',');

            if( token == "" ) {
                LOG_D(TAG,"No SAT ID");
                continue;
            } else {
                satId = atoi(token.c_str());
            }
            for (int j = 0; j < 2; j++)
                std::getline(ss, token, ',');

            std::getline(ss, token, ',');

            if( token == "" ) {
                LOG_D(TAG,"No SNR");
            } else {
                snr = atoi(token.c_str());
            }
            if(snr >= GOOD_SNR_THRESHOLD)
            {
                goodSatellites+=1;
            }
            LOG_D(TAG,"sattelite ID: %d - SNR: %d", satId, snr);
        }
    }
}

static void parse_lumia3(char *buf) {
	if ( strlen(buf) == 0) {
	   LOG_E(TAG, "buff len is 0");
	   return;
	}

	string input(buf);
	//   Debug Log Utility
	if(nmea_log_val)
	{

        if (nmea_log_file != NULL){
            
            fprintf(nmea_log_file, "NMEA: %s\n", input.c_str());
        } 
        else {
            LOG_I(TAG, "Failed to open nmea log file\n");
        }
    }
	stringstream ss(input);
	string header;
	string token;
	bool b1=false, b2=false, cstatus = false;
	int csum=0, read_csum = 0;

	std::getline(ss, header, ',');
	static bool log_gps_rollover = true;

	LOG_D(TAG, "buff :: %s",buf);
	LOG_D(TAG, "HEADER: %s", header.c_str());
	b1 = get_checksum(buf, strlen(buf), csum);
	b2 = read_checksum(buf, strlen(buf), read_csum);

	if( b1 && b2 && (csum == read_csum) ) {
		cstatus = true;
	} else {
		cstatus = false;
		LOG_E(TAG, "Checksum failed");
	}

	if(cstatus && (header.find("GNRMC") != string::npos)) {
		int dec;
		float frac;
		double lat = 0;
		double lon = 0;
		char ns = 'I';
		char ew = 'I';
		char av='V';
		double speed=0;
		int date = 0;
		uint64_t raw_time_ns = get_system_monotonic_time_ns();
		goodSatellites = 0;
		gprmc_status = cstatus;

		//System time
		data.system_timestamp = get_system_time();

		data.valid = false;

		//Time
		double time=0;
		std::getline(ss,token, ',');

		if( token != "" ) {
			time = atof(token.c_str());
		}
		if(nmea_log_val)
         {
            LOG_I(TAG, "Time: %lf", time);
         }
		//Valid
		std::getline(ss,token, ',');

		if( token != "" ) {
			av = token[0];
		}
        if(nmea_log_val)
        {
		    LOG_I(TAG, "Valid: %c", av);
        }
		//If GPS fix is not valid, set flag accordingly
		if( av == 'A' ) {
			data.valid = true;
		}
		else {
			data.valid = false;
		}

		//Latitude
		std::getline(ss,token, ',');

		if( token != "" ) {
			lat = atof(token.c_str());
			dec = (int)lat / 100;
			frac = lat - (dec *100);
			frac /= 60.0;
			data.latitude = dec + frac;
            if(nmea_log_val)
            {
			    LOG_I(TAG, "Lat: %lf", data.latitude);
            }
		}
		else
		{
			LOG_E (TAG,"Latitude field is blank, Assuming invalid GPS");
			data.valid = false;
		}

		//North South
		std::getline(ss,token, ',');

		if( token != "" ) {
			ns = token[0];
			LOG_D(TAG, "NorthSouth: %c", ns);
			if ( ns == 'S' ) {
				data.latitude *= -1;
			}
		}
		else
		{
			LOG_E (TAG,"NorthSouth field is blank, Assuming invalid GPS");
			data.valid = false;
		}

		//Longitude
		std::getline(ss,token, ',');

		if( token != "" ) {
			lon = atof(token.c_str());
			dec = (int)lon / 100;
			frac = lon - (dec *100);
			frac /= 60.0;
			data.longitude = dec + frac;
            if(nmea_log_val)
            {
			    LOG_I(TAG, "Lon: %lf", data.longitude);
            }
		}
		else
		{
			LOG_D (TAG,"Longitude field is blank, Assuming invalid GPS");
			data.valid = false;
		}

		//East West
		std::getline(ss,token, ',');

		if( token != "" ) {
			ew = token[0];
			LOG_D(TAG, "EastWest: %c", ew);

			if( ew == 'W' ) {
				data.longitude *= -1;
			}
		}
		else
		{
			LOG_D (TAG,"EastWest field is blank, Assuming invalid GPS");
			data.valid = false;
		}

		//Speed
		std::getline(ss,token, ',');

		if( token != "" ) {
			speed = atof(token.c_str());
            if(nmea_log_val)
            {
			    LOG_I(TAG, "Speed: %lf", speed);
            }
			if(speed <= STATIC_SPEED_THRESHOLD_L3)
			    data.speed = 0;
			else
			    data.speed = speed*1.15078;
            if(nmea_log_val)
            {
			    LOG_I(TAG, "Actual speed: %lf", data.speed);
            }
		}
		else
		{
			if(nmea_log_val)
            {
                LOG_I(TAG,"Speed field is blank. Assuming invalid GPS");
            }
			data.valid = false;
		}
		//True Course - skip
		std::getline(ss,token, ',');

		//Date
		std::getline(ss,token, ',');

		if( token != "" ) {
			date = atof(token.c_str());
		} else {
			LOG_D (TAG,"Date field is blank. Assuming invalid GPS");
			data.valid = false;
		}
        if(nmea_log_val)
        {
		    LOG_I(TAG, "Date: %lf", date);
        }

		data.timestamp = make_epoch_time(time, date);
		// hot fix for handling time rollout issue, time rolledoud by 1024 weeks from 3rd Nov 2019
		// https://www.sierrawireless.com/iot-blog/iot-blog/2019/10/gps-rollover/
		// 1572600000 is Friday, November 1, 2019 9:20:00 AM
		if( data.timestamp < 1572600000 ) {
			data.timestamp = data.timestamp + (1024*7*24*3600);
			if(log_gps_rollover) {
				log_gps_rollover = false;
				LOG_E(TAG, "entered into GPS-rollover case");
                if(nmea_log_val)
                {
				    LOG_I(TAG, "HEADER: %s", header.c_str());
                }
			}
		}
		data.timestamp = data.timestamp * 1000;
		if( gprmc_status && gpgga_status && pqxfi_status && gpvtg_status && data.valid ) {
			if( first_fix == false ) {
                if(nmea_log_val)
                {
				    LOG_I(TAG, "GPS FIRST FIX...");
                }
				first_fix = true;
                gps_first_fix_time = get_system_monotonic_time();
                extended_gps.ttff_ts = get_system_monotonic_time();
                if(is_gps_port_disconnected) gps_first_fix_time = gps_first_fix_time - gps_port_disconnected_time;
                if(((service_start_time/MILLISECOND_TO_SECOND) <= SERVICE_UPTIME_THRESHOLD) || is_gps_port_disconnected ){
                    string err_msg = "TTFF: " + std::to_string(gps_first_fix_time/MILLISECOND_TO_SECOND) + " sec, TTFF(excluding enumeration): " + std::to_string((gps_first_fix_time - gps_port_enumerate_time)/MILLISECOND_TO_SECOND) + " sec";
                    nd_service_obj->send_err_msg(SM_E_GPS_FIRST_FIX, 0, err_msg );
                    is_gps_port_disconnected = false;
                }
			}

            if(fix_quality_flag)
            {
                data.valid = true;
            }
            else
            {
                if(extended_gps.fix_quality == 1 || extended_gps.fix_quality == 2)
                {
                    fix_quality_flag = true;
                    LOG_I(TAG, "Fix quality flag set to true");
                    data.valid = true;
                    string err_msg = "GPS Fix Quality Valid";
                    nd_service_obj->send_err_msg(SM_E_GPS_FIX_CHANGE_VALID,extended_gps.fix_quality, err_msg);

                }
                else
                {
                    data.valid = false;
                    if(!critical_info_fix_quality_flag)
                    {
                        critical_info_fix_quality_flag = true;
                        string err_msg = "GPS Fix Quality Invalid";
                        nd_service_obj->send_err_msg(SM_E_GPS_FIX_CHANGE_INVALID,extended_gps.fix_quality, err_msg);
                    }
                }
            }
		} else {
			data.valid = false;
		}
        if(nmea_log_val)
        {
		    LOG_I(TAG, "valid : %d",data.valid);
        }
		extended_gps.raw_time_micro = raw_time_ns/ONE_MICRO_IN_NANO;
		do_gps_cb(data, extended_gps);

		gprmc_status = false;
		gpgga_status = false;
		pqxfi_status = false;
		gpvtg_status = false;
        pqtmsenmsg_status = false;
		//system( "lte_gps_sample_app \'at!gpsloc?\'" );
	}

	if(cstatus && (header.find("GNGGA") != string::npos)) {

		gpgga_status = cstatus;

		//Skip 5 fields
		for(int i=0; i<5; i++) {
			std::getline(ss,token, ',');
		}

		std::getline(ss,token, ',');
        if(nmea_log_val)
        {
		    LOG_I(TAG, "GPS FIX %s", token.c_str());
        }
		extended_gps.fix_quality = atoi(token.c_str());


	LOG_D(TAG, "GPS ALT %s", token.c_str());
		//If fix is not valid, set altitude to 0
		if( token == "" || token == "0" ) {
			LOG_D (TAG,"Altitude field is blank");
			data.altitude = 0;
			return;
		}

		//Skip 2 fields
		for(int i=0; i<2; i++) {
			std::getline(ss,token, ',');
		}

		//Altitude
		//std::getline(ss,token, ',');

		if( token != "" ) {
			data.altitude = atof(token.c_str());
		} else {
			LOG_D(TAG, "Altitude field is NULL");
		}
        if(nmea_log_val)
        {
		    LOG_I(TAG,"Altitude: %lf", data.altitude);
        }
	}

	if(cstatus && (header.find("PQTMEPE") != string::npos)) {

		pqxfi_status = cstatus;

		//Skip 5 fields for 2d accuracy
		for(int i=0; i<5; i++) {
			std::getline(ss,token, ',');
		}

		//If fix is not valid, set altitude to 0
		if( token == "" ) {
			data.accuracy = 0;
			LOG_D(TAG,"Accuracy field is null, Assuming invalid GPS");
			data.valid = false;
		} else {
			data.accuracy = atof(token.c_str());
			update_accuracy_list(data.accuracy);
		}
        if(nmea_log_val)
        {
		    LOG_I(TAG,"Accuracy: %lf", data.accuracy);
        }
	}

    if (cstatus && header.find("PQTMSENMSG") != string::npos &&  !pqtmsenmsg_status)
    {
        for (int i = 0; i < 3; i++)
        {
            std::getline(ss, token, ',');
        }
        std::getline(ss, token, ',');

        if (token == "")
        {
            LOG_D(TAG, "IMU temperature field is null");
        }
        else
        {
            extended_gps.gnss_temp = atof(token.c_str());
            LOG_I(TAG,"IMU Temperature: %lf", extended_gps.gnss_temp);
        }
        pqtmsenmsg_status = true;
    }

	if(cstatus && (header.find("GNVTG") != string::npos)) {

		gpvtg_status = cstatus;

		//Read true heading
		std::getline(ss, token, ',');

		//If Bearing is not valid, set it to 0
		if( token == "" ) {
			data.bearing = 0;
			LOG_D(TAG,"Bearing field is null");
		} else {
			data.bearing = atof(token.c_str());
		}
        if(nmea_log_val)
        {
		    LOG_I(TAG,"Bearing: %lf", data.bearing);
        }
        //GPVTG always comes after all GPGSV messages with sat info

	}

	if(cstatus && (header.find("GSV") != string::npos)) {
        parsing_gns_sat_msg = true;
        if(nmea_log_val)
        {
		    LOG_I(TAG,"GSV message header = %s", header.c_str());
        }
		int totalMsgNo = 0, curMsgNo = 0, totalSatNo = 0, maxSatInMsg = 4;

		std::getline(ss, token, ',');

		if( token == "" ) {
			LOG_D(TAG,"Corrupt GSV info");
			return;
		} else {
			totalMsgNo = atoi(token.c_str());
		}
        if(nmea_log_val)
        {
		    LOG_I(TAG,"total msg No. : %d", totalMsgNo);
        }

		std::getline(ss, token, ',');

		if( token == "" ) {
			LOG_D(TAG,"Corrupt GSV info");
			return;
		} else {
			curMsgNo = atoi(token.c_str());
		}
        if(nmea_log_val)
        {
		    LOG_I(TAG,"total current msg number : %d", curMsgNo);
        }
		std::getline(ss, token, ',');

		if( token == "" ) {
			LOG_D(TAG,"Corrupt GSV info");
			return;
		} else {
			totalSatNo = atoi(token.c_str());
		}
        if(nmea_log_val)
        {
		    LOG_I(TAG,"total sattelite : %d", totalSatNo);
        }
		if (totalMsgNo == 1)
			maxSatInMsg = totalSatNo;
		else if ((totalMsgNo == curMsgNo) && (totalSatNo%totalMsgNo != 0))
			maxSatInMsg = totalSatNo%maxSatInMsg;
        if(nmea_log_val)
        {
		    LOG_I(TAG,"sat info for this line : %d", maxSatInMsg);
        }
		for (int i = 0; i < maxSatInMsg; i++)
		{
			int satId = 0; int snr = -1;
			std::getline(ss, token, ',');

			if( token == "" ) {
				LOG_D(TAG,"No SAT ID");
				continue;
			} else {
				satId = atoi(token.c_str());
			}
			for (int j = 0; j < 2; j++)
				std::getline(ss, token, ',');

			std::getline(ss, token, ',');

			if( token == "" ) {
				LOG_D(TAG,"No SNR");
			} else {
				snr = atoi(token.c_str());
			}
			if(snr >= GOOD_SNR_THRESHOLD)
			{
				goodSatellites+=1;
			}
			LOG_D(TAG,"sattelite ID: %d - SNR: %d", satId, snr);
		}


	}

    if(cstatus &&  (header.find("GSV") == string::npos) && parsing_gns_sat_msg)
    {
        // This is to handle the case when all GSV message is read then 
        // we need to push the good satellites count in extended gps
        // structure.
        parsing_gns_sat_msg = false;
        extended_gps.good_satellites = goodSatellites;
        LOG_D(TAG,"good satellites value is %d",goodSatellites);
    }
}

static void *pps_record_thread (void *) {

    char buffer[100] = {'\0'};
    int64_t pps_raw_time = 0, pps_clock_time = 0;
    struct pollfd ufds[1];
    int poll_timeout = -1;
    int poll_rv = -1;
    Gps::gps_pps_data_t pps_data;
    int pps_number = 0;



    int pps_fd = open (GPS_PPS_FILE.c_str(), O_RDONLY);
    if (pps_fd < 0 ) {
        LOG_E (TAG, "Can't open %s. No PPS data will be present in metadata",
                GPS_PPS_FILE.c_str());
        pthread_exit (NULL);
    }
    //dummy read
    read (pps_fd, buffer, sizeof (buffer));

    while (enable_mask) {
        if (pps_fd <= 0) {
            // We can return from here because insertion of gps_pps_interrupt.ko
            // is done at the beginning of bagheera service itself and hence we are sure
            // file will be present if everything had gone well.
            //
            // In short, we don't need to keep checking if file is created.

            LOG_E (TAG, "No pps_timestamp is available since SYSFS entry was not open");
            break;
        }
        ufds[0].fd = pps_fd;
        ufds[0].events = POLLPRI|POLLERR;
        ufds[0].revents = 0;
        do
        {

            poll_rv = poll (ufds, 1, poll_timeout);
        }while (poll_rv <= 0);

        if (ufds[0].revents & POLLPRI)
        {
            pps_number++;
            lseek(pps_fd, 0, SEEK_SET);
            read (pps_fd, buffer, sizeof (buffer));
            sscanf(buffer, "%lld,%lld", &pps_raw_time, &pps_clock_time);

            pps_data.pps_clock_time = pps_clock_time;
            pps_data.pps_raw_time = pps_raw_time;
            do_pps_cb(pps_data);
        }
    }
}

bool write_to_port(int fd, const char* cmd, int sleep_time) {
    int retry_count = 0;
    while(retry_count < 3) {
        int n_written = write(fd, cmd, strlen(cmd));
        if (n_written < 0) {
            LOG_E(TAG2, "Error writing command %s to port, will try writing again in %d ", cmd, sleep_time );
            sleep(sleep_time);
            retry_count++;
        } else {
            LOG_I(TAG2, "%s Command written to port successfully", cmd);
            return true;
        }
    }
    return false;
}

static void* gps_cold_start(void *){
    bool cold_start_portwrite_res = false;
    while(true){
        sleep(cold_start_thread_sleep_time);
        pthread_mutex_lock(&gps_port_write_mutex);
        LOG_I(TAG2, "Writing gps_cold_start_cmd to port");
        cold_start_portwrite_res = write_to_port(fd, gps_subsys_cold_start_cmd.c_str(), SLEEP_AFTER_WRITEPORT_FAIL);
        pthread_mutex_unlock(&gps_port_write_mutex);
        if(!cold_start_portwrite_res){
            LOG_I(TAG2, "Failed to write gps_cold_start_cmd to port, retrying in 5");
            continue;
        }
        else{
            LOG_I(TAG2, "gps_cold_start_cmd written to port successfully");
        }
        continue;
    }
}

void clear_epo_from_ram(){
    // Remove the contents of the directory agnss_epo_files in device RAM
    string remove_cmd = "rm -rf /dev/shm/agnss_epo_files/*";
    string resp;
    bool res = system_execute_with_resp(TAG2, remove_cmd, resp);
    if (!res) {
        LOG_E(TAG2, "Failed to remove contents of directory /dev/shm/agnss_epo_files");
    } else {
        LOG_I(TAG2, "Successfully removed contents of directory /dev/shm/agnss_epo_files");
    }
}

/*
Periodically checks and manages AGNSS EPO file expiry.
1. Opens GPS module and retries if needed.
2. Opens/creates file that stores epo file download time and reads download time.
3. If file is empty or expired(240+hrs from its downloaded time), downloads new epo files into device RAM and erases old files from the module.
4. If valid, waits for the expiry check interval and repeats the process.
5. Cold start thread is created in the end, for testing purpose.
*/
static void* agnss_epo_files_expiry_check(void*) {
    int create_file_retry_count = 0;
    bool agnss_portwrite_res = false;
    while(true){
      
        std::ifstream download_time_ifs;
        std::ofstream download_time_ofs;

        download_time_ifs.open(epo_file_download_time_path);
        
        if (!download_time_ifs.is_open()) {  
            LOG_I(TAG2, "Failed to open /home/ubuntu/.nddevice/.epo_file_download_time file");
            while (create_file_retry_count < 3) {
                std::ofstream download_time_ofs(epo_file_download_time_path);
        
                if (!download_time_ofs) {
                    create_file_retry_count++;
                    LOG_I(TAG2, "Failed to create /home/ubuntu/.nddevice/.epo_file_download_time file. Retrying in 5 seconds");
                    sleep(SLEEP_TIME_BEFORE_RETRY);
                    continue;
                } 
                else {
                    LOG_I(TAG2, "File /home/ubuntu/.nddevice/.epo_file_download_time created successfully");
                    download_time_ofs.close();  
                    create_file_retry_count = 0;  
                    break; 
                }
            }
        
            if (create_file_retry_count >= 3) {
                LOG_E(TAG2, "Failed to create /home/ubuntu/.nddevice/.epo_file_download_time file after 3 retries. Retrying Again");
                continue;
            }

            download_time_ifs.open(epo_file_download_time_path); //opening file after creating it
        }

        if (download_time_ifs.is_open()) {
            LOG_I(TAG2, "File epo_file_download_time opened successfully!");
            std::time_t download_time;
            download_time_ifs.clear();  // Clear any error or EOF flags
            download_time_ifs.seekg(0, std::ios::beg);  // Seek back to the beginning

            if (download_time_ifs.peek() == std::ifstream::traits_type::eof()) {
                LOG_I(TAG2, "File epo_file_download_time is empty");
            
                if(!agnss_init(no_of_epofiles)){
                    //Few EPO files might got downloaded, but not all. 
                    clear_epo_from_ram();
                    sleep(SLEEP_TIME_BEFORE_RETRY);
                    continue;
                }
                pthread_mutex_lock(&gps_port_write_mutex);
                agnss_portwrite_res = write_to_port(fd,epo_erase_cmd.c_str(), SLEEP_AFTER_WRITEPORT_FAIL);
                pthread_mutex_unlock(&gps_port_write_mutex);
                if(!agnss_portwrite_res){
                    sleep(SLEEP_TIME_BEFORE_RETRY);
                    continue;
                }
               
            }
            else {
                if (download_time_ifs >> download_time) {

                    LOG_I(TAG2, "Successfully read download time from the epo_file_download_time:  %d in secs", download_time);
                    // Get the current time
                    std::time_t current_time = time(nullptr);
                    std::time_t expiry_time = download_time + (3 * no_of_epofiles * SECONDS_IN_24_HOURS);
                    std::time_t time_left = expiry_time > current_time ? (expiry_time - current_time) : 0;

                    if ((current_time >= expiry_time) || (time_left <= (2*SECONDS_IN_24_HOURS))) {
                        if(current_time >= expiry_time){
                            LOG_I(TAG2, "EPO files expired %lld hours ago, Time to download new files!", (current_time - expiry_time)/SECONDS_IN_AN_HOUR);
                        }
                        else {
                            LOG_I(TAG2, "EPO files expiry is expected in %lld hours, Time to download new files!", time_left/SECONDS_IN_AN_HOUR);
                        }
                
                        if(!agnss_init(no_of_epofiles)){
                            clear_epo_from_ram();
                            sleep(SLEEP_TIME_BEFORE_RETRY);
                            continue;
                        }
    
                        // Write the epo_erase_command to the port, to erase the previous EPO files
                        pthread_mutex_lock(&gps_port_write_mutex);
                        agnss_portwrite_res = write_to_port(fd,epo_erase_cmd.c_str(), SLEEP_AFTER_WRITEPORT_FAIL);
                        pthread_mutex_unlock(&gps_port_write_mutex);
                        if(!agnss_portwrite_res){
                            sleep(SLEEP_TIME_BEFORE_RETRY);
                            continue;
                        }
                    }
                    else{
                        LOG_I(TAG2, "EPO files expiry is expected in %lld hours", time_left / SECONDS_IN_AN_HOUR);
                    }
                } 
                else {
                    LOG_I(TAG2, "Failed to read download time from file, retrying in 5");
                    sleep(SLEEP_TIME_BEFORE_RETRY);
                    continue;
                }
            }
        }
        if(is_cold_start_enabled == "true"){
            if(pthread_create(&gps_cold_start_thread, NULL, gps_cold_start, NULL)){
                LOG_I(TAG2, "Failed to create thread for gps_cold_start");
                return nullptr;
            }
        }
        sleep(EPO_FILES_EXPIRY_CHECK_INTERVAL);
        continue;
    }  
}

//Wrapper function to get config parameter
string get_config_param (Config_parser &conf, string section, string key, string default_val, bool get_override_val, bool& is_val_overridden){
    string res = "";
    if(conf.isPresent(section, key)){
        res = conf.getConfig(section, key, default_val, get_override_val, is_val_overridden);
        LOG_I(TAG2, "Key : %s, Value : %s", key.c_str(), res.c_str());
    }
    return res;
}

static void *read_thread( void *p ) {
	thread_alive = true;
	int buflen;
	char buf[1];
	char data_line[NMEA_LEN];
	int i=0;

    // Delay recommended as the Lumia boot up itself takes around 24 seconds
    // BGR2-327
    sleep(5);
    Config_parser conf (BAGHEERACONFIG_INI);
    bool get_override_val = true;
    bool is_val_overridden = false;
    
    if (conf.getParseStatus() == false)
    {
        LOG_E (TAG," Failed to parse bagheera_config.ini");
    }
    
    else{
        // Get the gps, agnss config parameters
        is_val_overridden = false;
        nmea_log_status = get_config_param(conf, "gps", "nmea_log_enabled", "false", get_override_val, is_val_overridden);
        LOG_I (TAG, "nmea_log_enabled: %s", nmea_log_status.c_str());
        is_val_overridden = false;
        is_agnss_enabled = get_config_param(conf, "agnss", "enabled", "true", get_override_val, is_val_overridden);
        LOG_I(TAG2, "is_agnss_enabled: %s", is_agnss_enabled.c_str());
        is_val_overridden = false;
        string epo_files = get_config_param(conf, "agnss", "no_of_epofiles", "4", get_override_val, is_val_overridden);
        no_of_epofiles = atoi(epo_files.c_str());
        LOG_I(TAG2, "no_of_epofiles: %d", no_of_epofiles);
        is_val_overridden = false;
        is_cold_start_enabled=get_config_param(conf, "agnss", "cold_start_enabled", "false", get_override_val, is_val_overridden);
        LOG_I(TAG2, "is_cold_start_enabled: %s", is_cold_start_enabled.c_str());
        is_val_overridden = false;
        string sleep_time = get_config_param(conf, "agnss", "cold_start_thread_sleep_time", "600", get_override_val, is_val_overridden);
        cold_start_thread_sleep_time = atoi(sleep_time.c_str());
        LOG_I(TAG2, "cold_start_thread_sleep_time: %d", cold_start_thread_sleep_time);
        is_val_overridden = false;
    }

    if( nmea_log_status == "true")
    {
        nmea_log_val = true;
        sprintf(nmea_log_filename, "/home/ubuntu/.nddevice/log/gps/nmea_log_%llu.log", get_system_time());
        nmea_log_file = fopen(nmea_log_filename, "a");
        LOG_I(TAG,"NMEA log created %s", nmea_log_filename);
    }
    else
    {
        nmea_log_val = false;
    }   

#ifdef DECOUPLE_GPS_CAM
    while(!open_port()) {
        LOG_I (TAG,"GPS port not available yet");
        extended_gps.gps_port_status = false;
        extended_gps.gps_port_status_ts = get_system_monotonic_time();

        if(read_last_known_valid_gps_data(latest_cached_latitude,latest_cached_longitude))
        {
            data.valid = false;
            data.latitude = latest_cached_latitude;
            data.longitude = latest_cached_longitude;
            data.altitude = 0.0;
            data.speed = 0.0;
            data.bearing = 0.0;
            data.accuracy = 1000; // Set a high accuracy value
            data.timestamp = get_system_time();
            data.system_timestamp = get_system_time();
            data.flags = 0; 
            extended_gps.gps_data = data;
            extended_gps.gps_index = 0;
            extended_gps.fix_quality = 0;
            extended_gps.good_satellites = 0;
            extended_gps.raw_time_micro = get_system_monotonic_time_ns()/ONE_MICRO_IN_NANO;
            extended_gps.gps_data.flags = 0;
            do_gps_cb(data, extended_gps);
            string msg = "Using Retained GPS : Long + " + std::to_string(data.longitude) + ", Lat " + std::to_string(data.latitude);
            
            nd_service_obj->send_err_msg(SM_E_GPS_LAST_VALID_GPS_INFO,1,msg);
            LOG_I(TAG, "%s", msg.c_str());
        
        }
        else
        {
            LOG_I(TAG, "No last known valid GPS data");
            nd_service_obj->send_err_msg(SM_E_GPS_LAST_VALID_GPS_INFO,0,"No last known valid GPS data");
        }
        sleep(1);
    }

    LOG_I(TAG, "GPS connected");
    extended_gps.gps_port_status = true;
    extended_gps.gps_port_status_ts = get_system_monotonic_time();
    gps_port_enumerate_time = get_system_monotonic_time();
    LOG_I(TAG, "GPS Port Enumerate Time: %lld sec", gps_port_enumerate_time/MILLISECOND_TO_SECOND);
    if((service_start_time/MILLISECOND_TO_SECOND) <= SERVICE_UPTIME_THRESHOLD){
        string err_msg = "GPS Port Enumerate Time: " + std::to_string(gps_port_enumerate_time/MILLISECOND_TO_SECOND) + " seconds";
        nd_service_obj->send_err_msg(SM_E_GPS_PORT_ENUMERATE, 0, err_msg );
    }
    config_port();

    //Creating epo file expiry check thread, for quectel modem
    if(is_quectel_modem()){
        if(is_agnss_enabled=="true"){
            if(pthread_create(&epo_expiry_check_thread, NULL, agnss_epo_files_expiry_check, NULL)) {
                LOG_E(TAG2, "Failed to create thread for agnss_epo_files_expiry_check");
                return NULL;
            }    
        }
    }
   
#endif

    while (enable_mask)
    {

        buflen = read(fd, buf, 1);

        if (buflen == 0 && access(gps_port.c_str(), F_OK) == -1)
        {
            /*Buflen is Zero and current GPS file node is not present,
            Looks like modem crashed. Keep trying to open node until
            modem is back alive*/

            LOG_E(TAG, "GPS Device not found, trying to reconnect");
            log_file("gps_crashed");
            is_gps_port_disconnected = true;
            gps_port_disconnected_time = get_system_monotonic_time();
            first_fix = false;
            while( !open_port() ) {
                    LOG_E(TAG, "GPS Device not found");
                    extended_gps.gps_port_status = false;
                    extended_gps.gps_port_status_ts = get_system_monotonic_time();
                    sleep(1);
            }

            LOG_I(TAG, "GPS connected");
            extended_gps.gps_port_status = true;
            extended_gps.gps_port_status_ts = get_system_monotonic_time();
            gps_port_enumerate_time = get_system_monotonic_time() - gps_port_disconnected_time;
            LOG_I(TAG, "GPS Port Enumerate Time: %lld sec", gps_port_enumerate_time/MILLISECOND_TO_SECOND);
            string err_msg = "GPS Port Enumerate Time: " + std::to_string(gps_port_enumerate_time/MILLISECOND_TO_SECOND) + " seconds";
            nd_service_obj->send_err_msg(SM_E_GPS_PORT_ENUMERATE, 0, err_msg );
            config_port();
            i=0;
            log_file("gps_revived");
            continue;
        }
        if (buflen > 0) // got input bytes
        {
            if (i > NMEA_LEN)
            {
                i = 0;
                continue;
            }
            else if (buf[0] == '\r')
            {
                continue;
            }
            else if (buf[0] == '\n')
            {
                data_line[i++] = '\0';
                string line_buffer(data_line);
                
                if(line_buffer.find("$PAIR001,006,0*3D")!= std::string::npos){
                    string off_resp="";
                    string on_resp="";
                    LOG_I(TAG2, "Received GPS subsys cold start ack : %s", line_buffer.c_str());
                    LOG_I(TAG2, "Writing GPS off/on command to port");
                    bool off_res = system_execute_with_resp(TAG2, gps_off_cmd, off_resp);
                    if(off_res){
                        LOG_I(TAG2, "%s command is executed ", gps_off_cmd.c_str());
                        sleep(5);
                        bool on_res = system_execute_with_resp(TAG2, gps_on_cmd, on_resp);
                        if(on_res){
                            LOG_I(TAG2, "%s command is executed ", gps_on_cmd.c_str());
                        }
                    }
                }

                if (line_buffer.find("$PAIR001,472") != std::string::npos) {

                    LOG_I(TAG2, "Received PAIR command ack : %s", line_buffer.c_str());
                       
                    for(int i=1; i<=no_of_epofiles; ++i){
                        LOG_I(TAG2,"Sending EPO file %d to GNSS module", i);
                        string epo_file_path = "/dev/shm/agnss_epo_files/EPO_GPS_3_" + std::to_string(i) + ".DAT";
                        if(Send_EPO(epo_file_path.c_str(), fd, i, no_of_epofiles)) continue;  //if the file is written and recieved ack then continue sending next file 
                        else if(raise_ack_fail_ce()==false) i=0; //Incase of ack timeout while sending a file to port, Send_EPO returns false, so whatever written till then is erased and writing from first file.
                        else break; //Max write retry count reached, means failed to load files to module, so raised a critical event, thus breaking out of the loop.
                    }
                    // Remove the contents of the directory agnss_epo_files in device RAM
                    clear_epo_from_ram();
                           
                }

                if (is_sierra_modem())
                    parse(data_line);
                else if (is_quectel_modem())
                    parse_lumia3(data_line);
               
                i = 0;
            }
            else
            {
                data_line[i++] = buf[0];
            }
        }
    }
    thread_alive = false;
}

//Temporary hack to enable GPS NMEA commands
bool at_config() {
	fd = open("/dev/ttyUSB2", O_RDWR);

	if (fd < 0) {
                LOG_E(TAG, "Cannot open AT command port");
		return false;
	}

	struct termios tty;
	tcgetattr( fd, &tty );

	/* SEt Baud Rate */

	cfsetospeed( &tty, B115200 );
	cfsetispeed( &tty, B115200 );

 	tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;

	tty.c_iflag =  IGNBRK;

	tty.c_lflag = 0;
	tty.c_oflag = 0;

	tty.c_cflag |= CLOCAL | CREAD;
	//tty.c_cc[VMIN] = 1;
	//tty.c_cc[VTIME] = 5;

	tty.c_iflag &= ~(IXON|IXOFF|IXANY);

	tty.c_cflag &= ~(PARENB | PARODD);

	tcsetattr(fd, TCSANOW, &tty);

 	struct termios sgg;

	tcgetattr(fd, &sgg);
	sgg.c_cflag |= CLOCAL;
	tcsetattr(fd, TCSANOW, &sgg);
	{
	struct termios sgg;

	tcgetattr(fd, &sgg);
	sgg.c_cflag |= HUPCL;
	tcsetattr(fd, TCSANOW, &sgg);
	}

        char buff[100];
        strncpy(buff, "AT!GPSAUTOSTART=1, 1, 1, 100, 1\r\n",sizeof(buff));
	write(fd, buff, strlen(buff));

        strncpy(buff, "AT!GPSNMEA=1\r\n",sizeof(buff));
	write(fd, buff, strlen(buff));

        strncpy(buff, "AT!GPSNMEACONFIG=1,1\r\n",sizeof(buff));
	write(fd, buff, strlen(buff));

	close(fd);

	return true;
}


void run_at_command_gps_config_recover()
{

    if (!is_sierra_modem())
    {
        LOG_I(TAG, "Not Sierra modem, so nothing to be done");
        return;
    }
    string resp;
    bool res = system_execute_with_resp(TAG, GPS_AUTO_RESTART_CMD, resp);
    if(res)
    {
        LOG_I(TAG, "Output of %s command is \n: %s", GPS_AUTO_RESTART_CMD.c_str(), resp.c_str());
    }
    res = system_execute_with_resp(TAG, GPS_AUTO_RESTART_CMD2, resp);
    if(res)
    {

       LOG_I(TAG, "Output of %s command is \n: %s", GPS_AUTO_RESTART_CMD2.c_str(), resp.c_str());
    }
}
