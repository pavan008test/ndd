/******************************************************************************************************
 * @file       dummy_adc_api.h
 *
 * @brief      This file contains the header file for dummyfying adc api for krait
 *             For krait adc comes from CAN adapter.
 *             Untill OBD service can send message this shall be a dummy value
 *
 ***********************************************************************************************************/
#ifndef _DUMMY_ADC_API_H__
#define _DUMMY_ADC_API_H__
#ifdef KRAIT
int read_adc_channel_one_data(void) {
    return 12;
    
}
float read_adc_channel_two_data(void) {
    return 12.12;
    
}
float adc_calc_voltage(float) {
    return 12.12;
    
}
#endif
#endif
