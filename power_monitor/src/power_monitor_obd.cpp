/* Copyright (C) 2019 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Devendra Yadav <devendra.yadav@netradyne.com>
 */

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <string>
#include <sys/time.h>
#include "ndmb/nd_msg_interface.h"
#include "ndmb/nd_mbclient.h"
#include "power_monitor_obd.h"

#define TAG "PM_OBD"

static const int32_t THREAD_TIMEOUT = 1;

ndmbmsg_obd_adc_data_t *obd_data_ptr=NULL;
pthread_mutex_t obd_data_lock;

std::atomic<float> volt_obd_ndmb(0.0);
std::atomic<int> obd_state_ndmb(0);
std::atomic<uint> obd_data_count(0);
std::atomic<uint> prev_obd_data_count(0);

bool read_adc_status_and_channel_two_data(float &adc_val,int &adc_state){

    // update count
    if(obd_data_count.load() != prev_obd_data_count.load()) {
        prev_obd_data_count.store(obd_data_count.load());
        adc_state = obd_state_ndmb.load();
        adc_val = volt_obd_ndmb.load();
    }
    else {
        adc_state = INVALID_ADC_STATE;
        LOG_D(TAG, "No new OBD ADC data received in the current cycle");
    }

    LOG_D(TAG, "OBD: V:%f, S:%d, C:%d, PC:%d",
            adc_val,
            adc_state,
            obd_data_count.load(),
            prev_obd_data_count.load() );

    if(INVALID_ADC_STATE == adc_state) {
        LOG_D(TAG, "Invalid OBD ADC state received");
        return false;
    }

    return true;
}

bool msg_cb_obd(ndmb_generic_msg_t *msg) {
    ndmbmsg_obd_adc_data_t *ptr1=NULL;
    ptr1 = reinterpret_cast<ndmbmsg_obd_adc_data_t *>( msg );
    if(NULL == ptr1){
        LOG_E(TAG, "obd publisher failed to send data");
        return false;
    }
    if(NULL == obd_data_ptr){
        LOG_E(TAG,"obd_adc_open failed to allocate memory for obd_data_ptr");
        return false;
    }
    if( ptr1->topic != TOPIC_OBD_ADC_DATA ) {
        LOG_I(TAG, "Unkown topic: ->%s",ptr1->topic);
        return false;
    }

    pthread_mutex_lock(&obd_data_lock);
    bool ret = memcpy(obd_data_ptr,ptr1, sizeof(ndmbmsg_obd_adc_data_t));
    if (false == ret){
        LOG_E(TAG,"Memory copy failed for obd_data_ptr with return:%d",ret);
        pthread_mutex_unlock(&obd_data_lock);
        return false;
    }
    LOG_D(TAG,"obd topic:%s",obd_data_ptr->topic);
    LOG_D(TAG,"obd adc value:%f",obd_data_ptr->adc_value);
    LOG_D(TAG,"obd state:%d",obd_data_ptr->adc_state);
    pthread_mutex_unlock(&obd_data_lock);

    // update voltage and state
    volt_obd_ndmb.store(obd_data_ptr->adc_value);
    obd_state_ndmb.store(obd_data_ptr->adc_state);
    obd_data_count.fetch_add(1);

    return true;
}

void* obd_adc_subs_funcptr(void *args)
{
    std::string s = "NDMB_OBD_SERVICE";
    NDMBClient msg_client(s);
    bool ret = msg_client.subscribe(TOPIC_OBD_ADC_DATA, msg_cb_obd);
   if(false == ret)
    {
        LOG_E(TAG,"msg_client.subscribe failed");
	return (void *)false;
    }
    while (1){ sleep(THREAD_TIMEOUT); }

    return (void *)true;
}

bool obd_adc_open()
{
    obd_data_ptr = (ndmbmsg_obd_adc_data_t *)malloc(sizeof(ndmbmsg_obd_adc_data_t));
    if(NULL == obd_data_ptr){
        LOG_E(TAG,"Memory allcation failed for obd_data_ptr with return:%d",obd_data_ptr);
        return false;
    }
    memset(obd_data_ptr,0,sizeof(ndmbmsg_obd_adc_data_t));

    return true;
}



