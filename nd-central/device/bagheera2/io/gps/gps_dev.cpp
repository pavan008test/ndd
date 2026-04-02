#include <unistd.h>  /* UNIX standard function definitions */
#include <fcntl.h>   /* File control definitions */
#include <termios.h> /* POSIX terminal control definitions */
#include <cstdio>
#include <string.h>
#include <stdio.h>
#include <cstdlib>
#include <unistd.h>

#include "system_utils.h"
#include <gps_dev.h>
#include <log.h>

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
#define GOOD_SNR_THRESHOLD 30     //Good GNSS signal should be considered only when snr is more than 30

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
static bool at_config();

static int fd;
static int enable_mask = 0;
static pthread_t gps_thread;
static pthread_t gps_pps_thread;
static volatile bool thread_alive=false;

static const int gps_interface = 2;
static const int fnamelen = 128;

static string gps_port = "";
static const int NMEA_LEN = 1024;
static const string gps_restart_log = "/home/ubuntu/.nddevice/log/gps_restart.log";

static bool first_fix = false;

//when module gps_pps_interrupt.ko is inserted (just before starting bagheera service), below file is created.
static const string GPS_PPS_FILE = "/sys/gps_pps_sysfs/gps_pps_timestamps";

static bool hdmaps_mode;

using namespace std;

extern NDService *nd_service_obj; //nd service object, to detect crashes

#define TAG "GPSD"

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

        FILE *pipe;
        vector<string> ports;
        string cmd = "ls /dev/ttyUSB*";
        if( NULL == (pipe = popen(cmd.c_str(),"r") ) ) {
                LOG_E(TAG, "Cannot list devices");
                return ports;
        }

        char buff[fnamelen];
        while (fgets(buff, fnamelen, pipe) != NULL) {
                LOG_I(TAG, "Detected port: %s", buff);
                string port = string(buff);
                //Erase extra line ending at the end of port number;
                port.erase( std::remove(port.begin(), port.end(), '\n'), port.end() );
                ports.push_back(string(port));
        }

        pclose(pipe);
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

        //Go through the list and check for gps_interface
        for( vector<string>::iterator iter = ports.begin(), end = ports.end(); iter!=end; iter++ ) {
                interface = get_interface(*iter);
                LOG_I(TAG, "PORT: %s interface: %d", (*iter).c_str(), interface  );
                if ( gps_interface ==  interface ) {
                        LOG_I(TAG, "Found GPS interface");
                        return *iter;
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

	cfsetospeed( &tty, B9600 );
	cfsetispeed( &tty, B9600 );

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
    if(pthread_create(&gps_pps_thread, NULL, pps_record_thread, NULL)) {
        return false;
    }
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

void log_gps_ttf() {

    pid_t pid = fork();
    if (pid == 0)
    {
        LOG_I(TAG, "Got first fix");
        //Setting child process kill on parent death
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        FILE *pipe;
        vector<string> ports;
        static const int buff_len = 128;

        LOG_I(TAG, "Starting lte_gps_sample_app");
        string cmd = "lte_gps_sample_app \'at!gpsstatus\?\'";
        if( NULL == (pipe = popen(cmd.c_str(),"r") ) ) {
            LOG_E(TAG, "Cannot get gpsstatus");
        }

        char buff[buff_len];
        while (fgets(buff, buff_len, pipe) != NULL) {
            LOG_I(TAG, "%s", buff);
        }

        pclose(pipe);
        LOG_I(TAG, "Finished lte_gps_sample_app");
        _exit (0);
    }
    else if (pid < 0)
    {
        LOG_E (TAG,"Creating child process for logging first fix failed");
    }
}


Gps::gps_data_t data;
Gps::gps_sensor_data_t gps_sensor_data;
bool gprmc_status = false;
bool gpgga_status = false;
bool pqxfi_status = false;
bool gpvtg_status = false;
int32_t goodSatellites = 0;
float accuracy_list[MAX_ACCURACY_DATA_LEN];
int accuracy_index = 0;
float avg_accuracy_list_in_hour[MAX_ACCURACY_DATA_LEN];
int avg_accuracy_index = 0;
int64_t accuracy_start_timestamp = 0;

static void update_accuracy_list (float current_accuracy)
{
    float sum = 0, avg = 0;

    LOG_I(TAG,"update_accuracy_list: Accuracy = %lf", current_accuracy);
    LOG_I(TAG,"update_accuracy_list: accuracy_index = %d", accuracy_index);
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

static void do_gps_cb (Gps::gps_sensor_data_t gps_sensor_data) {
    if (!cb) {
        return;
    }

    static bool first_gps_data_pushed = false;
    static uint64_t gps_index = 0;
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

    ++gps_index;
    gps_sensor_data.gps_index = gps_index;
    gps_sensor_data.gps_data = data;
    cb (gps_sensor_data);
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
        if( access( "/dev/shm/nmea.txt", F_OK ) != -1)
        {
            LOG_I("NMEA", " %s", input.c_str());
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
                            log_gps_ttf();
                            first_fix = true;
                        }

                        data.valid = true;
                } else {
                        data.valid = false;
                }

                gps_sensor_data.raw_time_micro = raw_time_ns/ONE_MICRO_IN_NANO;
                do_gps_cb(gps_sensor_data);

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
                gps_sensor_data.fix_quality = atoi(token.c_str());
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
                gps_sensor_data.good_satellites = goodSatellites;
                LOG_D(TAG,"good satellites value is %d",goodSatellites);

       }
    if(cstatus && (header.find("GSV") != string::npos)) {

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

static void *read_thread( void *p ) {
	thread_alive = true;
	int buflen;
	char buf[1];
	char data_line[NMEA_LEN];
	int i=0;

    // Delay recommended as the Lumia boot up itself takes around 24 seconds
    // BGR2-327
    sleep(5);
#ifdef DECOUPLE_GPS_CAM
    while(!open_port()) {
        LOG_I (TAG,"GPS port not available yet");
        sleep (1);
    }

    LOG_I(TAG, "GPS connected");
    config_port();
#endif

	while( enable_mask ) {

		buflen = read(fd, buf, 1);

                if( buflen == 0 && access(gps_port.c_str(), F_OK) == -1 ) {
                        /*Buflen is Zero and current GPS file node is not present,
                        Looks like modem crashed. Keep trying to open node until
                        modem is back alive*/

                        LOG_E(TAG, "GPS Device not found, trying to reconnect");
                        log_file("gps_crashed");

                        while( !open_port() ) {
                                LOG_E(TAG, "GPS Device not found");
                                sleep(1);
                        }

                        LOG_I(TAG, "GPS connected");
                        config_port();
                        i=0;
                        log_file("gps_revived");
                        continue;
                }

		if (buflen > 0 ) // got input bytes
		{
                        if( i>NMEA_LEN ) {
                                i=0;
                                continue;
                        }
			else if( buf[0] == '\r' ) {
				continue;
                        }
			else if( buf[0] == '\n' ) {
				data_line[i++] = '\0';
				parse(data_line);
				i=0;
                        }
                        else {
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
