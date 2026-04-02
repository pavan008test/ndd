/* Copyright (C) 2023 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, January 2023
 */

#ifndef INC_ND_BLUETOOTH_MANAGER_H_
#define INC_ND_BLUETOOTH_MANAGER_H_

#include <atomic>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class nd_msgq_t;
class NDService;
class Config_parser;
class ND_DeviceFactory;

namespace nd {

namespace interface {
    class IState;
    class IBluetooth;
}

namespace utils {
    class TimerTick;
}

namespace helpers {
    class BleEventObserver;
    struct ServiceInfo;
}

namespace device {
class BtPersistenceDBHelper;
class BtAdsmStateManager;
class IdleState;
class MotionState;

class BTManager {
 public:
    BTManager();
    BTManager(const BTManager &) = default;
    BTManager(BTManager &&) = default;
    BTManager &operator=(const BTManager &) = default;
    BTManager & operator=(BTManager &&) = default;
    ~BTManager();

    bool Init();
    bool Run();

 private:

    struct BleDevicesData;
    struct ConfigData;
    struct DiscoveredDeviceAttributes;
    struct DriverAssociation;
    struct DriverData;
    struct NearbyDevicesData;
    struct NearbyAlertBeaconData;

#ifdef ENABLE_VEHICLE_QR_BASED_LOGIN
    struct DriverLoginAppQRAttributes;
#endif

    struct InstallerAppAttributes;
    struct NewBleDevicesNotifyData;
    struct RetryEventQueue;
    struct ScanEventsTimeout;
    struct ScannedLogins;
    struct SpeedServices;
    struct VehicleStateAttributes;
    struct DriverQrLogins;

    std::atomic<bool> nearby_devices_set_full_sent_{false};

    std::atomic<int> msg_id_{0};
    std::atomic<bool> stop_audio_loop_{true};
    std::atomic<bool> ble_alert_age_threshold_logged_{false};
    std::atomic<bool> is_qr_scan_active_{false};
    std::atomic<bool> is_qr_scan_required_{false};
    std::atomic<bool> is_qr_scan_allowed_{true};
    std::atomic<bool> stop_qr_messenger_{false};
    std::unique_ptr<ConfigData> config_data_;
    std::unique_ptr<SpeedServices> speed_services_;
    std::unique_ptr<VehicleStateAttributes> vehicle_state_attr_;
    std::unique_ptr<InstallerAppAttributes> installer_app_attr_;
#ifdef ENABLE_VEHICLE_QR_BASED_LOGIN
    std::unique_ptr<DriverLoginAppQRAttributes> driver_login_app_qr_attr_;
#endif
    std::unique_ptr<DriverAssociation> driver_association_;
    std::unique_ptr<nd::helpers::BleEventObserver> ble_observer_ptr_;
    std::unique_ptr<ScannedLogins> scanned_legacy_logins_;
    std::unique_ptr<DriverQrLogins> driver_qr_logins_;
    std::unique_ptr<nd::utils::TimerTick> cyclic_timer_tick_;
    std::unique_ptr<ScanEventsTimeout> scan_events_timeout_;
    std::unique_ptr<BtPersistenceDBHelper> persistence_ptr_;
    std::unique_ptr<BtAdsmStateManager> adsm_state_mgr_;
    std::unique_ptr<NewBleDevicesNotifyData> new_ble_devices_ptr_;
    std::unique_ptr<RetryEventQueue> retry_event_queue_ptr_;
    std::unique_ptr<void, void(*)(void*)> qr_client_builder_ptr_;

    std::shared_ptr<NearbyDevicesData> nearby_devices_data_ptr_;
    std::shared_ptr<NearbyAlertBeaconData> nearby_alert_beacon_data_ptr_;

    std::shared_ptr<BleDevicesData> ble_devices_data_ptr_;
    std::shared_ptr<nd::interface::IState> error_state_ptr_;
    std::shared_ptr<nd::interface::IState> idle_state_ptr_;
    std::shared_ptr<nd::interface::IState> init_state_ptr_;
    std::shared_ptr<nd::interface::IState> motion_state_ptr_;
    std::shared_ptr<nd::interface::IState> current_state_ptr_;

