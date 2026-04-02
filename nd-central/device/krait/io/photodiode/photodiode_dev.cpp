/* Copyright (C) 2017 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Fayyas Manzoor <fayyas.manzoor@netradyne.com>, February 2017
 */

#include <cstdio>

#include <iostream>
#include <iomanip>

#include <stdlib.h>
#include <unistd.h>

#include <photodiode_dev.h>
#include <nd_msp_utils.h>
#include <log.h>
#include <nd_factory.h>
#ifdef __cplusplus
extern "C" {
#endif
    #include "irled_api.h"
    #include "msp_api.h"
#ifdef __cplusplus
}
#endif
#define nd_device_obj (ND_DeviceFactory::Create_NDDevice())
using namespace std;

#define TAG "PHOTO_DEV"

static const int pt_read_threashold = 80; // threshold to decide on the IRLED ON / OFF
static const int irled_off_count = 10; // Counter used while turning off the IRLED

static Photodiode::photodiode_callback_t *cb=NULL;

pthread_t photodiode_thread;

light_mode g_mode = DAY_MODE; // 0 - night mode, 1 - day mode
bool keep_alive = false; // flag to check if photodiode thread should be live or not

//static bool set_irled_mode (irled_mode_t mode);


int photodiode_dev_open()
{
	//set_irled_mode (IRLED_MODE_NOTSET);
	return 0;
}

bool photodiode_dev_close( ) {

	//set_irled_mode (IRLED_MODE_NOTSET);
	return true;
}


bool photodiode_dev_config( ) {

	return true;
}

void* photodiode_thread_fn(void* args) {

    int pt_reading = pt_read_threashold + 1; // photo transistor reading
    int counter = 0; // counter for turning OFF IRLED
    light_mode prev_mode = DAY_MODE;

    // Initial callback is sent to clear any previous states
    if(cb) {
        cb(DAY_MODE);
    }

    while (1) {
        if(keep_alive == false) {
            break;
        }
        sleep(2);

        if (nd_msp_read_phototransistor(&pt_reading) != true) {
            LOG_E(TAG, "Could not read phototransistor");
            pt_reading = pt_read_threashold + 1;
        }
        if (pt_reading < pt_read_threashold) {
            g_mode = NIGHT_MODE;
        }
        else {
            g_mode = DAY_MODE;
        }

        if ((g_mode != prev_mode) || (counter)) {
            if (g_mode != DAY_MODE)
            {
                counter = 0;
                LOG_I (TAG, "switch ON IR LED");
                if (cb)
                    cb (g_mode);
            }
            else
            {
                counter = ++counter % irled_off_count;
                if(!counter) {
                    LOG_I (TAG, "switch OFF IR LED");
                    if (cb) {
                        cb (g_mode);
                    }
                }
            }
            prev_mode = g_mode;
        }
    }
    return NULL;
}

bool photodiode_dev_reg_cb( Photodiode::photodiode_callback_t *cb ) {

	::cb = cb;
	return true;
}

/*
   IRLED will be switched on whenever photodiode detects night mode
 */
bool photodiode_dev_enable( ) {

        keep_alive = true;
 
        pthread_create(&photodiode_thread, NULL, photodiode_thread_fn, NULL);

	LOG_I (TAG, "photodiode_dev_enable successful");
	return true;
}

bool photodiode_dev_disable( )
{
    g_mode = DAY_MODE;
    keep_alive = false;
    pthread_join(photodiode_thread, NULL);
    return true;
}

bool set_irled_brightness_dev(int brightness) {
    return nd_device_obj->set_irled_brightness((irled_brightness_level_t) brightness);
}

bool set_irled_clear_dev() {
    nd_device_obj->set_irled_clear();
}

bool get_phototransistor_status_dev(light_mode *mode) {
    *mode = g_mode;
    return true;
}

