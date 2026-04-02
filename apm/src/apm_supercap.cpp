/* Copyright (C) 2019 - 2020 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Devendra Yadav <devendra.yadav@netradyne.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <stdbool.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <mutex>
#include <sys/resource.h>
#include <sys/syscall.h>
#include "system_utils.h"
#include "log.h"
#include "apm.h"
#include "apm_supercap.h"
#include "apm_worker.h"
#include "nd_msg_utils.h"
#include <config_parser.h>

#define TAG "A_SUPERCAP"
#define DEVICECONFIG_INI   "/home/ubuntu/config/deviceconfig.ini"
extern NDService *nd_service_obj;
extern ND_DeviceFactory *nd_device_obj;  // nd device object based on deviceType
extern int all_thread_keepalive_status;
extern bool apm_wom_enable;
extern int32_t imu_data_outage_count;
// supercap active: status = 0, battery active: status = 1
static int supercap_status = BATTERY_ACTIVE;
std::atomic<int> supercap_event_count{0};
std::mutex sc_lock;
static int msg_idx = 0;
constexpr int DECREASE_SUPERCAP_THREAD_NICE_VALUE_BY = 5;
constexpr int MIN_NICE_VALUE_THREAD = -15;
// In case of continous BAD VOLTAGE condition device will reboot after 90 - 100 secs
// But if BAD VOLTAGE comes intermittently supercap event can come as well
// If supercap event comes more than max below, interrupt will be disabled
constexpr int MAX_SUPERCAP_EVENT_COUNT_B2 = 100;
constexpr int MAX_SUPERCAP_EVENT_COUNT_OTHERS = 10000;
bool max_supercap_event_count_reached = false;
const std::string first_supercap_event_count = "1";
const int64_t delay_supercap_event_seconds = 10; // For draining the initial supercap events after power on


void SC_worker::send_sc_status(bool force) {

    supercap_msg_t supercap_msg = {0};
    supercap_msg.status = BATTERY_ACTIVE;

    if((true == force) && (false == is_data_outage()) && (getMotionState() == STATIONARY)) {

        // Set the count and then force to MOVING state
        set_decision_count(eAPM_Decision_Engine_Off, 0);
        set_decision_count(eAPM_Decision_Moving, PFI_MAX_DEBOUNCE_COUNT);
        setMotionState(MOVING);
        supercap_msg.status = SUPERCAP_ACTIVE;

        if(false == use_legacy) {
            reset_bit(get_signal_mask());
        }

        update_supercap_backup_count(true); // Reset count to 0

        LOG_C(TAG, "SUPERCAP : Detected SUPERCAP_ACTIVE state");
    }
    else if((getMotionState() == STATIONARY) && (false == force)) {
        supercap_msg.status = BATTERY_ACTIVE;
        send_health_event = false; // reset health event flag
    }
    else if((getMotionState() == MOVING) && (true == force)) {
        set_decision_count(eAPM_Decision_Debounce, (get_decision_count(eAPM_Decision_Debounce, get_decision_index()) + 1), is_event_driven);
        return ;
    }
    else { // MOVING && false == force

        if(false == is_data_outage() ) {
            update_supercap_backup_count();
            write_supercap_event_xattr(event_count_xattr_key, to_string(get_supercap_backup_count()));
            write_supercap_event_xattr(event_volt_xattr_key, to_string(nd_device_obj->get_voltage_value(eCRANK_VOLT)));
        }

        if((true == force) && (getMotionState() == STATIONARY) ) {
            set_decision_count(eAPM_Decision_Debounce, (get_decision_count(eAPM_Decision_Debounce, get_decision_index()) + 1), is_event_driven);
        }
        return ;
    }

    // Sending message to nd-central before configuring wom
    if(false == send_msg( (generic_msg_t *)&supercap_msg, SUPERCAP_STATUS, sizeof(supercap_msg), "q_apm", "q_nd_central", msg_idx++ )) {
        LOG_E(TAG, "FAiled to send supercap status to NDC");
    }

    if(false == send_msg( (generic_msg_t *)&supercap_msg, SUPERCAP_STATUS, sizeof(supercap_msg), "q_apm", "q_power_monitor", msg_idx++ )) {
        LOG_E(TAG, "FAiled to send supercap status to Power Monitor");
    }

    if( SUPERCAP_ACTIVE == supercap_msg.status) {

        if (false == nd_device_obj->configure_imu_wom(apm_wom_enable)){
            LOG_E(TAG, " WOM Configuration Failed !!! apm_wom_enable(%d)", apm_wom_enable);
        } else {
            LOG_C(TAG, " WOM Configuration Successfull apm_wom_enable(%d)", apm_wom_enable);
        }

        // disable supercap interrupt
        if(false == nd_device_obj->configure_supercap_interrupt(nd_supercap_interrupt_state_t::eNDDisable)){
            LOG_E(TAG, "Failed to disable supercap interrupt after supercap active");
        }

        // This has to be force again to reset the counts properly.
        // As super cap and wom configurations taking more than 2 sec.
        set_decision_count(eAPM_Decision_Engine_Off, 0);
        set_decision_count(eAPM_Decision_Moving, PFI_MAX_DEBOUNCE_COUNT);

    }
    else {

        if(false == nd_device_obj->configure_supercap_interrupt(nd_supercap_interrupt_state_t::eNDEnable)){
            LOG_E(TAG, "Failed to enable supercap interrupt after battery active");
        }
        LOG_I(TAG, "SUPERCAP : Detected BATTERY_ACTIVE state");
    }

    //Increment Super Cap Event Count
    set_decision_count(eAPM_Decision_Debounce, (get_decision_count(eAPM_Decision_Debounce, get_decision_index()) + 1), is_event_driven);
    push_data_to_sysfs(PowermonParam::ePFI_VALID, 0); // Update PFI Validity to true
    if(true == force){
        string event_count_str = to_string(get_supercap_backup_count());
        string cur_time_str = to_string(get_system_time());
        write_supercap_event_xattr(event_timestamp_xattr_key, cur_time_str);
        write_supercap_event_xattr(event_count_xattr_key, to_string(get_supercap_backup_count()));
        write_supercap_event_xattr(event_volt_xattr_key, to_string(nd_device_obj->get_voltage_value(eCRANK_VOLT)));
    }
}

/*
 * Func name: filter_func()
 * function is used to update global mask bit if supercap status changes
 * returns SUCCESS on success
 */