    std::shared_ptr<nd::interface::IBluetooth> bt_interface_ptr_;
    std::shared_ptr<nd::interface::IBluetooth> bt_interface_backup_ptr_;

    std::promise<void> installer_device_found_promise_;

    bool is_exit_main_loop_set_ = false;
    bool is_uninterrupted_scan_required_ = false;
    bool restart_from_same_boot_ = false;
    bool recovery_window_duration_expired_ = true;
    bool is_adsm_watchdog_registered_ = false;

    nd_msgq_t *server_mq_ = nullptr;
    NDService *service_obj_ptr_ = nullptr;
    ND_DeviceFactory *device_factory_ptr_ = nullptr;
    std::thread login_audio_play_th_;
    std::mutex login_audio_play_mutex_;
    std::condition_variable login_audio_play_cv_;

    std::thread qr_messenger_client_th_;

    int64_t steady_service_start_time_;

    enum class States {
        kInit,
        kIdle,
        kMotion,
        kError,
    };

    enum class InitErrCode {
        kOk = 0,
        kMQServerFail,
        kConfigFail,
        kStateInitsFail,
        kDependencyFail,
        kBtModuleFail,
        kBtScanFail,
        kGenericFail,
        kPersistencyFail,
    };

    enum class BtStackErr {
        kOk = 0,
        kEnableFailed,
        kDisableFailed,
    };

    enum class BleButtonErr {
        kOk = 0,
        kPacketAgeExceeded,
    };

#ifdef ENABLE_VEHICLE_QR_BASED_LOGIN
    enum class DriverAppLoginWithQrCode {
        kOk = 0,
        kInvalid,
    };
#endif

    enum class SpeedServiceEvents {
        kIdleEngine,
        kIdlePrivacy,
        kIdleLogin,
        kIdleLoginFr,
        kSpeedLogin,
        kSpeedPrivacy,
        kIdleQrLogin,
        kSpeedQrLogin
    };

    enum class FilterKeys {
        kBlePair,
        kBleUserAlert,
        kBleBatteryStatus,
        kInstallerApp,
        kInstallerAppUpdate,
        kDriverLoginEddyStone,
        kDriverLoginAppleBackgroundService,
        kDriverLoginService,
        kNearbyDevices,
        kDriverLoginQR,

#ifdef ENABLE_VEHICLE_QR_BASED_LOGIN
        kVehicleQr,
#endif

        kVBUS,
    };

    enum class AssociationCallStatus {
        kAssociated,
        kDisassociated,
        kCallFailure
    };

    enum class AudioInitiator {
        kBt,
        kQr,
        kMax
    };

    enum class LoginRegisters {
        kDriverQr,
    };

    // struct FilterAttributes;
    std::unordered_map<FilterKeys, uint32_t> registered_filter_id_map_;
    std::unordered_set<LoginRegisters> login_registers_set_;
    std::unordered_map<std::string, int64_t> current_scanned_drivers_;
    std::unordered_map<std::string, uint64_t> rate_limiter_write_char_map_;
    std::uint64_t driver_session_start_time_ = {};
    std::uint64_t remaining_login_scan_count_ = {};
    std::atomic<std::uint64_t> remaining_login_audio_prompt_count_ = {};

    std::vector<std::string> failed_health_msgs_;

    States current_state_ = States::kInit;

    friend class IdleState;
    friend class MotionState;

