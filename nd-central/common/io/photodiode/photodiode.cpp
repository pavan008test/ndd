/* Copyright (C) 2018 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Shravan Kumar M <shravan.kumar@netradyne.com>, December 2018
 */
#include <iostream>
#include <photodiode.h>
#include <photodiode_dev.h>
//#include "irled_api.h"
#include <algorithm>
#include <log.h>
using namespace std;
#define TAG "PHOTODIODE"

#ifdef __cplusplus
extern "C"
{
#endif
#include "irled_api.h"
#ifdef __cplusplus
}
#endif

struct PhotodiodeCtx {
	int handle;
};

Photodiode* Photodiode::sObj=NULL;
PhotodiodeCtx Photodiode::ctx = {-1};

Photodiode::Photodiode( string name ) : Component(name) {
}

Photodiode::~Photodiode() {
}

#if 0
Photodiode* Photodiode::get_photodiode( string name ) {
	return NULL;
}
#endif

bool Photodiode::release_photodiode( Photodiode *photodiode ) {
	return true;
}

/*
IRLED will be switched on whenever photodiode detects night mode
*/
bool Photodiode::enable_sensor(  ) {
	return photodiode_dev_enable ();
}

bool Photodiode::disable_sensor() {
    return photodiode_dev_disable ();
}

bool Photodiode::register_photodiode_callback(Photodiode::photodiode_callback_t *cb) {
    return photodiode_dev_reg_cb(cb);
}

bool Photodiode::register_alive_callback(component_alive_cb_t *cb, void *app) {

}

bool Photodiode::register_error_callback(component_error_cb_t *cb, void *app) {

}

vector< pair< string, string > > Photodiode::get_capabilities() {

}

vector< pair< string, string > > Photodiode::dump_stats() {

}

bool Photodiode::configure(vector< pair< string, string > > &config) {

}

bool Photodiode::configure(string key, string value) {

}

string Photodiode::get_config(string key) {

}

bool Photodiode::set_irled_brightness (int brightness) {

        if (false == set_irled_brightness_dev(brightness))
        {
            return false;
        }
        else
        {
            return true;
        }
}

bool Photodiode::set_irled_clear() {
	return set_irled_clear_dev ();
}

bool Photodiode::get_phototransistor_status(light_mode *mode) {
	return get_phototransistor_status_dev (mode);
}

