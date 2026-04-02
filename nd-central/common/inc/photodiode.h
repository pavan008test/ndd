/* Copyright (C) 2020 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Shravan Kumar M <shravan.kumar@netradyne.com>, January 2020
 */

#ifndef PHOTODIODE_H
#define PHOTODIODE_H

#include <vector>
#include <string>
#include <stdint.h>

#include <component.h>
#include <error.h>

using namespace std;

enum light_mode {
    NIGHT_MODE = 0,
    DAY_MODE,
    UNKNOWN_MODE
};

class PhotodiodeCtx;

class Photodiode: public Component {
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
		typedef bool photodiode_callback_t ( int ir_status);

#if 0
		static Photodiode* get_photodiode( string name );
#endif
		static bool release_photodiode( Photodiode *photodiode );

		bool   enable_sensor();
 
		bool   disable_sensor();
		bool   set_photodiode_ir_mode (string ir_mode);
		bool   register_photodiode_callback(photodiode_callback_t *cb);
                bool   set_irled_brightness(int brightness);
                bool   set_irled_clear();
        bool   get_phototransistor_status(light_mode *mode);

	private:
		static Photodiode    *sObj;
		static PhotodiodeCtx ctx;
		Photodiode( string name );
		~Photodiode();

};

#endif