    bool AdvertiseUpdateCheckResponse();
    bool AdvertiseOtaVersion() const;
    bool BeginAudioPlayThread(AudioInitiator initiator, const std::string &state);
    void CleanUp();
    bool ClearAlertFilter();
    bool ClearBatteryStatusFilter();
    bool ClearBlePairFilter();
    bool ClearDriverLoginLegacyFilters();
    bool ClearDLRegister(LoginRegisters key);
    bool ClearFilter(FilterKeys key);
    bool ClearInstallerAppFilter();
#ifdef ENABLE_VEHICLE_QR_BASED_LOGIN
    bool ClearDriverLoginAppQRFilter();
#endif
    bool ClearVBUSFilter();
    std::unique_ptr<DiscoveredDeviceAttributes> DiscoverDevice(const std::string &addr, uint32_t timeout_secs);
    std::pair<AssociationCallStatus, std::string> DoesAssociationExistInCloud();
    bool EnableBTStartScan(bool force_enable = false);
    bool HandleGenericEvent(void *msg);
    bool HandleGenericRetryEvents();
    bool HandleRetryEventsInIdleState();
    bool HandleRetryEventsInMotionState();
    // NOTE: Implement when required
    // bool HandleIdleStateEvent(void *msg);
    bool HandleInitStateEvent(void *msg);
    bool HandleInstallerAppDetectionTimeout();
    bool HasDriverLoggedIn() const;
    bool SendInstallerScanLedMessage(const bool do_led_blink, const uint16_t timeout_sec,
                                     const char *src, const char *dest);
    bool WriteInstallerScanState(const std::string &state);
    bool CleanupStaleInstallerScanState();
    // NOTE: Implement when required
    // bool HandleMotionStateEvent(void *msg);
    // bool HoldForDependentServices();
    bool InitMQ();
    bool InitStates();
    bool IsFilterSet(FilterKeys key);
    bool MoveToState(States state);
    bool ReadBleDeviceList();
    bool ReadConfigData();
    bool ReloadBtStack();
    bool ReloadVehicleData();
    bool RegisterAlertFilter();
    bool RegisterNearbyDevicesFilter();
    void ReportNearbyDevicesToHealthStats();
    void ReportNearbyAlertBeaconToHealthStats();
    void ReportBleAlertToHealthStats(uint8_t seq, uint8_t age_secs, const std::string &mac, int32_t rssi);
    void ReportPeriodicNearbyDevices();
    void ReportPeriodicNearbyAlertBeaconDevices();
    bool RegisterBatteryStatusFilter();
    bool RegisterBlePairFilter();
    bool RegisterDriverLoginLegacyFilter();
    bool RegisterForIdleEvent(SpeedServiceEvents event);
    bool RegisterForSpeedEvent(SpeedServiceEvents event);
    bool RegisterForSpeedServices();
    bool RegisterVBUSFilter(const std::string &mac_addr);
    bool ReadVBUSAddress(std::string &vbus_mac_out);
    bool RetryHealthData();
    bool StartDriverLoginLegacyAdvertisement();
    bool StopDriverLoginLegacyAdvertisement();
    bool StartDriverLoginQRActivity(const std::string &state);
    bool StopDriverLoginQRActivity(const std::string &state, bool turn_scan_off = true);
    bool StopScanDisableBT(bool force_disable = false);
    bool TrySendStartQr();
    bool UnregisterForSpeedServices();
    bool UnregisterIdleEventHandle(int32_t handle);
    bool UnregisterSpeedEventHandle(int32_t handle);
    bool UpdateEngineIdleStatusToObservers();
    bool UpdateIdleFrStatusToObservers();
    bool UpdatePrivacyStatusToObservers();
    bool VerifyAndInitiateWiFiHotspot();
    bool VerifyAndTransitTo(States state, std::shared_ptr<nd::interface::IState> &state_ptr);
    std::future<void> RegisterInstallerAppFilter(bool needs_instant_notify);
    std::string PrepareUpdateCheckAdvPacket() const;
    std::string PrepareOtaAdvPacket() const;
    std::string GetMinimizedOTAString() const;
    void AddToPendingHealthData(std::string &&health_msg);
    void BeginBleDevicesIdentification();
    void BeginInstallerActivity(bool do_led_blink = false);
    void BeginLegacyLoginActivityIfEnabled();
    void BeginEnhancedLoginActivityIfEnabled(AudioInitiator initiator, const std::string &state);
    void BeginQrLoginActivityIfEnabled(AudioInitiator initiator, const std::string &state);
    void ClearDLSession();
    void ClearLoginAudioPrompt();
    void CheckAndReportInactiveBeacons();
    void CheckKAReloadStack();
    void CyclicTimerTickCB(uint64_t steady_now, uint64_t system_now);

#ifdef ENABLE_VEHICLE_QR_BASED_LOGIN
    void DoDriverLoginAppQrActivity();
#endif

