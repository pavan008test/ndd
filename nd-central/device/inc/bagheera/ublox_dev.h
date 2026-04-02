/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Arun V <arun.vj@netradyne.com>, October 2016
 */

#ifndef UBLOX_DEV_H
#define UBLOX_DEV_H

#include <ublox.h>
#include <string>
#include <pthread.h>
#include <fstream>
#include <boost/asio/serial_port.hpp>
#include <boost/asio.hpp>
#include <time.h>

// Message information
#define UBLOX_PORT_NAME "/dev/ttyACM0"
#define UBLOX_BAUD_RATE 921600

#define UBLOX_DEVICE		"/dev/ttyACM0"
#define BAUD_RATE		9600
#define MAXIMUM_RETRY		5
#define PPS_GPIO_VALUE     	"/sys/class/gpio/gpio336/value"
#define UBX_SYNC_BYTE_1		0xB5
#define UBX_SYNC_BYTE_2		0x62
#define PPS_INT                 336
#define MSG_CLASS_CFG		0x06
#define MSG_ID_CFG_PRT		0x00
#define MSG_CLASS_MON		0x0A
#define MSG_ID_MON_VER		0x04
#define MSG_ID_NAV_STATUS	0x03
#define MSG_ID_NAV_DOP		0x04
#define MSG_ID_NAV_PVT      0x07
#define MSG_CLASS_NAV		0x01
#define HDR_CHKSM_LENGTH	8
#define UBX_POLL_MSG_LEN	4
#define UBX_CFG_PORT_MSG_LEN 27

#define NAV_STATUS		259

#define MAXIMUM_BUF		5000
#define MSG_CLASS		0x01

/*GPS Fix types*/

#define DEAD_RECKONING		0x01
#define TWO_D_FIX		0x02
#define THREE_D_FIX		0x03
#define GPS_DEAD_RECKONING	0x04
#define TIMEONLY		0x05
#define USB_PORT_ID		3
#define UART_PORT_ID	1

#define SUCCESS			 0
#define OFFSET_VAL		0X00
#define MASK_BYTE		0XFF
#define ZERO 			SUCCESS
#define MSG_PAYLOAD_LENGTH 	20
#define BUF_SIZE_ZERO 		0
#define BUF_SIZE_ONE		1
#define BUF_SIZE_TWO		2
#define BUF_SIZE_THREE		3
#define BUF_SIZE_FOUR		4
#define BUF_SIZE_FIVE		5
#define BUF_SIZE_SIX		6
#define BUF_SIZE_SEVEN      7
#define BUF_SIZE_EIGHT		8
#define BUF_SIZE_NINE		9
#define IDX0		        BUF_SIZE_ZERO
#define IDX1		        BUF_SIZE_ONE
#define IDX2		        BUF_SIZE_TWO
#define IDX3		        BUF_SIZE_THREE
#define IDX4		        BUF_SIZE_FOUR
#define IDX5		        BUF_SIZE_FIVE
#define IDX6		        BUF_SIZE_SIX
#define IDX7		        BUF_SIZE_SEVEN
#define IDX8		        BUF_SIZE_EIGHT
#define IDX9		        BUF_SIZE_NINE
#define IDX10		        10
#define IDX11		        11
#define IDX12		        12
#define IDX13		        13
#define IDX14		        14
#define IDX15		        15
#define IDX16		        16
#define IDX17		        17
#define IDX18		        18
#define IDX19		        19
#define IDX20		        20
#define IDX21		        21
#define IDX22		        22
#define IDX23		        23
#define IDX24		        24
#define VAL_8BIT		8
#define VAL_16BIT		16
#define VAL_24BIT		24
#define MAX_LIMIT		100
#define MIN_LIMIT		10
#define DIV			1000
#define UBX_POLL_MSG_SIZE   8

static const string UBLOX_GPS_AUTO_RESTART_CMD =  "lte_gps_sample_app 'AT!GPSAUTOSTART=1,1,250,250,1'";
static const string UBLOX_GPS_AUTO_RESTART_CMD2 =  "lte_gps_sample_app 'AT!GPSAUTOSTART?'" ;

struct UbloxHeader {
	uint8_t sync1;   //!< start of packet first byte (0xB5)
	uint8_t sync2;   //!< start of packet second byte (0x62)
	uint8_t message_class; //!< Class that defines basic subset of message (NAV, RXM, etc.)
	uint8_t message_id;		//!< Message ID
	uint16_t payload_length; //!< length of the payload data, excluding header and checksum
};

