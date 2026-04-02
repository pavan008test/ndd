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
#include <ublox_dev.h>

#include "Base64.h"

#define SYNC1 0xB5 
#define SYNC2 0x62

#define NMEA_SYNC1 0x24
#define NMEA_SYNC2 0x47

#define UBX_CFG_PORT_BUF_LEN 14
#define GNSS_CFG_MSG_BUF_LEN 68
#define TAG "UBLOX"
#define HDMAPS

#ifdef HDMAPS
#define CLOCK_TYPE CLOCK_MONOTONIC_RAW
#else
#define CLOCK_TYPE CLOCK_REALTIME
#endif

#define BUFFER_LEN 2048
using namespace std;

pthread_t th_data;

static const int64_t ONE_MICRO_IN_NANO = 1000;
static const float MM_PER_S_TO_MPH = 0.002236936f;
static struct UbloxDev *ublox_obj = NULL;//TODO static member
void *ublox_data_thread (void *);

UbloxDev::UbloxDev (string port_name) {
    this->port_name = port_name;
}

UbloxDev::~UbloxDev() {
    if (port->is_open()) {
        port->close();
    }
    delete port;
}


static Ublox::ublox_callback_t *ublox_cb=NULL;
static Ublox::ublox_gps_callback_t *gps_cb=NULL;
static Ublox::ublox_pps_callback_t *pps_cb=NULL;
static Ublox::ublox_constellation_callback_t *ublox_constellation_cb=NULL;
static int handle = 0;

static bool create_thread();
static void *read_thread( void *p );
static void *pps_record_thread (void *);
static void parse(char *buf);
static bool at_config();

static int fd;
static int enable_mask = 1;
static pthread_t gps_pps_thread;
static pthread_t gnss_config_thread;
bool gnss_config_change_in_progress = false;
typedef enum gnss_config_change_status_ {
   GNSS_CONFIG_NO_CHANGE= 0,
   GNSS_CONFIG_CHANGE_IN_PROGRESS,
   GNSS_CONFIG_CHANGE_DONE
} gnss_config_change_status;
gnss_config_change_status gnss_config_status = GNSS_CONFIG_NO_CHANGE;

static const int gps_interface = 2;
static const int fnamelen = 128;

static string gps_port = "";
static const int NMEA_LEN = 1024;
static const string gps_restart_log = "/home/ubuntu/.nddevice/log/gps_restart.log";

static bool first_fix = false;

//when module gps_pps_interrupt.ko is inserted (just before starting bagheera service), below file is created.
static const string GPS_PPS_FILE = "/sys/gps_pps_sysfs/gps_pps_timestamps";

using namespace std;

#define TAG "GPSD"

string get_ublox_port() {

        return UBLOX_PORT_NAME;
}

int ublox_dev_open(string name, string port_name, int baud_rate, bool send_ubx_cfg_msgs, bool log_ubx_msgs, bool disable_nmea_msgs, bool save_ubx_cfg, Ublox::ublox_constellation_data_t data) {

    ublox_obj = new UbloxDev (port_name);
    if (!ublox_obj) {
        return -1;
    }

    ublox_obj->write_ubx_cfg_msgs = send_ubx_cfg_msgs;
    ublox_obj->log_ubx_msgs = log_ubx_msgs;
    ublox_obj->disable_nmea_msgs = disable_nmea_msgs;
    ublox_obj->save_ubx_cfg = save_ubx_cfg;
    ublox_obj->config.enable_GPS = data.enable_GPS;
    ublox_obj->config.enable_SBAS = data.enable_SBAS;
    ublox_obj->config.enable_Galileo = data.enable_Galileo;
    ublox_obj->config.enable_BeiDou = data.enable_BeiDou;
    ublox_obj->config.enable_GLONASS = data.enable_GLONASS;
    ublox_obj->config.enable_IMES = data.enable_IMES;
    ublox_obj->config.enable_QZSS = data.enable_QZSS;
    ublox_obj->reset_ublox_for_gnss_conf = false;

    try {
        ublox_obj->port = new boost::asio::serial_port(ublox_obj->port_io);
        ublox_obj->port->open (ublox_obj->port_name);
        if (!ublox_obj->port->is_open()) {
            LOG_E (TAG, "Failed to open port %s", port_name.c_str());
            delete ublox_obj;
            ublox_obj = NULL;
            return -1;
        }
        ublox_obj->port->set_option(boost::asio::serial_port_base::baud_rate(baud_rate));
    }
    catch (...) {
        LOG_E (TAG, "Catch!!");
        delete ublox_obj;
        ublox_obj = NULL;
        return -1;
    }

    return handle++;
}

bool ublox_dev_close(int handle) {
	handle--;
	return true;
}

bool ublox_dev_config( int handle, string key, string value ) {
	return true;
}

bool ublox_dev_reg_cb( int handle, Ublox::ublox_callback_t *cb ) {
	if( cb == NULL ) {
		return false;
	}

	::ublox_cb = cb;
	return true;
}

bool ublox_dev_reg_gps_cb( int handle, Ublox::ublox_gps_callback_t *cb ) {
    if (cb == NULL) {
        return false;
    }
    ::gps_cb = cb;
    return true;
}


bool ublox_dev_reg_pps_cb( int handle, Ublox::ublox_pps_callback_t *cb ) {
    if (cb == NULL) {
        return false;
    }
    ::pps_cb = cb;
    return true;
}