    void DoDriverLoginActivity();
    void DoInstallerAppWriteChar();
    void DisassociateDriver();
    std::string GetDLScanHash();
    void HandleDeviceWriteChar(void *g_msg, const std::string &client_name);

#ifdef ENABLE_VEHICLE_QR_BASED_LOGIN
    void HandleDriverLoginQrScanTimeout();
#endif

    void HandleDriverLoginQuery(void *msg);
    void HandleIdleEventRegistrationResponse(void *g_msg);
    void HandleIdleEventUpdateInIdleState(void *g_msg);
    void HandleIdleEventUpdateInMotionState(void *g_msg);
    void HandleIdleIgnitionStatus(void *g_msg);
    void HandleIgnitionStatus(void *g_msg);
    void HandleInternalEvent(void *msg);
    void HandleLoginCompleteInIdle();
    void ProcessDriverLoginQrData(const std::string &qr_data);
    void HandleNewBleAlertDevice(void *msg);
    void HandleRequestAntennaTime(void *msg);
    void ResetAntennaTime(void *msg);
    void HandleBagheeraRestartDoneMessage();
    void HandleQrLoginUpdate();
    void HandleSpeedEventRegistrationResponse(void *g_msg);
    void HandleSpeedEventUpdateInIdleState(void *g_msg);
    void HandleSpeedEventUpdateInMotionState(void *g_msg);
    void HandleStartQrScanResponse(void *g_msg);
    void HandleStopQrScanResponse(void *g_msg);
    void HandleVBUSDetectionTimeout();
    void HandleVBUSAvailability();
    void InitiateDriverLoginBtActivity();
    void InitiateDriverLoginQrActivity(const std::string &state);
    void MsgLoop();
    void NotifyDriverLoginsToObservers();
    void PlayLoginAcknowledgeAudio(bool is_forced = false);
    void ReadEnhancedLegacyLoginCharacteristics();
    void ReadLegacyLoginCharacteristics();
    void ReInitiateInstallerActivity();
    void RegisterForAdsmWatchdogTimer();
    void RegisterForRecoveryTimeout();
    void HandleAdsmWatchdogTimeout();
    void RegisterBtStackReload();
    void RegisterDriverLoginAppFilter();

#ifdef ENABLE_VEHICLE_QR_BASED_LOGIN
    void RegisterDriverLoginAppQRFilter();
#endif

    void RegisterInactiveBeaconCheck();
    void RegisterHealthDataRetry();
    void RegisterPeriodicKACheck();
    void RegisterSpeedEventRegRetry();
    void ReportBatteryToHealthStats(uint32_t voltage_perc, uint64_t tstamp, const std::string &mac,
                                    const std::string &label, const int32_t rssi);
    void RegisterStartQrMsgRetry();
    void ReportBleDeviceConfigToHealthStats(const std::string &mac, const std::string &label);
    void ReportDLScanStatusToHealthStats(const std::string &type, bool scan_started, const std::string &info);
    void ReportDLSessionStatusToHealthStats(bool session_started);
    void ReportDriveAudioInfoToHealthStats(const std::string &state, const std::string &desc, bool is_played);
    void ReportDriveAssignmentToHealthStats(const std::vector<std::string> &ids, const std::string &source);
    void ReportScannedLoginStatusToHealthStats(const std::vector<std::string> &scanned_logins_status);
    void RestartBluetoothActivities();
    void RunAudioPlayLoop(AudioInitiator initiator, std::string trigger_state);
    void ScanFor(uint64_t seconds);
    void SetNewDLScanHash();
    void StopDriverLoginBtLegacyDetectionActivity(bool keep_bt_on = false);
    void StopAudioPlayLoop();
    void UnSubscribeToQrLogins();
    void SubscribeToQrLogins();
    void TriggerDriverLoginAudio();
};

} // namespace device

} // namespace nd

#endif  // INC_ND_BLUETOOTH_MANAGER_H_
