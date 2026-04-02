/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Arun V <arun.vj@netradyne.com>, October 2016
 */

#ifndef GPS_H
#define GPS_H

#define ND_CONFIG_ANALYTICS "/home/ubuntu/.nddevice/latest/nd_config.ini"

#include <vector>
#include <string>
#include <stdint.h>
#include "ndmb/nd_msg_interface.h"
#include "ndmb/nd_mbclient.h"
#include "ndmb/nd_mbserver.h"

#include <component.h>
#include <error.h>

using namespace std;

extern double latest_cached_latitude;
extern double latest_cached_longitude;

void gps_config_recover();
extern atomic<uint64_t> gps_index;
class Gps: public Component {
    public:

        /** Inherited**/

	bool register_alive_callback( component_alive_cb_t *cb , void *app);

	bool register_error_callback( component_error_cb_t *cb , void *app);

	vector< pair< string, string > > get_capabilities();

	vector< pair< string, string > > dump_stats();

	bool configure(vector< pair< string, string > > &config);

	bool configure(string key, string value);

	string get_config(string key);

	void monitor_gps_cb();

	/**Specialization**/

	/**
	 * @brief String Constants for configuration keys
	 *
	 */
	static const string config_refresh;

	struct gps_data_t {
		bool    valid;
		double  latitude;		 
		double  longitude;
		double  altitude;	 
		float   speed;
		float   bearing;
		float   accuracy; //currently not supported
		int64_t timestamp;
		int64_t system_timestamp;

		int flags; //currently not used
	};

	//gps_data_t can't be modified to add new fields because Analytics also
	//is using the same structure. Hence the need of below gps_extended_data_t
	//struct
	struct gps_extended_data_t {
		struct gps_data_t gps_data;
		uint64_t gps_index;
		uint64_t raw_time_micro;
		double altitudeMSL;
        int32_t good_satellites;
        int32_t fix_quality;
		bool gps_port_status;
		int64_t gps_port_status_ts;
		int64_t ttff_ts;
        float gnss_temp;
	};

	struct gps_pps_data_t {
        	uint64_t  pps_index;
        	uint64_t pps_raw_time;
        	uint64_t pps_clock_time;
	};

	typedef bool gps_callback_t ( gps_data_t val, Gps::gps_extended_data_t);

	typedef bool gps_pps_callback_t ( gps_pps_data_t &val );

	static Gps* get_gps( string name );
	static bool release_gps( Gps *gps );
	
	bool   enable_gps(bool hdmaps_mode_enable );
	bool   disable_gps( );

	bool   register_gps_callback(gps_callback_t *cb);
	bool   register_gps_pps_callback(gps_pps_callback_t *cb);

private:
    static int handle;
    int gps_counter = 5;

    static Gps    *sObj;
    bool enable_rt_gps;
    void *zmq_publisher_gps = NULL;
	
	Gps(string name);
	~Gps();

};

#endif


