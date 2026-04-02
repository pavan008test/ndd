/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Arun V <arun.vj@netradyne.com>, October 2016
 */

#ifndef UBLOX_H
#define UBLOX_H

#include <vector>
#include <string>
#include <stdint.h>

#include <component.h>
#include <error.h>

#define UBLOX_PORT_NAME "/dev/ttyACM0"
#define UBLOX_BAUD_RATE 921600

using namespace std;

class UbloxCtx;

void ublox_gps_config_recover();

class Ublox: public Component {
public:

	/** Inherited**/

	bool register_alive_callback( component_alive_cb_t *cb , void *app);

	bool register_error_callback( component_error_cb_t *cb , void *app);

	vector< pair< string, string > > get_capabilities();

	vector< pair< string, string > > dump_stats();

	bool configure(vector< pair< string, string > > &config);

	bool configure(string key, string value);

	string get_config(string key);

	/**Specialization**/

	/**
	 * @brief String Constants for configuration keys
	 *
	 */
	static const string config_refresh;

	struct ublox_data_t {
		int64_t system_time;
		int64_t clock_time;
		uint32_t Class;
		uint32_t id;
		uint16_t length;
		std::string payload;
	};

	struct ublox_gps_data_t {
		bool    valid;
		double 	latitude;		 
		double 	longitude;
		double 	altitude;	 
		float 	speed;
		float 	bearing;
		float 	accuracy; //currently not supported
		int64_t	timestamp;
		int64_t system_timestamp;

		int flags; //currently not used
	};

    //ublox_gps_data_t can't be modified to add new fields because Analytics also
    //is using the same structure. Hence the need of below ublox_gps_sensor_data_data_t
    //struct
    struct ublox_gps_sensor_data_data_t {
        struct ublox_gps_data_t ublox_gps_data;
        double altitudeMSL;
        uint64_t ublox_gps_index;
        uint64_t raw_time_micro;
    };

    struct ublox_pps_data_t {
        uint64_t  pps_index;
        uint64_t pps_raw_time;
        uint64_t pps_clock_time;
    };

    typedef struct ublox_constellation_config_data_t {
        bool enable_GPS;
        bool enable_SBAS;
        bool enable_Galileo;
        bool enable_BeiDou;
        bool enable_IMES;
        bool enable_QZSS;
        bool enable_GLONASS;
    }ublox_constellation_data_t;

    typedef bool ublox_callback_t (ublox_data_t &data);
    typedef bool ublox_gps_callback_t ( ublox_gps_data_t val, uint64_t ublox_gps_index, uint64_t raw_time_ns, double altitudeMSL);
    typedef bool ublox_pps_callback_t ( ublox_pps_data_t &val );
	typedef bool ublox_constellation_callback_t (ublox_constellation_data_t &ublox_constellation_data);

	static Ublox* get_ublox( string name, string port_name, int baud_rate, bool send_ubx_cfg_msgs, bool log_ubx_msgs, bool disable_nmea_msgs, bool save_ubx_cfg, ublox_constellation_data_t data);
	static bool release_ublox( Ublox *ublox );
	
	bool   enable_ublox( );
	bool   disable_ublox( );

	bool   register_ublox_callback(ublox_callback_t *cb);
	bool   register_ublox_gps_callback(ublox_gps_callback_t *cb);
    bool   register_ublox_pps_callback(ublox_pps_callback_t *cb);
	bool register_ublox_constellation_callback(ublox_constellation_callback_t *ublox_constellation_cb);

private:
	static Ublox    *sObj;
	static UbloxCtx ctx;
	Ublox( string name,
	      string port_name,
              int baud_rate,
              bool send_ubx_cfg_msgs,
              bool log_ubx_msgs,
              bool disable_nmea_msgs,
              bool save_ubx_cfg,
              struct ublox_constellation_config_data_t data);
	~Ublox();

};

#endif


