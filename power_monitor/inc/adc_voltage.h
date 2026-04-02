/* Copyright (C) 2022 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Praveen M <praveen.mathad@netradyne.com>, May 2022
 */

#include <cstdlib>
#include <unistd.h>
#include <nd_msg_utils.h> 
#include <nd_msg_types.h>
#include <sys/time.h>
#include <errno.h>
#include "log.h"

#include <stdlib.h>
#include <fcntl.h>    /* For O_RDWR */
#include <unistd.h>   /* For open(), creat() */
#include <sys/sysinfo.h>

/* Engine state
 * ENGINE_OFF - if one volt less than max voltage
 * ENGINE_ON - if more than default voltage
 * ENGINE_STATE_UNKNOWN - Intial value 
 */
enum engine_state_t{
	ENGINE_OFF = 0,
	ENGINE_ON,
	ENGINE_STATE_UNKNOWN = 100
};

static const float DEFAULT_ENGINE_ON_VOLTAGE = 13.1; //default value for adc voltage
float engine_on_voltage = DEFAULT_ENGINE_ON_VOLTAGE;
float max_engine_on_voltage = 0;
engine_state_t engine_state = ENGINE_STATE_UNKNOWN; //Initial 




void monitor_engine_status(int ignition_status, float adc_value);