void SC_worker::filter_func()
{
    if(false == use_legacy){
        LOG_D(TAG, "Legacy mode disabled, skipping supercap filter_func");
        return;
    }

    static int previous_supercap_status = SUPERCAP_ERROR;

    volatile int l_supercap_status = BATTERY_ACTIVE;
    {
        std::lock_guard<std::mutex> lk(sc_lock);
        l_supercap_status = supercap_status;
    }

    LOG_I (TAG,"SUPERCAP : Status:%d(%s), Prev Status:%d(%s)",l_supercap_status, (l_supercap_status == BATTERY_ACTIVE ? "BATTERY_ACTIVE" : "SUPERCAP_ACTIVE"), previous_supercap_status, (previous_supercap_status == BATTERY_ACTIVE ? "BATTERY_ACTIVE" : "SUPERCAP_ACTIVE") );

    if(previous_supercap_status != l_supercap_status) {
        LOG_I (TAG,"SUPERCAP : Detected supercap state change");
        if(l_supercap_status ==  BATTERY_ACTIVE) {
            set_bit(SC_MASK_POS);
        } else {

            reset_bit(SC_MASK_POS);
        }
        previous_supercap_status = l_supercap_status;

        supercap_msg_t supercap_msg = {0};
        supercap_msg.status = l_supercap_status;

        if(false == send_msg( (generic_msg_t *)&supercap_msg, SUPERCAP_STATUS, sizeof(supercap_msg), "q_apm", "q_power_monitor", msg_idx++ )) {
            LOG_E(TAG, "FAiled to send supercap status to Power Monitor");
        }

        // Sending message to nd-central before configuring wom
        if(false == send_msg( (generic_msg_t *)&supercap_msg, SUPERCAP_STATUS, sizeof(supercap_msg), "q_apm", "q_nd_central", msg_idx++ )) {
            LOG_E(TAG, "FAiled to send supercap status to NDC");
        }

        if( SUPERCAP_ACTIVE == supercap_status) {

            if (false == nd_device_obj->configure_imu_wom(apm_wom_enable)){
                LOG_E(TAG, " WOM Configuration Failed !!! apm_wom_enable(%d)", apm_wom_enable);
            } else {
                LOG_E(TAG, " WOM Configuration Successfull apm_wom_enable(%d)", apm_wom_enable);
            }
        }

        // Write supercap event count to sysfs
        if(false == write_into_sysfs_entry(get_sysfs_path_from_enum(PowermonParam::eSUPERCAP_COUNT), supercap_event_count)) {
            LOG_E(TAG, "Failed to write supercap event count to sysfs");
        }

    }
    return;
}

