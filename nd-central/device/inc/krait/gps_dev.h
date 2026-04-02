/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Arun V <arun.vj@netradyne.com>, October 2016
 */

#ifndef GPS_DEV_H
#define GPS_DEV_H

#include <gps.h>


static const string GPS_AUTO_RESTART_CMD =  "lte_gps_sample_app 'AT!GPSAUTOSTART=1,1,250,250,1'";
static const string GPS_AUTO_RESTART_CMD2 =  "lte_gps_sample_app 'AT!GPSAUTOSTART?'" ;

void run_at_command_gps_config_recover();
int  gps_dev_open();
bool gps_dev_close( int handle );

bool gps_dev_config( int handle, string key, string value );

bool gps_dev_enable( int handle, bool hdmaps_mode_enable );
bool gps_dev_disable( int handle );

bool gps_dev_reg_cb( int handle, Gps::gps_callback_t *cb );

#endif