struct NavPvt{
    struct UbloxHeader header;
    uint32_t iTOW;          // GPS Millisecond time of week [ms]
    uint16_t year;          // Year (UTC)
    uint8_t month;          // Month, range 1..12 (UTC)
    uint8_t day;            // Day of month, range 1..31 (UTC)
    uint8_t hour;           // Hour of day, range 0..23 (UTC)
    uint8_t min;            // Minute of hour, range 0..59 (UTC)
    uint8_t sec;            // Seconds of minute, range 0..60 (UTC)

    uint8_t valid;          // Validity flags

    uint32_t tAcc;          // time accuracy estimate [ns] (UTC)
    int32_t nano;           // fraction of a second [ns], range -1e9 .. 1e9 (UTC)

    uint8_t fixType;        // GNSS fix Type, range 0..5

    uint8_t flags;          // Fix Status Flags

    uint8_t flags2;         // Additional Flags
    uint8_t numSV;          // Number of SVs used in Nav Solution
    int32_t lon;            // Longitude [deg / 1e-7]
    int32_t lat;            // Latitude [deg / 1e-7]
    int32_t height;         // Height above Ellipsoid [mm]
    int32_t hMSL;           // Height above mean sea level [mm]
    uint32_t hAcc;          // Horizontal Accuracy Estimate [mm]
    uint32_t vAcc;          // Vertical Accuracy Estimate [mm]

    int32_t velN;           // NED north velocity [mm/s]
    int32_t velE;           // NED east velocity [mm/s]
    int32_t velD;           // NED down velocity [mm/s]
    int32_t gSpeed;         // Ground Speed (2-D) [mm/s]
    int32_t headMot;        // Heading of motion 2-D [deg / 1e-5]
    uint32_t sAcc;          // Speed Accuracy Estimate [mm/s]
    uint32_t headAcc;       // Heading Accuracy Estimate (both motion & vehicle)

    uint16_t pDOP;          // Position DOP [1 / 0.01]
    uint8_t flags3;         // Additional Flags
    uint8_t reserved1[5];   // Reserved

    int32_t headVeh;        // Heading of vehicle (2-D) [deg / 1e-5]
    int16_t magDec;         // Magnetic declination [deg / 1e-2]
    uint16_t magAcc;        // Magnetic declination accuracy [deg / 1e-2]
};

struct AckMsg {
    unsigned char clsID;
    unsigned char msgID;
};
class UbloxDev {
public:
    boost::asio::io_service port_io;
    std::string port_name;
    UbloxDev(const std::string port_name);
    ~UbloxDev();

#if 0
    struct ublox_data_t {
        int64_t system_time;
        int64_t clock_time;
        uint32_t Class;
        uint32_t id;
        uint16_t length;
        std::string payload;
    };
    typedef bool ublox_callback_t (ublox_data_t &data);
#endif
    boost::asio::serial_port *port;
    bool ublox_disable;
    bool write_ubx_cfg_msgs; //Decides if various ublox messages have to be
                            //enabled by sending UBX-CFG-MSG
    bool log_ubx_msgs;
    bool disable_nmea_msgs;
    bool save_ubx_cfg;
    Ublox::ublox_constellation_data_t config;
    bool reset_ublox_for_gnss_conf;
    static UbloxDev* get_ublox( std::string name, std::string port_name, int baud_rate, bool send_ubx_cfg_msgs, bool log_ubx_msgs, bool disable_nmea_msgs, bool save_ubx_cfg, Ublox::ublox_constellation_data_t data);
    static bool release_ublox (UbloxDev *ublox);
    void send_ubx_cfg_rate(uint16_t measRate, uint16_t navRate, uint16_t timeRef);
    void send_ubx_cfg_msgs();
    void save_ublox_configs();
    void disable_ubx_nmea_msgs();
    void enable_ubx_nmea_msgs();
    void send_sbas_config_message(bool enable_sbas);
    void sync_to_packet(boost::asio::serial_port *serport);
    //bool register_ublox_callback (ublox_callback_t *cb);
    bool enable_ublox ();
    bool disable_ublox ();
};


void run_at_command_ublox_gps_config_recover();
int  ublox_dev_open(string name, string port_name, int baud_rate, bool send_ubx_cfg_msgs, bool log_ubx_msgs, bool disable_nmea_msgs, bool save_ubx_cfg, Ublox::ublox_constellation_data_t data);
bool ublox_dev_close( int handle );

bool ublox_dev_config( int handle, string key, string value );

bool ublox_dev_enable( int handle );
bool ublox_dev_disable( int handle );

bool ublox_dev_reg_cb( int handle, Ublox::ublox_callback_t *cb );
bool ublox_dev_reg_gps_cb( int handle, Ublox::ublox_gps_callback_t *cb );
bool ublox_dev_reg_pps_cb( int handle, Ublox::ublox_pps_callback_t *cb );
bool ublox_dev_reg_ublox_constellation_cb( int handle, Ublox::ublox_constellation_callback_t *cb );
#endif