#ifdef KRAIT
void* gpio119_interrupt_thread_fn(void* args)
{
    LOG_I(TAG, "inside gpio119_interrupt_thread_fn");
    bool legacy_mode = (APM::WorkerInstance(ePowerFail))->use_legacy;

    int ret = -1;
    while(1) {
        ret = sysfs_init_interrupt_enable(119, GPIO_EVENT_BOTH);
        if(ret != 0) {
            LOG_E(TAG, "error exporting the super cap gpio");
            sleep(2);
            continue;
        }
        break;
    }

    int gpio119_fd;
    string gpio_level_info_file = "/sys/class/gpio/gpio119/value";
    gpio119_fd = open(gpio_level_info_file.c_str(), O_RDWR | O_NONBLOCK);
    if (gpio119_fd < 0) {
        string str_msg = "failed in open " + gpio_level_info_file;
        LOG_C(TAG, str_msg.c_str());
        nd_service_obj->send_err_msg(SM_E_PM_GPIO_INT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        return NULL;
    }

    char a = 'e';
    fd_set fds;
    if(false == legacy_mode){ // drain the first battery active state, it is based on true supercap events only not status 
        ret = read(gpio119_fd,&a,1);
        if(ret != 1){
            LOG_E(TAG, "failed to read appropriate value; %d, %s", ret, strerror(errno));
        }
    }

    while (1) {
        FD_ZERO(&fds);
        FD_SET(gpio119_fd, &fds);

        ret = select(gpio119_fd+1, NULL, NULL, &fds, NULL);
        if (ret > 0 && FD_ISSET(gpio119_fd, &fds)) {
            ret = lseek(gpio119_fd, SEEK_SET, 0);
            if(ret == -1) {
                LOG_E(TAG, "lseek error %d, %s", ret, strerror(errno));
                continue;
            }
            ret = read(gpio119_fd,&a,1);
            if(ret != 1){
                LOG_E(TAG, "failed to read appropriate value; continue, %d, %s", ret, strerror(errno));
                continue;
            }
            if(true == legacy_mode){
                sc_lock.lock();
                if(a == '0') {
                    supercap_status = 0;
                }
                else {
                    supercap_status = 1;
                }
                supercap_event_count++;
                bool was_supercap_active = (supercap_status_t::SUPERCAP_ACTIVE == supercap_status);
                sc_lock.unlock();
                bool call_filter_func = true;
                if((true == was_supercap_active) && (supercap_event_count > MAX_SUPERCAP_EVENT_COUNT_OTHERS)){
                    call_filter_func = false;
                }
                if(true == call_filter_func){
                    (APM::WorkerInstance(ePowerFail))->filter_func();
                }
            }else{
                SC_worker* sc_worker = static_cast<SC_worker*>(APM::WorkerInstance(ePowerFail));
                sc_worker->send_sc_status(true);
            }
        }
        LOG_I(TAG, "supercap status %c",a);

    }
    return NULL;
}
#endif

void* volt_interrupt_thread_fn(void* args)
{
    bool legacy_mode = (APM::WorkerInstance(ePowerFail))->use_legacy;
    if(true == legacy_mode) {
        LOG_I (TAG,"SUPERCAP : Legacy Mode Enabled, skipping volt_interrupt_thread_fn");
        return NULL;
    }

    LOG_I(TAG, "inside volt_interrupt_thread_fn");
    int pfi_stat_fd = -1;
    float supercap_voltage_threshold = (APM::WorkerInstance(ePowerFail))->get_threshold(APM_Decision::eAPM_Decision_Engine_Off);
    string pfi_trigger_sysfs_path = nd_factory_utils::get_pfi_trigger_sysfs_path();
    pfi_stat_fd = open(pfi_trigger_sysfs_path.c_str(), O_RDWR | O_NONBLOCK);
    if (pfi_stat_fd < 0) {
        string str_msg = "failed in open " + pfi_trigger_sysfs_path;
        LOG_C(TAG, str_msg.c_str());
        nd_service_obj->send_err_msg(SM_E_PM_GPIO_INT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg);
    }

    if(pfi_stat_fd < 0) {
        LOG_E(TAG, "pfi_stat_fd is invalid, exiting thread");
        return NULL;
    }

    char trigger_val = '\0';
    float power_line_voltage = nd_factory_utils::get_max_valid_voltage();;
    int ret = eSYSFS_READ_ERROR;
    fd_set fds;

    //writing initial trigger as 0
    LOG_I(TAG, "Initialising PFI trigger as %d", (int)0);
    write_into_sysfs_entry(pfi_trigger_sysfs_path, 0);

    //Drain initial state to avoid pending event for select unblock
    if (lseek(pfi_stat_fd, 0, SEEK_SET) < eSYSFS_RET_ZERO) {
        LOG_E(TAG, "initial lseek failed: %s", strerror(errno));
    } else {
        ret = read(pfi_stat_fd, &trigger_val, 1);//initial read to clear the prior interrupts
        if(ret <= eSYSFS_RET_ZERO){
            LOG_E(TAG, "failed to read appropriate value; %d, %s", ret, strerror(errno));
        }
    }
    // Skip the ADC/OBD spurious interrupts came in the first 5 seconds
    int64_t start_monotonic_time = get_system_monotonic_time();
    bool delay_supercap_event = true;
    int64_t no_of_event_in_delay = 0;

    while (1) {
        FD_ZERO(&fds);
        FD_SET(pfi_stat_fd, &fds);

        ret = select(pfi_stat_fd+1, NULL, NULL, &fds, NULL);
        if (ret > 0 && FD_ISSET(pfi_stat_fd, &fds)) {
            ret = lseek(pfi_stat_fd, SEEK_SET, 0);
            if(ret == -1) {
                LOG_E(TAG, "lseek error %d, %s", ret, strerror(errno));
                continue;
            }
            ret = read(pfi_stat_fd,&trigger_val,1);
            if(ret != 1){
                LOG_E(TAG, "failed to read appropriate value; continue, %d, %s", ret, strerror(errno));
                continue;
            }
            if((true == delay_supercap_event)){
                if(get_system_monotonic_time() <= (start_monotonic_time + S_TO_MS(delay_supercap_event_seconds))){
                    ++no_of_event_in_delay;
                    continue;
                }
                LOG_I(TAG,"Delaying supercap event count %d", no_of_event_in_delay);
                no_of_event_in_delay = 0;
                delay_supercap_event = false;
            }
            SC_worker* sc_worker = static_cast<SC_worker*>(APM::WorkerInstance(ePowerFail));
            sc_worker->send_sc_status(true);
        }
    }
    return NULL;
}

/*
 * Func name: scCallback()
 * function registered by SC_worker::intr_func ,
 * updated the supercap_state the sooner supercap_interrupt generates
 * returns MSP_SUCCESS on success and MSP_FAILURE on failure
 * 0-> SC Active(PFO Asserted) 1-> SC Inactive(PFO is not asserted, Battery Active)
 */
int scCallback(int scStatus)
{
    bool legacy_mode = (APM::WorkerInstance(ePowerFail))->use_legacy;
    if(true == legacy_mode){
        sc_lock.lock();
        if('1' == scStatus) {
            supercap_status = BATTERY_ACTIVE;
        } else if ('0' == scStatus) {
            supercap_status = SUPERCAP_ACTIVE;
        } else {
            supercap_status = BATTERY_ACTIVE;
            LOG_I(TAG, ":: SUPERCAP_ERROR  supercap_status %c(%d)", (char)scStatus, scStatus );
        }

        ++supercap_event_count;
        bool was_supercap_active = (supercap_status_t::SUPERCAP_ACTIVE == supercap_status);
        sc_lock.unlock();

        bool call_filter_func = true;
        if (true == was_supercap_active){
            // Only for bagheeera2 supercap interrupt will be disabled, upon receving supercap event
            if(false == nd_device_obj->configure_supercap_interrupt(nd_supercap_interrupt_state_t::eNDDisable)) {
                LOG_E(TAG,"erro while disabling interrupt");
            }
            // This logic is to cap the supercap event count, after reaching the max count supercap event will not be processed
            if((supercap_event_count > MAX_SUPERCAP_EVENT_COUNT_OTHERS)){
                call_filter_func = false;
            }
        }

        if(true == call_filter_func){
            APM_worker* sc_worker = APM::WorkerInstance(ePowerFail);
            if(sc_worker != NULL) {
                sc_worker->filter_func();
            } else {
                LOG_E(TAG, "Supercap worker not initialized yet, skipping filter_func call");
            }
        }
    }
    else{
        // Skip the ADC/OBD spurious interrupts came in the first 5 seconds
        static int64_t start_monotonic_time = get_system_monotonic_time();
        static bool delay_supercap_event = true;
        static int64_t no_of_event_in_delay = 0;
        if((true == delay_supercap_event)){
            if(get_system_monotonic_time() <= (start_monotonic_time + S_TO_MS(delay_supercap_event_seconds))){
                ++no_of_event_in_delay;
                return 0;
            }
            LOG_I(TAG,"Delaying supercap event count %d", no_of_event_in_delay);
            no_of_event_in_delay = 0;
            delay_supercap_event = false;
        }
        SC_worker* sc_worker = static_cast<SC_worker*>(APM::WorkerInstance(ePowerFail));
        sc_worker->send_sc_status(true);
    }
    return 0;
}

void SC_worker::insert_bin_data() {

    all_thread_keepalive_status &= ~(1 << get_signal_mask());

    PFIData pfi_data;
    pfi_data.status = nd_device_obj->get_supercap_status();
    pfi_data.voltage = nd_device_obj->get_voltage_value(eCRANK_VOLT);

    getWorkerBinDataObj()->set_bin_data(get_index(), (void *) &pfi_data);

    push_data_to_sysfs(PowermonParam::ePFI_VALID, 0); // Update PFI Validity to true

    // Before incrementing the index, get the decision counts to identify PFI event in last few seconds
    uint prev_moving_count = get_decision_count(eAPM_Decision_Moving, get_prev_decision_index());
    uint moving_count = get_decision_count(eAPM_Decision_Moving, get_decision_index());

    incr_index();

	// persist debounce count
	set_decision_count(eAPM_Decision_Debounce, 0);
    if(moving_count > prev_moving_count) {
        // reset engine off count if there is a PFI event.
        set_decision_count(eAPM_Decision_Engine_Off, 0, false, true);
        set_decision_count(eAPM_Decision_Moving, 0, false, true);

        if(false == send_health_event) {
            send_health_event = true;
            // To send critical info and health data
            APM_worker::send_signal_status(getMotionState(), true);
        }
    }
}

/*
 * Func name: SC_worker::poll_func()
 * function polls power_disc state after every APM_SLEEP_TIME
 * returns NULL on success and APM_ERROR on failure
 */
void* SC_worker::poll_func() {

	//TODO::Need to handle imu data outage for D450 in imu_worker. On Data Outage, we need to reconfigure IMU from nd_central.

    // If A SuperCap Active Clear Event is missed, then IMU Data Loss will be captured by imu_data_outage_count,
    // will be used by supercap thread to re-initialize the IMU.
    while(1){

        sleep(APM_SLEEP_TIME);
        // Trigger on Minimum 20 Seconds of IMU Data Outage
        if(true == nd_device_obj->reinit_imu_supported()){
        LOG_D(TAG, "imu_data_outage_count %d", imu_data_outage_count);
            if( (imu_data_outage_count % 2) > 0 ) {
                supercap_msg_t supercap_msg;
                if(true == use_legacy){
                    std::lock_guard<std::mutex> lk(sc_lock);
                    if(BATTERY_ACTIVE == supercap_status) {
                        supercap_msg.status = BATTERY_ACTIVE;
                    }
                }else{
                    motion_status_t cur_motion_state = APM::WorkerInstance(ePowerFail)->getMotionState();
                    if(STATIONARY == cur_motion_state) {
                        supercap_msg.status = BATTERY_ACTIVE;
                    }
                }
                bool rc = send_msg( (generic_msg_t *)&supercap_msg, SUPERCAP_STATUS,
                        sizeof(supercap_msg), "q_apm", "q_nd_central", msg_idx++ );
                if(rc == false) {
                    LOG_E(TAG, "FAiled to send supercap status to NDC, imu_data_outage_count %d:", imu_data_outage_count);
                } else {
                    LOG_I(TAG, "imu_data_outage_count %d, Message Sent to q_nd_central", imu_data_outage_count);
                }
            }
        }


	    // This Logic will now be State Machine Based 
        if(true == use_legacy) {

            int l_supercap_status = BATTERY_ACTIVE;
            {
                std::lock_guard<std::mutex> lk(sc_lock);
                l_supercap_status = supercap_status;
            }
            if(true == nd_device_obj->configure_supercap_interrupt_supported()) {
                if((false == max_supercap_event_count_reached) && (supercap_event_count > MAX_SUPERCAP_EVENT_COUNT_B2)){
                    if(false == nd_device_obj->configure_supercap_interrupt(nd_supercap_interrupt_state_t::eNDStop)) {
                        LOG_E(TAG,"erro while disabling interrupt");
                        continue;
                    }
                    LOG_C(TAG, "Supercap interrupt disabled as max event count reached");
                    nd_service_obj->send_err_msg(SM_E_APM_SUPERPCAP_STATUS, MAX_SUPERCAP_EVENT_COUNT_B2, "Supercap max event count reached");
                    max_supercap_event_count_reached = true;
                }
                else if(SUPERCAP_ACTIVE == l_supercap_status){
                    if(false == nd_device_obj->configure_supercap_interrupt(nd_supercap_interrupt_state_t::eNDEnable)) {
                        LOG_E(TAG,"erro while disabling interrupt");
                    }
                    scCallback(BATTERY_ACTIVE_CHAR_VAL);//BATTERY ACTIVE ON 11 Seconds with now SC Events.
                }
            }

        }
    }
    return NULL;
}

void SC_worker::test_callback_fn(int status) {
    send_sc_status(true);
}

/*
 * Func name: SC_worker::intr_func()
 * function creates an interrupt thread to onitor the supercap GPIO 119
 * returns NULL on success and APM_ERROR on failure
 */
void* SC_worker::intr_func() {

    // Write pfi trigger threshold to sysfs
    string pfi_trigger_threshold_path = nd_factory_utils::get_pfi_trigger_threshold_path();
    if(false == write_into_dev_shm_file(pfi_trigger_threshold_path, get_threshold(eAPM_Decision_Moving))) {
        LOG_E(TAG, "Failed to write pfi trigger threshold to sysfs");
    }
    // Write BATTERY_ACTIVE in ePFI_SRC, used for bagheera2 only to simulate pfi src gpio
    write_into_dev_shm_file(get_sysfs_path_from_enum(PowermonParam::ePFI_SRC), static_cast<uint64_t>(BATTERY_ACTIVE));

    // Moving SM thread Above so that, it doesn't inherit the nice of the interrupt thread.
    detect_engine_status_th = std::thread(&SC_worker::detect_engine_status, this, true);

    // Decreasing nice value of Supercap Interrupt thread by 5, to get supercap interrupt faster
    pid_t tid = syscall(SYS_gettid);
    int old_nice_val = getpriority(PRIO_PROCESS, tid);
    int set_nice_val = old_nice_val - DECREASE_SUPERCAP_THREAD_NICE_VALUE_BY;
    if(set_nice_val < MIN_NICE_VALUE_THREAD) set_nice_val = MIN_NICE_VALUE_THREAD; // limit nice value to -15
    setpriority(PRIO_PROCESS, tid, set_nice_val);
    int new_nice_val = getpriority(PRIO_PROCESS, tid);
    LOG_I(TAG, "APM Supercap thread with tid %d, nice value changed from %d to %d", tid, old_nice_val, new_nice_val);

    // Set initial supercap count to 0 in sysfs
    if(false == write_into_sysfs_entry(get_sysfs_path_from_enum(PowermonParam::eSUPERCAP_COUNT), supercap_event_count)) {
        LOG_E(TAG, "Failed to write supercap event count to sysfs");
    }

#ifdef KRAIT
    pthread_t gpio119_interrupt_thread;

    if(pthread_create(&gpio119_interrupt_thread, NULL, gpio119_interrupt_thread_fn, NULL) == 0) {
        LOG_I(TAG,"Power connect/disconnect intr thread created successfully");
    }
    else{
        LOG_E(TAG,"Power connect/disconnect intr thread create failed");
    }
#endif

	// Only If test file is present , start the test thread
    if ( true == file_is_present(get_test_file_path()) ) {
        test_event_driven_thread = std::thread(&APM_worker::test_event_driven_worker_fn, this);
    }

    pthread_t volt_interrupt_thread;

    if(pthread_create(&volt_interrupt_thread, NULL, volt_interrupt_thread_fn, NULL) == 0) {
        LOG_I(TAG,"Power connect/disconnect intr thread created successfully");
    }
    else{
        LOG_E(TAG,"Power connect/disconnect intr thread create failed");
    }

	while(1) {
		if( false == nd_device_obj->register_supercap_interrupt(&scCallback, use_legacy)) {
			LOG_E(TAG, "registercallback failed");
			nd_service_obj->send_err_msg(SM_E_APM_MSP_FAIL, 0, "supercap registercallback failed");
			sleep(2);
			continue;
		}
		//motion_status_t init_status = read_status();
		LOG_I(TAG, "initial super cap gpio %d", supercap_status);
		break;
	}

    // Join the engine status detection thread
    if(detect_engine_status_th.joinable()){
        detect_engine_status_th.join();
    }

    // Join the test event driven thread
    if(test_event_driven_thread.joinable()){
        test_event_driven_thread.join();
    }

#ifdef KRAIT
    pthread_join(gpio119_interrupt_thread, NULL);
#endif
    pthread_join(volt_interrupt_thread, NULL);

	return NULL;
}

/*
 * Func name: SC_worker::read_status()
 * function reads the current motion status
 * returns motion_status
 */
motion_status_t SC_worker::read_status() {

	if((false == enabled) || (true == sensor_decision_disabled)) {
		LOG_D(TAG, "SC worker(%d), sensor decision disabled(%d), return STATIONARY", enabled, sensor_decision_disabled);
		return STATIONARY;
	}

    std::string path = get_sysfs_path_from_enum(PowermonParam::ePFI_STAT);
    std::string dummy_gpio_path = get_sysfs_path_from_enum(PowermonParam::ePFI_SRC); // As pfi src not present on bagheera2 using dummy gpio

    if(false == APM::WorkerInstance(ePowerFail)->use_legacy) {

        if (STATIONARY == getMotionState()) {
            write_into_sysfs_entry(path, IGNITION_ON);
        }
        else if(MOVING == getMotionState()) {
            write_into_sysfs_entry(path, IGNITION_OFF);
        }
        else {
            LOG_E(TAG, "Unknown motion state : %d", getMotionState());
            write_into_sysfs_entry(path, IGNITION_OFF);
        }
        write_into_sysfs_entry(dummy_gpio_path, IGNITION_ON); // Write IGNITON ON / BATTERY ACTIVE to dummy pfi src always for non-legacy mode this will only be used for bagheera2
        return ((getMotionState() == MOVING) || is_data_outage())? STATIONARY : MOVING;
    }

    int sc_state = nd_device_obj->get_supercap_status();
    if( SUPERCAP_ERROR == sc_state ) {
        LOG_E(TAG, "unable to read Supercap status value; return error");
        nd_service_obj->send_err_msg(SM_E_APM_FILE_OPEN_FAIL, 0, "Supercap status read failed");
        sc_state =  BATTERY_ACTIVE;
    }

    sc_lock.lock();
    supercap_status = sc_state;
    sc_lock.unlock();

    if(sc_state ==  BATTERY_ACTIVE) {
	    write_into_sysfs_entry(path, IGNITION_ON);
        write_into_sysfs_entry(dummy_gpio_path, IGNITION_ON);
	    return MOVING;
    }

    write_into_sysfs_entry(path, IGNITION_OFF);
    write_into_sysfs_entry(dummy_gpio_path, IGNITION_OFF);
    return STATIONARY;
}

const char* SC_worker::get_TAG() {
    return TAG;
}

bool SC_worker::write_supercap_event_xattr(std::string key ,std::string value) {
    if(false == apm_attr_util::set_attr_status(event_count_file_path, key, value.c_str())) {
        LOG_E(TAG, "Failed to write supercap event count to xattr");
        return false;
    }
    return true;
}



