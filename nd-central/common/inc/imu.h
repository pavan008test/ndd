/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Arun V <arun.vj@netradyne.com>, October 2016
 */

#ifndef IMU_H
#define IMU_H

#include <vector>
#include <string>
#include <stdint.h>

#include <component.h>
#include <error.h>

using namespace std;

class ImuCtx;

class Imu: public Component {
public:

	/** Inherited**/

	bool register_alive_callback(component_alive_cb_t *cb , void *app);

	bool register_error_callback(component_error_cb_t *cb , void *app);

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
	static const string config_refresh_low;
	static const string config_refresh_med;
	static const string config_refresh_high;

	enum imu_sensor_t {
		IMU_ACCEL,
		IMU_GYRO,
		IMU_MAGNETO,
		IMU_TEMP,
		IMU_MAX
	};
	
	struct val_t {
		int64_t raw_time;
		int64_t clock_time;
		float    x;
		float    y;
		float    z;
	};

	typedef bool imu_callback_t ( val_t val_a, val_t val_g, val_t val_m );
	typedef bool imu_temperature_callback_t ( val_t val);

	static Imu* get_imu( string name );
	static bool release_imu( Imu *imu );
	
	bool   enable_sensor(imu_sensor_t type );
	bool   disable_sensor(imu_sensor_t type );

	bool   register_imu_callback(imu_callback_t *cb);
	bool   register_imu_temperature_callback(imu_temperature_callback_t *cb);
	
private:
	static Imu    *sObj;
	static ImuCtx ctx;
	Imu( string name );
	~Imu();

};

#endif


