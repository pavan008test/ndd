#include <ublox.h>
#include <ublox_dev.h>
#include "system_utils.h"
#define TAG "UBLOX"
struct UbloxCtx {
	int handle;
};

Ublox *  Ublox::sObj=NULL;
UbloxCtx Ublox::ctx = {-1};

const string Ublox::config_refresh="ublox-refresh";
#if 0
Ublox::Ublox( string name,
              string port_name,
              int baud_rate,
              bool send_ubx_cfg_msgs,
              bool log_ubx_msgs,
              bool disable_nmea_msgs,
              bool save_ubx_cfg ) : Component((string)name,
                                              (string)port_name,
                                              (int)baud_rate,
                                              (bool)send_ubx_cfg_msgs,
                                              (bool)log_ubx_msgs,
                                              (bool)disable_nmea_msgs,
                                              (bool)save_ubx_cfg) {
	ctx.handle = ublox_dev_open(name, port_name, baud_rate, send_ubx_cfg_msgs, log_ubx_msgs, disable_nmea_msgs, save_ubx_cfg);
}
#endif

#if 1
Ublox::Ublox( string name,
              string port_name,
              int baud_rate,
              bool send_ubx_cfg_msgs,
              bool log_ubx_msgs,
              bool disable_nmea_msgs,
              bool save_ubx_cfg, 
              Ublox::ublox_constellation_data_t data ) : Component (name) {
	ctx.handle = ublox_dev_open(name, port_name, baud_rate, send_ubx_cfg_msgs, log_ubx_msgs, disable_nmea_msgs, save_ubx_cfg, data);
}
#endif

Ublox::~Ublox() {
	ublox_dev_close(ctx.handle);
}

Ublox* Ublox::get_ublox(string name, string port_name, int baud_rate, bool send_ubx_cfg_msgs, bool log_ubx_msgs, bool disable_nmea_msgs, bool save_ubx_cfg, Ublox::ublox_constellation_data_t data ) {
	if( sObj == NULL ) {
		sObj = new Ublox(name, port_name, baud_rate, send_ubx_cfg_msgs, log_ubx_msgs, disable_nmea_msgs, save_ubx_cfg, data);
		if( ctx.handle == -1 ) {
			return NULL;
		}
		return sObj;
	}

	return NULL;
}

bool Ublox::release_ublox( Ublox *ublox ) {
	if( ublox == sObj ) {
		delete sObj;
		sObj = NULL;
		return true;
	}

	return false;
}

bool   Ublox::enable_ublox( ) {
	return ublox_dev_enable(ctx.handle);
}

bool   Ublox::disable_ublox( ) {
	return ublox_dev_disable(ctx.handle);
}

bool   Ublox::register_ublox_callback(Ublox::ublox_callback_t *cb) {
	return ublox_dev_reg_cb(ctx.handle, cb);
}

bool   Ublox::register_ublox_gps_callback(Ublox::ublox_gps_callback_t *cb) {
	return ublox_dev_reg_gps_cb(ctx.handle, cb);
}

bool   Ublox::register_ublox_pps_callback(Ublox::ublox_pps_callback_t *cb) {
    return ublox_dev_reg_pps_cb(ctx.handle, cb);
}

bool   Ublox::register_ublox_constellation_callback(Ublox::ublox_constellation_callback_t *ublox_constellation_cb)
{
    // define the function from udev.cpp and return
    return ublox_dev_reg_ublox_constellation_cb(ctx.handle, ublox_constellation_cb);
}

bool Ublox::register_alive_callback(component_alive_cb_t *cb , void *app ) {

}

bool Ublox::register_error_callback(component_error_cb_t *cb , void *app) {

}

vector< pair< string, string > > Ublox::get_capabilities() {

}

vector< pair< string, string > > Ublox::dump_stats() {

}

bool Ublox::configure(vector< pair< string, string > > &config) {
	for( vector< pair< string, string > >::iterator iter = config.begin(), end = config.end(); iter != end; iter++ ) {
		configure( iter->first, iter->second );
	}
}

bool Ublox::configure(string key, string value) {
	if( key == Ublox::config_refresh ) {
		return ublox_dev_config(ctx.handle, key, value );
	} else {
		
	}
}

string Ublox::get_config(string key) {

}

#if 0
void ublox_config_recover(){

return run_at_command_ublox_config_recover();

}
#endif
