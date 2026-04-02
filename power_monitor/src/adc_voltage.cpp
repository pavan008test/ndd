/* Copyright (C) 2022 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Praveen M <praveen.mathad@netradyne.com>, May 2022
 */

#include <adc_voltage.h>
#include <config_parser.h>

static const char *TAG="PWR";
static const string bagheera_config_path = "/home/ubuntu/.nddevice/latest/bagheera_config.ini";

void monitor_engine_status(int ignition_status, float adc_value){

    float curr_adc_value = 0;
    curr_adc_value = adc_value;
     LOG_D(TAG, "OBD_INFO :: VoltMon - current voltage: %f, max volatge during engine on: %f, ignition status: %d, engine_on_voltage = %f, engine_state = %d\n",
              curr_adc_value, max_engine_on_voltage, ignition_status, engine_on_voltage, engine_state);

    if(curr_adc_value > max_engine_on_voltage && engine_state == ENGINE_ON){
        max_engine_on_voltage = curr_adc_value;
    }
    if ((curr_adc_value > engine_on_voltage) && (engine_state != ENGINE_ON))
    {
        engine_state = ENGINE_ON;
        LOG_I(TAG, "VoltMon - Probable Engine ON: %f, ignition status: %d\n", curr_adc_value, ignition_status);
    }
    else if ((max_engine_on_voltage - curr_adc_value) > 1 && (engine_state == ENGINE_ON))
    {
        engine_state = ENGINE_OFF;
        LOG_I(TAG, "VoltMon - Probable Engine OFF: current voltage: %f, max volatge during engine on: %f, ignition status: %d\n",
              curr_adc_value, max_engine_on_voltage, ignition_status);
        max_engine_on_voltage = 0;
    }
}

bool check_volt_mon_configuration(){

    string temp;
    bool get_override_val = true;
    bool is_val_overridden = false;
    Config_parser config_parser(bagheera_config_path);
    if (!config_parser.getParseStatus())
    {
        LOG_E(TAG, "Can not parse config\n");
        return false;
    }
    temp = config_parser.getConfig("vehicle_data", "engine_on_voltage", "false", get_override_val, is_val_overridden);
    engine_on_voltage = (float)atof(temp.c_str());
    return true;
}


