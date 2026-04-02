#include "apm_power_volt.h"
#include "apm.h"
#include "apm_worker.h"
#include "nd_time.h"
#include <pthread.h>
#include <unistd.h>
#include <deque>
#include <atomic>
#include "system_utils.h"


#define nd_device_obj (ND_DeviceFactory::Create_NDDevice())
#define nd_service_obj (NDService::get_service_obj("APM"))

extern int all_thread_keepalive_status;

// V1: ENGINE OFF VOLTAGE: Value recorded during LPW
// V2: ENGINE ON VOLTAGE: Value recorded during SPEED > 10 mph
// V3: Voltage Ramp Percentile for IGNITION ON Event: Voltage observed during the transition from V1 to V2
// V4: Voltage Ramp Percentile for IGNITION OFF Event: Voltage observed during the transition from V2 to V1

motion_status_t POWER_VOLT_worker::read_status() {

    if((false == enabled) || (true == sensor_decision_disabled)) {
        LOG_D(TAG_PWR_VOLT, "Power Volt worker(%d), sensor_decision_disabled(%d). Returning STATIONARY", enabled, sensor_decision_disabled);
        return STATIONARY;
    }

    if(true == use_legacy){
        return STATIONARY;
    }

    std::string crank_ign_sysfs_path = get_sysfs_path_from_enum(PowermonParam::eCRANK_IGN);

    if (STATIONARY == getMotionState()) {
        write_into_sysfs_entry(crank_ign_sysfs_path, IGNITION_OFF);
    }
    else if (MOVING == getMotionState()) {
        write_into_sysfs_entry(crank_ign_sysfs_path, IGNITION_ON);
    }
    else {
        LOG_E(TAG_PWR_VOLT, "Unknown motion state : %d", getMotionState());
        write_into_sysfs_entry(crank_ign_sysfs_path, IGNITION_OFF);
    }

    LOG_D(TAG_PWR_VOLT, "Vehicle motion status : %d", getMotionState());
    return getMotionState();
}

void POWER_VOLT_worker::filter_func () {
	LOG_I(TAG_PWR_VOLT, "Dummy function do nothing here");
}

void* POWER_VOLT_worker::poll_func() {
	LOG_I(TAG_PWR_VOLT, "Dummy function do nothing here");
    return NULL;
}

void POWER_VOLT_worker::crank_glitch_report() {
    //check for CRANK VOLT as wakeup reason
    bool wake_on_crank = file_is_present(nd_factory_utils::get_wake_on_crank_volt_file());
    //check for initial crank volt file (in case of no WAKE on CRANK)
    bool init_crank_volt = file_is_present(nd_factory_utils::get_initial_crank_volt_file());

    //check for current crank state
    bool crank_on_state = (getMotionState() == MOVING) ? true : false;

    //read initial and final voltages
    float init_voltage = 0.0f, curr_voltage = 0.0f;
    if(true == wake_on_crank) {
        read_from_dev_shm_file(nd_factory_utils::get_wake_on_crank_volt_file(), init_voltage);
    } else if(true == init_crank_volt) {
        read_from_dev_shm_file(nd_factory_utils::get_initial_crank_volt_file(), init_voltage);
    } else {
        return;
    }
    curr_voltage = nd_device_obj->get_voltage_value(eCRANK_VOLT);

    //detect glitch
    if(wake_on_crank ^ crank_on_state) {
        //truncating to 3 decimal places for critical event and logging
        std::ostringstream init_volt_stream, curr_volt_stream;
        init_volt_stream << std::fixed << std::setprecision(3) << init_voltage;
        curr_volt_stream << std::fixed << std::setprecision(3) << curr_voltage;

        std::string err_msg = "Glitch Detected: WakeOnCrank(" + to_string(wake_on_crank) + ") State(" +to_string(crank_on_state) + ") V: Init(" + init_volt_stream.str() + ") Curr(" + curr_volt_stream.str() + ")";
        LOG_C(TAG_PWR_VOLT, "%s", err_msg.c_str());
        nd_service_obj->send_err_msg(SM_E_APM_EVENT_STATUS, static_cast<int>(ePWR_VOLT_GLITCH_AUX_CODE), err_msg);
    } else {
        LOG_D(TAG_PWR_VOLT, "No Glitch: WakeOnCrank(%d) State(%d) V: Init(%.3f) Curr(%.3f)", wake_on_crank, crank_on_state, init_voltage, curr_voltage);
    }

    if(true == wake_on_crank){
        file_delete(nd_factory_utils::get_wake_on_crank_volt_file());
    } else if(true == init_crank_volt) {
        file_delete(nd_factory_utils::get_initial_crank_volt_file());
    }
    return;
}