bool ublox_dev_reg_ublox_constellation_cb( int handle, Ublox::ublox_constellation_callback_t *cb ) {
    if (cb == NULL) {
        return false;
    }
    ::ublox_constellation_cb = cb;
    return true;
}

bool ublox_dev_enable( int handle ) {
    ublox_obj->ublox_disable = false;

    if (pthread_create (&th_data, NULL, ublox_data_thread, (void*)ublox_obj) !=0 ) {
        LOG_E (TAG, "ublox_data_thread creation failed ");
        return false;
    }

    if(pthread_create(&gps_pps_thread, NULL, pps_record_thread, NULL)) {
        return false;
    }

    return true;
}

bool ublox_dev_disable( int handle ){
	ublox_obj->ublox_disable = true;
	if (!pthread_join (th_data, NULL)) {
		return true;
	}
	return false;
}

// Syncs to start of binary payload and returns message length
void UbloxDev::sync_to_packet(boost::asio::serial_port *serport) {
    bool gotSYNC1 = false;
    bool gotNMEASYNC1 = false;
    char buf;
    char nmea_buf[4];
    
    while (true) {
        boost::asio::read(*serport, boost::asio::buffer(&buf, 1));
        // Check for the two leading sync bytes
        if (buf == SYNC1)
            gotSYNC1 = true;
        else if ((buf == SYNC2) && gotSYNC1) {
            return;
        }
        else {
            gotSYNC1 = false;
        }

#if 1
        if (buf == NMEA_SYNC1)
            gotNMEASYNC1 = true;
        else if ((buf == NMEA_SYNC2) && gotNMEASYNC1) {
            boost::asio::read(*serport, boost::asio::buffer(&nmea_buf, 4));
            LOG_I (TAG, "NMEA message: %x %x %x %x %x %x", NMEA_SYNC1, NMEA_SYNC2, nmea_buf[0],nmea_buf[1], nmea_buf[2], nmea_buf[3]);
            return;
        }
        else {
            gotNMEASYNC1 = false;
        }
#endif
    }
}

static void checksum(unsigned char *buf, uint16_t payload_len, uint8_t CK_A_B[]) {
		uint8_t CK_A = 0;
		uint8_t CK_B = 0;
	
        // Compute checksum of the packet
        // start at i=2 to ignore header
        // add four extra bytes for class, id and length
        for (uint16_t i=2; i<(payload_len+4+2); i++) {
            CK_A = CK_A + (uint8_t)buf[i];
            CK_B = CK_B + CK_A;
        }
        
        CK_A_B[0] = CK_A;
        CK_A_B[1] = CK_B;
}

void UbloxDev::send_ubx_cfg_rate(uint16_t measRate=1000, uint16_t navRate=1, uint16_t timeRef=0) {
    uint8_t ubx_cfg_rate_class = 0x06;
    uint8_t ubx_cfg_rate_id = 0x08;
    unsigned char ubx_cfg_rate_buf[UBX_CFG_PORT_BUF_LEN] = {0};
    
    // Write the header
    ubx_cfg_rate_buf[0] = 0xB5;
    ubx_cfg_rate_buf[1] = 0x62;
    // Write class
    ubx_cfg_rate_buf[2] = ubx_cfg_rate_class;
    // Write ID
    ubx_cfg_rate_buf[3] = ubx_cfg_rate_id;
    // Write payload length
    ubx_cfg_rate_buf[4] = 0x06;
    // Write measRate
    ubx_cfg_rate_buf[6] = measRate;
    ubx_cfg_rate_buf[7] = measRate >> 8;
    // Write navRate
    ubx_cfg_rate_buf[8] = navRate; 
    ubx_cfg_rate_buf[9] = navRate >> 8;
    // Write time alignment reference (1=GPS, 0=UTC)
    ubx_cfg_rate_buf[10] = timeRef; 
    ubx_cfg_rate_buf[11] = timeRef >> 8;
    
    // Compute the checksum and store in the last two bytes
    uint8_t CK_A_B[2];
    checksum(ubx_cfg_rate_buf, 6, CK_A_B);
    ubx_cfg_rate_buf[12] = CK_A_B[0];
    ubx_cfg_rate_buf[13] = CK_A_B[1];

    // for (int i = 0; i < UBX_CFG_PORT_BUF_LEN; ++i)
    //     cout << std::hex << setfill('0') << setw(2) << (int)ubx_cfg_rate_buf[i] << " ";
    // cout << std::dec << endl;
    
    boost::asio::write(*(port), boost::asio::buffer(ubx_cfg_rate_buf, UBX_CFG_PORT_BUF_LEN));

}

void send_ubx_mon_gnss_poll_msg(boost::asio::serial_port *port)
{
    uint8_t ubx_cfg_msg_class = 0x0A;
    uint8_t ubx_cfg_msg_id = 0x28;

    uint8_t buffer [8] = {0};
    buffer[0] = 0xB5;
    buffer[1] = 0x62;
    buffer[2] = ubx_cfg_msg_class;
    buffer[3] = ubx_cfg_msg_id;
    buffer[4] = 0; //Payload len

    uint8_t CK_A_B[2];
    checksum(buffer, 0, CK_A_B);

    buffer[6] = CK_A_B[0];
    buffer[7] = CK_A_B[1];
    boost::asio::write(*(port), boost::asio::buffer(buffer, 8));

}

