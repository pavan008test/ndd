#include <gps.h>
#include <gps_dev.h>
#include "system_utils.h"
#define TAG "GPS"
struct GpsCtx {
	int handle;
};

Gps *  Gps::sObj=NULL;
GpsCtx Gps::ctx = {-1};

const string Gps::config_refresh="gps-refresh";

Gps::Gps( string name ) : Component(name) {
	ctx.handle = gps_dev_open();	
}

Gps::~Gps() {
	gps_dev_close(ctx.handle);
}

Gps* Gps::get_gps( string name ) {
	if( sObj == NULL ) {
		sObj = new Gps(name);
		if( ctx.handle == -1 ) {
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
	return gps_dev_enable(ctx.handle, hdmaps_mode_enable);
}

bool   Gps::disable_gps( ) {
	return gps_dev_disable(ctx.handle);
}

bool   Gps::register_gps_callback(Gps::gps_callback_t *cb) {
	return gps_dev_reg_cb(ctx.handle, cb);

}

bool   Gps::register_gps_pps_callback(Gps::gps_pps_callback_t *cb) {
#ifdef BAGHEERA2
    return gps_dev_reg_pps_cb(ctx.handle, cb);
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
		return gps_dev_config(ctx.handle, key, value );
	} else {
		
	}
}

string Gps::get_config(string key) {

}


void gps_config_recover(){

return run_at_command_gps_config_recover();

}
