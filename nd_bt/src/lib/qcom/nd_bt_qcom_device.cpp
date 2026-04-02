/* Copyright (C) 2022 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, October 2022
 */
#include <cstring>
#include <errno.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <thread>
#include <unordered_set>

#include <hardware/bluetooth.h>
#include <hardware/hardware.h>

#include <Gatt.hpp>

#include <hardware/bt_gatt_client.h>
#include <hardware/bt_gatt_server.h>

#include <nd_bt_qcom_device.h>
#include <nd_utils.h>
#include <nd_task.h>

#include <log.h>

namespace nd {

namespace device {

using nd::utils::Translator;

// 31 + 31 for advertising data and scan response. This is the maximum length
static constexpr size_t kScanRecordLength = 62;
static const char *const kLogTag = "QCOM";
static constexpr size_t kMaxConnectDevices = 4;

namespace qcom_utils {
    bool CopyUUID(bt_uuid_t *uuid, const std::vector<uint8_t> &desired_uuid);
    std::string GetMacAddressAsString(const bt_bdaddr_t& bda);
    int HexCharToInt(char c);
    bool GetMacAddrFromString(const std::string& str, bt_bdaddr_t* bda);
    std::string FetchAlphanumericString(const std::string &input);
    size_t GetAdvDataLength(uint8_t *bytes);
    // std::vector ConcatenateVectors
}

int qcom_utils::HexCharToInt(char c) {
    if (isdigit(c)) {
        return c - '0';
    } else {
        return toupper(c) - 'A' + 10;
    }
}

bool qcom_utils::GetMacAddrFromString(const std::string& str, bt_bdaddr_t* bda) {

    bool status = false;
    if (17 == str.size()) {
        bool invalid_data = false;
        for (unsigned int pos = 0; pos < 6; ++pos) {
            char c1 = str[pos * 3];
            char c2 = str[(pos * 3) + 1];
            if (!isxdigit(c1) || !isxdigit(c2)) {
                invalid_data = true;
                break;
            }
            bda->address[pos] = (HexCharToInt(c1) << 4) | HexCharToInt(c2);
        }
        if (!invalid_data) {
            status = true;
        }
    }

    return status;
}

std::string qcom_utils::FetchAlphanumericString(const std::string &input) {
    std::string out;
    std::copy_if (input.begin(), input.end(), std::back_inserter(out), [](char c) {
                                                                           return isalnum(c);
                                                                       });
    return out;
}

bool qcom_utils::CopyUUID(bt_uuid_t *uuid_dest, const std::vector<uint8_t> &desired_uuid) {
    bool status = false;
    constexpr size_t kUuidSize = 16;
    constexpr uint8_t kRandomUuid = 0x30;
    if (nullptr != uuid_dest) {
        if ((!desired_uuid.empty()) && (kUuidSize <= desired_uuid.size())) {
            for (unsigned int pos = 0; pos < kUuidSize; ++pos) {
                uuid_dest->uu[pos] = desired_uuid[pos];
            }
        } else {
            // Random UUID
            for (unsigned int pos = 0; pos < kUuidSize; ++pos) {
                uuid_dest->uu[pos] = kRandomUuid;
            }
        }
        status = true;
    }

    return status;
}

std::string qcom_utils::GetMacAddressAsString(const bt_bdaddr_t& bda) {
    static const char hex_chars[] = "0123456789ABCDEF";

    std::string mac;
    mac.reserve(17);
    for (unsigned int pos = 0; pos < 6; ++pos) {
        mac.push_back(hex_chars[(bda.address[pos] >> 4) & 0xF]);
        mac.push_back(hex_chars[bda.address[pos] & 0xF]);
        if (pos < 5) {
            mac.push_back(':');
        }
    }
    return mac;
}

// Returns the length of the given scan record array. We have to calculate this
// based on the maximum possible data length and the TLV data. See TODO above
// |kScanRecordLength|.
size_t qcom_utils::GetAdvDataLength(uint8_t *bytes) {
    for (size_t pos = 0, field_len = 0; pos < kScanRecordLength; pos += (field_len + 1)) {
        field_len = bytes[pos];

        // Assert here that the data returned from the stack is correctly formatted
        // in TLV form and that the length of the current field won't exceed the
        // total data length.
        if (!(pos + field_len < kScanRecordLength)) {
            LOG_E(kLogTag, "[%s:%d]Data error", __func__, __LINE__);
        }

        // If the field length is zero and we haven't reached the maximum length,
        // then we have found the length, as the stack will pad the data with zeros
        // accordingly.
        if (field_len == 0) {
            return pos;
        }
    }

    // We have reached the end.
    return kScanRecordLength;
}

QcomDevice::QcomDevice(): device_impl_ptr_(std::make_unique<QcomDeviceImpl>()) {
    if (nullptr == device_impl_ptr_) {
        LOG_E(kLogTag, "[%s:%d]Failed to create QcomDeviceImpl", __func__, __LINE__);
    }
};

QcomDevice::~QcomDevice() = default;

class QcomDevice::QcomDeviceImpl {
 public:
    QcomDeviceImpl();
    QcomDeviceImpl(const QcomDeviceImpl &) = delete;
    QcomDeviceImpl(QcomDeviceImpl &&) = delete;
    QcomDeviceImpl& operator=(const QcomDeviceImpl&) = delete;
    QcomDeviceImpl& operator=(QcomDeviceImpl&&) = delete;
    ~QcomDeviceImpl();

    bool DeinitializeGatt();
    bool Disable();
    bool Enable();
    bool InitializeGatt();
    bool IsEnabled();
    bool IsLeScanOn();
    bool LeScanOff();
    bool LeScanOn();
    bool LoadBtStack();
    bool RegisterLeEventCallback(nd::interface::LeEventCb cb);
    bool ReadCharacteristicData(const std::string &mac_addr, const std::string &uuid_str, std::vector<uint8_t> &result_out);
    bool ReadCharacteristicData(const std::string &mac_addr, std::unordered_map<std::string, std::vector<uint8_t>> &charac_data_out);
    bool StartBeaconAdvertising(int advertising_interval, const std::vector<uint8_t> &advertising_uuid, int major_number,
                                int minor_number, int8_t rssi_value);
    bool StartDiscovery();
    bool StartServiceAdvertising(const std::string &advertise_name, const std::vector<uint8_t> &advertise_uuid,
                                 const std::string &advertise_data, uint32_t duration);
    bool StopBeaconAdvertising();
    bool StopDiscovery();
    bool StopServiceAdvertising();
    bool WriteCharacteristicData(const std::string& mac_addr, const std::string &uuid_str,
                                 const std::vector<std::string> &data, nd::interface::AddressType addr_type);
    bool RegisterServer();
    bool AddService(const std::vector<uint8_t> &char_uuid);
    bool RegisterClient();
    bool UnregisterClient();
    bool SetAdvertisementData(int advertising_interval, const std::vector<uint8_t> &data);
    void DisconnectExistingDevices();
    bool ToggleAdvertise(bool start);
    bool ToggleScan(bool start);
    bool Connect(const bt_bdaddr_t *addr, int &conn_id_out);
    bool Disconnect(const bt_bdaddr_t *addr, int conn_id);
    bool GetGattDb(int conn_id);
    bool SearchService(int conn_id);
    bool WriteCharacteristic(const std::string &data, int conn_id);
    bool ReadCharacteristic(std::vector<uint8_t> &data_out, int conn_id);
    void DeInit();
    bool Init();
    void UnLoadBtStack();

    // Droid OS Callbacks
    static bool SetWakeAlarm(uint64_t delay_millis, bool should_wake, alarm_cb cb, void *data);
    static int AcquireWakeLock(const char *lock_name);
    static int ReleaseWakeLock(const char *lock_name);

    // BT Callbacks
    static void AdapterStateChangeCallback(bt_state_t state);
    static void AdapterPropertiesCb(bt_status_t status, int num_properties, bt_property_t *properties);
    static void RemoteDevicePropertiesCb(bt_status_t status, bt_bdaddr_t *bd_addr, int num_properties,
                                         bt_property_t *properties);
    static void DeviceFoundCb(int num_properties, bt_property_t *properties);
    static void DiscoveryStateChangedCb(bt_discovery_state_t state);
    static void PinRequestCb(bt_bdaddr_t *bd_addr, bt_bdname_t *bd_name, uint32_t cod, bool min_16_digit);
    static void SspRequestCb(bt_bdaddr_t *bd_addr, bt_bdname_t *bd_name, uint32_t cod, bt_ssp_variant_t pairing_variant,
                             uint32_t pass_key);
    static void BondStateChangedCb(bt_status_t status, bt_bdaddr_t *bd_addr, bt_bond_state_t state);
    static void AclStateChangedCb(bt_status_t status, bt_bdaddr_t *bd_addr, bt_acl_state_t state);
    static void CbThreadEvent(bt_cb_thread_evt event);
    static void DutModeRecvCb (uint16_t opcode, uint8_t *buf, uint8_t len);
    static void LeTestModeRecvCb (bt_status_t status, uint16_t packet_count);
    static void HciRawEventShow(uint8_t event_code, uint8_t *buf, uint8_t len);
    static bool GetValueFromPropertyList(int num_properties,bt_property_t *properties,
                                         bt_property_type_t type, void* dest);

    /** BT-GATT Server callback structure. */

    /** Callback invoked in response to register_server */
    static void RegisterServerCb(int status, int server_if, bt_uuid_t *app_uuid);
    /** Callback indicating that a remote device has connected or been disconnected */
    static void ConnectionCb(int conn_id, int server_if, int connected, bt_bdaddr_t *bda);

    /** Callback invoked in response to create_service */
    static void ServiceAddedCb(int status, int server_if, btgatt_srvc_id_t *srvc_id, int srvc_handle);

    /** Callback indicating that an included service has been added to a service */
    static void IncludedServiceAddedCb(int status, int server_if, int srvc_handle, int incl_srvc_handle);

    /** Callback invoked when a characteristic has been added to a service */
    static void CharacteristicAddedCb(int status, int server_if, bt_uuid_t *uuid, int srvc_handle, int char_handle);

    /** Callback invoked when a descriptor has been added to a characteristic */
    static void DescriptorAddedCb(int status, int server_if, bt_uuid_t *uuid, int srvc_handle, int descr_handle);

    /** Callback invoked in response to start_service */
    static void ServiceStartedCb(int status, int server_if, int srvc_handle);

    /** Callback invoked in response to stop_service */
    static void ServiceStoppedCb(int status, int server_if, int srvc_handle);

    /** Callback triggered when a service has been deleted */
    static void ServiceDeletedCb(int status, int server_if, int srvc_handle);

    /**
     * Callback invoked when a remote device has requested to read a characteristic
     * or descriptor. The application must respond by calling send_response
     */
    static void RequestReadCb(int conn_id, int trans_id, bt_bdaddr_t *bda, int attr_handle, int offset, bool is_long);

    /**
     * Callback invoked when a remote device has requested to write to a
     * characteristic or descriptor.
     */
    static void RequestWriteCb(int conn_id, int trans_id, bt_bdaddr_t *bda,
                                       int attr_handle, int offset, int length,
                                       bool need_rsp, bool is_prep, uint8_t* value);

    /** Callback invoked when a previously prepared write is to be executed */
    static void RequestExecWriteCb(int conn_id, int trans_id, bt_bdaddr_t *bda, int exec_write);

    /**
     * Callback triggered in response to send_response if the remote device
     * sends a confirmation.
     */
    static void ResponseConfirmationCb(int status, int handle);

    /**
     * Callback confirming that a notification or indication has been sent
     * to a remote device.
     */
    static void IndicationSentCb(int conn_id, int status);

    /**
     * Callback notifying an application that a remote device connection is currently congested
     * and cannot receive any more data. An application should avoid sending more data until
     * a further callback is received indicating the congestion status has been cleared.
     */
    static void ServerConnectionCongestionCb(int conn_id, bool congested);

    /** Callback invoked when the MTU for a given connection changes */
    static void MtuChangedCb(int conn_id, int mtu);

    /*==========================================================================================*/

    /** BT-GATT Client callback structure. */

    /** Callback invoked in response to register_client */
    static void RegisterClientCb(int status, int client_if, bt_uuid_t *app_uuid);

    /** Callback for scan results */
    static void ScanResultCb(bt_bdaddr_t* bda, int rssi, uint8_t* adv_data);

    /** GATT open callback invoked in response to open */
    static void ConnectCb(int conn_id, int status, int client_if, bt_bdaddr_t* bda);

    /** Callback invoked in response to close */
    static void DisconnectCb(int conn_id, int status, int client_if, bt_bdaddr_t* bda);

    /**
     * Invoked in response to search_service when the GATT service search
     * has been completed.
     */
    static void SearchCompleteCb(int conn_id, int status);

    /** Callback invoked in response to [de]register_for_notification */
    static void RegisterForNotificationCb(int conn_id, int registered, int status, uint16_t handle);

    /**
     * Remote device notification callback, invoked when a remote device sends
     * a notification or indication that a client has registered for.
     */
    static void NotifyCb(int conn_id, btgatt_notify_params_t *p_data);

    /** Reports result of a GATT read operation */
    static void ReadCharacteristicCb(int conn_id, int status, btgatt_read_params_t *p_data);

    /** GATT write characteristic operation callback */
    static void WriteCharacteristicCb(int conn_id, int status, uint16_t handle);

