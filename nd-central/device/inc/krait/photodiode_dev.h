/* Copyright (C) 2018 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Shravan Kumar M <shravan.kumar@netradyne.com>, December 2018
 */

#ifndef PHOTODIODE_DEV_H
#define PHOTODIODE_DEV_H

#include <photodiode.h>

int  photodiode_dev_open();
bool photodiode_dev_close();

bool photodiode_dev_config( );

/*
   IRLED will be switched on whenever photodiode detects night mode
 */
bool photodiode_dev_enable ();

bool photodiode_dev_disable ( );
bool photodiode_dev_reg_cb( Photodiode::photodiode_callback_t *cb );
bool set_irled_brightness_dev(int brightness);
bool set_irled_clear_dev();
bool get_phototransistor_status_dev(light_mode *mode);
#endif
