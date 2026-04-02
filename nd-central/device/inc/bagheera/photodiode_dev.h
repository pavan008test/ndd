/* Copyright (C) 2020 NetraDyne, Inc - All Rights Reserved
 *   Unauthorized copying of this file, via any medium is strictly prohibited
 *   Proprietary and confidential
 *   Written by Hari Seenivasan <hari.seenivasan@netradyne.com>, December 2020
 * */

#ifndef PHOTODIODE_DEV_H
#define PHOTODIODE_DEV_H

#ifdef BAGHEERA2
#include <photodiode.h>

int  photodiode_dev_open();
bool photodiode_dev_close();

bool photodiode_dev_config( );
/*
 *    IRLED will be switched on whenever photodiode detects threshold below 10
 *     */
bool photodiode_dev_enable ();
float photodiode_read();
bool photodiode_dev_disable ( );
bool photodiode_dev_reg_cb( Photodiode::photodiode_callback_t *cb );
bool set_irled_brightness_dev(int brightness);
bool set_irled_clear_dev();
bool get_phototransistor_status_dev(light_mode *mode);

#endif
#endif

