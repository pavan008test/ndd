/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Arun V <arun.vj@netradyne.com>, October 2016
 */

#include <imu.h>
#include <imu_dev.h>

#include <log.h>

#define TAG "IMU"

struct ImuCtx {
	int handle;
};

Imu * Imu::sObj=NULL;
ImuCtx Imu::ctx = {-1};

const string Imu::config_refresh="imu-refresh";
const string Imu::config_refresh_low="imu-refresh-low";
const string Imu::config_refresh_med="imu-refresh-med";
const string Imu::config_refresh_high="imu-refresh-high";

Imu::Imu( string name ) : Component(name) {
	ctx.handle = imu_dev_open();	
}

Imu::~Imu() {
	imu_dev_close(ctx.handle);
}

Imu* Imu::get_imu( string name ) {
	if( sObj == NULL ) {
		sObj = new Imu(name);
		if( ctx.handle == -1 ) {
			return NULL;
		}
		return sObj;
	}
	return NULL;
}

bool Imu::release_imu( Imu *imu ) {
	if( imu == sObj ) {
		delete sObj;
		sObj = NULL;
		return true;
	}

	return false;
}

bool   Imu::enable_sensor(Imu::imu_sensor_t type ) {
	switch(type) {
		case IMU_ACCEL:
			imu_dev_enable_accel(ctx.handle);
			break;
		case IMU_GYRO:
			imu_dev_enable_gyro(ctx.handle);
			break;
		case IMU_MAGNETO:
			imu_dev_enable_magneto(ctx.handle);
			break;
		default:
			return false;
	}

	return true;
}

bool   Imu::disable_sensor(Imu::imu_sensor_t type ) {
	switch(type) {
		case IMU_ACCEL:
			imu_dev_disable_accel(ctx.handle);
			break;
		case IMU_GYRO:
			imu_dev_disable_gyro(ctx.handle);
			break;
		case IMU_MAGNETO:
			imu_dev_disable_magneto(ctx.handle);
			break;
		default:
			return false;
	}
    return true;
}

bool   Imu::register_imu_callback(Imu::imu_callback_t *cb) {
	return imu_dev_reg_cb(ctx.handle, cb);

}

bool   Imu::register_imu_temperature_callback(Imu::imu_temperature_callback_t *cb) {
#ifdef BAGHEERA2
    return imu_dev_reg_temp_cb(ctx.handle, cb);
#elif KRAIT
    return true;
#endif
}

bool Imu::register_alive_callback(component_alive_cb_t *cb, void *app) {

}

bool Imu::register_error_callback(component_error_cb_t *cb, void *app) {

}

vector< pair< string, string > > Imu::get_capabilities() {

}

vector< pair< string, string > > Imu::dump_stats() {

}

bool Imu::configure(vector< pair< string, string > > &config) {
	for( vector< pair< string, string > >::iterator iter = config.begin(), end = config.end(); iter != end; iter++ ) {
		configure( iter->first, iter->second );
	}
}

bool Imu::configure(string key, string value) {
	if( key == Imu::config_refresh ) {
		return imu_dev_config(ctx.handle, key, value );
	} else {
		
	}
}

string Imu::get_config(string key) {

}