    /** GATT execute prepared write callback */
    static void ExecuteWriteCb(int conn_id, int status);

    /** Callback invoked in response to read_descriptor */
    static void ReadDescriptorCb(int conn_id, int status, btgatt_read_params_t *p_data);

    /** Callback invoked in response to write_descriptor */
    static void WriteDescriptorCb(int conn_id, int status, uint16_t handle);

    /** Callback triggered in response to read_remote_rssi */
    static void ReadRemoteRssiCb(int client_if, bt_bdaddr_t* bda, int rssi, int status);

    /**
     * Callback indicating the status of a listen() operation
     */
    static void ListenCb(int status, int server_if);

    /** Callback invoked when the MTU for a given connection changes */
    static void ConfigureMtuCb(int conn_id, int status, int mtu);

    /** Callback invoked when a scan filter configuration command has completed */
    static void ScanFilterCfgCb(int action, int client_if, int status, int filt_type, int avbl_space);

    /** Callback invoked when scan param has been added, cleared, or deleted */
    static void ScanFilterParamCb(int action, int client_if, int status, int avbl_space);

    /** Callback invoked when a scan filter configuration command has completed */
    static void ScanFilterStatusCb(int enable, int client_if, int status);

    /** Callback invoked when multi-adv enable operation has completed */
    static void MultiAdvEnableCb(int client_if, int status);

    /** Callback invoked when multi-adv param update operation has completed */
    static void MultiAdvUpdateCb(int client_if, int status);

    /** Callback invoked when multi-adv instance data set operation has completed */
    static void MultiAdvDataCb(int client_if, int status);

    /** Callback invoked when multi-adv disable operation has completed */
    static void MultiAdvDisableCb(int client_if, int status);

    /**
     * Callback notifying an application that a remote device connection is currently congested
     * and cannot receive any more data. An application should avoid sending more data until
     * a further callback is received indicating the congestion status has been cleared.
     */
    static void ClientConnectionCongestionCb(int conn_id, bool congested);
    /** Callback invoked when batchscan storage config operation has completed */
    static void BatchScanCfgStorageCb(int client_if, int status);

    /** Callback invoked when batchscan enable / disable operation has completed */
    static void BatchScanEnableDisableCb(int action, int client_if, int status);

    /** Callback invoked when batchscan reports are obtained */
    static void BatchScanReportsCb(int client_if, int status, int report_format,
                                            int num_records, int data_len, uint8_t* rep_data);

    /** Callback invoked when batchscan storage threshold limit is crossed */
    static void BatchScanThresholdCb(int client_if);

    /** Track ADV VSE callback invoked when tracked device is found or lost */
    static void TrackAdvEventCb(btgatt_track_adv_info_t *p_track_adv_info);

    /** Callback invoked when scan parameter setup has completed */
    static void ScanParameterSetupCompletedCb(int client_if, btgattc_error_t status);

    /** GATT get database callback */
    static void GetGattDbCb(int conn_id, btgatt_db_element_t *db, int count);

    /** GATT services between start_handle and end_handle were removed */
    static void ServicesRemovedCb(int conn_id, uint16_t start_handle, uint16_t end_handle);

    /** GATT services were added */
    static void ServicesAddedCb(int conn_id, btgatt_db_element_t *added, int added_count);

    /* helper for printing map*/
    static void PrintConnectionMap();

    struct RegClientResponseData {
        int status_ = -1;
        int interface_ = -1;
        bool data_ready_ = false;
    };

    static RegClientResponseData reg_client_data_;

    static std::mutex reg_client_mutex_;
    static std::mutex state_change_mutex_;
    static std::mutex device_discovered_mutex_;
    static std::mutex discovery_state_change_mutex_;
    static std::mutex client_connect_mutex_;
    static std::mutex connection_state_mutex_;
    static std::mutex client_disconnect_mutex_;
    static std::mutex search_complete_mutex_;
    static std::mutex db_search_service_mutex_;
    static std::mutex read_char_mutex_;
    static std::mutex write_char_mutex_;
    static std::mutex bt_adv_mutex_;

    static std::condition_variable reg_client_cv_;
    static std::condition_variable state_change_cv_;
    static std::condition_variable device_discovered_cv_;
    static std::condition_variable discovery_state_change_cv_;
    static std::condition_variable client_connect_cv_;
    static std::condition_variable client_disconnect_cv_;
    static std::condition_variable search_complete_cv_;
    static std::condition_variable db_search_service_cv_;
    static std::condition_variable read_char_cv_;
    static std::condition_variable write_char_cv_;
    static std::condition_variable bt_adv_cv_;

    struct RegServerResponseData {
        int status_ = -1;
        int interface_ = -1;
        bool data_ready_ = false;
    };

    static RegServerResponseData reg_server_data_;

    struct AddServiceResponseData {
        int status_ = -1;
        int handle_ = -1;
        int interface_ = -1;
        bool data_ready_ = false;
    };

    static AddServiceResponseData add_service_data_;

    struct ToggleAdvertiseResponseData {
        int status_ = -1;
        int interface_ = -1;
        bool data_ready_ = false;
    };

    static ToggleAdvertiseResponseData toggle_advertise_data_;

    struct ClientConnectResponseData {
        bt_bdaddr_t addr_;
        int status_ = -1;
        int conn_id_ = -1;
        int interface_ = -1;
        bool data_ready_ = false;
    };

    static ClientConnectResponseData client_connect_data_;

    struct ClientDisconnectResponseData {
        bt_bdaddr_t addr_;
        int status_ = -1;
        int conn_id_ = -1;
        int interface_ = -1;
        bool data_ready_ = false;
    };

    static ClientDisconnectResponseData client_disconnect_data_;

    struct SearchCompleteResponseData {
        int status_ = -1;
        int conn_id_ = -1;
        bool data_ready_ = false;
    };

    static SearchCompleteResponseData search_complete_data_;

    struct DbSearchServiceResponseData {
        int conn_id_ = -1;
        int charac_handle_ = -1;
        bool matched_ = false;
        bool data_ready_ = false;
    };

    static DbSearchServiceResponseData db_search_service_response_data_;

    struct WriteCharResponseData {
        int status_ = -1;
        int conn_id_ = -1;
        int handle_ = -1;
        bool data_ready_ = false;
    };

    static WriteCharResponseData write_char_response_data_;

    struct ReadCharResponseData {
        std::vector<uint8_t> data_;
        int status_ = -1;
        int conn_id_ = -1;
        int handle_ = -1;
        bool data_ready_ = false;
    };

    static ReadCharResponseData read_char_response_data_;

    struct AdapterStateChangeResponseData {
        bt_state_t state_ = BT_STATE_OFF;
        bool data_ready_ = false;
    };

    static AdapterStateChangeResponseData adapter_state_change_data_;

    struct DiscoveryStateChangeResponseData {
        bt_discovery_state_t state_ = BT_DISCOVERY_STOPPED;
        bool data_ready_ = false;
    };

    static DiscoveryStateChangeResponseData discovery_state_change_data_;

    struct RequiredDeviceDiscoveredData {
        bt_bdaddr_t addr_;
        bool data_required_ = false;
        bool data_ready_ = false;
    };

    static RequiredDeviceDiscoveredData required_device_discovered_data_;

    static std::mutex reg_server_mutex_;
    static std::mutex add_service_mutex_;
    static std::mutex add_char_mutex_;
    static std::mutex add_desc_mutex_;
    static std::mutex start_service_mutex_;

    static std::condition_variable reg_server_cv_;
    static std::condition_variable add_service_cv_;
    static std::condition_variable add_char_cv_;
    static std::condition_variable add_desc_cv_;
    static std::condition_variable start_service_cv_;

    static bt_callbacks_t bluetooth_callbacks_;
    static bt_os_callouts_t os_callouts_;
    static bt_state_t bt_state_;
    static int server_if_;
    static int client_if_;
    static int service_handle_;
    static uint16_t attribute_handle_;
    static std::vector<uint8_t> read_write_characteristic_uuid_;
    static int read_write_characteristic_handle_;
    static nd::interface::LeEventCb le_event_cb_;

    static const btgatt_client_callbacks_t gatt_client_callbacks_;
    static const btgatt_server_callbacks_t gatt_server_callbacks_;
    static const btgatt_callbacks_t gatt_callbacks_;

    hw_device_t *device_ = nullptr;
    bluetooth_device_t *bt_device_ = nullptr;
    const bt_interface_t *bt_interface_ = nullptr;
    std::unique_ptr<Gatt> gatt_ptr_;
    bool is_le_scan_on_ = false;
    bool is_beacon_advertising_on_ = false;

