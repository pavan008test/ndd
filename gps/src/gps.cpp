#include <gps.h>
#include <gps_dev.h>
#include "system_utils.h"
#include <service_utils.h>
#include <jansson/jansson.h>
static const int64_t ONE_MICRO_IN_NANO = 1000;
constexpr const char * TAG = "GPS";
constexpr const char * logDir = "/home/ubuntu/.nddevice/log/gps";
Gps *  Gps::sObj=NULL;
int    Gps::handle=0;
NDService *nd_service_obj = NULL;
const string Gps::config_refresh="gps-refresh";
extern int fd;

Gps::Gps( string name) : Component(name) {
    handle = gps_dev_open();
    nd_service_obj = NDService::get_service_obj(TAG);
    nd_log_init (logDir);
    route_logs( logDir);


}

Gps::~Gps() {
    gps_dev_close(handle);
}

Gps* Gps::get_gps( string name ) {
    if( sObj == NULL ) {
        sObj = new Gps(name);
        if( handle == -1 ) {
            return NULL;
        }
		return sObj;
	}

	return NULL;
}

bool Gps::release_gps( Gps *gps ) {

    if(gps == NULL) {
        return true;
    }

	if( gps == sObj ) {
		delete sObj;
		sObj = NULL;
		return true;
	}

	return false;
}

bool   Gps::enable_gps(bool hdmaps_mode_enable) {
	return gps_dev_enable(handle, hdmaps_mode_enable);
}

bool   Gps::disable_gps( ) {
	return gps_dev_disable(handle);
}

bool   Gps::register_gps_callback(Gps::gps_callback_t *cb) {
	return gps_dev_reg_cb(handle, cb);

}

bool   Gps::register_gps_pps_callback(Gps::gps_pps_callback_t *cb) {
#ifdef BAGHEERA2
    return gps_dev_reg_pps_cb(handle, cb);
#elif KRAIT
    return true;
#endif
}

bool Gps::register_alive_callback(component_alive_cb_t *cb , void *app ) {

}

bool Gps::register_error_callback(component_error_cb_t *cb , void *app) {

}

vector< pair< string, string > > Gps::get_capabilities() {

}

vector< pair< string, string > > Gps::dump_stats() {

}

bool Gps::configure(vector< pair< string, string > > &config) {
	for( vector< pair< string, string > >::iterator iter = config.begin(), end = config.end(); iter != end; iter++ ) {
		configure( iter->first, iter->second );
	}
}

bool Gps::configure(string key, string value) {
	if( key == Gps::config_refresh ) {
		return gps_dev_config(handle, key, value );
	} else {
		
	}
}

string Gps::get_config(string key) {

}

void Gps::monitor_gps_cb() {
	/*
	Monitor Gps Port and call backs every 2mins and raise error if no new data is received.
	*/
	
		static uint64_t last_saved_gps_index = 0;
		LOG_D(TAG, "Monitoring GPS data Current gps_index %lld,last saved gps index %lld", gps_index.load(), last_saved_gps_index);
		if(gps_index.load() > last_saved_gps_index) {
			last_saved_gps_index = gps_index.load();
			//save the gps data
		}
		else {
			//no new data
			string err_msg = "";
			int code_aux = 0;

			if(fd < 0) 
			{
				err_msg = "GPS Port Error";
				code_aux = 1;
			}
			else
			{
				err_msg = "No New Gps Data";
				code_aux = 2;
			}
			LOG_E(TAG, err_msg.c_str());

			nd_service_obj->send_err_msg(SM_E_NDC_GPS_FAIL, (int)code_aux, err_msg );// code aux 2 stands for 2mins.
		}
	
}

void gps_config_recover(){

return run_at_command_gps_config_recover();

}
