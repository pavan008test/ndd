/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Y Suresh Kumar <suresh.kumar@netradyne.com>, October 2016
 */


#ifndef CONFIG123_H
#define CONFIG123_H

#include <vector>
#include <string>
#include <iostream>

#include <component.h>
#include <error.h>

#include <gps.h>
#include <imu.h>
#include <log.h>
 
using namespace std;

class config_ctx;


class Config: public Component {

public:
	/** Inherited**/
	bool register_alive_callback(component_alive_cb_t *cb , void *app);

	bool register_error_callback(component_error_cb_t *cb , void *app);

	vector< pair< string, string > > get_capabilities();

	vector< pair< string, string > > dump_stats();

	bool configure(vector< pair< string, string > > &config);

	bool configure(string key, string value);

	string get_config(string key);


	Config( string name );
	~Config();

	bool sample_prog();



private:
	config_ctx* ctx;
	
};


//// 




#endif
