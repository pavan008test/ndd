/* Copyright (C) 2019 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Devendra Yadav <devendra.yadav@netradyne.com>
 */

#ifndef __POWER_MONITOR_OBD_H_
#define __POWER_MONITOR_OBD_H_

typedef struct obd_adc_thread_info{
    pthread_t obd_thread_id;
    char      *argv;
}obd_adc_thread_info_t;

constexpr int INVALID_ADC_STATE = 0;

bool obd_adc_open(void );
void* obd_adc_subs_funcptr(void* );
bool read_adc_status_and_channel_two_data(float &adc_val, int &adc_state);

#endif