    // addr - conn_id
    static std::unordered_map<std::string, int> connected_devices_map_;
    static std::unordered_set<std::string> connecting_devices_set_;
};

QcomDevice::QcomDeviceImpl::RegServerResponseData QcomDevice::QcomDeviceImpl::reg_server_data_;
QcomDevice::QcomDeviceImpl::AddServiceResponseData QcomDevice::QcomDeviceImpl::add_service_data_;

QcomDevice::QcomDeviceImpl::RegClientResponseData QcomDevice::QcomDeviceImpl::reg_client_data_;
QcomDevice::QcomDeviceImpl::ToggleAdvertiseResponseData QcomDevice::QcomDeviceImpl::toggle_advertise_data_;
QcomDevice::QcomDeviceImpl::ClientConnectResponseData QcomDevice::QcomDeviceImpl::client_connect_data_;
QcomDevice::QcomDeviceImpl::ClientDisconnectResponseData QcomDevice::QcomDeviceImpl::client_disconnect_data_;
QcomDevice::QcomDeviceImpl::SearchCompleteResponseData QcomDevice::QcomDeviceImpl::search_complete_data_;
QcomDevice::QcomDeviceImpl::DbSearchServiceResponseData QcomDevice::QcomDeviceImpl::db_search_service_response_data_;
QcomDevice::QcomDeviceImpl::WriteCharResponseData QcomDevice::QcomDeviceImpl::write_char_response_data_;
QcomDevice::QcomDeviceImpl::ReadCharResponseData QcomDevice::QcomDeviceImpl::read_char_response_data_;
QcomDevice::QcomDeviceImpl::AdapterStateChangeResponseData QcomDevice::QcomDeviceImpl::adapter_state_change_data_;

QcomDevice::QcomDeviceImpl::RequiredDeviceDiscoveredData QcomDevice::QcomDeviceImpl::required_device_discovered_data_;
QcomDevice::QcomDeviceImpl::DiscoveryStateChangeResponseData QcomDevice::QcomDeviceImpl::discovery_state_change_data_;

int QcomDevice::QcomDeviceImpl::read_write_characteristic_handle_ = -1;
int QcomDevice::QcomDeviceImpl::server_if_ = -1;
int QcomDevice::QcomDeviceImpl::client_if_ = -1;
int QcomDevice::QcomDeviceImpl::service_handle_ = -1;

nd::interface::LeEventCb QcomDevice::QcomDeviceImpl::le_event_cb_ = nullptr;
std::vector<uint8_t> QcomDevice::QcomDeviceImpl::read_write_characteristic_uuid_;

std::mutex QcomDevice::QcomDeviceImpl::reg_client_mutex_;
std::mutex QcomDevice::QcomDeviceImpl::state_change_mutex_;
std::mutex QcomDevice::QcomDeviceImpl::client_connect_mutex_;
std::mutex QcomDevice::QcomDeviceImpl::connection_state_mutex_;
std::mutex QcomDevice::QcomDeviceImpl::client_disconnect_mutex_;
std::mutex QcomDevice::QcomDeviceImpl::search_complete_mutex_;
std::mutex QcomDevice::QcomDeviceImpl::db_search_service_mutex_;
std::mutex QcomDevice::QcomDeviceImpl::read_char_mutex_;
std::mutex QcomDevice::QcomDeviceImpl::write_char_mutex_;
std::mutex QcomDevice::QcomDeviceImpl::bt_adv_mutex_;

std::condition_variable QcomDevice::QcomDeviceImpl::reg_client_cv_;
std::condition_variable QcomDevice::QcomDeviceImpl::state_change_cv_;
std::condition_variable QcomDevice::QcomDeviceImpl::client_connect_cv_;
std::condition_variable QcomDevice::QcomDeviceImpl::client_disconnect_cv_;
std::condition_variable QcomDevice::QcomDeviceImpl::search_complete_cv_;
std::condition_variable QcomDevice::QcomDeviceImpl::db_search_service_cv_;
std::condition_variable QcomDevice::QcomDeviceImpl::read_char_cv_;
std::condition_variable QcomDevice::QcomDeviceImpl::write_char_cv_;
std::condition_variable QcomDevice::QcomDeviceImpl::bt_adv_cv_;

std::mutex QcomDevice::QcomDeviceImpl::reg_server_mutex_;
std::mutex QcomDevice::QcomDeviceImpl::add_service_mutex_;
std::mutex QcomDevice::QcomDeviceImpl::add_char_mutex_;
std::mutex QcomDevice::QcomDeviceImpl::add_desc_mutex_;
std::mutex QcomDevice::QcomDeviceImpl::start_service_mutex_;

std::condition_variable QcomDevice::QcomDeviceImpl::reg_server_cv_;
std::condition_variable QcomDevice::QcomDeviceImpl::add_service_cv_;
std::condition_variable QcomDevice::QcomDeviceImpl::add_char_cv_;
std::condition_variable QcomDevice::QcomDeviceImpl::add_desc_cv_;
std::condition_variable QcomDevice::QcomDeviceImpl::start_service_cv_;

std::mutex QcomDevice::QcomDeviceImpl::device_discovered_mutex_;
std::mutex QcomDevice::QcomDeviceImpl::discovery_state_change_mutex_;

std::condition_variable QcomDevice::QcomDeviceImpl::device_discovered_cv_;
std::condition_variable QcomDevice::QcomDeviceImpl::discovery_state_change_cv_;

std::unordered_map<std::string, int> QcomDevice::QcomDeviceImpl::connected_devices_map_;
std::unordered_set<std::string> QcomDevice::QcomDeviceImpl::connecting_devices_set_;

const btgatt_client_callbacks_t QcomDevice::QcomDeviceImpl::gatt_client_callbacks_ = {
    QcomDevice::QcomDeviceImpl::RegisterClientCb,
    QcomDevice::QcomDeviceImpl::ScanResultCb,
    QcomDevice::QcomDeviceImpl::ConnectCb,
    QcomDevice::QcomDeviceImpl::DisconnectCb,
    QcomDevice::QcomDeviceImpl::SearchCompleteCb,
    QcomDevice::QcomDeviceImpl::RegisterForNotificationCb,
    QcomDevice::QcomDeviceImpl::NotifyCb,
    QcomDevice::QcomDeviceImpl::ReadCharacteristicCb,
    QcomDevice::QcomDeviceImpl::WriteCharacteristicCb,
    QcomDevice::QcomDeviceImpl::ReadDescriptorCb,
    QcomDevice::QcomDeviceImpl::WriteDescriptorCb,
    QcomDevice::QcomDeviceImpl::ExecuteWriteCb,
    QcomDevice::QcomDeviceImpl::ReadRemoteRssiCb,
    QcomDevice::QcomDeviceImpl::ListenCb,
    QcomDevice::QcomDeviceImpl::ConfigureMtuCb,
    QcomDevice::QcomDeviceImpl::ScanFilterCfgCb,
    QcomDevice::QcomDeviceImpl::ScanFilterParamCb,
    QcomDevice::QcomDeviceImpl::ScanFilterStatusCb,
    QcomDevice::QcomDeviceImpl::MultiAdvEnableCb,
    QcomDevice::QcomDeviceImpl::MultiAdvUpdateCb,
    QcomDevice::QcomDeviceImpl::MultiAdvDataCb,
    QcomDevice::QcomDeviceImpl::MultiAdvDisableCb,
    QcomDevice::QcomDeviceImpl::ClientConnectionCongestionCb,
    QcomDevice::QcomDeviceImpl::BatchScanCfgStorageCb,
    QcomDevice::QcomDeviceImpl::BatchScanEnableDisableCb,
    QcomDevice::QcomDeviceImpl::BatchScanReportsCb,
    QcomDevice::QcomDeviceImpl::BatchScanThresholdCb,
    QcomDevice::QcomDeviceImpl::TrackAdvEventCb,
    QcomDevice::QcomDeviceImpl::ScanParameterSetupCompletedCb,
    QcomDevice::QcomDeviceImpl::GetGattDbCb,
    QcomDevice::QcomDeviceImpl::ServicesRemovedCb,
    QcomDevice::QcomDeviceImpl::ServicesAddedCb
};

const btgatt_server_callbacks_t QcomDevice::QcomDeviceImpl::gatt_server_callbacks_ = {
    QcomDevice::QcomDeviceImpl::RegisterServerCb,
    QcomDevice::QcomDeviceImpl::ConnectionCb,
    QcomDevice::QcomDeviceImpl::ServiceAddedCb,
    QcomDevice::QcomDeviceImpl::IncludedServiceAddedCb,
    QcomDevice::QcomDeviceImpl::CharacteristicAddedCb,
    QcomDevice::QcomDeviceImpl::DescriptorAddedCb,
    QcomDevice::QcomDeviceImpl::ServiceStartedCb,
    QcomDevice::QcomDeviceImpl::ServiceStoppedCb,
    QcomDevice::QcomDeviceImpl::ServiceDeletedCb,
    QcomDevice::QcomDeviceImpl::RequestReadCb,
    QcomDevice::QcomDeviceImpl::RequestWriteCb,
    QcomDevice::QcomDeviceImpl::RequestExecWriteCb,
    QcomDevice::QcomDeviceImpl::ResponseConfirmationCb,
    QcomDevice::QcomDeviceImpl::IndicationSentCb,
    QcomDevice::QcomDeviceImpl::ServerConnectionCongestionCb,
    QcomDevice::QcomDeviceImpl::MtuChangedCb
};

const btgatt_callbacks_t QcomDevice::QcomDeviceImpl::gatt_callbacks_ = {
    sizeof(btgatt_callbacks_t),
    &QcomDevice::QcomDeviceImpl::gatt_client_callbacks_,
    &QcomDevice::QcomDeviceImpl::gatt_server_callbacks_
};

/** Callback invoked in response to register_server */
void QcomDevice::QcomDeviceImpl::RegisterServerCb(int status, int server_if, bt_uuid_t *app_uuid) {
    LOG_I(kLogTag, "Inside %s, status = %d", __func__, status);
    {
        const std::lock_guard<std::mutex> lock(reg_server_mutex_);
        reg_server_data_.data_ready_ = true;
        reg_server_data_.interface_ = server_if;
        reg_server_data_.status_ = status;
    }
    reg_server_cv_.notify_all();
}

/** Callback indicating that a remote device has connected or been disconnected */
void QcomDevice::QcomDeviceImpl::ConnectionCb(int conn_id, int server_if, int connected, bt_bdaddr_t *bda) {

}

/** Callback invoked in response to create_service */
void QcomDevice::QcomDeviceImpl::ServiceAddedCb(int status, int server_if, btgatt_srvc_id_t *srvc_id, int srvc_handle) {
    LOG_I(kLogTag, "Inside %s, status = %d", __func__, status);
    {
        const std::lock_guard<std::mutex> lock(add_service_mutex_);
        add_service_data_.data_ready_ = true;
        add_service_data_.interface_ = server_if;
        add_service_data_.handle_ = srvc_handle;
        add_service_data_.status_ = status;
    }
    add_service_cv_.notify_all();
}

/** Callback indicating that an included service has been added to a service */
void QcomDevice::QcomDeviceImpl::IncludedServiceAddedCb(int status, int server_if, int srvc_handle, int incl_srvc_handle) {

}

/** Callback invoked when a characteristic has been added to a service */
void QcomDevice::QcomDeviceImpl::CharacteristicAddedCb(int status, int server_if, bt_uuid_t *uuid, int srvc_handle, int char_handle) {

}

/** Callback invoked when a descriptor has been added to a characteristic */
void QcomDevice::QcomDeviceImpl::DescriptorAddedCb(int status, int server_if, bt_uuid_t *uuid, int srvc_handle, int descr_handle) {

}

/** Callback invoked in response to start_service */
void QcomDevice::QcomDeviceImpl::ServiceStartedCb(int status, int server_if, int srvc_handle) {

}

/** Callback invoked in response to stop_service */
void QcomDevice::QcomDeviceImpl::ServiceStoppedCb(int status, int server_if, int srvc_handle) {

}

/** Callback triggered when a service has been deleted */
void QcomDevice::QcomDeviceImpl::ServiceDeletedCb(int status, int server_if, int srvc_handle) {

}

/**
 * Callback invoked when a remote device has requested to read a characteristic
 * or descriptor. The application must respond by calling send_response
 */
void QcomDevice::QcomDeviceImpl::RequestReadCb(int conn_id, int trans_id, bt_bdaddr_t *bda, int attr_handle, int offset, bool is_long) {

}

/**
 * Callback invoked when a remote device has requested to write to a
 * characteristic or descriptor.
 */
void QcomDevice::QcomDeviceImpl::RequestWriteCb(int conn_id, int trans_id, bt_bdaddr_t *bda,
                                    int attr_handle, int offset, int length,
                                    bool need_rsp, bool is_prep, uint8_t* value) {
}

/** Callback invoked when a previously prepared write is to be executed */
void QcomDevice::QcomDeviceImpl::RequestExecWriteCb(int conn_id, int trans_id, bt_bdaddr_t *bda, int exec_write) {

}

/**
 * Callback triggered in response to send_response if the remote device
 * sends a confirmation.
 */
void QcomDevice::QcomDeviceImpl::ResponseConfirmationCb(int status, int handle) {

}

/**
 * Callback confirming that a notification or indication has been sent
 * to a remote device.
 */
void QcomDevice::QcomDeviceImpl::IndicationSentCb(int conn_id, int status) {

}

/**
 * Callback notifying an application that a remote device connection is currently congested
 * and cannot receive any more data. An application should avoid sending more data until
 * a further callback is received indicating the congestion status has been cleared.
 */
void QcomDevice::QcomDeviceImpl::ServerConnectionCongestionCb(int conn_id, bool congested) {

}

/** Callback invoked when the MTU for a given connection changes */
void QcomDevice::QcomDeviceImpl::MtuChangedCb(int conn_id, int mtu) {

}

/*==========================================================================================*/

/** BT-GATT Client callback structure. */

/** Callback invoked in response to register_client */
void QcomDevice::QcomDeviceImpl::RegisterClientCb(int status, int client_if, bt_uuid_t *app_uuid) {
    LOG_I(kLogTag, "Inside %s, status = %d", __func__, status);
    {
        const std::lock_guard<std::mutex> lock(reg_client_mutex_);

        reg_client_data_.data_ready_ = true;
        reg_client_data_.interface_ = client_if;
        reg_client_data_.status_ = status;
    }
    reg_client_cv_.notify_all();
}

/** Callback for scan results */
void QcomDevice::QcomDeviceImpl::ScanResultCb(bt_bdaddr_t* bda, int rssi, uint8_t* adv_data) {
    if (le_event_cb_) {
        const std::string mac_addr = qcom_utils::GetMacAddressAsString(*bda);
        const auto adv_data_size = qcom_utils::GetAdvDataLength(adv_data);
        le_event_cb_(mac_addr, adv_data, adv_data_size, rssi);
    }
}

void QcomDevice::QcomDeviceImpl::PrintConnectionMap() {

    const std::lock_guard<std::mutex> lock(connection_state_mutex_);
    LOG_I(kLogTag, "Connected devices: %lu", connected_devices_map_.size());
    for (const auto& device : connected_devices_map_) {
        LOG_I(kLogTag, "  %s (id: %d)", device.first.c_str(), device.second);
    }

    LOG_I(kLogTag, "Connecting devices: %lu", connecting_devices_set_.size());
    for (const auto& device : connecting_devices_set_) {
        LOG_I(kLogTag, "  %s", device.c_str());
    }
}

/** GATT open callback invoked in response to open */
void QcomDevice::QcomDeviceImpl::ConnectCb(int conn_id, int status, int client_if, bt_bdaddr_t* bda) {
    LOG_I(kLogTag, "Inside %s, status = %d, id: %d", __func__, status, conn_id);

    if (nullptr != bda) {

        const std::string mac = qcom_utils::GetMacAddressAsString(*bda);

        PrintConnectionMap();

        {
            const std::lock_guard<std::mutex> lock(client_connect_mutex_);
            std::memcpy(&client_connect_data_.addr_, bda, sizeof(bt_bdaddr_t));

            client_connect_data_.data_ready_ = true;
            client_connect_data_.interface_ = client_if;
            client_connect_data_.status_ = status;
            client_connect_data_.conn_id_ = conn_id;
        }

        {
            const std::lock_guard<std::mutex> lock(connection_state_mutex_);
            connecting_devices_set_.erase(mac);
            if (BT_STATUS_SUCCESS == status) {
                connected_devices_map_.emplace(mac, conn_id);
            }
        }

        PrintConnectionMap();

        client_connect_cv_.notify_all();

    } else {
        LOG_E(kLogTag, "bda is null in %s", __func__);
    }
}

/** Callback invoked in response to close */
void QcomDevice::QcomDeviceImpl::DisconnectCb(int conn_id, int status, int client_if, bt_bdaddr_t* bda) {
    LOG_I(kLogTag, "Inside %s, status = %d, id: %d", __func__, status, conn_id);

    if (nullptr != bda) {

        const std::string mac = qcom_utils::GetMacAddressAsString(*bda);

        {
            const std::lock_guard<std::mutex> lock(connection_state_mutex_);
            connecting_devices_set_.erase(mac);
            connected_devices_map_.erase(mac);
        }

        {
            const std::lock_guard<std::mutex> lock(client_disconnect_mutex_);
            std::memcpy(&client_disconnect_data_.addr_, bda, sizeof(bt_bdaddr_t));
            client_disconnect_data_.data_ready_ = true;
            client_disconnect_data_.interface_ = client_if;
            client_disconnect_data_.status_ = status;
            client_disconnect_data_.conn_id_ = conn_id;
        }

        PrintConnectionMap();

        client_disconnect_cv_.notify_all();
    } else {
        LOG_E(kLogTag, "bda is null in %s", __func__);
    }
}

/**
 * Invoked in response to search_service when the GATT service search
 * has been completed.
 */
void QcomDevice::QcomDeviceImpl::SearchCompleteCb(int conn_id, int status) {
    LOG_I(kLogTag, "Inside %s, status = %d, id: %d", __func__, status, conn_id);
    {
        const std::lock_guard<std::mutex> lock(search_complete_mutex_);
        search_complete_data_.data_ready_ = true;
        search_complete_data_.status_ = status;
        search_complete_data_.conn_id_ = conn_id;
    }
    search_complete_cv_.notify_all();
}

/** Callback invoked in response to [de]register_for_notification */
void QcomDevice::QcomDeviceImpl::RegisterForNotificationCb(int conn_id, int registered, int status, uint16_t handle) {
    LOG_I(kLogTag, "Inside %s, status = %d, id: %d", __func__, status, conn_id);
}

/**
 * Remote device notification callback, invoked when a remote device sends
 * a notification or indication that a client has registered for.
 */
void QcomDevice::QcomDeviceImpl::NotifyCb(int conn_id, btgatt_notify_params_t *p_data) {
    LOG_I(kLogTag, "Inside %s, id: %d", __func__, conn_id);
}

/** Reports result of a GATT read operation */
void QcomDevice::QcomDeviceImpl::ReadCharacteristicCb(int conn_id, int status, btgatt_read_params_t *p_data) {
    LOG_I(kLogTag, "Inside %s, status: %d, id: %d", __func__, status, conn_id);
    {
        const std::lock_guard<std::mutex> lock(read_char_mutex_);
        read_char_response_data_.data_ready_ = true;
        read_char_response_data_.status_ = status;
        read_char_response_data_.conn_id_ = conn_id;
        if (nullptr != p_data) {
            LOG_I(kLogTag, "Inside %s handle: %d", __func__, p_data->handle);
            const std::vector<uint8_t> data (p_data->value.value, p_data->value.value + p_data->value.len);
            read_char_response_data_.data_ = std::move(data);
            read_char_response_data_.handle_ = p_data->handle;
        } else {
            LOG_E(kLogTag, "p_data is null in %s", __func__);
        }
    }
    read_char_cv_.notify_all();
}

/** GATT write characteristic operation callback */
void QcomDevice::QcomDeviceImpl::WriteCharacteristicCb(int conn_id, int status, uint16_t handle) {
    LOG_I(kLogTag, "Inside %s, status: %d, id: %d, handle: %d", __func__, status, conn_id, handle);
    {
        const std::lock_guard<std::mutex> lock(write_char_mutex_);
        write_char_response_data_.data_ready_ = true;
        write_char_response_data_.handle_ = handle;
        write_char_response_data_.status_ = status;
        write_char_response_data_.conn_id_ = conn_id;
    }
    write_char_cv_.notify_all();
}

/** GATT execute prepared write callback */
void QcomDevice::QcomDeviceImpl::ExecuteWriteCb(int conn_id, int status) {
    LOG_I(kLogTag, "Inside %s, status = %d, id: %d", __func__, status, conn_id);
}

/** Callback invoked in response to read_descriptor */
void QcomDevice::QcomDeviceImpl::ReadDescriptorCb(int conn_id, int status, btgatt_read_params_t *p_data) {
    LOG_I(kLogTag, "Inside %s, status = %d, id: %d", __func__, status, conn_id);
}

/** Callback invoked in response to write_descriptor */
void QcomDevice::QcomDeviceImpl::WriteDescriptorCb(int conn_id, int status, uint16_t handle) {
    LOG_I(kLogTag, "Inside %s, status = %d, id: %d", __func__, status, conn_id);
}

/** Callback triggered in response to read_remote_rssi */
void QcomDevice::QcomDeviceImpl::ReadRemoteRssiCb(int client_if, bt_bdaddr_t* bda, int rssi, int status) {
    LOG_I(kLogTag, "Inside %s, status = %d, rssi: %d", __func__, status, rssi);
}

/**
 * Callback indicating the status of a listen() operation
 */
void QcomDevice::QcomDeviceImpl::ListenCb(int status, int server_if) {
    LOG_I(kLogTag, "Inside %s, status = %d", __func__, status);
    {
        const std::lock_guard<std::mutex> lock(bt_adv_mutex_);
        toggle_advertise_data_.data_ready_ = true;
        toggle_advertise_data_.interface_ = server_if;
        toggle_advertise_data_.status_ = status;
    }
    bt_adv_cv_.notify_all();
}

/** Callback invoked when the MTU for a given connection changes */
void QcomDevice::QcomDeviceImpl::ConfigureMtuCb(int conn_id, int status, int mtu) {
    LOG_I(kLogTag, "Inside %s, status = %d, id: %d", __func__, status, conn_id);
}

/** Callback invoked when a scan filter configuration command has completed */
void QcomDevice::QcomDeviceImpl::ScanFilterCfgCb(int action, int client_if, int status, int filt_type, int avbl_space) {
    LOG_I(kLogTag, "Inside %s, status = %d", __func__, status);
}

/** Callback invoked when scan param has been added, cleared, or deleted */
void QcomDevice::QcomDeviceImpl::ScanFilterParamCb(int action, int client_if, int status, int avbl_space) {
    LOG_I(kLogTag, "Inside %s, status = %d", __func__, status);
}

/** Callback invoked when a scan filter configuration command has completed */
void QcomDevice::QcomDeviceImpl::ScanFilterStatusCb(int enable, int client_if, int status) {
    LOG_I(kLogTag, "Inside %s, status = %d", __func__, status);
}

/** Callback invoked when multi-adv enable operation has completed */
void QcomDevice::QcomDeviceImpl::MultiAdvEnableCb(int client_if, int status) {
    LOG_I(kLogTag, "Inside %s, status = %d", __func__, status);
}

/** Callback invoked when multi-adv param update operation has completed */
void QcomDevice::QcomDeviceImpl::MultiAdvUpdateCb(int client_if, int status) {
    LOG_I(kLogTag, "Inside %s, status = %d", __func__, status);
}

/** Callback invoked when multi-adv instance data set operation has completed */
void QcomDevice::QcomDeviceImpl::MultiAdvDataCb(int client_if, int status) {
    LOG_I(kLogTag, "Inside %s, status = %d", __func__, status);
}

/** Callback invoked when multi-adv disable operation has completed */
void QcomDevice::QcomDeviceImpl::MultiAdvDisableCb(int client_if, int status) {
    LOG_I(kLogTag, "Inside %s, status = %d", __func__, status);
}

/**
 * Callback notifying an application that a remote device connection is currently congested
 * and cannot receive any more data. An application should avoid sending more data until
 * a further callback is received indicating the congestion status has been cleared.
 */
void QcomDevice::QcomDeviceImpl::ClientConnectionCongestionCb(int conn_id, bool congested) {
    LOG_I(kLogTag, "Inside %s, id: %d", __func__, conn_id);
}

/** Callback invoked when batchscan storage config operation has completed */
void QcomDevice::QcomDeviceImpl::BatchScanCfgStorageCb(int client_if, int status) {
    LOG_I(kLogTag, "Inside %s, status = %d", __func__, status);
}

/** Callback invoked when batchscan enable / disable operation has completed */
void QcomDevice::QcomDeviceImpl::BatchScanEnableDisableCb(int action, int client_if, int status) {
    LOG_I(kLogTag, "Inside %s, status = %d", __func__, status);
}

/** Callback invoked when batchscan reports are obtained */
void QcomDevice::QcomDeviceImpl::BatchScanReportsCb(int client_if, int status, int report_format,
                                                    int num_records, int data_len, uint8_t* rep_data) {
    LOG_I(kLogTag, "Inside %s, status = %d", __func__, status);
}

/** Callback invoked when batchscan storage threshold limit is crossed */
void QcomDevice::QcomDeviceImpl::BatchScanThresholdCb(int client_if) {

}

/** Track ADV VSE callback invoked when tracked device is found or lost */
void QcomDevice::QcomDeviceImpl::TrackAdvEventCb(btgatt_track_adv_info_t *p_track_adv_info) {

}

/** Callback invoked when scan parameter setup has completed */
void QcomDevice::QcomDeviceImpl::ScanParameterSetupCompletedCb(int client_if, btgattc_error_t status) {

}

/** GATT get database callback */
void QcomDevice::QcomDeviceImpl::GetGattDbCb(int conn_id, btgatt_db_element_t *db, int count) {
    LOG_I(kLogTag, "Inside %s, conn_id: %d count: %d", __func__, conn_id, count);
    {
        const std::lock_guard<std::mutex> lock(db_search_service_mutex_);
        db_search_service_response_data_.data_ready_ = true;
        db_search_service_response_data_.conn_id_ = conn_id;
        db_search_service_response_data_.matched_ = false;

        const size_t entries = (count > 0) ? static_cast<size_t>(count) : 0;

        for (unsigned int pos = 0; pos < entries; ++pos) {
            // LOG_I(kLogTag, "Inside %s, pos: %d", __func__, pos);
            btgatt_db_element_t *element = &(db[pos]);
            switch (element->type) {

                // case BTGATT_DB_PRIMARY_SERVICE: {
                //     break;
                // }
                // case BTGATT_DB_SECONDARY_SERVICE: {
                //     break;
                // }
                // case BTGATT_DB_DESCRIPTOR: {
                //     break;
                // }

                // NOTE We are only interested in characteristic uuid
                case BTGATT_DB_CHARACTERISTIC: {
                    const std::vector<uint8_t> uuid (element->uuid.uu, element->uuid.uu + 16);
                    // for(const auto ui : uuid) {
                    //     std::cout << (int)ui << " ";
                    // }
                    // std::cout << std::endl;
                    // for(const auto ui : read_write_characteristic_uuid_) {
                    //     std::cout << (int)ui << " ";
                    // }
                    db_search_service_response_data_.matched_ = (uuid == read_write_characteristic_uuid_);
                    db_search_service_response_data_.charac_handle_ = element->attribute_handle;
                    break;
                }
                default: {
                    break;
                }
            }

            if (db_search_service_response_data_.matched_) {
                LOG_I(kLogTag, "Inside %s, found characteristic", __func__);
                break;
            }
        }
    }
    db_search_service_cv_.notify_all();
}

/** GATT services between start_handle and end_handle were removed */
void QcomDevice::QcomDeviceImpl::ServicesRemovedCb(int conn_id, uint16_t start_handle, uint16_t end_handle) {
    LOG_I(kLogTag, "Inside %s, id: %d", __func__, conn_id);
}

/** GATT services were added */
void QcomDevice::QcomDeviceImpl::ServicesAddedCb(int conn_id, btgatt_db_element_t *added, int added_count) {
    LOG_I(kLogTag, "Inside %s, id: %d", __func__, conn_id);
}

bt_state_t QcomDevice::QcomDeviceImpl::bt_state_ = BT_STATE_OFF;

bt_callbacks_t QcomDevice::QcomDeviceImpl::bluetooth_callbacks_ = {
                                                                    sizeof(bt_callbacks_t),
                                                                    AdapterStateChangeCallback,
                                                                    AdapterPropertiesCb,
                                                                    RemoteDevicePropertiesCb,
                                                                    DeviceFoundCb,
                                                                    DiscoveryStateChangedCb,
                                                                    PinRequestCb,
                                                                    SspRequestCb,
                                                                    BondStateChangedCb,
                                                                    AclStateChangedCb,
                                                                    CbThreadEvent,
                                                                    DutModeRecvCb,
                                                                    LeTestModeRecvCb,
                                                                    NULL,
                                                                    HciRawEventShow,
                                                                };

bt_os_callouts_t QcomDevice::QcomDeviceImpl::os_callouts_ = {
                                                                sizeof(bt_os_callouts_t),
                                                                SetWakeAlarm,
                                                                AcquireWakeLock,
                                                                ReleaseWakeLock,
                                                            };

QcomDevice::QcomDeviceImpl::QcomDeviceImpl() {
}

QcomDevice::QcomDeviceImpl::~QcomDeviceImpl() {
    Disable();
}

bool QcomDevice::QcomDeviceImpl::IsLeScanOn() {
    return is_le_scan_on_;
}

bool QcomDevice::QcomDeviceImpl::LeScanOff() {
    LOG_I(kLogTag, "Inside %s", __func__);
    bool status = false;

    do {

        if (!IsEnabled()) {
            LOG_E(kLogTag, "Bluetooth is not enabled");
            break;
        }

        if (nullptr == bt_interface_) {
            LOG_E(kLogTag, "Bluetooth interface is not initialized");
            break;
        }

        if (-1 == client_if_) {
            LOG_E(kLogTag, "client_if_ is -1 in %s", __func__);
            break;
        }

        const bool start = false;
        if (!ToggleScan(start)) {
            LOG_E(kLogTag, "Failed to start LE scan");
            break;
        }

        is_le_scan_on_ = false;
        status = true;
    } while (false);

    return status;
}

bool QcomDevice::QcomDeviceImpl::LeScanOn() {
    LOG_I(kLogTag, "Inside %s", __func__);

    bool status = false;

    do {

        if (!IsEnabled()) {
            LOG_E(kLogTag, "Bluetooth is not enabled");
            break;
        }

        if (nullptr == bt_interface_) {
            LOG_E(kLogTag, "Bluetooth interface is not initialized");
            break;
        }

        if (is_le_scan_on_) {
            LOG_I(kLogTag, "LE Scan is already on");
            status = true;
            break;
        }

        if ((-1 == client_if_) && (!RegisterClient())) {
            LOG_E(kLogTag, "RegisterClient failed in %s", __func__);
            break;
        }

        const bool start = true;
        if (!ToggleScan(start)) {
            LOG_E(kLogTag, "Failed to start LE scan");
            break;
        }

        is_le_scan_on_ = true;
        status = true;
    } while (false);

    return status;
}

bool QcomDevice::QcomDeviceImpl::RegisterLeEventCallback(nd::interface::LeEventCb cb) {
    le_event_cb_ = std::move(cb);
    return true;
}

bool QcomDevice::QcomDeviceImpl::ReadCharacteristicData(const std::string& mac_addr,
                                                        const std::string &uuid_str, std::vector<uint8_t> &result_out) {
    bool status = false;
    bool needs_disconnect = false;

    bt_bdaddr_t bda{};

    int conn_id = -1;

    do {
        if (!qcom_utils::GetMacAddrFromString(mac_addr, &bda)) {
            LOG_E(kLogTag, "Failed to extract %s as bdaddr in %s", __func__);
            break;
        }

        DisconnectExistingDevices();

        if (-1 == client_if_) {
            if (!RegisterClient()) {
                LOG_E(kLogTag, "RegisterClient failed in %s", __func__);
                break;
            }
        }

        if (!Connect(&bda, conn_id)) {
            LOG_E(kLogTag, "Failed to connect to mac: %s", mac_addr.c_str());
            break;
        }

        needs_disconnect = true;

        const std::string uuid_clean = qcom_utils::FetchAlphanumericString(uuid_str);
        read_write_characteristic_uuid_ = std::move(Translator::HexStringToBytes(uuid_clean));

        // Reverse the characteristic uuid
        std::reverse(read_write_characteristic_uuid_.begin(), read_write_characteristic_uuid_.end());

        if (!SearchService(conn_id)) {
            LOG_E(kLogTag, "SearchService failed");
            break;
        }

        if (!GetGattDb(conn_id)) {
            LOG_E(kLogTag, "GetGattDb failed");
            break;
        }

        result_out.clear();
        const bool read_success = ReadCharacteristic(result_out, conn_id);

        if (read_success) {
            status = true;
        } else {
            LOG_E(kLogTag, "Failed to read on mac: %s", mac_addr.c_str());
            result_out.clear();
        }

    } while (false);

    if ((needs_disconnect) && (!Disconnect(&bda, conn_id))) {
        LOG_E(kLogTag, "Failed to disconnect mac: %s", mac_addr.c_str());
    }

    return status;
}

bool QcomDevice::QcomDeviceImpl::ReadCharacteristicData(const std::string &mac_addr,
                                                        std::unordered_map<std::string, std::vector<uint8_t>> &charac_data_out) {
    bool status = false;
    bool needs_disconnect = false;

    bt_bdaddr_t bda{};

    int conn_id = -1;

    do {
        if (!qcom_utils::GetMacAddrFromString(mac_addr, &bda)) {
            LOG_E(kLogTag, "Failed to extract %s as bdaddr in %s", __func__);
            break;
        }

        DisconnectExistingDevices();

        if (-1 == client_if_) {
            if (!RegisterClient()) {
                LOG_E(kLogTag, "RegisterClient failed in %s", __func__);
                break;
            }
        }
        if (!Connect(&bda, conn_id)) {
            LOG_E(kLogTag, "Failed to connect to mac: %s", mac_addr.c_str());
            break;
        }

        needs_disconnect = true;
        bool read_error = false;

        if (!SearchService(conn_id)) {
            LOG_E(kLogTag, "SearchService failed, trying once more");
            if (!SearchService(conn_id)) {
                LOG_E(kLogTag, "SearchService failed in retry too");
                break;
            }
        }

        for (auto & uuid_data : charac_data_out) {

            std::vector<uint8_t> result_out;

            const std::string uuid_clean = qcom_utils::FetchAlphanumericString(uuid_data.first);
            read_write_characteristic_uuid_ = std::move(Translator::HexStringToBytes(uuid_clean));

            // Reverse the characteristic uuid
            std::reverse(read_write_characteristic_uuid_.begin(), read_write_characteristic_uuid_.end());

            if (!GetGattDb(conn_id)) {
                LOG_E(kLogTag, "GetGattDb failed for uuid: %s", uuid_data.first.c_str());
                continue;
            }

            const bool read_success = ReadCharacteristic(result_out, conn_id);

            if (read_success) {
                uuid_data.second = std::move(result_out);
            } else {
                LOG_E(kLogTag, "Failed to read on mac: %s", mac_addr.c_str());
                read_error = true;
            }
        }

        status = !read_error;

    } while (false);

    if ((needs_disconnect) && (!Disconnect(&bda, conn_id))) {
        LOG_E(kLogTag, "Failed to disconnect mac: %s", mac_addr.c_str());
    }

    return status;
}

bool QcomDevice::QcomDeviceImpl::StartBeaconAdvertising(int advertising_interval, const std::vector<uint8_t> &advertising_uuid,
                                                        int major_number, int minor_number, int8_t rssi_value) {

    bool status = false;

    do {

        constexpr std::array<uint8_t, 2> kAppleBeaconSignature = {0x4C, 0x00};
        constexpr uint8_t kBeaconIdentifierID = 0x02;
        constexpr uint8_t kBeaconPayloadLength = 0x15; //(16+2+2+1)

        const std::vector<uint8_t> major_num = {major_number >> 8 & 0x00FF, major_number & 0x00FF};
        const std::vector<uint8_t> minor_num = {minor_number >> 8 & 0x00FF, minor_number & 0x00FF};

        const uint8_t rssi = static_cast<uint8_t>(rssi_value);

        std::vector<uint8_t> data;

        data.reserve(kAppleBeaconSignature.size() + 1 + 1 + advertising_uuid.size() + major_num.size() + minor_num.size() + 1);

        data.insert(data.end(), kAppleBeaconSignature.begin(), kAppleBeaconSignature.end());
        data.emplace_back(kBeaconIdentifierID);
        data.emplace_back(kBeaconPayloadLength);
        data.insert(data.end(), advertising_uuid.begin(), advertising_uuid.end());
        data.insert(data.end(), major_num.begin(), major_num.end());
        data.insert(data.end(), minor_num.begin(), minor_num.end());
        data.emplace_back(rssi);

        advertising_interval = 100;

        if ((-1 == client_if_) && (!RegisterClient())) {
            LOG_E(kLogTag, "RegisterClient failed in %s", __func__);
            break;
        }

        if (!SetAdvertisementData(advertising_interval, data)) {
            LOG_E(kLogTag, "SetAdvertisementData failed in %s", __func__);
            break;
        }

        const bool start = true;
        if (!ToggleAdvertise(start)) {
            LOG_E(kLogTag, "ToggleAdvertise failed in %s", __func__);
            break;
        }

        is_beacon_advertising_on_ = true;

        status = true;

    } while (false);

    return status;
}

void QcomDevice::QcomDeviceImpl::DisconnectExistingDevices() {

    std::unordered_map<std::string, int> connected_map;

    {
        const std::lock_guard<std::mutex> lock(connection_state_mutex_);
        connected_map = connected_devices_map_;
    }

    for (const auto &device_data: connected_map) {
        LOG_W(kLogTag, "DisconnectExistingDevices: Disconnecting: %s", device_data.first.c_str());
        bt_bdaddr_t bda{};
        if (qcom_utils::GetMacAddrFromString(device_data.first, &bda)) {
            if (!Disconnect(&bda, device_data.second)) {
                LOG_E(kLogTag, "Failed to disconnect mac : %s", device_data.first.c_str());
            }
        }
    }
}

bool QcomDevice::QcomDeviceImpl::StartDiscovery() {
    LOG_I(kLogTag, "Inside %s", __func__);
    bool status = false;

    discovery_state_change_data_ = {};
    const int start_discovery_status = bt_interface_->start_discovery();

    do {
        if (0 != start_discovery_status) {
            LOG_E(kLogTag, "start_discovery failed, status = %d", start_discovery_status);
            break;
        }

        constexpr int kGattConditionWaitDuration = 5;

        std::unique_lock<std::mutex> lock(discovery_state_change_mutex_);
        if (discovery_state_change_cv_.wait_for(lock, std::chrono::seconds(kGattConditionWaitDuration),
                                                [&discovery_state_change_data_] {
                                                    return discovery_state_change_data_.data_ready_;
                                                })) {

            if (BT_DISCOVERY_STARTED == discovery_state_change_data_.state_) {
                status = true;
                LOG_I(kLogTag, "start_discovery success");
            } else {
                LOG_E(kLogTag, "post cb wait, start_discovery failed, state = %d", discovery_state_change_data_.state_);
            }

        } else {
            LOG_E(kLogTag, "post cb wait, start_discovery failed with no response");
        }

    } while (false);

    return status;
}

bool QcomDevice::QcomDeviceImpl::StartServiceAdvertising(const std::string &advertise_name, const std::vector<uint8_t> &advertise_uuid,
                                const std::string &advertise_data, uint32_t duration) {
    LOG_E(kLogTag, "%s not implemented", __func__);
    return true;
}

bool QcomDevice::QcomDeviceImpl::StopBeaconAdvertising() {

    bool status = false;

    do {

        if (!is_beacon_advertising_on_) {
            LOG_I(kLogTag, "Beacon advertising is already off");
            status = true;
            break;
        }

        const bool start = false;
        if (!ToggleAdvertise(start)) {
            LOG_E(kLogTag, "ToggleAdvertise failed in %s", __func__);
        }

        is_beacon_advertising_on_ = false;

        status = true;

    } while (false);

    return status;
}

bool QcomDevice::QcomDeviceImpl::StopDiscovery() {
    LOG_I(kLogTag, "Inside %s", __func__);
    bool status = false;

    discovery_state_change_data_ = {};
    const int start_discovery_status = bt_interface_->cancel_discovery();

    do {
        if (0 != start_discovery_status) {
            LOG_E(kLogTag, "cancel_discovery failed, status = %d", start_discovery_status);
            break;
        }

        constexpr int kGattConditionWaitDuration = 5;

        std::unique_lock<std::mutex> lock(discovery_state_change_mutex_);
        if (discovery_state_change_cv_.wait_for(lock, std::chrono::seconds(kGattConditionWaitDuration),
                                                [&discovery_state_change_data_] {
                                                    return discovery_state_change_data_.data_ready_;
                                                })) {

            if (BT_DISCOVERY_STOPPED == discovery_state_change_data_.state_) {
                status = true;
                LOG_I(kLogTag, "cancel_discovery success");
            } else {
                LOG_E(kLogTag, "post cb wait, cancel_discovery failed, state = %d", discovery_state_change_data_.state_);
            }

        } else {
            LOG_E(kLogTag, "post cb wait, cancel_discovery failed with no response");
        }

    } while (false);

    return status;
}

bool QcomDevice::QcomDeviceImpl::GetValueFromPropertyList(int num_properties,bt_property_t *properties,
                                                          bt_property_type_t type, void* dest) {
    bool status = false;
    for (int index = 0; index < num_properties; index++) {
        if (type == properties[index].type) {
            memcpy(dest, properties[index].val, properties[index].len);
            status = true;
            break;
        }
    }
    return status;
}

bool QcomDevice::QcomDeviceImpl::StopServiceAdvertising() {
    LOG_E(kLogTag, "%s not implemented", __func__);
    return true;
}

bool QcomDevice::QcomDeviceImpl::WriteCharacteristicData(const std::string& mac_addr, const std::string &uuid_str,
                                                         const std::vector<std::string> &data, nd::interface::AddressType addr_type) {

    LOG_I(kLogTag, "Inside %s", __func__);

    bool status = false;
    bool needs_disconnect = false;

    bt_bdaddr_t bda{};

    int conn_id = -1;

    do {

        if (!qcom_utils::GetMacAddrFromString(mac_addr, &bda)) {
            LOG_E(kLogTag, "Failed to extract %s as bdaddr in %s", __func__);
            break;
        }

        DisconnectExistingDevices();

        if (-1 == client_if_) {
            if (!RegisterClient()) {
                LOG_E(kLogTag, "RegisterClient failed in %s", __func__);
                break;
            }
        }

        if (!Connect(&bda, conn_id)) {
            LOG_E(kLogTag, "Failed to connect to mac: %s", mac_addr.c_str());
            break;
        }

        needs_disconnect = true;

        const std::string uuid_clean = qcom_utils::FetchAlphanumericString(uuid_str);
        read_write_characteristic_uuid_ = std::move(Translator::HexStringToBytes(uuid_clean));

        // Reverse the characteristic uuid
        std::reverse(read_write_characteristic_uuid_.begin(), read_write_characteristic_uuid_.end());

        if (!SearchService(conn_id)) {
            LOG_E(kLogTag, "SearchService failed");
            break;
        }

        if (!GetGattDb(conn_id)) {
            LOG_E(kLogTag, "GetGattDb failed");
            break;
        }

        const bool write_failed = ((data.empty()) ||
                                   (std::any_of(data.begin(), data.end(), [this, conn_id](const std::string &element) {
                                                                                return (!WriteCharacteristic(element, conn_id));
                                                                            })));

        if (!write_failed) {
            status = true;
        } else {
            LOG_E(kLogTag, "Failed to write on mac: %s", mac_addr.c_str());
        }

    } while (false);

    if (needs_disconnect) {
        if (!Disconnect(&bda, conn_id)) {
            LOG_E(kLogTag, "Failed to disconnect mac: %s", mac_addr.c_str());
        }
    }

    return status;
}

bool QcomDevice::QcomDeviceImpl::RegisterServer() {
    bool status = false;

    bt_uuid_t server_uuid;
    const std::vector<uint8_t> kServerUUID = {};
    qcom_utils::CopyUUID(&server_uuid, kServerUUID);
    gatt_ptr_->RegisterServerCallback(NULL, &server_uuid);

    reg_server_data_ = {};
    const bt_status_t reg_status = gatt_ptr_->register_server(&server_uuid);

    do {
        if (BT_STATUS_SUCCESS != reg_status) {
            LOG_E(kLogTag, "register_server failed, status = %d", reg_status);
            break;
        }

        constexpr int kGattConditionWaitDuration = 5;

        std::unique_lock<std::mutex> lock(reg_server_mutex_);
        if (reg_server_cv_.wait_for(lock, std::chrono::seconds(kGattConditionWaitDuration), [&reg_server_data_] {
                                                                                                return reg_server_data_.data_ready_;
                                                                                            })) {
            if (BT_STATUS_SUCCESS == reg_server_data_.status_) {
                server_if_ = reg_server_data_.interface_;
                status = true;
                LOG_I(kLogTag, "register_server success with iface: %d", server_if_);
            } else {
                LOG_E(kLogTag, "post cb wait, register_server failed, status = %d", reg_server_data_.status_);
            }

        } else {
            LOG_E(kLogTag, "post cb wait, register_server failed with no response");
        }

    } while (false);

    return status;
}

bool QcomDevice::QcomDeviceImpl::AddService(const std::vector<uint8_t> &char_uuid) {
    bool status = false;

    bt_uuid_t service_uuid;
    qcom_utils::CopyUUID(&service_uuid, char_uuid);

    btgatt_srvc_id_t srvc_id{};
    srvc_id.id.inst_id = 0;   // 1 instance
    srvc_id.is_primary = 1;   // Primary addition
    srvc_id.id.uuid = service_uuid;

    add_service_data_ = {};
    const bt_status_t add_srv_status = gatt_ptr_->add_service(server_if_, &srvc_id, 4);

    do {
        if (BT_STATUS_SUCCESS != add_srv_status) {
            LOG_E(kLogTag, "add_service failed, status = %d", add_srv_status);
            break;
        }

        constexpr int kGattConditionWaitDuration = 5;

        std::unique_lock<std::mutex> lock(reg_server_mutex_);
        if (reg_server_cv_.wait_for(lock, std::chrono::seconds(kGattConditionWaitDuration), [&add_service_data_] {
                                                                                                return add_service_data_.data_ready_;
                                                                                            })) {
            if (BT_STATUS_SUCCESS == add_service_data_.status_) {
                service_handle_ = add_service_data_.handle_;
                status = true;
                LOG_I(kLogTag, "add_service success with iface: %d & handle: %d", add_service_data_.interface_,
                        add_service_data_.handle_);
            } else {
                LOG_E(kLogTag, "post cb wait, add_service failed, status = %d", add_service_data_.status_);
            }
        } else {
            LOG_E(kLogTag, "post cb wait, add_service failed with no response");
        }

    } while (false);

    return status;
}

bool QcomDevice::QcomDeviceImpl::RegisterClient() {
    bool status = false;

    bt_uuid_t client_uuid{};
    const std::vector<uint8_t> kClientUUID = {0xFF, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30};
    qcom_utils::CopyUUID(&client_uuid, kClientUUID);

    gatt_ptr_->RegisterClientCallback(NULL, &client_uuid);

    reg_client_data_ = {};
    const bt_status_t reg_status = gatt_ptr_->register_client(&client_uuid);

    do {
        if (BT_STATUS_SUCCESS != reg_status) {
            LOG_E(kLogTag, "register_client failed, status = %d", reg_status);
            break;
        }

        constexpr int kGattConditionWaitDuration = 5;

        std::unique_lock<std::mutex> lock(reg_client_mutex_);
        if (reg_client_cv_.wait_for(lock, std::chrono::seconds(kGattConditionWaitDuration), [&reg_client_data_] {
                                                                                                return reg_client_data_.data_ready_;
                                                                                            })) {
            if (BT_STATUS_SUCCESS == reg_client_data_.status_) {
                client_if_ = reg_client_data_.interface_;
                status = true;
                LOG_I(kLogTag, "register_client success with iface: %d", client_if_);
            } else {
                LOG_E(kLogTag, "post cb wait, register_client failed, status = %d", reg_client_data_.status_);
            }

        } else {
            LOG_E(kLogTag, "post cb wait, register_client failed with no response");
        }

    } while (false);

    return status;
}

bool QcomDevice::QcomDeviceImpl::UnregisterClient() {
    bool status = false;

    if (-1 != client_if_) {

        gatt_ptr_->UnRegisterClientCallback(client_if_);

        const bt_status_t unreg_status = gatt_ptr_->unregister_client(client_if_);
        if (BT_STATUS_SUCCESS == unreg_status) {
            LOG_I(kLogTag, "unregister_client success");
            status = true;
        } else {
            LOG_E(kLogTag, "register_client failed, status = %d", unreg_status);
        }

        client_if_ = -1;
    } else {
        status = true;
    }

    return status;
}

bool QcomDevice::QcomDeviceImpl::SetAdvertisementData(int advertising_interval, const std::vector<uint8_t> &data) {

    LOG_I(kLogTag, "Inside %s", __func__);

    bool status = false;

    constexpr bool set_scan_resp = false;
    constexpr bool include_name = false;
    constexpr bool include_tx_power = false;
    const int min_interval = advertising_interval;
    constexpr int max_interval = 1000;
    constexpr int appearance = 0;
    constexpr uint16_t service_data_len = 0;
    char* service_data = nullptr;
    constexpr uint16_t service_uuid_len = 0;
    char* service_uuid = nullptr;

    const bt_status_t set_adv_data_status = gatt_ptr_->set_adv_data(client_if_, set_scan_resp, include_name, include_tx_power,
                                                        min_interval, max_interval, appearance,
                                                        // data.size(), const_cast<char *>(data.c_str()),
                                                        data.size(), const_cast<char *>(reinterpret_cast<const char *>(data.data())),
                                                        service_data_len, service_data, service_uuid_len, service_uuid);

    if (BT_STATUS_SUCCESS == set_adv_data_status) {
        LOG_I(kLogTag, "set_adv_data success");
        status = true;
    } else {
        LOG_E(kLogTag, "set_adv_data failed, status = %d", set_adv_data_status);
    }

    return status;
}

bool QcomDevice::QcomDeviceImpl::ToggleAdvertise(bool start) {
    bool status = false;

    toggle_advertise_data_ = {};
    const bt_status_t adv_start_stop_status = gatt_ptr_->listen(client_if_, start);

    do {
        if (BT_STATUS_SUCCESS != adv_start_stop_status) {
            LOG_E(kLogTag, "adv_start_stop listen failed, status = %d", adv_start_stop_status);
            break;
        }

        constexpr int kGattConditionWaitDuration = 5;

        std::unique_lock<std::mutex> lock(bt_adv_mutex_);
        if (bt_adv_cv_.wait_for(lock, std::chrono::seconds(kGattConditionWaitDuration), [&toggle_advertise_data_] {
                                                                                                return toggle_advertise_data_.data_ready_;
                                                                                            })) {
            if (BT_STATUS_SUCCESS == toggle_advertise_data_.status_) {
                status = true;
                LOG_I(kLogTag, "adv_start_stop listen success with iface: %d start: %d", toggle_advertise_data_.interface_, start);
            } else {
                LOG_E(kLogTag, "post cb wait, adv_start_stop listen failed, status = %d", toggle_advertise_data_.status_);
            }

        } else {
            LOG_E(kLogTag, "post cb wait, adv_start_stop listen failed with no response");
        }

    } while (false);

    return status;
}

bool QcomDevice::QcomDeviceImpl::ToggleScan(bool start) {
    bool status = false;

    const bt_status_t scan_start_stop_status = gatt_ptr_->scan(start, client_if_);
    if (BT_STATUS_SUCCESS == scan_start_stop_status) {
        LOG_I(kLogTag, "scan_start_stop success");
        status = true;
    } else {
        LOG_E(kLogTag, "scan toggle failed, status = %d", scan_start_stop_status);
    }

    return status;
}

bool QcomDevice::QcomDeviceImpl::Connect(const bt_bdaddr_t *addr, int &conn_id_out) {
    LOG_I(kLogTag, "Inside %s", __func__);

    bool status = false;
    const bool is_direct = true;

    do {

        const std::string mac = qcom_utils::GetMacAddressAsString(*addr);

        PrintConnectionMap();

        {
            const std::lock_guard<std::mutex> lock(connection_state_mutex_);

            if (connecting_devices_set_.find(mac) != connecting_devices_set_.end()) {
                LOG_W(kLogTag, "Already connecting to device: %s", mac.c_str());
                break;
            }

            if (connected_devices_map_.find(mac) != connected_devices_map_.end()) {
                LOG_W(kLogTag, "Already connected to device: %s", mac.c_str());
                conn_id_out = connected_devices_map_[mac];
                status = true;
                break;
            }

            if (connecting_devices_set_.size() + connected_devices_map_.size() >= kMaxConnectDevices) {
                LOG_W(kLogTag, "Device limit reached. connecting: %lu connected: %lu", connecting_devices_set_.size(), connected_devices_map_.size());
                break;
            }

            LOG_I(kLogTag, "Current size connecting: %lu connected: %lu", connecting_devices_set_.size(), connected_devices_map_.size());

            connecting_devices_set_.emplace(mac);
        }

        client_connect_data_ = {};
        const bt_status_t connect_status = gatt_ptr_->clientConnect(client_if_, addr, is_direct, GATT_TRANSPORT_LE);

        std::unique_lock<std::mutex> lock(client_connect_mutex_);

        if (BT_STATUS_SUCCESS != connect_status) {
            LOG_E(kLogTag, "clientConnect failed, status = %d", connect_status);
            break;
        }

        constexpr int kGattConditionWaitDuration = 5;

        if (client_connect_cv_.wait_for(lock, std::chrono::seconds(kGattConditionWaitDuration),
                                        [&client_connect_data_] {
                                            return client_connect_data_.data_ready_;
                                        })) {

            if (BT_STATUS_SUCCESS == client_connect_data_.status_) {
                const std::string mac_addr = qcom_utils::GetMacAddressAsString(client_connect_data_.addr_);
                status = true;
                conn_id_out = client_connect_data_.conn_id_;
                LOG_I(kLogTag, "clientConnect success with addr: %s iface: %d id: %d",
                        mac_addr.c_str(), client_connect_data_.interface_, client_connect_data_.conn_id_);
            } else {
                LOG_E(kLogTag, "post cb wait, clientConnect failed, status = %d", client_connect_data_.status_);
            }

        } else {
            LOG_E(kLogTag, "post cb wait, clientConnect failed with no response");
        }

    } while (false);

    return status;
}

bool QcomDevice::QcomDeviceImpl::Disconnect(const bt_bdaddr_t *addr, int conn_id) {
    bool status = false;

    client_disconnect_data_ = {};
    const bt_status_t disconnect_status = gatt_ptr_->clientDisconnect(client_if_, addr, conn_id);

    do {
        if (BT_STATUS_SUCCESS != disconnect_status) {
            LOG_E(kLogTag, "clientDisconnect failed for id: %d, status = %d", conn_id, disconnect_status);
            break;
        }

        constexpr int kGattConditionWaitDuration = 5;

        std::unique_lock<std::mutex> lock(client_disconnect_mutex_);
        if (client_disconnect_cv_.wait_for(lock, std::chrono::seconds(kGattConditionWaitDuration), [&client_disconnect_data_] {
                                                                                                    return client_disconnect_data_.data_ready_;
                                                                                                    })) {
            if (BT_STATUS_SUCCESS == client_disconnect_data_.status_) {
                const std::string mac_addr = qcom_utils::GetMacAddressAsString(client_disconnect_data_.addr_);
                status = true;
                LOG_I(kLogTag, "clientDisconnect success with addr: %s iface: %d id: %d",
                        mac_addr.c_str(), client_disconnect_data_.interface_, client_disconnect_data_.conn_id_);
            } else {
                LOG_E(kLogTag, "post cb wait, clientDisconnect failed, status = %d", client_disconnect_data_.status_);
            }

        } else {
            LOG_E(kLogTag, "post cb wait, clientDisconnect failed with no response");
        }

    } while (false);

    return status;
}

bool QcomDevice::QcomDeviceImpl::GetGattDb(int conn_id) {

    bool status = false;

    db_search_service_response_data_ = {};

    const bt_status_t get_db_status = gatt_ptr_->get_gatt_db(conn_id);

    do {

        if (BT_STATUS_SUCCESS != get_db_status) {
            LOG_E(kLogTag, "get_gatt_db failed for conn_id: %d, status = %d", conn_id, get_db_status);
            break;
        }

        constexpr int kGattConditionWaitDuration = 5;

        std::unique_lock<std::mutex> lock(db_search_service_mutex_);
        if (db_search_service_cv_.wait_for(lock, std::chrono::seconds(kGattConditionWaitDuration), [&db_search_service_response_data_] {
                                                                                                    return db_search_service_response_data_.data_ready_;
                                                                                                    })) {
            LOG_I(kLogTag, "get_gatt_db success with id: %d matched?: %d handle: %d",
                    db_search_service_response_data_.conn_id_, db_search_service_response_data_.matched_,
                    db_search_service_response_data_.charac_handle_);

            if (db_search_service_response_data_.matched_) {
                status = true;
                read_write_characteristic_handle_ = db_search_service_response_data_.charac_handle_;
            }

        } else {
            LOG_E(kLogTag, "post cb wait, search_service failed with no response");
        }

    } while (false);

    return status;
}


bool QcomDevice::QcomDeviceImpl::SearchService(int conn_id) {
    bool status = false;

    search_complete_data_ = {};

    const bt_status_t search_status = gatt_ptr_->search_service(conn_id, nullptr);

    do {
        if (BT_STATUS_SUCCESS != search_status) {
            LOG_E(kLogTag, "search_service failed for id: %d, status = %d", conn_id, search_status);
            break;
        }

        constexpr int kGattConditionWaitDuration = 5;

        std::unique_lock<std::mutex> lock(search_complete_mutex_);
        if (search_complete_cv_.wait_for(lock, std::chrono::seconds(kGattConditionWaitDuration), [&search_complete_data_] {
                                                                                                    return search_complete_data_.data_ready_;
                                                                                                    })) {
            if (BT_STATUS_SUCCESS == search_complete_data_.status_) {
                status = true;
                LOG_I(kLogTag, "search_service success on conn: %d", search_complete_data_.conn_id_);
            } else {
                LOG_E(kLogTag, "post cb wait, search_service failed, status = %d", search_complete_data_.status_);
            }

        } else {
            LOG_E(kLogTag, "post cb wait, search_service failed with no response");
        }

    } while (false);

    return status;
}

bool QcomDevice::QcomDeviceImpl::WriteCharacteristic(const std::string &data, int conn_id) {
    bool status = false;

    /*
    * https://android.googlesource.com/platform/system/bt/+/master/stack/include/gatt_api.h:468
    * GATT write type enumeration
    * enum { GATT_WRITE_NO_RSP = 1, GATT_WRITE, GATT_WRITE_PREPARE };
    */
    constexpr int GATT_WRITE = 2;
    constexpr int AUTH_REQ = 0;

    write_char_response_data_ = {};
    const bt_status_t write_status = gatt_ptr_->write_characteristic(conn_id, read_write_characteristic_handle_, GATT_WRITE, data.size(), AUTH_REQ, const_cast<char *>(data.c_str()));

    do {
        if (BT_STATUS_SUCCESS != write_status) {
            LOG_E(kLogTag, "write_characteristic failed, status = %d", write_status);
            break;
        }

        constexpr int kGattConditionWaitDuration = 5;

        std::unique_lock<std::mutex> lock(write_char_mutex_);

        if (write_char_cv_.wait_for(lock, std::chrono::seconds(kGattConditionWaitDuration), [&write_char_response_data_] {
                                                                                                return write_char_response_data_.data_ready_;
                                                                                            })) {
            if (BT_STATUS_SUCCESS == write_char_response_data_.status_) {
                status = true;
                LOG_I(kLogTag, "write_characteristic success on handle: %d, conn: %d",
                    write_char_response_data_.handle_, write_char_response_data_.conn_id_);
            } else {
                LOG_E(kLogTag, "post cb wait, write_characteristic failed, status = %d", write_char_response_data_.status_);
            }

        } else {
            LOG_E(kLogTag, "post cb wait, write_characteristic failed with no response");
        }

    } while (false);

    return status;
}

bool QcomDevice::QcomDeviceImpl::ReadCharacteristic(std::vector<uint8_t> &data_out, int conn_id) {
    bool status = false;

    constexpr int AUTH_REQ = 0;

    read_char_response_data_ = {};
    const bt_status_t read_status = gatt_ptr_->read_characteristic(conn_id, read_write_characteristic_handle_, AUTH_REQ);

    do {
        if (BT_STATUS_SUCCESS != read_status) {
            LOG_E(kLogTag, "read_characteristic failed, status = %d", read_status);
            break;
        }

        constexpr int kGattConditionWaitDuration = 5;

        std::unique_lock<std::mutex> lock(read_char_mutex_);
        if (read_char_cv_.wait_for(lock, std::chrono::seconds(kGattConditionWaitDuration), [&read_char_response_data_] {
                                                                                                return read_char_response_data_.data_ready_;
                                                                                            })) {
            if (BT_STATUS_SUCCESS == read_char_response_data_.status_) {
                status = true;
                data_out = std::move(read_char_response_data_.data_);
                LOG_I(kLogTag, "read_characteristic success on handle: %d, conn: %d",
                    read_char_response_data_.handle_, read_char_response_data_.conn_id_);
            } else {
                LOG_E(kLogTag, "post cb wait, read_characteristic failed, status = %d", read_char_response_data_.status_);
            }

        } else {
            LOG_E(kLogTag, "post cb wait, read_characteristic failed with no response");
        }

    } while (false);

    return status;
}

bool QcomDevice::QcomDeviceImpl::SetWakeAlarm(uint64_t delay_millis, bool should_wake, alarm_cb cb, void *data) {
    LOG_I(kLogTag, "Inside %s, delay_millis: %lu, should_wake: %d", __func__, delay_millis, should_wake);
    return BT_STATUS_SUCCESS;
}

int QcomDevice::QcomDeviceImpl::AcquireWakeLock(const char *lock_name) {
    LOG_I(kLogTag, "Inside %s, lock_name: %s", __func__, lock_name);
    return BT_STATUS_SUCCESS;
}

int QcomDevice::QcomDeviceImpl::ReleaseWakeLock(const char *lock_name) {
    LOG_I(kLogTag, "Inside %s, lock_name: %s", __func__, lock_name);
    return BT_STATUS_SUCCESS;
}

void QcomDevice::QcomDeviceImpl::AdapterStateChangeCallback(bt_state_t state) {
    LOG_I(kLogTag, "Inside %s, state = %d", __func__, state);

    {
        const std::lock_guard<std::mutex> lock(state_change_mutex_);
        adapter_state_change_data_.data_ready_ = true;
        adapter_state_change_data_.state_ = state;
    }
    state_change_cv_.notify_all();
}

void QcomDevice::QcomDeviceImpl::AdapterPropertiesCb(bt_status_t status, int num_properties, bt_property_t *properties) {
    LOG_I(kLogTag, "Inside %s, status: %d", __func__, static_cast<int>(status));
}

void QcomDevice::QcomDeviceImpl::RemoteDevicePropertiesCb(bt_status_t status, bt_bdaddr_t *bd_addr, int num_properties,
                                                          bt_property_t *properties) {
    // LOG_I(kLogTag, "Inside %s, status: %d", __func__, static_cast<int>(status));
}

void QcomDevice::QcomDeviceImpl::DeviceFoundCb(int num_properties, bt_property_t *properties) {
    LOG_I(kLogTag, "Inside %s", __func__);

    bt_property_t *props = new bt_property_t[num_properties];

    do {
        if (nullptr == props) {
            LOG_E (kLogTag, "Error allocating memory for properties");
            break;
        }

        memcpy(props, properties, num_properties * sizeof(bt_property_t));

        bool memory_unavailable = false;
        for (size_t index = 0; index < num_properties; index++) {
            props[index].val = new char[properties[index].len];
            if (nullptr == props[index].val) {
                memory_unavailable = true;
                break;
            }
            memcpy(props[index].val, properties[index].val, properties[index].len);
        }

        if (memory_unavailable) {
            LOG_E (kLogTag, "Error allocating memory for property values");
            break;
        }

        bt_bdaddr_t bd_addr{};

        if (!GetValueFromPropertyList(num_properties, props, BT_PROPERTY_BDADDR, &bd_addr)) {
            LOG_E (kLogTag, "Error finding device address");
            break;
        }

        if (required_device_discovered_data_.data_required_) {
            const std::lock_guard<std::mutex> lock(device_discovered_mutex_);
            if (std::equal(std::begin(required_device_discovered_data_.addr_.address),
                           std::end(required_device_discovered_data_.addr_.address),
                           std::begin(bd_addr.address))) {
                required_device_discovered_data_.data_ready_ = true;
                device_discovered_cv_.notify_all();
            }
        }

        char device_name[248];

        if (GetValueFromPropertyList(num_properties, props, BT_PROPERTY_BDNAME, device_name)) {
            LOG_I (kLogTag, "Device found: %s", device_name);
        }

    } while (false);

    if (nullptr != props) {
        delete [] props;
        /* Free the memory used for properties */
        for (size_t index = 0; index < num_properties; index++) {
            if (nullptr != props[index].val) {
                delete [] props[index].val;
            }
        }
    }

}

void QcomDevice::QcomDeviceImpl::DiscoveryStateChangedCb(bt_discovery_state_t state) {
    LOG_I(kLogTag, "Inside %s, state = %d", __func__, state);

    {
        const std::lock_guard<std::mutex> lock(discovery_state_change_mutex_);
        discovery_state_change_data_.data_ready_ = true;
        discovery_state_change_data_.state_ = state;
    }
    discovery_state_change_cv_.notify_all();

}

void QcomDevice::QcomDeviceImpl::PinRequestCb(bt_bdaddr_t *bd_addr, bt_bdname_t *bd_name, uint32_t cod, bool min_16_digit) {
    LOG_I(kLogTag, "Inside %s", __func__);
}

void QcomDevice::QcomDeviceImpl::SspRequestCb(bt_bdaddr_t *bd_addr, bt_bdname_t *bd_name, uint32_t cod,
                                              bt_ssp_variant_t pairing_variant, uint32_t pass_key) {
    LOG_I(kLogTag, "Inside %s", __func__);
}

void QcomDevice::QcomDeviceImpl::BondStateChangedCb(bt_status_t status, bt_bdaddr_t *bd_addr, bt_bond_state_t state) {
    LOG_I(kLogTag, "Inside %s, status: %d", __func__, static_cast<int>(status));
}

void QcomDevice::QcomDeviceImpl::AclStateChangedCb(bt_status_t status, bt_bdaddr_t *bd_addr, bt_acl_state_t state) {
    LOG_I(kLogTag, "Inside %s, status: %d", __func__, static_cast<int>(status));
}

void QcomDevice::QcomDeviceImpl::CbThreadEvent(bt_cb_thread_evt event) {
    LOG_I(kLogTag, "Inside %s", __func__);
}

void QcomDevice::QcomDeviceImpl::DutModeRecvCb (uint16_t opcode, uint8_t *buf, uint8_t len) {
    LOG_I(kLogTag, "Inside %s", __func__);
}

void QcomDevice::QcomDeviceImpl::LeTestModeRecvCb (bt_status_t status, uint16_t packet_count) {
    LOG_I(kLogTag, "Inside %s", __func__);
}

void QcomDevice::QcomDeviceImpl::HciRawEventShow(uint8_t event_code, uint8_t *buf, uint8_t len){
    LOG_I(kLogTag, "Inside %s", __func__);
}

bool QcomDevice::QcomDeviceImpl::LoadBtStack() {
    LOG_I(kLogTag, "Entered %s", __func__);

    bool status = false;

    do {
        if (nullptr != bt_device_) {
            LOG_I(kLogTag, "bt_device_ is already initialised");
            status = true;
            break;
        }

        static hw_module_t *module = nullptr;

        if (nullptr == module) {
            if (0 != hw_get_module(BT_STACK_MODULE_ID, (hw_module_t const **) &module)) {
                LOG_E(kLogTag, "hw_get_module failed");
                break;
            }

            if (nullptr == module) {
                LOG_E(kLogTag, "module is null");
                break;
            }
        }

        if (0 != module->methods->open(module, BT_STACK_MODULE_ID, &device_)) {
            LOG_E(kLogTag, "open module failed");
            break;
        }

        bt_device_ = reinterpret_cast<bluetooth_device_t *>(device_);
        bt_interface_ = bt_device_->get_bluetooth_interface();

        if (nullptr == bt_interface_) {
            LOG_E(kLogTag, "bt_interface_ is null");
            bt_device_->common.close(reinterpret_cast<hw_device_t *>(&bt_device_->common));
            bt_device_ = nullptr;
            break;
        }

        status = true;
        LOG_I(kLogTag, "LoadBtStack success");
    } while (false);

    return status;
}

bool QcomDevice::QcomDeviceImpl::DeinitializeGatt() {
    LOG_I(kLogTag, "Entered %s", __func__);

    bool status = false;

    do {
        if (!gatt_ptr_) {
            LOG_I(kLogTag, "gatt_ptr_ is already deinitialized in %s", __func__);
            status = true;
            break;
        }

        const bool disable_gatt_success = gatt_ptr_->HandleDisableGatt();

        if (!disable_gatt_success) {
            LOG_E(kLogTag, "(%s) HandleDisableGatt Failed \n ",__func__);
        }

        gatt_ptr_.reset(nullptr);

        if (disable_gatt_success) {
            status = true;
        }

    } while (false);

    return status;
}

void QcomDevice::QcomDeviceImpl::UnLoadBtStack() {
    LOG_I(kLogTag, "Entered %s", __func__);
    {
        constexpr int kUnloadTaskTimeout = 5;
        const task_result_t timed_task_result = nd_timed_task([](void *args) -> bool {
                                                                  QcomDeviceImpl *ptr = reinterpret_cast<QcomDeviceImpl *>(args);
                                                                  if (nullptr != ptr->bt_interface_) {
                                                                      auto interface_ptr = ptr->bt_interface_;
                                                                      ptr->bt_interface_ = nullptr;
                                                                      interface_ptr->cleanup();
                                                                  }
                                                                  return true;
                                                              }, kUnloadTaskTimeout, this, "BT interface close task");
        if(TASK_SUCCESS != timed_task_result) {
            LOG_E(kLogTag, "BT interface close timed task failed with result %d", timed_task_result);
        }
    }
    {
        constexpr int kUnloadTaskTimeout = 5;
        const task_result_t timed_task_result = nd_timed_task([](void *args) -> bool {

                                                                  QcomDeviceImpl *ptr = reinterpret_cast<QcomDeviceImpl *>(args);
                                                                  if (nullptr != ptr->bt_device_) {
                                                                      auto device_ptr = ptr->bt_device_;
                                                                      ptr->bt_device_ = nullptr;
                                                                      device_ptr->common.close(reinterpret_cast<hw_device_t *>(&ptr->bt_device_->common));
                                                                  }
                                                                  return true;
                                                              }, kUnloadTaskTimeout, this, "BT interface close task");
        if(TASK_SUCCESS != timed_task_result) {
            LOG_E(kLogTag, "BT interface close timed task failed with result %d", timed_task_result);
        }
    }
}

bool QcomDevice::QcomDeviceImpl::Init() {
    LOG_I(kLogTag, "Entered %s", __func__);

    bool status = false;

    do {

        system("killall -s SIGTERM btproperty");

        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        system("btproperty &");

        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        if (!LoadBtStack()) {
            LOG_E(kLogTag, "LoadBtStack failed");
            break;
        }

        //////////////////////////////////////////
        //Taken from GAP constructor
        //////////////////////////////////////////

        if ((BT_STATUS_SUCCESS != bt_interface_->init(&bluetooth_callbacks_))) {
            LOG_E(kLogTag, "bt_interface_->init failed");
            break;
        }

        if (BT_STATUS_SUCCESS != bt_interface_->set_os_callouts(&os_callouts_)) {
            LOG_E(kLogTag, "bt_interface_->set_os_callouts failed");
            break;
        }

        system("killall -s SIGTERM wcnssfilter");
        system("killall -s SIGTERM btsnoop");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        LOG_I(kLogTag, "Init success");

        status = true;
    } while (false);

    return status;
}

void QcomDevice::QcomDeviceImpl::DeInit() {
    LOG_I(kLogTag, "Entered %s", __func__);

    UnLoadBtStack();
}

bool QcomDevice::QcomDeviceImpl::Enable() {
    LOG_I(kLogTag, "Entered %s", __func__);

    bool status = false;
    do {

        if (!Init()) {
            LOG_E(kLogTag, "Init failed");
            break;
        }

        if (nullptr == bt_interface_) {
            LOG_E(kLogTag, "bt_interface_ is null in %s", __func__);
            break;
        }

        adapter_state_change_data_ = {};

        const bool guest_mode = false;
        const auto enable_status = bt_interface_->enable(guest_mode);
        if (BT_STATUS_SUCCESS != enable_status) {
            LOG_E(kLogTag, "bt_interface_->enable failed : %d", enable_status);
            break;
        }

        constexpr int kGattConditionWaitDuration = 5;

        std::unique_lock<std::mutex> lock(state_change_mutex_);
        if (state_change_cv_.wait_for(lock, std::chrono::seconds(kGattConditionWaitDuration), [&adapter_state_change_data_] {
                                                                                                return adapter_state_change_data_.data_ready_;
                                                                                            })) {

            if (BT_STATE_ON == adapter_state_change_data_.state_) {
                status = true;
                bt_state_ = adapter_state_change_data_.state_;
                LOG_I(kLogTag, "BT interface enable success");
            } else {
                LOG_E(kLogTag, "post cb wait, interface enable failed, state = %d", adapter_state_change_data_.state_);
            }

        } else {
            LOG_E(kLogTag, "post cb wait, interface enable failed with no response");
        }

        if (status) {
            if (!InitializeGatt()) {
                LOG_E(kLogTag, "InitializeGatt failed");
                status = false;
                break;
            }
        }

    } while (false);

    return status;
}

bool QcomDevice::QcomDeviceImpl::Disable() {
    LOG_I(kLogTag, "Entered %s", __func__);

    bt_state_ = BT_STATE_OFF;

    bool status = false;
    do {

        if (nullptr == bt_interface_) {
            LOG_E(kLogTag, "bt_interface_ is null in %s", __func__);
            break;
        }

        DisconnectExistingDevices();

        // TODO(sunils): Unregister server if started
        if (!UnregisterClient()) {
            LOG_E(kLogTag, "UnregisterClient failed in %s", __func__);
        }

        DeinitializeGatt();

        adapter_state_change_data_ = {};
        const auto disable_status = bt_interface_->disable();
        if (BT_STATUS_SUCCESS != disable_status) {
            LOG_E(kLogTag, "bt_interface_->disable failed : %d", disable_status);
            break;
        }

        constexpr int kGattConditionWaitDuration = 5;

        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // sleep before executing disable

        std::unique_lock<std::mutex> lock(state_change_mutex_);
        if (state_change_cv_.wait_for(lock, std::chrono::seconds(kGattConditionWaitDuration), [&adapter_state_change_data_] {
                                                                                                return adapter_state_change_data_.data_ready_;
                                                                                            })) {

            if (BT_STATE_OFF == adapter_state_change_data_.state_) {
                status = true;
                bt_state_ = adapter_state_change_data_.state_;
                LOG_I(kLogTag, "BT interface disable success");
            } else {
                LOG_E(kLogTag, "post cb wait, interface disable failed, state = %d", adapter_state_change_data_.state_);
            }

        } else {
            LOG_E(kLogTag, "post cb wait, interface disable failed with no response");
        }

        DeInit();

    } while (false);

    {
        const std::lock_guard<std::mutex> lock(connection_state_mutex_);
        connecting_devices_set_.clear();
        connected_devices_map_.clear();
    }

    return status;
}

bool QcomDevice::QcomDeviceImpl::InitializeGatt() {
    LOG_I(kLogTag, "Entered %s", __func__);

    bool status = false;

    do {
        if (gatt_ptr_) {
            LOG_I(kLogTag, "gatt_ptr_ is already initialized in %s", __func__);
            status = true;
            break;
        }

        if (nullptr == bt_interface_) {
            LOG_E(kLogTag, "bt_interface_ is null in %s", __func__);
            break;
        }

        try {
            gatt_ptr_ = std::make_unique<Gatt>(bt_interface_);
            status = true;
        } catch (const std::bad_alloc& e) {
            LOG_E(kLogTag, "[%s:%d] Allocation failed what(): %s", __func__, __LINE__, e.what());
        } catch (const std::exception& e){
            LOG_E(kLogTag, "[%s:%d] Allocation failed what(): %s", __func__, __LINE__, e.what());
        } catch (...) {
            LOG_E(kLogTag, "[%s:%d] Caught an exception of an undetermined type", __func__, __LINE__);
        }

        if (!status) {
            break;
        }

        if (!gatt_ptr_->HandleEnableGatt(&gatt_callbacks_)) {
            LOG_E(kLogTag, "(%s) Gatt Initialization Failed \n ",__func__);
            status = false;
            break;
        }

        status = true;
    } while (false);

    return status;
}

bool QcomDevice::QcomDeviceImpl::IsEnabled() {
    return (BT_STATE_ON == bt_state_);
}

bool QcomDevice::Disable() {
    bool status = false;
    const std::lock_guard<std::mutex> lock(api_mutex_);
    if (device_impl_ptr_) {
        status = device_impl_ptr_->Disable();
    }
    return status;
}

bool QcomDevice::Enable() {
    bool status = false;
    const std::lock_guard<std::mutex> lock(api_mutex_);
    if (device_impl_ptr_) {
        status = device_impl_ptr_->Enable();
    }
    return status;
}

bool QcomDevice::IsEnabled() {
    bool status = false;
    const std::lock_guard<std::mutex> lock(api_mutex_);
    if (device_impl_ptr_) {
        status = device_impl_ptr_->IsEnabled();
    }
    return status;
}

bool QcomDevice::LeScanOff() {
    bool status = false;
    const std::lock_guard<std::mutex> lock(api_mutex_);
    if (device_impl_ptr_) {
        status = device_impl_ptr_->LeScanOff();
    }
    return status;
}

bool QcomDevice::LeScanOn() {
    bool status = false;
    const std::lock_guard<std::mutex> lock(api_mutex_);
    if (device_impl_ptr_) {
        status = device_impl_ptr_->LeScanOn();
    }
    return status;
}

bool QcomDevice::RegisterLeEventCallback(nd::interface::LeEventCb cb) {
    bool status = false;
    const std::lock_guard<std::mutex> lock(api_mutex_);
    if (device_impl_ptr_) {
        status = device_impl_ptr_->RegisterLeEventCallback(cb);
    }
    return status;
}

bool QcomDevice::ReadCharacteristicData(const std::string& mac_addr, const std::string &uuid_str,
                                        std::vector<uint8_t> &result_out, void *caller_arg) {
    bool status = false;
    const std::lock_guard<std::mutex> lock(api_mutex_);
    if (device_impl_ptr_) {
        status = device_impl_ptr_->ReadCharacteristicData(mac_addr, uuid_str, result_out);
    }
    return status;
}

bool QcomDevice::ReadCharacteristicData(const std::string &mac_addr,
                                        std::unordered_map<std::string, std::vector<uint8_t>> &charac_data_out,
                                        void *caller_arg) {
    bool status = false;
    const std::lock_guard<std::mutex> lock(api_mutex_);
    if (device_impl_ptr_) {
        status = device_impl_ptr_->ReadCharacteristicData(mac_addr, charac_data_out);
    }
    return status;
}

bool QcomDevice::StartBeaconAdvertising(int advertising_interval, const std::vector<uint8_t> &advertising_uuid, int major_number,
                                        int minor_number, int8_t rssi_value) {
    bool status = false;
    const std::lock_guard<std::mutex> lock(api_mutex_);
    if (device_impl_ptr_) {
        status = device_impl_ptr_->StartBeaconAdvertising(advertising_interval, advertising_uuid, major_number, minor_number, rssi_value);
    }
    return status;
}

bool QcomDevice::StartDiscovery() {
    bool status = false;
    const std::lock_guard<std::mutex> lock(api_mutex_);
    if (device_impl_ptr_) {
        status = device_impl_ptr_->StartDiscovery();
    }
    return status;
}

bool QcomDevice::StartServiceAdvertising(const std::string &advertise_name, const std::vector<uint8_t> &advertise_uuid,
                                         const std::string &advertise_data, uint32_t duration) {
    bool status = false;
    const std::lock_guard<std::mutex> lock(api_mutex_);
    if (device_impl_ptr_) {
        status = device_impl_ptr_->StartServiceAdvertising(advertise_name, advertise_uuid, advertise_data, duration);
    }
    return status;
}

bool QcomDevice::StopBeaconAdvertising() {
    bool status = false;
    const std::lock_guard<std::mutex> lock(api_mutex_);
    if (device_impl_ptr_) {
        status = device_impl_ptr_->StopBeaconAdvertising();
    }
    return status;
}

bool QcomDevice::StopDiscovery() {
    bool status = false;
    const std::lock_guard<std::mutex> lock(api_mutex_);
    if (device_impl_ptr_) {
        status = device_impl_ptr_->StopDiscovery();
    }
    return status;
}

bool QcomDevice::StopServiceAdvertising() {
    bool status = false;
    const std::lock_guard<std::mutex> lock(api_mutex_);
    if (device_impl_ptr_) {
        status = device_impl_ptr_->StopServiceAdvertising();
    }
    return status;
}

bool QcomDevice::WriteCharacteristicData(const std::string& mac_addr, const std::string &uuid_str,
                                         const std::vector<std::string> &data, nd::interface::AddressType addr_type,
                                         void *caller_arg) {
    bool status = false;
    const std::lock_guard<std::mutex> lock(api_mutex_);
    if (device_impl_ptr_) {
        status = device_impl_ptr_->WriteCharacteristicData(mac_addr, uuid_str, data, addr_type);
    }
    return status;
}

bool QcomDevice::SupportsScanAndConnectionParallelly() {
    return true;
}

bool QcomDevice::IsLeScanOn() {
    const std::lock_guard<std::mutex> lock(api_mutex_);
    return device_impl_ptr_->IsLeScanOn();
}

bool QcomDevice::DoGattSetup() {
    LOG_I(kLogTag, "%s not implemented", __func__);
    return true;
}

bool QcomDevice::TearDownGattSetup() {
    LOG_I(kLogTag, "%s not implemented", __func__);
    return true;
}

bool QcomDevice::SetScanParameters(int scan_interval, int scan_window) {
    return false;
}

std::unique_ptr<interface::IBluetooth> BackupDeviceHelper::CreateBackUpInstance() {
    return nullptr;
}

std::unique_ptr<interface::IBluetooth> DeviceHelper::CreateConcreteInstance() {
    return std::make_unique<QcomDevice>();
}

}  // namespace device

}  // namespace nd