static void prepare_and_send (boost::asio::serial_port *port, uint8_t Class, uint8_t id, uint8_t rate=1) {
    uint8_t ubx_cfg_msg_class = 0x06;
    uint8_t ubx_cfg_msg_id = 0x01;

    uint8_t buffer [11] = {0};
    buffer[0] = 0xB5;
    buffer[1] = 0x62;
    buffer[2] = ubx_cfg_msg_class;
    buffer[3] = ubx_cfg_msg_id;
    buffer[4] = 3; //Payload len
    buffer[6] = Class; // TIM TP class
    buffer[7] = id; //TIM TP id
    buffer[8] = rate; // Rate 1Hz

    uint8_t CK_A_B[2];
    checksum(buffer, 3, CK_A_B);

    buffer[9] = CK_A_B[0];
    buffer[10] = CK_A_B[1];
    boost::asio::write(*(port), boost::asio::buffer(buffer, 11));
}

static void send_tim_tp_msg (boost::asio::serial_port *port) {
    prepare_and_send (port, 0x0D, 0x01, 0x1);
}

static void send_rxm_rawx_msg (boost::asio::serial_port *port) {
    prepare_and_send (port, 0x02, 0x15, 0x1);
}

static void send_rxm_sfrbx_msg (boost::asio::serial_port *port) {
    prepare_and_send (port, 0x02, 0x13, 0x1);
}

static void send_nav_timeutc_msg (boost::asio::serial_port *port) {
    prepare_and_send (port, 0x01, 0x21, 0x1);
}

static void send_nav_pvt_msg (boost::asio::serial_port *port) {
    prepare_and_send (port, 0x01, 0x07, 0x1);
}

void UbloxDev::save_ublox_configs () {

    uint8_t ubx_cfg_cfg_class = 0x06;
    uint8_t ubx_cfg_cfg_id = 0x09;

    uint8_t buffer [21] = {0};
    buffer[0] = 0xB5;
    buffer[1] = 0x62;
    buffer[2] = ubx_cfg_cfg_class;
    buffer[3] = ubx_cfg_cfg_id;
    buffer[4] = 0xD; //Payload len
    buffer[6] = 0x00;
    buffer[7] = 0x00;
    buffer[8] = 0x00;
    buffer[9] = 0x00;
    buffer[10] = 0x00;
    buffer[11] = 0x00;
    buffer[12] = 0x00;
    buffer[13] = 0x02;
    buffer[14] = 0x00;
    buffer[15] = 0x00;
    buffer[16] = 0x00;
    buffer[17] = 0x00;
    buffer[18] = 0x02;

    uint8_t CK_A_B[2];
    checksum(buffer, 3, CK_A_B);

    buffer[19] = CK_A_B[0];
    buffer[20] = CK_A_B[1];
    LOG_I (TAG, "Saving configurations to Ublox");
    boost::asio::write(*(port), boost::asio::buffer(buffer, 21));

}

static void set_nmea_msg_rate (boost::asio::serial_port *port, uint8_t msg_rate) {

        uint8_t Class = 0xf0;
        uint8_t id = 0x00;

        for (; id <= 0xa; id++) {
            prepare_and_send (port, Class, id, msg_rate);
        }

        prepare_and_send (port, Class, 0x0d, msg_rate);
        prepare_and_send (port, Class, 0x0f, msg_rate);
}

static void disable_nmea_msgs_func (boost::asio::serial_port *port) {
    set_nmea_msg_rate (port, 0);
}

static void enable_nmea_msgs_func (boost::asio::serial_port *port) {
    set_nmea_msg_rate (port, 1);
}

void UbloxDev::disable_ubx_nmea_msgs() {

    //Disable NMEA MSGS
    if (disable_nmea_msgs) {
        disable_nmea_msgs_func (port);
    }
    if (save_ubx_cfg) {
        save_ublox_configs ();
    }
}

void UbloxDev::enable_ubx_nmea_msgs() {

    //ENABLE NMEA MSGS
    if (!disable_nmea_msgs) {
        enable_nmea_msgs_func (port);
    }
    if (save_ubx_cfg) {
        save_ublox_configs ();
    }
}

void UbloxDev::send_ubx_cfg_msgs() {

    uint8_t *buffer;
    int buf_length = 0;

    //TIM TP
    LOG_I (TAG, "Enabling TIM-TP");
    send_tim_tp_msg(port);

    //UBX-RXM-RAWX
    LOG_I (TAG, "Enabling RXM-RAWX");
    send_rxm_rawx_msg (port);

    //UBX-RXM-SFRBX
    LOG_I (TAG, "Enabling RXM-SFRBX");
    send_rxm_sfrbx_msg (port);

    //UBX-NAV-TIMEUTC
    LOG_I (TAG, "Enabling NAV-TIMEUTC");
    send_nav_timeutc_msg (port);

    //UBX-NAV-PVT
    LOG_I (TAG, "Enabling NAV-PVT");
    send_nav_pvt_msg (port);

    if (save_ubx_cfg) {
        save_ublox_configs ();
    }
}

static bool first_pps_pushed = false;