void POWER_VOLT_worker::insert_bin_data() {

    all_thread_keepalive_status &= ~(1 << get_signal_mask());

    VoltData volt_data;
    volt_data.power_volt = nd_device_obj->get_voltage_value(eCRANK_VOLT);
    volt_data.drp_volt = nd_device_obj->get_voltage_value(eCRANK_VDROP);

    // For D2XX OBD service sending voltage data and while pushing data to sysfs generator count will increase
    // For D4XX we are reading voltage data from ADC, So after reading voltage we need to increment the generator count.
    if(true == increment_self_gen_count) {
        push_data_to_sysfs(eADC_VALID, 0); // To increment the genrator count for ADC read
    }

    getWorkerBinDataObj()->set_bin_data(get_index(), (void *) &volt_data);

    // Update index for next data
    incr_index();

    // detecting glitch only once, after first decision is made on short window
    if( (false == glitch_reported) && (get_index() > PWR_SHORT_WINDOW) ) {
        crank_glitch_report();
        glitch_reported = true;
    }

    if(true == update_reader_count) {
        // If device type is D2XX call update_data_validity again to increment reader count properly, As obd updating valid entry at 2 hz rate.
        // If can worker is disabled.
        std::string valid_sysfs_path = get_sysfs_path_from_enum(PowermonParam::eADC_VALID);
        update_data_validity(valid_sysfs_path, false, is_event_driven, get_decision_interval(eDecisionWindow_Valid));
    }
}

void POWER_VOLT_worker::detect_engine_status() {

    LOG_I(TAG_PWR_VOLT, "In %s", __func__);

    check_battery_status_file();

    APM_worker::detect_engine_status();

}

void* POWER_VOLT_worker::intr_func() {
    LOG_I(TAG_PWR_VOLT, "In %s", __func__);

    // Set increment_self_gen_count flag based on device type
    if((eKrait_1 == nd_device_obj->getDeviceType()) || (eKrait_2 == nd_device_obj->getDeviceType())) {
        increment_self_gen_count = false;
    }
    else {
        increment_self_gen_count = true;
    }

    detect_engine_status();
    return NULL;
}

