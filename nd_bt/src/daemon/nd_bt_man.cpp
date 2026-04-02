/* Copyright (C) 2023 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, January 2023
 */

#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>

#include <chrono>
#include <fstream>
#include <list>
#include <limits>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include <jansson/jansson.h>

#include <nd_accessory_db.h>
#include <nd_bt_ble_observer.h>
#include <nd_bt_constants.h>
#include <nd_bt_device_interface.h>
#include <nd_bt_factory.h>
#include <nd_bt_idle_state.h>
#include <nd_bt_man.h>
#include <nd_bt_motion_state.h>
#include <nd_bt_persistence_helper.h>
#include <nd_bt_platform_specific.h>
#include <nd_curl_helper.h>
#include <nd_security_crypt.h>
#include <nd_utils.h>

#include <nd_timer.h>

#include <config_parser.h>
#include <data_recording.h>
#include <log.h>
#include <nd_configurator.h>
#include <nd_ext_cam_utils.h>
#include <nd_factory.h>
#include <nd_messenger.h>
#include <nd_msg_types.h>
#include <nd_msg_utils.h>
#include <nd_net_utils.h>
#include <nd_utils.h>
#include <system_utils.h>

#include <nd_data_security.h>

namespace nd {

namespace device {

using nd::constants::kBagheeraConfigFile;
using nd::constants::kCameraOverrideFile;
using nd::constants::kDeviceConfigFile;
using nd::constants::kNDConfigFile;
using nd::constants::kNDDeviceConfigFile;
using nd::constants::kRebootIndicatorFile;
using nd::constants::kDriverAppLoginIndicatorFile;
using nd::constants::kNDCoreCommonConfigFile;
using nd::constants::kSpeedInfoFile;
using nd::constants::kNDHomePath;
using nd::constants::kNDTempPath;

using nd::helpers::AdvertisementBase;
using nd::helpers::BeaconInfo;
using nd::helpers::ISpecification;
using nd::helpers::ServiceInfo;
using nd::helpers::SpecificationType;
using nd::utils::Translator;

static constexpr char kBtServiceTag[]                     = "nd_bt_man";

static constexpr char kLogTag[]                           = "BTMAN";

static constexpr char kBT_Server_MQ_Name[]                = "BTFV";
static constexpr char kWiFi_Server_MQ_Name[]              = "WIFI_MGR";
static constexpr char kAwsIot_Pub_Server_MQ_Name[]        = "AWSIOT_PUB";
static constexpr char kPower_Server_MQ_Name[]             = "q_power_monitor";
static constexpr char kNDCentral_Server_MQ_Name[]         = "q_nd_central";
static constexpr char kSpeed_Server_MQ_Name[]             = "SPEED";
static constexpr char kSvc_Server_MQ_Name[]               = "Q_SVC";
static constexpr char kObd_Server_MQ_Name[]               = "OBD_PUB";
static constexpr char kInstaller_Server_MQ_Name[]         = "installer_queue";

static constexpr char kInstallerAppPattern[]              = "NTDI:";
static constexpr char kInstallerAppUpdateCheckPattern[]   = "NI:";
static constexpr char kInstallerAppShortPattern[]         = "ND:";
static constexpr char kDriverLoginAppPattern[]            = "NTDA:";

static constexpr char kDriverAppLoginDriverIdString[]     = "app_driver:";
static constexpr char kUnassignedDriverAppLoginString[]   = "app_driver: unassigned_driver";

static constexpr char kDriverLegacyLoginDriverInfo[]      = "149C3F2B-0CEB-494F-9C85-F4952D1CB2B0";
static constexpr char kDriverAppLoginTimeInfo[]           = "A56748A2-14C8-4E95-B637-8A371A9856F2";
static constexpr char kInstallerAppWriteCharUuid[]        = "6BAF94A2-885E-49B6-AA4C-FC937345555F";
static constexpr char kDriverLegacyLoginBeaconUuid[]      = "71B2F81728754835A94EF9B6C31A5A24";
static constexpr char kOtaAdvertiseUuid[]                 = "4E44";  // 4E -> N, 44 -> D
// static constexpr char kDriverAppLoginStopBeacon[]         = "4FE879EC-B40A-11EC-B909-0242AC120002";
static constexpr char kDriverAppLoginVehicleIdInfo[]      = "4FBE9F74-AB67-11EC-B909-0242AC120002";
// static constexpr char kDriverAppLoginLicenceInfo[]        = "933EBAA0-249B-11EE-BE56-0242AC120002";
// static constexpr char kDriverAppLoginVehicleNumInfo[]     = "B749D89E-249B-11EE-BE56-0242AC120002";
static constexpr char kDriverAppLoginDriverInfo[]         = "D96FE63E-B638-11EC-B909-0242AC120002";
static constexpr char kVehicleHashInfo[]                  = "B749D89E-249B-11EE-BE56-0242AC120002";

static constexpr char kBleDriverAssigneeLabel[]           = "driver";
static constexpr char kBlePassengerAssigneeLabel[]        = "passenger";
static constexpr char kBleTrailerAssigneeLabel[]          = "trailer";

static constexpr char kBleMokoVendorString[]              = "MOKO";
static constexpr char kBleMinewVendorString[]             = "MINEW";

static constexpr bool kIsConfigValueOverridden            = true;

static constexpr uint32_t kDefaultDLMaxRetry              = 30;
static constexpr uint32_t kDefaultDLMaxRetryWithVP        = 10;
static constexpr uint32_t kDefaultDLTime                  = 10;
static constexpr uint32_t kDefaultDLSpeed                 = 15;
static constexpr uint32_t kDefaultDLv2Time                = 0;
static constexpr float kDefaultDLv2Speed                  = 0;
static constexpr uint32_t kDefaultReloadStackInterval     = 0; // 0 minutes
static constexpr uint32_t kMaxReloadStackInterval         = 2880; // 48 hours, max limit only to match with B2 cyclic reboot
static constexpr uint32_t kDefaultScanTimeBtwCharOps      = 0; // in seconds; 0 -> NO SCAN
static constexpr uint32_t kMaxScanTimeBtwCharOps          = 20; // 20 seconds that matches with complete 20 seconds broadcast of BLE Button
static constexpr uint32_t kDefaultDLIdleTime              = 300;
static constexpr uint32_t kMinDLIdleTime                  = 0;
static constexpr uint32_t kDefaultDLIdleSpeed             = 0;
static constexpr uint32_t kDefaultDLv2IdleTime            = 0;
static constexpr uint32_t kDefaultDLv2IdleSpeed           = 0;
// FR -> Face Recognition?
static constexpr uint32_t kDefaultDLFRIdleTime            = 300;
static constexpr uint32_t kDefaultDLFRIdleSpeed           = 0;

static constexpr uint32_t kDefaultPrivacyDisableTime      = 5;
static constexpr uint32_t kDefaultPrivacyDisableSpeed     = 5;
static constexpr uint32_t kDefaultPrivacyEnableTime       = 180;
static constexpr uint32_t kDefaultPrivacyEnableSpeed      = 0;

static constexpr uint32_t kDefaultEngineIdleEnableTime    = 300;
static constexpr uint32_t kDefaultEngineIdleEnableSpeed   = 0;

static constexpr uint32_t kOTAbroadcastDurationDefault    = 7;
static constexpr uint32_t kInstLEDBlinkTimeout= 240; // in seconds

static constexpr uint32_t kInstallerAppReScanTimeout      = 5;
static constexpr uint32_t kInstallerFreshScanTimeout      = 120;

// static constexpr uint32_t kDriverLoginQRScanTimeout    = 60;

#ifdef ENABLE_VEHICLE_QR_BASED_LOGIN
static constexpr uint32_t kDriverLoginQRScanMaxTimeout    = 300;
#endif

static constexpr uint32_t kBleDevicesIdentificationTimeout = 120;

static constexpr uint32_t kMaxBtEnableRetryCount          = 3;
static constexpr uint32_t kMaxSpeedEventRegistrationRetryCount = 5;

static constexpr uint32_t kTimerTickInterval              = 1;

static constexpr uint32_t kMaxDriverLoginLegacyScanCount  = 4; // 0 to 4 -> 5 times

static constexpr int32_t kDriverLoginLegacyBeaconInterval = 0x00A0; // 0xA0 * 0.625ms = 100ms
static constexpr uint32_t kDriverLoginLegacyBroadcastDuration = 30; // 30 seconds
static constexpr uint32_t kAddToBtEndTime                 = 60000; //Extra 1 minute added to BT scan end time.

static constexpr uint32_t kMaxDelayBetweenAssociationRetries = 1000; //in ms -> 1 second

static constexpr uint64_t kDefaultBleBatteryCapacity      = 3000;    // in millivolts
static constexpr uint64_t kDefaultBleLowBatteryThreshold  = 30;      // in percentage
static constexpr uint64_t kMinBleLowBatteryThreshold      = 1;      // in percentage
static constexpr uint64_t kMaxBleLowBatteryThreshold      = 99;      // in percentage

static constexpr uint64_t kBleBatteryHealthUpdateInterval = 9 * 60; // in seconds -> 540 seconds = 9 minutes
static constexpr uint64_t kBleBatteryHealthUpdateTimeout  = 11 * 60; // in seconds -> 660 seconds = 11 minutes
static constexpr uint64_t kBleKAHealthTimeoutMaxInterval  = 3 * 20 * 60; // in seconds -> 3600 seconds = 60 minutes
static constexpr uint64_t kBleAlertTriggerInterval        = 5; // in seconds
static constexpr uint64_t kBleButtonPacketInterval        = 7; // in seconds

static constexpr uint64_t kRetryHealthDataRetryInterval   = 60; // in seconds

static constexpr int32_t kDLDefaultAudioInterval          = 60;
static constexpr int32_t kDLMinAudioInterval              = 10;
static constexpr int32_t kDLMaxAudioInterval              = 3600;
static constexpr int32_t kDLAudioMaxCount                 = 100;
static constexpr int32_t kDLAudioMinCount                 = 0;
static constexpr int32_t kDLDefaultAudioDefCount          = 3;
static constexpr uint64_t kDefaultEventRetryInterval      = 5; // in seconds
static constexpr uint64_t kDownTimeDuration               = 5 * 60; // 5 minutes in seconds
static constexpr uint64_t kRecoveryDuration               = 5 * 60; // 5 minutes in seconds
static constexpr uint64_t kAdsmWatchdogTimeOut            = 60; // in seconds
static constexpr uint64_t kMinAdsmRecoveryDuration        = 60; // 1 minute in seconds
static constexpr uint64_t kMaxAdsmRecoveryDuration        = 600; // 10 minutes in seconds
static constexpr uint64_t kMinAdsmDownTimeDuration        = 60; // 1 minute in seconds
static constexpr uint64_t kMaxAdsmDownTimeDuration        = 600; // 10 minutes in seconds

static constexpr uint32_t kVBUSDetectionTimeout           = ((kInstallerFreshScanTimeout > 5) ? kInstallerFreshScanTimeout - 5
                                                                                               : kInstallerFreshScanTimeout); //5 seconds lesser than
                                                                                             // installer app detection

static constexpr uint64_t kMacAddressLength               = 17;

static constexpr uint32_t kDefaultRelaxedScanWindow       = 48; // 48 * 0.625 ms = 30 ms
static constexpr uint32_t kDefaultRelaxedScanInterval     = 96; // 96 * 0.625 ms = 60 ms

static constexpr uint32_t kDefaultScanWindow              = 18; // 18 * 0.625 ms = 11.25 ms
static constexpr uint32_t kDefaultScanInterval            = 18; // 18 * 0.625 ms = 11.25 ms

static constexpr uint32_t kMinScanWindow                  = 0x0004; // 4 * 0.625 ms = 2.5 ms
static constexpr uint32_t kMinScanInterval                = 0x0004; // 4 * 0.625 ms = 2.5 ms

static constexpr uint32_t kMaxScanInterval                = 0x4000; // 16384 * 0.625 ms = 10.24 seconds
static constexpr uint32_t kMaxScanWindow                  = kMaxScanInterval;

static constexpr uint32_t kRelaxedScanTimeout             = 8; // in seconds
static constexpr uint32_t kMinRelaxedScanTimeout          = 2; // in seconds
static constexpr uint32_t kMaxRelaxedScanTimeout          = 60; // in seconds

// ND:BLE in hex. Netradyne device tag. Used as KeepAlive BLE packet
// static constexpr char kDefaultBleDeviceTag            = "4E443A424C45";
// ND:UALRT in hex. The Alert BLE packet must begin with this tag
static constexpr char kDefaultBleUuidAlertTag[]           = "4E443A55414C5254";
static constexpr char kDefaultBleLongPressTag[]           = "4E443A4C4F4E47";  // ND:LONG in hex.
static constexpr char kDefaultBleScannerHash[]            = "27A01DE70420D045F16E";

static constexpr size_t kRandomDriveHashSize              = 4;

static constexpr int64_t kDefaultAppLoginTime             = -1;

static constexpr uint32_t kDefaultRateLimiterCharSeconds  = 60;

static constexpr size_t kDefaultNearbyDevicesMaxSize = 50;
static constexpr uint64_t kNearbyDevicesReportIntervalSeconds = 30*60; // 30 minutes

static constexpr size_t kDefaultNearbyAlertBeaconMaxSize = 30;
static constexpr uint64_t kNearbyAlertBeaconReportIntervalSeconds = 30*60; // 30 minutes

static constexpr size_t kNearbyDevicesMinSize = 0;
static constexpr size_t kNearbyDevicesMaxSize = 100;

static constexpr size_t kNearbyAlertBeaconMinSize = 0;
static constexpr size_t kNearbyAlertBeaconMaxSize = 50;

struct BTManager::NearbyDevicesData {
    std::mutex data_lock_;
    std::unordered_set<std::string> data_set_;  // Just store MAC addresses
};

struct NearbyAlertBeaconInfo {
    uint32_t battery_pct_;
    int32_t live_speed_;
};

struct BTManager::NearbyAlertBeaconData {
    std::mutex data_lock_;
    std::unordered_map<std::string, NearbyAlertBeaconInfo> data_map_;  // mac_address -> beacon info
};

static constexpr int64_t kMaxQRScanDuration               = 3600; // 1 hour
static constexpr int64_t kMinQRScanDuration               = 60;  // 60 seconds
static constexpr int64_t kDefaultQRScanDuration           = 3600; // 1 hour

static constexpr uint32_t kIdleSpeedQRResumeDuration      = 5;
static constexpr uint32_t kIdleSpeedQRResumeThreshold     = 2;

static constexpr unsigned int kQrLoginClientRetryCount    = 300;
static constexpr unsigned int kQrLoginClientRetryTime     = 100;

static constexpr int64_t kQrLoginAckAudioIntervalSecs     = 10; // 10 seconds

static constexpr char kDefaultQrMessengerSocket[]         = "ipc:///dev/shm/MSGQ/10355";
static constexpr char kDefaultQrMessengerTopic[]          = "qr";

static constexpr char kAdsmStateDbName[]                  = "bt_adsm_state.db";
static constexpr char kQrScanStartIndicator[]             = "nd_bt_qr_scan_start.flag";


// Installer app LED blink constants
static constexpr char kInstallerScanStateNdBt[]           = "nd_bt";
static constexpr char kInstallerScanStateInstallerApp[]   = "installer_app";
static constexpr char kInstallerConnIndicatorFileName[]   = "installer_app_connected";
static constexpr char kInstallerScanIndicatorFileName[]   = "installer_scan_ongoing";

enum class QRLoginErrCode {
    kOk = 0,
    kScanStartTimeout,
};

enum class TimeoutEvents {
    kInstallerApp,
    kDriverLoginLegacy,
    kHotspotActive,
    kBleBatteryStatus,

#ifdef ENABLE_VEHICLE_QR_BASED_LOGIN
    kVehicleQr,
#endif

    kVBUS,
    kRelaxedScan,
    // kDriverLoginQrDetection,
};

enum class BleDeviceAssignees {
    kDriver,
    kPassenger,
    kTrailer,
};

enum class BeaconTypes {
    kAlertBeacon,
};

enum ButtonIds {
    // 0 and 1 are hardware buttons of driver i
    kButtonIdDriver = 2,
    kButtonIdPassenger,
    kButtonIdTrailer,
    kButtonIdMax
};

enum class DriverLoginFeature : uint32_t {
    kNone          = 0,
    kLegacy        = 1u << 0,
    kEnhancedBt    = 1u << 1,
    kDriverQR      = 1u << 2,
#ifdef ENABLE_VEHICLE_QR_BASED_LOGIN
    kVehicleQR     = 1u << 3,
#endif
    // add future bits here
};

// Enable bitwise ops (type-safe)
constexpr inline DriverLoginFeature operator|(DriverLoginFeature a, DriverLoginFeature b) {
    return static_cast<DriverLoginFeature>(
        static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr inline DriverLoginFeature operator&(DriverLoginFeature a, DriverLoginFeature b) {
    return static_cast<DriverLoginFeature>(
        static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

constexpr inline DriverLoginFeature& operator|=(DriverLoginFeature& a, DriverLoginFeature b) {
    a = a | b; return a;
}

constexpr inline bool HasFeature(DriverLoginFeature set, DriverLoginFeature f) {
    return (set & f) != DriverLoginFeature::kNone;
}

enum class InternalRetryEvents {
    kSpeedServiceRegistration,
    kBleBeaconKACheck,
    kInactiveBleBeaconCheck,
    kBtStackReload,
    kRetryHealthMsgs,
    kBleDevicePairTimeout,
    kNearbyDevicesReport,
    kNearbyAlertBeaconReport,
    kStartQrMsg,
    kRecoveryWindowTimer,
    kAdsmWatchdogTimer,
    kInvalid,
};

enum class InternalEvents {
    kNone,
    kNearbyDevicesSetFull,
    kQrLogins,
    kQrLoginAckAudio,
    kMax,
};

struct BTManager::ScanEventsTimeout {
    std::unordered_map<TimeoutEvents, uint64_t> data_map_;
    std::mutex data_lock_;
};

struct BTManager::DriverData {
    ServiceInfo info_;
    std::string id_charac_;
    std::string start_time_charac_;
};

struct BTManager::ScannedLogins {
    // mac_id <-> data
    std::unordered_map<std::string, DriverData> clients_;
    uint64_t timestamp_;
    std::mutex data_lock_;
};

struct BTManager::DriverQrLogins {
    // mac_id <-> data
    std::unordered_set<std::string> ids_;
    uint64_t timestamp_;
    std::mutex data_lock_;
};

struct BTManager::VehicleStateAttributes {
    ignition_status_t ign_status_ = IGNITION_OFF;
    bool wake_up_status_ = false;
    bool driver_login_idle_on_ = false;
    bool engine_idle_on_ = false;
    bool is_privacy_mode_active_ = false;
    bool idle_fr_status_ = false;
    bool does_current_speed_meets_login_threshold_ = false;
    bool qr_login_idle_on_ = false;
    std::atomic<bool> had_vehicle_moved_out_of_idle_post_login_{false};
};

struct BTManager::SpeedServices {
    int32_t idle_engine_handle_ = -1;
    int32_t idle_privacy_handle_ = -1;
    int32_t idle_login_handle_ = -1;
    int32_t idle_login_fr_handle_ = -1;
    int32_t speed_login_handle_ = -1;
    int32_t speed_privacy_handle_ = -1;
    int32_t idle_qr_handle_ = -1;
    int32_t speed_qr_handle_ = -1;
};

struct BleDeviceAssociatedData {
    // std::string uuid_;
    // std::string mac_addr_;
    uint64_t last_keep_alive_received_at_{0};
    uint64_t last_alert_trigger_received_at_{0};
    uint32_t battery_voltage_ {0};
    // Sequence byte (XX) of last valid button press; initialized to 0xFF (sentinel meaning 'none seen yet').
    // New button press beacons will advertise minor bytes as: [XX, YY]
    //   XX - increments per press and wraps after 0xFF back to 0x00
    //   YY - age of the broadcast since the original press in seconds (0x00 - 0x17)
    // Alert time for a press = current_time_ms - (YY * 1000)
    uint16_t last_press_seq_{0xFFFF};
};

using BleBeaconMajorMinor_t = std::array<uint8_t, 2>;

static constexpr BleBeaconMajorMinor_t kDriverUuidMajor = {0x01, 0x00};
static constexpr BleBeaconMajorMinor_t kPassengerUuidMajor = {0x02, 0x00};
static constexpr BleBeaconMajorMinor_t kTrailerUuidMajor = {0x03, 0x00};

// enum BleDeviceBeaconMajors {
//     kDriver = 1,
//     kPassenger,
//     kTrailer
// };

struct RetryEventData {
    InternalRetryEvents type_ {InternalRetryEvents::kInvalid};
    void *data_ {nullptr};
};

struct BTManager::RetryEventQueue {
    std::mutex data_lock_;
    std::multimap<uint64_t, RetryEventData> data_map_;
};

struct BleDeviceAssigneesConstants {
    BleBeaconMajorMinor_t major_;
    std::string label_;
    ButtonIds button_id_;
};

// static const std::unordered_map<BleDeviceAssignees, std::string> kBleDeviceAssigneesConstantsMap = {
//     {BleDeviceAssignees::kDriver, kBleDriverAssigneeLabel},
//     {BleDeviceAssignees::kPassenger, kBlePassengerAssigneeLabel},
//     {BleDeviceAssignees::kTrailer, kBleTrailerAssigneeLabel},
// };

static const std::unordered_map<InternalRetryEvents, uint64_t> kInternalRetryEventsTimeoutMap = {
    {InternalRetryEvents::kBleBeaconKACheck, kBleBatteryHealthUpdateTimeout},
    {InternalRetryEvents::kSpeedServiceRegistration, kDefaultEventRetryInterval},
    {InternalRetryEvents::kInactiveBleBeaconCheck, kBleKAHealthTimeoutMaxInterval},
    {InternalRetryEvents::kRetryHealthMsgs, kRetryHealthDataRetryInterval},
    {InternalRetryEvents::kBleDevicePairTimeout, kBleDevicesIdentificationTimeout},
    {InternalRetryEvents::kNearbyDevicesReport, kNearbyDevicesReportIntervalSeconds},
    {InternalRetryEvents::kNearbyAlertBeaconReport, kNearbyAlertBeaconReportIntervalSeconds},
    {InternalRetryEvents::kStartQrMsg, kDefaultEventRetryInterval},
    {InternalRetryEvents::kRecoveryWindowTimer, kRecoveryDuration},
    {InternalRetryEvents::kAdsmWatchdogTimer, kAdsmWatchdogTimeOut}
};

static const std::unordered_map<BleDeviceAssignees, BleDeviceAssigneesConstants> kBleDeviceAssigneesConstantsMap = {
    {BleDeviceAssignees::kDriver, {.major_ = kDriverUuidMajor, .label_ = kBleDriverAssigneeLabel, .button_id_ = kButtonIdDriver}},
    {BleDeviceAssignees::kPassenger, {.major_ = kPassengerUuidMajor, .label_ = kBlePassengerAssigneeLabel, .button_id_ = kButtonIdPassenger}},
    {BleDeviceAssignees::kTrailer, {.major_ = kTrailerUuidMajor, .label_ = kBleTrailerAssigneeLabel, .button_id_ = kButtonIdTrailer}}};

static const std::unordered_map<std::string, BleDeviceAssignees> kBleLabelAssigneeMap = {
    {kBleDriverAssigneeLabel, BleDeviceAssignees::kDriver},
    {kBlePassengerAssigneeLabel, BleDeviceAssignees::kPassenger},
    {kBleTrailerAssigneeLabel, BleDeviceAssignees::kTrailer}};

enum class DLScanInitiator {
    kOnIgnition,
    kOnIgnitionOff,
    kOnOutOfIdle,
    kOnIdle,
    kMotion,
    kWoM,
    kOnQrResumeIdle,
    kOnOutOfQrResumeIdle
};

static const std::unordered_map<DLScanInitiator, std::string> kDLScanInitiatorStrMap = {
    {DLScanInitiator::kOnIgnition, "on_ignition"},
    {DLScanInitiator::kOnIgnitionOff, "on_ignition_off"},
    {DLScanInitiator::kOnOutOfIdle, "on_out_of_idle"},
    {DLScanInitiator::kOnIdle, "on_idle"},
    {DLScanInitiator::kMotion, "motion"},
    {DLScanInitiator::kWoM, "wom"},
    {DLScanInitiator::kOnQrResumeIdle, "on_qr_resume_idle"},
    {DLScanInitiator::kOnOutOfQrResumeIdle, "on_out_of_qr_resume_idle"}
};

enum class DLSource {
    kCloud,
    kBT,
    kQR
};

static const std::unordered_map<DLSource, std::string> kDLSourceStrMap = {
    {DLSource::kCloud, "cloud"},
    {DLSource::kBT, "bt"},
    {DLSource::kQR, "qr"}};

struct BTManager::BleDevicesData {
    std::mutex data_lock_;
    // mac address , data
    std::unordered_map<BleDeviceAssignees, std::unordered_map<std::string, BleDeviceAssociatedData>> data_map_;
};

struct BTManager::NewBleDevicesNotifyData {
    std::mutex data_lock_;
    std::unordered_map<BleDeviceAssignees, std::vector<std::string>> data_map_;
};

struct BTManager::ConfigData {
    // std::unordered_map<BleDeviceAssignees, BleDeviceData> ble_assignee_data_map_;
    std::vector<std::string> installer_adv_tags_;
    std::vector<std::string> qr_login_tags_;
    std::string device_id_;
    std::string ota_version_;
    std::string ble_hash_;
    std::string device_vin_;   // "chasisNumber": VIN (just retained for earlier QR code flow)
    // std::string device_vno_;   // "engineNumber": -> vehicle Number
    // std::string device_lp_;    // "registrationNumber": Licence plate
    std::string vehicle_hash_;    // digest of all data
    std::string ble_vendor_name_;
    std::vector<uint8_t> uuid_alert_tag_;
    std::vector<uint8_t> long_press_tag_;
    std::string login_audio_file_;
    std::string qr_login_success_audio_file_;
    std::string qr_messenger_topic_;
    std::string qr_messenger_socket_;
    // std::vector<uint8_t> ble_device_tag_;

    DriverLoginFeature driver_login_features_{DriverLoginFeature::kNone};

    uint64_t driver_login_max_retry_{kMaxDriverLoginLegacyScanCount};
    // uint64_t enhanced_driver_login_app_max_retry_{kMaxDriverLoginLegacyScanCount};
    uint64_t driver_login_audio_play_count_{kDLDefaultAudioDefCount};
    uint64_t driver_login_audio_interval_{kDLDefaultAudioInterval};
    uint64_t driver_login_first_audio_interval_{kDLDefaultAudioInterval};
    uint64_t driver_login_speed_{0};
    uint64_t driver_login_time_{0};
    uint64_t driver_login_idle_speed_{0};
    uint64_t driver_login_idle_time_{0};
    uint64_t driver_login_fr_idle_speed_{0};
    uint64_t driver_login_fr_idle_time_{0};

    uint64_t relaxed_scan_window_{0};
    uint64_t relaxed_scan_interval_{0};

    uint64_t scan_window_{0};
    uint64_t scan_interval_{0};

    uint64_t relaxed_scan_max_timeout_{0};

    uint64_t reload_stack_interval_minutes_{0};
    uint64_t scan_time_btw_char_ops_{kDefaultScanTimeBtwCharOps};
    uint64_t rate_limiter_write_char_seconds_{kDefaultRateLimiterCharSeconds};

    uint64_t privacy_disable_speed_{0};
    uint64_t privacy_disable_time_{0};
    uint64_t privacy_enable_speed_{0};
    uint64_t privacy_enable_time_{0};

    uint64_t engine_idle_enable_speed_{0};
    uint64_t engine_idle_enable_time_{0};

    uint64_t ota_adv_duration_{0};

    // uint64_t ble_battery_capacity_{1};
    uint64_t ble_low_battery_threshold_{1};

    uint64_t nearby_devices_max_size_{0};
    uint64_t nearby_alert_beacon_max_size_{0};
    uint64_t keep_alive_miss_interval_{0};

    uint16_t installer_led_blink_timeout_sec_{0};

    uint64_t qr_max_scan_duration_{kMaxQRScanDuration};

    uint64_t idle_speed_qr_resume_threshold_{0};
    uint64_t idle_speed_qr_resume_duration_secs_{0};

#ifdef ENABLE_VEHICLE_QR_BASED_LOGIN
    bool driver_login_app_qr_enabled_{false};
    bool driver_app_login_qr_vehicle_idle_enabled_{false};
#endif

    bool driver_login_fr_enabled_{false};
    bool driver_login_coexist_with_visionpro_{false};

    bool privacy_mode_enabled_{false};
    bool default_privacy_status_{false};
    bool engine_idle_enabled_{false};

    bool driver_cam_enabled_{false};

    bool beacon_alert_enabled_{false};

    bool beacon_auto_pairing_enabled_{false};
#ifdef DO_FIRMWARE_FLASH
    bool toggle_firmware_flash_{false};
#endif

    bool requires_login_audio_on_ignition_on_{false};

    bool enable_login_audio_reminder_{false}; // global setting for login audio reminders
    bool driver_login_force_first_audio_play_{false};
    bool can_vd_enabled_{false};
    bool is_ble_button_auto_pair_enabled_{false};
    bool is_adsm_enabled_{false};
    uint64_t adsm_recovery_window_duration_{kRecoveryDuration}; // in seconds
    uint64_t adsm_downtime_duration_{kDownTimeDuration}; // in seconds
};

struct BTManager::InstallerAppAttributes {
    std::mutex data_lock_;
    std::string mac_address_;
    std::string name_;
};

struct BTManager::DiscoveredDeviceAttributes {
    std::string mac_address_;
    std::string name_;
};

#ifdef ENABLE_VEHICLE_QR_BASED_LOGIN
struct BTManager::DriverLoginAppQRAttributes {
    std::mutex data_lock_;
    // mac address, name
    std::unordered_map<std::string, std::string> data_map_;
};
#endif

struct BTManager::DriverAssociation {
    struct SessionData {
        std::string hash_;
        std::mutex mutex_;
        uint32_t audio_play_count_{0};
        uint32_t audio_skip_count_{0};
    };
    SessionData session_data_;
    std::unordered_map<std::string, int64_t> cached_scanned_drivers_;
    bool disassociate_on_idle_ = false;
    bool play_audio_on_out_of_idle_ = false;
    bool notify_on_disassociation_ = false;
};

inline auto CurrentSteadyClockSeconds() -> long long {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

inline auto CurrentSystemClockSeconds() -> long long {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

inline auto CurrentSteadyClockMilliSeconds() -> long long {
    return
    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

inline auto CurrentSystemClockMilliSeconds() -> long long {
    return
    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

inline std::string GetQrScanStartIndicatorPath() {
    return std::string(kNDTempPath) + kQrScanStartIndicator;
}

inline std::string GetInstallerConnIndicatorFile() {
    return std::string(kNDTempPath) + kInstallerConnIndicatorFileName;
}

inline std::string GetInstallerScanIndicatorFile() {
    return std::string(kNDTempPath) + kInstallerScanIndicatorFileName;
}

std::vector<std::string> SplitString(const std::string &str, char delimiter) {
    std::vector<std::string> elements;
    if ((!str.empty()) && ('\0' != delimiter)) {
        size_t prev_pos = 0, next_pos  = 0;
        while ((next_pos = str.find(delimiter, prev_pos)) != std::string::npos) {
            elements.emplace_back(str.substr(prev_pos, next_pos - prev_pos));
            prev_pos = next_pos + 1;
        }
        elements.emplace_back(str.substr(prev_pos));
    }
    return elements;
}

BTManager::BTManager():steady_service_start_time_(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count()),
                       qr_client_builder_ptr_(nullptr, [](void* ptr) {
                               if (ptr) {
                                   delete static_cast<NDMessenger::ClientBuilder*>(ptr);
                               }
                       }) {
}

BTManager::~BTManager() {
    CleanUp();
};

bool BTManager::ReadConfigData() {
    bool status = false;
    LOG_I(kLogTag, "Entered %s", __func__);
    constexpr char kConfigParamEnable[]           = "true";
    constexpr char kConfigParamDisable[]          = "false";
    constexpr char kConfigParamEnableAsNumeric[]  = "1";
    constexpr char kConfigParamDisableAsNumeric[] = "0";
    constexpr char kConfigParamEnableStr[]        = "enable";
    constexpr char kConfigParamDisableStr[]       = "disable";

    const bool get_override_val = true;

    nd::utils::Configurator configurator;
    /*file name*/   /*section*/ /*key*/ /*default*/

    configurator.Run({
        {kBagheeraConfigFile, {
            {"driverlogin", {
                {"enabled", {kConfigParamDisable, get_override_val}},
                {"driveri_app_login", {kConfigParamDisable, get_override_val}},
#ifdef ENABLE_VEHICLE_QR_BASED_LOGIN
                {"driveri_app_qr_login", {kConfigParamDisable, get_override_val}},
                {"vehicle_idle_driver_login", {kConfigParamDisable, get_override_val}},
#endif
                {"bt_max_retry", {std::to_string(kDefaultDLMaxRetry), get_override_val}},
                {"coexist_with_visionpro", {kConfigParamDisable, get_override_val}},
                {"bt_scan_retry_vp_enabled", {std::to_string(kDefaultDLMaxRetryWithVP), get_override_val}},
                {"login_speed", {std::to_string(kDefaultDLSpeed), get_override_val}},
                {"login_time", {std::to_string(kDefaultDLTime), get_override_val}},
                {"idle_speed", {std::to_string(kDefaultDLIdleSpeed), get_override_val}},
                {"idle_time", {std::to_string(kDefaultDLIdleTime), get_override_val}},
                {"enable_audio_reminders", {kConfigParamDisable, get_override_val}},
                {"requires_audio_reminders_on_ignition", {kConfigParamDisable, get_override_val}},
                {"audio_interval", {std::to_string(kDLDefaultAudioInterval), get_override_val}},
                {"audio_max_count", {std::to_string(kDLDefaultAudioDefCount), get_override_val}},
                {"audio_file", {"", get_override_val}},
                {"force_first_audio_play", {kConfigParamDisableAsNumeric, get_override_val}},
                {"first_audio_play_interval", {std::to_string(kDLDefaultAudioInterval), get_override_val}}
            }},
            {"driverlogin_v2", {
                {"enabled", {kConfigParamDisable, get_override_val}},
                {"login_speed", {std::to_string(kDefaultDLv2Speed), get_override_val}},
                {"login_time", {std::to_string(kDefaultDLv2Time), get_override_val}},
                {"idle_speed", {std::to_string(kDefaultDLv2IdleSpeed), get_override_val}},
                {"idle_time", {std::to_string(kDefaultDLv2IdleTime), get_override_val}},
                {"enable_audio_reminders", {kConfigParamDisable, get_override_val}},
                {"requires_audio_reminders_on_ignition", {kConfigParamDisable, get_override_val}},
                {"audio_interval", {std::to_string(kDLDefaultAudioInterval), get_override_val}},
                {"audio_max_count", {std::to_string(kDLDefaultAudioDefCount), get_override_val}},
                {"login_reminder_audio_file", {"", get_override_val}},
                {"force_first_audio_play", {kConfigParamDisableAsNumeric, get_override_val}},
                {"first_audio_play_interval", {std::to_string(kDLDefaultAudioInterval), get_override_val}},
                {"qr_enabled", {kConfigParamDisable, get_override_val}},
                {"max_qr_scan_duration", {std::to_string(kMaxQRScanDuration), get_override_val}},
                {"qr_login_tags", {"", get_override_val}},
                {"qr_login_success_audio_file", {"", get_override_val}},
                {"idle_speed_qr_resume_threshold", {std::to_string(kIdleSpeedQRResumeThreshold), get_override_val}},
                {"idle_speed_qr_resume_duration_secs", {std::to_string(kIdleSpeedQRResumeDuration), get_override_val}}
            }},
            {"driverlogin_fr", {
                {"enabled", {kConfigParamDisable, get_override_val}},
                {"idle_speed", {std::to_string(kDefaultDLFRIdleSpeed), get_override_val}},
                {"idle_time", {std::to_string(kDefaultDLFRIdleTime), get_override_val}}
            }},
            {"engine_idle", {
                {"enabled", {kConfigParamDisable, get_override_val}},
                {"engine_idle_enable_speed", {std::to_string(kDefaultEngineIdleEnableSpeed), get_override_val}},
                {"engine_idle_enable_time", {std::to_string(kDefaultEngineIdleEnableTime), get_override_val}}
            }},
            {"INSTALLER_APP", {
                {"ota_advertise_duration_s", {std::to_string(kOTAbroadcastDurationDefault), get_override_val}},
                {"bt_adv_tag", {kInstallerAppPattern, get_override_val}},
                {"installer_led_blink_timeout_sec", {std::to_string(kInstLEDBlinkTimeout), get_override_val}}
            }},
            {"ble_alert", {
                {"enabled", {kConfigParamDisableAsNumeric, get_override_val}},
                {"uuid_alert_tag", {kDefaultBleUuidAlertTag, get_override_val}},
                {"long_press_tag", {kDefaultBleLongPressTag, get_override_val}},
                {"low_battery_threshold_percentage", {std::to_string(kDefaultBleLowBatteryThreshold), get_override_val}},
                {"ble_vendor", {kBleMokoVendorString, get_override_val}},
                {"auto_pairing", {kConfigParamDisable, get_override_val}},
                {"keep_alive_miss_interval", {std::to_string(kBleKAHealthTimeoutMaxInterval), get_override_val}}
            }},
            {"camera", {
                {"back", {kConfigParamDisable, !get_override_val}}
            }},
#ifdef ENABLE_PRIVACY_BT
            {"privacy_mode", {
                {"enabled", {kConfigParamDisable, get_override_val}},
                {"privacy_disable_speed", {std::to_string(kDefaultPrivacyDisableSpeed), get_override_val}},
                {"privacy_disable_time", {std::to_string(kDefaultPrivacyDisableTime), get_override_val}},
                {"privacy_enable_speed", {std::to_string(kDefaultPrivacyEnableSpeed), get_override_val}},
                {"privacy_enable_time", {std::to_string(kDefaultPrivacyEnableTime), get_override_val}},
                {"default_privacy_v3", {kConfigParamEnable, get_override_val}}
            }},
#endif
            {"nd_bt", {
                {"reload_stack_interval_minutes", {std::to_string(kDefaultReloadStackInterval), get_override_val}},
                {"adsm_enabled", {kConfigParamDisableAsNumeric, get_override_val}},
                {"adsm_recovery_window_duration", {std::to_string(kRecoveryDuration), get_override_val}},
                {"adsm_downtime_duration", {std::to_string(kDownTimeDuration), get_override_val}},
#ifdef DO_FIRMWARE_FLASH
                {"toggle_fw_flash", {std::to_string(kConfigParamDisableAsNumeric), get_override_val}},
#endif
                {"relaxed_window", {std::to_string(kDefaultRelaxedScanWindow), get_override_val}},
                {"relaxed_interval", {std::to_string(kDefaultRelaxedScanInterval), get_override_val}},
                {"window", {std::to_string(kDefaultScanWindow), get_override_val}},
                {"interval", {std::to_string(kDefaultScanInterval), get_override_val}},
                {"relaxed_scan_max_timeout", {std::to_string(kRelaxedScanTimeout), get_override_val}},
                {"scan_time_btw_char_ops", {std::to_string(kDefaultScanTimeBtwCharOps), get_override_val}},
                {"rate_limiter_write_char_seconds", {std::to_string(kDefaultRateLimiterCharSeconds), get_override_val}},
                {"nearby_devices_max_size", {std::to_string(kDefaultNearbyDevicesMaxSize), get_override_val}},
                {"nearby_alert_beacon_max_size", {std::to_string(kDefaultNearbyAlertBeaconMaxSize), get_override_val}}
            }},
            {"vehicle_data", {
                {"enabled", {kConfigParamDisable, get_override_val}}
            }}
        }},
        {kCameraOverrideFile, {
            {"camera", {
                {"back", {kConfigParamDisable, !get_override_val}}
            }}
        }},
        {kDeviceConfigFile, {
            {"identity", {
                {"deviceId", {"", !get_override_val}}
            }}
        }},
        {kNDDeviceConfigFile, {
            {"version", {
                {"ndDevice", {"", !get_override_val}}
            }}
        }},
        {kNDConfigFile, {
            {"bleScanner", {
                {"hash", {"", !get_override_val}}
            }}
        }},
        {kNDCoreCommonConfigFile, {
            {"messenger_sockets", {
                {"qr_decodes", {kDefaultQrMessengerSocket, !get_override_val}}
            }},
            {"messenger_topics", {
                {"qr_decodes", {kDefaultQrMessengerTopic, !get_override_val}}
            }}
        }}
    });

    /*Dumps the configuration read*/
    configurator.Dump();

    do {
        config_data_ = std::unique_ptr<ConfigData>(new (std::nothrow) ConfigData());

        if (!config_data_) {
            LOG_E(kLogTag, "Failed to allocate memory for ConfigData");
            break;
        }

        LOG_I(kLogTag, "Parse status of %s:%d | %s:%d", kBagheeraConfigFile,
              configurator.IsSuccessfullyParsed(kBagheeraConfigFile),
              kCameraOverrideFile, configurator.IsSuccessfullyParsed(kCameraOverrideFile));

        LOG_I(kLogTag, "Parse status of %s:%d | %s:%d | %s:%d", kDeviceConfigFile,
              configurator.IsSuccessfullyParsed(kDeviceConfigFile),
              kNDDeviceConfigFile, configurator.IsSuccessfullyParsed(kNDDeviceConfigFile),
              kNDConfigFile, configurator.IsSuccessfullyParsed(kNDConfigFile));

        LOG_I(kLogTag, "Parse status of %s:%d", kNDCoreCommonConfigFile,
              configurator.IsSuccessfullyParsed(kNDCoreCommonConfigFile));

        if (!configurator.IsSuccessfullyParsed(kBagheeraConfigFile) ||
            !configurator.IsSuccessfullyParsed(kDeviceConfigFile) ||
            !configurator.IsSuccessfullyParsed(kNDDeviceConfigFile) ||
            !configurator.IsSuccessfullyParsed(kNDConfigFile)) {
            LOG_E(kLogTag, "Unable to parse one of the config files");
            break;
        }

        config_data_->device_id_ = configurator.Get(kDeviceConfigFile, "identity", "deviceId", "");

        if (config_data_->device_id_.empty()) {
            LOG_E(kLogTag, "device_id is empty");
            break;
        }

        LOG_I(kLogTag, "device_id: %s", config_data_->device_id_.c_str());

        config_data_->ota_version_ = configurator.Get(kNDDeviceConfigFile, "version", "ndDevice", "");

        if (config_data_->ota_version_.empty()) {
            LOG_E(kLogTag, "ota_version is empty");
            break;
        }

        LOG_I(kLogTag, "ota_version: %s", config_data_->ota_version_.c_str());

        if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "nd_bt", "reload_stack_interval_minutes",
                                              config_data_->reload_stack_interval_minutes_,
                                              kDefaultReloadStackInterval,
                                              kDefaultReloadStackInterval, kMaxReloadStackInterval)) {
            LOG_E(kLogTag, "ExtractAs failed for reload_stack_interval_minutes");
        }

        LOG_I(kLogTag, "reload_stack_interval_minutes: %llu", config_data_->reload_stack_interval_minutes_);

        if (kConfigParamEnableAsNumeric == configurator.Get(kBagheeraConfigFile, "nd_bt", "adsm_enabled",
                                                            kConfigParamDisableAsNumeric)) {
            config_data_->is_adsm_enabled_ = true;
        }

        LOG_I(kLogTag, "adsm_enabled: %d", config_data_->is_adsm_enabled_);

        if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "nd_bt", "adsm_recovery_window_duration",
                                              config_data_->adsm_recovery_window_duration_,
                                              kRecoveryDuration,
                                              kMinAdsmRecoveryDuration, kMaxAdsmRecoveryDuration)) {
            LOG_E(kLogTag, "ExtractAs failed for adsm_recovery_window_duration");
        }

        LOG_I(kLogTag, "adsm_recovery_window_duration: %llu seconds", config_data_->adsm_recovery_window_duration_);

        if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "nd_bt", "adsm_downtime_duration",
                                              config_data_->adsm_downtime_duration_,
                                              kDownTimeDuration,
                                              kMinAdsmDownTimeDuration, kMaxAdsmDownTimeDuration)) {
            LOG_E(kLogTag, "ExtractAs failed for adsm_downtime_duration");
        }

        LOG_I(kLogTag, "adsm_downtime_duration: %llu seconds", config_data_->adsm_downtime_duration_);

        if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "nd_bt", "relaxed_window",
            config_data_->relaxed_scan_window_,
            kDefaultRelaxedScanWindow,
            kMinScanWindow, kMaxScanWindow)) {
            LOG_E(kLogTag, "ExtractAs failed for relaxed_window");
        }

        LOG_I(kLogTag, "relaxed_window: %llu", config_data_->relaxed_scan_window_);

        if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "nd_bt", "relaxed_interval",
            config_data_->relaxed_scan_interval_,
            kDefaultRelaxedScanInterval,
            kMinScanInterval, kMaxScanInterval)) {
            LOG_E(kLogTag, "ExtractAs failed for relaxed_interval");
        }

        LOG_I(kLogTag, "relaxed_interval: %llu", config_data_->relaxed_scan_interval_);

        if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "nd_bt", "window",
            config_data_->scan_window_,
            kDefaultScanWindow,
            kMinScanWindow, kMaxScanWindow)) {
            LOG_E(kLogTag, "ExtractAs failed for scan_window");
        }

        LOG_I(kLogTag, "scan_window: %llu", config_data_->scan_window_);

        if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "nd_bt", "interval",
            config_data_->scan_interval_,
            kDefaultScanInterval,
            kMinScanInterval, kMaxScanInterval)) {
            LOG_E(kLogTag, "ExtractAs failed for scan_interval");
        }

        LOG_I(kLogTag, "scan_interval: %llu", config_data_->scan_interval_);

        if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "nd_bt", "relaxed_scan_max_timeout",
            config_data_->relaxed_scan_max_timeout_,
            kRelaxedScanTimeout,
            kMinRelaxedScanTimeout, kMaxRelaxedScanTimeout)) {
            LOG_E(kLogTag, "ExtractAs failed for relaxed_scan_max_timeout");
        }

        LOG_I(kLogTag, "relaxed_scan_max_timeout: %llu", config_data_->relaxed_scan_max_timeout_);

        if ((kConfigParamEnable == configurator.Get(kBagheeraConfigFile, "vehicle_data", "enabled",
                                                    kConfigParamDisable))) {
            config_data_->can_vd_enabled_ = true;
            LOG_I(kLogTag, "Vehicle data is enabled");
        }

        do {

            const bool is_enhanced_login_enabled = (kConfigParamEnable == configurator.Get(kBagheeraConfigFile,
                                                                                      "driverlogin_v2", "enabled",
                                                                                      kConfigParamDisable));

            LOG_I(kLogTag, "Enhanced Driver App login (2.0) BT is %s", is_enhanced_login_enabled ? "enabled" : "disabled");

            if (is_enhanced_login_enabled) {
                config_data_->driver_login_features_ |= DriverLoginFeature::kEnhancedBt;
            }

            const bool is_qr_enabled = (kConfigParamEnable == configurator.Get(kBagheeraConfigFile,
                                                                               "driverlogin_v2", "qr_enabled",
                                                                               kConfigParamDisable));

            LOG_I(kLogTag, "Enhanced Driver App login (2.0) QR is %s", is_qr_enabled ? "enabled" : "disabled");

            if (is_qr_enabled) {

                if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "driverlogin_v2", "max_qr_scan_duration",
                                                    config_data_->qr_max_scan_duration_,
                                                    static_cast<uint64_t>(kDefaultQRScanDuration),
                                                    static_cast<uint64_t>(kMinQRScanDuration),
                                                    static_cast<uint64_t>(kMaxQRScanDuration))) {
                    LOG_E(kLogTag, "ExtractAs failed for qr_max_scan_duration_");
                }

                LOG_I(kLogTag, "driver_login_qr_max_scan_duration: %llu", config_data_->qr_max_scan_duration_);

                if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "driverlogin_v2", "idle_speed_qr_resume_threshold",
                                                      config_data_->idle_speed_qr_resume_threshold_,
                                                      kIdleSpeedQRResumeThreshold,
                                                      kIdleSpeedQRResumeThreshold, std::numeric_limits<uint32_t>::max())) {
                    LOG_E(kLogTag, "ExtractAs failed for idle_speed_qr_resume_threshold");
                }

                LOG_I(kLogTag, "idle_speed_qr_resume_threshold: %llu", config_data_->idle_speed_qr_resume_threshold_);

                if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "driverlogin_v2", "idle_speed_qr_resume_duration_secs",
                                                      config_data_->idle_speed_qr_resume_duration_secs_,
                                                      kIdleSpeedQRResumeDuration,
                                                      kIdleSpeedQRResumeDuration, std::numeric_limits<uint32_t>::max())) {
                    LOG_E(kLogTag, "ExtractAs failed for idle_speed_qr_resume_duration_secs");
                }

                LOG_I(kLogTag, "idle_speed_qr_resume_duration_secs: %llu", config_data_->idle_speed_qr_resume_duration_secs_);

                config_data_->qr_login_success_audio_file_ = configurator.Get(kBagheeraConfigFile, "driverlogin_v2",
                                                                                "qr_login_success_audio_file", "");

                LOG_I(kLogTag, "Driver QR login success audio_file: %s",
                        config_data_->qr_login_success_audio_file_.c_str());

                const auto tags = configurator.Get(kBagheeraConfigFile, "driverlogin_v2", "qr_login_tags", "");

                LOG_I(kLogTag, "Driver QR login tags: %s", tags.c_str());

                config_data_->qr_login_tags_ = nd::utils::StringUtils::SplitString(tags, ',');

                for (const auto& tag : config_data_->qr_login_tags_) {
                    LOG_I(kLogTag, "Driver QR login tag: %s", tag.c_str());
                }

                if (!config_data_->qr_login_tags_.empty()) {
                    config_data_->driver_login_features_ |= DriverLoginFeature::kDriverQR;
                } else {
                    LOG_W(kLogTag, "No QR login tags configured, QR login will not work");
                }
            }

            if (is_qr_enabled || is_enhanced_login_enabled) {

                if (kConfigParamEnable == configurator.Get(kBagheeraConfigFile, "driverlogin_v2",
                                                           "enable_audio_reminders", kConfigParamDisable)) {

                    config_data_->enable_login_audio_reminder_ = true;
                    LOG_I(kLogTag, "Driver login Audio reminder is enabled");

                    if (kConfigParamEnable == configurator.Get(kBagheeraConfigFile, "driverlogin_v2",
                                                            "requires_audio_reminders_on_ignition",
                                                            kConfigParamDisable)) {
                        config_data_->requires_login_audio_on_ignition_on_ = true;
                        LOG_I(kLogTag, "Driver login requires reminder on ignition");
                    } else {
                        LOG_I(kLogTag, "Driver login requires_audio_reminders_on_ignition is disabled");
                    }

                    if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "driverlogin_v2", "audio_interval",
                                                        config_data_->driver_login_audio_interval_,
                                                        kDLDefaultAudioInterval,
                                                        kDLMinAudioInterval, kDLMaxAudioInterval)) {
                        LOG_E(kLogTag, "ExtractAs failed for driver_login_audio_interval");
                    }

                    LOG_I(kLogTag, "driver_login_audio_interval: %llu", config_data_->driver_login_audio_interval_);

                    if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "driverlogin_v2", "audio_max_count",
                                                        config_data_->driver_login_audio_play_count_,
                                                        kDLDefaultAudioDefCount,
                                                        kDLAudioMinCount, kDLAudioMaxCount)) {
                        LOG_E(kLogTag, "ExtractAs failed for driver_login_audio_play_count");
                    }

                    LOG_I(kLogTag, "driver_login_audio_play_count: %llu", config_data_->driver_login_audio_play_count_);

                    config_data_->login_audio_file_ = configurator.Get(kBagheeraConfigFile, "driverlogin_v2", "login_reminder_audio_file", "");

                    LOG_I(kLogTag, "Driver login audio_file: %s", config_data_->login_audio_file_.c_str());

                    if (kConfigParamEnableAsNumeric == configurator.Get(kBagheeraConfigFile, "driverlogin_v2",
                                                            "force_first_audio_play", kConfigParamDisableAsNumeric)) {
                        config_data_->driver_login_force_first_audio_play_ = true;
                        LOG_I(kLogTag, "Force first audio play enabled");
                    } else {
                        LOG_I(kLogTag, "Force first audio play disabled");
                    }

                    if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "driverlogin_v2", "first_audio_play_interval",
                                                        config_data_->driver_login_first_audio_interval_,
                                                        config_data_->driver_login_audio_interval_,
                                                        kDLMinAudioInterval, std::numeric_limits<uint32_t>::max())) {
                        LOG_E(kLogTag, "ExtractAs failed for first_audio_play_interval");
                    }

                    LOG_I(kLogTag, "driver_login_first_audio_interval: %llu", config_data_->driver_login_first_audio_interval_);
                } else {
                    LOG_I(kLogTag, "Driver login Audio reminder is disabled");
                }

                float login_speed = 0;
                if (!configurator.ExtractAs<float>(kBagheeraConfigFile, "driverlogin_v2", "login_speed",
                                                        login_speed,
                                                        kDefaultDLv2Speed,
                                                        kDefaultDLv2Speed, std::numeric_limits<int>::max())) {
                    LOG_E(kLogTag, "ExtractAs failed for driver_login_speed");
                }

                // convert to integer
                config_data_->driver_login_speed_ = static_cast<uint64_t> (login_speed);

                LOG_I(kLogTag, "driver_login_speed: %llu", config_data_->driver_login_speed_);

                if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "driverlogin_v2", "login_time",
                                                        config_data_->driver_login_time_,
                                                        kDefaultDLv2Time,
                                                        kDefaultDLTime, std::numeric_limits<int>::max())) {
                    LOG_E(kLogTag, "ExtractAs failed for driver_login_time");
                }

                LOG_I(kLogTag, "driver_login_time: %llu", config_data_->driver_login_time_);

                if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "driverlogin_v2", "idle_speed",
                                                        config_data_->driver_login_idle_speed_,
                                                        kDefaultDLv2IdleSpeed,
                                                        kDefaultDLv2IdleSpeed, std::numeric_limits<int>::max())) {
                    LOG_E(kLogTag, "ExtractAs failed for driver_login_idle_speed");
                }

                LOG_I(kLogTag, "driver_login_idle_speed: %llu", config_data_->driver_login_idle_speed_);

                if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "driverlogin_v2", "idle_time",
                                                        config_data_->driver_login_idle_time_,
                                                        kDefaultDLv2Time,
                                                        kDefaultDLv2Time, std::numeric_limits<int>::max())) {
                    LOG_E(kLogTag, "ExtractAs failed for driver_login_idle_time");
                }

                LOG_I(kLogTag, "driver_login_idle_time: %llu", config_data_->driver_login_idle_time_);

                break;
            }

            if (kConfigParamEnable == configurator.Get(kBagheeraConfigFile, "driverlogin", "enabled",
                                                       kConfigParamDisable)) {

                LOG_I(kLogTag, "Driver login is enabled");

                const bool is_dl_v2_config_overridden = configurator.IsConfigOverridden(kBagheeraConfigFile,
                                                                                        "driverlogin_v2", "enabled");

                // backward compatibility v2 start
                if ((!is_dl_v2_config_overridden) &&
                    (kConfigParamEnable == configurator.Get(kBagheeraConfigFile, "driverlogin", "driveri_app_login",
                                                            kConfigParamDisable))) {

                    LOG_I(kLogTag, "Enhanced Driver App login (2.0) BT is enabled:driveri_app_login");

                    config_data_->driver_login_features_ |= DriverLoginFeature::kEnhancedBt;

                    float login_speed = 0;
                    if (!configurator.ExtractAs<float>(kBagheeraConfigFile, "driverlogin", "login_speed",
                                                            login_speed,
                                                            static_cast<float>(kDefaultDLSpeed),
                                                            kDefaultDLv2Speed, std::numeric_limits<int>::max())) {
                        LOG_E(kLogTag, "ExtractAs failed for driver_login_speed");
                    }

                    // convert to integer
                    config_data_->driver_login_speed_ = static_cast<uint64_t> (login_speed);

                    LOG_I(kLogTag, "driver_login_speed: %llu", config_data_->driver_login_speed_);

                    if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "driverlogin", "login_time",
                                                            config_data_->driver_login_time_,
                                                            kDefaultDLTime,
                                                            kDefaultDLTime, std::numeric_limits<int>::max())) {
                        LOG_E(kLogTag, "ExtractAs failed for driver_login_time");
                    }

                    LOG_I(kLogTag, "driver_login_time: %llu", config_data_->driver_login_time_);

                    if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "driverlogin", "idle_speed",
                                                            config_data_->driver_login_idle_speed_,
                                                            kDefaultDLIdleSpeed,
                                                            kDefaultDLIdleSpeed, std::numeric_limits<int>::max())) {
                        LOG_E(kLogTag, "ExtractAs failed for driver_login_idle_speed");
                    }

                    LOG_I(kLogTag, "driver_login_idle_speed: %llu", config_data_->driver_login_idle_speed_);

                    if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "driverlogin", "idle_time",
                                                            config_data_->driver_login_idle_time_,
                                                            kDefaultDLIdleTime,
                                                            kMinDLIdleTime, std::numeric_limits<int>::max())) {
                        LOG_E(kLogTag, "ExtractAs failed for driver_login_idle_time");
                    }

                    LOG_I(kLogTag, "driver_login_idle_time: %llu", config_data_->driver_login_idle_time_);

#ifdef ENABLE_VEHICLE_QR_BASED_LOGIN
                    if (kConfigParamEnable == configurator.Get(kBagheeraConfigFile, "driverlogin", "driveri_app_qr_login",
                                                                kConfigParamDisable)) {
                        config_data_->driver_login_app_qr_enabled_ = true;
                        LOG_I(kLogTag, "Driver Login App QR login is enabled");
                        config_data_->driver_login_features_ |= DriverLoginFeature::kVehicleQR;

                        if (kConfigParamEnable == configurator.Get(kBagheeraConfigFile, "driverlogin",
                                                                "vehicle_idle_driver_login", kConfigParamDisable)) {
                            config_data_->driver_app_login_qr_vehicle_idle_enabled_ = true;
                            LOG_I(kLogTag, "Driver App login in vehicle idle is enabled");
                        }
                    }
#endif

                    if (kConfigParamEnable == configurator.Get(kBagheeraConfigFile, "driverlogin", "enable_audio_reminders",
                                                            kConfigParamDisable)) {
                        config_data_->enable_login_audio_reminder_ = true;
                        LOG_I(kLogTag, "Driver login Audio reminder is enabled");
                    } else {
                        LOG_I(kLogTag, "Driver login Audio reminder is disabled");
                    }

                    if (kConfigParamEnable == configurator.Get(kBagheeraConfigFile, "driverlogin",
                                                            "requires_audio_reminders_on_ignition",
                                                            kConfigParamDisable)) {
                        config_data_->requires_login_audio_on_ignition_on_ = true;
                        LOG_I(kLogTag, "Driver login requires reminder on ignition");
                    } else {
                        LOG_I(kLogTag, "Driver login requires_audio_reminders_on_ignition is disabled");
                    }

                    if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "driverlogin", "audio_interval",
                                                            config_data_->driver_login_audio_interval_,
                                                            kDLDefaultAudioInterval,
                                                            kDLMinAudioInterval, kDLMaxAudioInterval)) {
                        LOG_E(kLogTag, "ExtractAs failed for driver_login_audio_interval");
                    }

                    LOG_I(kLogTag, "driver_login_audio_interval: %llu", config_data_->driver_login_audio_interval_);

                    if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "driverlogin", "audio_max_count",
                                                            config_data_->driver_login_audio_play_count_,
                                                            kDLDefaultAudioDefCount,
                                                            kDLAudioMinCount, kDLAudioMaxCount)) {
                        LOG_E(kLogTag, "ExtractAs failed for driver_login_audio_play_count");
                    }

                    LOG_I(kLogTag, "driver_login_audio_play_count: %llu", config_data_->driver_login_audio_play_count_);

                    config_data_->login_audio_file_ = configurator.Get(kBagheeraConfigFile, "driverlogin", "audio_file", "");

                    LOG_I(kLogTag, "Driver login audio_file: %s", config_data_->login_audio_file_.c_str());

                    if (kConfigParamEnableAsNumeric == configurator.Get(kBagheeraConfigFile, "driverlogin",
                                                            "force_first_audio_play", kConfigParamDisableAsNumeric)) {
                        config_data_->driver_login_force_first_audio_play_ = true;
                        LOG_I(kLogTag, "Force first audio play enabled");
                    } else {
                        LOG_I(kLogTag, "Force first audio play disabled");
                    }

                    if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "driverlogin", "first_audio_play_interval",
                                                        config_data_->driver_login_first_audio_interval_,
                                                        config_data_->driver_login_audio_interval_,
                                                        kDLMinAudioInterval, std::numeric_limits<uint32_t>::max())) {
                        LOG_E(kLogTag, "ExtractAs failed for first_audio_play_interval");
                    }

                    LOG_I(kLogTag, "driver_login_first_audio_interval: %llu", config_data_->driver_login_first_audio_interval_);

                    // backward compatibility v2 end

                    break;

                } else {

                    config_data_->driver_login_features_ |= DriverLoginFeature::kLegacy;

                    LOG_I(kLogTag, "Enhanced Driver App login (2.0) BT is disabled, fall back");

                    // This means legacy
                    config_data_->driver_login_speed_ = kDefaultDLSpeed;
                    config_data_->driver_login_time_ = kDefaultDLTime;
                    config_data_->driver_login_idle_time_ = kDefaultDLIdleTime;
                    config_data_->driver_login_idle_speed_ = kDefaultDLIdleSpeed;

                    LOG_I(kLogTag, "Will use legacy default params for BT");
                    LOG_I(kLogTag, "driver_login_speed: %llu", config_data_->driver_login_speed_);
                    LOG_I(kLogTag, "driver_login_time: %llu", config_data_->driver_login_time_);
                    LOG_I(kLogTag, "driver_login_idle_time: %llu", config_data_->driver_login_idle_time_);
                    LOG_I(kLogTag, "driver_login_idle_speed: %llu", config_data_->driver_login_idle_speed_);

                    if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "driverlogin", "bt_max_retry",
                                                        config_data_->driver_login_max_retry_,
                                                        kDefaultDLMaxRetry,
                                                        0, std::numeric_limits<uint32_t>::max())) {
                        LOG_E(kLogTag, "ExtractAs failed for driver_login_max_retry");
                    }

                    LOG_I(kLogTag, "driver_login_max_retry: %llu", config_data_->driver_login_max_retry_);

                    if (kConfigParamEnable == configurator.Get(kBagheeraConfigFile, "driverlogin",
                                                                "coexist_with_visionpro",
                                                                kConfigParamDisable)) {
                        config_data_->driver_login_coexist_with_visionpro_ = true;
                        LOG_I(kLogTag, "Driver login is set to coexist with visionpro");
                    } else {
                        LOG_I(kLogTag, "Driver login requires_audio_reminders_on_ignition is disabled");
                    }

                    {
                        bool ext_cam_feature_enabled = false;
                        read_ext_camera_common_config(ext_cam_feature_enabled);

                        if (ext_cam_feature_enabled) {
                            if(!config_data_->driver_login_coexist_with_visionpro_) {
                                config_data_->driver_login_features_ = DriverLoginFeature::kNone;
                                LOG_I(kLogTag, "ext_cam_feature_enabled:disabling driver login as it is set not to coexist with visionpro");
                            } else {
                                // scan for less time if visionpro is enabled
                                config_data_->driver_login_max_retry_= kDefaultDLMaxRetryWithVP;
                                LOG_I(kLogTag, "coexist ext_cam_feature_enabled:driver_login_max_retry: %llu",
                                    config_data_->driver_login_max_retry_);
                            }

                            if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "driverlogin", "bt_scan_retry_vp_enabled",
                                                                config_data_->driver_login_max_retry_,
                                                                kDefaultDLMaxRetryWithVP,
                                                                0, std::numeric_limits<uint32_t>::max())) {
                                LOG_E(kLogTag, "ExtractAs failed for driver_login_max_retry");
                            }

                            LOG_I(kLogTag, "ext_cam_feature_enabled:driver_login_max_retry: %llu",
                                config_data_->driver_login_max_retry_);
                        }
                    }
                }
            }

        } while (false);

        if (HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kEnhancedBt) ||
            HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kDriverQR)) {

            //NOTE: Do not change the below order.

            if (0 == config_data_->driver_login_audio_play_count_) {
                LOG_I(kLogTag, "Driver Login audio play count is 0 setting it to unlimited");
                config_data_->driver_login_audio_play_count_ = std::numeric_limits<uint64_t>::max();
            }

            if (config_data_->login_audio_file_.empty()) {
                LOG_E(kLogTag, "Driver login audio_file is empty");
                config_data_->driver_login_audio_play_count_ = 0;
                config_data_->enable_login_audio_reminder_ = false;
            }

            if (HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kEnhancedBt) &&
                (0 == config_data_->driver_login_speed_) &&
                (config_data_->enable_login_audio_reminder_)) {
                LOG_I(kLogTag, "Driver Login speed is 0 under enhanced legacy, forcing audio reminder on ignition to true");
                config_data_->requires_login_audio_on_ignition_on_ = true;
            }
        }

        if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "nd_bt", "scan_time_btw_char_ops",
                                                config_data_->scan_time_btw_char_ops_,
                                                kDefaultScanTimeBtwCharOps,
                                                0, kMaxScanTimeBtwCharOps)) {
            LOG_E(kLogTag, "ExtractAs failed for scan_time_btw_char_ops");
        }

        LOG_I(kLogTag, "scan_time_btw_char_ops: %llu", config_data_->scan_time_btw_char_ops_);

        if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "nd_bt", "rate_limiter_write_char_seconds",
                                                config_data_->rate_limiter_write_char_seconds_,
                                                kDefaultRateLimiterCharSeconds,
                                                kDefaultRateLimiterCharSeconds, std::numeric_limits<uint64_t>::max())) {
            LOG_E(kLogTag, "ExtractAs failed for rate_limiter_write_char_seconds");
        }

        LOG_I(kLogTag, "rate_limiter_write_char_seconds: %llu", config_data_->rate_limiter_write_char_seconds_);

        if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "nd_bt", "nearby_devices_max_size",
                                                config_data_->nearby_devices_max_size_,
                                                kDefaultNearbyDevicesMaxSize,
                                                kNearbyDevicesMinSize, kNearbyDevicesMaxSize)) {
            LOG_E(kLogTag, "ExtractAs failed for nearby_devices_max_size");
        }

        LOG_I(kLogTag, "nearby_devices_max_size: %llu", config_data_->nearby_devices_max_size_);

        if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "nd_bt", "nearby_alert_beacon_max_size",
                                                config_data_->nearby_alert_beacon_max_size_,
                                                kDefaultNearbyAlertBeaconMaxSize,
                                                kNearbyAlertBeaconMinSize, kNearbyAlertBeaconMaxSize)) {
            LOG_E(kLogTag, "ExtractAs failed for nearby_alert_beacon_max_size");
        }

        LOG_I(kLogTag, "nearby_alert_beacon_max_size: %llu", config_data_->nearby_alert_beacon_max_size_);

#ifdef DO_FIRMWARE_FLASH
        if (kConfigParamEnableAsNumeric == configurator.Get(kBagheeraConfigFile, "nd_bt", "toggle_fw_flash",
                                                   kConfigParamDisableAsNumeric)) {
            config_data_->toggle_firmware_flash_ = true;
            LOG_I(kLogTag, "Need to toggle bt firmware");
        }
#endif

        if (kConfigParamEnable == configurator.Get(kBagheeraConfigFile, "driverlogin_fr",
                                                    "enabled",
                                                    kConfigParamDisable)) {
            config_data_->driver_login_fr_enabled_ = true;
            LOG_I(kLogTag, "Driver login FR is enabled");

            if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "driverlogin_fr", "idle_speed",
                                                  config_data_->driver_login_fr_idle_speed_,
                                                  kDefaultDLFRIdleSpeed,
                                                  0, std::numeric_limits<int>::max())) {
                LOG_E(kLogTag, "ExtractAs failed for driver_login_fr_idle_speed");
            }

            LOG_I(kLogTag, "driver_login_fr_idle_speed: %llu", config_data_->driver_login_fr_idle_speed_);

            if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "driverlogin_fr", "idle_time",
                                                  config_data_->driver_login_fr_idle_time_,
                                                  kDefaultDLFRIdleTime,
                                                  0, std::numeric_limits<int>::max())) {
                LOG_E(kLogTag, "ExtractAs failed for driver_login_fr_idle_time");
            }

            LOG_I(kLogTag, "driver_login_fr_idle_time: %llu", config_data_->driver_login_fr_idle_time_);

        } else {
            LOG_I(kLogTag, "Driver login FR is disabled");
        }

#ifdef ENABLE_PRIVACY_BT
        if (kConfigParamEnable == configurator.Get(kBagheeraConfigFile, "privacy_mode", "enabled", kConfigParamDisable)) {
            config_data_->privacy_mode_enabled_ = true;
            LOG_I(kLogTag, "Privacy mode is enabled");

            if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "privacy_mode", "privacy_disable_speed",
                                                  config_data_->privacy_disable_speed_,
                                                  kDefaultPrivacyDisableSpeed,
                                                  0, std::numeric_limits<int>::max())) {
                LOG_E(kLogTag, "ExtractAs failed for privacy_disable_speed");
            }

            LOG_I(kLogTag, "privacy_disable_speed: %llu", config_data_->privacy_disable_speed_);

            if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "privacy_mode", "privacy_disable_time",
                                                  config_data_->privacy_disable_time_,
                                                  kDefaultPrivacyDisableTime,
                                                  0, std::numeric_limits<int>::max())) {
                LOG_E(kLogTag, "ExtractAs failed for privacy_disable_time");
            }

            LOG_I(kLogTag, "privacy_disable_time: %llu", config_data_->privacy_disable_time_);

            if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "privacy_mode", "privacy_enable_speed",
                                                  config_data_->privacy_enable_speed_,
                                                  kDefaultPrivacyEnableSpeed,
                                                  0, std::numeric_limits<int>::max())) {
                LOG_E(kLogTag, "ExtractAs failed for privacy_enable_speed");
            }

            LOG_I(kLogTag, "privacy_enable_speed: %llu", config_data_->privacy_enable_speed_);

            if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "privacy_mode", "privacy_enable_time",
                                                  config_data_->privacy_enable_time_,
                                                  kDefaultPrivacyEnableTime,
                                                  0, std::numeric_limits<int>::max())) {
                LOG_E(kLogTag, "ExtractAs failed for privacy_enable_time");
            }

            LOG_I(kLogTag, "privacy_enable_time: %llu", config_data_->privacy_enable_time_);

        } else {
            LOG_I(kLogTag, "Privacy mode is disabled");
        }

        // In legacy code "default_privacy" is checked for irrespective of privacy_mode is enabled or not.
        if (kConfigParamDisable == configurator.Get(kBagheeraConfigFile, "privacy_mode", "default_privacy_v3",
                                                    kConfigParamEnable)) {
            config_data_->default_privacy_status_ = false;
            LOG_I(kLogTag, "default_privacy is false");
        } else {
            config_data_->default_privacy_status_ = true;
            LOG_I(kLogTag, "default_privacy is true");
        }
#endif

        if (kConfigParamEnable == configurator.Get(kBagheeraConfigFile, "engine_idle", "enabled", kConfigParamDisable)) {
            config_data_->engine_idle_enabled_ = true;
            LOG_I(kLogTag, "engine idle is enabled");

            if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "engine_idle", "engine_idle_enable_speed",
                                                  config_data_->engine_idle_enable_speed_,
                                                  kDefaultEngineIdleEnableSpeed,
                                                  0, std::numeric_limits<int>::max())) {
                LOG_E(kLogTag, "ExtractAs failed for engine_idle_enable_speed");
            }

            LOG_I(kLogTag, "engine_idle_enable_speed: %llu", config_data_->engine_idle_enable_speed_);

            if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "engine_idle", "engine_idle_enable_time",
                                                  config_data_->engine_idle_enable_time_,
                                                  kDefaultEngineIdleEnableTime,
                                                  0, std::numeric_limits<int>::max())) {
                LOG_E(kLogTag, "ExtractAs failed for engine_idle_enable_time");
            }

            LOG_I(kLogTag, "engine_idle_enable_time: %llu", config_data_->engine_idle_enable_time_);
        } else {
            LOG_I(kLogTag, "engine idle is disabled");
        }

        if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "INSTALLER_APP", "ota_advertise_duration_s",
                                              config_data_->ota_adv_duration_,
                                              kOTAbroadcastDurationDefault,
                                              0, std::numeric_limits<int>::max())) {
            LOG_E(kLogTag, "ExtractAs failed for ota_advertise_duration");
        }

        LOG_I(kLogTag, "ota_advertise_duration: %llu", config_data_->ota_adv_duration_);

        // const std::string installer_adv_tags_str = configurator.Get(kBagheeraConfigFile, "INSTALLER_APP", "bt_adv_tag", kInstallerAppPattern);

        if (!configurator.ExtractAs<std::uint16_t>(kBagheeraConfigFile, "INSTALLER_APP", "installer_led_blink_timeout_sec",
                                                  config_data_->installer_led_blink_timeout_sec_,
                                                  kInstLEDBlinkTimeout,
                                                  0, std::numeric_limits<uint16_t>::max())) {
            LOG_E(kLogTag, "ExtractAs failed for installer_led_blink_timeout_sec");
        }

        const std::string installer_adv_tags_str = "NTDI:,ND:";

        LOG_I(kLogTag, "installer_adv_tags_str: %s", installer_adv_tags_str.c_str());

        config_data_->installer_adv_tags_ = SplitString(installer_adv_tags_str, ',');

        if (config_data_->installer_adv_tags_.empty()) {
            LOG_E(kLogTag, "No valid installer adv tags found, using default");
            config_data_->installer_adv_tags_.push_back(kInstallerAppPattern);
        } else {
            for (const auto& tag : config_data_->installer_adv_tags_) {
                LOG_I(kLogTag, "installer_adv_tag: %s", tag.c_str());
            }
        }

        if (kConfigParamEnableAsNumeric == configurator.Get(kBagheeraConfigFile, "ble_alert", "enabled",
                                                            kConfigParamDisableAsNumeric)) {
            config_data_->beacon_alert_enabled_ = true;
            LOG_I(kLogTag, "ble_alert is enabled");

            // auto_pairing extraction
            if (kConfigParamEnableAsNumeric == configurator.Get(kBagheeraConfigFile, "ble_alert", "auto_pairing",
                                                            kConfigParamDisableAsNumeric)) {
                config_data_->beacon_auto_pairing_enabled_ = true;
                LOG_I(kLogTag, "ble_alert auto_pairing is enabled");
            } else {
                LOG_I(kLogTag, "ble_alert auto_pairing is disabled");
            }

            const auto uuid_alert_tag = configurator.Get(kBagheeraConfigFile, "ble_alert", "uuid_alert_tag", kDefaultBleUuidAlertTag);

            config_data_->uuid_alert_tag_ = Translator::HexStringToBytes(uuid_alert_tag);

            LOG_I(kLogTag, "ble alert_tag: %s", uuid_alert_tag.c_str());

            const auto uuid_long_press_tag = configurator.Get(kBagheeraConfigFile, "ble_alert", "long_press_tag", kDefaultBleLongPressTag);

            config_data_->long_press_tag_ = Translator::HexStringToBytes(uuid_long_press_tag);

            LOG_I(kLogTag, "ble long_press_tag: %s", uuid_long_press_tag.c_str());

            if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "ble_alert", "low_battery_threshold_percentage",
                                                  config_data_->ble_low_battery_threshold_,
                                                  kDefaultBleLowBatteryThreshold,
                                                  kMinBleLowBatteryThreshold, kMaxBleLowBatteryThreshold)) {
                LOG_E(kLogTag, "ExtractAs failed for low_battery_threshold_percentage");
            }

            LOG_I(kLogTag, "low_battery_threshold: %llu", config_data_->ble_low_battery_threshold_);

            config_data_->ble_vendor_name_ = configurator.Get(kBagheeraConfigFile, "ble_alert", "ble_vendor", kBleMokoVendorString);

            if ((kBleMokoVendorString != config_data_->ble_vendor_name_) && (kBleMinewVendorString != config_data_->ble_vendor_name_)) {
                LOG_E(kLogTag, "Invalid Ble vendor name: %s, using default : %s", config_data_->ble_vendor_name_.c_str(),
                      kBleMokoVendorString);
                config_data_->ble_vendor_name_ = kBleMokoVendorString;
            }

            LOG_I(kLogTag, "ble_vendor_name: %s", config_data_->ble_vendor_name_.c_str());

            if (!configurator.ExtractAs<uint64_t>(kBagheeraConfigFile, "ble_alert", "keep_alive_miss_interval",
                                                  config_data_->keep_alive_miss_interval_,
                                                  kBleKAHealthTimeoutMaxInterval,
                                                  0, std::numeric_limits<uint64_t>::max())) {
                LOG_E(kLogTag, "ExtractAs failed for keep_alive_miss_interval");
            }

            LOG_I(kLogTag, "keep_alive_miss_interval: %llu", config_data_->keep_alive_miss_interval_);

        } else {
                LOG_I(kLogTag, "BLE based alerts are disabled");
        }

        if (configurator.IsSuccessfullyParsed(kCameraOverrideFile)) {
            if (kConfigParamEnableStr == configurator.Get(kCameraOverrideFile, "camera", "back", kConfigParamDisableStr)) {
                config_data_->driver_cam_enabled_ = true;
                LOG_I(kLogTag, "Back camera is enabled. FV can be done");
            }
        } else {
            LOG_E(kLogTag, "Unable to parse %s", kCameraOverrideFile);
            if (kConfigParamEnableStr == configurator.Get(kBagheeraConfigFile, "camera", "back", kConfigParamDisable)) {
                config_data_->driver_cam_enabled_ = true;
                LOG_I(kLogTag, "bag_conf: Back camera is enabled. FV can be done");
            }
        }

        config_data_->ble_hash_ = configurator.Get(kNDConfigFile, "bleScanner", "hash", kDefaultBleScannerHash);

        LOG_I(kLogTag, "ble_hash: %s", config_data_->ble_hash_.c_str());

        config_data_->qr_messenger_topic_ = configurator.Get(kNDCoreCommonConfigFile,
                                                             "messenger_topics", "qr_decodes",
                                                             kDefaultQrMessengerTopic);

        config_data_->qr_messenger_socket_ = configurator.Get(kNDCoreCommonConfigFile,
                                                              "messenger_sockets", "qr_decodes",
                                                              kDefaultQrMessengerSocket);

        LOG_I(kLogTag, "qr_messenger_topic: %s, qr_messenger_socket: %s",
              config_data_->qr_messenger_topic_.c_str(), config_data_->qr_messenger_socket_.c_str());

        status = true;

        LOG_I(kLogTag, "ReadConfigData success");

    } while (false);

    return status;
}

bool BTManager::ReloadVehicleData() {
    bool status = false;

    nd::utils::Configurator configurator;
    const bool get_override_val = true;
                    /*file name*/   /*section*/ /*key*/ /*default*/
    configurator.Run({{kDeviceConfigFile, {{"vehicle", {{"vin", {"", !get_override_val}},
                                                        {"vhash", {"", !get_override_val}}
                                                        }}}}});

    /*Dumps the configuration read*/
    configurator.Dump();

    if (configurator.IsSuccessfullyParsed(kDeviceConfigFile)) {
        config_data_->device_vin_ = configurator.Get(kDeviceConfigFile, "vehicle", "vin", "");
        config_data_->vehicle_hash_ = configurator.Get(kDeviceConfigFile, "vehicle", "vhash", "");

        if (config_data_->device_vin_.empty() && config_data_->vehicle_hash_.empty()) {
            LOG_E(kLogTag, "device_vin and vehicle_hash are empty");
        } else {
            status = true;
        }

    } else {
        LOG_E(kLogTag, "Unable to parse %s", kDeviceConfigFile);
    }

    return status;
}

bool BTManager::InitMQ() {
    bool status = false;
    const bool flush_queue = true;
    server_mq_ = nd_msgq_t::get_msgq(kBT_Server_MQ_Name, nd_msgq_t::ND_MSGQ_SERVER, flush_queue);
    if (nullptr != server_mq_) {
        status = true;
        LOG_I(kLogTag, "BT_Server_MQ_Name created successfully");
    } else {
        LOG_E(kLogTag, "BT_Server_MQ_Name creation failed");
    }
    return status;
}

void BTManager::MsgLoop() {
    LOG_I(kLogTag, "Entered %s", __func__);

    nd_msgq_t::nd_msg_t *msg = nullptr;

    while (!is_exit_main_loop_set_) {
        if ((msg = server_mq_->receive()) == nullptr) {
            LOG_E(kLogTag, "Receive message failed" );
            break;
        }

        generic_msg_t *g_msg = reinterpret_cast<generic_msg_t *>(msg->get_buffer());
        if (nullptr == g_msg) {
            LOG_E(kLogTag, "msg->get_buffer() returned NULL");
            delete msg;
            continue;
        }

        current_state_ptr_->OnMessage(msg);
        delete msg;
    }

    LOG_I(kLogTag, "Exit %s", __func__);
}

bool BTManager::InitStates() {
    bool status = false;
    try {
        // init_state__ptr_   = std::make_shared<InitState>();
        // error_state_ptr_ = std::make_shared<ErrorState>();
        idle_state_ptr_   = std::make_shared<IdleState>(this);
        motion_state_ptr_ = std::make_shared<MotionState>(this);
        status = true;
    } catch (const std::bad_alloc& e) {
        LOG_E(kLogTag, "Allocation failed in %s what(): %s", __func__, e.what());
    } catch (const std::exception& e){
        LOG_E(kLogTag, "Allocation failed in %s what(): %s", __func__, e.what());
    } catch (...) {
        LOG_E(kLogTag, "Caught an exception of an undetermined type in %s", __func__);
    }
    return status;
}

// NOTE: Enable only if needed
// bool BTManager::HoldForDependentServices() {
//     std::list<std::string> service_list = {kWiFi_Server_MQ_Name, kPower_Server_MQ_Name,
//                                            kNDCentral_Server_MQ_Name, kSpeed_Server_MQ_Name};

//     while (true) {
//         int32_t max_wait_counter = 100;
//         for (auto service_itr = service_list.begin(); service_list.end() != service_itr;) {
//             if (is_msg_q_created(*service_itr)) {
//                 LOG_I(kLogTag, "%s client MQ created", (*service_itr).c_str());
//                 service_itr = service_list.erase(service_itr);
//             } else {
//                 ++service_itr;
//             }
//         }
//         if (service_list.empty()) {
//             LOG_I(kLogTag, "All dependent services are up!");
//             break;
//         } else {
//             if (0 >= max_wait_counter--) {
//                 LOG_E(kLogTag, "Max wait for dependent services reached, breaking now!");
//                 break;
//             } else {
//                 std::this_thread::sleep_for(std::chrono::milliseconds(100));
//             }
//         }
//     }

//     return true;
// }

bool BTManager::Run() {
    bool status = false;

    do {
        // if (!HoldForDependentServices()) {
        //     LOG_E(kLogTag, "Dependent services failed to start");
        //     if (!service_obj_ptr_->send_err_msg(SM_E_BTFV_INIT_FAIL, static_cast<int>(InitErrCode::kDependencyFail),
        //                                         "Dependent services fail")) {
        //         LOG_E(kLogTag, "SM_E_BTFV_INIT_FAIL send_err_msg failed");
        //     }
        //     break;
        // }

        if (is_uninterrupted_scan_required_) {
            RegisterBtStackReload();
        } else {
            LOG_I(kLogTag, "Uninterrupted scan is not required, stack will not be reloaded");
        }

        nd::platform_specific::SendGetIgnitionStatus(kBT_Server_MQ_Name, kPower_Server_MQ_Name, msg_id_);

        if (!RegisterForSpeedServices()) {
            LOG_E(kLogTag, "Speed service registration failed");
            RegisterSpeedEventRegRetry();
        }

        // Start with Idle state
        MoveToState(States::kIdle);

        MsgLoop();

        status = true;
    } while (false);

    return status;
}

void BTManager::CyclicTimerTickCB(uint64_t steady_now, uint64_t system_now) {
    // LOG_I(kLogTag, "%s called %llu %llu", __func__, steady_now, system_now);

    {
        const std::lock_guard<std::mutex> lock(scan_events_timeout_->data_lock_);
        // auto const now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();

        auto &timeout_map = scan_events_timeout_->data_map_;

        for (auto itr = timeout_map.begin(); itr != timeout_map.end();) {

            // LOG_I(kLogTag, "CyclicTimerTickCB: %d %llu %llu", static_cast<int32_t>(itr->first), itr->second, steady_now);

            if (steady_now >= itr->second) {

                msg_type_t type = INVALID_MSG;
                std::string type_str;

                switch (itr->first) {
                    case TimeoutEvents::kInstallerApp: {
                        LOG_I(kLogTag, "InstallerApp timeout cb triggered");
                        type = INSTALLER_APP_DETECTION_TIMEOUT;
                        type_str = "INSTALLER_APP_DETECTION_TIMEOUT";
                        break;
                    }

                    case TimeoutEvents::kDriverLoginLegacy: {
                        LOG_I(kLogTag, "DriverLogin legacy scan complete cb triggered");
                        type = DRIVER_LOGIN_SCAN_COMPLETE;
                        type_str = "DRIVER_LOGIN_SCAN_COMPLETE";
                        break;
                    }

                    case TimeoutEvents::kHotspotActive: {
                        LOG_I(kLogTag, "Hotspot timeout cb triggered"); // This should not happen most likely
                        break;
                    }

#ifdef ENABLE_VEHICLE_QR_BASED_LOGIN
                    case TimeoutEvents::kVehicleQr: {
                        LOG_I(kLogTag, "DriverLogin QR scan complete cb triggered");
                        type = DRIVER_LOGIN_APP_QR_SCAN_COMPLETE;
                        type_str = "DRIVER_LOGIN_APP_QR_SCAN_COMPLETE";
                        break;
                    }
#endif

                    case TimeoutEvents::kVBUS: {
                        LOG_I(kLogTag, "VBUS Detection Timeout");
                        type = VBUS_BT_DETECTION_TIMEOUT;
                        type_str = "VBUS_BT_DETECTION_TIMEOUT";
                        break;
                    }

                    case TimeoutEvents::kRelaxedScan: {
                        LOG_I(kLogTag, "Relaxed Scan Timeout");
                        type = RESET_ANTENNA_TIME;
                        type_str = "RESET_ANTENNA_TIME";
                        break;
                    }

                    default: {
                        LOG_E(kLogTag, "Invalid event in timeout: %d", static_cast<int32_t>(itr->first));
                        break;
                    }
                }

                if (INVALID_MSG != type) {
                    nd::platform_specific::SendGenericMessage(type, type_str.c_str(), kBT_Server_MQ_Name, kBT_Server_MQ_Name, msg_id_);
                }

                itr = timeout_map.erase(itr);
            } else {
                ++itr;
            }
        }
    }

    do {
        const std::lock_guard<std::mutex> lock(retry_event_queue_ptr_->data_lock_);
        auto &timeout_map = retry_event_queue_ptr_->data_map_;

        if (timeout_map.empty()) {
            break;
        }

        if (steady_now < timeout_map.cbegin()->first) {
            break;
        }

        static uint64_t last_notified = 0;

        if (timeout_map.cbegin()->first == last_notified) {
            break;
        }

        last_notified = timeout_map.cbegin()->first;

        LOG_I(kLogTag, "Retry internal event timeout msg sent");
        nd::platform_specific::SendGenericMessage(BTFV_INTERNAL_RETRY_EVENT, "BTFV_INTERNAL_RETRY_EVENT",
                                                  kBT_Server_MQ_Name, kBT_Server_MQ_Name, msg_id_);

    } while (false);
}

bool BTManager::HandleGenericRetryEvents() {
    bool handled = false;

    const uint64_t now = CurrentSteadyClockSeconds();

    do {

        InternalRetryEvents event = InternalRetryEvents::kInvalid;
        void *data = nullptr;

        {
            const std::lock_guard<std::mutex> lock(retry_event_queue_ptr_->data_lock_);
            auto &timeout_map = retry_event_queue_ptr_->data_map_;

            if (timeout_map.empty()) {
                LOG_I(kLogTag, "No events to retry");
                break;
            } else {
                LOG_I(kLogTag, "Events to retry: %zu", timeout_map.size());
            }

            auto event_itr = timeout_map.begin();

            if (now < event_itr->first) {
                break;
            }

            if (is_exit_main_loop_set_) {
                break;
            }

            event = event_itr->second.type_;
            data = event_itr->second.data_;
            timeout_map.erase(event_itr);
        }

        switch (event) {

            case InternalRetryEvents::kRetryHealthMsgs: {
                if (!RetryHealthData()) {
                    RegisterHealthDataRetry();
                }
                break;
            }

            case InternalRetryEvents::kInactiveBleBeaconCheck: {
                CheckAndReportInactiveBeacons();
                RegisterInactiveBeaconCheck();
                break;
            }

            case InternalRetryEvents::kBleBeaconKACheck: {
                RegisterPeriodicKACheck();
                break;
            }

            case InternalRetryEvents::kSpeedServiceRegistration: {
                if (!RegisterForSpeedServices()) {
                    LOG_E(kLogTag, "Retry: Speed service registration failed");
                    RegisterSpeedEventRegRetry();
                }
                break;
            }

            case InternalRetryEvents::kBtStackReload: {
                ReloadBtStack();
                RegisterBtStackReload();
                break;
            }

            case InternalRetryEvents::kBleDevicePairTimeout: {
                ClearBlePairFilter();
                break;
            }

            case InternalRetryEvents::kNearbyDevicesReport: {
                ReportPeriodicNearbyDevices();
                break;
            }

            case InternalRetryEvents::kNearbyAlertBeaconReport: {
                ReportPeriodicNearbyAlertBeaconDevices();
                break;
            }

            case InternalRetryEvents::kStartQrMsg: {
                if (!TrySendStartQr()) {
                    RegisterStartQrMsgRetry();
                }
                break;
            }

            case InternalRetryEvents::kRecoveryWindowTimer: {
                LOG_I(kLogTag, "Recovery window timer expired");
                recovery_window_duration_expired_ = true;
                if (!restart_from_same_boot_) {
                    nd::platform_specific::SendGetIgnitionStatus(kBT_Server_MQ_Name, kPower_Server_MQ_Name, msg_id_);
                }
                break;
            }

            case InternalRetryEvents::kAdsmWatchdogTimer: {
                HandleAdsmWatchdogTimeout();
                is_adsm_watchdog_registered_ = false;
                RegisterForAdsmWatchdogTimer();
                break;
            }

            default: {
                LOG_E(kLogTag, "Invalid event in retry: %d", static_cast<int32_t>(event));
                break;
            }
        }

    } while (true);

    return handled;
}

bool BTManager::HandleRetryEventsInIdleState() {
    bool handled = false;

    handled = HandleGenericRetryEvents();

    return handled;
}

bool BTManager::HandleRetryEventsInMotionState() {
    bool handled = false;

    handled = HandleGenericRetryEvents();

    return handled;
}

bool BTManager::ReadBleDeviceList() {
    bool status = false;
    using nd::device::Accessory;
    using nd::device::AccessoryDB;

    constexpr char kBleAccessory[] = "BLE_BUTTON";
    std::vector<Accessory> accessories;

    const auto get_status = AccessoryDB::GetAllOfType(kBleAccessory, accessories);

    do {

        if (0 != get_status.first) {
            LOG_E(kLogTag, "ReadBleDeviceList: GetAllOfType failed with error: %d:%s", get_status.first, get_status.second.c_str());
            break;
        }

        if (accessories.empty()) {
            LOG_I(kLogTag, "No BLE devices found");
            break;
        }

        const std::lock_guard<std::mutex> lock(ble_devices_data_ptr_->data_lock_);

        for (const auto& accessory : accessories) {

            json_error_t error{};
            json_t *root = json_loads(accessory.data_.c_str(), 0, &error);

            do {

                if (nullptr == root) {
                    LOG_E(kLogTag, "ReadBleDeviceList: json_loads failed for acc: %s with error: %s", accessory.data_.c_str(), error.text);
                    break;
                }

                constexpr char kAccessoryIdKey[]       = "accessory_id";
                constexpr char kMacIdKey[]             = "mac";
                constexpr char kLabelKey[]             = "label";

                const char *accessory_id = nullptr;
                const char *mac_id = nullptr;
                const char *label = nullptr;

                error = {};
                if (0 != json_unpack_ex(root, &error, 0, "{s:s, s:s, s:s}", kAccessoryIdKey, &accessory_id,
                                        kMacIdKey, &mac_id, kLabelKey, &label)) {
                    LOG_E(kLogTag, "ReadBleDeviceList: json_unpack_ex failed with error: %s for acc: %s",
                            error.text, accessory.data_.c_str());
                    break;
                }

                const auto itr = kBleLabelAssigneeMap.find(label);
                if (kBleLabelAssigneeMap.end() == itr) {
                    LOG_E(kLogTag, "ReadBleDeviceList: Invalid label: %s for id: %s data: %s", label,
                        accessory.id_type_.first.c_str(), accessory.data_.c_str());
                    break;
                }

                const auto emplace_status = ble_devices_data_ptr_->data_map_[itr->second].emplace(mac_id,
                                                                                                BleDeviceAssociatedData{});
                if (emplace_status.second) {
                    LOG_I(kLogTag, "Added BLE with mac: %s label: %s", mac_id, label);
                } else {
                    LOG_I(kLogTag, "BLE with mac: %s already exists", mac_id);
                }

            } while (false);

            if (nullptr != root) {
                json_decref(root);
            }
        }

        for (const auto &assignee_data : ble_devices_data_ptr_->data_map_) {
            for (const auto &associated_data : assignee_data.second) {
                ReportBleDeviceConfigToHealthStats(associated_data.first, kBleDeviceAssigneesConstantsMap.at(assignee_data.first).label_);
            }
        }

        status = true;

    } while (false);

    return status;
}

bool BTManager::Init() {
    LOG_I(kLogTag, "Entered %s", __func__);

    bool status = false;

    do {
        // Get factory instance
        const auto bt_factory_ptr = BluetoothFactory::GetInstance();

        if (!bt_factory_ptr) {
            LOG_E(kLogTag, "bt_factory_ptr is null");
            break;
        }

        std::function<void(int signum)> sig_usr_callback = [this](int signum) {
            LOG_I (kLogTag, "received user signal: %d", signum);

            if ((SIGUSR1 == signum) || (SIGUSR2 == signum)) {
                this->is_exit_main_loop_set_ = true;
                // this->CleanUp();
            }
        };

        service_obj_ptr_ = bt_factory_ptr->GetServiceObj(kBtServiceTag, std::move(sig_usr_callback));
        if (nullptr == service_obj_ptr_) {
            LOG_E(kLogTag, "service_obj_ptr_ is null");
            break;
        }

        device_factory_ptr_ = bt_factory_ptr->GetDeviceFactoryObj();
        if (nullptr == device_factory_ptr_) {
            LOG_E(kLogTag, "device_factory_ptr_ is null");
            break;
        }

        const NDDeviceTypeT dev_type = device_factory_ptr_->getNDDeviceType();
        LOG_I (kLogTag," DeviceType: %s", NDDeviceTypeT::toString(dev_type).c_str());

        if (!InitMQ()) {
            LOG_E(kLogTag, "InitMQ failed");
            if (!service_obj_ptr_->send_err_msg(SM_E_BTFV_INIT_FAIL, static_cast<int>(InitErrCode::kMQServerFail),
                                                "MQ server fail")) {
                LOG_E(kLogTag, "SM_E_BTFV_INIT_FAIL send_err_msg failed");
            }
            break;
        }

        if (!ReadConfigData()) {
            LOG_E(kLogTag, "ReadConfigData failed");
            if (!service_obj_ptr_->send_err_msg(SM_E_BTFV_INIT_FAIL, static_cast<int>(InitErrCode::kConfigFail),
                                                "Config Read failed")) {
                LOG_E(kLogTag, "SM_E_BTFV_INIT_FAIL send_err_msg failed");
            }
            break;
        }

        if (!InitStates()) {
            LOG_E(kLogTag, "InitStates failed");
            if (!service_obj_ptr_->send_err_msg(SM_E_BTFV_INIT_FAIL, static_cast<int>(InitErrCode::kStateInitsFail),
                                                "State Inits fail")) {
                LOG_E(kLogTag, "SM_E_BTFV_INIT_FAIL send_err_msg failed");
            }
            break;
        }

        try {
            installer_app_attr_ = std::make_unique<InstallerAppAttributes>();

#ifdef ENABLE_VEHICLE_QR_BASED_LOGIN
            driver_login_app_qr_attr_ = std::make_unique<DriverLoginAppQRAttributes>();
#endif

            driver_association_ = std::make_unique<DriverAssociation>();
            vehicle_state_attr_ = std::make_unique<VehicleStateAttributes>();
            speed_services_ = std::make_unique<SpeedServices>();
            scanned_legacy_logins_ = std::make_unique<ScannedLogins>();
            driver_qr_logins_ = std::make_unique<DriverQrLogins>();
            ble_observer_ptr_ = std::make_unique<nd::helpers::BleEventObserver>();
            cyclic_timer_tick_ = std::make_unique<nd::utils::TimerTick>();
            scan_events_timeout_ = std::make_unique<ScanEventsTimeout>();

            BtPersistenceDBHelper::ApiVersion api_version = BtPersistenceDBHelper::ApiVersion::kLegacy;

            if (HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kEnhancedBt) ||
                HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kDriverQR)) {
                api_version = BtPersistenceDBHelper::ApiVersion::kV2;
            }

            persistence_ptr_ = std::make_unique<BtPersistenceDBHelper>(config_data_->device_id_, api_version);
            new_ble_devices_ptr_ = std::make_unique<NewBleDevicesNotifyData>();
            ble_devices_data_ptr_ = std::make_shared<BleDevicesData>();
            retry_event_queue_ptr_ = std::make_unique<RetryEventQueue>();

            // Initialize ADSM state manager and load persisted state only if enabled
            const std::string state_file_path = std::string(kNDHomePath) + kAdsmStateDbName;

            if (config_data_->is_adsm_enabled_) {
                adsm_state_mgr_ = std::make_unique<BtAdsmStateManager>(state_file_path);
                LOG_I(kLogTag, "ADSM state manager initialized");
            } else {
                LOG_I(kLogTag, "ADSM state manager disabled by configuration");
            }

        } catch (const std::exception& e){
            LOG_E(kLogTag, "[%s:%d] Allocation failed what(): %s", __FUNCTION__, __LINE__, e.what());
        } catch (...) {
            LOG_E(kLogTag, "[%s:%d] Caught an exception of an undetermined type", __FUNCTION__, __LINE__);
        }

        if (!vehicle_state_attr_ || !speed_services_ || !scanned_legacy_logins_ || !ble_observer_ptr_ ||
            !cyclic_timer_tick_ || !scan_events_timeout_ ||
            (config_data_->is_adsm_enabled_ && (!adsm_state_mgr_))) {
            LOG_E(kLogTag, "member pointers are null");
            if (!service_obj_ptr_->send_err_msg(SM_E_BTFV_INIT_FAIL, static_cast<int>(InitErrCode::kGenericFail),
                                                "Memory unavailable")) {
                LOG_E(kLogTag, "SM_E_BTFV_INIT_FAIL send_err_msg failed");
            }
            break;
        }

        try {
            nearby_devices_data_ptr_ = std::make_shared<NearbyDevicesData>();
            nearby_alert_beacon_data_ptr_ = std::make_shared<NearbyAlertBeaconData>();
        } catch (const std::exception& e){
            LOG_E(kLogTag, "[%s:%d] Allocation failed what(): %s", __FUNCTION__, __LINE__, e.what());
        } catch (...) {
            LOG_E(kLogTag, "[%s:%d] Caught an exception of an undetermined type", __FUNCTION__, __LINE__);
        }

        if (!nearby_devices_data_ptr_ || !nearby_alert_beacon_data_ptr_) {
            LOG_E(kLogTag, "nearby devices/beacon data pointers are null");
            if (!service_obj_ptr_->send_err_msg(SM_E_BTFV_INIT_FAIL, static_cast<int>(InitErrCode::kGenericFail),
                                                "Memory unavailable")) {
                LOG_E(kLogTag, "SM_E_BTFV_INIT_FAIL send_err_msg failed");
            }
            break;
        }

#ifdef ENABLE_PRIVACY_BT
        // This has to be initialized with default privacy value as nd_central uses privacy status
        // from RES_DRV_LOGIN_QUERY also, apart from PRIVACY_MODE_UPDATE.
        // Refer comment : https://github.com/netradyne/nd_device_services/blame/2a8a987651fd936adb1da150d46b580b69bd3939/nd-central/common/central/nd_central.cpp#L6817
        vehicle_state_attr_->is_privacy_mode_active_ = config_data_->default_privacy_status_;
#endif
        // Do not change the below code order
        if ((config_data_->enable_login_audio_reminder_) &&
            (0 < config_data_->driver_login_idle_time_) &&
            (0 == config_data_->driver_login_speed_)) {
            driver_association_->play_audio_on_out_of_idle_ = true;
            LOG_I(kLogTag, "Login audio will play on out of idle as login_speed is 0");
        }

        if (0 == config_data_->driver_login_idle_time_) {
            LOG_I(kLogTag, "Driver will not be logged out on idle");
            // minimum idle time is set to 5 seconds in this scenario.
            // This will be used to retrigger scan : https://netradyne.atlassian.net/browse/PM-2894
            constexpr uint64_t kMinimumIdleTime = 5;
            config_data_->driver_login_idle_time_ = kMinimumIdleTime;
        } else {
            LOG_I(kLogTag, "Driver will be logged out on idle");
            driver_association_->disassociate_on_idle_ = true;
        }

        auto timer_cb = [&](uint64_t steady_now, uint64_t system_now) {
            this->CyclicTimerTickCB(steady_now, system_now);
        };

        cyclic_timer_tick_->RegisterCB(timer_cb);

        // cyclic timer tick interval is 1 second
        cyclic_timer_tick_->SetInterval(kTimerTickInterval);
        cyclic_timer_tick_->Start();

        // Create platform specific bt interface
        bt_interface_ptr_ = bt_factory_ptr->CreateBtInterface();

        if (!bt_interface_ptr_) {
            LOG_E(kLogTag, "bt_interface_ptr_ is null");
            break;
        }

        if (!bt_interface_ptr_->SupportsScanAndConnectionParallelly()) {
            bt_interface_backup_ptr_ = bt_factory_ptr->CreateBackUpBtInterface();
        } else {
            bt_interface_backup_ptr_ = bt_interface_ptr_;
        }

        if (!bt_interface_backup_ptr_) {
            LOG_E(kLogTag, "bt_interface_backup_ptr_ is null");
            break;
        }

        nd::interface::LeEventCb callback = [&](const std::string &mac_addr, const uint8_t *const adv_data, size_t length, int rssi) {
            ble_observer_ptr_->LeScanCallback(mac_addr, adv_data, length, rssi);
        };

        bt_interface_ptr_->RegisterLeEventCallback(callback);
        bt_interface_backup_ptr_->RegisterLeEventCallback(callback);

        if (config_data_->beacon_alert_enabled_) {
            ReadBleDeviceList();
            RegisterAlertFilter();
            RegisterBatteryStatusFilter();
            // RegisterPeriodicKACheck(); // NOTE: This was required in krait to reload stack retrospectively.
            RegisterInactiveBeaconCheck();
            RegisterNearbyDevicesFilter();
        }

        current_state_ptr_ = idle_state_ptr_;

        if (config_data_->beacon_alert_enabled_) {

            is_uninterrupted_scan_required_ = true;

            // BT Module enable
            bool bt_module_enabled = bt_interface_ptr_->IsEnabled();
            for (uint32_t counter = 0;
                 (counter < kMaxBtEnableRetryCount) && (!bt_module_enabled);
                 ++counter) {
                if (bt_interface_ptr_->Enable()) {
                    LOG_I(kLogTag, "BT Enabled successfully");
                    bt_module_enabled = true;
                    break;
                } else {
                    // MoveToState(States::kError); // Move to error sate for retry
                    LOG_E(kLogTag, "BT Enable failed, will retry again");
                    if (bt_interface_ptr_->Disable()) {
                        LOG_I(kLogTag, "BT Disabled successfully");
                    } else {
                        LOG_E(kLogTag, "BT Disable failed");
                    }
                }
            }

            if (!bt_module_enabled) {
                LOG_E(kLogTag, "Max retries failed to enable bluetooth");
                if (!service_obj_ptr_->send_err_msg(SM_E_BTFV_INIT_FAIL, static_cast<int>(InitErrCode::kBtModuleFail),
                                                    "BT module failed")) {
                    LOG_E(kLogTag, "SM_E_BTFV_INIT_FAIL send_err_msg failed");
                }
                break;
            }

            // BT Scan enable
            if (!bt_interface_ptr_->LeScanOn()) {
                LOG_E(kLogTag, "LeScanOn failed");
                if (!service_obj_ptr_->send_err_msg(SM_E_BTFV_INIT_FAIL, static_cast<int>(InitErrCode::kBtScanFail),
                                                    "BT scan failed")) {
                    LOG_E(kLogTag, "SM_E_BTFV_INIT_FAIL send_err_msg failed");
                }
                break;
            }

            LOG_I(kLogTag, "LeScanOn success");
        }

        // Start processing thread
        if (!persistence_ptr_->StartProcessing()) {
            LOG_E(kLogTag, "Persistency processing failed");
            if (!service_obj_ptr_->send_err_msg(SM_E_BTFV_INIT_FAIL, static_cast<int>(InitErrCode::kPersistencyFail),
                                                "DB Write error")) {
                LOG_E(kLogTag, "SM_E_BTFV_INIT_FAIL send_err_msg failed");
            }
            break;
        }

        const auto current_boot_id = nd::utils::SystemInfo::GetBootId();
        LOG_I(kLogTag, "System Boot ID: >>>> %s <<<<", current_boot_id.c_str());

        const bool has_bt_login = (HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kEnhancedBt));
        const bool has_qr_login = (HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kDriverQR));
        const std::string state_file_path = std::string(kNDHomePath) + kAdsmStateDbName;

        if (config_data_->is_adsm_enabled_ && (has_bt_login || has_qr_login)) {
            // Helper lambda to reset state manager
            auto reset_state_manager = [&]() {
                adsm_state_mgr_.reset(nullptr);
                FileSystemHandler::DeleteFile(state_file_path);
                adsm_state_mgr_ = std::make_unique<BtAdsmStateManager>(state_file_path);
            };

            // Check if state file exists at primary location
            if (FileSystemHandler::FileExists(state_file_path)) {
                LOG_I(kLogTag, "ADSM state file found at: %s", state_file_path.c_str());

                const int64_t current_time = CurrentSystemClockSeconds();
                bool should_reset = false;

                if (adsm_state_mgr_ && adsm_state_mgr_->LoadState()) {
                    const auto last_updated_time = adsm_state_mgr_->GetLastUpdated();

                    if (last_updated_time > 0 && last_updated_time <= current_time) {
                        const int64_t boot_time_diff_s = current_time - last_updated_time;

                        LOG_I(kLogTag, "Last updated time: %lld, Time diff: %lld s (%.2f min)",
                            last_updated_time, boot_time_diff_s, boot_time_diff_s / 60.0);

                        if (boot_time_diff_s > static_cast<int64_t>(config_data_->adsm_downtime_duration_)) {
                            LOG_W(kLogTag, "Time difference exceeds %llu seconds, not considering recovery",
                                  config_data_->adsm_downtime_duration_);
                            should_reset = true;
                        }
                    } else {
                        LOG_W(kLogTag, "Invalid last updated time in ADSM state manager");
                        should_reset = true;
                    }
                } else {
                    LOG_W(kLogTag, "Failed to load ADSM state manager for last updated time check");
                    should_reset = true;
                }

                if (should_reset) {
                    reset_state_manager();
                }
            } else {
                LOG_I(kLogTag, "ADSM state file not found at primary location: %s", state_file_path.c_str());
            }
        } else {
            LOG_I(kLogTag, "ADSM feature is disabled or not required");
            FileSystemHandler::DeleteFile(state_file_path);
        }

        if ((adsm_state_mgr_) && (adsm_state_mgr_->LoadState())) {
            LOG_I(kLogTag, "ADSM state loaded successfully");

            const auto adsm_state = adsm_state_mgr_->GetState();

            LOG_I(kLogTag, "ADSM boot id : >>>> %s <<<<", adsm_state.boot_id_.c_str());
            if (current_boot_id == adsm_state.boot_id_) {
                LOG_W(kLogTag, "ADSM state boot ID matches current boot ID");
                restart_from_same_boot_ = true;
            } else {
                LOG_W(kLogTag, "ADSM state boot ID does not match current boot ID");
            }

            LOG_I(kLogTag, "ADSM State - Session: hash=%s, drivers=%zu, audio_count=%llu",
                  adsm_state.current_hash_.c_str(), adsm_state.current_drivers_.size(),
                  adsm_state.audio_count_);

            LOG_I(kLogTag, "ADSM State - Vehicle: ign_status=%d, wake_up_status=%d",
                  adsm_state.ign_status_, adsm_state.wake_up_status_);

            if (!adsm_state.current_hash_.empty()) {
                LOG_W(kLogTag, "Detected incomplete session from crash, hash: %s, drivers: %zu",
                      adsm_state.current_hash_.c_str(), adsm_state.current_drivers_.size());

                driver_association_->cached_scanned_drivers_ = adsm_state.current_drivers_;
                driver_session_start_time_ = CurrentSystemClockMilliSeconds();
                driver_association_->session_data_.audio_play_count_ = 0;
                driver_association_->session_data_.hash_ = adsm_state.current_hash_;

                if (HasDriverLoggedIn()) {
                    driver_association_->notify_on_disassociation_ = true;
                }

                NotifyDriverLoginsToObservers();

                {
                    // Create a new entry in session table
                    BtPersistenceDBHelper::DriverSessionData data{};
                    data.start_time_ = driver_session_start_time_;
                    data.id_map_ = driver_association_->cached_scanned_drivers_;
                    data.do_upload_ = false;
                    data.audio_count_ = 0;

                    const std::string current_hash = GetDLScanHash();
                    persistence_ptr_->BeginDriverSession(std::move(data), current_hash);
                }

                if (restart_from_same_boot_) {
                    // We are recovering from the same boot

                    ignition_status_t current_ign_status = IGNITION_ERR;
                    bool is_wakeup = false;

                    if (nd::platform_specific::GetCurrentIgnitionStatus(current_ign_status, is_wakeup)) {

                        LOG_I(kLogTag, "Current Ignition status: %d, Wakeup status: %d",
                            static_cast<int32_t>(current_ign_status), is_wakeup);

                        vehicle_state_attr_->ign_status_ = current_ign_status;
                        vehicle_state_attr_->wake_up_status_ = is_wakeup;
                    } else {
                        LOG_E(kLogTag, "Failed to get current ignition status during recovery using adsm states");
                        vehicle_state_attr_->ign_status_ = static_cast<ignition_status_t>(adsm_state.ign_status_);
                        vehicle_state_attr_->wake_up_status_ = adsm_state.wake_up_status_;
                    }
                }

                RegisterForRecoveryTimeout();

                if ((has_bt_login || has_qr_login) && (IGNITION_ON == vehicle_state_attr_->ign_status_)) {
                    RegisterForAdsmWatchdogTimer();
                    const AudioInitiator audio_initiator = has_qr_login ? AudioInitiator::kQr : AudioInitiator::kBt;
                    const DLScanInitiator scan_initiator = DLScanInitiator::kOnIgnition;
                    LOG_I(kLogTag, "%s: ADSM - Ignition ON, hence starting audio play thread", __func__);
                    BeginAudioPlayThread(audio_initiator, kDLScanInitiatorStrMap.at(scan_initiator));
                }

            } else {
                LOG_I(kLogTag, "No active session to restore");
            }
        } else {
            LOG_I(kLogTag, "No valid ADSM state found, using defaults");
        }

        if (adsm_state_mgr_) {
            adsm_state_mgr_->SetBootId(current_boot_id);
            // NOTE: Do not call savestate() here yet.
        }

        // Initialize the cache with unassigned driver if not restored
        if (driver_association_->cached_scanned_drivers_.empty()) {
            driver_association_->cached_scanned_drivers_.emplace(kUnassignedDriverAppLoginString, kDefaultAppLoginTime);
        }

        status = true;
        LOG_I(kLogTag, "Init success");

        // TODO(sunils): Test code remove later
        // MoveToState(States::kMotion);
        // driver_session_start_time_ = CurrentSystemClockMilliSeconds() - 1000000;

    } while (false);

    return status;
}

bool BTManager::HasDriverLoggedIn() const {

    const bool is_driver_map_empty = driver_association_->cached_scanned_drivers_.empty();
    const bool is_unassigned_driver = ((1 == driver_association_->cached_scanned_drivers_.count(kUnassignedDriverAppLoginString)) &&
                                       (1 == driver_association_->cached_scanned_drivers_.size()));

    return (!is_driver_map_empty && !is_unassigned_driver);
}

bool BTManager::RegisterForSpeedEvent(SpeedServiceEvents event) {

    bool status = false;

    switch (event) {
        case SpeedServiceEvents::kSpeedLogin: {
            status = nd::platform_specific::SendSpeedRegistrationRequest(config_data_->driver_login_speed_,
                                                                        config_data_->driver_login_time_,
                                                                        SPEED_REG_DRV_LOGIN,
                                                                        kBT_Server_MQ_Name, kSpeed_Server_MQ_Name, msg_id_);
            break;
        }

        case SpeedServiceEvents::kSpeedPrivacy: {
            status = nd::platform_specific::SendSpeedRegistrationRequest(config_data_->privacy_disable_speed_,
                                                                        config_data_->privacy_disable_time_,
                                                                        SPEED_REG_PRIVACY,
                                                                        kBT_Server_MQ_Name, kSpeed_Server_MQ_Name, msg_id_);
            break;
        }

        case SpeedServiceEvents::kSpeedQrLogin: {
            status = nd::platform_specific::SendSpeedRegistrationRequest(config_data_->idle_speed_qr_resume_threshold_,
                                                                        config_data_->idle_speed_qr_resume_duration_secs_,
                                                                        SPEED_REG_QR,
                                                                        kBT_Server_MQ_Name, kSpeed_Server_MQ_Name, msg_id_);
            break;
        }

        default: {
            LOG_E(kLogTag, "Unknown speed event: %d", static_cast<int32_t>(event));
            break;
        }
    }

    return status;
}

bool BTManager::RegisterForIdleEvent(SpeedServiceEvents event) {
    bool status = false;

    switch (event) {
        case SpeedServiceEvents::kIdleEngine: {
            status = nd::platform_specific::SendIdleRegistrationRequest(config_data_->engine_idle_enable_speed_,
                                                                        config_data_->engine_idle_enable_time_,
                                                                        IDLE_REG_ENGINE,
                                                                        kBT_Server_MQ_Name, kSpeed_Server_MQ_Name, msg_id_);
            break;
        }

        case SpeedServiceEvents::kIdlePrivacy: {
            status = nd::platform_specific::SendIdleRegistrationRequest(config_data_->privacy_enable_speed_,
                                                                        config_data_->privacy_enable_time_,
                                                                        IDLE_REG_PRIVACY,
                                                                        kBT_Server_MQ_Name, kSpeed_Server_MQ_Name, msg_id_);
            break;
        }

        case SpeedServiceEvents::kIdleLogin: {
            status = nd::platform_specific::SendIdleRegistrationRequest(config_data_->driver_login_idle_speed_,
                                                                        config_data_->driver_login_idle_time_,
                                                                        IDLE_REG_DL,
                                                                        kBT_Server_MQ_Name, kSpeed_Server_MQ_Name, msg_id_);
            break;
        }

        case SpeedServiceEvents::kIdleLoginFr: {
            status = nd::platform_specific::SendIdleRegistrationRequest(config_data_->driver_login_fr_idle_speed_,
                                                                        config_data_->driver_login_fr_idle_time_,
                                                                        IDLE_REG_DL_FR,
                                                                        kBT_Server_MQ_Name, kSpeed_Server_MQ_Name, msg_id_);
            break;
        }

        case SpeedServiceEvents::kIdleQrLogin: {
            status = nd::platform_specific::SendIdleRegistrationRequest(config_data_->idle_speed_qr_resume_threshold_,
                                                                        config_data_->idle_speed_qr_resume_duration_secs_,
                                                                        IDLE_REG_QR,
                                                                        kBT_Server_MQ_Name, kSpeed_Server_MQ_Name, msg_id_);
            break;
        }

        default: {
            LOG_E(kLogTag, "Unknown idle event: %d", static_cast<int32_t>(event));
            break;
        }
    }

    return status;
}

bool BTManager::RegisterForSpeedServices() {
    bool status = false;

    do {
        if ((config_data_->engine_idle_enabled_) &&
            (!RegisterForIdleEvent(SpeedServiceEvents::kIdleEngine))) {
            break;
        }

        if ((config_data_->driver_cam_enabled_) &&
            (config_data_->privacy_mode_enabled_) &&
            (!RegisterForIdleEvent(SpeedServiceEvents::kIdlePrivacy))) {
            break;
        }

        if ((DriverLoginFeature::kNone != config_data_->driver_login_features_) &&
            (0 < config_data_->driver_login_idle_time_) &&
            (!RegisterForIdleEvent(SpeedServiceEvents::kIdleLogin))) {
            break;
        }

        if ((config_data_->driver_login_fr_enabled_) &&
            (!RegisterForIdleEvent(SpeedServiceEvents::kIdleLoginFr))) {
            break;
        }

        if ((DriverLoginFeature::kNone != config_data_->driver_login_features_) &&
            (0 < config_data_->driver_login_speed_) &&
            (!RegisterForSpeedEvent(SpeedServiceEvents::kSpeedLogin))) {
            break;
        }

        if (HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kDriverQR)) {
            if ((!RegisterForIdleEvent(SpeedServiceEvents::kIdleQrLogin)) ||
                (!RegisterForSpeedEvent(SpeedServiceEvents::kSpeedQrLogin))) {
                break;
            }
        }

        if ((config_data_->driver_cam_enabled_) &&
            (config_data_->privacy_mode_enabled_) &&
            (!RegisterForSpeedEvent(SpeedServiceEvents::kSpeedPrivacy))) {
            break;
        }

        LOG_I(kLogTag, "All speed events registrations sent successfully");
        status = true;
    } while (false);

    if (!status) {
        LOG_E(kLogTag, "Failed to register for speed events, to be retried later");
    }

    return status;
}

bool BTManager::UnregisterIdleEventHandle(int32_t handle) {
    return nd::platform_specific::SendUnregisterIdleEventHandle(handle, kBT_Server_MQ_Name, kSpeed_Server_MQ_Name, msg_id_);
}

bool BTManager::UnregisterSpeedEventHandle(int32_t handle) {
    return nd::platform_specific::SendUnregisterSpeedEventHandle(handle, kBT_Server_MQ_Name, kSpeed_Server_MQ_Name, msg_id_);
}

bool BTManager::UnregisterForSpeedServices() {

    bool status = true;

    if (-1 < speed_services_->idle_engine_handle_) {
        if (!UnregisterIdleEventHandle(speed_services_->idle_engine_handle_)) {
            LOG_E(kLogTag, "Failed to unregister idle_engine_handle");
            status = false;
        }
        speed_services_->idle_engine_handle_ = -1;
    }

    if (-1 < speed_services_->idle_privacy_handle_) {
        if (!UnregisterIdleEventHandle(speed_services_->idle_privacy_handle_)) {
            LOG_E(kLogTag, "Failed to unregister idle_privacy_handle");
            status = false;
        }
        speed_services_->idle_privacy_handle_ = -1;
    }

    if (-1 < speed_services_->idle_qr_handle_) {
        if (!UnregisterIdleEventHandle(speed_services_->idle_qr_handle_)) {
            LOG_E(kLogTag, "Failed to unregister idle_qr_handle");
            status = false;
        }
        speed_services_->idle_qr_handle_ = -1;
    }

    if (-1 < speed_services_->idle_login_handle_) {
        if (!UnregisterIdleEventHandle(speed_services_->idle_login_handle_)) {
            LOG_E(kLogTag, "Failed to unregister idle_login_handle");
            status = false;
        }
        speed_services_->idle_login_handle_ = -1;
    }

    if (-1 < speed_services_->idle_login_fr_handle_) {
        if (!UnregisterIdleEventHandle(speed_services_->idle_login_fr_handle_)) {
            LOG_E(kLogTag, "Failed to unregister idle_login_fr_handle");
            status = false;
        }
        speed_services_->idle_login_fr_handle_ = -1;
    }

    if (-1 < speed_services_->speed_login_handle_) {
        if (!UnregisterSpeedEventHandle(speed_services_->speed_login_handle_)) {
            LOG_E(kLogTag, "Failed to unregister speed_login_handle");
            status = false;
        }
        speed_services_->speed_login_handle_ = -1;
    }

    if (-1 < speed_services_->speed_privacy_handle_) {
        if (!UnregisterSpeedEventHandle(speed_services_->speed_privacy_handle_)) {
            LOG_E(kLogTag, "Failed to unregister speed_privacy_handle");
            status = false;
        }
        speed_services_->speed_privacy_handle_ = -1;
    }

    if (-1 < speed_services_->speed_qr_handle_) {
        if (!UnregisterSpeedEventHandle(speed_services_->speed_qr_handle_)) {
            LOG_E(kLogTag, "Failed to unregister speed_qr_handle");
            status = false;
        }
        speed_services_->speed_qr_handle_ = -1;
    }

    return status;
}

void BTManager::HandleDriverLoginQuery(void *msg) {
    const auto client_name = nd::platform_specific::FetchMsgClientName(msg);

    std::vector<std::string> ids;
    std::vector<std::string> timestamps;
    ids.reserve(NUM_MAX_DRIVERS);
    timestamps.reserve(NUM_MAX_DRIVERS);

    for(const auto & driver: driver_association_->cached_scanned_drivers_) {
        ids.emplace_back(driver.first);
        timestamps.emplace_back(std::to_string(driver.second));
        if (NUM_MAX_DRIVERS == ids.size()) {
            break;
        }
    }

    nd::platform_specific::SendDriverLoginQueryResponse(std::move(ids), std::move(timestamps),
                                                        vehicle_state_attr_->engine_idle_on_,
                                                        vehicle_state_attr_->is_privacy_mode_active_,
                                                        kBT_Server_MQ_Name, client_name.c_str(), msg_id_);
}

void BTManager::HandleNewBleAlertDevice(void *msg) {
    // TODO(sunil.s): The below code should use accessory db once the auto pair is implemented

    /*
    std::unordered_map<BleDeviceAssignees, std::vector<std::string>> new_data_map;

    // {
    //     const std::lock_guard<std::mutex> lock(new_ble_devices_ptr_->data_lock_);
    //     new_data_map = std::move(new_ble_devices_ptr_->data_map_);
    // }

    // size_t total_size = 0;

    // for (const auto & data: new_data_map) {
    //     total_size += data.second.size();
    // }

    // if (0 < total_size) {
    //     std::vector<BlePersistenceDBHelper::BleBeaconData> ble_data;
    //     ble_data.resize(total_size);

    //     for (const auto & data: new_data_map) {
    //         const std::string label = (kBleDeviceAssigneesConstantsMap.at(data.first)).label_;
    //         for (const auto & mac_addr: data.second) {
    //             ble_data.emplace_back(BlePersistenceDBHelper::BleBeaconData{mac_addr, label});
    //         }
    //     }

        if (!BlePersistenceDBHelper::StoreBleAlertDevices(std::move(ble_data))) {
            LOG_E(kLogTag, "Failed to store BLE alert devices");
        }
    }
    */
}

#ifdef ENABLE_VEHICLE_QR_BASED_LOGIN
void BTManager::HandleDriverLoginQrScanTimeout() {
    ClearDriverLoginAppQRFilter();

    if (!file_is_present(kDriverAppLoginIndicatorFile)) {
        current_scanned_drivers_.emplace(kUnassignedDriverAppLoginString, 0);
        NotifyDriverLoginsToObservers();

        if (driver_association_->notify_on_disassociation_) {
            // Store unassigned in db
            driver_association_->notify_on_disassociation_ = false;

            BtPersistenceDBHelper::DriverSessionData data{};
            data.start_time_ = driver_session_start_time_;
            data.id_map_ = driver_association_->cached_scanned_drivers_;
            data.do_upload_ = true;
            std::string current_hash;
            {
                std::lock_guard<std::mutex> session_lock (driver_association_->session_data_.mutex_);
                data.audio_count_ = driver_association_->session_data_.audio_play_count_;
                current_hash = driver_association_->session_data_.hash_;
            }

            persistence_ptr_->AddLoginEntry(std::move(data), current_hash);
        }

        if (!file_touch(kDriverAppLoginIndicatorFile)) {
            LOG_E(kLogTag, "Failed to create %s in [%s:%d]", kDriverAppLoginIndicatorFile, __func__, __LINE__);
        }
    }
}
#endif

void BTManager::CleanUp() {
    LOG_I(kLogTag, "Entered %s", __func__);

    is_exit_main_loop_set_ = true;

    try {
        if (cyclic_timer_tick_) {
            LOG_I(kLogTag, "Stopping timer tick...");
            cyclic_timer_tick_->Stop();
        }
    } catch (const std::exception &e) {
        LOG_E(kLogTag, "[%s:%d:%s] Exception stopping timer: %s", __FILE__, __LINE__, __func__, e.what());
    }

    try {
        if (bt_interface_ptr_) {
            LOG_I(kLogTag, "Disabling BT interface...");
            bt_interface_ptr_->LeScanOff();
            bt_interface_ptr_->Disable();
            LOG_I(kLogTag, "Interface deleted");
        }
    } catch (const std::exception &e) {
        LOG_E(kLogTag, "[%s:%d:%s] Exception disabling BT: %s", __FILE__, __LINE__, __func__, e.what());
    }

    try {
        if (persistence_ptr_) {
            LOG_I(kLogTag, "Stopping persistence...");
            persistence_ptr_->StopProcessing();
        }
    } catch (const std::exception &e) {
        LOG_E(kLogTag, "[%s:%d:%s] Exception stopping persistence: %s", __FILE__, __LINE__, __func__, e.what());
    }

    try {
        LOG_I(kLogTag, "Stopping audio play loop...");
        StopAudioPlayLoop();
    } catch (const std::exception &e) {
        LOG_E(kLogTag, "[%s:%d:%s] Exception stopping audio: %s", __FILE__, __LINE__, __func__, e.what());
    }

    try {
        LOG_I(kLogTag, "Unsubscribing from QR logins...");
        UnSubscribeToQrLogins();
    } catch (const std::exception &e) {
        LOG_E(kLogTag, "[%s:%d:%s] Exception in UnSubscribeToQrLogins: %s", __FILE__, __LINE__, __func__, e.what());
    }

    LOG_I(kLogTag, "Exiting %s", __func__);
}

bool BTManager::HandleGenericEvent(void *msg) {
    bool status = true;
    generic_msg_t *g_msg = reinterpret_cast<generic_msg_t *>(msg);

    const auto client_name = nd::platform_specific::FetchMsgClientName(msg);

    switch(g_msg->msg_type) {

        case INSTALLER_APP_DETECTION_TIMEOUT: {
            LOG_I(kLogTag, "[GEN] Received INSTALLER_APP_DETECTION_TIMEOUT msg from: %s", client_name.c_str());
            HandleInstallerAppDetectionTimeout();
            break;
        }

        case NEW_BLE_ALERT_DEVICE: {
            LOG_I(kLogTag, "[GEN] Received NEW_BLE_ALERT_DEVICE msg from: %s", client_name.c_str());
            HandleNewBleAlertDevice(g_msg);
            break;
        }

        case DRV_LOGIN_QUERY: {
            LOG_I(kLogTag, "[GEN] Received DRV_LOGIN_QUERY msg from: %s", client_name.c_str());
            HandleDriverLoginQuery(g_msg);
            break;
        }

        case RES_IDLE_REG: {
            LOG_I(kLogTag, "[GEN] Received RES_IDLE_REG msg from: %s", client_name.c_str());
            HandleIdleEventRegistrationResponse(g_msg);
            break;
        }

        case RES_SPEED_REG: {
            LOG_I(kLogTag, "[GEN] Received RES_SPEED_REG msg from: %s", client_name.c_str());
            HandleSpeedEventRegistrationResponse(g_msg);
            break;
        }

        case POWERMON_IGNITION: {
            LOG_I(kLogTag, "[GEN] Received POWERMON_IGNITION msg from: %s", client_name.c_str());
            HandleIgnitionStatus(g_msg);
            break;
        }

#ifdef ENABLE_VEHICLE_QR_BASED_LOGIN
        case DRIVER_LOGIN_APP_QR_SCAN_COMPLETE: {
            LOG_I(kLogTag, "Received DRIVER_LOGIN_APP_QR_SCAN_COMPLETE msg from: %s", client_name.c_str());
            HandleDriverLoginQrScanTimeout();
            break;
        }

        case DRIVER_LOGIN_APP_QR_DETECTED : {
            LOG_I(kLogTag, "Received DRIVER_LOGIN_APP_QR_DETECTED msg from: %s", client_name.c_str());
            DoDriverLoginAppQrActivity();
            break;
        }
#endif

        case BTFV_SHUTDOWN : {
            LOG_I(kLogTag, "Received BTFV_SHUTDOWN msg from: %s", client_name.c_str());
            CleanUp();
            break;
        }

        case DEVICE_WRITE_CHAR : {
            LOG_I(kLogTag, "Received DEVICE_WRITE_CHAR msg from: %s", client_name.c_str());
            HandleDeviceWriteChar(g_msg, client_name);
            break;
        }

        case VBUS_BT_DETECTION_TIMEOUT : {
            LOG_I(kLogTag, "Received VBUS_BT_DETECTION_TIMEOUT msg from: %s", client_name.c_str());
            HandleVBUSDetectionTimeout();
            break;
        }

        case REQUEST_ANTENNA_TIME: {
            LOG_I(kLogTag, "Received REQUEST_ANTENNA_TIME msg from: %s", client_name.c_str());
            HandleRequestAntennaTime(g_msg);
            break;
        }

        case RESET_ANTENNA_TIME: {
            LOG_I(kLogTag, "Received RESET_ANTENNA_TIME msg from: %s", client_name.c_str());
            ResetAntennaTime(g_msg);
            break;
        }

        case BT_INTERNAL_EVENT: {
            LOG_I(kLogTag, "Received BT_INTERNAL_EVENT msg from: %s", client_name.c_str());
            HandleInternalEvent(g_msg);
            break;
        }

        case START_QR_SCAN_RES: {
            LOG_I(kLogTag, "Received START_QR_SCAN_RES msg from: %s", client_name.c_str());
            HandleStartQrScanResponse(g_msg);
            break;
        }

        case STOP_QR_SCAN_RES: {
            LOG_I(kLogTag, "Received STOP_QR_SCAN_RES msg from: %s", client_name.c_str());
            HandleStopQrScanResponse(g_msg);
            break;
        }

        case BAGHEERA_RESTART_DONE_MSG: {
            LOG_I(kLogTag, "Received BAGHEERA_RESTART_DONE_MSG from: %s", client_name.c_str());
            HandleBagheeraRestartDoneMessage();
            break;
        }

        case SPEED_SERVICE_STARTED: {
            LOG_I(kLogTag, "Received SPEED_SERVICE_STARTED msg from: %s", client_name.c_str());

            break;
        }

        default: {
            status = false;
            break;
        }
    }
    return status;
}

void BTManager::HandleStartQrScanResponse(void *g_msg) {
    const auto client_name = nd::platform_specific::FetchMsgClientName(g_msg);
    QrScanStatusCodes status_code = QR_SCAN_STATUS_FAILED;

    std::string reason;
    if (nd::platform_specific::FetchQRScanStartStatus(g_msg, status_code, reason)) {
        LOG_I(kLogTag, "QR Scan start response: %d from: %s, reason: %s", status_code, client_name.c_str(), reason.c_str());

        const bool is_scan_on = ((QR_SCAN_STATUS_SUCCESS == status_code) ||
                                 (QR_SCAN_STATUS_IN_PROGRESS == status_code)) ? true : false;

        const bool is_timeout = (QR_SCAN_STATUS_TIMEOUT == status_code);

        is_qr_scan_active_ = is_scan_on;

        if (is_qr_scan_required_) {
            if (!is_qr_scan_active_) {
                LOG_E(kLogTag, "QR Scan could not be started, clearing QR filters");
                ReportDLScanStatusToHealthStats(kDLSourceStrMap.at(DLSource::kQR), true, reason);
            } else {
                ReportDLScanStatusToHealthStats(kDLSourceStrMap.at(DLSource::kQR), true, "QR scan started");
            }

            if ((QR_SCAN_ERROR_FEATURE_DISABLED == status_code) || (QR_SCAN_ERROR_INWARD_CAM_DISABLED == status_code)) {
                is_qr_scan_required_ = false;
                is_qr_scan_allowed_ = false;
                LOG_W(kLogTag, "Inward camera or qr feature is disabled, stopping further QR scan attempts");

                if (!HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kEnhancedBt)) {
                    ClearLoginAudioPrompt();
                    LOG_W(kLogTag, "Cleared login audio prompt as Enhanced BT feature is not enabled");
                }
            }

        } else {
            LOG_I(kLogTag, "QR Scan not required, sending stop scan");
            nd::platform_specific::SendStopQRScanMsg(kBT_Server_MQ_Name, kNDCentral_Server_MQ_Name, msg_id_);
            FileSystemHandler::DeleteFile(GetQrScanStartIndicatorPath());
        }

        if (is_timeout) {
            LOG_E(kLogTag, "QR Scan start timed out, not intended");
            if (!service_obj_ptr_->send_err_msg(SM_E_BTFV_QR_LOGIN, static_cast<int>(QRLoginErrCode::kScanStartTimeout),
                                                "QR scan start timeout")) {
                LOG_E(kLogTag, "SM_E_BTFV_QR_LOGIN send_err_msg failed");
            }
        }

    } else {
        LOG_W(kLogTag, "Failed to fetch QR scan start status from: %s", client_name.c_str());
    }
}

void BTManager::HandleStopQrScanResponse(void *g_msg) {
    const auto client_name = nd::platform_specific::FetchMsgClientName(g_msg);
    QrScanStatusCodes status_code = QR_SCAN_STATUS_FAILED;

    std::string reason;

    if (nd::platform_specific::FetchQRScanStopStatus(g_msg, status_code, reason)) {
        LOG_I(kLogTag, "QR Scan stop response: %d from: %s, reason: %s", status_code, client_name.c_str(), reason.c_str());

        const bool is_scan_stopped = (QR_SCAN_STATUS_SUCCESS == status_code) ||
                                     (QR_SCAN_ERROR_FEATURE_DISABLED == status_code) ||
                                     (QR_SCAN_ERROR_INWARD_CAM_DISABLED == status_code) ||
                                     (QR_SCAN_ERROR_NOT_IN_PROGRESS == status_code);

        const bool is_timeout = (QR_SCAN_STATUS_TIMEOUT == status_code);

        if (!is_scan_stopped) {
            LOG_W(kLogTag, "QR Scan could not be stopped");
            ReportDLScanStatusToHealthStats(kDLSourceStrMap.at(DLSource::kQR), false, reason);
        } else {
            ReportDLScanStatusToHealthStats(kDLSourceStrMap.at(DLSource::kQR), false, "QR scan stopped");
        }

        if (is_timeout) {
            LOG_W(kLogTag, "QR Scan timed out");
            ReportDLScanStatusToHealthStats(kDLSourceStrMap.at(DLSource::kQR), false, "QR scan timed out");
            if (is_qr_scan_required_) {
                LOG_I(kLogTag, "Re-initiating QR scan as it is required");
                const bool scan_msg_status = nd::platform_specific::SendStartQRScanMsg(config_data_->qr_login_tags_,
                                                                                       kBT_Server_MQ_Name,
                                                                                       kNDCentral_Server_MQ_Name,
                                                                                       msg_id_);
                if (!scan_msg_status) {
                    RegisterStartQrMsgRetry();
                } else {
                    if (!file_touch(GetQrScanStartIndicatorPath())) {
                        LOG_E(kLogTag, "Failed to create %s in [%s:%d]", GetQrScanStartIndicatorPath().c_str(), __func__, __LINE__);
                    }
                }

                const std::string message = scan_msg_status ? "QR scan start requested on nd-central qr timeout"
                                                              : "QR scan start request failed on nd-central qr timeout";

                ReportDLScanStatusToHealthStats(kDLSourceStrMap.at(DLSource::kQR), true, message);
            }
        }

    } else {
        LOG_W(kLogTag, "Failed to fetch QR scan stop status from: %s", client_name.c_str());
    }
}

void BTManager::HandleBagheeraRestartDoneMessage() {
    LOG_I(kLogTag, "Inside: %s", __func__);

    // Re-init QR scan if required
    if (is_qr_scan_required_) {
        LOG_I(kLogTag, "Re-initiating QR scan as it is required");

        const bool scan_msg_status = nd::platform_specific::SendStartQRScanMsg(config_data_->qr_login_tags_,
                                                                               kBT_Server_MQ_Name,
                                                                               kNDCentral_Server_MQ_Name,
                                                                               msg_id_);
        if (!scan_msg_status) {
            RegisterStartQrMsgRetry();
        } else {
            if (!file_touch(GetQrScanStartIndicatorPath())) {
                LOG_E(kLogTag, "Failed to create %s in [%s:%d]", GetQrScanStartIndicatorPath().c_str(), __func__, __LINE__);
            }
        }

        const std::string message = scan_msg_status ? "QR scan start requested on nd-central restart"
                                                    : "QR scan start request failed on nd-central restart";

        ReportDLScanStatusToHealthStats(kDLSourceStrMap.at(DLSource::kQR), true, message);
    }
}

void BTManager::PlayLoginAcknowledgeAudio(bool is_forced) {

    LOG_I(kLogTag, "Inside: %s", __func__);

    static auto last_audio_play_time = int64_t{0}; // static, store last audio time

    const auto now = CurrentSteadyClockSeconds();

    if ((is_forced) ||
        (0 == last_audio_play_time) ||
        (now > last_audio_play_time + kQrLoginAckAudioIntervalSecs)) {

        last_audio_play_time = now;
        nd::platform_specific::SendDriverLoginAudioNotification(config_data_->qr_login_success_audio_file_,
                                                        kBT_Server_MQ_Name, kPower_Server_MQ_Name, msg_id_);

        const std::string state = ((States::kMotion == current_state_) ? "motion" : "idle");
        const std::string desc = "successful login audio"s + (is_forced ? "" : " -- duplicate");

        ReportDriveAudioInfoToHealthStats(state, desc, true);
    }
}

void BTManager::HandleInternalEvent(void *msg) {
    int32_t event = 0;

    const auto client_name = nd::platform_specific::FetchMsgClientName(msg);
    nd::platform_specific::FetchInternalEventMessage(msg, event);
    LOG_I(kLogTag, "Received internal event: %d from: %s", event, client_name.c_str());

    const auto internal_event = static_cast<InternalEvents>(event);

    switch (internal_event) {
        case InternalEvents::kQrLogins: {
            HandleQrLoginUpdate();
            break;
        }

        case InternalEvents::kQrLoginAckAudio: {
            PlayLoginAcknowledgeAudio();
            break;
        }

        case InternalEvents::kNearbyDevicesSetFull: {
            ClearFilter(FilterKeys::kNearbyDevices);
            break;
        }

        default: {
            LOG_E(kLogTag, "Unknown internal event: %d", static_cast<int32_t>(internal_event));
            break;
        }
    }
}

void BTManager::HandleRequestAntennaTime(void *msg) {
    const auto client_name = nd::platform_specific::FetchMsgClientName(msg);

    if (!bt_interface_ptr_->SupportsScanAndConnectionParallelly()) {

        {
            const std::lock_guard<std::mutex> lock(scan_events_timeout_->data_lock_);
            auto const now = CurrentSteadyClockSeconds();
            scan_events_timeout_->data_map_[TimeoutEvents::kRelaxedScan] = now + config_data_->relaxed_scan_max_timeout_;
        }

        {
            if (1 == registered_filter_id_map_.count(FilterKeys::kDriverLoginService)) {
                LOG_I(kLogTag, "Stopping DriverLoginLegacyAdvertisement for relaxed scan");
                StopDriverLoginLegacyAdvertisement();
            }
        }

        bt_interface_ptr_->SetScanParameters(config_data_->relaxed_scan_interval_, config_data_->relaxed_scan_window_);
        if (bt_interface_ptr_->IsEnabled() && bt_interface_ptr_->IsLeScanOn()) {
            bt_interface_ptr_->LeScanOff();
            bt_interface_ptr_->LeScanOn();
        }
    }

    nd::platform_specific::SendAntennaTimeResponse(kBT_Server_MQ_Name, client_name.c_str(), msg_id_);
}

void BTManager::ResetAntennaTime(void *msg) {
    if (!bt_interface_ptr_->SupportsScanAndConnectionParallelly()) {

        bt_interface_ptr_->SetScanParameters(config_data_->scan_interval_, config_data_->scan_window_);
        if (bt_interface_ptr_->IsEnabled() && bt_interface_ptr_->IsLeScanOn()) {
            bt_interface_ptr_->LeScanOff();
            bt_interface_ptr_->LeScanOn();
        }

        {
            const std::lock_guard<std::mutex> lock(scan_events_timeout_->data_lock_);
            scan_events_timeout_->data_map_.erase(TimeoutEvents::kRelaxedScan);
        }

        if (bt_interface_ptr_->IsEnabled()) {
            if (1 == registered_filter_id_map_.count(FilterKeys::kDriverLoginService)) {
                LOG_I(kLogTag, "Starting DriverLoginLegacyAdvertisement post relaxed scan");
                StartDriverLoginLegacyAdvertisement();
            }
        }
    }
}

void BTManager::HandleVBUSDetectionTimeout() {
    ClearVBUSFilter();
    const bool device_found = false;
    nd::platform_specific::SendVbusAvailabilityMessage(device_found, kBT_Server_MQ_Name,
                                                       kAwsIot_Pub_Server_MQ_Name, msg_id_);
    if (config_data_->can_vd_enabled_) {
        nd::platform_specific::SendVbusAvailabilityMessage(device_found, kBT_Server_MQ_Name,
                                                           kObd_Server_MQ_Name, msg_id_);
    }

    StopScanDisableBT();
}

bool BTManager::IsFilterSet(FilterKeys key) {
    LOG_I(kLogTag, "Inside: %s", __func__);
    return (0 < registered_filter_id_map_.count(key));
}

bool BTManager::ClearDLRegister(LoginRegisters key) {
    return (0 != login_registers_set_.erase(key));
}

bool BTManager::ClearFilter(FilterKeys key) {
    bool status = false;
    LOG_I(kLogTag, "Inside: %s", __func__);
    const auto itr = registered_filter_id_map_.find(key);
    if (registered_filter_id_map_.end() != itr && 0 != itr->second) {
        LOG_I(kLogTag, "Found entry for %d, Calling ClearFilter", static_cast<int>(key));
        ble_observer_ptr_->ClearFilter(itr->second);
        registered_filter_id_map_.erase(key);
        status = true;
    } else {
        LOG_I(kLogTag, "No entry found entry for %d", static_cast<int>(key));
    }

    return status;
}

bool BTManager::ClearVBUSFilter() {
    LOG_I(kLogTag, "Inside: %s", __func__);
    return ClearFilter(FilterKeys::kVBUS);
}

#ifdef ENABLE_VEHICLE_QR_BASED_LOGIN
bool BTManager::ClearDriverLoginAppQRFilter() {
    LOG_I(kLogTag, "Inside: %s", __func__);
    return ClearFilter(FilterKeys::kVehicleQr);
}
#endif

bool BTManager::ClearInstallerAppFilter() {
    LOG_I(kLogTag, "Inside: %s", __func__);
    return ClearFilter(FilterKeys::kInstallerApp) && ClearFilter(FilterKeys::kInstallerAppUpdate);
}

bool BTManager::ClearDriverLoginLegacyFilters() {
    LOG_I(kLogTag, "Inside: %s", __func__);

    bool status = ClearFilter(FilterKeys::kDriverLoginService);
    status &= ClearFilter(FilterKeys::kDriverLoginEddyStone);
    status &= ClearFilter(FilterKeys::kDriverLoginAppleBackgroundService);

    return status;
}

bool BTManager::ClearAlertFilter() {
    LOG_I(kLogTag, "Inside: %s", __func__);
    return ClearFilter(FilterKeys::kBleUserAlert);
}

bool BTManager::ClearBatteryStatusFilter() {
    LOG_I(kLogTag, "Inside: %s", __func__);
    return ClearFilter(FilterKeys::kBleBatteryStatus);
}

bool BTManager::ClearBlePairFilter() {
    LOG_I(kLogTag, "Inside: %s", __func__);
    return ClearFilter(FilterKeys::kBlePair);
}

void BTManager::ReportPeriodicNearbyDevices() {
    LOG_I(kLogTag, "Entered %s", __func__);

    ReportNearbyDevicesToHealthStats();

    nearby_devices_set_full_sent_.store(false);

    ClearFilter(FilterKeys::kNearbyDevices);

    if ((0 == registered_filter_id_map_.count(FilterKeys::kNearbyDevices))) {
        LOG_I(kLogTag, "Re-registering NearbyDevices filter after report");
        RegisterNearbyDevicesFilter();
    }
}

void BTManager::ReportPeriodicNearbyAlertBeaconDevices() {
    LOG_I(kLogTag, "Entered %s", __func__);

    ReportNearbyAlertBeaconToHealthStats();

    {
        std::lock_guard<std::mutex> lock(retry_event_queue_ptr_->data_lock_);
        const uint64_t later = CurrentSteadyClockSeconds() +
                                   kInternalRetryEventsTimeoutMap.at(InternalRetryEvents::kNearbyAlertBeaconReport);
        retry_event_queue_ptr_->data_map_.emplace(later,
                                                  RetryEventData{.type_ = InternalRetryEvents::kNearbyAlertBeaconReport,
                                                                .data_ = nullptr});
    }
}

bool BTManager::RegisterDriverLoginLegacyFilter() {
    LOG_I(kLogTag, "Inside: %s", __func__);

    if (0 == registered_filter_id_map_.count(FilterKeys::kDriverLoginService)) {

        // Clear previously scanned logins
        scanned_legacy_logins_->clients_.clear();

        class DriverLoginEddyStoneSpec : virtual public ISpecification {
         public:
            explicit DriverLoginEddyStoneSpec(const std::vector<uint8_t> &checksum):checksum_(checksum){}
            virtual bool IsSatisfied(const AdvertisementBase &device_attr) override {
                bool status = false;
                const ServiceInfo * const serv_info_ptr = static_cast<const ServiceInfo *>(&device_attr);
                if (SpecificationType::kEddyStoneUID == device_attr.type_) {
                    if (( 1 < (serv_info_ptr->service_data_).size()) &&
                        (std::equal(checksum_.begin(), checksum_.end(), (serv_info_ptr->service_data_).begin() + 1))) {
                        // do checksum match
                        status = true;
                        std::vector<uint8_t> driver_id((serv_info_ptr->service_data_).end() - 4, (serv_info_ptr->service_data_).end());
                        const auto driver_id_str = Translator::BytesToHexString(driver_id.data(), driver_id.size());
                        LOG_I(kLogTag, "Found Eddystone with name: %s mac: %s and driver id %s",
                                device_attr.name_.c_str(), device_attr.mac_address_.c_str(),
                                driver_id_str.c_str());
                    }
                }
                return status;
            }
         private:
            const std::vector<uint8_t> checksum_;
        };

        class DriverLoginAppleBackgroundSpec : virtual public ISpecification {
         public:
            virtual bool IsSatisfied(const AdvertisementBase &device_attr) override {
                bool status = false;
                if (SpecificationType::kAppleBackgroundServiceID == device_attr.type_) {
                    LOG_I(kLogTag, "Found Background Apple: %s with mac: %s", device_attr.name_.c_str(),
                          device_attr.mac_address_.c_str());
                    status = true;
                }

                return status;
            }
        };

        class DriverLoginServiceIDSpec : virtual public ISpecification {
         public:
            virtual bool IsSatisfied(const AdvertisementBase &device_attr) override {
                bool status = false;

                const ServiceInfo * const serv_info_ptr = static_cast<const ServiceInfo *>(&device_attr);
                if (SpecificationType::kServiceID == device_attr.type_) {
                    for (const auto & uuid : serv_info_ptr->service_uuid_) {
                        if (driver_login_service_uuid_ == uuid) {
                            LOG_I(kLogTag, "Found Driver Login App: %s with mac: %s", device_attr.name_.c_str(), device_attr.mac_address_.c_str());
                            status = true;
                        }
                    }
                }
                return status;
            }
         private:
            // b0c14f1f019007b8ec4ae1cde8a47527
            const std::vector<uint8_t> driver_login_service_uuid_ = {0xb0, 0xc1, 0x4f, 0x1f, 0x01, 0x90, 0x07, 0xb8,
                                                                    0xec, 0x4a, 0xe1, 0xcd, 0xe8, 0xa4, 0x75, 0x27};
        };

        auto driver_login_fn_cb = [&](const AdvertisementBase &device_attr) {
            const ServiceInfo * const serv_info_ptr = static_cast<const ServiceInfo *>(&device_attr);

            DriverData driver_data{};
            driver_data.info_ = *serv_info_ptr;

            LOG_I(kLogTag, "Adding device: %s with mac: %s to vector", serv_info_ptr->name_.c_str(),
                    serv_info_ptr->mac_address_.c_str());
            {
                const std::lock_guard<std::mutex> lock(scanned_legacy_logins_->data_lock_);
                scanned_legacy_logins_->clients_.emplace(serv_info_ptr->mac_address_, std::move(driver_data));
            }
        };

        const auto checksum = Translator::HexStringToBytes(config_data_->ble_hash_);

        if (HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kLegacy)) {

            std::unique_ptr<DriverLoginEddyStoneSpec> driver_login_eddy_stone_spec = std::make_unique<DriverLoginEddyStoneSpec>(checksum);
            registered_filter_id_map_[FilterKeys::kDriverLoginEddyStone] = ble_observer_ptr_->SetFilter(SpecificationType::kEddyStoneUID,
                                                                                                        std::move(driver_login_eddy_stone_spec),
                                                                                                        driver_login_fn_cb, false, false);
        }

        std::unique_ptr<DriverLoginAppleBackgroundSpec> driver_login_apple_bg_spec = std::make_unique<DriverLoginAppleBackgroundSpec>();
        registered_filter_id_map_[FilterKeys::kDriverLoginAppleBackgroundService] = ble_observer_ptr_->SetFilter(
                                                                                                    SpecificationType::kAppleBackgroundServiceID,
                                                                                                    std::move(driver_login_apple_bg_spec),
                                                                                                    driver_login_fn_cb,
                                                                                                    false, false);

        std::unique_ptr<DriverLoginServiceIDSpec> driver_login_service_id_spec = std::make_unique<DriverLoginServiceIDSpec>();
        registered_filter_id_map_[FilterKeys::kDriverLoginService] = ble_observer_ptr_->SetFilter(SpecificationType::kServiceID,
                                                                                                  std::move(driver_login_service_id_spec),
                                                                                                  driver_login_fn_cb, false, false);

    } else {
        LOG_I(kLogTag, "kDriverLoginService filter is already registered", __func__);
    }

    return true;
}

bool BTManager::EnableBTStartScan(bool force_enable) {

    bool status = false;

    do {

        // Turn on BT and Scan if not already started
        if (bt_interface_ptr_->IsEnabled()) {
            status = bt_interface_ptr_->LeScanOn();
            break;
        }

        if ((!force_enable) &&
            ((!is_uninterrupted_scan_required_) && (registered_filter_id_map_.empty()))) {
            LOG_I(kLogTag, "Scan is not required, not enabling BT");
            break;
        }

        uint32_t enable_max_retries = 2;

        do {
            if (bt_interface_ptr_->Enable()) {
                break;
            }
            bt_interface_ptr_->Disable();
        } while ((--enable_max_retries) > 0);

        if (bt_interface_ptr_->IsEnabled()) {
            status = bt_interface_ptr_->LeScanOn();
            break;
        } else {

            LOG_E(kLogTag, "Cleanup and exit service as BT module enable failed");

            if (!service_obj_ptr_->send_err_msg(SM_E_BTFV_BT_STACK_FAIL, static_cast<int>(BtStackErr::kEnableFailed),
                                                "BT module enable failed")) {
                LOG_E(kLogTag, "SM_E_BTFV_BT_STACK_FAIL send_err_msg failed");
            }

            // cleanup and exit service
            CleanUp();
            exit(EXIT_FAILURE);
        }

    } while (false);

    return status;
}

bool BTManager::StopScanDisableBT(bool force_disable) {

    bool status = false;

    do {
        if (!bt_interface_ptr_->IsEnabled()) {
            status = true;
            break;
        }

        if ((!force_disable) &&
            ((is_uninterrupted_scan_required_) || (!registered_filter_id_map_.empty()))) {
            LOG_I(kLogTag, "Scan is required, not disabling BT");
            break;
        }

        // Stop scan and turn off BT
        bt_interface_ptr_->LeScanOff();
        status = bt_interface_ptr_->Disable();
    } while (false);

    return status;
}

bool BTManager::RegisterBlePairFilter() {
    LOG_I(kLogTag, "Inside: %s", __func__);

    do {

        if (!config_data_->beacon_auto_pairing_enabled_) {
            LOG_I(kLogTag, "ble_alert auto_pairing is disabled, filter is not registered");
            break;
        }

        if (0 != registered_filter_id_map_.count(FilterKeys::kBlePair)) {
            LOG_I(kLogTag, "kBlePair filter is already registered");
            break;
        }

        class BlePairSpec : virtual public ISpecification {
         public:
            explicit BlePairSpec(const std::vector<uint8_t> &tag): uuid_tag_(tag) {}
            virtual bool IsSatisfied(const AdvertisementBase &device_attr) override {
                bool status = false;

                do {
                    const BeaconInfo * const beacon_info_ptr = static_cast<const BeaconInfo *>(&device_attr);

                    if (SpecificationType::kBeaconUUID != device_attr.type_) {
                        break;
                    }

                    // TODO(sunil.s): Update for new firmware behaviour

                    // TODO(sunil.s) Checking only driver for now, if required for other variants, uncomment below
                    if ((std::equal(uuid_tag_.begin(), uuid_tag_.end(), beacon_info_ptr->uuid_)) &&
                        ((std::equal(std::begin(beacon_info_ptr->major_), std::end(beacon_info_ptr->major_),
                                     std::begin(nd::device::kDriverUuidMajor)))/* ||
                         (std::equal(std::begin(beacon_info_ptr->major_), std::end(beacon_info_ptr->major_),
                                     std::begin(nd::device::kPassengerUuidMajor))) ||
                         (std::equal(std::begin(beacon_info_ptr->major_), std::end(beacon_info_ptr->major_),
                                     std::begin(nd::device::kTrailerUuidMajor)))*/)) {
                        status = true;
                        break;
                    }
                } while (false);

                return status;
            }
         private:
            const std::vector<uint8_t> uuid_tag_;
        };

        auto device_fn_cb = [&](const AdvertisementBase &device_attr) {
            const BeaconInfo * const beacon_info_ptr = static_cast<const BeaconInfo *>(&device_attr);

            const int rssi = static_cast<int8_t>(beacon_info_ptr->rssi_);
            const int tx_power = static_cast<int8_t>(beacon_info_ptr->tx_power_);

            const uint64_t time = CurrentSystemClockMilliSeconds();
            LOG_C(kLogTag, "Received pair packet from: %s with mac: %s at: %llu", beacon_info_ptr->name_.c_str(), beacon_info_ptr->mac_address_.c_str(), time);
            LOG_I(kLogTag, "iBeacon msg power: %ddBm, rssi: %ddBm possibly at proximity of: %lfm", tx_power, rssi, beacon_info_ptr->distance_);

            BleDeviceAssignees assignee = BleDeviceAssignees::kDriver;
            bool is_major_valid = true;

            if (std::equal(std::begin(beacon_info_ptr->major_), std::end(beacon_info_ptr->major_),
                            std::begin(nd::device::kDriverUuidMajor))) {
                assignee = BleDeviceAssignees::kDriver;
            // TODO(sunil.s) Checking only driver for now, if required for other variants, uncomment below

            // } else if (std::equal(std::begin(beacon_info_ptr->major_), std::end(beacon_info_ptr->major_),
            //                       std::begin(nd::device::kPassengerUuidMajor))) {
            //     assignee = BleDeviceAssignees::kPassenger;
            // } else if (std::equal(std::begin(beacon_info_ptr->major_), std::end(beacon_info_ptr->major_),
            //            std::begin(nd::device::kTrailerUuidMajor))) {
            //     assignee = BleDeviceAssignees::kTrailer;
            } else {
                is_major_valid = false;
            }

            if (is_major_valid) {
                bool is_new_mac = false;
                {
                    const std::lock_guard<std::mutex> lock(ble_devices_data_ptr_->data_lock_);
                    const auto emplace_status = (ble_devices_data_ptr_->data_map_[assignee]).emplace(beacon_info_ptr->mac_address_,
                                                                                                        BleDeviceAssociatedData{});
                    is_new_mac = emplace_status.second;
                }

                LOG_I(kLogTag, "Adding mac: %s to %s", beacon_info_ptr->mac_address_.c_str(),
                        (kBleDeviceAssigneesConstantsMap.at(assignee)).label_.c_str());

                // TODO(sunil.s): This should be used for LED glow -> confirm with PM
                nd::platform_specific::SendAlertBeaconPairedMessage(kBT_Server_MQ_Name, kNDCentral_Server_MQ_Name, msg_id_);

                if (is_new_mac) {
                    {
                        const std::lock_guard<std::mutex> lock(new_ble_devices_ptr_->data_lock_);
                        new_ble_devices_ptr_->data_map_[assignee].emplace_back(beacon_info_ptr->mac_address_);
                    }

                    // TODO(sunil.s): complete for requirements
                    nd::platform_specific::SendGenericMessage(NEW_BLE_ALERT_DEVICE, "NEW_BLE_ALERT_DEVICE",
                                                                kBT_Server_MQ_Name, kBT_Server_MQ_Name, msg_id_);

                }
            }
        };

        std::unique_ptr<BlePairSpec> ble_device_spec = std::make_unique<BlePairSpec>(config_data_->long_press_tag_);

        registered_filter_id_map_[FilterKeys::kBlePair] = ble_observer_ptr_->SetFilter(SpecificationType::kBeaconUUID,
                                                                                       std::move(ble_device_spec),
                                                                                       device_fn_cb, false, true);

    } while (false);

    return true;
}

void BTManager::ReportBleAlertToHealthStats(uint8_t seq, uint8_t age_secs, const std::string &mac, int32_t rssi) {
    LOG_I(kLogTag, "Entered %s", __func__);

    auto const now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    constexpr char kTimestampStr[]  = "ts";
    constexpr char kMacStr[]        = "mac";
    constexpr char kSeqStr[]        = "seq";
    constexpr char kAgeSecsStr[]    = "age_secs";
    constexpr char kRssiStr[]       = "rssi";
    constexpr char kIsArrayStr[]    = "isArray";
    constexpr char kHealthInfoStr[] = "health_info:peripherals:alert_beacon:alert";
    constexpr char kArrayTrueStr[]  = "true";

    json_error_t error{};

    json_t *pack_root = json_pack_ex(&error, 0, "{s:{s:I, s:s, s:i, s:i, s:i}, s:s}",
                                         kHealthInfoStr,
                                            kTimestampStr, now,
                                            kMacStr, mac.c_str(),
                                            kSeqStr, static_cast<int>(seq),
                                            kAgeSecsStr, static_cast<int>(age_secs),
                                            kRssiStr, rssi,
                                        kIsArrayStr, kArrayTrueStr
                                        );

    if (nullptr != pack_root) {
        char *dump = json_dumps(pack_root, JSON_COMPACT);
        if (nullptr != dump) {
            if (service_obj_ptr_->send_msg_healthstats(dump, strlen(dump))) {
                LOG_I(kLogTag, "BLE Alert Data reported successfully to HealthStats");
                LOG_I(kLogTag, "Data: %s", dump);
            } else {
                LOG_E(kLogTag, "BLE Alert Data failed to be reported to HealthStats, adding to pending");
                std::string health_msg = dump;
                AddToPendingHealthData(std::move(health_msg));
            }
            free(dump);
        } else {
            LOG_E(kLogTag, "ReportBleAlertToHealthStats: json_dumps failed");
        }

        json_decref(pack_root);
    } else {
        LOG_E(kLogTag, "ReportBleAlertToHealthStats: json_pack failed error: %s", error.text);
    }
}

void BTManager::ReportNearbyDevicesToHealthStats() {
    LOG_I(kLogTag, "Inside: %s", __func__);

    do {
        std::unordered_set<std::string> snapshot;

        // Take snapshot of nearby devices
        {
            const std::lock_guard<std::mutex> lock(nearby_devices_data_ptr_->data_lock_);
            if (nearby_devices_data_ptr_->data_set_.empty()) {
                LOG_I(kLogTag, "No nearby devices to report");
                break;
            }
            snapshot.swap(nearby_devices_data_ptr_->data_set_); // clears original set
        }

        // Create JSON array for devices
        json_t *devices_array = json_array();
        if (!devices_array) {
            LOG_E(kLogTag, "ReportNearbyDevicesToHealthStats: json_array alloc failed");
            break;
        }

        // Build JSON objects for each device
        for (const auto &mac_address : snapshot) {
            json_t *obj = json_object();
            if (!obj) {
                continue;
            }

            json_object_set_new(obj, "mac", json_string(mac_address.c_str()));
            json_array_append_new(devices_array, obj);
        }

        const uint64_t now_ms = CurrentSystemClockMilliSeconds();

        constexpr char kHealthKey[]    = "health_info:peripherals:nearby:bt_devices";
        constexpr char kIsArrayStr[]   = "isArray";
        constexpr char kArrayTrueStr[] = "true";
        constexpr char kTimestampStr[] = "ts";
        constexpr char kCountStr[]     = "count";
        constexpr char kDevicesStr[]   = "devices";

        // Pack complete JSON structure
        json_error_t error{};
        json_t *root = json_pack_ex(&error, 0, "{s:{s:I, s:i, s:o}, s:s}",
                                    kHealthKey,
                                      kTimestampStr, now_ms,
                                      kCountStr, (int)snapshot.size(),
                                      kDevicesStr, devices_array,
                                    kIsArrayStr, kArrayTrueStr);

        if (!root) {
            LOG_E(kLogTag, "ReportNearbyDevicesToHealthStats: json_pack failed: %s", error.text);
            json_decref(devices_array);
            break;
        }

        // Convert to string
        char *dump = json_dumps(root, JSON_COMPACT);
        if (!dump) {
            LOG_E(kLogTag, "ReportNearbyDevicesToHealthStats: json_dumps failed");
            json_decref(root);
            break;
        }

        // Send to health stats service
        if (service_obj_ptr_->send_msg_healthstats(dump, (int)strlen(dump))) {
            LOG_I(kLogTag, "Reported %zu nearby BLE devices", snapshot.size());
        } else {
            LOG_E(kLogTag, "Failed to send nearby devices healthstats, queueing for retry");
            AddToPendingHealthData(std::string(dump));
        }

        free(dump);
        json_decref(root);

    } while (false);

    return;
}

void BTManager::ReportNearbyAlertBeaconToHealthStats() {
    LOG_I(kLogTag, "Entered %s", __func__);

    do {
        std::unordered_map<std::string, NearbyAlertBeaconInfo> snapshot;
        {
            const std::lock_guard<std::mutex> lock(nearby_alert_beacon_data_ptr_->data_lock_);
            if (nearby_alert_beacon_data_ptr_->data_map_.empty()) {
                LOG_I(kLogTag, "No Nearby Alert Beacon devices to report");
                break;
            }
            snapshot.swap(nearby_alert_beacon_data_ptr_->data_map_);
        }

        // Create JSON array for devices
        json_t *devices_array = json_array();
        if (!devices_array) {
            LOG_E(kLogTag, "%s: json_array alloc failed", __func__);
            break;
        }

        // Build JSON objects for each device
        for (const auto &kv : snapshot) {
            const std::string &mac = kv.first;
            const NearbyAlertBeaconInfo &info = kv.second;

            json_t *obj = json_object();
            if (!obj) continue;

            json_object_set_new(obj, "mac", json_string(mac.c_str()));
            json_object_set_new(obj, "bat_perc", json_integer(info.battery_pct_));
            json_object_set_new(obj, "speed", json_integer(info.live_speed_));

            json_array_append_new(devices_array, obj);
        }

        const uint64_t now_ms = CurrentSystemClockMilliSeconds();

        constexpr char kHealthKey[]    = "health_info:peripherals:nearby:alert_beacons";
        constexpr char kTimestampStr[] = "ts";
        constexpr char kCountStr[]     = "count";
        constexpr char kDevicesStr[]   = "devices";
        constexpr char kIsArrayStr[]   = "isArray";
        constexpr char kArrayTrueStr[] = "true";

        // Pack complete JSON structure
        json_error_t error{};
        json_t *root = json_pack_ex(&error, 0, "{s:{s:I, s:i, s:o}, s:s}",
                                    kHealthKey,
                                      kTimestampStr, now_ms,
                                      kCountStr, (int)snapshot.size(),
                                      kDevicesStr, devices_array,
                                    kIsArrayStr, kArrayTrueStr);

        if (!root) {
            LOG_E(kLogTag, "%s: json_pack failed: %s", __func__, error.text);
            json_decref(devices_array);
            break;
        }

        // Convert JSON to string
        char *dump = json_dumps(root, JSON_COMPACT);
        if (!dump) {
            LOG_E(kLogTag, "%s: json_dumps failed", __func__);
            json_decref(root);
            break;
        }

        // Send to health stats service
        if (service_obj_ptr_->send_msg_healthstats(dump, (int)strlen(dump))) {
            LOG_I(kLogTag, "Reported %zu Nearby Alert Beacon devices", snapshot.size());
        } else {
            LOG_E(kLogTag, "Failed to send Nearby Alert Beacon devices, queueing");
            AddToPendingHealthData(std::string(dump));
        }

        free(dump);
        json_decref(root);

    } while (false);

    return;
}

#ifdef Age_Based_Alert_Timestamp
bool BTManager::RegisterAlertFilter() {
    LOG_I(kLogTag, "Inside: %s", __func__);

    do {

        if (0 != registered_filter_id_map_.count(FilterKeys::kBleUserAlert)) {
            LOG_I(kLogTag, "kBleUserAlert filter is already registered");
            break;
        }

        class UserAlertSpec : virtual public ISpecification {
         public:
            explicit UserAlertSpec(const std::shared_ptr<BleDevicesData> &data_ptr,
                                   const std::vector<uint8_t> &tag):devices_data_ptr_(data_ptr), uuid_tag_(tag){}
            virtual bool IsSatisfied(const AdvertisementBase &device_attr) override {
                bool status = false;

                do {
                    const BeaconInfo * const beacon_info_ptr = static_cast<const BeaconInfo *>(&device_attr);

                    if (SpecificationType::kBeaconUUID != device_attr.type_) {
                        break;
                    }

                    // Must match UUID
                    if (!std::equal(uuid_tag_.begin(), uuid_tag_.end(), beacon_info_ptr->uuid_)) {
                        break;
                    }

                    if (std::equal(std::begin(beacon_info_ptr->major_), std::end(beacon_info_ptr->major_),
                                   std::begin(nd::device::kDriverUuidMajor))) {

                        const std::lock_guard<std::mutex> lock(devices_data_ptr_->data_lock_);
                        const std::string &mac_address = beacon_info_ptr->mac_address_;

                        status = std::any_of(std::begin(devices_data_ptr_->data_map_),
                                                        std::end(devices_data_ptr_->data_map_),
                                                        [&mac_address](const auto &data) {
                                                            return (0 < data.second.count(mac_address));
                                                        }
                                                        );
                    }

                } while (false);

                return status;
            }
         private:
            const std::shared_ptr<BleDevicesData> devices_data_ptr_;
            const std::vector<uint8_t> uuid_tag_;
        };

        auto alert_fn_cb = [&](const AdvertisementBase &device_attr) {
            LOG_I(kLogTag, "Inside alert_fn_cb");
            const BeaconInfo * const beacon_info_ptr = static_cast<const BeaconInfo *>(&device_attr);

            const int rssi = static_cast<int8_t>(beacon_info_ptr->rssi_);
            const int tx_power = static_cast<int8_t>(beacon_info_ptr->tx_power_);

            const std::string &mac_address = beacon_info_ptr->mac_address_;
            const uint64_t steady_now = CurrentSteadyClockSeconds();
            const uint64_t current_time_ms = CurrentSystemClockMilliSeconds();

            // Minor bytes (new): [sequence (XX), age_seconds (YY)]
            const uint8_t seq = beacon_info_ptr->minor_[0];
            const uint8_t age_secs = beacon_info_ptr->minor_[1];
            const uint64_t actual_steady_alert_timestamp = (steady_now > static_cast<uint64_t>(age_secs) ?
                                                    (steady_now - static_cast<uint64_t>(age_secs)) : 0);
            const uint64_t actual_system_alert_timestamp = (current_time_ms > static_cast<uint64_t>(age_secs * 1000) ?
                                                    (current_time_ms - static_cast<uint64_t>(age_secs * 1000)) : 0);

            bool notify_alert = false;

            ButtonIds id = ButtonIds::kButtonIdDriver;
            {
                const std::lock_guard<std::mutex> lock(ble_devices_data_ptr_->data_lock_);
                for (auto & data: ble_devices_data_ptr_->data_map_) {
                    do {
                        auto itr = data.second.find(mac_address);
                        if (std::end(data.second) == itr) {
                            break;
                        }

                        if (0 != itr->second.last_alert_trigger_received_at_) {

                            if ((0 < age_secs) &&
                                (actual_steady_alert_timestamp <= itr->second.last_alert_trigger_received_at_ + kBleButtonPacketInterval)) {
                                break;
                            }

                            // TODO(sunil.s): should kBleAlertTriggerInterval be made configurable?

                            if ((steady_now <= (kBleAlertTriggerInterval + itr->second.last_alert_trigger_received_at_))) {
                                break;
                            }
                        }
                        notify_alert = true;
                        itr->second.last_press_seq_ = seq;
                        itr->second.last_alert_trigger_received_at_ = actual_steady_alert_timestamp;
                        id = (kBleDeviceAssigneesConstantsMap.at(data.first)).button_id_;
                    } while (false);
                }
            }

            if (notify_alert) {

                LOG_C(kLogTag, "Trigger alert for Ble Button id:%d name:%s mac:%s at:%llu aged:%u", static_cast<int>(id),
                      beacon_info_ptr->name_.c_str(), beacon_info_ptr->mac_address_.c_str(), current_time_ms, age_secs);

                LOG_I(kLogTag, "iBeacon msg power: %ddBm, rssi: %ddBm possibly at proximity of: %lfm", tx_power, rssi, beacon_info_ptr->distance_);

                nd::platform_specific::SendUserAlertMessage(static_cast<int>(id),
                                                            actual_system_alert_timestamp,
                                                            beacon_info_ptr->mac_address_,
                                                            kBT_Server_MQ_Name, kNDCentral_Server_MQ_Name, msg_id_);
            }

        };

        std::unique_ptr<UserAlertSpec> alert_spec = std::make_unique<UserAlertSpec>(ble_devices_data_ptr_,
                                                                                    config_data_->uuid_alert_tag_);

        registered_filter_id_map_[FilterKeys::kBleUserAlert] = ble_observer_ptr_->SetFilter(SpecificationType::kBeaconUUID,
                                                                                            std::move(alert_spec),
                                                                                            alert_fn_cb, false, true);

    } while (false);

    return true;
}
#endif

bool BTManager::RegisterAlertFilter() {

    do {
        if (0 != registered_filter_id_map_.count(FilterKeys::kBleUserAlert)) {
            LOG_I(kLogTag, "kBleUserAlert filter is already registered");
            break;
        }

        class UserAlertSpec : virtual public ISpecification {
         public:
            explicit UserAlertSpec(const std::shared_ptr<BleDevicesData> &data_ptr,
                                   const std::vector<uint8_t> &tag):devices_data_ptr_(data_ptr), uuid_tag_(tag) {}
            virtual bool IsSatisfied(const AdvertisementBase &device_attr) override {
                bool status = false;

                do {
                    const BeaconInfo * const beacon_info_ptr = static_cast<const BeaconInfo *>(&device_attr);

                    if (SpecificationType::kBeaconUUID != device_attr.type_) {
                        break;
                    }

                    // Must match UUID
                    if (!std::equal(uuid_tag_.begin(), uuid_tag_.end(), beacon_info_ptr->uuid_)) {
                        break;
                    }

                    if (std::equal(std::begin(beacon_info_ptr->major_), std::end(beacon_info_ptr->major_),
                                   std::begin(nd::device::kDriverUuidMajor))) {

                        const std::lock_guard<std::mutex> lock(devices_data_ptr_->data_lock_);
                        const std::string &mac_address = beacon_info_ptr->mac_address_;

                        status = std::any_of(std::begin(devices_data_ptr_->data_map_),
                                                        std::end(devices_data_ptr_->data_map_),
                                                        [&mac_address](const auto &data) {
                                                            return (0 < data.second.count(mac_address));
                                                        }
                                                        );
                    }

                } while (false);

                return status;
            }
         private:
            const std::shared_ptr<BleDevicesData> devices_data_ptr_;
            const std::vector<uint8_t> uuid_tag_;
        };

        auto alert_fn_cb = [&](const AdvertisementBase &device_attr) {
            const BeaconInfo * const beacon_info_ptr = static_cast<const BeaconInfo *>(&device_attr);

            const int rssi = static_cast<int8_t>(beacon_info_ptr->rssi_);
            const int tx_power = static_cast<int8_t>(beacon_info_ptr->tx_power_);

            const std::string &mac_address = beacon_info_ptr->mac_address_;
            const uint64_t steady_now = CurrentSteadyClockSeconds();
            const uint64_t current_time_ms = CurrentSystemClockMilliSeconds();

            const uint8_t seq = beacon_info_ptr->minor_[0];
            const uint8_t age_secs = beacon_info_ptr->minor_[1]; // Adjusting age to test New Firmware behaviour

            // Raise Critical alert if age exceeds 23 seconds (New Firmware max age value is 23 seconds)
            constexpr uint8_t kMaxAgeValue = 23;
            if (!ble_alert_age_threshold_logged_.load() && age_secs > kMaxAgeValue) {
                LOG_C(kLogTag, "BLE alert packet age %u seconds exceeded threshold of %u seconds for device mac:%s",
                    age_secs, kMaxAgeValue, beacon_info_ptr->mac_address_.c_str());

                // Send error message to service manager
                if (service_obj_ptr_) {
                    if (!service_obj_ptr_->send_err_msg(SM_E_BTFV_BLE_ALERT_PKT_AGE_EXCEEDED,
                                                        static_cast<int>(BleButtonErr::kPacketAgeExceeded),
                                                        "BLE alert packet age exceeded threshold")) {
                        LOG_E(kLogTag, "SM_E_BTFV_BLE_ALERT_PKT_AGE_EXCEEDED send_err_msg failed for BLE packet age threshold");
                    }
                }

                ble_alert_age_threshold_logged_.store(true);
            }

            const uint64_t actual_steady_alert_timestamp = (steady_now > static_cast<uint64_t>(age_secs) ?
                                                    (steady_now - static_cast<uint64_t>(age_secs)) : 0);
            const uint64_t actual_system_alert_timestamp = (current_time_ms > static_cast<uint64_t>(age_secs * 1000) ?
                                                    (current_time_ms - static_cast<uint64_t>(age_secs * 1000)) : 0);

            bool notify_alert = false;
            ButtonIds id = ButtonIds::kButtonIdDriver;

            {
                const std::lock_guard<std::mutex> lock(ble_devices_data_ptr_->data_lock_);
                for (auto & data: ble_devices_data_ptr_->data_map_) {
                    do {
                        auto itr = data.second.find(mac_address);
                        if (std::end(data.second) == itr) {
                            break;
                        }
                        // Check if this is a new sequence or a repeated sequence-0 press
                        bool is_sequence_different = (itr->second.last_press_seq_ != seq);
                        bool is_same_sequence = (itr->second.last_press_seq_ == seq);
                        bool is_sequence_zero = (seq == 0);
                        bool is_repeat_at_zero = is_same_sequence && is_sequence_zero;

                        // Process alert if sequence changed OR if it's a repeated sequence-0
                        if (is_sequence_different || is_repeat_at_zero) {

                            // If not the first alert, check for debouncing
                            if (0 != itr->second.last_alert_trigger_received_at_) {

                                // Determine the appropriate debounce interval
                                uint64_t debounce_interval_seconds = 0;

                                if (is_repeat_at_zero) {
                                    // Shorter interval for Older Firmware behaviour
                                    debounce_interval_seconds = kBleAlertTriggerInterval;
                                } else {
                                    // Longer interval for New Firmware button presses
                                    debounce_interval_seconds = kBleButtonPacketInterval;
                                }

                                // Calculate the next allowed alert time
                                uint64_t next_allowed_alert_time = itr->second.last_alert_trigger_received_at_ + debounce_interval_seconds;

                                // Check if current alert is too soon (debouncing check)
                                if (actual_steady_alert_timestamp < next_allowed_alert_time) {
                                    break;  // Skip this alert - within debounce period
                                }
                            }

                            // Alert passes all checks - proceed to notify
                            notify_alert = true;
                            itr->second.last_press_seq_ = seq;
                            itr->second.last_alert_trigger_received_at_ = actual_steady_alert_timestamp;

                        } else {
                            break;
                        }

                        id = (kBleDeviceAssigneesConstantsMap.at(data.first)).button_id_;

                    } while (false);
                }
            }

            if (notify_alert) {
                LOG_C(kLogTag, "Trigger alert for Ble Button id:%d name:%s mac:%s at:%llu aged:%u", static_cast<int>(id),
                      beacon_info_ptr->name_.c_str(), beacon_info_ptr->mac_address_.c_str(), current_time_ms, age_secs);
                LOG_I(kLogTag, "iBeacon msg power: %ddBm, rssi: %ddBm possibly at proximity of: %lfm", tx_power, rssi,
                      beacon_info_ptr->distance_);

                nd::platform_specific::SendUserAlertMessage(static_cast<int>(id),
                                                            actual_system_alert_timestamp,
                                                            beacon_info_ptr->mac_address_,
                                                            kBT_Server_MQ_Name, kNDCentral_Server_MQ_Name, msg_id_);

                // Report alert info to health stats
                ReportBleAlertToHealthStats(seq, age_secs, mac_address, rssi);
            }
        };

        std::unique_ptr<UserAlertSpec> alert_spec = std::make_unique<UserAlertSpec>(ble_devices_data_ptr_,
                                                                                    config_data_->uuid_alert_tag_);

        registered_filter_id_map_[FilterKeys::kBleUserAlert] = ble_observer_ptr_->SetFilter(SpecificationType::kBeaconUUID,
                                                                                            std::move(alert_spec),
                                                                                            alert_fn_cb, false, true);

    } while (false);

    return true;
}

bool BTManager::RegisterNearbyDevicesFilter() {
    LOG_I(kLogTag, "Inside: %s", __func__);

    do {
        if (0 == config_data_->nearby_devices_max_size_) {
            LOG_I(kLogTag, "nearby_devices_max_size is 0, feature is disabled. Not registering filter.");
            break;
        }

        if (0 != registered_filter_id_map_.count(FilterKeys::kNearbyDevices)) {
            LOG_I(kLogTag, "kNearbyDevices filter is already registered");
            break;
        }

        class NearbyDevicesSpec : virtual public ISpecification {
         public:
            bool IsSatisfied(const AdvertisementBase &device_attr) override {
                bool status = false;
                do {
                    if (device_attr.type_ != SpecificationType::kMacAddress){
                        break;
                    }
                    if (device_attr.mac_address_.empty()){
                        break;
                    }
                    status = true;
                } while (false);
                return status;
            }
        };

        auto nearby_devices_fn_cb = [this](const AdvertisementBase &device_attr) {

            do {
                const std::string &mac_address = device_attr.mac_address_;
                bool device_added = false;

                {
                    std::lock_guard<std::mutex> lock(nearby_devices_data_ptr_->data_lock_);

                    // Check if device already exists
                    if (nearby_devices_data_ptr_->data_set_.count(mac_address) > 0) {
                        LOG_D(kLogTag, "Device %s already in nearby devices set", mac_address.c_str());
                        break;
                    }

                    // Check if set is full before trying to add
                    if (nearby_devices_data_ptr_->data_set_.size() >= config_data_->nearby_devices_max_size_) {
                        LOG_D(kLogTag, "Nearby devices set is full (size: %zu), ignoring device %s",
                            nearby_devices_data_ptr_->data_set_.size(), mac_address.c_str());

                        // Use atomic compare-and-swap to ensure only one message is sent
                        bool expected = false;
                        if (nearby_devices_set_full_sent_.compare_exchange_strong(expected, true)) {
                            LOG_I(kLogTag, "Nearby devices Set full (%zu)",
                                  config_data_->nearby_devices_max_size_);
                            nd::platform_specific::SendInternalEventMessage("BTFV_INTERNAL_EVENT", kBT_Server_MQ_Name, msg_id_,
                                                                static_cast<int32_t>(InternalEvents::kNearbyDevicesSetFull));
                        }
                        break;
                    }

                    // Add new device
                    nearby_devices_data_ptr_->data_set_.insert(mac_address);
                    device_added = true;
                }

                if (device_added) {
                    LOG_I(kLogTag, "Stored nearby device: mac=%s (count=%zu)",
                        mac_address.c_str(),
                        nearby_devices_data_ptr_->data_set_.size());
                }

            } while (false);
        };

        std::unique_ptr<NearbyDevicesSpec> nearby_devices_spec = std::make_unique<NearbyDevicesSpec>();
        registered_filter_id_map_[FilterKeys::kNearbyDevices] =
            ble_observer_ptr_->SetFilter(SpecificationType::kMacAddress,
                                         std::move(nearby_devices_spec),
                                         nearby_devices_fn_cb,
                                         false,
                                         false);
        // Schedule initial nearby devices report using internal retry event
        {
            const uint64_t later = CurrentSteadyClockSeconds() +
                                       kInternalRetryEventsTimeoutMap.at(InternalRetryEvents::kNearbyDevicesReport);
            retry_event_queue_ptr_->data_map_.emplace(later,
                                                        RetryEventData{.type_ = InternalRetryEvents::kNearbyDevicesReport,
                                                                        .data_ = nullptr});
        }

    } while (false);

    return true;
}

void BTManager::ReportDLSessionStatusToHealthStats(bool session_started) {
    LOG_I(kLogTag, "Entered %s", __func__);

    auto const now = CurrentSystemClockMilliSeconds();

    constexpr char kTimestampStr[]      = "ts";
    constexpr char kHashStr[]           = "hash";
    constexpr char kStatusStr[]         = "status";
    constexpr char kUptimeStr[]         = "up";
    constexpr char kIsArrayStr[]        = "isArray";
    constexpr char kHealthInfoStr[]     = "health_info:driver_login:session_info";
    constexpr char kArrayTrueStr[]      = "true";
    constexpr char kTotalAudioPlayed[]  = "total_audio_played";
    constexpr char kTotalAudioSkipped[] = "total_audio_skipped";
    const std::string session_status = session_started ? "Start" : "End";

    json_error_t error{};

    uint32_t audio_count = 0;
    uint32_t skip_count = 0;
    {
        std::lock_guard<std::mutex> lock (driver_association_->session_data_.mutex_);
        audio_count = driver_association_->session_data_.audio_play_count_;
        skip_count = driver_association_->session_data_.audio_skip_count_;
    }

    json_t *pack_root = json_pack_ex(&error, 0, "{s:{s:I, s:s, s:s, s:i, s:i, s:I}, s:s}",
                                         kHealthInfoStr,
                                            kTimestampStr, now,
                                            kHashStr, GetDLScanHash().c_str(),
                                            kStatusStr, session_status.c_str(),
                                            kTotalAudioPlayed, audio_count,
                                            kTotalAudioSkipped, skip_count,
                                            kUptimeStr, CurrentSteadyClockSeconds(),
                                        kIsArrayStr, kArrayTrueStr
                                        );

    if (nullptr != pack_root) {
        char *dump = json_dumps(pack_root, JSON_COMPACT);
        if (nullptr != dump) {
            if (service_obj_ptr_->send_msg_healthstats(dump, strlen(dump))) {
                LOG_I(kLogTag, "Drive Status reported successfully to HealthStats");
                LOG_I(kLogTag, "Data: %s", dump);
            } else {
                LOG_E(kLogTag, "Drive Status failed to be reported to HealthStats, adding to pending");
                std::string health_msg = dump;
                AddToPendingHealthData(std::move(health_msg));
            }
            free(dump);
        } else {
            LOG_E(kLogTag, "%s: json_dumps failed", __func__);
        }

        json_decref(pack_root);
    } else {
        LOG_E(kLogTag, "%s: json_pack failed error: %s", __func__, error.text);
    }
}

void BTManager::ReportDLScanStatusToHealthStats(const std::string &type, bool scan_started, const std::string &info) {
     LOG_I(kLogTag, "Entered %s", __func__);

    auto const now =  CurrentSystemClockMilliSeconds();

    constexpr char kTimestampStr[]   = "ts";
    constexpr char kHashStr[]        = "hash";
    constexpr char kStatusStr[]      = "scan_status";
    constexpr char kTypeStr[]        = "scan_type";
    constexpr char kUptimeStr[]      = "up";
    constexpr char kIsArrayStr[]     = "isArray";
    constexpr char kInfoStr[]        = "info";
    constexpr char kHealthInfoStr[]  = "health_info:driver_login:scan_info";
    constexpr char kArrayTrueStr[]   = "true";

    json_error_t error{};

    json_t *pack_root = json_pack_ex(&error, 0, "{s:{s:I, s:s, s:s, s:s, s:s, s:I}, s:s}",
                                        kHealthInfoStr,
                                            kTimestampStr, now,
                                            kHashStr, GetDLScanHash().c_str(),
                                            kStatusStr, scan_started ? "start" : "end",
                                            kTypeStr, type.c_str(),
                                            kInfoStr, info.c_str(),
                                            kUptimeStr, CurrentSteadyClockSeconds(),
                                        kIsArrayStr, kArrayTrueStr
                                        );

    if (nullptr != pack_root) {
        char *dump = json_dumps(pack_root, JSON_COMPACT);
        if (nullptr != dump) {
            if (service_obj_ptr_->send_msg_healthstats(dump, strlen(dump))) {
                LOG_I(kLogTag, "Scan status Info reported successfully to HealthStats");
                LOG_I(kLogTag, "Data: %s", dump);
            } else {
                LOG_E(kLogTag, "Scan status Info failed to be reported to HealthStats, adding to pending");
                std::string health_msg = dump;
                AddToPendingHealthData(std::move(health_msg));
            }
            free(dump);
        } else {
            LOG_E(kLogTag, "%s: json_dumps failed", __func__);
        }

        json_decref(pack_root);
    } else {
        LOG_E(kLogTag, "%s: json_pack failed error: %s", __func__, error.text);
    }
}

void BTManager::ReportDriveAudioInfoToHealthStats(const std::string &state, const std::string &desc, bool is_played) {
    LOG_I(kLogTag, "Entered %s", __func__);

    auto const now = CurrentSystemClockMilliSeconds();

    constexpr char kTimestampStr[]   = "ts";
    constexpr char kHashStr[]        = "hash";
    constexpr char kStatusStr[]      = "status";
    constexpr char kUptimeStr[]      = "up";
    constexpr char kIsArrayStr[]     = "isArray";
    constexpr char kStateStr[]       = "state";
    constexpr char kDescriptionStr[] = "desc";
    constexpr char kHealthInfoStr[]  = "health_info:driver_login:audio_info";
    constexpr char kArrayTrueStr[]   = "true";

    json_error_t error{};

    json_t *pack_root = json_pack_ex(&error, 0, "{s:{s:I, s:s, s:s, s:s, s:s, s:I}, s:s}",
                                        kHealthInfoStr,
                                            kTimestampStr, now,
                                            kHashStr, GetDLScanHash().c_str(),
                                            kStatusStr, is_played ? "played" : "skipped",
                                            kStateStr, state.c_str(),
                                            kDescriptionStr, desc.c_str(),
                                            kUptimeStr, CurrentSteadyClockSeconds(),
                                        kIsArrayStr, kArrayTrueStr
                                        );

    if (nullptr != pack_root) {
        char *dump = json_dumps(pack_root, JSON_COMPACT);
        if (nullptr != dump) {
            if (service_obj_ptr_->send_msg_healthstats(dump, strlen(dump))) {
                LOG_I(kLogTag, "Drive Audio Info reported successfully to HealthStats");
                LOG_I(kLogTag, "Data: %s", dump);
            } else {
                LOG_E(kLogTag, "Drive Audio Info failed to be reported to HealthStats, adding to pending");
                std::string health_msg = dump;
                AddToPendingHealthData(std::move(health_msg));
            }
            free(dump);
        } else {
            LOG_E(kLogTag, "%s: json_dumps failed", __func__);
        }

        json_decref(pack_root);
    } else {
        LOG_E(kLogTag, "%s: json_pack failed error: %s", __func__, error.text);
    }
}

void BTManager::ReportDriveAssignmentToHealthStats(const std::vector<std::string> &ids, const std::string &source) {
    LOG_I(kLogTag, "Entered %s", __func__);

    auto const now = CurrentSystemClockMilliSeconds();

    constexpr char kTimestampStr[]  = "ts";
    constexpr char kHashStr[]       = "hash";
    constexpr char kSourceStr[]     = "source";
    constexpr char kDriverIdsStr[]  = "ids";
    constexpr char kUptimeStr[]     = "up";
    constexpr char kIsArrayStr[]    = "isArray";
    constexpr char kHealthInfoStr[] = "health_info:driver_login:assignment_info";
    constexpr char kArrayTrueStr[]  = "true";

    std::string concatenated_driver_ids;

    std::for_each(ids.begin(), ids.end(), [&concatenated_driver_ids](const std::string &id) {
                    concatenated_driver_ids.append(id);
                    concatenated_driver_ids.append(",");
                });

    if (!concatenated_driver_ids.empty()) {
        concatenated_driver_ids.pop_back();
    }

    json_error_t error{};

    json_t *pack_root = json_pack_ex(&error, 0, "{s:{s:I, s:s, s:s, s:s, s:I}, s:s}",
                                         kHealthInfoStr,
                                            kTimestampStr, now,
                                            kHashStr, GetDLScanHash().c_str(),
                                            kDriverIdsStr, concatenated_driver_ids.c_str(),
                                            kSourceStr, source.c_str(),
                                            kUptimeStr, CurrentSteadyClockSeconds(),
                                        kIsArrayStr, kArrayTrueStr
                                        );

    if (nullptr != pack_root) {
        char *dump = json_dumps(pack_root, JSON_COMPACT);
        if (nullptr != dump) {
            if (service_obj_ptr_->send_msg_healthstats(dump, strlen(dump))) {
                LOG_I(kLogTag, "Drive assignment reported successfully to HealthStats");
                LOG_I(kLogTag, "Data: %s", dump);
            } else {
                LOG_E(kLogTag, "Drive assignment failed to be reported to HealthStats, adding to pending");
                std::string health_msg = dump;
                AddToPendingHealthData(std::move(health_msg));
            }
            free(dump);
        } else {
            LOG_E(kLogTag, "%s: json_dumps failed", __func__);
        }

        json_decref(pack_root);
    } else {
        LOG_E(kLogTag, "%s: json_pack failed error: %s", __func__, error.text);
    }
}

void BTManager::ReportScannedLoginStatusToHealthStats(const std::vector<std::string> &scanned_logins_status) {
    LOG_I(kLogTag, "Entered %s", __func__);

    auto const now = CurrentSystemClockMilliSeconds();

    constexpr char kTimestampStr[]  = "ts";
    constexpr char kHashStr[]       = "hash";
    constexpr char kLoginsStr[]     = "logins";
    constexpr char kUptimeStr[]     = "up";
    constexpr char kIsArrayStr[]    = "isArray";
    constexpr char kHealthInfoStr[] = "health_info:driver_login:logins_info";
    constexpr char kArrayTrueStr[]  = "true";

    json_t *login_array = json_array();
    if (nullptr != login_array) {
        for (const auto &status : scanned_logins_status) {
            json_array_append_new(login_array, json_string(status.c_str()));
        }

        json_error_t error{};

        json_t *pack_root = json_pack_ex(&error, 0, "{s:{s:I, s:s, s:o, s:I}, s:s}",
                                            kHealthInfoStr,
                                                kTimestampStr, now,
                                                kHashStr, GetDLScanHash().c_str(),
                                                kLoginsStr, login_array,
                                                kUptimeStr, CurrentSteadyClockSeconds(),
                                            kIsArrayStr, kArrayTrueStr
                                            );

        if (nullptr != pack_root) {
            char *dump = json_dumps(pack_root, JSON_COMPACT);
            if (nullptr != dump) {
                if (service_obj_ptr_->send_msg_healthstats(dump, strlen(dump))) {
                    LOG_I(kLogTag, "Scanned logins reported successfully to HealthStats");
                    LOG_I(kLogTag, "Data: %s", dump);
                } else {
                    LOG_E(kLogTag, "Scanned logins failed to be reported to HealthStats, adding to pending");
                    std::string health_msg = dump;
                    AddToPendingHealthData(std::move(health_msg));
                }
                free(dump);
            } else {
                LOG_E(kLogTag, "ReportScannedLoginStatusToHealthStats: json_dumps failed");
            }

            json_decref(pack_root);
        } else {
            LOG_E(kLogTag, "ReportScannedLoginStatusToHealthStats: json_pack failed error: %s", error.text);
            json_decref(login_array);
        }
    } else {
        LOG_E(kLogTag, "ReportScannedLoginStatusToHealthStats: json_array failed");
    }
}

void BTManager::ReportBleDeviceConfigToHealthStats(const std::string &mac, const std::string &label) {
    LOG_I(kLogTag, "Entered %s", __func__);

    auto const now = CurrentSystemClockMilliSeconds();

    constexpr char kTimestampStr[]  = "ts";
    constexpr char kMacStr[]        = "mac";
    constexpr char kLabelStr[]      = "label";
    constexpr char kUptimeStr[]     = "up";
    constexpr char kIsArrayStr[]    = "isArray";
    constexpr char kHealthInfoStr[] = "health_info:peripherals:alert_beacon:config";
    constexpr char kArrayTrueStr[]  = "true";

    json_error_t error{};

    json_t *pack_root = json_pack_ex(&error, 0, "{s:{s:I, s:s, s:s, s:I}, s:s}",
                                         kHealthInfoStr,
                                            kTimestampStr, now,
                                            kMacStr, mac.c_str(),
                                            kLabelStr, label.c_str(),
                                            kUptimeStr, CurrentSteadyClockSeconds(),
                                        kIsArrayStr, kArrayTrueStr
                                        );

    if (nullptr != pack_root) {
        char *dump = json_dumps(pack_root, JSON_COMPACT);
        if (nullptr != dump) {
            if (service_obj_ptr_->send_msg_healthstats(dump, strlen(dump))) {
                LOG_I(kLogTag, "BLE Config Data reported successfully to HealthStats");
                LOG_I(kLogTag, "Data: %s", dump);
            } else {
                LOG_E(kLogTag, "BLE Config Data failed to be reported to HealthStats, adding to pending");
                std::string health_msg = dump;
                AddToPendingHealthData(std::move(health_msg));
            }
            free(dump);
        } else {
            LOG_E(kLogTag, "ReportBleDeviceConfigToHealthStats: json_dumps failed");
        }

        json_decref(pack_root);
    } else {
        LOG_E(kLogTag, "ReportBleDeviceConfigToHealthStats: json_pack failed error: %s", error.text);
    }
}

void BTManager::RegisterSpeedEventRegRetry() {
    const uint64_t later = std::chrono::duration_cast<std::chrono::seconds>
                            (std::chrono::steady_clock::now().time_since_epoch()).count() +
                            kInternalRetryEventsTimeoutMap.at(InternalRetryEvents::kSpeedServiceRegistration);
    const std::lock_guard<std::mutex> lock(retry_event_queue_ptr_->data_lock_);
    retry_event_queue_ptr_->data_map_.emplace(later,
                                                RetryEventData{.type_ = InternalRetryEvents::kSpeedServiceRegistration,
                                                               .data_ = nullptr});
}

void BTManager::RegisterBtStackReload() {
    if (0 < config_data_->reload_stack_interval_minutes_) {
        const uint64_t later = std::chrono::duration_cast<std::chrono::seconds>
                               (std::chrono::steady_clock::now().time_since_epoch()).count() +
                               config_data_->reload_stack_interval_minutes_ * 60;
        const std::lock_guard<std::mutex> lock(retry_event_queue_ptr_->data_lock_);
        retry_event_queue_ptr_->data_map_.emplace(later,
                                                    RetryEventData{.type_ = InternalRetryEvents::kBtStackReload,
                                                                   .data_ = nullptr});
    }
}

void BTManager::RegisterStartQrMsgRetry() {
    LOG_I(kLogTag, "Inside: %s", __func__);
    const uint64_t later = CurrentSteadyClockSeconds() +
                           kInternalRetryEventsTimeoutMap.at(InternalRetryEvents::kStartQrMsg);
    const std::lock_guard<std::mutex> lock(retry_event_queue_ptr_->data_lock_);
    retry_event_queue_ptr_->data_map_.emplace(later,
                                              RetryEventData{.type_ = InternalRetryEvents::kStartQrMsg,
                                                             .data_ = nullptr});
}

void BTManager::RegisterForRecoveryTimeout() {
    LOG_I(kLogTag, "Inside: %s", __func__);

    recovery_window_duration_expired_ = false;
    const uint64_t later = CurrentSteadyClockSeconds() + config_data_->adsm_recovery_window_duration_;
    const std::lock_guard<std::mutex> lock(retry_event_queue_ptr_->data_lock_);
    retry_event_queue_ptr_->data_map_.emplace(later,
                                              RetryEventData{.type_ = InternalRetryEvents::kRecoveryWindowTimer,
                                                             .data_ = nullptr});
    LOG_I(kLogTag, "Recovery window timer registered for %llu seconds", config_data_->adsm_recovery_window_duration_);
}

void BTManager::RegisterForAdsmWatchdogTimer() {
    LOG_I(kLogTag, "Inside: %s", __func__);

    if (adsm_state_mgr_ && !is_adsm_watchdog_registered_) {
        const uint64_t later = CurrentSteadyClockSeconds() +
                                kInternalRetryEventsTimeoutMap.at(InternalRetryEvents::kAdsmWatchdogTimer);
        const std::lock_guard<std::mutex> lock(retry_event_queue_ptr_->data_lock_);
        retry_event_queue_ptr_->data_map_.emplace(later,
                                                    RetryEventData{.type_ = InternalRetryEvents::kAdsmWatchdogTimer,
                                                                    .data_ = nullptr});
        is_adsm_watchdog_registered_ = true;
        LOG_I(kLogTag, "ADSM watchdog timer registered");
    }
}

void BTManager::HandleAdsmWatchdogTimeout() {
    LOG_I(kLogTag, "Inside: %s", __func__);

    if (adsm_state_mgr_) {
        if (adsm_state_mgr_->SaveState()) {
            LOG_I(kLogTag, "ADSM state saved successfully inside %s", __func__);
        } else {
            LOG_E(kLogTag, "ADSM state save failed inside %s", __func__);
        }
    }
}

bool BTManager::TrySendStartQr() {
    LOG_I(kLogTag, "Inside: %s", __func__);
    bool status = false;
    is_qr_scan_required_ = true;

    if ((is_qr_scan_required_) && (!is_qr_scan_active_)) {
        status = nd::platform_specific::SendStartQRScanMsg(config_data_->qr_login_tags_,
                                                           kBT_Server_MQ_Name,
                                                           kNDCentral_Server_MQ_Name,
                                                           msg_id_);
        if (status) {
            if (!file_touch(GetQrScanStartIndicatorPath())) {
                LOG_E(kLogTag, "Failed to create %s in [%s:%d]", GetQrScanStartIndicatorPath().c_str(), __func__, __LINE__);
            }
        }
    } else {
        status = true;
    }

    return status;
}

bool BTManager::ReloadBtStack() {
    bool status = false;
    LOG_I(kLogTag, "Inside: %s", __func__);

    do {

        if (!bt_interface_ptr_->IsEnabled()) {
            LOG_I(kLogTag, "BT is already disabled, no need to reload");
            break;
        }

        if (IsFilterSet(FilterKeys::kDriverLoginService)) {
            bt_interface_ptr_->StopBeaconAdvertising();
        }

        bt_interface_ptr_->LeScanOff();

        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        if (!bt_interface_ptr_->Disable()) {
            LOG_E(kLogTag, "Failed to disable BT");
            if (!service_obj_ptr_->send_err_msg(SM_E_BTFV_BT_STACK_FAIL, static_cast<int>(BtStackErr::kDisableFailed),
                                                "BT module disable failed")) {
                LOG_E(kLogTag, "SM_E_BTFV_BT_STACK_FAIL send_err_msg failed");
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        if (bt_interface_ptr_->Enable()) {
            LOG_I(kLogTag, "BT Enabled successfully");

            if (bt_interface_ptr_->LeScanOn()) {
                LOG_I(kLogTag, "LeScanOn success");
            } else {
                LOG_E(kLogTag, "LeScanOn failed");
            }

            status = true;
        } else {
            LOG_E(kLogTag, "BT Enable failed");
            if (!service_obj_ptr_->send_err_msg(SM_E_BTFV_BT_STACK_FAIL, static_cast<int>(BtStackErr::kEnableFailed),
                                                "BT module enable failed")) {
                LOG_E(kLogTag, "SM_E_BTFV_BT_STACK_FAIL send_err_msg failed");
            }

            // Exit service as we cannot reconnect with BT stack
            is_exit_main_loop_set_ = true;
        }

    } while (false);

    return status;
}

void BTManager::RegisterPeriodicKACheck() {
    const uint64_t later = std::chrono::duration_cast<std::chrono::seconds>
                                (std::chrono::steady_clock::now().time_since_epoch()).count() +
                                kInternalRetryEventsTimeoutMap.at(InternalRetryEvents::kBleBeaconKACheck);
    const std::lock_guard<std::mutex> lock(retry_event_queue_ptr_->data_lock_);
    retry_event_queue_ptr_->data_map_.emplace(later,
                                              RetryEventData{.type_ = InternalRetryEvents::kBleBeaconKACheck,
                                                             .data_ = nullptr});
}

void BTManager::RegisterInactiveBeaconCheck() {
    const uint64_t later = std::chrono::duration_cast<std::chrono::seconds>
                                (std::chrono::steady_clock::now().time_since_epoch()).count() +
                                kInternalRetryEventsTimeoutMap.at(InternalRetryEvents::kInactiveBleBeaconCheck);
    const std::lock_guard<std::mutex> lock(retry_event_queue_ptr_->data_lock_);
    retry_event_queue_ptr_->data_map_.emplace(later,
                                              RetryEventData{.type_ = InternalRetryEvents::kInactiveBleBeaconCheck,
                                                             .data_ = nullptr});
}

void BTManager::RegisterHealthDataRetry() {
    const uint64_t later = std::chrono::duration_cast<std::chrono::seconds>
                                (std::chrono::steady_clock::now().time_since_epoch()).count() +
                                kInternalRetryEventsTimeoutMap.at(InternalRetryEvents::kRetryHealthMsgs);
    const std::lock_guard<std::mutex> lock(retry_event_queue_ptr_->data_lock_);
    retry_event_queue_ptr_->data_map_.emplace(later,
                                              RetryEventData{.type_ = InternalRetryEvents::kRetryHealthMsgs,
                                                             .data_ = nullptr});
}

bool BTManager::RetryHealthData() {
    LOG_I(kLogTag, "Entered %s", __func__);

    bool status = true;

    for (auto msg_itr = failed_health_msgs_.begin(); msg_itr != failed_health_msgs_.end();) {
        if (service_obj_ptr_->send_msg_healthstats(const_cast<char *>((*msg_itr).c_str()),
                                                   static_cast<int>((*msg_itr).size()))) {
            LOG_I(kLogTag, "Retried: %s", (*msg_itr).c_str());
            msg_itr = failed_health_msgs_.erase(msg_itr);
            constexpr uint32_t kMaxDelayBetweenHealthMsgs = 50; //in ms
            std::this_thread::sleep_for(std::chrono::milliseconds(kMaxDelayBetweenHealthMsgs));
        } else {
            LOG_E(kLogTag, "Failed to send data on retry, will attempt again later");
            status = false;
            break;
        }
    }
    return status;
}

void BTManager::AddToPendingHealthData(std::string &&health_msg) {
    LOG_I(kLogTag, "Entered %s", __func__);

    // Register only if it the vector is empty
    if (failed_health_msgs_.empty()) {
        RegisterHealthDataRetry();
    }
    failed_health_msgs_.emplace_back(health_msg);
}


void BTManager::CheckKAReloadStack() {
    LOG_I(kLogTag, "Entered %s", __func__);

    if (bt_interface_ptr_->IsEnabled()) {

        const uint64_t steady_now = CurrentSteadyClockSeconds();

        bool is_stack_reloaded = false;

        const std::lock_guard<std::mutex> lock(ble_devices_data_ptr_->data_lock_);

        for (const auto &assignee_data : ble_devices_data_ptr_->data_map_) {
            for (const auto &associated_data : assignee_data.second) {
                if (steady_now > associated_data.second.last_keep_alive_received_at_ + kBleBatteryHealthUpdateTimeout) {
                    LOG_E(kLogTag, "BLE KA missed for mac: %s, do stack reload", associated_data.first.c_str());
                    ReloadBtStack();
                    is_stack_reloaded = true;
                    break;
                }
            }
            if (is_stack_reloaded) {
                break;
            }
        }
    }
}

void BTManager::CheckAndReportInactiveBeacons() {
    LOG_I(kLogTag, "Entered %s", __func__);

    if (bt_interface_ptr_->IsEnabled()) {

        const uint64_t steady_now = CurrentSteadyClockSeconds();

        const std::lock_guard<std::mutex> lock(ble_devices_data_ptr_->data_lock_);

        for (const auto &assignee_data : ble_devices_data_ptr_->data_map_) {
            for (const auto &associated_data : assignee_data.second) {
                if (steady_now > associated_data.second.last_keep_alive_received_at_ + config_data_->keep_alive_miss_interval_) {

                    const std::string err_msg = "BLE Button for: " + kBleDeviceAssigneesConstantsMap.at(assignee_data.first).label_ +
                                                " with mac: " + associated_data.first +
                                                " has not sent keep alive since " + std::to_string(steady_now - associated_data.second.last_keep_alive_received_at_) + " secs";
                    LOG_C(kLogTag, err_msg.c_str());

                    if (!service_obj_ptr_->send_err_msg(SM_E_BTFV_BLE_BUTTON_KA_MISSING, static_cast<int>(BeaconTypes::kAlertBeacon),
                                                        err_msg.c_str())) {
                        LOG_E(kLogTag, "BLE KA miss Alert failed to be sent");
                    } else {
                        LOG_I(kLogTag, "BLE KA miss Alert sent successfully");
                    }
                }
            }
        }
    }
}

void BTManager::ReportBatteryToHealthStats(uint32_t voltage_perc, uint64_t tstamp, const std::string &mac,
                                           const std::string &label, const int32_t rssi) {
    LOG_I(kLogTag, "Entered %s", __func__);

    auto const now = CurrentSystemClockMilliSeconds();

    constexpr char kTimestampStr[]  = "ts";
    constexpr char kMacStr[]        = "mac";
    constexpr char kLabelStr[]      = "label";
    constexpr char kUptimeStr[]     = "up";
    constexpr char kIsArrayStr[]    = "isArray";
    constexpr char kHealthInfoStr[] = "health_info:peripherals:alert_beacon:health";
    constexpr char kBatPercStr[]    = "bat_perc";
    constexpr char kAtStr[]         = "at";
    constexpr char kArrayTrueStr[]  = "true";
    constexpr char kRssiStr[]       = "rssi";

    json_error_t error{};

    json_t *pack_root = json_pack_ex(&error, 0, "{s:{s:I, s:s, s:s, s:i, s:I, s:i, s:I}, s:s}",
                                         kHealthInfoStr,
                                            kTimestampStr, now,
                                            kMacStr, mac.c_str(),
                                            kLabelStr, label.c_str(),
                                            kBatPercStr, voltage_perc,
                                            kAtStr, tstamp * 1000, // convert to milliseconds
                                            kRssiStr, rssi,
                                            kUptimeStr, CurrentSteadyClockSeconds(),
                                        kIsArrayStr, kArrayTrueStr
                                        );

    if (nullptr != pack_root) {
        char *dump = json_dumps(pack_root, JSON_COMPACT);
        if (nullptr != dump) {
            if (service_obj_ptr_->send_msg_healthstats(dump, strlen(dump))) {
                LOG_I(kLogTag, "BLE Health Data reported successfully to HealthStats");
                LOG_I(kLogTag, "Data: %s", dump);
            } else {
                LOG_E(kLogTag, "BLE Health Data failed to be reported to HealthStats, adding to pending");
                std::string health_msg = dump;
                AddToPendingHealthData(std::move(health_msg));
            }
            free(dump);
        } else {
            LOG_E(kLogTag, "ReportBatteryToHealthStats: json_dumps failed");
        }

        json_decref(pack_root);
    } else {
        LOG_E(kLogTag, "ReportBatteryToHealthStats: json_pack failed error: %s", error.text);
    }

    if (config_data_->ble_low_battery_threshold_ >= voltage_perc) {
        static bool is_low_battery_alert_sent = false;
        if (!is_low_battery_alert_sent) {
            const std::string err_msg = "Alert beacon charge is: " + std::to_string(voltage_perc) +
                                    "% for \"" + label + "\" with mac: " + mac;
            LOG_C(kLogTag, "BLE Battery level is below threshold, sending alert");
            if (!service_obj_ptr_->send_err_msg(SM_E_BTFV_BLE_BUTTON_LOW_BATTERY, static_cast<int>(BeaconTypes::kAlertBeacon),
                                                err_msg.c_str())) {
                LOG_E(kLogTag, "BLE Low Battery Alert failed to be sent");
            } else {
                LOG_I(kLogTag, "BLE Low Battery Alert sent successfully");
                is_low_battery_alert_sent = true;
            }
        }
    }
}

bool BTManager::RegisterBatteryStatusFilter() {
    LOG_I(kLogTag, "Inside: %s", __func__);

    do {
        if (0 != registered_filter_id_map_.count(FilterKeys::kBleBatteryStatus)) {
            LOG_I(kLogTag, "kBleBatteryStatus filter is already registered");
            break;
        }

        // Registering only driver beacon for now

        if (kBleMinewVendorString == config_data_->ble_vendor_name_) {

            class MinewBLEBatterySpec : virtual public ISpecification {

             public:
                explicit MinewBLEBatterySpec(const std::shared_ptr<BleDevicesData> &data_ptr,
                                            const std::vector<uint8_t> &id):devices_data_ptr_(data_ptr),
                                                                            variant_id_(id){}
                virtual bool IsSatisfied(const AdvertisementBase &device_attr) override {
                    bool status = false;

                    do {

                        const ServiceInfo * const serv_info_ptr = static_cast<const ServiceInfo *>(&device_attr);

                        if (SpecificationType::kServiceID != serv_info_ptr->type_) {
                            break;
                        }

                        if (!((3 <= (serv_info_ptr->service_data_).size()) && (1 < serv_info_ptr->service_id_.size()))) {
                            break;
                        }

                        if (battery_packet_identifier_ != serv_info_ptr->service_data_.at(0)) {
                            break;
                        }

                        if (!(variant_id_.at(0) == serv_info_ptr->service_id_.at(0)) &&
                            (variant_id_.at(1) == serv_info_ptr->service_id_.at(1))) {
                            break;
                        }

                        {
                            const std::lock_guard<std::mutex> lock(devices_data_ptr_->data_lock_);
                            const std::string &mac_address = serv_info_ptr->mac_address_;

                            status = std::any_of(std::begin(devices_data_ptr_->data_map_),
                                                 std::end(devices_data_ptr_->data_map_),
                                                 [&mac_address](const auto &data) {
                                                     return (0 < data.second.count(mac_address));
                                                 }
                                                 );
                        }

                    } while (false);

                    return status;
                }

             private:
                const uint8_t battery_packet_identifier_ = 0xA1;
                const std::shared_ptr<BleDevicesData> devices_data_ptr_;
                const std::vector<uint8_t> variant_id_;
            };

            auto battery_fn_cb = [this](const AdvertisementBase &device_attr) {
                const ServiceInfo * const serv_info_ptr = static_cast<const ServiceInfo *>(&device_attr);
                const std::string mac_address = serv_info_ptr->mac_address_;
                const int32_t rssi = static_cast<int8_t>(serv_info_ptr->rssi_);
                const uint32_t battery_percentage = serv_info_ptr->service_data_.at(2);
                const uint64_t now = CurrentSystemClockSeconds();
                const uint64_t steady_now = CurrentSteadyClockSeconds();
                bool notify_health = false;
                std::string label;
                {
                    const std::lock_guard<std::mutex> lock(ble_devices_data_ptr_->data_lock_);
                    for (auto & data: ble_devices_data_ptr_->data_map_) {

                        auto itr = data.second.find(mac_address);

                        if (std::end(data.second) != itr) {
                            itr->second.battery_voltage_ = battery_percentage;

                            label = kBleDeviceAssigneesConstantsMap.at(data.first).label_;
                            LOG_I(kLogTag, "Recv'd battery percentage for %s: %u% @ %llu"
                                            "after %llu secs of previous report "
                                            "; rssi of %ddBm ", itr->first.c_str(),
                                    itr->second.battery_voltage_, now,
                                    (steady_now - itr->second.last_keep_alive_received_at_), rssi);

                            if (steady_now >= (kBleBatteryHealthUpdateInterval + itr->second.last_keep_alive_received_at_)) {
                                notify_health = true;
                            }

                            itr->second.last_keep_alive_received_at_ = steady_now;
                            break;
                        }
                    }
                }

                if (notify_health) {
                    ReportBatteryToHealthStats(battery_percentage, now, mac_address, label, rssi);
                }
            };

            const auto battery_id_vec_variant = Translator::HexStringToBytes("FEE1");

            std::unique_ptr<MinewBLEBatterySpec> battery_spec = std::make_unique<MinewBLEBatterySpec>(ble_devices_data_ptr_,
                                                                                                    battery_id_vec_variant);

            registered_filter_id_map_[FilterKeys::kBleBatteryStatus] = ble_observer_ptr_->SetFilter(SpecificationType::kServiceID,
                                                                                                    std::move(battery_spec),
                                                                                                    battery_fn_cb, false, true);

        } else {

            // NOTE: Older Moko Spec

            // class MokoBLEBatterySpec : virtual public ISpecification {

            //  public:
            //     explicit MokoBLEBatterySpec(const std::shared_ptr<BleDevicesData> &data_ptr,
            //                                 const std::vector<uint8_t> &id_1,
            //                                 const std::vector<uint8_t> &id_2):devices_data_ptr_(data_ptr),
            //                                                                 variant_1_id_(id_1),
            //                                                                 variant_2_id_(id_2){}
            //     virtual bool IsSatisfied(const std::unique_ptr<AdvertisementBase> &device_attr_ptr) override {
            //         bool status = false;

            //         do {

            //             if (!device_attr_ptr) {
            //                 break;
            //             }

            //             const ServiceInfo * const serv_info_ptr = static_cast<ServiceInfo *>(device_attr_ptr.get());

            //             if (!serv_info_ptr) {
            //                 break;
            //             }

            //             if (SpecificationType::kServiceID != serv_info_ptr->type_) {
            //                 break;
            //             }

            //             if (!((4 < (serv_info_ptr->service_data_).size()) && (1 < serv_info_ptr->service_id_.size()))) {
            //                 break;
            //             }

            //             if (battery_packet_identifier_ != serv_info_ptr->service_data_.at(0)) {
            //                 break;
            //             }

            //             if (!(((variant_1_id_.at(0) == serv_info_ptr->service_id_.at(0)) &&
            //                 (variant_1_id_.at(1) == serv_info_ptr->service_id_.at(1))) ||
            //                 ((variant_2_id_.at(0) == serv_info_ptr->service_id_.at(0)) &&
            //                 (variant_2_id_.at(1) == serv_info_ptr->service_id_.at(1))))) {
            //                 break;
            //             }

            //             {
            //                 const std::lock_guard<std::mutex> lock(devices_data_ptr_->data_lock_);
            //                 const std::string &mac_address = serv_info_ptr->mac_address_;

            //                 status = std::any_of(std::begin(devices_data_ptr_->data_map_),
            //                                      std::end(devices_data_ptr_->data_map_),
            //                                      [&mac_address](const auto &data) {
            //                                          return (0 < data.second.count(mac_address));
            //                                      }
            //                                      );
            //             }

            //         } while (false);

            //         return status;
            //     }

            //  private:
            //     const uint8_t battery_packet_identifier_ = 0x40;
            //     const std::shared_ptr<BleDevicesData> devices_data_ptr_;
            //     const std::vector<uint8_t> variant_1_id_;
            //     const std::vector<uint8_t> variant_2_id_;
            // };

            // auto battery_fn_cb = [this](const std::unique_ptr<AdvertisementBase> &client) {
            //     const ServiceInfo * const serv_info_ptr = static_cast<ServiceInfo *>(client.get());
            //     if (serv_info_ptr) {
            //         const std::string mac_address = serv_info_ptr->mac_address_;
            //         const int32_t rssi = static_cast<int8_t>(serv_info_ptr->rssi_);
            //         const uint32_t voltage = (serv_info_ptr->service_data_.at(3) << 8) | (serv_info_ptr->service_data_.at(4));
            //         const uint64_t now = CurrentSystemClockSeconds();
            //         bool notify_health = false;
            //         std::string label;
            //         {
            //             const std::lock_guard<std::mutex> lock(ble_devices_data_ptr_->data_lock_);
            //             for (auto & data: ble_devices_data_ptr_->data_map_) {

            //                 auto itr = data.second.find(mac_address);

            //                 if (std::end(data.second) != itr) {
            //                     itr->second.battery_voltage_ = voltage;
            //                     label = kBleDeviceAssigneesConstantsMap.at(data.first).label_;
            //                     LOG_I(kLogTag, "Recv'd battery voltage for %s: %umV @ %llu with rssi of %ddBm "
            //                                    "after %llu secs of previous report ", itr->first.c_str(),
            //                           itr->second.battery_voltage_, now, rssi,
            //                           (now - itr->second.last_keep_alive_received_at_));
            //                     itr->second.last_keep_alive_received_at_ = now;
            //                     notify_health = true;
            //                     break;
            //                 }
            //             }
            //         }

            //         if (notify_health) {
            //             const uint32_t battery_percentage = ((voltage * 100) / config_data_->ble_battery_capacity_);
            //             ReportBatteryToHealthStats(battery_percentage, now, mac_address, label);
            //         }
            //     }
            // };

            // const auto battery_id_vec_variant_1 = Translator::HexStringToBytes("FEAB");
            // const auto battery_id_vec_variant_2 = Translator::HexStringToBytes("FEAC");

            // std::unique_ptr<MokoBLEBatterySpec> battery_spec = std::make_unique<MokoBLEBatterySpec>(ble_devices_data_ptr_,
            //                                                                                 battery_id_vec_variant_1,
            //                                                                                 battery_id_vec_variant_2);

        class MokoBLEBatterySpec : virtual public ISpecification {
         public:
            explicit MokoBLEBatterySpec(const std::shared_ptr<BleDevicesData> &data_ptr,
                                        const std::vector<uint8_t> &id,
                                        const std::shared_ptr<NearbyAlertBeaconData> &beacon_data_ptr,
                                        const uint64_t max_beacon_size):
                devices_data_ptr_(data_ptr),
                variant_id_(id),
                nearby_alert_beacon_data_ptr_(beacon_data_ptr),
                max_alert_beacon_map_size_(max_beacon_size){}

            virtual bool IsSatisfied(const AdvertisementBase &device_attr) override {
                bool is_required_device = false;

                do {
                    const ServiceInfo * const serv_info_ptr = static_cast<const ServiceInfo *>(&device_attr);

                    if (SpecificationType::kServiceID != serv_info_ptr->type_) {
                        break;
                    }

                    if (!((1 < (serv_info_ptr->service_data_).size()) && (1 < serv_info_ptr->service_id_.size()))) {
                        break;
                    }

                    if (!((variant_id_.at(0) == serv_info_ptr->service_id_.at(0)) &&
                        (variant_id_.at(1) == serv_info_ptr->service_id_.at(1)))) {
                        break;
                    }

                    {
                        const std::lock_guard<std::mutex> lock(devices_data_ptr_->data_lock_);
                        const std::string &mac_address = serv_info_ptr->mac_address_;

                        is_required_device = std::any_of(std::begin(devices_data_ptr_->data_map_),
                                            std::end(devices_data_ptr_->data_map_),
                                            [&mac_address](const auto &data) {
                                                return (0 < data.second.count(mac_address));
                                            });
                    }

                    if (!is_required_device) {
                        std::lock_guard<std::mutex> lock(nearby_alert_beacon_data_ptr_->data_lock_);
                        const std::string &mac_address = serv_info_ptr->mac_address_;

                        if ((nearby_alert_beacon_data_ptr_->data_map_.count(mac_address) > 0) ||
                            (nearby_alert_beacon_data_ptr_->data_map_.size() >= max_alert_beacon_map_size_)) {
                            LOG_D(kLogTag, "NearbyAlert BeaconMap %s already in map, or map is full", mac_address.c_str());
                        } else {
                            is_required_device = true;
                        }
                    }

                } while (false);

                return is_required_device;
            }

            private:
                const std::shared_ptr<BleDevicesData> devices_data_ptr_;
                const std::vector<uint8_t> variant_id_;
                const std::shared_ptr<NearbyAlertBeaconData> nearby_alert_beacon_data_ptr_;
                const uint64_t max_alert_beacon_map_size_;
            };

            auto battery_fn_cb = [this](const AdvertisementBase &device_attr) {
                do {

                    const ServiceInfo * const serv_info_ptr = static_cast<const ServiceInfo *>(&device_attr);

                    // Validate service data
                    if (serv_info_ptr->service_data_.empty()) {
                        LOG_W(kLogTag, "Empty service data received");
                        break;
                    }

                    const std::string &mac_address = serv_info_ptr->mac_address_;

                    const int32_t rssi = static_cast<int8_t>(serv_info_ptr->rssi_);
                    const uint32_t battery_percentage = serv_info_ptr->service_data_.at(0);
                    const uint64_t now = CurrentSystemClockSeconds();
                    const uint64_t steady_now = CurrentSteadyClockSeconds();

                    // Read speed from file
                    float vehicle_curr_speed = 0.0f;

                    if (file_is_present(kSpeedInfoFile)) {
                        std::ifstream speed_file;
                        speed_file.open(kSpeedInfoFile);
                        if (speed_file.is_open()) {
                            speed_file >> vehicle_curr_speed;
                            speed_file.close();
                            LOG_I(kLogTag, "Speed Read From File: %.2f", vehicle_curr_speed);
                        } else {
                            LOG_W(kLogTag, "Failed to open %s", kSpeedInfoFile);
                        }
                    } else {
                        LOG_W(kLogTag, "File %s does not exist", kSpeedInfoFile);
                    }

                    bool notify_health = false;
                    bool is_nearby_battery_beacon_device = false;
                    std::string label;

                    // Check if device is paired
                    {
                        bool is_paired_device_updated = false;
                        const std::lock_guard<std::mutex> lock(ble_devices_data_ptr_->data_lock_);

                        for (auto & data: ble_devices_data_ptr_->data_map_) {
                            auto itr = data.second.find(mac_address);

                            if (std::end(data.second) != itr) {
                                // Paired device found - update its data
                                itr->second.battery_voltage_ = battery_percentage;
                                label = kBleDeviceAssigneesConstantsMap.at(data.first).label_;

                                LOG_I(kLogTag, "Battery health recv'd for %s @ %llu is %u "
                                            "after %llu secs of previous report; RSSI: %ddBm",
                                    itr->first.c_str(), now, itr->second.battery_voltage_,
                                    (steady_now - itr->second.last_keep_alive_received_at_), rssi);

                                if ((steady_now >= (kBleBatteryHealthUpdateInterval + itr->second.last_keep_alive_received_at_)) ||
                                    (0 == itr->second.last_keep_alive_received_at_)) {
                                    notify_health = true;
                                }

                                itr->second.last_keep_alive_received_at_ = steady_now;
                                is_paired_device_updated = true;
                                break;
                            }
                        }

                        if (!is_paired_device_updated) {
                            is_nearby_battery_beacon_device = true;
                        }
                    }

                    // Handle unpaired device
                    if (is_nearby_battery_beacon_device) {

                        {
                            std::lock_guard<std::mutex> lock(nearby_alert_beacon_data_ptr_->data_lock_);

                            // Check if device already exists
                            if (nearby_alert_beacon_data_ptr_->data_map_.count(mac_address) > 0) {
                                LOG_D(kLogTag, "Nearby alert beacon %s already in map", mac_address.c_str());
                                break;
                            }

                            // Check if map is full
                            if (nearby_alert_beacon_data_ptr_->data_map_.size() >= config_data_->nearby_alert_beacon_max_size_) {
                                LOG_W(kLogTag, "Nearby alert beacon map is full (size: %zu)",
                                    nearby_alert_beacon_data_ptr_->data_map_.size());
                            } else {
                                // Add new device with speed from file
                                nearby_alert_beacon_data_ptr_->data_map_[mac_address] = {battery_percentage,
                                                                                         static_cast<int32_t>(vehicle_curr_speed)};

                                LOG_I(kLogTag, "NearbyAlertBeaconMap INSERT: mac=%s bat=%u speed=%.2f size=%zu",
                                    mac_address.c_str(), battery_percentage, vehicle_curr_speed,
                                    nearby_alert_beacon_data_ptr_->data_map_.size());
                            }
                        }
                    }

                    // Report health data for known devices
                    if (notify_health) {
                        ReportBatteryToHealthStats(battery_percentage, now, mac_address, label, rssi);
                    }

                } while (false);
            };

            const auto battery_id_vec_variant = Translator::HexStringToBytes("EB04");

            std::unique_ptr<MokoBLEBatterySpec> battery_spec = std::make_unique<MokoBLEBatterySpec>(ble_devices_data_ptr_,
                                                                                                    battery_id_vec_variant,
                                                                                                    nearby_alert_beacon_data_ptr_,
                                                                                                    config_data_->nearby_alert_beacon_max_size_
                                                                                                );

            registered_filter_id_map_[FilterKeys::kBleBatteryStatus] = ble_observer_ptr_->SetFilter(SpecificationType::kServiceID,
                                                                                                    std::move(battery_spec),
                                                                                                    battery_fn_cb, false, true);

            {
                std::lock_guard<std::mutex> lock(retry_event_queue_ptr_->data_lock_);
                const uint64_t later = CurrentSteadyClockSeconds() +
                                        kInternalRetryEventsTimeoutMap.at(InternalRetryEvents::kNearbyAlertBeaconReport);
                                        retry_event_queue_ptr_->data_map_.emplace(later,
                                                            RetryEventData{.type_ = InternalRetryEvents::kNearbyAlertBeaconReport,
                                                                            .data_ = nullptr});
            }
        }

    } while (false);

    return true;
}

std::future<void> BTManager::RegisterInstallerAppFilter(bool needs_instant_notify) {
    LOG_I(kLogTag, "Inside: %s", __func__);

    constexpr size_t kCharsForComparison = 6;

    std::future<void> device_found_future;

    if (0 == registered_filter_id_map_.count(FilterKeys::kInstallerApp)) {

        // Clear previous entries
        {
            const std::lock_guard<std::mutex> lock(installer_app_attr_->data_lock_);
            installer_app_attr_->mac_address_.clear();
            installer_app_attr_->name_.clear();
        }

        class InstallerAppUpdateCheckSpec : virtual public ISpecification {
         public:
            explicit InstallerAppUpdateCheckSpec(const std::string &pattern):pattern_(pattern) {
                LOG_I(kLogTag, "Installer app update check pattern: %s", pattern_.c_str());
            }
            virtual bool IsSatisfied(const AdvertisementBase &device_attr) override {
                bool status = false;
                if (std::string::npos != device_attr.name_.find(pattern_)) {
                    LOG_I(kLogTag, "device update adv matched: %s with mac: %s", device_attr.name_.c_str(),
                          device_attr.mac_address_.c_str());
                    status = true;
                }
                return status;
            }
         private:
            const std::string pattern_;
        };

        class InstallerAppSpec : virtual public ISpecification {
         public:
            explicit InstallerAppSpec(const std::string &pattern,
                                      const std::vector<std::string> tags):pattern_(pattern) {

                // for(const auto &tag : tags) {
                //     installer_adv_tagged_pattern_.emplace_back(tag + pattern);
                //     installer_adv_tagged_pattern_.emplace_back(tag + (pattern.size() > kCharsForComparison ?
                //                                                      pattern.substr(pattern.size() - kCharsForComparison)
                //                                                      : pattern));
                // }

                installer_adv_tagged_pattern_.emplace_back(kInstallerAppPattern + pattern);
                installer_adv_tagged_pattern_.emplace_back(kInstallerAppShortPattern + pattern);
                installer_adv_tagged_pattern_.emplace_back(kInstallerAppPattern +
                                                          (pattern.size() > kCharsForComparison ?
                                                           pattern.substr(pattern.size() - kCharsForComparison)
                                                           : pattern));
                installer_adv_tagged_pattern_.emplace_back(kInstallerAppShortPattern +
                                                          (pattern.size() > kCharsForComparison ?
                                                           pattern.substr(pattern.size() - kCharsForComparison)
                                                           : pattern));
                for (const auto tagged_patterns : installer_adv_tagged_pattern_) {
                    LOG_I(kLogTag, "Installer app tagged pattern: %s", tagged_patterns.c_str());
                }
            }

            virtual bool IsSatisfied(const AdvertisementBase &device_attr) override {
                bool status = false;

                for (const auto &tagged_pattern : installer_adv_tagged_pattern_) {
                    if (std::string::npos != device_attr.name_.find(tagged_pattern)) {
                        LOG_I(kLogTag, "device adv tag matched: %s with mac: %s", device_attr.name_.c_str(),
                              device_attr.mac_address_.c_str());
                        status = true;
                        break;
                    }
                }

                return status;
            }

         private:
            const std::string pattern_;
            std::vector<std::string> installer_adv_tagged_pattern_;
        };

        nd::helpers::DeviceFoundCB fn_cb = nullptr;

        if (needs_instant_notify) {

            std::promise<void> device_found_promise;
            installer_device_found_promise_ = std::move(device_found_promise);
            device_found_future = installer_device_found_promise_.get_future();

            fn_cb = [this](const AdvertisementBase &device_attr) {
                // device_found_promise.set_value_at_thread_exit();
                {
                    const std::lock_guard<std::mutex> lock(installer_app_attr_->data_lock_);
                    installer_app_attr_->mac_address_ = device_attr.mac_address_;
                    installer_app_attr_->name_ = device_attr.name_;
                }

                installer_device_found_promise_.set_value();
            };

        } else {

            fn_cb = [this](const AdvertisementBase &device_attr) {

                {
                    const std::lock_guard<std::mutex> lock(installer_app_attr_->data_lock_);
                    installer_app_attr_->mac_address_ = device_attr.mac_address_;
                    installer_app_attr_->name_ = device_attr.name_;
                }

                nd::platform_specific::SendGenericMessage(INSTALLER_APP_DETECTED, "INSTALLER_APP_DETECTED",
                                                          kBT_Server_MQ_Name, kBT_Server_MQ_Name, msg_id_);

            };

            nd::helpers::DeviceFoundCB update_fn_cb = [this](const AdvertisementBase &device_attr) {
                nd::platform_specific::SendGenericMessage(INSTALLER_APP_UPDATE_DETECTED, "INSTALLER_APP_UPDATE_DETECTED",
                                                          kBT_Server_MQ_Name, kBT_Server_MQ_Name, msg_id_);
            };

            const std::string pattern = kInstallerAppUpdateCheckPattern +
                                        (config_data_->device_id_.size() > kCharsForComparison ?
                                         config_data_->device_id_.substr(config_data_->device_id_.size() - kCharsForComparison)
                                         : config_data_->device_id_);

            std::unique_ptr<InstallerAppUpdateCheckSpec>
            installer_app_update_spec = std::make_unique<InstallerAppUpdateCheckSpec>(pattern);
            registered_filter_id_map_[FilterKeys::kInstallerAppUpdate] = ble_observer_ptr_->SetFilter(
                                                                                    SpecificationType::kName,
                                                                                    std::move(installer_app_update_spec),
                                                                                    update_fn_cb, true, false);
            LOG_I(kLogTag, "Registered InstallerAppUpdateCheckSpec with pattern: %s", pattern.c_str());
        }

        const std::string pattern = config_data_->device_id_;

        std::unique_ptr<InstallerAppSpec>
            installer_app_spec = std::make_unique<InstallerAppSpec>(pattern, config_data_->installer_adv_tags_);
        registered_filter_id_map_[FilterKeys::kInstallerApp] = ble_observer_ptr_->SetFilter(SpecificationType::kName,
                                                                          std::move(installer_app_spec),
                                                                          fn_cb, true, false);

    } else {
        LOG_I(kLogTag, "kInstallerApp filter is already registered", __func__);
    }

    return device_found_future;
}

#ifdef ENABLE_VEHICLE_QR_BASED_LOGIN
void BTManager::RegisterDriverLoginAppQRFilter() {
    LOG_I(kLogTag, "Inside: %s", __func__);

    do {
        if (0 != registered_filter_id_map_.count(FilterKeys::kVehicleQr)) {
            LOG_I(kLogTag, "BlePair is already being scanned for");
            break;
        }

        if (!ReloadVehicleData()) {
            LOG_E(kLogTag, "Failed to reload vin in [%s:%d]", __func__, __LINE__);
            break;
        }

        current_scanned_drivers_.clear();
        driver_session_start_time_ = CurrentSystemClockMilliSeconds();

        class DriverLoginAppQRSpec : virtual public ISpecification {
         public:
            explicit DriverLoginAppQRSpec(const std::string &name, const std::string &pattern):name_(name),pattern_(pattern) {}
            virtual bool IsSatisfied(const AdvertisementBase &device_attr) override {
                bool status = false;
                if (std::string::npos != device_attr.name_.find(name_)) {
                    LOG_I(kLogTag, "Device name matched: %s mac: %s", device_attr.name_.c_str(), device_attr.mac_address_.c_str());
                    status = true;
                } else if (std::string::npos != device_attr.name_.find(pattern_)) {
                    LOG_I(kLogTag, "Other nearby device pattern detected: %s mac: %s", device_attr.name_.c_str(), device_attr.mac_address_.c_str());
                    status = true;
                }
                return status;
            }
         private:
            const std::string name_;
            const std::string pattern_;
        };

        auto driver_login_qr_fn_cb = [this](const AdvertisementBase &device_attr) {
            bool new_app_found = false;
            {
                const std::lock_guard<std::mutex> lock(driver_login_app_qr_attr_->data_lock_);
                const auto emplace_status = driver_login_app_qr_attr_->data_map_.emplace(device_attr.mac_address_, device_attr.name_);
                new_app_found = emplace_status.second;
            }

            if (new_app_found) {
                nd::platform_specific::SendGenericMessage(DRIVER_LOGIN_APP_QR_DETECTED, "DRIVER_LOGIN_APP_QR_DETECTED",
                                                          kBT_Server_MQ_Name, kBT_Server_MQ_Name, msg_id_);
            }
        };

        const std::string name = kDriverLoginAppPattern + config_data_->device_vin_;
        const std::string pattern = kDriverLoginAppPattern;

        std::unique_ptr<DriverLoginAppQRSpec> driver_login_qr_spec = std::make_unique<DriverLoginAppQRSpec>(name, pattern);
        registered_filter_id_map_[FilterKeys::kVehicleQr] = ble_observer_ptr_->SetFilter(SpecificationType::kName,
                                                                                             std::move(driver_login_qr_spec),
                                                                                             driver_login_qr_fn_cb,
                                                                                             false, false);

        {
            const std::lock_guard<std::mutex> lock(scan_events_timeout_->data_lock_);
            auto const now = CurrentSteadyClockSeconds();
            scan_events_timeout_->data_map_[TimeoutEvents::kVehicleQr] = now + kDriverLoginQRScanMaxTimeout;
        }
    } while (false);

}
#endif

bool BTManager::RegisterVBUSFilter(const std::string &mac_addr) {
    LOG_I(kLogTag, "Inside: %s", __func__);

    do {
        if (0 != registered_filter_id_map_.count(FilterKeys::kVBUS)) {
            LOG_I(kLogTag, "VBUS is already being scanned for");
            break;
        }

        class DeviceMacSpec : virtual public ISpecification {
         public:
            explicit DeviceMacSpec(const std::string &mac):mac_(mac) {}
            virtual bool IsSatisfied(const AdvertisementBase &device_attr) override {
                bool status = false;
                if (mac_ == device_attr.mac_address_) {
                    LOG_I(kLogTag, "device found, name: %s with: mac %s", device_attr.name_.c_str(),
                                                                          device_attr.mac_address_.c_str());
                    status = true;
                }
                return status;
            }
         private:
            const std::string mac_;
        };

        auto device_found_cb = [this](const AdvertisementBase &device_attr) {
            const bool device_found = true;
            nd::platform_specific::SendVbusAvailabilityMessage(device_found, kBT_Server_MQ_Name,
                                                               kAwsIot_Pub_Server_MQ_Name, msg_id_);
            if( config_data_->can_vd_enabled_) {
                nd::platform_specific::SendVbusAvailabilityMessage(device_found, kBT_Server_MQ_Name,
                                                                   kObd_Server_MQ_Name, msg_id_);
            }
        };

        std::unique_ptr<DeviceMacSpec> device_mac_spec = std::make_unique<DeviceMacSpec>(mac_addr);
        registered_filter_id_map_[FilterKeys::kVBUS] = ble_observer_ptr_->SetFilter(SpecificationType::kMacAddress,
                                                                                             std::move(device_mac_spec),
                                                                                             device_found_cb,
                                                                                             true, false);

        {
            const std::lock_guard<std::mutex> lock(scan_events_timeout_->data_lock_);
            auto const now = CurrentSteadyClockSeconds();
            scan_events_timeout_->data_map_[TimeoutEvents::kVBUS] = now + kVBUSDetectionTimeout;
        }
    } while (false);

    return true;
}

std::unique_ptr<BTManager::DiscoveredDeviceAttributes> BTManager::DiscoverDevice(const std::string &addr, uint32_t timeout_secs) {
    LOG_I(kLogTag, "Inside: %s", __func__);

    std::unique_ptr<DiscoveredDeviceAttributes> dev_attr_ptr;

    try {
        dev_attr_ptr = std::make_unique<DiscoveredDeviceAttributes>();
    } catch (const std::exception& e){
        LOG_E(kLogTag, "Allocation failed in %s what(): %s", __func__, e.what());
    } catch (...) {
        LOG_E(kLogTag, "Caught an exception of an undetermined type in %s", __func__);
    }

    if ((!addr.empty()) && (0 < timeout_secs) && (dev_attr_ptr)) {
        class DeviceDiscoverySpec : virtual public ISpecification {
         public:
            explicit DeviceDiscoverySpec(const std::string &mac):mac_(mac) {}
            virtual bool IsSatisfied(const AdvertisementBase &device_attr) override {
                bool status = false;
                if (mac_ == device_attr.mac_address_) {
                    LOG_I(kLogTag, "ScanForDevice: Device mac matched: %s with name: %s", device_attr.mac_address_.c_str(),
                          device_attr.name_.c_str());
                    status = true;
                }
                return status;
            }
         private:
            const std::string mac_;
        };

        std::promise<void> device_discovered_promise;
        std::future<void> device_found_future = device_discovered_promise.get_future();

        nd::helpers::DeviceFoundCB fn_cb = [&device_discovered_promise, &dev_attr_ptr](const AdvertisementBase &device_attr) {
            dev_attr_ptr->mac_address_ = device_attr.mac_address_;
            dev_attr_ptr->name_ = device_attr.name_;
            device_discovered_promise.set_value();
        };

        std::unique_ptr<DeviceDiscoverySpec> discovery_spec = std::make_unique<DeviceDiscoverySpec>(addr);
        auto filter_id = ble_observer_ptr_->SetFilter(SpecificationType::kName, std::move(discovery_spec), fn_cb, true, false);

        if ((device_found_future.valid()) &&
            (std::future_status::ready == device_found_future.wait_for(std::chrono::seconds(timeout_secs)))) {
            LOG_I(kLogTag, "Device: %s discovered", addr.c_str());
        } else {
            LOG_E(kLogTag, "Device: %s not discovered", addr.c_str());
        }

        ble_observer_ptr_->ClearFilter(filter_id);

    } else {
        LOG_E(kLogTag, "Invalid data in %s", __func__);
    }

    return dev_attr_ptr;
}

void BTManager::BeginBleDevicesIdentification() {
    LOG_I(kLogTag, "Inside: %s", __func__);

    do {
        if (0 != registered_filter_id_map_.count(FilterKeys::kBlePair)) {
            LOG_I(kLogTag, "BlePair is already being scanned for");
            break;
        }

        RegisterBlePairFilter();

        // Register timeout using internal retry event
        {
            const uint64_t later = CurrentSteadyClockSeconds() +
                                   kInternalRetryEventsTimeoutMap.at(InternalRetryEvents::kBleDevicePairTimeout);
            const std::lock_guard<std::mutex> lock(retry_event_queue_ptr_->data_lock_);
            retry_event_queue_ptr_->data_map_.emplace(later,
                                                      RetryEventData{.type_ = InternalRetryEvents::kBleDevicePairTimeout,
                                                                     .data_ = nullptr});
        }
    } while (false);
}

bool BTManager::ReadVBUSAddress(std::string &vbus_mac_out) {

    bool status = false;

    using nd::device::Accessory;
    using nd::device::AccessoryDB;

    constexpr char kVBUSAccessory[] = "VBUS";
    std::vector<Accessory> accessories;

    const auto get_status = AccessoryDB::GetAllOfType(kVBUSAccessory, accessories);
    do {

        if (0 != get_status.first) {
            LOG_E(kLogTag, "ReadVBUSAddress: GetAllOfType failed with error: %d:%s", get_status.first, get_status.second.c_str());
            break;
        }

        if (accessories.empty()) {
            LOG_I(kLogTag, "No VBUS devices found");
            break;
        }

        LOG_I(kLogTag, "accessory data fetched from db id: %s, type %s:, data %s:",
                                                    accessories[0].id_type_.first.c_str(),
                                                    accessories[0].id_type_.second.c_str(),
                                                    accessories[0].data_.c_str());

        std::string mac = accessories[0].id_type_.first;

        if (kMacAddressLength == mac.size()) {
            LOG_I(kLogTag, "VBUS mac from db is: %s", mac.c_str() );
            status = true;
            vbus_mac_out = std::move(mac);
        } else if ((kMacAddressLength - 5) == mac.size()) { // without colons
            mac.insert(2,":");
            mac.insert(5,":");
            mac.insert(8,":");
            mac.insert(11,":");
            mac.insert(14,":");
            LOG_I(kLogTag, "VBUS mac from db is after colon append: %s", mac.c_str() );
            status = true;
            vbus_mac_out = std::move(mac);
        } else {
            LOG_E(kLogTag, "Invalid VBUS mac from db: %s", mac.c_str() );
        }

    } while (false);

    return status;
}

void BTManager::HandleVBUSAvailability() {
    std::string vbus_mac;
    if (ReadVBUSAddress(vbus_mac)) {
        RegisterVBUSFilter(vbus_mac);
    }
}

void BTManager::BeginInstallerActivity(bool do_led_blink) {
    LOG_I(kLogTag, "Inside: %s", __func__);

    bool start_led_blink = false;

    do {
        if (file_is_present(kRebootIndicatorFile)) {
            LOG_E(kLogTag, "Device Rebooting");
            break;
        }

        // TODO(sunil.s): Need a get_system_uptime api. Perform only on fresh boot and not every time the service restarts

        // TODO(sunil.s): Bluetooth will now always be ON, is the below check required?
        // if (!file_touch(enablingBluetoothIndicatorFile)) {
        //     LOG_E(kLogTag, "Cannot create %s file", kEnablingBluetoothIndicatorFile);
        //     break;
        // }
        // std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // if( file_is_present(rebootIndicatorFile) ) {
        //     LOG_I (kLogTag, "Device Rebooting ");
        //     break;
        // }

        {
            const std::lock_guard<std::mutex> lock(installer_app_attr_->data_lock_);
            if (!installer_app_attr_->mac_address_.empty()) {
                LOG_I(kLogTag, "Installer app already detected: %s mac:%s", installer_app_attr_->name_.c_str(),
                      installer_app_attr_->mac_address_.c_str());
                break;
            }
        }

        {
            const std::lock_guard<std::mutex> lock(scan_events_timeout_->data_lock_);
            if (0 != scan_events_timeout_->data_map_.count(TimeoutEvents::kHotspotActive)) {
                LOG_I(kLogTag, "Hotspot is still on from previous installer detection");
                break;
            }
        }

        {
            const std::lock_guard<std::mutex> lock(scan_events_timeout_->data_lock_);
            if (0 != registered_filter_id_map_.count(FilterKeys::kInstallerApp)) {
                auto const now = CurrentSteadyClockSeconds();
                scan_events_timeout_->data_map_[TimeoutEvents::kInstallerApp] = now + kInstallerFreshScanTimeout;
                LOG_I(kLogTag, "Installer app scan already in progress, extending timeout");
                start_led_blink = true;
                break;
            }
        }

        // const auto itr = registered_filter_id_map_.find(kInstallerApp);
        // if (registered_filter_id_map_.end() != itr && 0 != itr->second) {
        //     LOG_E(kLogTag, "Installer detection already in progress");
        //     break;
        // }

        const bool force_enable = true;
        if (!EnableBTStartScan(force_enable)) {
            LOG_E(kLogTag, "Failed to enable Bluetooth scan");
            break;
        }
        RegisterInstallerAppFilter(false);
        if (!bt_interface_ptr_->LeScanOn()) {
            LOG_E(kLogTag, "Failed to start BLE scan for installer app");
            break;
        }

        // Watch for timeout
        {
            const std::lock_guard<std::mutex> lock(scan_events_timeout_->data_lock_);
            auto const now = CurrentSteadyClockSeconds();
            scan_events_timeout_->data_map_[TimeoutEvents::kInstallerApp] = now + kInstallerFreshScanTimeout;
        }

        start_led_blink = true;
    } while (false);

    bool is_installer_connected = file_is_present(GetInstallerConnIndicatorFile());

    if ((start_led_blink && do_led_blink) || is_installer_connected) {
        const char* destination_mq = is_installer_connected ? kInstaller_Server_MQ_Name : kNDCentral_Server_MQ_Name;
        if (!SendInstallerScanLedMessage(do_led_blink, config_data_->installer_led_blink_timeout_sec_,
                                          kBT_Server_MQ_Name, destination_mq)) {
            LOG_E(kLogTag, "Failed to start LED blink message");
        }
    }

}

void BTManager::HandleIgnitionStatus(void *g_msg) {
    do {

        ignition_status_t ign_status{};
        bool is_wake_up_event = false;

        if (!nd::platform_specific::FetchIgnitionStatus(g_msg, ign_status, is_wake_up_event)) {
            LOG_E(kLogTag, "[GEN] Failed to fetch ignition status");
            break;
        }

        if (ign_status == vehicle_state_attr_->ign_status_) {

            if ((IGNITION_ON != ign_status) && (is_wake_up_event) &&
                (is_wake_up_event != vehicle_state_attr_->wake_up_status_)) {
                LOG_I(kLogTag, "[GEN] Wake up event received in non IGNITION ON state");
            } else {
                LOG_I(kLogTag, "[GEN] Already in ignition status: %d, hence ignoring", ign_status);
                vehicle_state_attr_->wake_up_status_ = is_wake_up_event;
                break;
            }
        }

        vehicle_state_attr_->ign_status_ = ign_status;
        vehicle_state_attr_->wake_up_status_ = is_wake_up_event;

        if (adsm_state_mgr_) {
            adsm_state_mgr_->SetIgnStatus(static_cast<int>(ign_status));
            adsm_state_mgr_->SetWakeUpStatus(is_wake_up_event);
            if (adsm_state_mgr_->SaveState()) {
                LOG_I(kLogTag, "ADSM state saved successfully on ignition change");
            } else {
                LOG_E(kLogTag, "Failed to save ADSM state on ignition change");
            }
        }

        switch(ign_status) {
            case IGNITION_OFF: {
                LOG_I(kLogTag, "[GEN] IGNITION OFF received");

                if (vehicle_state_attr_->wake_up_status_) {
                    if (HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kDriverQR)) {
                        BeginQrLoginActivityIfEnabled(AudioInitiator::kQr,
                                                      kDLScanInitiatorStrMap.at(DLScanInitiator::kWoM));
                        RegisterForAdsmWatchdogTimer();
                    }
                } else {
                    if (recovery_window_duration_expired_) {
                        if (HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kEnhancedBt) ||
                            HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kDriverQR)) {
                            ClearLoginAudioPrompt();
                            StopDriverLoginBtLegacyDetectionActivity();
                            StopDriverLoginQRActivity(kDLScanInitiatorStrMap.at(DLScanInitiator::kOnIgnitionOff));
                            DisassociateDriver();
                            LOG_I(kLogTag, "Driver logins cleared");
                        }
                    } else {
                        LOG_I(kLogTag, "[GEN] Ignition OFF received before recovery timer expiry for receiving ignition status");
                    }
                }

                vehicle_state_attr_->had_vehicle_moved_out_of_idle_post_login_ = false;

                break;
            }

            case IGNITION_ON: {
                LOG_I(kLogTag, "[GEN] IGNITION ON received");
                RegisterForAdsmWatchdogTimer();

                const bool has_bt_login = HasFeature(config_data_->driver_login_features_,
                                                      DriverLoginFeature::kEnhancedBt);

                const bool has_qr_login = HasFeature(config_data_->driver_login_features_,
                                                     DriverLoginFeature::kDriverQR);


                if (has_bt_login || has_qr_login) {
                    if (config_data_->requires_login_audio_on_ignition_on_) {
                        TriggerDriverLoginAudio();
                    }
                }

#ifdef ENABLE_VEHICLE_QR_BASED_LOGIN
                if (HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kVehicleQR)) {
                    RegisterDriverLoginAppQRFilter();
                } else {
                    BeginEnhancedLoginActivityIfEnabled(AudioInitiator::kBt,
                                                        kDLScanInitiatorStrMap.at(DLScanInitiator::kOnIgnition));
                }
#else
                BeginQrLoginActivityIfEnabled(AudioInitiator::kQr,
                                                      kDLScanInitiatorStrMap.at(DLScanInitiator::kOnIgnition));
                BeginEnhancedLoginActivityIfEnabled(AudioInitiator::kBt,
                                                    kDLScanInitiatorStrMap.at(DLScanInitiator::kOnIgnition));
#endif

                break;
            }

            case IGNITION_ERR: {
                LOG_I(kLogTag, "[GEN] IGNITION ERR received");
                break;
            }

            default: {
                LOG_E(kLogTag, "[GEN] Unknown ignition status: %d received", ign_status);
                break;
            }
        }
    } while (false);
}

void BTManager::HandleIdleIgnitionStatus(void *g_msg) {
    do {

        ignition_status_t ign_status{};
        bool is_wake_up_event = false;

        if (!nd::platform_specific::FetchIgnitionStatus(g_msg, ign_status, is_wake_up_event)) {
            LOG_E(kLogTag, "[GEN] Failed to fetch ignition status");
            break;
        }

        if (ign_status == vehicle_state_attr_->ign_status_) {

            if ((IGNITION_ON != ign_status) && (is_wake_up_event) &&
                (is_wake_up_event != vehicle_state_attr_->wake_up_status_)) {
                LOG_I(kLogTag, "Wake up event received in non IGNITION ON state");
            } else {
                LOG_I(kLogTag, "Already in ignition status: %d, hence ignoring", ign_status);
                vehicle_state_attr_->wake_up_status_ = is_wake_up_event;
                break;
            }
        }

        vehicle_state_attr_->ign_status_ = ign_status;
        vehicle_state_attr_->wake_up_status_ = is_wake_up_event;

        if (adsm_state_mgr_) {
            adsm_state_mgr_->SetIgnStatus(static_cast<int>(ign_status));
            adsm_state_mgr_->SetWakeUpStatus(is_wake_up_event);
            if (adsm_state_mgr_->SaveState()) {
                LOG_I(kLogTag, "ADSM state saved successfully on ignition change");
            } else {
                LOG_E(kLogTag, "Failed to save ADSM state on ignition change");
            }
        }

        switch(ign_status) {
            case IGNITION_OFF: {
                LOG_I(kLogTag, "IGNITION OFF received");

                if (vehicle_state_attr_->wake_up_status_) {
                    if (HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kDriverQR)) {
                        BeginQrLoginActivityIfEnabled(AudioInitiator::kQr,
                                                      kDLScanInitiatorStrMap.at(DLScanInitiator::kWoM));
                        RegisterForAdsmWatchdogTimer();
                    }
                } else {
                    if (recovery_window_duration_expired_) {
                        if (HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kEnhancedBt) ||
                            HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kDriverQR)) {
                            ClearLoginAudioPrompt();
                            StopDriverLoginBtLegacyDetectionActivity();
                            StopDriverLoginQRActivity(kDLScanInitiatorStrMap.at(DLScanInitiator::kOnIgnitionOff));
                            DisassociateDriver();
                            LOG_I(kLogTag, "Driver logins cleared");
                        }
                    } else {
                        LOG_I(kLogTag, "Ignition OFF received before recovery timer expiry for receiving ignition status");
                    }
                }

                vehicle_state_attr_->had_vehicle_moved_out_of_idle_post_login_ = false;

                break;
            }

            case IGNITION_ON: {
                LOG_I(kLogTag, "IGNITION ON received");

                RegisterForAdsmWatchdogTimer();

                BeginInstallerActivity();
                HandleVBUSAvailability();

                const bool has_bt_login = HasFeature(config_data_->driver_login_features_,
                                                      DriverLoginFeature::kEnhancedBt);

                const bool has_qr_login = HasFeature(config_data_->driver_login_features_,
                                                     DriverLoginFeature::kDriverQR);

                if (has_bt_login || has_qr_login) {
                    if (config_data_->requires_login_audio_on_ignition_on_) {
                        TriggerDriverLoginAudio();
                    }
                }

#ifdef ENABLE_VEHICLE_QR_BASED_LOGIN
                if (HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kVehicleQR)) {
                    RegisterDriverLoginAppQRFilter();
                } else {
                    BeginEnhancedLoginActivityIfEnabled(AudioInitiator::kBt,
                                                        kDLScanInitiatorStrMap.at(DLScanInitiator::kOnIgnition));
                }
#else
                BeginQrLoginActivityIfEnabled(AudioInitiator::kQr,
                                                      kDLScanInitiatorStrMap.at(DLScanInitiator::kOnIgnition));
                BeginEnhancedLoginActivityIfEnabled(AudioInitiator::kBt,
                                                    kDLScanInitiatorStrMap.at(DLScanInitiator::kOnIgnition));
#endif

                break;
            }

            case IGNITION_ERR: {
                LOG_I(kLogTag, "IGNITION ERR received");
                break;
            }

            default: {
                LOG_E(kLogTag, "Unknown ignition status: %d received", ign_status);
                break;
            }
        }
    } while (false);
}

void BTManager::TriggerDriverLoginAudio() {

    if ((HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kEnhancedBt)) ||
        ((HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kDriverQR)) && is_qr_scan_allowed_)) {

        if (!HasDriverLoggedIn()) {
            LOG_I(kLogTag, "Triggering driver login audio prompt as no driver logged in");

            uint64_t previous_audio_count = 0;

            if (adsm_state_mgr_) {
                previous_audio_count = adsm_state_mgr_->GetAudioCount();
            }

            // Drop audios if triggered within recovery window duration
            if (recovery_window_duration_expired_ || (0 == previous_audio_count)) {
                remaining_login_audio_prompt_count_ = config_data_->driver_login_audio_play_count_;
            } else {
                LOG_I(kLogTag, "Dropping audios within recovery window");
            }

            {
                login_audio_play_cv_.notify_all();
            }
        } else {
            LOG_I(kLogTag, "Driver already detected");
        }
    }
}

std::string BTManager::PrepareUpdateCheckAdvPacket() const {
    // size of the advertised packet is 16 (although it supports to a maximum of 23 bytes) which
    // includes 1 byte of length, which leaves 15 bytes for OTA version

    // output string example : -
    // ota version size (0x0B) + ota version (0x33 0x2e 0x35 0x2e 0x32 0x32 0x2e 0x72 0x63 0x2e 0x32)
    // 0x0B 0x33 0x2e 0x35 0x2e 0x32 0x32 0x2e 0x72 0x63 0x2e 0x32

    std::string status_to_broadcast = "OK";

    std::ostringstream oss;
    oss << "0x" << std::hex << status_to_broadcast.size();

    const std::string final_advertising_string = oss.str() + " " +
                                                 Translator::StringToAsciiHex(status_to_broadcast, ' ', true);

    return final_advertising_string;
}

bool BTManager::AdvertiseUpdateCheckResponse() {

    const uint64_t duration_multiplier = config_data_->ota_adv_duration_ * 3;

    constexpr uint64_t kMaxUpdateAckAdvertiseDuration = 25;

    const uint64_t ack_advertise_duration = duration_multiplier > kMaxUpdateAckAdvertiseDuration ?
                                           kMaxUpdateAckAdvertiseDuration : duration_multiplier;

    if (0 != registered_filter_id_map_.count(FilterKeys::kInstallerApp)) {
        LOG_I(kLogTag, "Installer App scan already in progress, we have to extending the time");
        const std::lock_guard<std::mutex> lock(scan_events_timeout_->data_lock_);
        const auto itr = scan_events_timeout_->data_map_.find(TimeoutEvents::kInstallerApp);

        // Increase the time for detection of Installer App
        if (itr != scan_events_timeout_->data_map_.end()) {
            itr->second += ack_advertise_duration;
            LOG_I(kLogTag, "Extended Installer App scan time by %llu seconds", ack_advertise_duration);
        }
    }

    LOG_I(kLogTag, "Inside: %s", __func__);
    const std::string advertise_name = "DI:" + config_data_->device_id_;
    const auto advertise_uuid = Translator::HexStringToBytes(kOtaAdvertiseUuid);
    const auto advertise_data = PrepareUpdateCheckAdvPacket();
    const bool status = bt_interface_ptr_->StartServiceAdvertising(advertise_name, advertise_uuid,
                                                                   advertise_data, ack_advertise_duration);

    bt_interface_ptr_->StopServiceAdvertising();
    const std::string audio_path = "/home/ubuntu/autocam/audio/nd_debug2/fcw.wav";
    nd::platform_specific::SendDriverLoginAudioNotification(audio_path,
                                                            kBT_Server_MQ_Name, kPower_Server_MQ_Name,
                                                            msg_id_);
    return status;
}

bool BTManager::VerifyAndInitiateWiFiHotspot() {
    bool status = false;

    ClearInstallerAppFilter();

    const std::lock_guard<std::mutex> lock(installer_app_attr_->data_lock_);

    if (!installer_app_attr_->name_.empty()) {

        int band_type = 'b'; //default value for 2.4Ghz

        const std::size_t band_type_pos = installer_app_attr_->name_.find('#');

        // pattern possibility : NTDI:3633000350#a
        if ((std::string::npos != band_type_pos) && (band_type_pos + 1 < installer_app_attr_->name_.size())) {
            LOG_I(kLogTag, "band type : %c", installer_app_attr_->name_.at(band_type_pos + 1));
            band_type = installer_app_attr_->name_.at(band_type_pos + 1);
        }

        LOG_I(kLogTag, "MAC address before sending wifi: %s, band: %c", installer_app_attr_->mac_address_.c_str(), band_type);

        if (!nd::platform_specific::SendCreateHotspotMsg(true, band_type, "",
                                                         kBT_Server_MQ_Name, kWiFi_Server_MQ_Name, msg_id_)) {
            LOG_E(kLogTag, "Sending msg to wifi mgr failed");

            // create self message to re-initiate scan to retry later
            nd::platform_specific::SendGenericMessage(START_INSTALLER_SCAN, "START_INSTALLER_SCAN",
                                                      kBT_Server_MQ_Name, kBT_Server_MQ_Name, msg_id_);
        } else {
            LOG_I(kLogTag, "Sent message to wifi mgr service");
            status = true;
        }
    } else {
        LOG_E(kLogTag, "installer_app_attr_->name_ is empty");
    }
    return status;
}

std::string BTManager::PrepareOtaAdvPacket() const {

    // size of the advertised packet is 16 (although it supports to a maximum of 23 bytes) which
    // includes 1 byte of length, which leaves 15 bytes for OTA version

    // Aug 23, 2023: increasing to 22
    constexpr size_t kMaxOtaLengthToBroadcast = 22;

    // output string example : -
    // ota version size (0x0B) + ota version (0x33 0x2e 0x35 0x2e 0x32 0x32 0x2e 0x72 0x63 0x2e 0x32)
    // 0x0B 0x33 0x2e 0x35 0x2e 0x32 0x32 0x2e 0x72 0x63 0x2e 0x32

    auto ota_version_to_broadcast = GetMinimizedOTAString();

    if (kMaxOtaLengthToBroadcast < ota_version_to_broadcast.size()) {
        LOG_E(kLogTag, "OTA version length %d exceeds max broadcast length %d, will truncate to max length",
                    ota_version_to_broadcast.size(), kMaxOtaLengthToBroadcast);
        ota_version_to_broadcast.resize(kMaxOtaLengthToBroadcast);
    }

    std::ostringstream oss;
    oss << "0x" << std::hex << ota_version_to_broadcast.size();

    const std::string final_advertising_string = oss.str() + " " + Translator::StringToAsciiHex(ota_version_to_broadcast, ' ', true);

    return final_advertising_string;
}

bool BTManager::AdvertiseOtaVersion() const {
    const std::string advertise_name = "DI:" + config_data_->device_id_;
    const auto advertise_uuid = Translator::HexStringToBytes(kOtaAdvertiseUuid);
    const auto advertise_data = PrepareOtaAdvPacket();
    const bool status = bt_interface_ptr_->StartServiceAdvertising(advertise_name, advertise_uuid,
                                                                   advertise_data, config_data_->ota_adv_duration_);

    bt_interface_ptr_->StopServiceAdvertising();
    return status;
}

std::string BTManager::GetMinimizedOTAString() const{
    LOG_I(kLogTag, "Actual OTA version: %s", config_data_->ota_version_.c_str());

    bool is_version_valid = false;

    std::string minified_ota = config_data_->ota_version_;

    // Actual OTA version: 88.1.66.sp.3.6.2.rc.2_VDM_P34_ST -> Minimized OTA version: 3.6.2

    // Actual OTA version: 3.6.2.rc.1 -> Minimized OTA version: 3.6.2

    size_t pos = config_data_->ota_version_.find("sp");

    if (std::string::npos != pos) {

        constexpr uint32_t kSpecialPackageIndicatorSize = 3;   // 3 -> "sp."

        if (config_data_->ota_version_.size() > (pos + kSpecialPackageIndicatorSize)) {
            pos = pos + kSpecialPackageIndicatorSize;
            minified_ota = (config_data_->ota_version_).substr(pos);
        }
    }

    pos = 0;

    constexpr unsigned int kMaxDotSeparatedVersionCount = 3;

    for (unsigned int counter = 1; counter <= kMaxDotSeparatedVersionCount; ++counter) {
        pos = minified_ota.find('.', pos + 1);
        if (std::string::npos == pos) {
            break;
        }

        if (kMaxDotSeparatedVersionCount == counter) {
            is_version_valid = true;
        }
    }

    std::string minimized_ota_version;

    if (is_version_valid) {
        constexpr char kShortenedIndicator[] = "...";
        minimized_ota_version = minified_ota.substr(0, pos);
        minimized_ota_version += kShortenedIndicator;
    } else {
        LOG_E(kLogTag, "OTA version is not valid");
        minimized_ota_version = config_data_->ota_version_;
    }

    LOG_I(kLogTag, "Minimized OTA version: %s", minimized_ota_version.c_str());

    return minimized_ota_version;
}

void BTManager::DoInstallerAppWriteChar() {
    // At this stage, hotspot is created and installer service is waiting for connection for 90 seconds
    bool status = false;

    do {
        {
            // WriteInstallerScanState
            if (WriteInstallerScanState(kInstallerScanStateInstallerApp)) {
                LOG_I(kLogTag, "WriteInstallerScanState success");
            } else {
                LOG_E(kLogTag, "WriteInstallerScanState failed");
            }
        }

        {
            const std::lock_guard<std::mutex> lock(scan_events_timeout_->data_lock_);
            scan_events_timeout_->data_map_.erase(TimeoutEvents::kInstallerApp);
            scan_events_timeout_->data_map_[TimeoutEvents::kHotspotActive] = std::numeric_limits<uint32_t>::max(); //max value
        }

        std::string cache_mac_address_;
        std::string cache_name_;

        {
            const std::lock_guard<std::mutex> lock(installer_app_attr_->data_lock_);
            cache_mac_address_ = installer_app_attr_->mac_address_;
            cache_name_ = installer_app_attr_->name_;
            installer_app_attr_->mac_address_.clear();
            installer_app_attr_->name_.clear();
        }

        LOG_I(kLogTag, "Cached name: %s, mac: %s", cache_name_.c_str(), cache_mac_address_.c_str());

        const bool force_enable = true;
        EnableBTStartScan(force_enable);

        if (AdvertiseOtaVersion()) {
            status = true;
        }

        auto device_found_future = RegisterInstallerAppFilter(true);
        bt_interface_ptr_->LeScanOn();

        if ((device_found_future.valid()) &&
            (std::future_status::ready == device_found_future.wait_for(std::chrono::seconds(kInstallerAppReScanTimeout)))) {
            LOG_I(kLogTag, "Installer App device found");
        } else {
            LOG_E(kLogTag, "Timeout, no installer app found. "
                           "It is possible that app has received advertising packet and turned off BT.");
        }

        ClearInstallerAppFilter();

        bt_interface_ptr_->LeScanOff();

        if (installer_app_attr_->mac_address_.empty()) {
            // Hotspot is still ON but app is not available
            LOG_E(kLogTag, "installer_app_attr_->mac_address_ is empty after re-scan in [%s:%d]", __func__, __LINE__);
            break;
        }

        if (cache_mac_address_ != installer_app_attr_->mac_address_) {
            LOG_I(kLogTag, "Mac address changed from: %s to: %s",
                  cache_mac_address_.c_str(), installer_app_attr_->mac_address_.c_str());
        }

        if (IsFilterSet(FilterKeys::kDriverLoginService)) {
            StopDriverLoginLegacyAdvertisement();
            LOG_I(kLogTag, "Stopped driver login detection for DBUS write char operation");
        }

        const auto minimized_ota_version = GetMinimizedOTAString();

        const std::vector<std::string> write_data = {std::to_string(minimized_ota_version.size()), minimized_ota_version};

        if (bt_interface_backup_ptr_->WriteCharacteristicData(installer_app_attr_->mac_address_,
                                                       kInstallerAppWriteCharUuid, write_data, nd::interface::AddressType::kRandom,
                                                       service_obj_ptr_)) {
            LOG_I(kLogTag, "Write char success");
            status = true;
            break;
        }

        if (bt_interface_backup_ptr_->WriteCharacteristicData(installer_app_attr_->mac_address_,
                                                       kInstallerAppWriteCharUuid, write_data, nd::interface::AddressType::kAny,
                                                       service_obj_ptr_)) {
            LOG_I(kLogTag, "Write char success");
            status = true;
            break;
        }

        LOG_E(kLogTag, "Write char failed even after retries");

    } while (false);

    if (status) {
        const bool force_bt_off = true;
        StopScanDisableBT(force_bt_off);
    } else {
        EnableBTStartScan();
        if (IsFilterSet(FilterKeys::kDriverLoginService)) {
            StartDriverLoginLegacyAdvertisement();
            LOG_I(kLogTag, "Started driver login detection after DBUS write char operation");
        }
    }
}

void BTManager::ReInitiateInstallerActivity() {
    do {
        {
            const std::lock_guard<std::mutex> lock(installer_app_attr_->data_lock_);
            installer_app_attr_->mac_address_.clear();
            installer_app_attr_->name_.clear();
        }

        {
            const std::lock_guard<std::mutex> lock(scan_events_timeout_->data_lock_);
            const auto itr = scan_events_timeout_->data_map_.find(TimeoutEvents::kInstallerApp);

            if (scan_events_timeout_->data_map_.end() == itr) {
                LOG_E(kLogTag, "Trigger received past installer scan period");
                scan_events_timeout_->data_map_.erase(TimeoutEvents::kHotspotActive); // reset hotspot time
                break;
            }
        }

        const bool force_enable = true;
        EnableBTStartScan(force_enable);
        auto device_found_future = RegisterInstallerAppFilter(true);
        bt_interface_ptr_->LeScanOn();

        if ((device_found_future.valid()) &&
            (std::future_status::ready == device_found_future.wait_for(std::chrono::seconds(kInstallerAppReScanTimeout)))) {
            LOG_I(kLogTag, "Installer App device found");
            ClearInstallerAppFilter();
            VerifyAndInitiateWiFiHotspot();
        } else {
            LOG_E(kLogTag, "Timeout, no installer app found");
            ClearInstallerAppFilter();
            StopScanDisableBT();
            {
                const std::lock_guard<std::mutex> lock(installer_app_attr_->data_lock_);
                installer_app_attr_->mac_address_.clear();
                installer_app_attr_->name_.clear();
            }
        }

    } while (false);
}

bool BTManager::HandleInstallerAppDetectionTimeout() {
    bool status = false;
    {
        const std::lock_guard<std::mutex> lock(scan_events_timeout_->data_lock_);
        scan_events_timeout_->data_map_.erase(TimeoutEvents::kInstallerApp);
    }

    const uint16_t blink_timeout = 0;
    const bool do_led_blink = false;
    if (!SendInstallerScanLedMessage(do_led_blink, blink_timeout,
                                                kBT_Server_MQ_Name, kNDCentral_Server_MQ_Name)) {
        LOG_E(kLogTag, "Failed to send stop LED blink message");
    }

    ClearInstallerAppFilter();
    StopScanDisableBT();
    {
        const std::lock_guard<std::mutex> lock(installer_app_attr_->data_lock_);
        installer_app_attr_->mac_address_.clear();
        installer_app_attr_->name_.clear();
    }
    return status;
}

bool BTManager::SendInstallerScanLedMessage(const bool do_led_blink, const uint16_t timeout_sec,
                                             const char *src, const char *dest) {
    bool ret = false;
    do {
        if (do_led_blink) {
            if (!WriteInstallerScanState(kInstallerScanStateNdBt)) {
                LOG_E(kLogTag, "Failed to write installer scan state for LED_BLINK_REQ message");
                break;
            }
        } else {
            if (!CleanupStaleInstallerScanState()) {
                LOG_E(kLogTag, "Failed to cleanup installer scan state for LED_BLINK_REQ message");
                break;
            }
        }

        led_blink_status_t led_status = do_led_blink ? LED_START_BLINKING : LED_STOP_BLINKING;

        if (!nd::platform_specific::SendLedBlinkMessage(led_status, timeout_sec, src, dest, msg_id_)) {
            LOG_E(kLogTag, "Failed to send LED_BLINK_REQ message");
            break;
        }

        ret = true;
    } while (false);

    if (!ret) {
        CleanupStaleInstallerScanState();
        LOG_E(kLogTag, "Failed to process LED_BLINK_REQ message for do_led_blink: %d", do_led_blink);
    }
    return ret;
}

bool BTManager::WriteInstallerScanState(const std::string &state) {
    bool ret = false;
    do {
        const std::string installer_scan_state_file = GetInstallerScanIndicatorFile();
        std::ofstream ofs(installer_scan_state_file, std::ofstream::out | std::ofstream::trunc);
        if (!ofs.is_open()) {
            LOG_E(kLogTag, "Failed to open installer scan state file: %s", installer_scan_state_file.c_str());
            break;
        }

        ofs << state;
        ofs.flush();
        ofs.close();
        LOG_I(kLogTag, "Written installer scan state: %s to file: %s", state.c_str(), installer_scan_state_file.c_str());
        ret = true;
    } while (false);

    return ret;
}

bool BTManager::CleanupStaleInstallerScanState() {
    bool ret = false;
    const std::string installer_scan_state_file = GetInstallerScanIndicatorFile();
    if (file_is_present(installer_scan_state_file)) {
        LOG_I(kLogTag, "Stale installer scan state file found, attempting to remove: %s", installer_scan_state_file.c_str());
        if(!file_delete(installer_scan_state_file)) {
            LOG_E(kLogTag, "Failed to remove stale installer scan state file: %s", installer_scan_state_file.c_str());
        } else {
            LOG_I(kLogTag, "Successfully removed stale installer scan state file: %s", installer_scan_state_file.c_str());
            ret = true;
        }

    } else {
        LOG_I(kLogTag, "No stale installer scan state file found to remove");
        ret = true;
    }
    return ret;
}

void BTManager::RestartBluetoothActivities() {
    {
        const std::lock_guard<std::mutex> lock(scan_events_timeout_->data_lock_);
        scan_events_timeout_->data_map_.erase(TimeoutEvents::kHotspotActive);
    }

    {
        const std::lock_guard<std::mutex> lock(installer_app_attr_->data_lock_);
        installer_app_attr_->mac_address_.clear();
        installer_app_attr_->name_.clear();
    }

    EnableBTStartScan();

    // move to motion state if speed event had occurred earlier
    if (vehicle_state_attr_->does_current_speed_meets_login_threshold_ &&
        !vehicle_state_attr_->driver_login_idle_on_) {
        LOG_I(kLogTag, "End of installer activity. Move to motion state");
        MoveToState(States::kMotion);
    }
}

void BTManager::HandleIdleEventRegistrationResponse(void *g_msg) {
    int handle{};
    idle_reg_type_t type{};

    if (nd::platform_specific::FetchIdleEventRegistrationResponse(g_msg, handle, type)) {
        switch (type) {
            case IDLE_REG_ENGINE: {
                speed_services_->idle_engine_handle_ = handle;
                break;
            }
            case IDLE_REG_DL_FR: {
                speed_services_->idle_login_fr_handle_ = handle;
                break;
            }
            case IDLE_REG_DL: {
                speed_services_->idle_login_handle_ = handle;
                break;
            }
            case IDLE_REG_PRIVACY: {
                speed_services_->idle_privacy_handle_ = handle;
                break;
            }
            case IDLE_REG_QR: {
                speed_services_->idle_qr_handle_ = handle;
                break;
            }
            default: {
                break;
            }
        }
    }
}

void BTManager::HandleSpeedEventRegistrationResponse(void *g_msg) {
    int handle{};
    speed_reg_type_t type{};

    if (nd::platform_specific::FetchSpeedEventRegistrationResponse(g_msg, handle, type)) {
        switch (type) {
            case SPEED_REG_DRV_LOGIN: {
                speed_services_->speed_login_handle_ = handle;
                break;
            }
            case SPEED_REG_PRIVACY: {
                speed_services_->speed_privacy_handle_ = handle;
                break;
            }
            case SPEED_REG_QR: {
                speed_services_->speed_qr_handle_ = handle;
                break;
            }
            default: {
                break;
            }
        }
    }
}

void BTManager::ScanFor(uint64_t seconds) {
    LOG_I(kLogTag, "Inside: %s", __func__);

    if ((!bt_interface_ptr_->SupportsScanAndConnectionParallelly()) &&
        (0 < seconds)) {
        // Turn off Scan
        bt_interface_ptr_->LeScanOn();
        std::this_thread::sleep_for(std::chrono::seconds(seconds));
        bt_interface_ptr_->LeScanOff();
    }
}

void BTManager::HandleDeviceWriteChar(void *g_msg, const std::string &client_name) {

    LOG_I(kLogTag, "Inside: %s", __func__);

    bool status = false;
    BTErrCode ret_code = BTErrCode::kOther;
    bool bt_enabled_for_write_char = false;
    bool bt_scan_enabled_for_write_char = false;
    bool is_le_scan_in_progress = false;

    if (bt_interface_ptr_->IsEnabled()) {
        is_le_scan_in_progress = bt_interface_ptr_->IsLeScanOn();
    }

    do {

        std::string mac_address;
        std::string uuid;
        std::vector<std::string> data;

        const uint64_t steady_now = CurrentSteadyClockSeconds();
        auto itr = rate_limiter_write_char_map_.find(client_name);

        if (itr != rate_limiter_write_char_map_.end()) {
            LOG_I(kLogTag, "Found existing rate limiter entry for client: %s", client_name.c_str());

            auto &last_request_time = itr->second;
            const uint64_t duration_since_last_request = steady_now - last_request_time;

            if (duration_since_last_request < config_data_->rate_limiter_write_char_seconds_) {
                LOG_W(kLogTag, "Write char request from client: %s is rate limited. Time since last request: %lld seconds",
                      client_name.c_str(), duration_since_last_request);
                ret_code = BTErrCode::kRateLimited;
                break;
            } else {
                last_request_time = steady_now;
            }

        } else {
            rate_limiter_write_char_map_[client_name] = steady_now;
        }

        if (!nd::platform_specific::FetchWriteCharData(g_msg, mac_address, uuid, data)) {
            LOG_E(kLogTag, "Failed to fetch write char data");
            ret_code = BTErrCode::kInvalidData;
            break;
        }

        if (mac_address.empty() || uuid.empty() || data.empty()) {
            LOG_E(kLogTag, "Invalid data received");
            ret_code = BTErrCode::kInvalidData;
            break;
        }

        if (!bt_interface_ptr_->IsEnabled()) {

            {
                const std::lock_guard<std::mutex> lock(scan_events_timeout_->data_lock_);
                if (0 != scan_events_timeout_->data_map_.count(TimeoutEvents::kHotspotActive)) {
                    LOG_I(kLogTag, "Hotspot is on for installer app, hence not turning on BT");
                    ret_code = BTErrCode::kOther;
                    break;
                }
            }

            LOG_I(kLogTag, "BT is not enabled, enabling now for write_char");
            bt_enabled_for_write_char = true;
        }

        if (!bt_interface_ptr_->SupportsScanAndConnectionParallelly()) {

            if (bt_interface_ptr_->IsEnabled()) {
                bt_interface_ptr_->LeScanOff();
                if (IsFilterSet(FilterKeys::kDriverLoginService)) {
                    StopDriverLoginLegacyAdvertisement();
                    LOG_I(kLogTag, "Stopped driver login detection for DBUS write char operation");
                }
            }

#ifdef DO_FIRMWARE_FLASH
            if (config_data_->toggle_firmware_flash_) {
                bt_interface_ptr_->Disable();
                bt_interface_ptr_->DoGattSetup();
                constexpr uint32_t kBTDelayPostGattSetup = 1000; // in milliseconds
                std::this_thread::sleep_for(std::chrono::milliseconds(kBTDelayPostGattSetup));
            }
#endif
        }

        if (!bt_interface_ptr_->IsEnabled()) {
            if (!bt_interface_ptr_->Enable()) {
                LOG_E(kLogTag, "Failed to enable BT");
                ret_code = BTErrCode::kDisabled;
                break;
            }
        }

        int retries_left = 1;

        do {

            if (!bt_interface_ptr_->SupportsScanAndConnectionParallelly()) {
                // Turn off Scan
                bt_interface_ptr_->LeScanOff();

            } else {
                if (!bt_interface_ptr_->IsLeScanOn()) {
                    if (bt_interface_ptr_->LeScanOn()) {
                        bt_scan_enabled_for_write_char = true;
                    }
                }

                constexpr uint32_t kDeviceDetectionTimeout = 5; // in seconds
                auto device_ptr = DiscoverDevice(mac_address, kDeviceDetectionTimeout);
                if ((!device_ptr) || (device_ptr->mac_address_.empty())) {
                    LOG_E(kLogTag, "Failed to discover device: %s", mac_address.c_str());
                    ret_code = BTErrCode::kOther;
                    --retries_left;
                    continue;
                }
            }

            const std::vector<std::string> write_data = data;
            for (const auto &element : write_data) {
                LOG_I(kLogTag, "Data: %s, size: %zu", element.c_str(), element.size());
            }

            LOG_I(kLogTag, "Writing data to device: %s, size: %zu", mac_address.c_str(), write_data.size());

            if (!bt_interface_backup_ptr_->WriteCharacteristicData(mac_address, uuid, write_data,
                                                            nd::interface::AddressType::kAny, service_obj_ptr_)) {
                LOG_E(kLogTag, "Write char failed");
                ret_code = BTErrCode::kWriteFailed;
                --retries_left;
            } else {
                status = true;
                ret_code = BTErrCode::kNoError;

                LOG_I(kLogTag, "Write char success on device: %s", mac_address.c_str());
                break;
            }

            if (!bt_interface_ptr_->SupportsScanAndConnectionParallelly()) {
                // Do intermediate scan before retry
                if (is_le_scan_in_progress) {
                    ScanFor(config_data_->scan_time_btw_char_ops_);
                }
            }

        } while (retries_left > 0);

    } while (false);

    nd::platform_specific::SendDeviceWriteCharResponse(status, ret_code, kBT_Server_MQ_Name, client_name.c_str(), msg_id_);

#ifdef DO_FIRMWARE_FLASH
    if ((!bt_interface_ptr_->SupportsScanAndConnectionParallelly()) &&
        (config_data_->toggle_firmware_flash_)) {

        bt_interface_ptr_->LeScanOff();
        bt_interface_ptr_->Disable();
        bt_interface_ptr_->TearDownGattSetup();
        bt_interface_ptr_->Enable();

    }
#endif

    if (is_le_scan_in_progress) {
        bt_interface_ptr_->LeScanOn();
        if (IsFilterSet(FilterKeys::kDriverLoginService)) {
            StartDriverLoginLegacyAdvertisement();
            LOG_I(kLogTag, "Started driver login detection After DBUS write char operation");
        }
    } else if (bt_scan_enabled_for_write_char) {
        bt_interface_ptr_->LeScanOff();
    }

    if (bt_enabled_for_write_char) {
        bt_interface_ptr_->LeScanOff();
        if (!bt_interface_ptr_->Disable()) {
            LOG_I(kLogTag, "Failed to disable BT in [%s:%d]", __func__, __LINE__);
        }
    }
}

bool BTManager::HandleInitStateEvent(void *msg) {
    bool message_handled = true;
    nd_msgq_t::nd_msg_t *message = reinterpret_cast<nd_msgq_t::nd_msg_t *>(msg);
    generic_msg_t *g_msg = reinterpret_cast<generic_msg_t *>(message->get_buffer());

    const auto client_name = nd::platform_specific::FetchMsgClientName(g_msg);

    switch(g_msg->msg_type) {
        case RES_IDLE_REG: {
            LOG_I(kLogTag, "Received RES_IDLE_REG msg from: %s", client_name.c_str());
            HandleIdleEventRegistrationResponse(g_msg);
            break;
        }

        case RES_SPEED_REG: {
            LOG_I(kLogTag, "Received RES_SPEED_REG msg from: %s", client_name.c_str());
            HandleSpeedEventRegistrationResponse(g_msg);
            break;
        }

        default: {
            message_handled = HandleGenericEvent(g_msg);
            if (!message_handled) {
                LOG_I(kLogTag, "%d message remained unhandled in Init State, pushing to queue", g_msg->msg_type);
            }
            break;
        }
    }
    return message_handled;
}

#ifdef ENABLE_VEHICLE_QR_BASED_LOGIN
void BTManager::DoDriverLoginAppQrActivity() {

    do {
        // check if BT is on? installer activity will close bt
        // Read charac
        // Validate
        // Write charac

        // app_map-> mac : name
        std::unordered_map<std::string, std::string> app_map;

        {
            const std::lock_guard<std::mutex> lock(driver_login_app_qr_attr_->data_lock_);
            app_map = std::move(driver_login_app_qr_attr_->data_map_);
        }

        if (app_map.empty()) {
            LOG_I(kLogTag, "No app found in [%s:%d]", __func__, __LINE__);
            break;
        }

        if (!bt_interface_ptr_->IsEnabled()) {
            LOG_I(kLogTag, "BT is not enabled in [%s:%d]", __func__, __LINE__);
            break;
        }

        const std::string vin_data = "vin:" + config_data_->device_vin_;

        bool found_new_app_login = false;

        if (!bt_interface_ptr_->SupportsScanAndConnectionParallelly()) {
            // Turn off Scan
            bt_interface_ptr_->LeScanOff();
        }

        struct DriverAppLoginStatus {
            std::string driver_id_;
            bool login_status_;
        };

        std::vector<DriverAppLoginStatus> app_login_status;

        for (const auto &app : app_map) {

            bool vin_matched = false;

            std::string read_vin;
            std::vector<uint8_t> vin_char_vec;
            if (bt_interface_backup_ptr_->ReadCharacteristicData(app.first, kDriverAppLoginVehicleIdInfo,
                                                          vin_char_vec, service_obj_ptr_)) {
                read_vin = Translator::HexNumberToAscii(vin_char_vec.data(), vin_char_vec.size());
                LOG_I(kLogTag, "Read char success, vin:%s", read_vin.c_str());
            } else {
                LOG_E(kLogTag, "Failed to read vin in [%s:%d]", __func__, __LINE__);
            }

            if (0 == read_vin.compare(0, vin_data.size(), vin_data)) {
                vin_matched = true;
            } else {
                LOG_E(kLogTag, "VIN mismatch");
            }

            std::string read_id;
            std::vector<uint8_t> driver_id_char_vec;
            if (bt_interface_backup_ptr_->ReadCharacteristicData(app.first, kDriverAppLoginDriverInfo,
                                                          driver_id_char_vec, service_obj_ptr_)) {
                read_id = Translator::HexNumberToAscii(driver_id_char_vec.data(), driver_id_char_vec.size());
                LOG_I(kLogTag, "Read char success, driver id: %s", read_id.c_str());
            } else {
                LOG_E(kLogTag, "Failed to read driver id in [%s:%d]", __func__, __LINE__);
            }

            if (read_id.empty()) {
                LOG_E(kLogTag, "Driver id is empty in [%s:%d]", __func__, __LINE__);
            }

            std::string driver_id = "0000";
            const size_t pos = read_id.find("dId:");
            if (std::string::npos != pos) {
                driver_id = kDriverAppLoginDriverIdString;
                driver_id += read_id.substr(pos + 1);
                if (vin_matched) {
                    const auto emplace_status = current_scanned_drivers_.emplace(driver_id, 0);
                    if (emplace_status.second) {
                        found_new_app_login = true;
                    }
                }
            }

            app_login_status.emplace_back(DriverAppLoginStatus{driver_id, vin_matched});

            // const std::string write_data = vin_matched ? "1" : "0";
            const std::vector<std::string> write_data = {(vin_matched ? "1" : "0")};

            if (!bt_interface_backup_ptr_->WriteCharacteristicData(app.first, kDriverAppLoginVehicleIdInfo, write_data,
                                                            nd::interface::AddressType::kRandom,
                                                            service_obj_ptr_)) {
                LOG_E(kLogTag, "Failed to write vin status in [%s:%d]", __func__, __LINE__);
            }
        }

        // Turn scan back on
        bt_interface_ptr_->LeScanOn();

        if (found_new_app_login) {
            if (!file_touch(kDriverAppLoginIndicatorFile)) {
                LOG_E(kLogTag, "Failed to create %s in [%s:%d]", kDriverAppLoginIndicatorFile, __func__, __LINE__);
            }
        }

        for (const auto &app_login : app_login_status) {

            nd::platform_specific::SendDriverLoginUpdate(app_login.login_status_, kBT_Server_MQ_Name,
                                                         kPower_Server_MQ_Name, msg_id_);

            const std::string err_msg = "Driver (" + app_login.driver_id_ + ") - " +
                                        (app_login.login_status_ ? "Login Success" : "Invalid Login Attempt");

            if (!service_obj_ptr_->send_err_msg(SM_E_BTFV_DRIVER_UPDATE,
                                                (app_login.login_status_ ?
                                                 static_cast<int>(DriverAppLoginWithQrCode::kOk) :
                                                 static_cast<int>(DriverAppLoginWithQrCode::kInvalid)),
                                                err_msg.c_str())) {
                LOG_E(kLogTag, "SM_E_BTFV_DRIVER_UPDATE send_err_msg failed");
            }
        }

        if (found_new_app_login) {
            const auto tstamp = CurrentSystemClockMilliSeconds();
            NotifyDriverLoginsToObservers(tstamp);

            {
                BtPersistenceDBHelper::DriverSessionData data{};
                data.start_time_ = driver_session_start_time_;
                data.id_map_ = driver_association_->cached_scanned_drivers_;
                data.do_upload_ = true;

                std::string current_hash;
                {
                    std::lock_guard<std::mutex> session_lock (driver_association_->session_data_.mutex_);
                    data.audio_count_ = driver_association_->session_data_.audio_play_count_;
                    current_hash = driver_association_->session_data_.hash_;
                }

                persistence_ptr_->AddLoginEntry(std::move(data), current_hash);

                // Update ADSM state with new drivers and audio count
                if (adsm_state_mgr_) {
                    adsm_state_mgr_->SetCurrentDrivers(driver_association_->cached_scanned_drivers_);
                    adsm_state_mgr_->SetCurrentHash(GetDLScanHash());
                    adsm_state_mgr_->SetAudioCount(driver_association_->session_data_.audio_play_count_);
                    if (adsm_state_mgr_->SaveState()) {
                        LOG_I(kLogTag, "ADSM state updated with driver login, audio_count=%u",
                              driver_association_->session_data_.audio_play_count_);
                    } else {
                        LOG_E(kLogTag, "Failed to update ADSM state after driver login");
                    }
                }
            }
        }

    } while (false);
}
#endif

void BTManager::NotifyDriverLoginsToObservers() {

    // Notify nd_central

    std::vector<std::string> ids;
    std::vector<std::string> timestamps;
    ids.reserve(10);

    // constexpr char kDriverIdSeparator = ':'; // separator between driver id and app login count

    for (const auto & driver: driver_association_->cached_scanned_drivers_) {
        ids.emplace_back(driver.first);
        timestamps.emplace_back(std::to_string(driver.second));
        if (NUM_MAX_DRIVERS == ids.size()) {
            break;
        }
    }

    nd::platform_specific::SendDriverIdsToObservers(std::move(ids), std::move(timestamps),
                                                    kBT_Server_MQ_Name, kNDCentral_Server_MQ_Name, msg_id_);
}

void BTManager::ReadLegacyLoginCharacteristics() {

    LOG_I(kLogTag, "Inside: %s", __func__);

    const std::lock_guard<std::mutex> lock(scanned_legacy_logins_->data_lock_);
    scanned_legacy_logins_->timestamp_ = CurrentSystemClockMilliSeconds();

    for (auto &client: scanned_legacy_logins_->clients_) {
        if (is_exit_main_loop_set_) {
            LOG_I(kLogTag, "Exit main loop set, hence exiting: %s", __func__);
            break;
        }

        if ((SpecificationType::kServiceID == client.second.info_.type_) ||
            (SpecificationType::kAppleBackgroundServiceID == client.second.info_.type_)) {

            std::vector<uint8_t> char_vec;
            if (bt_interface_backup_ptr_->ReadCharacteristicData(client.first, kDriverLegacyLoginDriverInfo,
                                                          char_vec, service_obj_ptr_)) {
                auto driver_id = Translator::HexNumberToAscii(char_vec.data(), char_vec.size());
                if (nd::utils::StringUtils::IsAlphanumeric(driver_id)) {
                    client.second.id_charac_ = std::move(driver_id);
                } else {
                    LOG_E(kLogTag, "Invalid driver id: %s", driver_id.c_str());
                }
            } else {
                LOG_E(kLogTag, "Failed to read data");
            }
        }
    }
}

void BTManager::ReadEnhancedLegacyLoginCharacteristics() {

    LOG_I(kLogTag, "Inside: %s", __func__);

    std::vector<std::string> scanned_logins_status;

    const std::string &hash_data = config_data_->vehicle_hash_;

    const std::lock_guard<std::mutex> lock(scanned_legacy_logins_->data_lock_);
    scanned_legacy_logins_->timestamp_ = CurrentSystemClockMilliSeconds();

    if (!scanned_legacy_logins_->clients_.empty()) {
        ReloadVehicleData();
    }

    constexpr uint64_t kLoginStatusSpaceReserve = kMacAddressLength
                                                  +  6 /* DID - v12345 */
                                                  +  6 /* - separators */
                                                  + 16 /* hash size*/
                                                  + 10 /* etc. data */;

    for (auto clients_itr = scanned_legacy_logins_->clients_.begin();
         scanned_legacy_logins_->clients_.end() != clients_itr;) {

        bool valid = false;
        std::string login_status;
        login_status.reserve(kLoginStatusSpaceReserve);
        login_status.append(clients_itr->first);

        do {

            if (is_exit_main_loop_set_) {
                LOG_I(kLogTag, "Exit main loop set, hence exiting: %s", __func__);
                break;
            }

            if (!((SpecificationType::kServiceID == clients_itr->second.info_.type_) ||
                  (SpecificationType::kAppleBackgroundServiceID == clients_itr->second.info_.type_))) {

                LOG_E(kLogTag, "Invalid service type: %u in [%s:%d]", static_cast<uint32_t>(clients_itr->second.info_.type_),
                          __func__, __LINE__);
                break;
            }

            login_status += (SpecificationType::kServiceID == clients_itr->second.info_.type_) ? "-FG-" : "-BG-";

            std::unordered_map<std::string, std::vector<uint8_t>> charac_map = {{kVehicleHashInfo, {}},
                                                                                {kDriverLegacyLoginDriverInfo, {}},
                                                                                {kDriverAppLoginTimeInfo, {}}};

            if (!bt_interface_backup_ptr_->ReadCharacteristicData(clients_itr->first, charac_map, service_obj_ptr_)) {
                LOG_W(kLogTag, "Failed to read one/all characteristics in [%s:%d]", __func__, __LINE__);
                login_status.append("RF-");
            } else {
                login_status.append("RS-");
            }

            std::string read_driver_id = Translator::HexNumberToAscii(charac_map[kDriverLegacyLoginDriverInfo].data(),
                                                                             charac_map[kDriverLegacyLoginDriverInfo].size());
            LOG_I(kLogTag, "Read char driver id: %s, size: %llu", read_driver_id.c_str(), read_driver_id.size());

            if (!nd::utils::StringUtils::IsAlphanumeric(read_driver_id)) {
                LOG_E(kLogTag, "Invalid driver id: %s", read_driver_id.c_str());
                read_driver_id.clear();
            }

            const std::string read_hash = Translator::HexNumberToAscii(charac_map[kVehicleHashInfo].data(),
                                                                       charac_map[kVehicleHashInfo].size());
            LOG_I(kLogTag, "Read char hash: %s, size: %llu", read_hash.c_str(), read_hash.size());

            const std::string read_login_time = Translator::HexNumberToAscii(charac_map[kDriverAppLoginTimeInfo].data(),
                                                                             charac_map[kDriverAppLoginTimeInfo].size());
            LOG_I(kLogTag, "Read char login time: %s, size: %llu", read_login_time.c_str(), read_login_time.size());

            login_status += read_driver_id + "-" + read_hash + "-" + read_login_time;

            if (read_driver_id.empty()) {
                LOG_E(kLogTag, "Failed to read driver id from: %s hence removing entry", clients_itr->first.c_str());
                break;
            }

            if (read_hash.empty()) {
                LOG_E(kLogTag, "Failed to read hash from: %s hence removing entry. Is it older Driveri app?", clients_itr->first.c_str());
                break;
            }

            if (read_hash != hash_data) {
                LOG_E(kLogTag, "Hash mismatch for driver: %s, hash_data: %s, read_hash: %s",
                      read_driver_id.c_str(), hash_data.c_str(), read_hash.c_str());
                break;
            } else {
                LOG_I(kLogTag, "Hash matched for driver: %s, hash_data: %s, read_hash: %s",
                      read_driver_id.c_str(), hash_data.c_str(), read_hash.c_str());
            }

            login_status.append("-OK");

            clients_itr->second.id_charac_ = std::move(read_driver_id);
            clients_itr->second.start_time_charac_ = std::move(read_login_time);
            valid = true;

        } while (false);

        if (!valid) {
            clients_itr = scanned_legacy_logins_->clients_.erase(clients_itr);
        } else {
            ++clients_itr;
        }

        scanned_logins_status.emplace_back(std::move(login_status));

        if (scanned_legacy_logins_->clients_.end() != clients_itr) {
            ScanFor(config_data_->scan_time_btw_char_ops_);
        }
    }

    ReportScannedLoginStatusToHealthStats(scanned_logins_status);
}

void BTManager::BeginLegacyLoginActivityIfEnabled() {
    if (HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kLegacy)) {
        InitiateDriverLoginBtActivity();
    }
}

void BTManager::BeginQrLoginActivityIfEnabled(AudioInitiator initiator, const std::string &state) {
    if (HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kDriverQR)) {
        InitiateDriverLoginQrActivity(state);

        if (kDLScanInitiatorStrMap.at(DLScanInitiator::kWoM) != state) {
            LOG_I(kLogTag, "%s: Not WoM initiated, hence starting audio play thread", __func__);
            BeginAudioPlayThread(initiator, state);
        } else {
            LOG_I(kLogTag, "%s: WoM initiated, hence not starting audio play thread", __func__);
        }
    }
}

void BTManager::BeginEnhancedLoginActivityIfEnabled(AudioInitiator initiator, const std::string &state) {
    if (HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kEnhancedBt)) {
        // To begin the driver login activity, disassociate_on_idle must not be set, no drivers must be in cache.
        if ((driver_association_->disassociate_on_idle_) ||
            ((1 == driver_association_->cached_scanned_drivers_.count(kUnassignedDriverAppLoginString)))) {
                InitiateDriverLoginBtActivity();
                BeginAudioPlayThread(initiator, state);
        } else {
            LOG_I(kLogTag, "%s: Conditions not met, hence not initiating login activity", __func__);
        }
    }
}

void BTManager::DisassociateDriver() {

    // Move table data if valid
    const std::string current_hash = GetDLScanHash();
    persistence_ptr_->EndDriverSession(current_hash);

    if (!GetDLScanHash().empty()) {
        const bool is_session_start = false;
        ReportDLSessionStatusToHealthStats(is_session_start);
        ClearDLSession();
    }

    if (driver_association_->notify_on_disassociation_) {
        LOG_I(kLogTag, "Disassociating driver and notifying observers");
        // Store unassigned in db
        driver_association_->notify_on_disassociation_ = false;

        current_scanned_drivers_.clear();
        current_scanned_drivers_.emplace(kUnassignedDriverAppLoginString, kDefaultAppLoginTime);
        driver_association_->cached_scanned_drivers_ = current_scanned_drivers_;
        driver_session_start_time_ = CurrentSystemClockMilliSeconds();
        NotifyDriverLoginsToObservers();

        BtPersistenceDBHelper::DriverSessionData data{};
        data.start_time_ = driver_session_start_time_;
        data.id_map_ = driver_association_->cached_scanned_drivers_;
        data.end_time_ = driver_session_start_time_ + kAddToBtEndTime;
        data.do_upload_ = true;
        // No hash required as it is unasignment call
        persistence_ptr_->AddLoginEntry(std::move(data), "");

        // Clear ADSM state on disassociation
        if (adsm_state_mgr_) {
            adsm_state_mgr_->SetCurrentHash("");
            adsm_state_mgr_->SetCurrentDrivers(driver_association_->cached_scanned_drivers_);
            adsm_state_mgr_->SetAudioCount(0);
            if (adsm_state_mgr_->SaveState()) {
                LOG_I(kLogTag, "ADSM state cleared on driver disassociation");
            } else {
                LOG_E(kLogTag, "Failed to clear ADSM state on disassociation");
            }
        }
    }
}

void BTManager::InitiateDriverLoginQrActivity(const std::string &state) {
    LOG_I(kLogTag, "Entered %s", __func__);

    if (0 == login_registers_set_.count(LoginRegisters::kDriverQr)) {
        if (GetDLScanHash().empty()) {
            SetNewDLScanHash();
            ReportDLSessionStatusToHealthStats(true);

            driver_session_start_time_ = CurrentSystemClockMilliSeconds();

            // Create a new entry in session table
            BtPersistenceDBHelper::DriverSessionData data{};
            data.start_time_ = driver_session_start_time_;
            data.id_map_ = driver_association_->cached_scanned_drivers_;
            data.do_upload_ = false;
            data.audio_count_ = 0;

            const std::string current_hash = GetDLScanHash();
            persistence_ptr_->BeginDriverSession(std::move(data), current_hash);

            // Persist ADSM state for crash recovery
            if (adsm_state_mgr_) {
                adsm_state_mgr_->SetCurrentHash(current_hash);
                adsm_state_mgr_->SetCurrentDrivers(driver_association_->cached_scanned_drivers_);
                if (adsm_state_mgr_->SaveState()) {
                    LOG_I(kLogTag, "ADSM state persisted for QR session: %s", current_hash.c_str());
                } else {
                    LOG_E(kLogTag, "Failed to persist ADSM state for QR session");
                }
            }
        } else {
            LOG_I(kLogTag, "In %s Session already exists, %s not creating new one", __func__, GetDLScanHash().c_str());
        }
        StartDriverLoginQRActivity(state);
        login_registers_set_.emplace(LoginRegisters::kDriverQr);
    } else {
        LOG_I(kLogTag, "In %s QR Filter already registered, not calling again", __func__);
    }
}

void BTManager::InitiateDriverLoginBtActivity() {
    // Initialize the session start time
    LOG_I(kLogTag, "Entered %s", __func__);

    if (HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kEnhancedBt)) {
        if (0 == registered_filter_id_map_.count(FilterKeys::kDriverLoginService)) {
            if (GetDLScanHash().empty()) {
                SetNewDLScanHash();
                ReportDLSessionStatusToHealthStats(true);

                driver_session_start_time_ = CurrentSystemClockMilliSeconds();

                // Create a new entry in session table
                BtPersistenceDBHelper::DriverSessionData data{};
                data.start_time_ = driver_session_start_time_;
                data.id_map_ = driver_association_->cached_scanned_drivers_;
                data.do_upload_ = false;
                data.audio_count_ = 0;

                const std::string current_hash = GetDLScanHash();
                persistence_ptr_->BeginDriverSession(std::move(data), current_hash);

                // Persist ADSM state for crash recovery
                if (adsm_state_mgr_) {
                    adsm_state_mgr_->SetCurrentHash(current_hash);
                    adsm_state_mgr_->SetCurrentDrivers(driver_association_->cached_scanned_drivers_);
                    if (adsm_state_mgr_->SaveState()) {
                        LOG_I(kLogTag, "ADSM state persisted for BT session: %s", current_hash.c_str());
                    } else {
                        LOG_E(kLogTag, "Failed to persist ADSM state for BT session");
                    }
                }
            } else {
                LOG_I(kLogTag, "In %s Session already exists, %s not creating new one", __func__,
                      GetDLScanHash().c_str());
            }
            ReportDLScanStatusToHealthStats(kDLSourceStrMap.at(DLSource::kBT), true, "BT scan started");
        }
    }

    current_scanned_drivers_.clear();

    if (HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kEnhancedBt)) {
        remaining_login_scan_count_ = std::numeric_limits<uint64_t>::max(); // perform continuous scan until a login is found
    } else {
        remaining_login_scan_count_ = config_data_->driver_login_max_retry_ - 1;
    }

    RegisterDriverLoginLegacyFilter();
    StartDriverLoginLegacyAdvertisement();
}

void BTManager::HandleLoginCompleteInIdle() {
    if (HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kEnhancedBt)) {
        DoDriverLoginActivity();
    } else {
        StopDriverLoginBtLegacyDetectionActivity();
    }
}

void BTManager::StopDriverLoginBtLegacyDetectionActivity(bool keep_bt_on) {
    {
        const std::lock_guard<std::mutex> lock(scan_events_timeout_->data_lock_);
        scan_events_timeout_->data_map_.erase(TimeoutEvents::kDriverLoginLegacy);
    }

    if (IsFilterSet(FilterKeys::kDriverLoginService) && bt_interface_ptr_->IsEnabled()) {
        StopDriverLoginLegacyAdvertisement();
    }

    ClearDriverLoginLegacyFilters();

    if (!keep_bt_on) {
        StopScanDisableBT();
    }
}

void BTManager::HandleQrLoginUpdate() {

    LOG_I(kLogTag, "Entered %s", __func__);

    do {

        if (!HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kDriverQR)) {
            LOG_I(kLogTag, "Driver QR login feature is not enabled, ignoring update");
            break;
        }

        if (!is_qr_scan_required_) {
            LOG_W(kLogTag, "QR scan is not required, still processing");
        }

        driver_association_->notify_on_disassociation_ = true;
        vehicle_state_attr_->had_vehicle_moved_out_of_idle_post_login_ = false;

        PlayLoginAcknowledgeAudio(true);
        ClearLoginAudioPrompt();

        std::unordered_map<std::string, int64_t> new_qr_logins;

        {
            std::lock_guard<std::mutex> lock(driver_qr_logins_->data_lock_);
            new_qr_logins.reserve(driver_qr_logins_->ids_.size());
            for (const auto &id : driver_qr_logins_->ids_) {
                new_qr_logins.emplace(id, driver_qr_logins_->timestamp_);
            }
        }

        // LOG all new_qr_logins
        for (const auto &id : new_qr_logins) {
            LOG_I(kLogTag, "QR Login Driver ID: %s, Timestamp: %lld", id.first.c_str(), id.second);
        }

        // If a driver already exists and a new driver logs in now then new driver's session starts from this point
        if (HasDriverLoggedIn()) {
            LOG_I(kLogTag, "Previous driver exists");
            driver_session_start_time_ = CurrentSystemClockMilliSeconds();
        }

        driver_association_->cached_scanned_drivers_.clear(); // TODO(sunil.s): clearing this, check if it is fine
        driver_association_->cached_scanned_drivers_.insert(new_qr_logins.begin(), new_qr_logins.end());

        NotifyDriverLoginsToObservers();

        {
            BtPersistenceDBHelper::DriverSessionData data{};
            data.start_time_ = driver_session_start_time_;
            data.id_map_ = driver_association_->cached_scanned_drivers_;
            data.end_time_ = driver_session_start_time_ + kAddToBtEndTime;
            data.do_upload_ = true;

            std::string current_hash;
            {
                std::lock_guard<std::mutex> session_lock (driver_association_->session_data_.mutex_);
                data.audio_count_ = driver_association_->session_data_.audio_play_count_;
                current_hash = driver_association_->session_data_.hash_;
            }

            persistence_ptr_->AddLoginEntry(std::move(data), current_hash);
        }

        // Save ADSM state with updated driver information
        if (adsm_state_mgr_) {
            adsm_state_mgr_->SetCurrentDrivers(driver_association_->cached_scanned_drivers_);
            adsm_state_mgr_->SetCurrentHash(GetDLScanHash());
            if (adsm_state_mgr_->SaveState()) {
                LOG_I(kLogTag, "ADSM state saved after driver association");
            } else {
                LOG_E(kLogTag, "Failed to save ADSM state after driver association");
            }
        }

        std::vector<std::string> drv_ids;
        drv_ids.reserve(driver_association_->cached_scanned_drivers_.size());

        for_each(driver_association_->cached_scanned_drivers_.begin(), driver_association_->cached_scanned_drivers_.end(),
                    [&drv_ids](const auto &drv_data) {
                        drv_ids.emplace_back(drv_data.first);
                    });

        ReportDriveAssignmentToHealthStats(std::move(drv_ids), kDLSourceStrMap.at(DLSource::kQR));

    } while (false);
}

void BTManager::ProcessDriverLoginQrData(const std::string &qr_data) {

    // json string to receive :
    // {
    //     "data": [
    //         {"Driverid": "driver_12345"},
    //         {"Driverid": "driver_67890"}
    //     ],
    //     "login_time": 1697055600000
    // }

    do {

        if (!HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kDriverQR)) {
            LOG_I(kLogTag, "Driver login kind is not QR, ignoring update");
            break;
        }

        if (!is_qr_scan_required_) {
            LOG_W(kLogTag, "QR scan is not required, still processing");
        }

        std::unordered_set<std::string> new_qr_logins;
        int64_t login_time = 0;

        json_error_t error{};
        json_t *root = json_loads(qr_data.c_str(), 0, &error);
        if (NULL != root) {
            const json_t *data_array = json_object_get(root, "data");
            const json_t *login_time_json = json_object_get(root, "login_time");

            if ((nullptr != data_array) && (json_is_array(data_array))) {
                size_t index;
                json_t *value;

                json_array_foreach(data_array, index, value) {
                    // Extract Driverid from each array element
                    const json_t *driver_id_json = json_object_get(value, "Driverid");
                    if (NULL != driver_id_json) {
                        const char *driver_id_cstr = json_string_value(driver_id_json);
                        if (nullptr != driver_id_cstr) {
                            if (nd::utils::StringUtils::IsAlphanumeric(driver_id_cstr)) {
                                const std::string id = kDriverAppLoginDriverIdString + std::string(driver_id_cstr);
                                new_qr_logins.emplace(std::move(id));
                            } else {
                                LOG_W(kLogTag, "Driverid is not alphanumeric in array element %s", driver_id_cstr);
                            }
                        } else {
                            LOG_W(kLogTag, "Driverid is not a string in array element %zu", index);
                        }
                    } else {
                        LOG_W(kLogTag, "\"Driverid\" key not found in array element %zu", index);
                    }
                }
            } else {
                LOG_W(kLogTag, "\"data\" key is not an array in QR data: %s", qr_data.c_str());
            }

            if ((nullptr != login_time_json) && json_is_integer(login_time_json)) {
                login_time = json_integer_value(login_time_json);
            } else {
                LOG_W(kLogTag, "\"login_time\" key is %s in QR data: %s",
                      (nullptr == login_time_json) ? "missing" : "not an integer", qr_data.c_str());
            }

            json_decref(root);

        } else {
            LOG_W(kLogTag, "Error parsing JSON payload! line %d, column %d: %s",
                error.line, error.column, error.text);
            break;
        }

        if (0 >= login_time) {
            login_time = CurrentSystemClockMilliSeconds();
            LOG_W(kLogTag, "Invalid login time received, using current time: %lld", login_time);
        }

        if (new_qr_logins.empty()) {
            LOG_W(kLogTag, "No valid QR logins found, ignoring update");
            break;
        }

        std::unordered_set<std::string> cached_qr_logins;

        {
            std::lock_guard<std::mutex> lock(driver_qr_logins_->data_lock_);
            cached_qr_logins = driver_qr_logins_->ids_;
        }

        vehicle_state_attr_->had_vehicle_moved_out_of_idle_post_login_ = false;

        if (cached_qr_logins == new_qr_logins) {
            // NOTE: Ignoring to limit the multiple db and cloud updates
            LOG_I(kLogTag, "No change in QR logins, ignoring update");

            static auto last_audio_play_time = int64_t{0}; // static, store last audio time

            const auto now = CurrentSteadyClockSeconds();

            // NOTE: User may still be holding QR for longer time, so playing audio again. (10 secs diff)
            // To improve user experience, replay the QR login acknowledgment audio if the user continues holding the QR code for an extended period.
            // This ensures that if the user missed or did not hear the initial audio prompt, a repeated notification is provided after a timeout (kQrLoginAckAudioIntervalSecs).

            if ((0 == last_audio_play_time) || (now > last_audio_play_time + kQrLoginAckAudioIntervalSecs)) {
                last_audio_play_time = now;
                nd::platform_specific::SendInternalEventMessage("BTFV_INTERNAL_EVENT", kBT_Server_MQ_Name, msg_id_,
                                                                static_cast<int32_t>(InternalEvents::kQrLoginAckAudio));
            }

            break;
        }

        {
            std::lock_guard<std::mutex> lock(driver_qr_logins_->data_lock_);
            driver_qr_logins_->ids_ = std::move(new_qr_logins);
            driver_qr_logins_->timestamp_ = login_time;
        }

        nd::platform_specific::SendInternalEventMessage("BTFV_INTERNAL_EVENT", kBT_Server_MQ_Name, msg_id_,
                                                        static_cast<int32_t>(InternalEvents::kQrLogins));

    } while (false);

}

void BTManager::UnSubscribeToQrLogins() {
    LOG_I(kLogTag, "Entered %s", __func__);

    try {
        stop_qr_messenger_ = true;

        if (qr_client_builder_ptr_) {
            LOG_I(kLogTag, "Shutting down ZMQ context to unblock thread...");
            NDMessenger::ClientBuilder* builder = static_cast<NDMessenger::ClientBuilder*>(qr_client_builder_ptr_.get());
            builder->shutdownContext();
        }

        if (qr_messenger_client_th_.joinable()) {
            LOG_I(kLogTag, "Joining QR Messenger thread...");
            qr_messenger_client_th_.join();
            LOG_I(kLogTag, "QR Messenger thread joined successfully");
        } else {
            LOG_I(kLogTag, "QR Messenger thread not joinable, skipping join");
        }

        if (qr_client_builder_ptr_) {
            qr_client_builder_ptr_.reset(nullptr);
            LOG_I(kLogTag, "QR client builder destroyed");
        }

    } catch (const std::system_error &e) {
        LOG_W(kLogTag, "[%s:%d:%s] Thread operation failed: %s (code=%d)", __FILE__, __LINE__, __func__,
              e.what(), e.code().value());
    } catch (const std::exception &e) {
        LOG_W(kLogTag, "[%s:%d:%s] Exception during cleanup: %s", __FILE__, __LINE__, __func__, e.what());
    }

    LOG_I(kLogTag, "Exiting %s", __func__);
}

void BTManager::SubscribeToQrLogins() {

    do {
        if (qr_messenger_client_th_.joinable()) {
            LOG_I(kLogTag, "QR Messenger thread already running");
            break;
        }

        // Create client builder if not already created
        if (!qr_client_builder_ptr_) {
            constexpr int linger_value = 0; // immediate close
            qr_client_builder_ptr_.reset(new (std::nothrow) NDMessenger::ClientBuilder(linger_value));
        }

        if (!qr_client_builder_ptr_) {
            LOG_E(kLogTag, "Failed to create QR Messenger client builder");
            break;
        }

        NDMessenger::ClientBuilder* builder = static_cast<NDMessenger::ClientBuilder*>(qr_client_builder_ptr_.get());

        if (!builder) {
            LOG_E(kLogTag, "Failed to create QR Messenger client builder");
            break;
        }

        try {
            builder->setServer(config_data_->qr_messenger_socket_);
            builder->setTopic(config_data_->qr_messenger_topic_);
        } catch (const std::exception &e) {
            LOG_W(kLogTag, "[%s:%d:%s] Exception during ClientBuilder : %s", __FILE__, __LINE__, __func__, e.what());
            break;
        }

        if (!builder->isConnected()) {
            LOG_W(kLogTag, "Failed to connect QR Messenger client");
            break;
        }

        LOG_I(kLogTag, "QR Messenger client connected");

        stop_qr_messenger_ = false;

        try {
            // Ensure no existing thread is joinable before assignment
            if (qr_messenger_client_th_.joinable()) {
                LOG_W(kLogTag, "[%s:%d] QR thread already joinable, joining before new assignment", __FILE__, __LINE__);
                qr_messenger_client_th_.join();
            }

            qr_messenger_client_th_ = std::thread([this, builder]() {
                LOG_I(kLogTag, "QR Messenger listener thread started");

                while (!stop_qr_messenger_ && !is_exit_main_loop_set_) {
                    try {
                        std::string qr_decoded_data = builder->subscribe(); // 5 minute default timeout

                        if (qr_decoded_data.empty()) {
                            continue;
                        }

                        LOG_I(kLogTag, "Received QR decoded data from QR Messenger: %s", qr_decoded_data.c_str());

                        ProcessDriverLoginQrData(qr_decoded_data);
                    } catch (const zmq::error_t& e) {
                        // Expected when context.shutdown() is called
                        LOG_I(kLogTag, "QR Messenger subscribe interrupted by shutdown: %s", e.what());
                        break;
                    }
                }

                LOG_I(kLogTag, "QR Messenger listener thread exiting");
            });
        } catch (const std::system_error &e) {
            LOG_W(kLogTag, "[%s:%d:%s] Thread creation failed: %s (code=%d)", __FILE__, __LINE__, __func__, e.what(), e.code().value());
            break;
        } catch (const std::exception &e) {
            LOG_W(kLogTag, "[%s:%d:%s] Exception during thread creation: %s", __FILE__, __LINE__, __func__, e.what());
            break;
        }
    } while (false);
}

bool BTManager::StartDriverLoginQRActivity(const std::string &state) {
    bool status = false;

    if (is_qr_scan_allowed_) {
        is_qr_scan_required_ = true;

        SubscribeToQrLogins();

        if (!is_qr_scan_active_) {
            status = nd::platform_specific::SendStartQRScanMsg(config_data_->qr_login_tags_,
                                                            kBT_Server_MQ_Name,
                                                            kNDCentral_Server_MQ_Name,
                                                            msg_id_);
            if (!status) {
                RegisterStartQrMsgRetry();
            } else {
                if (!file_touch(GetQrScanStartIndicatorPath())) {
                    LOG_E(kLogTag, "Failed to create %s in [%s:%d]", GetQrScanStartIndicatorPath().c_str(), __func__, __LINE__);
                }
            }

            const std::string msg = (status ? "QR scan start requested" : "QR scan start request failed") +
                                    (" : " + state);

            ReportDLScanStatusToHealthStats(kDLSourceStrMap.at(DLSource::kQR), true, msg);

        } else {
            status = true; // already active
            LOG_I(kLogTag, "%s QR scan already active", __func__);
        }

        LOG_I(kLogTag, "%s QR scan req send status : %s", __func__, status ? "true" : "false");

    } else {
        LOG_W(kLogTag, "%s QR scan not allowed, hence not starting QR scan", __func__);
        const std::string msg = "QR scan not allowed : " + state;
        ReportDLScanStatusToHealthStats(kDLSourceStrMap.at(DLSource::kQR), false, msg);
    }

    return status;
}

bool BTManager::StopDriverLoginQRActivity(const std::string &state, bool turn_scan_off) {
    bool status = false;

    ClearDLRegister(LoginRegisters::kDriverQr);

    if (turn_scan_off) {
        is_qr_scan_required_ = false;

        // send stop message
        if ((is_qr_scan_active_) || (FileSystemHandler::FileExists(GetQrScanStartIndicatorPath()))) {
            status = nd::platform_specific::SendStopQRScanMsg(kBT_Server_MQ_Name,
                                                              kNDCentral_Server_MQ_Name,
                                                              msg_id_);
            is_qr_scan_active_ = false;

            const std::string msg = (status ? "QR scan stop requested" : "QR scan stop request failed") + (" : " + state);

            ReportDLScanStatusToHealthStats(kDLSourceStrMap.at(DLSource::kQR), false, msg);

            FileSystemHandler::DeleteFile(GetQrScanStartIndicatorPath());

        } else {
            status = true; // already stopped
            LOG_D(kLogTag, "%s QR scan already inactive", __func__);
        }

    } else {
        status = true;
        LOG_D(kLogTag, "%s QR scan turn off not requested", __func__);
    }

    {
        // clear cache
        std::lock_guard<std::mutex> lock(driver_qr_logins_->data_lock_);
        driver_qr_logins_->ids_.clear();
        driver_qr_logins_->timestamp_ = 0;
    }

    return status;
}

void BTManager::ClearLoginAudioPrompt() {
    remaining_login_audio_prompt_count_ = 0;
    StopAudioPlayLoop();
}

void BTManager::StopAudioPlayLoop() {
    if (0 < login_audio_play_th_.native_handle()) {
        LOG_I(kLogTag, "Waiting for Audio Loop Thread to join");
        stop_audio_loop_ = true;
        {
            std::unique_lock<std::mutex> lock(login_audio_play_mutex_);
        }
        login_audio_play_cv_.notify_all();
        if (login_audio_play_th_.joinable()) {
            login_audio_play_th_.join();
            LOG_I(kLogTag, "Audio Loop Thread joined");
        }
    } else {
        // already stopped?
        LOG_I(kLogTag, "Looks like thread is already stopped or not running in %s", __func__);
    }
}

std::pair<BTManager::AssociationCallStatus, std::string> BTManager::DoesAssociationExistInCloud() {

    AssociationCallStatus association_status = AssociationCallStatus::kCallFailure;
    std::string call_err;
    constexpr uint32_t kMaxAssociationRetryAttempts = 1;
    const long kCurlMaxConnectTimeout = 5L;
    const long kCurlMaxTimeout = 10L;

    ReloadVehicleData();

    const std::string association_end_point = "devices/driverLoginStatus/" + config_data_->device_id_;

    const std::string data = "{\"version\": \"" + config_data_->ota_version_ + "\"" +
                               ",\"v_hash\": \"" + config_data_->vehicle_hash_ + "\"" +
                               ",\"device_action\": \"" + ((States::kMotion == current_state_) ? "motion" : "idle") + "\"}";

    json_t *root = nullptr;
    uint32_t retry_count = 0;

    do {

        nd::utils::CurlHelper curl;
        curl.SetMaxConnectTimeout(kCurlMaxConnectTimeout).SetMaxTimeout(kCurlMaxTimeout);
        const auto status_ptr = curl.Call(association_end_point, data);

        if (!status_ptr) {
            LOG_E(kLogTag, "status_ptr null");
            break;
        }

        call_err = std::to_string(status_ptr->code_) + " : "
                   + std::to_string(status_ptr->resp_code_) + " : "
                   + status_ptr->error_str_;

        bool response = false;
        json_error_t error;
        root = json_loads(status_ptr->response_string_.c_str(), 0, &error);

        if (NULL != root) {
            const json_t *call_json_resp = json_object_get(root, "response");
            if (NULL != call_json_resp) {
                response = json_boolean_value(call_json_resp);
            } else {
                LOG_E(kLogTag, "\"response\" key not found in curl response");
            }

        } else {
            LOG_E(kLogTag, "Error parsing JSON payload! line %d, column %d: %s",
                  error.line, error.column, error.text);
        }

        constexpr long kCurlCallSuccessResponseCode = 200;

        if ((CURLE_OK == status_ptr->code_) &&
            (kCurlCallSuccessResponseCode == status_ptr->resp_code_) &&
            (response)) {

            LOG_I(kLogTag, "Response success for session active query");

            json_t *data_section = json_object_get(root, "data");

            if (NULL == data_section) {
                LOG_E(kLogTag, "data key not present in json although response is 200");
                if (!service_obj_ptr_->send_err_msg(SM_E_BTFV_DL_INVALID_ASSOCIATION_PARAMS, 1, "No data section")) {
                    LOG_E(kLogTag, "SM_E_BTFV_DL_INVALID_ASSOCIATION_PARAMS send_err_msg failed");
                }
                break;
            }

            const json_t *call_json_session = json_object_get(data_section, "active_session");
            if (NULL != call_json_session) {
                association_status = (1 == json_integer_value(call_json_session)) ? AssociationCallStatus::kAssociated :
                                                                                    AssociationCallStatus::kDisassociated;
            } else {
                LOG_E(kLogTag, "\"active_session\" key not found in curl response");
                if (!service_obj_ptr_->send_err_msg(SM_E_BTFV_DL_INVALID_ASSOCIATION_PARAMS, 2, "No active_session key")) {
                    LOG_E(kLogTag, "SM_E_BTFV_DL_INVALID_ASSOCIATION_PARAMS send_err_msg failed");
                }
            }

            const json_t *call_json_driver = json_object_get(data_section, "driver_id");
            if (NULL != call_json_driver) {
                const char *driver_id = json_string_value(call_json_driver);
                const std::string driver_id_str = (nullptr == driver_id ? "null" : driver_id);
                LOG_I(kLogTag, "\"driver_id\" : %s", driver_id_str.c_str());
                ReportDriveAssignmentToHealthStats({driver_id_str}, kDLSourceStrMap.at(DLSource::kCloud));
            } else {
                LOG_E(kLogTag, "\"driver_id\" key not found in curl response");
            }

            break;
        } else {
            LOG_E(kLogTag, "Response failed for session active query");
        }

        if ((CURLE_COULDNT_RESOLVE_PROXY == status_ptr->code_) ||
            (CURLE_COULDNT_RESOLVE_HOST == status_ptr->code_) ||
            (CURLE_COULDNT_CONNECT == status_ptr->code_)) {

            LOG_E(kLogTag, "Connectivity error?");
        }

        if (kMaxAssociationRetryAttempts > 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(kMaxDelayBetweenAssociationRetries));
        }

        ++retry_count;

        if (nullptr != root) {
            json_decref(root);
        }

    } while (kMaxAssociationRetryAttempts > retry_count);

    if (nullptr != root) {
        json_decref(root);
    }

    return {association_status, call_err};
}

void BTManager::RunAudioPlayLoop(AudioInitiator initiator, std::string trigger_state) {
    LOG_I(kLogTag, "Entered %s", __func__);

    auto last_login_audio_played_time = 0;

    //NOTE: Remember "No internet, no audio" unless forced in case of BT Driver Login

    bool is_first_audio = true;

    while (!stop_audio_loop_) {

        auto last_association_check_start_time = CurrentSteadyClockSeconds();
        uint32_t retry_count = 0;

        auto association_status = AssociationCallStatus::kCallFailure;

        const auto interval_to_check = is_first_audio ? config_data_->driver_login_first_audio_interval_ :
                                                        config_data_->driver_login_audio_interval_;

        while (((CurrentSteadyClockSeconds() < (interval_to_check + last_association_check_start_time))) && (!stop_audio_loop_)) {

            if (0 == remaining_login_audio_prompt_count_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(kMaxDelayBetweenAssociationRetries));
                continue;
            }

            // Not Enhanced BT Driver Login, no need to check for internet for playing audio
            if (!HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kEnhancedBt)) {
                break;
            }

            constexpr bool suppress_info_logs = true;

            if (check_internet_exist(suppress_info_logs)) {

                const auto association_pair_status = DoesAssociationExistInCloud();

                association_status = association_pair_status.first;

                if (AssociationCallStatus::kAssociated == association_status) {
                    LOG_I(kLogTag, "Association exists in cloud, hence clearing audio counts");
                    remaining_login_audio_prompt_count_ = 0;
                    break;
                } else if (AssociationCallStatus::kDisassociated == association_status) {
                    LOG_I(kLogTag, "Association does not exist in cloud");
                    break;
                } else {
                    LOG_E(kLogTag, "Failed to get association status, will retry again, current count: %u", retry_count);
                    const std::string err_msg = "Cloud fail although internet exists" + association_pair_status.second;
                    if (!service_obj_ptr_->send_err_msg(SM_E_BTFV_DL_INVALID_ASSOCIATION_PARAMS, 2, err_msg)) {
                        LOG_E(kLogTag, "SM_E_BTFV_DL_INVALID_ASSOCIATION_PARAMS send_err_msg failed");
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(kMaxDelayBetweenAssociationRetries));
                }

            } else {
                LOG_I(kLogTag, "No internet, will retry again, current count: %u", retry_count);
                std::this_thread::sleep_for(std::chrono::milliseconds(kMaxDelayBetweenAssociationRetries));
            }

            ++retry_count;
        }

        std::unique_lock<std::mutex> lock(login_audio_play_mutex_);

        const auto elapsed_time = CurrentSteadyClockSeconds() - last_association_check_start_time;

        if (login_audio_play_cv_.wait_for(lock, std::chrono::seconds(((elapsed_time >= interval_to_check) ||
                                                                      (AssociationCallStatus::kCallFailure == association_status) ||
                                                                      (AssociationCallStatus::kAssociated == association_status) ||
                                                                      (is_first_audio)) ? 1 :
                                                                     interval_to_check - elapsed_time),
                                          [&] {
            const auto now = CurrentSteadyClockSeconds();
            return ((((interval_to_check <= (now - last_login_audio_played_time)) ||
                      (is_first_audio)) &&
                     (0 < remaining_login_audio_prompt_count_)) ||
                    (stop_audio_loop_));
        })) {

            // NOTE: Add code comments for below logic
            if (HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kEnhancedBt)) {
                if ((!stop_audio_loop_) && ((AssociationCallStatus::kCallFailure == association_status) ||
                                            ((AssociationCallStatus::kDisassociated == association_status) &&
                                             (!is_first_audio)))) {
                    LOG_E(kLogTag, "Calling once more");
                    const auto association_pair_status = DoesAssociationExistInCloud();
                    association_status = association_pair_status.first;
                }
            } else {
                association_status = AssociationCallStatus::kDisassociated;
            }

            if (AssociationCallStatus::kAssociated == association_status) {
                LOG_I(kLogTag, "Association exists in cloud, hence clearing audio counts");
                remaining_login_audio_prompt_count_ = 0;
            } else {

                if (!stop_audio_loop_) {

                    if (remaining_login_audio_prompt_count_ > 0) {
                        --remaining_login_audio_prompt_count_;
                    }

                    const std::string state = ((States::kMotion == current_state_) ? "motion" : "idle");
                    const std::string desc = trigger_state + "-" + std::to_string(remaining_login_audio_prompt_count_);

                    if ((AssociationCallStatus::kDisassociated == association_status) ||
                        (config_data_->driver_login_force_first_audio_play_ && is_first_audio)) {

                        last_login_audio_played_time = CurrentSteadyClockSeconds();
                        nd::platform_specific::SendDriverLoginAudioNotification(config_data_->login_audio_file_,
                                                                                kBT_Server_MQ_Name, kPower_Server_MQ_Name,
                                                                                msg_id_);
                        uint32_t audio_count = 0;
                        std::string current_hash;
                        {
                            std::lock_guard<std::mutex> session_lock (driver_association_->session_data_.mutex_);
                            audio_count = ++(driver_association_->session_data_.audio_play_count_);
                            current_hash = driver_association_->session_data_.hash_;
                        }
                        ReportDriveAudioInfoToHealthStats(state, desc, true);
                        persistence_ptr_->UpdateDriverSessionAudioCount(audio_count, current_hash);

                        // Update ADSM state with new audio count
                        if (adsm_state_mgr_) {
                            adsm_state_mgr_->SetAudioCount(audio_count);
                            if (!adsm_state_mgr_->SaveState()) {
                                LOG_E(kLogTag, "Failed to update ADSM state after audio play");
                            }
                        }

                    } else {
                        const uint64_t count = remaining_login_audio_prompt_count_;
                        LOG_E(kLogTag, "Skipping audio play, remaining count: %u", count);
                        {
                            std::lock_guard<std::mutex> session_lock (driver_association_->session_data_.mutex_);
                            ++(driver_association_->session_data_.audio_skip_count_);
                        }
                        ReportDriveAudioInfoToHealthStats(state, desc, false);
                    }

                    is_first_audio = false;
                }
            }
        }
    }
    LOG_I(kLogTag, "Exiting %s", __func__);
}

bool BTManager::BeginAudioPlayThread(AudioInitiator initiator, const std::string &state) {

    bool status = false;
    if (config_data_->enable_login_audio_reminder_) {
        if (!stop_audio_loop_) {
            LOG_I(kLogTag, "Audio Loop has already begun");
            status = true;
        } else {
            stop_audio_loop_ = false;
            try {
                login_audio_play_th_ = std::thread(&BTManager::RunAudioPlayLoop, this, initiator, state);
                status = true;
            } catch (const std::system_error &e) {
                LOG_E(kLogTag, "[%s:%d] System error what(): %s", __FUNCTION__, __LINE__, e.what());
            } catch (...) {
                LOG_E(kLogTag, "[%s:%d] Caught an exception of an undetermined type", __FUNCTION__, __LINE__);
            }
        }
    } else {
        LOG_I(kLogTag, "Login audio reminder is disabled");
        status = true;
    }

    if (!status) {
        StopAudioPlayLoop();
    }

    return status;
}

void BTManager::SetNewDLScanHash() {

    std::string new_hash;

    if ((HasDriverLoggedIn()) && (adsm_state_mgr_) && (adsm_state_mgr_->GetCurrentHash().empty())) {
        LOG_W(kLogTag, "Driver already logged in, not setting new DL session hash, probabyly returning from crash?");
        new_hash = adsm_state_mgr_->GetCurrentHash();
        LOG_I(kLogTag, "Set DL session hash from adsm: %s", new_hash.c_str());
    } else {
        std::vector<unsigned char> rbytes(kRandomDriveHashSize);

        // Generate random key
        unsigned long err_code = 0;

        if (nd::security::RandomBytes::Get(rbytes, err_code)) {
            // convert to hex
            new_hash = Translator::BytesToHexString(rbytes.data(), rbytes.size());
            LOG_I(kLogTag, "New DL session hash: %s", new_hash.c_str());
        } else {
            LOG_E(kLogTag, "RandomBytes::Get() failed");
        }
    }

    if (!new_hash.empty()) {
        std::lock_guard<std::mutex> lock (driver_association_->session_data_.mutex_);
        driver_association_->session_data_.hash_ = new_hash;
    } else {
        LOG_C(kLogTag, "New DL session hash is empty");
    }

}

void BTManager::ClearDLSession() {
    LOG_I(kLogTag, "Clearing DL session");
    std::lock_guard<std::mutex> lock (driver_association_->session_data_.mutex_);
    driver_association_->session_data_.hash_.clear();
    driver_association_->session_data_.audio_play_count_ = 0;
    driver_association_->session_data_.audio_skip_count_ = 0;
}

std::string BTManager::GetDLScanHash() {
    std::lock_guard<std::mutex> lock (driver_association_->session_data_.mutex_);
    return driver_association_->session_data_.hash_;
}

void BTManager::DoDriverLoginActivity() {
    //NOTE: Heavy duty function, add checkpoints for graceful exit
    do {

        if (!bt_interface_ptr_->IsEnabled()) {
            LOG_I(kLogTag, "BT is not enabled in [%s:%d]", __func__, __LINE__);
            break;
        }

        const bool keep_bt_on = true;
        StopDriverLoginBtLegacyDetectionActivity(keep_bt_on);

        if (!bt_interface_ptr_->SupportsScanAndConnectionParallelly()) {
            // Turn off Scan
            bt_interface_ptr_->LeScanOff();
        }

        // Read characteristics

        if (HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kEnhancedBt)) {
            //NOTE: Reloading just in case there is an update from cloud
            ReadEnhancedLegacyLoginCharacteristics();
        } else if (HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kLegacy)) {
            ReadLegacyLoginCharacteristics();
        } else {}

        if (is_exit_main_loop_set_) {
            LOG_I(kLogTag, "Exit main loop set, hence exiting: %s", __func__);
            break;
        }

        // Turn scan back on
        bt_interface_ptr_->LeScanOn();

        uint64_t scan_timestamp{};

        current_scanned_drivers_.clear();
        {
            const std::lock_guard<std::mutex> lock(scanned_legacy_logins_->data_lock_);
            scan_timestamp = scanned_legacy_logins_->timestamp_;

            // Handling time jump issues
            if (scan_timestamp < driver_session_start_time_) {
                scan_timestamp = driver_session_start_time_;
            }

            LOG_I(kLogTag, "Current scan_timestamp: %llu", scan_timestamp);
            for (auto &client: scanned_legacy_logins_->clients_) {
                switch(client.second.info_.type_) {
                    case SpecificationType::kServiceID:
                    case SpecificationType::kAppleBackgroundServiceID: {
                        if (!client.second.id_charac_.empty()) {
                            const std::string id = kDriverAppLoginDriverIdString + client.second.id_charac_;
                            int64_t start_time = -1;

                            if (!client.second.start_time_charac_.empty()) {
                                const std::string start_time_str = client.second.start_time_charac_;
                                LOG_I(kLogTag, "Added [%s] [%s] [%s]", client.first.c_str(), id.c_str(),
                                      start_time_str.c_str());

                                if (!string_to_int64(start_time_str, start_time)) {
                                    LOG_W(kLogTag, "Failed to convert start time to int64_t");
                                    start_time = -1;
                                }

                            } else {
                                LOG_W(kLogTag, "start_time_charac_ empty");
                            }

                            current_scanned_drivers_.emplace(std::move(id), start_time);

                        } else {
                            LOG_W(kLogTag, "id_charac_ empty");
                        }
                        break;
                    }
                    case SpecificationType::kEddyStoneUID: {
                        auto & serv_data = client.second.info_.service_data_;
                        if (!serv_data.empty()) {
                            const std::vector<uint8_t> driver_id(serv_data.end() - 4, serv_data.end());
                            const std::string id = Translator::BytesToHexString(driver_id.data(), driver_id.size());
                            LOG_I(kLogTag, "Added %s: %s", client.first.c_str(), id.c_str());
                            current_scanned_drivers_.emplace(std::move(id), kDefaultAppLoginTime);
                        } else {
                            LOG_E(kLogTag, "serv_data empty");
                        }
                        break;
                    }
                    default: {
                        LOG_E(kLogTag, "Unknown specification: %u in %s", static_cast<uint32_t>(client.second.info_.type_), __FUNCTION__);
                        break;
                    }
                }
            }
        }

        for (const auto &driver: current_scanned_drivers_) {
            LOG_I(kLogTag, "Detected driver: %s", driver.first.c_str());
        }

        if (!current_scanned_drivers_.empty()) {
            if (driver_association_->cached_scanned_drivers_ != current_scanned_drivers_) {
                // Cache the scanner drivers
                driver_association_->cached_scanned_drivers_ = current_scanned_drivers_;
                NotifyDriverLoginsToObservers();

                {
                    BtPersistenceDBHelper::DriverSessionData data{};
                    data.start_time_ = driver_session_start_time_;
                    data.id_map_ = driver_association_->cached_scanned_drivers_;
                    data.end_time_ = scan_timestamp + kAddToBtEndTime;
                    data.do_upload_ = true;

                    std::string current_hash;
                    {
                        std::lock_guard<std::mutex> session_lock (driver_association_->session_data_.mutex_);
                        data.audio_count_ = driver_association_->session_data_.audio_play_count_;
                        current_hash = driver_association_->session_data_.hash_;
                    }

                    persistence_ptr_->AddLoginEntry(std::move(data), current_hash);
                }

                // Save ADSM state with updated driver information
                if (adsm_state_mgr_) {
                    adsm_state_mgr_->SetCurrentDrivers(driver_association_->cached_scanned_drivers_);
                    adsm_state_mgr_->SetCurrentHash(GetDLScanHash());
                    if (adsm_state_mgr_->SaveState()) {
                        LOG_I(kLogTag, "ADSM state saved after driver association");
                    } else {
                        LOG_E(kLogTag, "Failed to save ADSM state after driver association");
                    }
                }

                if (HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kEnhancedBt)) {
                    std::vector<std::string> drv_ids;
                    drv_ids.reserve(current_scanned_drivers_.size());

                    for_each(current_scanned_drivers_.begin(), current_scanned_drivers_.end(),
                             [&drv_ids](const auto &drv_data) {
                                 drv_ids.emplace_back(drv_data.first);
                             });

                    ReportDriveAssignmentToHealthStats(std::move(drv_ids), kDLSourceStrMap.at(DLSource::kBT));
                }
            } else {
                LOG_I(kLogTag, "No new drivers detected");
            }
            // Valid driver is associated, so on disassociation notify and write to DB
            driver_association_->notify_on_disassociation_ = true;
        }

        if (HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kEnhancedBt) &&
            (0 == driver_association_->cached_scanned_drivers_.count(kUnassignedDriverAppLoginString))) {
            // Driver identified, no need to perform scans further.
            remaining_login_scan_count_ = 0;
            ClearLoginAudioPrompt();
        }

        if (remaining_login_scan_count_ > 0) {
            LOG_I(kLogTag, "Login Scan iteration count: %llu", remaining_login_scan_count_);
            --remaining_login_scan_count_;
            RegisterDriverLoginLegacyFilter();
            StartDriverLoginLegacyAdvertisement();
        } else {
            LOG_I(kLogTag, "Login Scan iterations completed");
            StopScanDisableBT();
        }

    } while (false);
}

bool BTManager::UpdatePrivacyStatusToObservers() {
    bool status = false;

#ifdef ENABLE_PRIVACY_BT
    status =  nd::platform_specific::SendPrivacyModeToObservers(vehicle_state_attr_->is_privacy_mode_active_,
                                                             kBT_Server_MQ_Name, kNDCentral_Server_MQ_Name, msg_id_);
#endif

    return status;
}

bool BTManager::VerifyAndTransitTo(States state, std::shared_ptr<nd::interface::IState> &state_ptr) {
    bool status = false;

    do {
        if (state == current_state_) {
            LOG_I(kLogTag, "Already in the requested state: %d", static_cast<int32_t>(current_state_));
            status = true;
            break;
        }

        if (!current_state_ptr_->MeetsExitCriteria()) {
            LOG_E(kLogTag, "Current State's(%d) exit criteria not met", static_cast<int32_t>(current_state_));
            break;
        }

        if (!state_ptr->MeetsEntryCriteria()) {
            LOG_E(kLogTag, "Destination State's(%d) entry criteria not met", static_cast<int32_t>(state));
            break;
        }

        current_state_ptr_->ActionOnExit();
        state_ptr->ActionOnEntry();
        current_state_ptr_ = state_ptr;
        current_state_ = state;
        status = true;
    } while (false);

    if (!status) {
        LOG_E(kLogTag, "Transition fail");
    }

    return status;
}

bool BTManager::MoveToState(States state) {
    LOG_I(kLogTag, "Current state is %d", static_cast<int32_t>(current_state_));

    bool status = false;

    switch(state) {
        case States::kInit: {
            LOG_I(kLogTag, "Request received to move to Init state");
            status = VerifyAndTransitTo(state, init_state_ptr_);
            break;
        }
        case States::kIdle: {
            LOG_I(kLogTag, "Request received to move to Idle state");
            status = VerifyAndTransitTo(state, idle_state_ptr_);
            break;
        }
        case States::kMotion: {
            LOG_I(kLogTag, "Request received to move to Motion state");
            status = VerifyAndTransitTo(state, motion_state_ptr_);
            break;
        }
        case States::kError: {
            LOG_I(kLogTag, "Request received to move to Init state");
            status = VerifyAndTransitTo(state, error_state_ptr_);
            break;
        }
        default: {
            LOG_E(kLogTag, "Unknown state move request received: %d", static_cast<int32_t>(state));
            break;
        }
    }

    return status;
}

bool BTManager::UpdateEngineIdleStatusToObservers() {
    return nd::platform_specific::SendEngineIdleStatusToObservers(vehicle_state_attr_->engine_idle_on_,
                                                                  kBT_Server_MQ_Name, kNDCentral_Server_MQ_Name,
                                                                  msg_id_);
}

bool BTManager::UpdateIdleFrStatusToObservers() {
    return nd::platform_specific::SendFrStatusToObservers(!vehicle_state_attr_->idle_fr_status_, kBT_Server_MQ_Name,
                                                 kNDCentral_Server_MQ_Name, msg_id_);
}

bool BTManager::StartDriverLoginLegacyAdvertisement() {
    LOG_I(kLogTag, "Starting Driver Login Advertisement");

    const bool force_enable = true;
    EnableBTStartScan(force_enable);

    bool status = false;

    bool on_relaxed_scan = false;
    {
        const std::lock_guard<std::mutex> lock(scan_events_timeout_->data_lock_);
        on_relaxed_scan = scan_events_timeout_->data_map_.count(TimeoutEvents::kRelaxedScan) > 0;
    }

    if (!on_relaxed_scan) {
        const auto advertise_bytes = Translator::HexStringToBytes(kDriverLegacyLoginBeaconUuid);
        constexpr int32_t kAdvertiseMajor = 0x007B; // -> 123
        constexpr int32_t kAdvertiseMinor = 0x01C8; // -> 456
        constexpr int8_t kAdvertiseRssi = 0xC8;
        status = bt_interface_ptr_->StartBeaconAdvertising(kDriverLoginLegacyBeaconInterval, advertise_bytes,
                                                           kAdvertiseMajor, kAdvertiseMinor, kAdvertiseRssi);

        if (!status) {
            LOG_W(kLogTag, "Continue to register for login timeout, the next iteration should retry the advertisement");
        }

        {
            const std::lock_guard<std::mutex> lock(scan_events_timeout_->data_lock_);
            auto const now = CurrentSteadyClockSeconds();
            scan_events_timeout_->data_map_[TimeoutEvents::kDriverLoginLegacy] = now + kDriverLoginLegacyBroadcastDuration;
        }
    } else {
        LOG_I(kLogTag, "Relaxed scan is already on, not starting advertisement");
    }

    return status;
}

bool BTManager::StopDriverLoginLegacyAdvertisement() {
    LOG_I(kLogTag, "Stopping Driver Login Advertisement");

    return bt_interface_ptr_->StopBeaconAdvertising();
}

void BTManager::HandleIdleEventUpdateInMotionState(void *g_msg) {

    int handle{};
    bool idle_on{};

    if (nd::platform_specific::FetchIdleEventUpdate(g_msg, handle, idle_on)) {
        if ((handle == speed_services_->idle_engine_handle_) &&
            (-1 < speed_services_->idle_engine_handle_)) {

            LOG_I(kLogTag, "Idle Update received for Engine Idle in motion state with idle status: %d", idle_on);

            vehicle_state_attr_->engine_idle_on_ = idle_on;

            if (idle_on) {
                LOG_I(kLogTag, "Vehicle is in Engine Idle");
            } else {
                LOG_I(kLogTag, "Vehicle is out of Engine Idle");
            }
            UpdateEngineIdleStatusToObservers();

        } else if ((handle == speed_services_->idle_login_handle_) &&
                   (-1 < speed_services_->idle_login_handle_)) {

            LOG_I(kLogTag, "Idle Update received for Driver Login in motion state with idle status: %d", idle_on);

            vehicle_state_attr_->driver_login_idle_on_ = idle_on;

            if ((IGNITION_ON == vehicle_state_attr_->ign_status_) || (vehicle_state_attr_->wake_up_status_)) {

                if (idle_on) {
                    LOG_I(kLogTag, "Vehicle is in Driver Login Idle");

                    const bool has_bt_login = (HasFeature(config_data_->driver_login_features_,
                                                        DriverLoginFeature::kLegacy) ||
                                               HasFeature(config_data_->driver_login_features_,
                                                        DriverLoginFeature::kEnhancedBt));

                    const bool has_qr_login = HasFeature(config_data_->driver_login_features_,
                                                        DriverLoginFeature::kDriverQR);

                    if (has_bt_login || has_qr_login) {

                        if (has_bt_login) {
                            StopDriverLoginBtLegacyDetectionActivity();
                        }

                        if (has_qr_login) {
                            // NOTE: We are not stopping the QR scan at nd-central, this will just be a change of session
                            StopDriverLoginQRActivity(kDLScanInitiatorStrMap.at(DLScanInitiator::kOnIdle), false);
                        }

                        if (driver_association_->disassociate_on_idle_ &&
                            vehicle_state_attr_->had_vehicle_moved_out_of_idle_post_login_) {
                            ClearLoginAudioPrompt();
                            DisassociateDriver();
                            LOG_I(kLogTag, "Driver logins cleared");
                        }

                        if (has_qr_login) {
                            BeginQrLoginActivityIfEnabled(AudioInitiator::kQr,
                                                        kDLScanInitiatorStrMap.at(DLScanInitiator::kOnIdle));
                        }
                    }

#ifdef ENABLE_VEHICLE_QR_BASED_LOGIN
                    if (config_data_->driver_app_login_qr_vehicle_idle_enabled_) {
                        if (!file_delete(kDriverAppLoginIndicatorFile)) {
                            LOG_E(kLogTag, "Failed to delete %s in [%s:%d]", kDriverAppLoginIndicatorFile, __func__, __LINE__);
                        }
                        driver_session_start_time_ = CurrentSystemClockMilliSeconds();
                        RegisterDriverLoginAppQRFilter();
                    }
#endif

                    vehicle_state_attr_->does_current_speed_meets_login_threshold_ = false;

                    MoveToState(States::kIdle);

                } else {
                    LOG_I(kLogTag, "Vehicle is out of Driver Login Idle");

                    if (driver_association_->play_audio_on_out_of_idle_) {
                        TriggerDriverLoginAudio();
                    }

                    BeginEnhancedLoginActivityIfEnabled(AudioInitiator::kBt,
                                                        kDLScanInitiatorStrMap.at(DLScanInitiator::kOnOutOfIdle));
                }

            } else {
                LOG_I(kLogTag, "Ignoring as Ignition is not ON and no wake up event state");
            }

        } else if ((handle == speed_services_->idle_login_fr_handle_) &&
                   (-1 < speed_services_->idle_login_fr_handle_)) {

            LOG_I(kLogTag, "Idle Update received for Fr Login in motion state with idle status: %d", idle_on);

            vehicle_state_attr_->idle_fr_status_ = idle_on;

            if (idle_on) {
                LOG_I(kLogTag, "Vehicle is in Driver FR login Idle");
            } else {
                LOG_I(kLogTag, "Vehicle is out of Driver FR login Idle");
            }
            UpdateIdleFrStatusToObservers();

        } else if ((handle == speed_services_->idle_privacy_handle_) &&
            (-1 < speed_services_->idle_privacy_handle_)) {

            LOG_I(kLogTag, "Idle Update received for privacy mode in motion state with idle status: %d", idle_on);

            if (idle_on) {
                LOG_I(kLogTag, "Privacy mode ON");
                vehicle_state_attr_->is_privacy_mode_active_ = true;

                UpdatePrivacyStatusToObservers();
            }

        } else if ((handle == speed_services_->idle_qr_handle_) &&
                   (-1 < speed_services_->idle_qr_handle_)) {

            LOG_I(kLogTag, "Idle Update received for QR resume Login in motion state with idle status: %d", idle_on);

            vehicle_state_attr_->qr_login_idle_on_ = idle_on;

            if ((IGNITION_ON == vehicle_state_attr_->ign_status_) || (vehicle_state_attr_->wake_up_status_)) {
                if (HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kDriverQR)) {

                    if (idle_on) {
                        LOG_I(kLogTag, "Vehicle is in QR resume Login Idle");

                        if (HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kDriverQR)) {
                            InitiateDriverLoginQrActivity(kDLScanInitiatorStrMap.at(DLScanInitiator::kOnQrResumeIdle));
                        }

                    } else {
                        LOG_I(kLogTag, "Vehicle is out of QR resume Login Idle");
                        StopDriverLoginQRActivity(kDLScanInitiatorStrMap.at(DLScanInitiator::kOnOutOfQrResumeIdle));
                        vehicle_state_attr_->had_vehicle_moved_out_of_idle_post_login_ = true;
                    }

                } else {
                    LOG_W(kLogTag, "QR Driver Login feature not enabled, this was not intended");
                }
            } else {
                LOG_I(kLogTag, "Ignoring as Ignition is not ON and no wake up event state");
            }

        } else {
            LOG_E(kLogTag, "Unknown Idle Update received in motion state with handle: %d with idle status: %d",
                handle, idle_on);
        }
    }
}

void BTManager::HandleSpeedEventUpdateInMotionState(void *g_msg) {

    int handle{};
    int speed{};

    if (nd::platform_specific::FetchSpeedEventUpdate(g_msg, handle, speed)) {

        if ((handle == speed_services_->speed_login_handle_) &&
            (-1 < speed_services_->speed_login_handle_)) {

            LOG_I(kLogTag, "Speed Update received for driver login in motion state with speed: %d", speed);
            vehicle_state_attr_->does_current_speed_meets_login_threshold_ = true;

        } else if ((handle == speed_services_->speed_privacy_handle_) &&
                   (-1 < speed_services_->speed_privacy_handle_)) {

            vehicle_state_attr_->is_privacy_mode_active_ = false;

            LOG_I(kLogTag, "Speed Update received for privacy mode in motion state with speed: %d", speed);
            LOG_I(kLogTag, "Privacy mode OFF");
            UpdatePrivacyStatusToObservers();

        } else if ((handle == speed_services_->speed_qr_handle_) &&
                   (-1 < speed_services_->speed_qr_handle_)) {

            LOG_I(kLogTag, "Speed Update received for QR resume login in motion state with speed: %d", speed);

            vehicle_state_attr_->had_vehicle_moved_out_of_idle_post_login_ = true;

            if (HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kDriverQR)) {
                if (!vehicle_state_attr_->qr_login_idle_on_) {
                    StopDriverLoginQRActivity(kDLScanInitiatorStrMap.at(DLScanInitiator::kMotion));
                } else {
                    LOG_I(kLogTag, "Vehicle is still in QR resume Login Idle, not stopping QR activity");
                }
            } else {
                LOG_W(kLogTag, "QR Driver Login feature not enabled, this was not intended");
            }

        } else {
            LOG_E(kLogTag, "Unknown Speed Update received in motion state with handle: %d with speed: %d",
                  handle, speed);
        }
    }
}

void BTManager::HandleIdleEventUpdateInIdleState(void *g_msg) {

    int handle{};
    bool idle_on{};

    if (nd::platform_specific::FetchIdleEventUpdate(g_msg, handle, idle_on)) {

        if ((handle == speed_services_->idle_engine_handle_) &&
            (-1 < speed_services_->idle_engine_handle_)) {

            LOG_I(kLogTag, "Idle Update received for Engine Idle in idle state with idle status: %d", idle_on);
            vehicle_state_attr_->engine_idle_on_ = idle_on;

            if (idle_on) {
                LOG_I(kLogTag, "Vehicle is in Engine Idle");
            } else {
                LOG_I(kLogTag, "Vehicle is out of Engine Idle");
            }
            UpdateEngineIdleStatusToObservers();

        } else if ((handle == speed_services_->idle_login_handle_) &&
                   (-1 < speed_services_->idle_login_handle_)) {

            LOG_I(kLogTag, "Idle Update received for Driver Login in idle state with idle status: %d", idle_on);

            vehicle_state_attr_->driver_login_idle_on_ = idle_on;

            if ((IGNITION_ON == vehicle_state_attr_->ign_status_) || (vehicle_state_attr_->wake_up_status_)) {

                if (idle_on) {
                    LOG_I(kLogTag, "Vehicle is in Driver Login Idle");

                    const bool has_bt_login = (HasFeature(config_data_->driver_login_features_,
                                                        DriverLoginFeature::kLegacy) ||
                                               HasFeature(config_data_->driver_login_features_,
                                                        DriverLoginFeature::kEnhancedBt));

                    const bool has_qr_login = HasFeature(config_data_->driver_login_features_,
                                                        DriverLoginFeature::kDriverQR);

                    if (has_bt_login || has_qr_login) {

                        if (has_bt_login) {
                            StopDriverLoginBtLegacyDetectionActivity();
                        }

                        if (has_qr_login) {
                            // NOTE: We are not stopping the QR scan at nd-central, this will just be a change of session
                            StopDriverLoginQRActivity(kDLScanInitiatorStrMap.at(DLScanInitiator::kOnIdle), false);
                        }

                        if (driver_association_->disassociate_on_idle_ &&
                            vehicle_state_attr_->had_vehicle_moved_out_of_idle_post_login_) {
                            ClearLoginAudioPrompt();
                            DisassociateDriver();
                            LOG_I(kLogTag, "Driver logins cleared");
                        }

                        if (has_qr_login) {
                            BeginQrLoginActivityIfEnabled(AudioInitiator::kQr,
                                                        kDLScanInitiatorStrMap.at(DLScanInitiator::kOnIdle));
                        }
                    }

                } else {
                    LOG_I(kLogTag, "Vehicle is out of Driver Login Idle");

                    if (driver_association_->play_audio_on_out_of_idle_) {
                        TriggerDriverLoginAudio();
                    }

                    BeginEnhancedLoginActivityIfEnabled(AudioInitiator::kBt,
                                                        kDLScanInitiatorStrMap.at(DLScanInitiator::kOnOutOfIdle));
                }
            } else {
                LOG_I(kLogTag, "Ignoring as Ignition is not ON and no wake up event state");
            }

        } else if ((handle == speed_services_->idle_login_fr_handle_) &&
                   (-1 < speed_services_->idle_login_fr_handle_)) {

            LOG_I(kLogTag, "Idle Update received for Fr Login in idle state with idle status: %d", idle_on);
            vehicle_state_attr_->idle_fr_status_ = idle_on;

            if (idle_on) {
                LOG_I(kLogTag, "Vehicle is in Driver FR login Idle");
            } else {
                LOG_I(kLogTag, "Vehicle is out of Driver FR login Idle");
            }
            UpdateIdleFrStatusToObservers();

        } else if ((handle == speed_services_->idle_privacy_handle_) &&
                   (-1 < speed_services_->idle_privacy_handle_)) {

            LOG_I(kLogTag, "Idle Update received for privacy mode in idle state with idle status: %d", idle_on);
            if (idle_on) {
                LOG_I(kLogTag, "Privacy mode ON");
                vehicle_state_attr_->is_privacy_mode_active_ = true;

                UpdatePrivacyStatusToObservers();
            }

        } else if ((handle == speed_services_->idle_qr_handle_) &&
                   (-1 < speed_services_->idle_qr_handle_)) {

            LOG_I(kLogTag, "Idle Update received for QR resume Login in idle state with idle status: %d", idle_on);

            vehicle_state_attr_->qr_login_idle_on_ = idle_on;

            if ((IGNITION_ON == vehicle_state_attr_->ign_status_) || (vehicle_state_attr_->wake_up_status_)) {
                if (HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kDriverQR)) {

                    if (idle_on) {
                        LOG_I(kLogTag, "Vehicle is in QR resume Login Idle");

                        if (HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kDriverQR)) {
                            InitiateDriverLoginQrActivity(kDLScanInitiatorStrMap.at(DLScanInitiator::kOnQrResumeIdle));
                        }

                    } else {
                        LOG_I(kLogTag, "Vehicle is out of QR resume Login Idle");
                        StopDriverLoginQRActivity(kDLScanInitiatorStrMap.at(DLScanInitiator::kOnOutOfQrResumeIdle));
                        vehicle_state_attr_->had_vehicle_moved_out_of_idle_post_login_ = true;
                    }

                } else {
                    LOG_W(kLogTag, "QR Driver Login feature not enabled, this was not intended");
                }
            } else {
                LOG_I(kLogTag, "Ignoring as Ignition is not ON and no wake up event state");
            }

        } else {
            LOG_E(kLogTag, "Unknown Idle Update received in idle state with handle: %d with idle status: %d",
                handle, idle_on);
        }
    }
}

// bool BTManager::CanExitIdleState() {
//     bool status = false;

//     do {
//         {
//             const std::lock_guard<std::mutex> lock(scan_events_timeout_->data_lock_);
//             if (0 != scan_events_timeout_->data_map_.count(TimeoutEvents::kHotspotActive)) {
//                 LOG_I(kLogTag, "Hotspot is still on from previous installer detection");
//                 break;
//             }
//         }

//         {
//             const std::lock_guard<std::mutex> lock(installer_app_attr_->data_lock_);
//             if (!installer_app_attr_->mac_address_.empty()) {
//                 LOG_I(kLogTag, "Installer has been detected: %s:%s", installer_app_attr_->name_.c_str(),
//                       installer_app_attr_->mac_address_.c_str());
//                 break;
//             }
//         }
//         status = true;
//     } while (false);
//     return status;
// }

void BTManager::HandleSpeedEventUpdateInIdleState(void *g_msg) {

    int handle{};
    int speed{};

    if (nd::platform_specific::FetchSpeedEventUpdate(g_msg, handle, speed)) {
        if ((handle == speed_services_->speed_login_handle_) &&
            (-1 < speed_services_->speed_login_handle_)) {

            LOG_I(kLogTag, "Speed Update received for driver login in idle state with speed: %d", speed);
            vehicle_state_attr_->does_current_speed_meets_login_threshold_ = true;

            MoveToState(States::kMotion);

        } else if ((handle == speed_services_->speed_privacy_handle_) &&
                   (-1 < speed_services_->speed_privacy_handle_)) {

            vehicle_state_attr_->is_privacy_mode_active_ = false;

            LOG_I(kLogTag, "Speed Update received for privacy mode in idle state with speed: %d", speed);
            LOG_I (kLogTag,"Privacy mode OFF");
            UpdatePrivacyStatusToObservers();

        } else if ((handle == speed_services_->speed_qr_handle_) &&
                   (-1 < speed_services_->speed_qr_handle_)) {

            LOG_I(kLogTag, "Speed Update received for QR resume login in idle state with speed: %d", speed);

            vehicle_state_attr_->had_vehicle_moved_out_of_idle_post_login_ = true;

            if (HasFeature(config_data_->driver_login_features_, DriverLoginFeature::kDriverQR)) {
                if (!vehicle_state_attr_->qr_login_idle_on_) {
                    StopDriverLoginQRActivity(kDLScanInitiatorStrMap.at(DLScanInitiator::kMotion));
                } else {
                    LOG_I(kLogTag, "Vehicle is still in QR resume Login Idle, not stopping QR activity");
                }
            } else {
                LOG_W(kLogTag, "QR Driver Login feature not enabled, this was not intended");
            }
        } else {
            LOG_E(kLogTag, "Unknown Speed Update received in idle state with handle: %d with speed: %d",
                  handle, speed);
        }
    }
}

} // namespace device

} // namespace nd