static void do_gps_cb (Ublox::ublox_gps_data_t data, uint64_t raw_time_ns, double altitudeMSL) {
    if (!gps_cb) {
        return;
    }

    static bool first_gps_data_pushed = false;
    static uint64_t ublox_gps_index = 0;
    static Ublox::ublox_gps_data_t first_gps_to_push;

    if (!first_pps_pushed) {
        first_gps_to_push = data;
        LOG_I (TAG, "First PPS is not yet pushed. Return");
        //return;
    }
    if (!first_gps_data_pushed) {
        LOG_I (TAG, "Pushing first GPS data. Index: %llu", ublox_gps_index);
        gps_cb (first_gps_to_push, ublox_gps_index, raw_time_ns, altitudeMSL);
        first_gps_data_pushed = true;
    }
    //monotonic time is in nanos
    //data.system_monotonic_time = data.system_monotonic_time/ONE_MICRO_IN_NANO;

    ++ublox_gps_index;
    gps_cb (data, ublox_gps_index, raw_time_ns, altitudeMSL);
}

void do_pps_cb (Ublox::ublox_pps_data_t pps_data) {

    if (!pps_cb) {
        return;
    }

    static uint64_t pps_index = 0;
    static Ublox::ublox_pps_data_t first_pps_to_push;
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

int parse_nav_pvt(struct NavPvt *pvtData, uint8_t *result, size_t bytes_read)
{
	uint16_t length = bytes_read;
	uint8_t *payload;
	LOG_D(TAG, "%s: bytes_read = %d, first byte: 0x%x", __func__, bytes_read, *(result));
	LOG_D(TAG, "length = %u\n",length);
	if (length < 92) {
		LOG_I(TAG, "Incomplete pvt message received\n");
		return FAILURE;
	}
	LOG_D(TAG, "Entered parsing UBX-NAV-PVT Parameters\n");
	//payload = (uint8_t *)&result[ii + IDX6];
	payload = (uint8_t *)result;
	pvtData->iTOW = payload[0] + (payload[1] << VAL_8BIT) + (payload[2] << VAL_16BIT) + (payload[3] << VAL_24BIT);
	pvtData->year = payload[4] + (payload[5] << VAL_8BIT);
	pvtData->month = payload[6];
	pvtData->day = payload[7];
	pvtData->hour = payload[8];
	pvtData->min = payload[9];
	pvtData->sec = payload[10];
	LOG_D(TAG, "Year = %d, month = %d, day = %d, hour = %d, min = %d, sec = %d",
		    pvtData->year, pvtData->month, pvtData->day, pvtData->hour, pvtData->min, pvtData->sec);
	pvtData->valid = payload[11];
	pvtData->tAcc = payload[12] + (payload[13] << VAL_8BIT) + (payload[14] << VAL_16BIT) + (payload[15] << VAL_24BIT);
	pvtData->nano = payload[16] + (payload[17] << VAL_8BIT) + (payload[18] << VAL_16BIT) + (payload[19] << VAL_24BIT);
	pvtData->fixType = payload[20];
	pvtData->flags = payload[21];
	pvtData->flags2 = payload[22];
	pvtData->numSV = payload[23];
	pvtData->lon = payload[24] + (payload[25] << VAL_8BIT) + (payload[26] << VAL_16BIT) + (payload[27] << VAL_24BIT);
	pvtData->lat = payload[28] + (payload[29] << VAL_8BIT) + (payload[30] << VAL_16BIT) + (payload[31] << VAL_24BIT);
	pvtData->height = payload[32] + (payload[33] << VAL_8BIT) + (payload[34] << VAL_16BIT) + (payload[35] << VAL_24BIT);
	pvtData->hMSL = payload[36] + (payload[37] << VAL_8BIT) + (payload[38] << VAL_16BIT) + (payload[39] << VAL_24BIT);
	pvtData->hAcc = payload[40] + (payload[41] << VAL_8BIT) + (payload[42] << VAL_16BIT) + (payload[43] << VAL_24BIT);
	pvtData->vAcc = payload[44] + (payload[45] << VAL_8BIT) + (payload[46]  << VAL_16BIT) + (payload[47] << VAL_24BIT);
	pvtData->velN = payload[48] + (payload[49] << VAL_8BIT) + (payload[50]  << VAL_16BIT) + (payload[51] << VAL_24BIT);
	pvtData->velE = payload[52] + (payload[53] << VAL_8BIT) + (payload[54]  << VAL_16BIT) + (payload[55] << VAL_24BIT);
	pvtData->velD = payload[56] + (payload[57] << VAL_8BIT) + (payload[58]  << VAL_16BIT) + (payload[59] << VAL_24BIT);
	pvtData->gSpeed = payload[60] + (payload[61] << VAL_8BIT) + (payload[62]  << VAL_16BIT) + (payload[63] << VAL_24BIT);
	pvtData->headMot = payload[64] + (payload[65] << VAL_8BIT) + (payload[66]  << VAL_16BIT) + (payload[67] << VAL_24BIT);
	pvtData->sAcc = payload[68] + (payload[69] << VAL_8BIT) + (payload[70]  << VAL_16BIT) + (payload[71] << VAL_24BIT);
	pvtData->headAcc = payload[72] + (payload[73] << VAL_8BIT) + (payload[74] << VAL_16BIT) + (payload[75] << VAL_24BIT);
	pvtData->pDOP = payload[76] + (payload[77] << VAL_8BIT);
	pvtData->flags3 = payload[78];
	pvtData->headVeh = payload[84] + (payload[85] << VAL_8BIT) + (payload[86] << VAL_16BIT) + (payload[87] << VAL_24BIT);
	pvtData->magDec = payload[88] + (payload[89] << VAL_8BIT);
	pvtData->headVeh = payload[90] + (payload[91] << VAL_8BIT);
	LOG_D(TAG, "pvt parsed lat %d lon %d heught %d\n",pvtData->lat, pvtData->lon, pvtData->height);
	return SUCCESS;
}

void *send_gnss_config_message_thread(void *arg)
{
    gnss_config_change_in_progress = true; // to stop invoke more than one thread

    UbloxDev *obj = (UbloxDev*)arg;
    unsigned char gnss_cfg_msg_buf[GNSS_CFG_MSG_BUF_LEN] = {0};
    uint8_t header[2] = {0xB5, 0x62};
    uint8_t gnss_cfg_class = 0x06;
    uint8_t gnss_cfg_id = 0x3E;

    gnss_cfg_msg_buf[0] = header[0];
    gnss_cfg_msg_buf[1] = header[1];
    gnss_cfg_msg_buf[2] = gnss_cfg_class;
    gnss_cfg_msg_buf[3] = gnss_cfg_id;
    gnss_cfg_msg_buf[4] = 60;       //msg_len
    gnss_cfg_msg_buf[6] = 0x00;     //msg_ver
    gnss_cfg_msg_buf[7] = 0x00;     //numTrkChHw - readonly
    gnss_cfg_msg_buf[8] = 0xFF;     //numTrkChUse
    gnss_cfg_msg_buf[9] = 7;        //numConfigBlocks
    // GPS config
    gnss_cfg_msg_buf[10] = 0;        //gnssid
    gnss_cfg_msg_buf[11] = 8;       //resTrkCh - readonly
    gnss_cfg_msg_buf[12] = 16;      //maxTrkCh - readonly
    gnss_cfg_msg_buf[13] = 0x00;    //reserved
    gnss_cfg_msg_buf[14] = (obj->config.enable_GPS == 1) ? 0x01 : 0x00;    //enable - flag 0
    gnss_cfg_msg_buf[15] = 0x00;    //reserved byte - flag 1
    gnss_cfg_msg_buf[16] = 0x01;    //sigCfgMask - flag 2
    gnss_cfg_msg_buf[17] = 0x00;    //reserved byte - flag 3

    // SBAS config
    gnss_cfg_msg_buf[18] = 1;        //gnssid
    gnss_cfg_msg_buf[19] = 1;    //resTrkCh - readonly
    gnss_cfg_msg_buf[20] = 2;    //maxTrkCh - readonly
    gnss_cfg_msg_buf[21] = 0x00;    //reserved
    gnss_cfg_msg_buf[22] = (obj->config.enable_SBAS == 1) ? 0x01 : 0x00;    //enable - flag 0
    gnss_cfg_msg_buf[23] = 0x00;    //reserved byte - flag 1
    gnss_cfg_msg_buf[24] = 0x01;    //sigCfgMask - flag 2
    gnss_cfg_msg_buf[25] = 0x00;    //reserved byte - flag 3

    // Galileo config
    gnss_cfg_msg_buf[26] = 2;        //gnssid
    gnss_cfg_msg_buf[27] = 4;    //resTrkCh - readonly
    gnss_cfg_msg_buf[28] = 8;    //maxTrkCh - readonly
    gnss_cfg_msg_buf[29] = 0x00;    //reserved
    gnss_cfg_msg_buf[30] = (obj->config.enable_Galileo == 1) ? 0x01 : 0x00;    //enable - flag 0
    gnss_cfg_msg_buf[31] = 0x00;    //reserved byte - flag 1
    gnss_cfg_msg_buf[32] = 0x01;    //sigCfgMask - flag 2
    gnss_cfg_msg_buf[33] = 0x00;    //reserved byte - flag 3

    //BeiDou config
    gnss_cfg_msg_buf[34] = 3;        //gnssid
    gnss_cfg_msg_buf[35] = 8;    //resTrkCh - readonly
    gnss_cfg_msg_buf[36] = 16;    //maxTrkCh - readonly
    gnss_cfg_msg_buf[37] = 0x00;    //reserved
    gnss_cfg_msg_buf[38] = (obj->config.enable_BeiDou == 1) ? 0x01 : 0x00;    //enable - flag 0
    gnss_cfg_msg_buf[39] = 0x00;    //reserved byte - flag 1
    gnss_cfg_msg_buf[40] = 0x01;    //sigCfgMask - flag 2
    gnss_cfg_msg_buf[41] = 0x00;    //reserved byte - flag 3

    //IMES config
    gnss_cfg_msg_buf[42] = 4;        //gnssid
    gnss_cfg_msg_buf[43] = 0;    //resTrkCh - readonly
    gnss_cfg_msg_buf[44] = 16;    //maxTrkCh - readonly
    gnss_cfg_msg_buf[45] = 0x00;    //reserved
    gnss_cfg_msg_buf[46] = (obj->config.enable_IMES == 1) ? 0x01 : 0x00; // enable - flag 0
    gnss_cfg_msg_buf[47] = 0x00;    //reserved byte - flag 1
    gnss_cfg_msg_buf[48] = 0x01;    //sigCfgMask - flag 2
    gnss_cfg_msg_buf[49] = 0x00;    //reserved byte - flag 3

    //QZSS config
    gnss_cfg_msg_buf[50] = 5;        //gnssid
    gnss_cfg_msg_buf[51] = 0;    //resTrkCh - readonly
    gnss_cfg_msg_buf[52] = 8;    //maxTrkCh - readonly
    gnss_cfg_msg_buf[53] = 0x00;    //reserved
    gnss_cfg_msg_buf[54] = (obj->config.enable_QZSS == 1) ? 0x01 : 0x00; // enable - flag 0
    gnss_cfg_msg_buf[55] = 0x00;    //reserved byte - flag 1
    gnss_cfg_msg_buf[56] = 0x01;    //sigCfgMask - flag 2
    gnss_cfg_msg_buf[57] = 0x00;    //reserved byte - flag 3

    //GLONASS config
    gnss_cfg_msg_buf[58] = 6;        //gnssid
    gnss_cfg_msg_buf[59] = 8;    //resTrkCh - readonly
    gnss_cfg_msg_buf[60] = 14;    //maxTrkCh - readonly
    gnss_cfg_msg_buf[61] = 0x00;    //reserved
    gnss_cfg_msg_buf[62] = (obj->config.enable_GLONASS == 1) ? 0x01 : 0x00;    //enable - flag 0
    gnss_cfg_msg_buf[63] = 0x00;    //reserved byte - flag 1
    gnss_cfg_msg_buf[64] = 0x01;    //sigCfgMask - flag 2
    gnss_cfg_msg_buf[65] = 0x00;    //reserved byte - flag 3

    // Compute the checksum and store in the last two bytes
    uint8_t CK_A_B[2];
    memset(CK_A_B, 0, 2);
    checksum(gnss_cfg_msg_buf, 60, CK_A_B);
    gnss_cfg_msg_buf[66] = CK_A_B[0];
    gnss_cfg_msg_buf[67] = CK_A_B[1];

    LOG_I(TAG, "sending UBX-CFG-GNSS message");
    boost::asio::write(*(obj->port), boost::asio::buffer(gnss_cfg_msg_buf, 68));

    sleep(2); // After sending UBX-CFG-GNSS message it will take some time to apply the changes

    // Send Reset Message UBX-CFG-RST for Cold Start
    if (obj->reset_ublox_for_gnss_conf) {
        uint8_t reset_cfg_class = 0x06;
        uint8_t reset_cfg_id = 0x04;
        unsigned char reset_msg_buf[12] = {0};

        reset_msg_buf[0] = header[0];
        reset_msg_buf[1] = header[1];
        reset_msg_buf[2] = reset_cfg_class;
        reset_msg_buf[3] = reset_cfg_id;
        reset_msg_buf[4] = 4;
        reset_msg_buf[6] = 0xFFFF;
        reset_msg_buf[8] = 0x00;
        memset(CK_A_B, 0, 2);
        checksum(reset_msg_buf, 4, CK_A_B);
        reset_msg_buf[10] = CK_A_B[0];
        reset_msg_buf[11] = CK_A_B[0];
        LOG_I(TAG, "sending UBX-CFG-RST message for Cold Start");
        boost::asio::write(*(obj->port), boost::asio::buffer(reset_msg_buf, 12));

        obj->reset_ublox_for_gnss_conf = false;
        sleep(10);
    }

    LOG_I (TAG, "Polling UBX-MON-GNSS");
    send_ubx_mon_gnss_poll_msg (obj->port);
    gnss_config_status = GNSS_CONFIG_CHANGE_IN_PROGRESS;

}

void *ublox_data_thread (void *arg) {

    UbloxDev *obj = (UbloxDev*)arg;
    char buf[BUFFER_LEN];
    char crc_buf[2], class_id_len_buf[4];
    uint8_t uclass, uID, CK_A, CK_B;
    uint16_t payload_len, i;
    struct timespec ts;
    struct timespec ts_wallclock;
    struct NavPvt pvtData;
    struct AckMsg *ack_data = NULL;
    Ublox::ublox_data_t ublox_data;
    Ublox::ublox_gps_data_t ublox_gps_data;
    Ublox::ublox_pps_data_t ublox_pps_data;
    Ublox::ublox_constellation_data_t ublox_constellation_configs;

    gnss_config_change_in_progress = false; 

    LOG_D (TAG,"ublox_data_thread");

    if (obj->write_ubx_cfg_msgs) {
        LOG_I (TAG, "Sending UBX-CFG-MSG");
        obj->send_ubx_cfg_msgs();
    }

    LOG_D (TAG, "Polling UBX-MON-GNSS");
    send_ubx_mon_gnss_poll_msg (obj->port);

    if (obj->disable_nmea_msgs) {
        LOG_I (TAG,"Disabling NMEA messages");
        obj->disable_ubx_nmea_msgs();
    }
    else {
        obj->enable_ubx_nmea_msgs();
    }

    obj->send_ubx_cfg_rate();

    bool is_nav_pvt = false, print_ack_msg = false, print_gnss_msg = false, print_nak_msg = false;

    // read a GNSS packet at a time
    while (!obj->ublox_disable) {
        obj->sync_to_packet(obj->port);
            
        // Record the time the message arrived
        clock_gettime(CLOCK_TYPE, &ts);
        clock_gettime(CLOCK_REALTIME, &ts_wallclock);
        ublox_data.system_time = ts.tv_sec * 1000000000 + ts.tv_nsec;
        ublox_data.clock_time = ts_wallclock.tv_sec * 1000000000 + ts_wallclock.tv_nsec;
        
        // Read class, ID and length of the message
        boost::asio::read(*(obj->port), boost::asio::buffer(&class_id_len_buf, 4));
        uclass = (uint8_t)class_id_len_buf[0];
        uID = (uint8_t)class_id_len_buf[1];
        payload_len = (((uint16_t)class_id_len_buf[3]) << 8) | class_id_len_buf[2];

        ublox_data.Class = uclass;
        ublox_data.id = uID;
        ublox_data.length = payload_len;

        if (obj->log_ubx_msgs) {
            if (uclass == 0x0d && uID == 0x01) {
                LOG_D (TAG, "Received UBX-TIM-TP message\n");
            }
            else if (uclass == 0x02 && uID == 0x15) {
                LOG_D (TAG, "Received UBX-RXM-RAWX message\n");
            }

            else if (uclass == 0x02 && uID == 0x13) {
                LOG_D (TAG, "Received UBX-RXM-SFRBX message\n");
            }

            else if (uclass == 0x01 && uID == 0x07) {
                LOG_D (TAG, "Received UBX-NAV-PVT message\n");
                is_nav_pvt = true;
            }

            else if (uclass == 0x01 && uID == 0x21) {
                LOG_D (TAG, "Received UBX-NAV-TIMEUTC message\n");
            }

            else if (uclass == 0xf0) {
                LOG_D (TAG, "Received NMEA message, id: %u", uID);
            }
            else if (uclass == 0x05 && uID == 0x01) {
                print_ack_msg = true;
            }
            else if (uclass == 0x0A && uID == 0x28) {
                LOG_D (TAG, "Received UBX-MON-GNSS message");
                print_gnss_msg = true;
                if(gnss_config_status == GNSS_CONFIG_CHANGE_IN_PROGRESS)
                    gnss_config_status = GNSS_CONFIG_CHANGE_DONE;
            }
            else if(uclass == 0x05 && uID == 0x00) {
                print_nak_msg = true;
                LOG_D(TAG, "Received UBX-ACK-NAK message");
            }
            else {
                LOG_D (TAG, "Received message, class: %u\tid: %u", uclass, uID);
            }
        }

        if (payload_len > BUFFER_LEN) {
            LOG_I (TAG,"UbloxDev::capture(): Payload length of %hu too long", payload_len);
            continue;
        }
        
        // Read the entire binary payload
        boost::asio::read(*(obj->port), boost::asio::buffer(&buf, payload_len));
        
        // Read the CRC
        boost::asio::read(*(obj->port), boost::asio::buffer(&crc_buf, 2));
        // Compute checksum of the packet
        CK_A = 0;
        CK_B = 0;
        // class
        CK_A = CK_A + uclass;
        CK_B = CK_B + CK_A;
        // ID
        CK_A = CK_A + uID;
        CK_B = CK_B + CK_A;
        // length
        CK_A = CK_A + (uint8_t)class_id_len_buf[2];
        CK_B = CK_B + CK_A;
        CK_A = CK_A + (uint8_t)class_id_len_buf[3];
        CK_B = CK_B + CK_A;
        // payload
        for (i=0; i<payload_len; i++) {
            CK_A = CK_A + (uint8_t)buf[i];
            CK_B = CK_B + CK_A;
        }
        if ((CK_A != (uint8_t)crc_buf[0]) || (CK_B != (uint8_t)crc_buf[1])) {
            LOG_E(TAG,"Checksum mismatch. Got: %u,%u expected: %u,%u",(unsigned int)CK_A,(unsigned int)CK_B, (unsigned int)crc_buf[0], (unsigned int)crc_buf[1]);
            continue;
        }
        
        if(print_ack_msg) {
            print_ack_msg = false;
            ack_data = (struct AckMsg *)buf;
            LOG_I(TAG, "Received UBX-ACK-ACK message from clsID : 0x%02X\tmsgID : 0x%02X", ack_data->clsID, ack_data->msgID);
            if(ack_data->clsID == 0x06 && ack_data->msgID == 0x3E) {
                LOG_I(TAG, "UBX-CFG-GNSS : Received ACK message");
            }
            ack_data = NULL;
        }

       if(print_nak_msg) {
            print_nak_msg = false;
            ack_data = (struct AckMsg *)buf;
            LOG_I(TAG, "Received UBX-ACK-NAK message from clsID : 0x%02X\tmsgID : 0x%02X", ack_data->clsID, ack_data->msgID);
            if(ack_data->clsID == 0x06 && ack_data->msgID == 0x3E) {
                LOG_I(TAG, "UBX-CFG-GNSS : Received ACK-NAK message");
            }
            ack_data = NULL;
        }

       if(print_gnss_msg) {
            print_gnss_msg = false;
            uint8_t version = buf[0];
            uint8_t supported = buf[1];
            uint8_t defaultGnss = buf[2];
            uint8_t enabled = buf[3];
            uint8_t simultaneous = buf[4];
            bool GPSEna = false, GlonassEna = false, BeidouEna = false, GalileoEna = false;
            //bit 0 : GPS is enabled, bit 1 : GLONASS is enabled, bit 2 : BeiDou is enabled, bit 4 : Galileo is enabled
            LOG_I(TAG, "UBX-MON-GNSS : enabled = %d %d %d %d %d %d %d %d",
                                                    (enabled >> 7 & 1),
                                                    (enabled >> 6 & 1),
                                                    (enabled >> 5 & 1),
                                                    (enabled >> 4 & 1),
                                                    (enabled >> 3 & 1),
                                                    (enabled >> 2 & 1),
                                                    (enabled >> 1 & 1),
                                                    (enabled >> 0 & 1));
            if (!gnss_config_change_in_progress) {

                GPSEna = (enabled >> 0 & 1) ? true : false;
                GlonassEna = (enabled >> 1 & 1) ? true : false;
                BeidouEna = (enabled >> 2 & 1) ? true : false;
                GalileoEna = (enabled >> 3 & 1) ? true : false;

                if((obj->config.enable_GPS != GPSEna) || (obj->config.enable_GLONASS != GlonassEna) || 
                        (obj->config.enable_BeiDou != BeidouEna) || (obj->config.enable_Galileo != GalileoEna)){
                    LOG_I(TAG, "UBX-MON-GNSS : GNSS config is changed");
                    obj->reset_ublox_for_gnss_conf = true;
                } else {
                    LOG_I(TAG, "UBX-MON-GNSS : GNSS config is same as config file");
                }

                LOG_I(TAG, "UBX-MON-GNSS : GNSS config to be applied");
                if(pthread_create(&gnss_config_thread, NULL, send_gnss_config_message_thread, (void*)ublox_obj)) {
                    LOG_E(TAG, "UBX-MON-GNSS : send_gnss_config_message_thread thread creation failed");
                }
            } else {
                LOG_I(TAG, "UBX-MON-GNSS : GNSS config already done");
            }
            if(gnss_config_status == GNSS_CONFIG_CHANGE_DONE)
            {
                memset(&ublox_constellation_configs, 0, sizeof(ublox_constellation_configs));
                ublox_constellation_configs.enable_GPS = (enabled >> 0 & 1) ? true : false;
                ublox_constellation_configs.enable_GLONASS = (enabled >> 1 & 1) ? true : false;
                ublox_constellation_configs.enable_BeiDou = (enabled >> 2 & 1) ? true : false;
                ublox_constellation_configs.enable_Galileo = (enabled >> 3 & 1) ? true : false;
                //TODO:: SBAS, QZSS, IMES values need to read in other polling method.
                ublox_constellation_configs.enable_SBAS = (obj->config.enable_SBAS == 1) ? true : false;
                ublox_constellation_configs.enable_IMES = (obj->config.enable_IMES == 1) ? true : false;
                ublox_constellation_configs.enable_QZSS = (obj->config.enable_QZSS == 1) ? true : false;
                // GNSS configuration has changed
                gnss_config_status = GNSS_CONFIG_NO_CHANGE;
                LOG_I(TAG, "UBX-MON-GNSS : GNSS config changed updating to metadata");
                ublox_constellation_cb(ublox_constellation_configs);
            }

        }

        // gps callback
        if(is_nav_pvt == true) {
            double altitudeMSL;
            is_nav_pvt = false;
            parse_nav_pvt(&pvtData, (uint8_t *)buf, payload_len);

            memset(&ublox_gps_data, 0, sizeof(ublox_gps_data));
            ublox_gps_data.valid = (pvtData.flags & 0x1) ? true : false; // checking gnssFixOK bit in flags field
            ublox_gps_data.latitude = (double)pvtData.lat / 1e7; // scaling factor for lat is 1e-7
            ublox_gps_data.longitude = (double)pvtData.lon / 1e7; // scaling factor for lat is 1e-7
            ublox_gps_data.altitude = (double)pvtData.height / 1000; // Height above Ellipsoid is given in mm
            ublox_gps_data.speed = (float)pvtData.gSpeed * MM_PER_S_TO_MPH ; // gSpeed is given in mm/sec
            ublox_gps_data.bearing = (float)pvtData.headMot / 1e5; // scaling factor is 1e-5
            ublox_gps_data.accuracy = (float)pvtData.hAcc / 1000; // hAcc is given in mm
            ublox_gps_data.timestamp = 1000 * make_epoch_time_from_utc(pvtData.year, pvtData.month, pvtData.day,
			   				       pvtData.hour, pvtData.min, pvtData.sec); // UTC time is in packet
            ublox_gps_data.system_timestamp = (int64_t)ublox_data.clock_time; //  iTOW is nav epoch
            altitudeMSL = (double)pvtData.hMSL / 1000; // hMSL is given in mm
            do_gps_cb(ublox_gps_data, ublox_data.system_time, altitudeMSL);
            
        }

	// Base-64 encode the binary payload
        ublox_data.payload = encode64(string(buf, payload_len));
        if (ublox_cb) {
            ublox_cb (ublox_data);
        }

    }
}

static bool create_thread() {

    if(pthread_create(&gps_pps_thread, NULL, pps_record_thread, NULL)) {
        return false;
    }
    return true;
}

static void *pps_record_thread (void *) {

    char buffer[100] = {'\0'};
    int64_t pps_raw_time = 0, pps_clock_time = 0;
    struct pollfd ufds[1];
    int poll_timeout = -1;
    int poll_rv = -1;
    Ublox::ublox_pps_data_t pps_data;
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