void VoltBinData::update_calibration_data(uint index) {
    bool gps_is_moving = false;
    int cur_ign_state = CRANK_ERROR;
    bool is_pwr_above_thresh = false;
    uint fusion_count = 0;
    uint prev_index = ( 0 == index ) ? (PWR_FULL_WINDOW - 1) : (index - 1) ;
    float avg_voltage_to_write = 0.0;
    if(is_above_threshold(APM_Decision::eAPM_Decision_Moving, index)) {
        is_pwr_above_thresh = true;
        fusion_count = get_decision_count(eAPM_Decision_Fusion_Moving, prev_index);
    }else{
        is_pwr_above_thresh = false;
        fusion_count = get_decision_count(eAPM_Decision_Fusion_Engine_Off, prev_index);
    }
    
    
    APM_worker* gps_worker = APM::WorkerInstance(eSatellite);
    if(gps_worker != nullptr) {
        uint gps_fusion_decision_count = gps_worker->get_decision_count(eAPM_Decision_Fusion_Moving, gps_worker->get_decision_index());

        if(gps_fusion_decision_count >= gps_worker->get_decision_interval(eDecisionWindow_Median)){
            fusion_count++;
            if(false == is_pwr_above_thresh){
                set_decision_count(eAPM_Decision_Fusion_Engine_Off, index, fusion_count);
                fusion_avg_voltage_below += get_data(index).power_volt;
                avg_voltage_to_write = fusion_avg_voltage_below / fusion_count;
            }else{
                set_decision_count(eAPM_Decision_Fusion_Moving, index, fusion_count);
                fusion_avg_voltage_abv += get_data(index).power_volt;
                avg_voltage_to_write = fusion_avg_voltage_abv / fusion_count;
            }
            gps_is_moving = true;
            cur_ign_state = CRANK_HIGH; // this means fusion ignition is ON (gps speed > 10) 
        }
    }

    int global_ign_state = nd_device_obj->get_crank_level();
    if(global_ign_state == CRANK_LOW){
        fusion_count++;
        if(true == is_pwr_above_thresh){
            set_decision_count(eAPM_Decision_Fusion_Moving, index, fusion_count);
            fusion_avg_voltage_abv += get_data(index).power_volt;
            avg_voltage_to_write = fusion_avg_voltage_abv / fusion_count;
        }else{
            set_decision_count(eAPM_Decision_Fusion_Engine_Off, index, fusion_count);
            fusion_avg_voltage_below += get_data(index).power_volt;
            avg_voltage_to_write = fusion_avg_voltage_below / fusion_count;
        }
        gps_is_moving = false;
        cur_ign_state = CRANK_LOW;
    }

    LOG_D(TAG_PWR_VOLT, "Current ignition_state %d , Prev ignition_state %d",cur_ign_state, prev_ign_state);
    // Write to extended attribute if index reaches a particular threshold
    if(((index % PWR_FULL_WINDOW == 0) && (fusion_count > 0)) || ((prev_ign_state != CRANK_ERROR) && (cur_ign_state != CRANK_ERROR) && (prev_ign_state != cur_ign_state))){
        LOG_D(TAG_PWR_VOLT, "ENTERED in writing window fusion_count %d , fusion avg volt abv %f, fusion avg volt below %f, avg_voltage to write %f", fusion_count, fusion_avg_voltage_abv, fusion_avg_voltage_below, avg_voltage_to_write);
        int cur_count = 0;
        float cur_avg = 0.0;
        string attribute_for_cnt = "", attribute_for_avg = "";

        if(cur_ign_state == CRANK_HIGH){
            if(is_pwr_above_thresh) {
                attribute_for_cnt = USER_ON_GT_CNT;
                attribute_for_avg = USER_ON_GT_AVG;
            }else{
                attribute_for_cnt = USER_ON_LT_CNT;
                attribute_for_avg = USER_ON_LT_AVG;
            }
        }else if(cur_ign_state == CRANK_LOW){
            if(is_pwr_above_thresh) {
                attribute_for_cnt = USER_OFF_GT_CNT;
                attribute_for_avg = USER_OFF_GT_AVG;
            }else{
                attribute_for_cnt = USER_OFF_LT_CNT;
                attribute_for_avg = USER_OFF_LT_AVG;
            }
        }

        if(attribute_for_cnt != "" ){
            if(false == string_to_integer(apm_attr_util::get_attr_status(vehicle_attributes, attribute_for_cnt, "0"), cur_count)){
                cur_count = 0;
            }
            if(false == string_to_float(apm_attr_util::get_attr_status(vehicle_attributes, attribute_for_avg, "0.0"), cur_avg)){
                cur_avg = 0.0;
            }
            LOG_D(TAG_PWR_VOLT, " Current count %d , avg %f ", cur_count, cur_avg);
            cur_count = fusion_count + cur_count;
            cur_avg = (cur_avg + avg_voltage_to_write) / 2;
            apm_attr_util::set_attr_status(vehicle_attributes, attribute_for_cnt, to_string(cur_count).c_str());
            apm_attr_util::set_attr_status(vehicle_attributes, attribute_for_avg, to_string(cur_avg).c_str());
            if(is_pwr_above_thresh){
                set_decision_count(APM_Decision::eAPM_Decision_Fusion_Moving, index, 0);
                fusion_avg_voltage_abv = 0.0;
            }else{
                set_decision_count(APM_Decision::eAPM_Decision_Fusion_Engine_Off, index, 0);
                fusion_avg_voltage_below = 0.0;
            }
            LOG_D(TAG_PWR_VOLT, " Current count %d , avg %f ", cur_count, cur_avg);
            LOG_D(TAG_PWR_VOLT, "Data written to xattr");
        }
    }

    prev_ign_state = cur_ign_state;
    return;
}

bool POWER_VOLT_worker::handle_data_outage() {

    bool outage_status = APM_worker::handle_data_outage();

    // If data outage count read default count then create file
    if((true == outage_status) && (false == extend_misc_file_created)) {

        if((false == file_is_present(extend_misc_file)) && (false == file_touch(extend_misc_file))) {
            LOG_C(TAG_PWR_VOLT, "Failed to create file %s", extend_misc_file.c_str());
        }
        else {
            LOG_C(TAG_PWR_VOLT, "File %s already present or created", extend_misc_file.c_str());
            extend_misc_file_created = true;
        }
    }
    else if((false == outage_status) && (true == extend_misc_file_created)) {
        if(true == file_is_present(extend_misc_file)) {
            if(false == file_delete(extend_misc_file)) {
                LOG_C(TAG_PWR_VOLT, "Failed to delete file %s", extend_misc_file.c_str());
            }
        }
        extend_misc_file_created = false;
    }
    else {}

    return outage_status;

}

