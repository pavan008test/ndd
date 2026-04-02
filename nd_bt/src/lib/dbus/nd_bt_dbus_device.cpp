/* Copyright (C) 2025 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, July 2025
 * Written by Deepak Chaurasiya <deepak.chaurasiya@netradyne.com>, July 2025
 */

#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdint>

#include <algorithm>
#include <condition_variable>
#include <iomanip>
#include <iostream>
#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <gio/gio.h>
#include <glib.h>
#include <log.h>
#include <nd_utils.h>
#include <service_utils.h>

// Raw HCI scan support (standalone, no dependency on HciDevice)
#include <errno.h>
#include <poll.h>
#include <unistd.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include <config_parser.h>

#include <nd_bt_dbus_device.hpp>

#ifndef EVT_LE_EXT_ADVERTISING_REPORT
#define EVT_LE_EXT_ADVERTISING_REPORT 0x0D
#endif

using nd::utils::Translator;

namespace nd {

namespace device {

static constexpr char kRadioFreqBin[]     = "/usr/sbin/rfkill";

static constexpr std::size_t BT_ADDRESS_STRING_SIZE = 18;
static const char *const kLogTag = "DBUS";

static constexpr const char *BLUEZ_SERVICE_NAME = "org.bluez";
static constexpr const char *kBtAdapterPath = "/org/bluez/";
static constexpr const char *kDefaultBtAdapterName = "hci0";
static constexpr const char *ADVERTISING_MANAGER_IFACE = "org.bluez.LEAdvertisingManager1";
static constexpr const char *DBUS_OM_IFACE = "org.freedesktop.DBus.ObjectManager";
static constexpr const char *BLUEZ_ADAPTER_INTERFACE = "org.bluez.Adapter1";
static constexpr const char *PROPERTIES_IFACE = "org.freedesktop.DBus.Properties";
static constexpr const char *BLUEZ_ADAPTER_PATH = "/org/bluez/hci0";
static constexpr const char *kPoweredProperty = "Powered";
static constexpr const char *kGVariantTypeBoolean = "b";
static constexpr const char *kGVariantTypeManagedObjects = "(a{oa{sa{sv}}})";
static constexpr const char *kGVariantTypeManagedObjectsIter = "{&oa{sa{sv}}}";
static constexpr const char *kGVariantTypeArrayUint16ToVariant = "a{qv}";  // Array of {uint16 -> variant}
static constexpr const char *kGVariantTypeUint16ToVariant = "{qv}";        // Single {uint16 -> variant}
static constexpr const char *kGVariantTypeArrayStringToVariant = "a{sv}";  // Array of {string -> variant}
static constexpr const char *kGVariantTypeStringToVariant = "{sv}";        // Single {string -> variant}
static constexpr const char *kGVariantTypeObjectPathAndArrayStringToVariant = "(o@a{sv})";
static constexpr const char *kGVariantTypeTupleArrayStringToVariant = "(@a{sv})";
static constexpr const char *kGVariantTypeArrayStringToVariantTuple = "(a{sv})";
static constexpr const char *kGVariantTypeString = "s";                           // Single string
static constexpr const char *kGVariantTypeArrayString = "as";                     // Array of strings
static constexpr const char *kGVariantTypeObjectAndInterfaces = "(&oa{sa{sv}})";  // Tuple: object path and interfaces
static constexpr const char *kGVariantTypePropertiesChangedWithRef =
    "(&sa{sv}as)";  // PropertiesChanged: {string -> variant} and array of strings
static constexpr const char *kGVariantTypeStringAndArrayStringToVariant =
    "{&s@a{sv}}";                                                        // Map: string to array of {string->variant}
static constexpr const char *kGVariantTypeStringToVariantRef = "{&sv}";  // Map: string to variant (with ref)
static constexpr const char *kGVariantTypeByte = "y";                    // Single byte
static constexpr const char *kGVariantTypeObjectPathAndArrayString =
    "(&oas)";                                                // Tuple: object path and array of strings
static constexpr const char *kGVariantTypeArrayByte = "ay";  // Array of bytes
static constexpr const char *kGVariantTypePropertiesChanged = "(sa{sv}as)";
static constexpr const char *kGVariantTypeTupleArrayByteArrayStringToVariant =
    "(@ay@a{sv})";  // Tuple: array of bytes and array of {string->variant}
static constexpr const char *kBluezGattCharacteristicInterface =
    "org.bluez.GattCharacteristic1";                                       // GATT characteristic interface
static constexpr const char *kBluezDeviceInterface = "org.bluez.Device1";  // Device interface
static constexpr const char *kDeviceString = "device";
static constexpr const char *kGattOptionTypeKey = "type";
static constexpr const char *kGattOptionTypeValue = "request";
static constexpr const char *kGattOptionOffsetKey = "offset";
static constexpr const char *kGVariantTypeObjectPathAndInterfaces =
    "{&o@a{sa{sv}}}";  // Tuple: object path and interfaces
static constexpr const char *kGVariantTypeSetAdapterProperty =
    "(ssv)";  // Tuple: (string, string, variant) for SetAdapterProperty
static constexpr const char *kGVariantTypeObjectPath = "(o)";   // Object path type
static constexpr gint kMaxDefaultConnectionCallTimeout = 5000;  // 5 seconds
static constexpr uint32_t kMaxDefaultCondWaitTimeout = 5;       // 5 seconds
static constexpr uint32_t services_resolved_time_out = 10;      // 10 seconds
// Offsets for fields in LE Extended Advertising Report
constexpr size_t EXT_ADV_ADDR_OFFSET = 3;
constexpr size_t EXT_ADV_RSSI_OFFSET = 13;
constexpr size_t EXT_ADV_DATA_LEN_OFFSET = 23;

// Extended Advertising Report
constexpr size_t EXT_ADV_FIXED_FIELDS_LEN = 24;

constexpr uint32_t kTimeoutSecs = 5;

enum class AdapterPropertyType {
    Powered,
    Discovering,
    Unknown
};

static const std::unordered_map<std::string, AdapterPropertyType> kAdapterPropertyTypeMap = {
    {"Powered", AdapterPropertyType::Powered},
    {"Discovering", AdapterPropertyType::Discovering}
    // Add more properties as needed
};

enum class InterfaceType {
    GattCharacteristic,
    Device,
    Unknown
};

static const std::unordered_map<std::string, InterfaceType> kInterfaceTypeMap = {
    {kBluezGattCharacteristicInterface, InterfaceType::GattCharacteristic},
    {kBluezDeviceInterface, InterfaceType::Device}
    // Add more interfaces as needed
};

struct ObjectParams {
    GDBusProxy *proxy;
    std::string address;
    gulong signal_handler_id;
    std::unordered_map<std::string, std::string> properties;  // {uuid, CharacteristicPath}
};

// Context struct for synchronization
struct RegisterAdvContext {
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;
    bool success = false;
};

static const gchar advertisement_xml[] =
    "<node>"
    "  <interface name='org.bluez.LEAdvertisement1'>"
    "    <method name='Release'/>"
    "    <property name='LocalName' type='s' access='read'/>"
    "    <property name='Type' type='s' access='read'/>"
    "    <property name='ServiceUUIDs' type='as' access='read'/>"
    "    <property name='ManufacturerData' type='a{qv}' access='read'/>"
    "    <property name='ServiceData' type='a{sv}' access='read'/>"
    "    <property name='Discoverable' type='b' access='read'/>"
    "    <property name='Flags' type='ay' access='read'/>"
    "  </interface>"
    "</node>";

class DbusUtil {
   public:
    static std::string CreateObjectPathFromMac(const std::string &mac,
                                               const std::string &adapter = kDefaultBtAdapterName) {
        constexpr const char *kDevPrefix = "/dev_";
        std::string path = kBtAdapterPath + adapter + kDevPrefix;
        for (auto const &character : mac) {
            if (':' == character) {
                path += '_';  // Replace ':' with '_'
            } else if (std::isxdigit(character)) {
                path += character;  // Keep hex characters
            } else {
                LOG_E(kLogTag, "Invalid character '%c' in MAC address", character);
                path = "";  // Return empty string on error
                break;
            }
        }
        return path;
    }

    static std::string CreateMacFromObjectPath(const std::string &object_path) {
        // Example input: "/org/bluez/hci0/dev_XX_XX_XX_XX_XX_XX"
        const std::string dev_prefix = "/dev_";
        auto pos = object_path.find(dev_prefix);
        if (pos == std::string::npos) {
            return "";
        }
        std::string mac_part = object_path.substr(pos + dev_prefix.length());
        // Replace '_' with ':'
        std::replace(mac_part.begin(), mac_part.end(), '_', ':');
        // Validate length (should be 17 for "XX:XX:XX:XX:XX:XX")
        if (mac_part.length() != 17) {
            return "";
        }
        return mac_part;
    }

    static bool UnblockBluetoothRadio() {
        bool status = false;

        do {
            if (!nd::utils::CommandExecutors::Execute(kRadioFreqBin, {"unblock", "bluetooth"}, kTimeoutSecs)) {
                LOG_E(kLogTag, "[%s:%d] ERROR: Execute failed for command: %s", __FUNCTION__, __LINE__, kRadioFreqBin);
                break;
            }
            status = true;
        } while (false);

        return status;
    }

    static bool BlockBluetoothRadio() {
        bool status = false;
        do {
            if (!nd::utils::CommandExecutors::Execute(kRadioFreqBin, {"block", "bluetooth"}, kTimeoutSecs)) {
                LOG_E(kLogTag, "[%s:%d] ERROR: Execute failed for command: %s", __FUNCTION__, __LINE__, kRadioFreqBin);
                break;
            }
            status = true;
        } while (false);

        return status;
    }

    static bool ResetBluetoothRadio() {
        bool status = false;
        LOG_I(kLogTag, "Resetting Bluetooth radio (block then unblock)...");

        do {
            // First block the Bluetooth radio
            if (!BlockBluetoothRadio()) {
                LOG_E(kLogTag, "Failed to block Bluetooth radio during reset");
                break;
            }

            LOG_I(kLogTag, "Bluetooth radio blocked successfully, waiting  before unblock...");
            std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Brief delay between operations

            // Then unblock the Bluetooth radio
            if (!UnblockBluetoothRadio()) {
                LOG_E(kLogTag, "Failed to unblock Bluetooth radio during reset");
                break;
            }
            LOG_I(kLogTag, "Bluetooth radio reset completed successfully");
            status = true;
        } while (false);

        return status;
    }
};

DbusBTDevice::DbusBTDevice(): dbus_device_impl_ptr_(std::make_unique<DbusDeviceImpl>()) {
    if (nullptr == dbus_device_impl_ptr_) {
        LOG_E(kLogTag, "[%s:%d]Failed to create DbusDeviceImpl", __func__, __LINE__);
    }
}

DbusBTDevice::~DbusBTDevice() {
    LOG_I(kLogTag, "[%s:%d]Destroying DbusBTDevice", __func__, __LINE__);
}

// Implementation class for DbusBTDevice, encapsulating all internal state and logic.
class DbusBTDevice::DbusDeviceImpl {
   public:
    DbusDeviceImpl();
    DbusDeviceImpl(const DbusDeviceImpl &) = delete;
    DbusDeviceImpl(DbusDeviceImpl &&) = delete;
    DbusDeviceImpl &operator=(const DbusDeviceImpl &) = delete;
    DbusDeviceImpl &operator=(DbusDeviceImpl &&) = delete;
    ~DbusDeviceImpl();

    // Start the LE scan loop in a thread
    bool StartLeScanLoop();
    void StopLeScanLoop();

    // Adapter power management
    bool Enable();     // Power on the Bluetooth adapter
    bool IsEnabled();  // Check if adapter is powered
    bool Disable();    // Power off the Bluetooth adapter

    // Advertising control
    bool StartBeaconAdvertising(int advertising_interval, const std::vector<uint8_t> &advertising_uuid,
                                int major_number, int minor_number,
                                int8_t rssi_value);  // Start iBeacon advertising
    bool StopBeaconAdvertising();                    // Stop iBeacon advertising

    // Segregated LE scan report handlers
    void HandleLeAdvertisingReport(const evt_le_meta_event *meta);
    void HandleLeExtAdvertisingReport(const evt_le_meta_event *meta);

    bool StartServiceAdvertising(const std::string &advertise_name, const std::vector<uint8_t> &advertise_uuid,
                                 const std::string &advertise_data,
                                 uint32_t duration);  // Start custom service advertising
    bool StopServiceAdvertising();                    // Stop custom service advertising

    // Device and scan management
    bool StartDiscovery();  // Start device discovery (scan)
    bool StopDiscovery();   // Stop device discovery (scan)
    bool IsLeScanOn();      // Is LE scan currently active

    // GATT characteristic write/read
    bool WriteCharacteristicData(const std::string &mac_addr, const std::string &uuid,
                                 const std::vector<std::string> &data, nd::interface::AddressType addr_type,
                                 void *caller_arg);  // Write to GATT characteristic

    bool ReadCharacteristicData(const std::string &mac_addr, const std::string &uuid, std::vector<uint8_t> &result_out,
                                void *caller_arg);  // Read a single GATT characteristic

    bool ReadCharacteristicData(const std::string &mac_addr,
                                std::unordered_map<std::string, std::vector<uint8_t>> &charac_data_out,
                                void *caller_arg);  // Read multiple characteristics for one device

    // Register callback for LE scan events
    bool RegisterLeEventCallback(nd::interface::LeEventCb cb);

    // bool ReadCharacteristicData(
    //     const std::unordered_map<std::string, std::vector<std::string>> &macToUuids,
    //     std::unordered_map<std::string, std::unordered_map<std::string, std::vector<uint8_t>>> &charac_data_out,
    //     void *caller_arg);  // Read characteristics for multiple devices

    // Initialization and advertisement helpers
    void OnBoot();                  // Setup signal handlers and initialize state
    std::string FindAdapterPath();  // Find the BlueZ adapter object path
    bool WaitForServicesResolved(const std::string& mac_addr, int timeout_seconds); // Wait for ServicesResolved property
    std::vector<uint8_t> CreateIbeaconData(const std::string &uuid_str, uint16_t major, uint16_t minor,
                                           int8_t tx_power);  // Build iBeacon payload

    bool RegisterAdvertisementObject(const std::string &advertisement_path, const std::string &local_name,
                                     const std::string &service_uuid, const std::vector<uint8_t> &manufacturer_data,
                                     const std::vector<uint8_t> &service_data);  // Register advertisement object

    bool RegisterAdvertisementAsync(const std::string &advertisement_path, const std::string &local_name,
                                    const std::string &service_uuid, const std::vector<uint8_t> &manufacturer_data,
                                    const std::vector<uint8_t> &service_data, uint16_t min_interval = 0,
                                    uint16_t max_interval = 0);  // Register advertisement asynchronously

    bool SetLeDiscoveryFilter();                             // Set scan/discovery filter
    gboolean ClearPrevDevices(const std::string &mac_addr);  // Clear previous devices from the list
    gboolean ClearPrevDevices();                             // Remove previously paired/known devices

    // Device connection/disconnection
    bool ConnectToDevice(const std::string &mac_addr);  // Connect to a device by MAC
    std::string FindCharacteristicPath(const std::string &mac_addr, const std::string &uuid);  // Find GATT char path
    bool DisconnectDevice(const std::string &mac_addr);                                        // Disconnect from device
    void RemoveDeviceSync(const std::vector<std::string> &objects);  // Remove device synchronously
    bool RemoveDeviceAsync(const std::string &device_path);          // Remove device asynchronously

    // BlueZ property helpers and signal handlers
    static void BluezPropertyValue(const gchar *key, GVariant *value);  // Print/parse BlueZ property
    static void OnPropertiesChanged(GDBusProxy *pProxy, GVariant *params, gpointer user_data,
                                    const gchar *object_path);  // Handle property change

    // Internal characteristic read/write
    bool WriteCharacteristic(const gchar *char_path, const guint8 *data, size_t data_len);  // Write to GATT char
    std::pair<bool, std::vector<uint8_t>> ReadCharacteristic(const gchar *char_path,
                                                             const gchar *mac_address);  // Read from GATT char

    // Asynchronous operation callbacks
    static void WriteValueCallback(GDBusConnection *connection, GAsyncResult *res, gpointer user_data);
    static void BluezDisconnectCallback(GDBusConnection *dbus_connection, GAsyncResult *res, gpointer user_data);
    static void ReadValueCallback(GDBusConnection *connection, GAsyncResult *res, gpointer user_data);
    static void RegisterAdvertisementCallback(GObject *source_object, GAsyncResult *res, gpointer user_data);
    static void OnAdapterChangedSignal(GDBusConnection *conn, const gchar *sender, const gchar *path,
                                       const gchar *interface, const gchar *signal, GVariant *params, void *user_data);
    static void OnDeviceAppearedSignal(GDBusConnection *connection, const gchar *sender_name, const gchar *object_path,
                                       const gchar *interface, const gchar *signal_name, GVariant *parameters,
                                       gpointer user_data);
    static void OnPropertiesHandler(GDBusProxy *proxy, gchar *sender_name, gchar *signal_name, GVariant *params,
                                    gpointer user_data);
    static void OnConnectCallback(GDBusConnection *con, GAsyncResult *res, gpointer user_data);
    static void OnDeviceDisappearedSignal(GDBusConnection *connection, const gchar *sender_name,
                                          const gchar *object_path, const gchar *interface, const gchar *signal_name,
                                          GVariant *parameters, gpointer user_data);
    static void RemoveDeviceAsyncCallback(GObject *source_object, GAsyncResult *res, gpointer user_data);

    // helper Function to Modularize the code
    static void HandleAdapterPoweredProperty(const char *key, GVariant *value, DbusDeviceImpl &self);
    static void HandleAdapterDiscoveringProperty(const char *key, GVariant *value, DbusDeviceImpl &self);
    static void HandleGattCharacteristicAppearance(const char *object, GVariant *properties, DbusDeviceImpl &self);
    static void HandleDeviceAppeared(GDBusConnection *connection, const char *object, GVariant *properties,
                                     DbusDeviceImpl &self);
    // --- Internal state variables ---

    // D-Bus and BlueZ objects
    GDBusConnection *connection_ = nullptr;  // D-Bus connection handle
    GDBusProxy *ad_manager_ = nullptr;       // Advertising manager proxy
    GMainLoop *loop_ = nullptr;              // Main event loop
    std::thread main_loop_thread_;           // Thread running the main loop

    // Adapter and advertisement paths
    std::string adapter_path_;                // Adapter object path
    std::string beacon_advertisement_path_;   // iBeacon advertisement path
    std::string service_advertisement_path_;  // Service advertisement path
    std::string name_to_write_;               // Device name for write

    // Signal subscription IDs
    guint prop_changed_ = 0;   // PropertiesChanged signal ID
    guint iface_added_ = 0;    // InterfacesAdded signal ID
    guint iface_removed_ = 0;  // InterfacesRemoved signal ID

    // Synchronization primitives for async operations
    std::mutex connect_mutex_;
    std::condition_variable connect_cv_;
    bool connect_done_ = false;
    bool connect_success_ = false;

    // In DbusDeviceImpl class (private section)
    std::mutex remove_device_mutex_;
    std::condition_variable remove_device_cv_;
    bool remove_device_done_ = false;
    bool remove_device_success_ = false;

    std::mutex scan_status_mutex_;
    std::condition_variable scan_status_cv_;
    bool scan_status_ = false;

    std::mutex adapter_status_mutex_;
    std::condition_variable adapter_status_cv_;
    bool adapter_powered_status_ = false;

    std::mutex write_mutex_;
    std::condition_variable write_cv_;
    bool write_done_ = false;
    bool write_success_ = false;

    std::mutex read_mutex_;
    std::condition_variable read_cv_;
    bool read_done_ = false;
    bool read_success_ = false;
    std::vector<uint8_t> last_read_data_;  // Last read value

    std::mutex disconnect_mutex_;
    std::condition_variable disconnect_cv_;
    bool disconnect_done_ = false;
    bool disconnect_success_ = false;
    std::string disconnect_object_path_;  // Path for disconnect

    static nd::interface::LeEventCb le_event_cb_;

    // Device discovery synchronization
    std::condition_variable device_found_cv_;
    std::mutex device_found_mutex_;
    std::string target_mac_addr_;  // MAC address being searched
    bool device_found_ = false;    // Device found flag

    std::mutex services_resolved_mutex_;
    std::condition_variable services_resolved_cv_;
    bool services_resolved_ = false;
    std::string services_resolved_mac_;

    // Characteristic discovery synchronization
    std::mutex char_discovery_mutex_;
    std::condition_variable char_discovery_cv_;
    bool char_discovery_done_ = false;  // Characteristic found flag
    // std::string discovered_char_path_;                // Discovered characteristic path
    std::string target_uuid_;  // Target characteristic UUID

    // Advertising state
    bool is_beacon_advertising_on_ = false;   // Is beacon advertising active
    bool is_service_advertising_on_ = false;  // Is service advertising active

    // Device listeners: map of object path to device parameters
    std::unordered_map<std::string, ObjectParams> listeners_;  // {object_path, ObjectParams}
    std::mutex listeners_mutex_;                     // Protects listeners_

    bool scanned_results_arrived_ = false;

    // Raw HCI scan state
    std::thread hci_scan_thread_;
    std::atomic<bool> stop_scan_{true};

    void LeScanLoop(std::promise<bool> &&scan_start_promise);

    enum class BtStackErr {
        kOk = 0,
        kEnableFailed,
        kDisableFailed,
        kScanFailed,
        kResetBluetoothRadioFailed,
    };
};

nd::interface::LeEventCb DbusBTDevice::DbusDeviceImpl::le_event_cb_ = nullptr;

bool DbusBTDevice::DbusDeviceImpl::StartLeScanLoop() {
    LOG_I(kLogTag, "Starting LE Scan Loop");
    constexpr uint32_t kScanWaitTimeout = 10;
    bool status = false;
    if (!stop_scan_) {
        LOG_I(kLogTag, "Scan has already begun");
        status = true;
    } else {
        std::promise<bool> scan_start_promise;
        std::future<bool> scan_start_future = scan_start_promise.get_future();
        stop_scan_ = false;
        hci_scan_thread_ = std::thread(&DbusDeviceImpl::LeScanLoop, this, std::move(scan_start_promise));

        if (scan_start_future.valid() && scan_start_future.wait_for(std::chrono::seconds(kScanWaitTimeout)) == std::future_status::ready) {
            status = scan_start_future.get();
        } else {
            LOG_E(kLogTag, "Timeout, scan could not start");
        }
    }
    return status;
}

void DbusBTDevice::DbusDeviceImpl::StopLeScanLoop(){
    LOG_I(kLogTag, "Stopping LE Scan Loop");
    if (0 < hci_scan_thread_.native_handle()) {
        stop_scan_ = true;
        if (hci_scan_thread_.joinable()) {
            hci_scan_thread_.join();
            LOG_I(kLogTag, "LeScanLoop Thread joined");
        }
    } else {
        LOG_E(kLogTag, "Looks like thread is already stopped or not running in %s", __func__);
    }
    LOG_I(kLogTag, "LE Scan Loop stopped");
}

DbusBTDevice::DbusDeviceImpl::DbusDeviceImpl() {
    do {
        // Initialize the GMainLoop
        loop_ = g_main_loop_new(nullptr, FALSE);
        if (!loop_) {
            LOG_E(kLogTag, "[%s:%d]Failed to create GMainLoop", __func__, __LINE__);
            break;
        }

        // Get a GDBusConnection
        GError *error = nullptr;
        connection_ = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
        if (error) {
            LOG_E(kLogTag, "Failed to get GDBusConnection: %s (code: %d)", error->message, error->code);
            g_error_free(error);
            break;
        }

        LOG_I(kLogTag, "GMainLoop and GDBusConnection initialized successfully");

        // Start the main loop in a member thread
        main_loop_thread_ = std::thread([this]() {
            LOG_I(kLogTag, "Starting GMainLoop");
            g_main_loop_run(this->loop_);
        });

        if (connection_) {
            OnBoot();
        }
    } while (false);
}

DbusBTDevice::DbusDeviceImpl::~DbusDeviceImpl() {
    // Stop raw HCI scan thread if running
    StopLeScanLoop();
    StopBeaconAdvertising();
    StopServiceAdvertising();
    StopDiscovery();
    // Disable(); -> TODO:(sunil) Do this once complete DBUS stack is in use.
    // Stop the main loop and clean up
    if (connection_) {
        g_dbus_connection_signal_unsubscribe(connection_, prop_changed_);
        g_dbus_connection_signal_unsubscribe(connection_, iface_added_);
        g_dbus_connection_signal_unsubscribe(connection_, iface_removed_);
        g_object_unref(connection_);
        connection_ = nullptr;
    }
    if (ad_manager_) {
        g_object_unref(ad_manager_);
        ad_manager_ = nullptr;
    }
    if (loop_) {
        g_main_loop_quit(loop_);
    }
    if (main_loop_thread_.joinable()) {
        main_loop_thread_.join();
        LOG_I(kLogTag, "Raw Main Loop thread joined");
    }
    if (loop_) {
        g_main_loop_unref(loop_);
        loop_ = nullptr;
    }
    LOG_I(kLogTag, "GMainLoop, GDBusConnection, and ad_manager cleaned up");
}

bool DbusBTDevice::DbusDeviceImpl::Enable() {
    bool result = false;
    LOG_I(kLogTag, "Enabling Bluetooth adapter...");

    GVariant *dbus_result = nullptr;
    GError *error = nullptr;
    constexpr const char *kSetAdapterPropertyMethod = "Set";

    dbus_result = g_dbus_connection_call_sync(
        connection_, BLUEZ_SERVICE_NAME, BLUEZ_ADAPTER_PATH, PROPERTIES_IFACE, kSetAdapterPropertyMethod,
        g_variant_new(kGVariantTypeSetAdapterProperty, BLUEZ_ADAPTER_INTERFACE, kPoweredProperty,
                      g_variant_new(kGVariantTypeBoolean, TRUE)),
        nullptr, G_DBUS_CALL_FLAGS_NONE, kMaxDefaultConnectionCallTimeout, nullptr, &error);

    if ((error != nullptr) && (dbus_result == nullptr)) {
        LOG_E(kLogTag, "Not able to enable the adapter: %s (code: %d)", error->message, error->code);
    } else {
        LOG_I(kLogTag, "Adapter Enable called Successfully");
    }

    if (error) {
        g_error_free(error);
    }
    if (dbus_result) {
        g_variant_unref(dbus_result);
    }

    {
        std::unique_lock<std::mutex> lock(adapter_status_mutex_);
        constexpr auto kAdapterEnableTimeout = std::chrono::seconds(5);
        if (adapter_status_cv_.wait_for(lock, kAdapterEnableTimeout, [this] { return adapter_powered_status_; })) {
            LOG_I(kLogTag, "Enable completed with status: success");
            result = true;
        } else {
            LOG_E(kLogTag, "Enable timed out.");
        }
    }

    return result;
}

bool DbusBTDevice::Enable() {
    bool status = false;
    const std::lock_guard<std::mutex> lock(api_mutex_);
    if (dbus_device_impl_ptr_) {
        status = dbus_device_impl_ptr_->Enable();
    }
    return status;
}

bool DbusBTDevice::DbusDeviceImpl::IsEnabled() {
    bool status = false;
    {
        std::lock_guard<std::mutex> lock(adapter_status_mutex_);
        status = adapter_powered_status_;
    }

    return status;
}

bool DbusBTDevice::IsEnabled() {
    bool status = false;
    if (dbus_device_impl_ptr_) {
        status = dbus_device_impl_ptr_->IsEnabled();
    }
    return status;
}

bool DbusBTDevice::DbusDeviceImpl::Disable() {
    LOG_I(kLogTag, "Disabling Bluetooth adapter...");
    bool result = false;

    GVariant *dbus_result = nullptr;
    GError *error = nullptr;
    constexpr const char *kSetAdapterPropertyMethod = "Set";

    dbus_result = g_dbus_connection_call_sync(
        connection_, BLUEZ_SERVICE_NAME, BLUEZ_ADAPTER_PATH, PROPERTIES_IFACE, kSetAdapterPropertyMethod,
        g_variant_new(kGVariantTypeSetAdapterProperty, BLUEZ_ADAPTER_INTERFACE, kPoweredProperty,
                      g_variant_new(kGVariantTypeBoolean, FALSE)),
        nullptr, G_DBUS_CALL_FLAGS_NONE, kMaxDefaultConnectionCallTimeout, nullptr, &error);

    if ((error != nullptr) && (dbus_result == nullptr)) {
        LOG_E(kLogTag, "Not able to disable the adapter: %s (code: %d)", error->message, error->code);
    } else {
        LOG_I(kLogTag, "Adapter Disable called Successfully");
    }

    if (error) {
        g_error_free(error);
    }
    if (dbus_result) {
        g_variant_unref(dbus_result);
    }

    {
        std::unique_lock<std::mutex> lock(adapter_status_mutex_);
        constexpr auto kAdapterEnableTimeout = std::chrono::seconds(5);
        if (adapter_status_cv_.wait_for(lock, kAdapterEnableTimeout, [this] { return !adapter_powered_status_; })) {
            LOG_I(kLogTag, "Disable completed with status: success");
            result = true;
        } else {
            LOG_E(kLogTag, "Disable timed out.");
        }
    }

    return result;
}

bool DbusBTDevice::DbusDeviceImpl::RegisterLeEventCallback(nd::interface::LeEventCb cb) {
    le_event_cb_ = std::move(cb);
    return true;
}

void DbusBTDevice::DbusDeviceImpl::HandleLeAdvertisingReport(const evt_le_meta_event *meta) {
    const uint8_t *meta_data = meta->data;
    uint8_t reports_count = meta_data[0];
    const void *offset = meta_data + 1;
    const int adv_report_trailer_size = 2;
    while (reports_count-- > 0) {
        const le_advertising_info *info = (const le_advertising_info *)offset;
        if (info->length == 0) {
            offset = info->data + info->length + adv_report_trailer_size;
            continue;
        }
        std::string mac_addr;
        mac_addr.resize(BT_ADDRESS_STRING_SIZE - 1);
        ba2str(&info->bdaddr, const_cast<char *>(mac_addr.data()));
        const int rssi = static_cast<int8_t>(info->data[info->length]);
        if (le_event_cb_) {
            le_event_cb_(mac_addr, info->data, info->length, rssi);
        }
        offset = info->data + info->length + adv_report_trailer_size;
    }
}

void DbusBTDevice::DbusDeviceImpl::HandleLeExtAdvertisingReport(const evt_le_meta_event *meta) {
    const uint8_t *ext_report_data = meta->data;
    uint8_t num_reports = ext_report_data[0];
    const uint8_t *ptr = ext_report_data + 1;
    while (num_reports-- > 0) {
        char addr[BT_ADDRESS_STRING_SIZE] = {0};
        ba2str((const bdaddr_t *)(ptr + EXT_ADV_ADDR_OFFSET), addr);
        const int8_t rssi = (int8_t)ptr[EXT_ADV_RSSI_OFFSET];
        const uint8_t data_len = ptr[EXT_ADV_DATA_LEN_OFFSET];
        const uint8_t *data = ptr + EXT_ADV_FIXED_FIELDS_LEN;
        if (le_event_cb_) {
            le_event_cb_(addr, data, data_len, rssi);
        }
        ptr += EXT_ADV_FIXED_FIELDS_LEN + data_len;
    }
}

void DbusBTDevice::DbusDeviceImpl::LeScanLoop(std::promise<bool> &&scan_start_promise) {
    LOG_I(kLogTag, "Starting LE Scan Loop thread");
    int device_handle = -1;
    bool status = false;

    do {
        // Open default adapter device
        int device_id = hci_get_route(NULL);
        if (device_id < 0) {
            LOG_E(kLogTag, "ERROR: Invalid device: %d:%s", errno, strerror(errno));
            break;
        }
        // LOG_I(kLogTag,"After Device ID obtained");

        device_handle = hci_open_dev(device_id);
        if (device_handle < 0) {
            LOG_E(kLogTag, "Could not open device: %s", strerror(errno));
            break;
        }
        // LOG_I(kLogTag,"After Device Handle obtained");

        // Create and set the new filter
        hci_filter new_filter{};

        hci_filter_clear(&new_filter);
        hci_filter_set_ptype(HCI_EVENT_PKT, &new_filter);
        hci_filter_set_event(EVT_LE_META_EVENT, &new_filter);

        if (0 > setsockopt(device_handle, SOL_HCI, HCI_FILTER, &new_filter, sizeof(new_filter))) {
            LOG_E(kLogTag, "Could not set socket options: %s", strerror(errno));
            break;
        }

        scan_start_promise.set_value(true);

        bool done = false;
        // LOG_I(kLogTag, "Raw HCI poll/read loop started");

        while (!done && !stop_scan_.load()) {
            // LOG_I(kLogTag,"Raw HCI poll/read loop running");
            ssize_t len = 0;
            uint8_t buf[HCI_MAX_EVENT_SIZE] = {};
            /*
            buf Structure = {
                [0]  : Packet Type (should be HCI_EVENT_PKT, e.g., 0x04)
                [1]  : Event Code (should be EVT_LE_META_EVENT, e.g., 0x3E)
                [2]  : Parameter Total Length (number of bytes after this)
                [3]  : Subevent Code (e.g., EVT_LE_ADVERTISING_REPORT = 0x02)
                [4...] : Subevent Data (depends on subevent type)
            }
            */

            constexpr int kPollTimeoutMs = 100;
            struct pollfd fds[1] = {};
            fds[0].fd = device_handle;
            fds[0].events = POLLIN;

            const int ready = poll(fds, 1, kPollTimeoutMs);
            if (ready == -1) {
                LOG_E(kLogTag, "poll() failed errno: %d, err: %s", errno, strerror(errno));
                break;
            }
            // LOG_I(kLogTag,"Raw HCI poll/read loop before if(ready==0)");
            if (ready == 0) {
                // LOG_I(kLogTag, "poll() timeout with no data");
                continue;
            }
            // LOG_I(kLogTag,"Raw HCI poll/read loop After if(ready==0)");

            if (fds[0].revents & POLLIN) {
                // LOG_I(kLogTag, "LE Scan data available");
                while ((len = read(device_handle, buf, HCI_MAX_EVENT_SIZE)) < 0) {
                    if (stop_scan_.load()) {
                        done = true;
                        break;
                    }
                    if (errno == EINTR || errno == EAGAIN) {
                        continue;
                    } else {
                        LOG_E(kLogTag, "read() error -> %d:%s", errno, strerror(errno));
                        done = true;
                        break;
                    }
                }
                if (done) {
                    break;
                }
            } else {
                continue;
            }

            // Parse LE Advertising reports only (ignore other packets)
            if (len < 4 || buf[0] != HCI_EVENT_PKT || buf[1] != EVT_LE_META_EVENT || buf[2] != (len - 3)) {
                continue;
            }
            const evt_le_meta_event *meta = (evt_le_meta_event *)(buf + 1 + HCI_EVENT_HDR_SIZE);
            if (meta->subevent == EVT_LE_ADVERTISING_REPORT) {
                HandleLeAdvertisingReport(meta);
            } else if (meta->subevent == EVT_LE_EXT_ADVERTISING_REPORT) {
                HandleLeExtAdvertisingReport(meta);
            } else {
                LOG_I(kLogTag, "LE Scan data ignored (subevent: %d)", meta->subevent);
            }
        }

        status = true;
    } while (false);

    if (!status) {
        scan_start_promise.set_value(false);
    }

    if (device_handle >= 0) {
        if (hci_close_dev(device_handle) < 0) {
            LOG_E(kLogTag, "Failed to close device handle: %s", strerror(errno));
        }
    }
}

bool DbusBTDevice::Disable() {
    bool status = false;
    const std::lock_guard<std::mutex> lock(api_mutex_);
    if (dbus_device_impl_ptr_) {
        status = dbus_device_impl_ptr_->Disable();
    }
    return status;
}

bool DbusBTDevice::DbusDeviceImpl::WaitForServicesResolved(const std::string& mac_addr, int timeout_seconds) {
    std::unique_lock<std::mutex> lock(services_resolved_mutex_);
    services_resolved_ = false;
    services_resolved_mac_.clear(); // Clear previous MAC
    return services_resolved_cv_.wait_for(lock, std::chrono::seconds(timeout_seconds), [this, &mac_addr] {
        return services_resolved_ && services_resolved_mac_ == mac_addr;
    });
}

std::string DbusBTDevice::DbusDeviceImpl::FindAdapterPath() {
    LOG_I(kLogTag, "Starting FindAdapterPath()");
    std::string adapter_path;
    // TODO:(sunil) code to be reviewed
    // GError *error = nullptr;
    // GDBusProxy *proxy = nullptr;
    // GVariant *result = nullptr;
    // GVariantIter *iter = nullptr;

    // if (!connection_) {
    //     LOG_E(kLogTag, "GDBusConnection* 'connection_' is null!");
    // } else {
    //     proxy = g_dbus_proxy_new_sync(connection_, G_DBUS_PROXY_FLAGS_NONE, nullptr, BLUEZ_SERVICE_NAME, "/",
    //                                   DBUS_OM_IFACE, nullptr, &error);

    //     if (error) {
    //         LOG_E(kLogTag, "Failed to create proxy: %s (code: %d)", error->message, error->code);
    //         g_error_free(error);
    //     } else {
    //         LOG_I(kLogTag, "Proxy created successfully");

    //         result = g_dbus_proxy_call_sync(proxy, "GetManagedObjects", nullptr, G_DBUS_CALL_FLAGS_NONE, -1, nullptr,
    //                                         &error);

    //         if (error) {
    //             LOG_E(kLogTag, "Failed GetManagedObjects call: %s (code: %d)", error->message, error->code);
    //             g_error_free(error);
    //         } else {
    //             LOG_I(kLogTag, "GetManagedObjects call succeeded");

    //             g_variant_get(result, kGVariantTypeManagedObjects, &iter);

    //             const gchar *path = nullptr;
    //             while (g_variant_iter_loop(iter, kGVariantTypeManagedObjectsIter, &path, nullptr)) {
    //                 LOG_I(kLogTag, "Checking path: %s", path);
    //                 if (g_str_has_prefix(path, "/org/bluez/hci")) {
    //                     LOG_I(kLogTag, "Found adapter path: %s", path);
    //                     adapter_path = path;
    //                     break;
    //                 }
    //             }

    //             if (adapter_path.empty()) {
    //                 LOG_E(kLogTag, "No valid adapter path found!");
    //             }
    //         }
    //     }
    // }

    // if (iter)
    //     g_variant_iter_free(iter);
    // if (result)
    //     g_variant_unref(result);
    // if (proxy)
    //     g_object_unref(proxy);

    // LOG_I(kLogTag, "Finished FindAdapterPath(), returning: %s", adapter_path.c_str());
    return adapter_path;
}

std::vector<uint8_t> DbusBTDevice::DbusDeviceImpl::CreateIbeaconData(const std::string &uuid_str, uint16_t major,
                                                                     uint16_t minor, int8_t tx_power) {
    std::vector<uint8_t> data;
    // TODO:(sunil) code to be reviewed
    // std::string uuid_hex = uuid_str;
    // uuid_hex.erase(std::remove(uuid_hex.begin(), uuid_hex.end(), '-'), uuid_hex.end());

    // // iBeacon payload: 0x02 0x15 + 16 bytes UUID + 2 bytes major + 2 bytes minor + 1 byte tx power
    // constexpr uint8_t kIBeaconPrefix = 0x02;
    // constexpr uint8_t kIBeaconType = 0x15;
    // data.push_back(kIBeaconPrefix);  // iBeacon prefix
    // data.push_back(kIBeaconType);    // iBeacon type

    // // Add UUID bytes (16 bytes)
    // for (size_t i = 0; i < uuid_hex.length(); i += 2) {
    //     std::string byte_str = uuid_hex.substr(i, 2);
    //     data.push_back(static_cast<uint8_t>(std::stoi(byte_str, nullptr, 16)));
    // }

    // // Add major and minor (2 bytes each, big-endian)
    // data.push_back((major >> 8) & 0xFF);
    // data.push_back(major & 0xFF);
    // data.push_back((minor >> 8) & 0xFF);
    // data.push_back(minor & 0xFF);

    // // Add tx power (1 byte)
    // data.push_back(static_cast<uint8_t>(tx_power));

    // // Debug print the data using LOG_I
    // LOG_I(kLogTag, "iBeacon data: %s", Translator::BytesToHexString(data.data(), data.size()).c_str());

    return data;
}

bool DbusBTDevice::DbusDeviceImpl::RegisterAdvertisementObject(const std::string &advertisement_path,
                                                               const std::string &local_name,
                                                               const std::string &service_uuid,
                                                               const std::vector<uint8_t> &manufacturer_data,
                                                               const std::vector<uint8_t> &service_data) {
    bool result = false;
    // TODO:(sunil) code to be reviewed
    // GError *error = nullptr;
    // GDBusNodeInfo *node_info = nullptr;
    // auto *user_data =
    //     static_cast<std::tuple<std::string, std::string, std::vector<uint8_t>, std::vector<uint8_t>> *>(nullptr);

    // do {
    //     node_info = g_dbus_node_info_new_for_xml(advertisement_xml, &error);
    //     if (error) {
    //         LOG_E(kLogTag, "Error parsing XML: %s (code: %d)", error->message, error->code);
    //         g_error_free(error);
    //         break;
    //     }

    //     GDBusInterfaceVTable interface_vtable = {};
    //     interface_vtable.get_property = [](GDBusConnection *, const gchar *, const gchar *, const gchar *,
    //                                        const gchar *property_name, GError **, gpointer user_data) -> GVariant * {
    //         auto *data =
    //             static_cast<std::tuple<std::string, std::string, std::vector<uint8_t>, std::vector<uint8_t>> *>(
    //                 user_data);
    //         if (g_strcmp0(property_name, "LocalName") == 0) {
    //             return g_variant_new_string(std::get<0>(*data).c_str());
    //         } else if (g_strcmp0(property_name, "Type") == 0) {
    //             return g_variant_new_string("peripheral");
    //         } else if (g_strcmp0(property_name, "ServiceUUIDs") == 0) {
    //             GVariantBuilder builder;
    //             g_variant_builder_init(&builder, G_VARIANT_TYPE(kGVariantTypeArrayString));
    //             if (!std::get<1>(*data).empty()) {
    //                 g_variant_builder_add(&builder, kGVariantTypeString, std::get<1>(*data).c_str());
    //             }
    //             return g_variant_builder_end(&builder);
    //         } else if (g_strcmp0(property_name, "ManufacturerData") == 0) {
    //             GVariantBuilder builder;
    //             g_variant_builder_init(&builder, G_VARIANT_TYPE(kGVariantTypeArrayUint16ToVariant));
    //             if (!std::get<2>(*data).empty()) {
    //                 GVariant *payload = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, std::get<2>(*data).data(),
    //                                                               std::get<2>(*data).size(), sizeof(uint8_t));
    //                 g_variant_builder_add(&builder, kGVariantTypeUint16ToVariant, 0x004C, payload);
    //             }
    //             return g_variant_builder_end(&builder);
    //         } else if (g_strcmp0(property_name, "ServiceData") == 0) {
    //             GVariantBuilder builder;
    //             g_variant_builder_init(&builder, G_VARIANT_TYPE(kGVariantTypeArrayStringToVariant));
    //             if (!std::get<3>(*data).empty()) {
    //                 GVariant *payload = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, std::get<3>(*data).data(),
    //                                                               std::get<3>(*data).size(), sizeof(uint8_t));
    //                 g_variant_builder_add(&builder, kGVariantTypeStringToVariant, std::get<1>(*data).c_str(),
    //                 payload);
    //             }
    //             return g_variant_builder_end(&builder);
    //         }
    //         return nullptr;
    //     };

    //     user_data = new std::tuple<std::string, std::string, std::vector<uint8_t>, std::vector<uint8_t>>(
    //         local_name, service_uuid, manufacturer_data, service_data);

    //     g_dbus_connection_register_object(
    //         connection_, advertisement_path.c_str(), node_info->interfaces[0], &interface_vtable, user_data,
    //         [](gpointer user_data) {
    //             delete static_cast<std::tuple<std::string, std::string, std::vector<uint8_t>, std::vector<uint8_t>>
    //             *>(
    //                 user_data);
    //         },
    //         &error);

    //     if (error) {
    //         LOG_E(kLogTag, "Error registering object: %s (code: %d)", error->message, error->code);
    //         g_error_free(error);
    //         delete user_data;
    //         user_data = nullptr;
    //         break;
    //     }

    //     result = true;
    // } while (false);

    // if (node_info) {
    //     g_dbus_node_info_unref(node_info);
    // }
    // // user_data is deleted by the unregister callback, not here

    return result;
}

// Static callback function for GIO async
void DbusBTDevice::DbusDeviceImpl::RegisterAdvertisementCallback(GObject *source_object, GAsyncResult *res,
                                                                 gpointer user_data) {
    // TODO:(sunil) code to be reviewed
    // auto *ctx = static_cast<RegisterAdvContext *>(user_data);
    // GError *error = nullptr;
    // g_dbus_proxy_call_finish(G_DBUS_PROXY(source_object), res, &error);
    // {
    //     std::lock_guard<std::mutex> lock(ctx->mtx);
    //     ctx->done = true;
    //     ctx->success = (error == nullptr);
    // }
    // if (error) {
    //     LOG_E(kLogTag, "Failed to register advertisement asynchronously: %s (code: %d)",
    //           error->message ? error->message : "Unknown error", error->code);
    //     g_error_free(error);
    // } else {
    //     LOG_I(kLogTag, "Advertisement registered successfully asynchronously");
    // }
    // ctx->cv.notify_all();
}

bool DbusBTDevice::DbusDeviceImpl::RegisterAdvertisementAsync(const std::string &advertisement_path,
                                                              const std::string &local_name,
                                                              const std::string &service_uuid,
                                                              const std::vector<uint8_t> &manufacturer_data,
                                                              const std::vector<uint8_t> &service_data,
                                                              uint16_t min_interval, uint16_t max_interval) {
    bool result = false;
    // TODO:(sunil) code to be reviewed
    // RegisterAdvContext ctx;
    // GVariantBuilder options;
    // GVariant *options_variant = nullptr;

    // bool reg_result =
    //     RegisterAdvertisementObject(advertisement_path, local_name, service_uuid, manufacturer_data, service_data);
    // if (!reg_result) {
    //     LOG_E(kLogTag, "Failed to register advertisement object");
    // } else {
    //     g_variant_builder_init(&options, G_VARIANT_TYPE(kGVariantTypeArrayStringToVariant));
    //     g_variant_builder_add(&options, kGVariantTypeStringToVariant, "Type", g_variant_new_string("peripheral"));

    //     if (min_interval > 0) {
    //         g_variant_builder_add(&options, kGVariantTypeStringToVariant, "MinInterval",
    //                               g_variant_new_uint16(min_interval));
    //     }
    //     if (max_interval > 0) {
    //         g_variant_builder_add(&options, kGVariantTypeStringToVariant, "MaxInterval",
    //                               g_variant_new_uint16(max_interval));
    //     }

    //     options_variant = g_variant_builder_end(&options);

    //     g_dbus_proxy_call(
    //         ad_manager_, "RegisterAdvertisement",
    //         g_variant_new(kGVariantTypeObjectPathAndArrayStringToVariant, advertisement_path.c_str(),
    //         options_variant), G_DBUS_CALL_FLAGS_NONE, -1, nullptr,
    //         DbusBTDevice::DbusDeviceImpl::RegisterAdvertisementCallback, &ctx);

    //     constexpr auto kTimeout = std::chrono::seconds(5);
    //     std::unique_lock<std::mutex> lock(ctx.mtx);
    //     if (!ctx.cv.wait_for(lock, kTimeout, [&ctx] { return ctx.done; })) {
    //         LOG_E(kLogTag, "Advertisement registration timed out after 5 seconds.");
    //     } else {
    //         result = ctx.success;
    //     }
    // }
    return result;
}

bool DbusBTDevice::DbusDeviceImpl::StartBeaconAdvertising(int advertising_interval,
                                                          const std::vector<uint8_t> &advertising_uuid,
                                                          int major_number, int minor_number, int8_t rssi_value) {
    bool result = false;
    // TODO:(sunil) code to be reviewed
    // GError *error = nullptr;

    // do {
    //     if (adapter_path_.empty()) {
    //         // Try again
    //         adapter_path_ = FindAdapterPath();
    //         if (adapter_path_.empty()) {
    //             LOG_E(kLogTag, "No suitable adapter found after retry");
    //             break;
    //         }
    //     }

    //     beacon_advertisement_path_ = adapter_path_ + "/beacon_advertisement";

    //     ad_manager_ = g_dbus_proxy_new_sync(connection_, G_DBUS_PROXY_FLAGS_NONE, nullptr, BLUEZ_SERVICE_NAME,
    //                                         adapter_path_.c_str(), ADVERTISING_MANAGER_IFACE, nullptr, &error);
    //     if (error) {
    //         LOG_E(kLogTag, "Error creating advertising manager proxy: %s (code: %d)",
    //               error->message ? error->message : "Unknown error", error->code);
    //         g_error_free(error);
    //         break;
    //     }

    //     // Use CreateIbeaconData to generate the iBeacon payload
    //     if (advertising_uuid.size() != 16) {
    //         LOG_E(kLogTag, "UUID must be exactly 16 bytes");
    //         break;
    //     }
    //     // Convert advertising_uuid to a hex string for CreateIbeaconData
    //     std::stringstream uuid_ss;
    //     uuid_ss << std::hex << std::setfill('0');
    //     for (size_t i = 0; i < advertising_uuid.size(); ++i) {
    //         uuid_ss << std::setw(2) << static_cast<int>(advertising_uuid[i]);
    //     }
    //     std::string uuid_hex = uuid_ss.str();

    //     std::vector<uint8_t> ibeacon_data = CreateIbeaconData(uuid_hex, static_cast<uint16_t>(major_number),
    //                                                           static_cast<uint16_t>(minor_number), rssi_value);

    //     // Compute min/max interval
    //     uint16_t min_interval = advertising_interval;
    //     uint16_t max_interval = advertising_interval;

    //     // Register advertisement with intervals
    //     if (!RegisterAdvertisementAsync(beacon_advertisement_path_, "", "", ibeacon_data, {}, min_interval,
    //                                     max_interval)) {
    //         LOG_E(kLogTag, "Failed to register beacon advertisement asynchronously.");
    //         break;
    //     }

    //     LOG_I(kLogTag, "Beacon advertising started with min_interval=%u ms, max_interval=%u ms", min_interval,
    //           max_interval);
    //     is_beacon_advertising_on_ = true;  // Update state
    //     result = true;
    // } while (false);

    return result;
}

bool DbusBTDevice::StartBeaconAdvertising(int advertising_interval, const std::vector<uint8_t> &advertising_uuid,
                                          int major_number, int minor_number, int8_t rssi_value) {
    bool status = false;
    const std::lock_guard<std::mutex> lock(api_mutex_);
    if (dbus_device_impl_ptr_) {
        status = dbus_device_impl_ptr_->StartBeaconAdvertising(advertising_interval, advertising_uuid, major_number,
                                                               minor_number, rssi_value);
    }
    return status;
}

bool DbusBTDevice::DbusDeviceImpl::StartServiceAdvertising(const std::string &advertise_name,
                                                           const std::vector<uint8_t> &advertise_uuid,
                                                           const std::string &advertise_data, uint32_t duration) {
    bool result = false;
    // TODO:(sunil) code to be reviewed
    // GError *error = nullptr;

    // do {
    //     if (adapter_path_.empty()) {
    //         // Try again
    //         adapter_path_ = FindAdapterPath();
    //         if (adapter_path_.empty()) {
    //             LOG_E(kLogTag, "No suitable adapter found after retry");
    //             break;
    //         }
    //     }

    //     service_advertisement_path_ = adapter_path_ + "/service_advertisement";

    //     ad_manager_ = g_dbus_proxy_new_sync(connection_, G_DBUS_PROXY_FLAGS_NONE, nullptr, BLUEZ_SERVICE_NAME,
    //                                         adapter_path_.c_str(), ADVERTISING_MANAGER_IFACE, nullptr, &error);
    //     if (error) {
    //         LOG_E(kLogTag, "Error creating advertising manager proxy: %s (code: %d)",
    //               error->message ? error->message : "Unknown error", error->code);
    //         g_error_free(error);
    //         break;
    //     }

    //     // Convert UUID bytes to string format
    //     std::stringstream uuid_stream;
    //     if (advertise_uuid.size() == 16) {
    //         uuid_stream << std::hex << std::setfill('0');
    //         for (size_t byte_index = 0; byte_index < advertise_uuid.size(); ++byte_index) {
    //             uuid_stream << std::setw(2) << static_cast<int>(advertise_uuid[byte_index]);
    //             if (byte_index == 3 || byte_index == 5 || byte_index == 7 || byte_index == 9) {
    //                 uuid_stream << "-";
    //             }
    //         }
    //     } else {
    //         LOG_E(kLogTag, "UUID must be exactly 16 bytes");
    //         break;
    //     }
    //     std::string uuid_str = uuid_stream.str();

    //     std::vector<uint8_t> service_data;
    //     if (!advertise_data.empty()) {
    //         service_data.assign(reinterpret_cast<const uint8_t *>(advertise_data.data()),
    //                             reinterpret_cast<const uint8_t *>(advertise_data.data() + advertise_data.length()));
    //     }

    //     // Compute min/max interval
    //     uint16_t min_interval = duration;
    //     uint16_t max_interval = duration;

    //     // Register advertisement with intervals
    //     if (!RegisterAdvertisementAsync(service_advertisement_path_, advertise_name, uuid_str, {}, service_data,
    //                                     min_interval, max_interval)) {
    //         LOG_E(kLogTag, "Failed to register service advertisement asynchronously.");
    //         break;
    //     }

    //     std::ostringstream oss;
    //     oss << "Service advertising started with min_interval=" << min_interval << " ms, max_interval=" <<
    //     max_interval
    //         << " ms";
    //     LOG_I(kLogTag, "%s", oss.str().c_str());
    //     LOG_I(kLogTag, "Service Name: %s", advertise_name.c_str());
    //     LOG_I(kLogTag, "Service UUID: %s", uuid_str.c_str());

    //     std::ostringstream data_oss;
    //     data_oss << "Service Data (" << service_data.size() << " bytes): ";
    //     for (const auto &byte : service_data) {
    //         data_oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte) << " ";
    //     }
    //     LOG_I(kLogTag, "%s", data_oss.str().c_str());

    //     result = true;
    //     is_service_advertising_on_ = true;  // Update state
    // } while (false);

    return result;
}

bool DbusBTDevice::StartServiceAdvertising(const std::string &advertise_name,
                                           const std::vector<uint8_t> &advertise_uuid,
                                           const std::string &advertise_data, uint32_t duration) {
    bool status = false;
    const std::lock_guard<std::mutex> lock(api_mutex_);
    if (dbus_device_impl_ptr_) {
        status =
            dbus_device_impl_ptr_->StartServiceAdvertising(advertise_name, advertise_uuid, advertise_data, duration);
    }
    return status;
}

bool DbusBTDevice::DbusDeviceImpl::IsLeScanOn() {
    bool status = false;
    {
        std::lock_guard<std::mutex> lock(scan_status_mutex_);
        status = scan_status_;
    }
    return (true == status);  // Return the current scan status
}

bool DbusBTDevice::IsLeScanOn() {
    bool status = false;
    const std::lock_guard<std::mutex> lock(api_mutex_);
    if (dbus_device_impl_ptr_) {
        status = dbus_device_impl_ptr_->IsLeScanOn();
    }
    return status;
}

bool DbusBTDevice::DbusDeviceImpl::StopBeaconAdvertising() {
    bool result = false;
    // TODO:(sunil) code to be reviewed
    // GError *error = nullptr;

    // do {
    //     if (!is_beacon_advertising_on_) {
    //         LOG_I(kLogTag, "Beacon advertising is not currently active.");
    //         result = true;  // No error, just not active
    //         break;
    //     }
    //     if (!ad_manager_) {
    //         LOG_I(kLogTag, "Beacon Advertisement manager is not initialized.");
    //         break;
    //     }

    //     if (beacon_advertisement_path_.empty()) {
    //         LOG_E(kLogTag, "Beacon advertisement path is empty or invalid.");
    //         break;
    //     }

    //     g_dbus_proxy_call_sync(ad_manager_, "UnregisterAdvertisement",
    //                            g_variant_new(kGVariantTypeObjectPath, beacon_advertisement_path_.c_str()),
    //                            G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);

    //     if (error) {
    //         LOG_E(kLogTag, "Error unregistering beacon advertisement: %s (code: %d)",
    //               error->message ? error->message : "Unknown error", error->code);
    //         g_error_free(error);
    //         break;
    //     }

    //     LOG_I(kLogTag, "Beacon advertisement unregistered successfully.");
    //     is_beacon_advertising_on_ = false;  // Update state
    //     result = true;
    // } while (false);

    return result;
}

bool DbusBTDevice::StopBeaconAdvertising() {
    bool status = false;
    const std::lock_guard<std::mutex> lock(api_mutex_);
    if (dbus_device_impl_ptr_) {
        status = dbus_device_impl_ptr_->StopBeaconAdvertising();
    }
    return status;
}

bool DbusBTDevice::DbusDeviceImpl::StopServiceAdvertising() {
    bool result = false;
    // TODO:(sunil) code to be reviewed
    // GError *error = nullptr;

    // do {
    //     if (!is_service_advertising_on_) {
    //         LOG_I(kLogTag, "Service advertising is not currently active.");
    //         result = true;  // No error, just not active
    //         break;
    //     }
    //     if (!ad_manager_) {
    //         LOG_I(kLogTag, "Service Advertisement manager is not initialized.");
    //         break;
    //     }

    //     if (service_advertisement_path_.empty()) {
    //         LOG_E(kLogTag, "Service advertisement path is empty or invalid.");
    //         break;
    //     }

    //     static constexpr const char *kUnregisterAdvertisementMethod = "UnregisterAdvertisement";

    //     g_dbus_proxy_call_sync(ad_manager_, kUnregisterAdvertisementMethod,
    //                            g_variant_new(kGVariantTypeObjectPath, service_advertisement_path_.c_str()),
    //                            G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);

    //     if (error) {
    //         LOG_E(kLogTag, "Error unregistering service advertisement: %s (code: %d)",
    //               error->message ? error->message : "Unknown error", error->code);
    //         g_error_free(error);
    //         break;
    //     }

    //     LOG_I(kLogTag, "Service advertisement unregistered successfully.");
    //     is_service_advertising_on_ = false;  // Update state
    //     result = true;
    // } while (false);

    return result;
}

bool DbusBTDevice::StopServiceAdvertising() {
    bool status = false;
    const std::lock_guard<std::mutex> lock(api_mutex_);
    if (dbus_device_impl_ptr_) {
        status = dbus_device_impl_ptr_->StopServiceAdvertising();
    }
    return status;
}

void DbusBTDevice::DbusDeviceImpl::OnBoot() {
    static constexpr const char *kPropertiesChangedSignal = "PropertiesChanged";
    static constexpr const char *kInterfacesAddedSignal = "InterfacesAdded";
    static constexpr const char *kInterfacesRemovedSignal = "InterfacesRemoved";

    prop_changed_ = g_dbus_connection_signal_subscribe(
        connection_, BLUEZ_SERVICE_NAME, PROPERTIES_IFACE, kPropertiesChangedSignal, NULL, BLUEZ_ADAPTER_INTERFACE,
        G_DBUS_SIGNAL_FLAGS_NONE, DbusBTDevice::DbusDeviceImpl::OnAdapterChangedSignal, this, NULL);

    iface_added_ = g_dbus_connection_signal_subscribe(connection_, BLUEZ_SERVICE_NAME, DBUS_OM_IFACE,
                                                      kInterfacesAddedSignal, NULL, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
                                                      DbusBTDevice::DbusDeviceImpl::OnDeviceAppearedSignal, this, NULL);

    iface_removed_ = g_dbus_connection_signal_subscribe(
        connection_, BLUEZ_SERVICE_NAME, DBUS_OM_IFACE, kInterfacesRemovedSignal, NULL, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
        DbusBTDevice::DbusDeviceImpl::OnDeviceDisappearedSignal, this, NULL);

    LOG_I(kLogTag, "Signal subscriptions set up");
}

gboolean DbusBTDevice::DbusDeviceImpl::ClearPrevDevices(const std::string &mac_addr) {
    LOG_I(kLogTag, "Clearing device for MAC: %s", mac_addr.c_str());
    std::vector<std::string> objects;
    gboolean result = FALSE;

    {
        std::string object_path = DbusUtil::CreateObjectPathFromMac(mac_addr);
        if (!object_path.empty()) {
            objects.push_back(object_path);

            std::lock_guard<std::mutex> lock(listeners_mutex_);
            auto it = listeners_.find(object_path);

            if (it != listeners_.end()) {
                if (it->second.proxy) {
                    g_object_unref(it->second.proxy);
                }
                listeners_.erase(it);
            }
        }
    }

    if (!objects.empty()) {
        RemoveDeviceSync(objects);
        result = TRUE;
        LOG_I(kLogTag, "Device %s cleared successfully.", mac_addr.c_str());
    } else {
        LOG_I(kLogTag, "Device %s not found in listeners map.", mac_addr.c_str());
    }

    return result;
}

gboolean DbusBTDevice::DbusDeviceImpl::ClearPrevDevices() {
    LOG_I(kLogTag, "Clearing previous devices...");
    GError *error = NULL;
    GVariant *retval = NULL, *objs = NULL;
    std::vector<std::string> objects;
    const gchar *object_path = NULL;
    GVariant *ifaces_n_props = NULL;
    GDBusProxy *bluez_proxy = NULL;
    gboolean result = FALSE;

    do {
        bluez_proxy = g_dbus_proxy_new_sync(connection_, G_DBUS_PROXY_FLAGS_NONE, NULL, BLUEZ_SERVICE_NAME, "/",
                                            DBUS_OM_IFACE, NULL, &error);

        if (error != NULL) {
            LOG_E(kLogTag, "Error creating proxy: %s (code: %d)", error->message, error->code);
            g_error_free(error);
            error = NULL;
            break;
        }

        retval =
            g_dbus_proxy_call_sync(bluez_proxy, "GetManagedObjects", NULL, G_DBUS_CALL_FLAGS_NONE,
                                   kMaxDefaultConnectionCallTimeout, NULL, &error);

        if (error != NULL) {
            LOG_E(kLogTag, "Error calling GetManagedObjects: %s (code: %d)", error->message, error->code);
            g_error_free(error);
            error = NULL;
            break;
        }

        objs = g_variant_get_child_value(retval, 0);

        GVariantIter iter1;
        g_variant_iter_init(&iter1, objs);

        while (g_variant_iter_next(&iter1, kGVariantTypeObjectPathAndInterfaces, &object_path, &ifaces_n_props)) {
            if (g_str_has_prefix(object_path, "/org/bluez/hci0/dev_") &&
                strlen(object_path) == strlen("/org/bluez/hci0/dev_XX_XX_XX_XX_XX_XX")) {
                LOG_I(kLogTag, "Added path: %s", object_path);
                objects.emplace_back(object_path);
            } else {
                LOG_I(kLogTag, "Unmatched Object path: %s", object_path);
            }

            g_variant_unref(ifaces_n_props);
        }

        if (!objects.empty()) {
            RemoveDeviceSync(objects);
        }

        result = TRUE;
    } while (false);

    if (retval) {
        g_variant_unref(retval);
    }

    if (objs) {
        g_variant_unref(objs);
    }

    if (bluez_proxy) {
        g_object_unref(bluez_proxy);
    }

    if (result) {
        LOG_I(kLogTag, "Previous devices cleared successfully.");
    } else {
        LOG_W(kLogTag, "No previous devices found or an error occurred.");
    }

    return result;
}

// Sync Remove Device

void DbusBTDevice::DbusDeviceImpl::RemoveDeviceSync(const std::vector<std::string> &objects) {
    GError *error = nullptr;
    GDBusProxy *adapter_proxy = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE, NULL,
                                                              BLUEZ_SERVICE_NAME, BLUEZ_ADAPTER_PATH,
                                                              BLUEZ_ADAPTER_INTERFACE, NULL, &error);

    if (nullptr != adapter_proxy) {
        constexpr const char *kRemoveDeviceMethod = "RemoveDevice";
        for (const auto &object : objects) {
            LOG_I(kLogTag, "Removing Device Sync: %s", object.c_str());

            // Reset error for each call
            error = nullptr;

            GVariant *result = g_dbus_proxy_call_sync(adapter_proxy, kRemoveDeviceMethod,
                                                      g_variant_new(kGVariantTypeObjectPath, object.c_str()),
                                                      G_DBUS_CALL_FLAGS_NONE, kMaxDefaultConnectionCallTimeout, NULL, &error);

            if (NULL != result) {
                g_variant_unref(result);  // Only unref if result is valid
            }

            if (nullptr != error) {
                LOG_W(kLogTag, "Error calling RemoveDevice for %s: %s (code: %d)", object.c_str(), error->message,
                      error->code);
                g_error_free(error);
                error = nullptr;
            }
        }
        g_object_unref(adapter_proxy);
    }

    if (nullptr != error) {
        LOG_E(kLogTag, "Error creating proxy: %s (code: %d)", error->message, error->code);
        g_error_free(error);
    }

    LOG_I(kLogTag, "Device removal completed.");
}

void DbusBTDevice::DbusDeviceImpl::HandleAdapterDiscoveringProperty(const char *key, GVariant *value,
                                                                    DbusDeviceImpl &self) {
    if (!g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN)) {
        LOG_E(kLogTag, "Invalid argument type for %s: %s != %s", key, g_variant_get_type_string(value),
              kGVariantTypeBoolean);
    } else {
        bool scan_status = g_variant_get_boolean(value);
        LOG_I(kLogTag, "Adapter scan \"%s\"", scan_status ? "on" : "off");
        {
            std::lock_guard<std::mutex> lock(self.scan_status_mutex_);
            self.scan_status_ = scan_status;
        }
        self.scan_status_cv_.notify_all();
    }
}

void DbusBTDevice::DbusDeviceImpl::HandleAdapterPoweredProperty(const char *key, GVariant *value,
                                                                DbusDeviceImpl &self) {
    if (!g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN)) {
        LOG_E(kLogTag, "Invalid argument type for %s: %s != %s", key, g_variant_get_type_string(value),
              kGVariantTypeBoolean);
    } else {
        bool powered = g_variant_get_boolean(value);
        LOG_I(kLogTag, "Adapter is Powered \"%s\"", powered ? "on" : "off");
        {
            std::lock_guard<std::mutex> lock(self.adapter_status_mutex_);
            self.adapter_powered_status_ = powered;
        }
        self.adapter_status_cv_.notify_all();
    }
}

void DbusBTDevice::DbusDeviceImpl::OnAdapterChangedSignal(GDBusConnection *conn, const gchar *sender, const gchar *path,
                                                          const gchar *interface, const gchar *signal, GVariant *params,
                                                          void *user_data) {
    (void) conn;
    (void) sender;
    (void) path;
    (void) interface;

    if (nullptr != user_data) {
        GVariantIter *properties = NULL;
        GVariantIter *unknown = NULL;
        const char *iface = NULL;
        const char *key = NULL;
        GVariant *value = NULL;
        const gchar *signature = g_variant_get_type_string(params);

        if (g_strcmp0(signature, kGVariantTypePropertiesChanged) != 0) {
            LOG_E(kLogTag, "Invalid signature for %s: %s != %s", signal, signature, kGVariantTypePropertiesChanged);
        } else {
            g_variant_get(params, kGVariantTypePropertiesChangedWithRef, &iface, &properties, &unknown);
            DbusBTDevice::DbusDeviceImpl &self = *static_cast<DbusBTDevice::DbusDeviceImpl *>(user_data);
            while (g_variant_iter_next(properties, kGVariantTypeStringToVariantRef, &key, &value)) {
                AdapterPropertyType prop_type = AdapterPropertyType::Unknown;
                auto it = kAdapterPropertyTypeMap.find(key);
                if (it != kAdapterPropertyTypeMap.end()) {
                    prop_type = it->second;
                }

                switch (prop_type) {
                    case AdapterPropertyType::Powered: {
                        HandleAdapterPoweredProperty(key, value, self);
                        break;
                    }
                    case AdapterPropertyType::Discovering: {
                        HandleAdapterDiscoveringProperty(key, value, self);
                        break;
                    }
                    default: {
                        LOG_W(kLogTag, "Unknown adapter property: %s", key);
                        break;
                    }
                }

                if (value) {
                    g_variant_unref(value);  // Unref value after each use
                }
            }
        }

        // Always free the iterators if they were allocated
        if (properties != NULL) {
            g_variant_iter_free(properties);
        }

        if (unknown != NULL) {
            g_variant_iter_free(unknown);
        }
    }
}

void DbusBTDevice::DbusDeviceImpl::OnPropertiesChanged(GDBusProxy *pProxy, GVariant *params, gpointer user_data,
                                                       const gchar *object_path) {
    GVariantIter *changed_properties = nullptr;
    GVariantIter *invalidated_properties = nullptr;
    const gchar *interface_name = nullptr;

    auto *self = static_cast<DbusBTDevice::DbusDeviceImpl*>(user_data);
    if (self==nullptr) {
        LOG_E(kLogTag, "OnPropertiesChanged: user_data is NULL");
        return;
    }

    do {
        // Try to parse the variant
        if (!params) {
            LOG_E(kLogTag, "OnPropertiesChanged: params is NULL");
            break;
        }
        g_variant_get(params, kGVariantTypePropertiesChanged, &interface_name, &changed_properties,
                      &invalidated_properties);
        if (!changed_properties || !invalidated_properties) {
            LOG_E(
                kLogTag,
                "OnPropertiesChanged: Failed to parse properties (changed_properties: %p, invalidated_properties: %p)",
                changed_properties, invalidated_properties);
            break;
        }
        // printf("OnPropertiesChanged interface_name: %s\n", interface_name);
        const gchar *property_name = nullptr;
        GVariant *property_value = nullptr;
        while (g_variant_iter_next(changed_properties, kGVariantTypeStringToVariant, &property_name, &property_value)) {
            do {
                // printf("changed property_name: %s\n ", property_name);
                // const gchar *type_string = g_variant_get_type_string(property_value);
                // printf("type_string: %s\n", type_string);

                if (g_strcmp0(property_name, "RSSI") == 0) {
                    // printf("RSSI: %d\n", g_variant_get_int16(property_value));
                } else if (g_strcmp0(property_name, "ManufacturerData") == 0) {
                    GVariantIter *iter = nullptr;
                    g_variant_get(property_value, kGVariantTypeArrayUint16ToVariant, &iter);

                    if (!iter) {
                        LOG_E(kLogTag, "ManufacturerData: Failed to get iterator (code: %d)", -1);
                        break;
                    }

                    GVariant *array = nullptr;
                    guint16 key;

                    while (g_variant_iter_loop(iter, kGVariantTypeUint16ToVariant, &key, &array)) {
                        size_t data_length = 0;
                        const guint8 *data =
                            (const guint8 *) g_variant_get_fixed_array(array, &data_length, sizeof(guint8));
                        if (!data) {
                            // LOG_E(kLogTag, "ManufacturerData: Failed to get fixed array");
                            break;
                        }
                        GByteArray *byteArray = g_byte_array_sized_new(data_length);
                        if (!byteArray) {
                            LOG_E(kLogTag, "ManufacturerData: Failed to allocate GByteArray (code: %d)", -2);
                            break;
                        }
                        g_byte_array_append(byteArray, data, data_length);

                        // g_print("ManufacturerData: ");
                        for (guint i = 0; i < byteArray->len; i++) {
                            // g_print("%02X ", byteArray->data[i]);
                        }
                        // g_print("\n");

                        if (byteArray->len > 10) {
                            if (byteArray->data[0] == 0x02 && byteArray->data[1] == 0x15 &&
                                byteArray->data[2] == 0x4E && byteArray->data[3] == 0x44 &&
                                byteArray->data[4] == 0x3A && byteArray->data[5] == 0x55 &&
                                byteArray->data[6] == 0x41 && byteArray->data[7] == 0x4C &&
                                byteArray->data[8] == 0x52 && byteArray->data[9] == 0x54) {
                                LOG_I(kLogTag, "ALRT packet detected from: %s", object_path);
                                // Print the manufacturer data
                                std::vector<uint8_t> full_data(byteArray->data, byteArray->data + byteArray->len);
                                const std::string full_data_str =
                                    Translator::BytesToHexString(full_data.data(), full_data.size());
                                LOG_D(kLogTag, "Full Data (Hex): %s", full_data_str.c_str());
                            }
                        }

                        g_byte_array_free(byteArray, TRUE);  // Free the byte array
                    }

                    g_variant_iter_free(iter);
                } else if (g_str_equal(property_name, "ServiceData")) {
                    GVariantIter *iter = nullptr;
                    g_variant_get(property_value, kGVariantTypeArrayStringToVariant, &iter);

                    if (!iter) {
                        LOG_E(kLogTag, "ServiceData: Failed to get iterator (code: %d)", -3);
                        break;
                    }

                    GVariant *array = nullptr;
                    char *key = nullptr;

                    while (g_variant_iter_loop(iter, kGVariantTypeStringToVariant, &key, &array)) {
                        size_t data_length = 0;
                        const guint8 *data =
                            (const guint8 *) g_variant_get_fixed_array(array, &data_length, sizeof(guint8));
                        if (!data) {
                            // LOG_E(kLogTag, "ServiceData: Failed to get fixed array");
                            break;
                        }
                        GByteArray *byteArray = g_byte_array_sized_new(data_length);
                        if (!byteArray) {
                            LOG_E(kLogTag, "ServiceData: Failed to allocate GByteArray (code: %d)", -4);
                            break;
                        }
                        g_byte_array_append(byteArray, data, data_length);

                        char *keyCopy = g_strdup(key);

                        // g_print("ServiceData: ");
                        for (guint byteIndex = 0; byteIndex < byteArray->len; byteIndex++) {
                            // g_print("%02X ", byteArray->data[byteIndex]);
                        }
                        // g_print("key: %s\n", key);

                        g_free(keyCopy);                     // Free the duplicated key
                        g_byte_array_free(byteArray, TRUE);  // Free the byte array
                    }

                    g_variant_iter_free(iter);
                } else if (g_strcmp0(property_name, "Name") == 0) {
                    // printf("Name: %s\n", g_variant_get_string(property_value, NULL));
                } else if (g_strcmp0(property_name, "ServiceUUIDs") == 0) {
                    GVariantIter *iter = nullptr;
                    g_variant_get(property_value, kGVariantTypeArrayString, &iter);

                    if (nullptr != iter) {
                        const char *uuid = nullptr;
                        // CORRECTED: Use g_variant_iter_loop for automatic cleanup
                        while (g_variant_iter_loop(iter, kGVariantTypeString, &uuid)) {
                            // g_print("ServiceUUIDs: %s\n", uuid);
                        }
                        g_variant_iter_free(iter);
                    } else {
                        LOG_E(kLogTag, "ServiceUUIDs: Failed to get iterator (code: %d)", -5);
                    }
                } else if (g_strcmp0(interface_name, "org.bluez.Device1") == 0 &&
                        g_strcmp0(property_name, "ServicesResolved") == 0) {
                    bool resolved = g_variant_get_boolean(property_value);
                    LOG_I(kLogTag, "Device %s ServicesResolved: %s", object_path, resolved ? "true" : "false");
                    {
                        std::lock_guard<std::mutex> lock(self->services_resolved_mutex_);
                        self->services_resolved_ = resolved;
                        self->services_resolved_mac_ = DbusUtil::CreateMacFromObjectPath(object_path);
                    }
                    self->services_resolved_cv_.notify_all();
                }
                // } else if (g_strcmp0(property_name, "UUIDs")) {

                // 	GVariantIter *iter;
                // 	g_variant_get(property_value, kGVariantTypeArrayString, &iter);

                // 	const char *uuid;

                // 	while (g_variant_iter_next(iter, kGVariantTypeString, &uuid)) {
                // 		g_print("UUIDs: %s\n", uuid);
                // 	}

                // 	g_variant_iter_free(iter);
                // }
            } while (false);

            g_variant_unref(property_value);  // Moved inside loop to ensure cleanup
            g_free((gpointer) property_name);
        }
        while (g_variant_iter_next(invalidated_properties, kGVariantTypeString, &property_name)) {
            // printf("%s invalidated property_name: %s\n ", property_name);
            g_free((gpointer) property_name);
        }
    } while (false);

    // Free all allocated resources
    if (changed_properties) {
        g_variant_iter_free(changed_properties);
    }

    if (invalidated_properties) {
        g_variant_iter_free(invalidated_properties);
    }

    if (interface_name) {
        g_free((gpointer) interface_name);
    }
}

void DbusBTDevice::DbusDeviceImpl::OnPropertiesHandler(GDBusProxy *proxy, gchar *sender_name, gchar *signal_name,
                                                       GVariant *params, gpointer user_data) {
    // This function acts as a dispatcher for signals.
    // It currently only handles "PropertiesChanged".

    // Explicitly marked unused parameters
    (void) sender_name;
    (void) user_data;

    do {
        const gchar *object_path = g_dbus_proxy_get_object_path(proxy);
        if (object_path == nullptr) {
            // This is a critical failure, as we cannot identify the source object.
            LOG_E(kLogTag, "OnPropertiesHandler: Failed to get object path from proxy.");
            break;
        }
        // LOG_I(kLogTag, "Signal name: %s", signal_name);

        static constexpr const char *kPropertiesChangedSignal = "PropertiesChanged";
        if (g_strcmp0(signal_name, kPropertiesChangedSignal) == 0) {
            OnPropertiesChanged(proxy, params, user_data, object_path);
        }

    } while (false);
}

void DbusBTDevice::DbusDeviceImpl::BluezPropertyValue(const gchar *key, GVariant *value) {
    do {
        if (!value) {
            LOG_I(kLogTag, "%s: NULL value", key);
            break;
        }

        const gchar *type = g_variant_get_type_string(value);
        if (!type) {
            LOG_I(kLogTag, "%s: Unknown type", key);
            break;
        }

        try {
            if (g_variant_type_equal(g_variant_get_type(value), G_VARIANT_TYPE_STRING)) {
                LOG_I(kLogTag, "%s: %s", key, g_variant_get_string(value, NULL));
            } else if (g_variant_type_equal(g_variant_get_type(value), G_VARIANT_TYPE_BOOLEAN)) {
                LOG_I(kLogTag, "%s: %s", key, g_variant_get_boolean(value) ? "true" : "false");
            } else if (g_variant_type_equal(g_variant_get_type(value), G_VARIANT_TYPE_UINT32)) {
                LOG_I(kLogTag, "%s: %u", key, g_variant_get_uint32(value));
            } else if (g_variant_type_equal(g_variant_get_type(value), G_VARIANT_TYPE_INT16)) {
                LOG_I(kLogTag, "%s: %d", key, g_variant_get_int16(value));
            } else if (g_variant_type_equal(g_variant_get_type(value), G_VARIANT_TYPE(kGVariantTypeArrayString))) {
                GVariantIter str_iter;
                const gchar *str;
                std::ostringstream oss;
                oss << key << ": [";
                g_variant_iter_init(&str_iter, value);
                bool first = true;
                while (g_variant_iter_next(&str_iter, "&s", &str)) {
                    if (!first)
                        oss << ", ";
                    oss << str;
                    first = false;
                }
                oss << "]";
                LOG_I(kLogTag, "%s", oss.str().c_str());
            } else if (g_variant_type_equal(g_variant_get_type(value),
                                            G_VARIANT_TYPE(kGVariantTypeArrayUint16ToVariant))) {
                // Handle manufacturer data
                GVariantIter *mfg_iter = nullptr;
                g_variant_get(value, kGVariantTypeArrayUint16ToVariant, &mfg_iter);

                GVariant *array = nullptr;
                guint16 mfg_key;

                std::ostringstream oss;
                oss << key << ": {";
                while (g_variant_iter_loop(mfg_iter, kGVariantTypeUint16ToVariant, &mfg_key, &array)) {
                    size_t data_length = 0;
                    guint8 *data = (guint8 *) g_variant_get_fixed_array(array, &data_length, sizeof(guint8));

                    oss << "Company ID: 0x" << std::hex << std::setw(4) << std::setfill('0') << mfg_key << ", Data: ";
                    for (size_t i = 0; i < data_length; i++) {
                        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]) << " ";
                    }
                }
                oss << "}";
                LOG_I(kLogTag, "%s", oss.str().c_str());
                if (mfg_iter)
                    g_variant_iter_free(mfg_iter);
            } else {
                LOG_I(kLogTag, "%s: [Unhandled type: %s]", key, type);
            }
        } catch (const std::exception &e) {
            LOG_E(kLogTag, "Exception handling property %s: %s", key, e.what());
        }
    } while (false);
}

void DbusBTDevice::DbusDeviceImpl::HandleGattCharacteristicAppearance(const char *object, GVariant *properties,
                                                                      DbusBTDevice::DbusDeviceImpl &self) {
    // Check if the current D-Bus object path represents a GATT characteristic ("/char").
    const auto char_pos = std::string(object).find("/char");
    if (std::string::npos != char_pos) {
        const auto service_pos = std::string(object).find("/service");
        if (std::string::npos != service_pos) {
            std::string current_mac_address;

            // Get UUID property
            GVariant *uuid_prop = g_variant_lookup_value(properties, "UUID", G_VARIANT_TYPE_STRING);
            std::string uuid_str;
            if (nullptr != uuid_prop) {
                const gchar *uuid = g_variant_get_string(uuid_prop, NULL);
                if (uuid) {
                    uuid_str = uuid;
                }
                g_variant_unref(uuid_prop);  // Free UUID variant
            }

            {
                std::string dev_path = std::string(object).substr(0, service_pos);
                std::lock_guard<std::mutex> lock(self.listeners_mutex_);
                auto dev_it = self.listeners_.find(dev_path);
                if (dev_it != self.listeners_.end()) {
                    // Add the characteristic path to the device's properties map
                    if (!uuid_str.empty()) {
                        dev_it->second.properties[uuid_str] = object;
                    } else {
                        LOG_W(kLogTag, "UUID is empty for characteristic at path: %s", object);
                    }

                    current_mac_address = dev_it->second.address;
                    LOG_I(kLogTag, "Found characteristic: %s with UUID: %s for device: %s", object, uuid_str.c_str(),
                          dev_path.c_str());
                }
            }

            // Notify if this is the characteristic we're looking for
            {
                std::lock_guard<std::mutex> char_lock(self.char_discovery_mutex_);
                if (!self.char_discovery_done_ && current_mac_address == self.target_mac_addr_) {
                    LOG_I(kLogTag, "Discovered characteristic: %s with UUID: %s", object, uuid_str.c_str());
                    self.char_discovery_done_ = true;
                    self.char_discovery_cv_.notify_all();
                }
            }
        }
    }
}

void DbusBTDevice::DbusDeviceImpl::HandleDeviceAppeared(GDBusConnection *connection, const char *object,
                                                        GVariant *properties, DbusBTDevice::DbusDeviceImpl &self) {
    do {
        if (!connection || !object || !properties) {
            LOG_E(kLogTag, "HandleDeviceAppeared: Null argument(s): connection=%p, object=%p, properties=%p",
                  connection, object, properties);
            break;
        }

        const std::string str_object = object;
        if ((std::string::npos != str_object.find("service")) || (std::string::npos != str_object.find("char"))) {
            break;
        }

        // Get MAC address
        static constexpr const char *kDeviceAddressProperty = "Address";
        GVariant *macv = g_variant_lookup_value(properties, kDeviceAddressProperty, G_VARIANT_TYPE_STRING);
        std::string addr;
        if (macv) {
            const gchar *mac = g_variant_get_string(macv, NULL);
            if (mac) {
                addr = mac;
            }
            g_variant_unref(macv);  // Free MAC variant
        }

        if (!addr.empty()) {
            LOG_D(kLogTag, "Found device: %s at path: %s", addr.c_str(), object);
            if (self.scanned_results_arrived_) {
                LOG_I(kLogTag, "Devices are scanned");
                self.scanned_results_arrived_ = false;  // Reset the flag after processing
            }
        } else {
            LOG_W(kLogTag, "Device address is empty at path: %s", object);
        }

        // Add to listeners_ if not already present
        {
            std::lock_guard<std::mutex> lock(self.listeners_mutex_);
            if ((!addr.empty()) && (self.listeners_.find(object) == self.listeners_.end())) {
                GError *error = NULL;
                GDBusProxy *deviceProxy = g_dbus_proxy_new_sync(connection, G_DBUS_PROXY_FLAGS_NONE, NULL,
                                                                BLUEZ_SERVICE_NAME, object, PROPERTIES_IFACE, NULL,
                                                                &error);
                if (deviceProxy) {
                    static constexpr const char *kGSignalName = "g-signal";
                    auto signal_id = g_signal_connect(
                        deviceProxy, kGSignalName, G_CALLBACK(DbusBTDevice::DbusDeviceImpl::OnPropertiesHandler), &self);
                    self.listeners_.emplace(object, ObjectParams{.proxy = deviceProxy,
                                                                 .address = addr,
                                                                 .signal_handler_id = signal_id,
                                                                 .properties = {}});
                } else {
                    LOG_E(kLogTag, "Error creating device proxy: %s (code: %d)", error->message, error->code);
                    g_error_free(error);  // Free GError
                }
            }
        }

        // Notify if this is the device we're looking for
        {
            std::lock_guard<std::mutex> lock(self.device_found_mutex_);
            if (addr == self.target_mac_addr_) {
                LOG_I(kLogTag, "Device found: %s at path: %s", addr.c_str(), object);
                self.device_found_ = true;
                self.device_found_cv_.notify_all();
            }
        }
    } while (false);
}

void DbusBTDevice::DbusDeviceImpl::OnDeviceAppearedSignal(GDBusConnection *connection, const gchar *sender_name,
                                                          const gchar *object_path, const gchar *interface,
                                                          const gchar *signal_name, GVariant *parameters,
                                                          gpointer user_data) {
    if ((nullptr != user_data) && (nullptr != connection) && (nullptr != parameters)) {
        DbusBTDevice::DbusDeviceImpl &self = *static_cast<DbusBTDevice::DbusDeviceImpl *>(user_data);

        GVariantIter *interfaces = nullptr;
        const char *object = nullptr;
        const gchar *interface_name = nullptr;
        GVariant *properties = nullptr;

        g_variant_get(parameters, kGVariantTypeObjectAndInterfaces, &object, &interfaces);
        while (
            g_variant_iter_next(interfaces, kGVariantTypeStringAndArrayStringToVariant, &interface_name, &properties)) {
            if (nullptr == properties) {
                LOG_W(kLogTag, "OnDeviceAppearedSignal: properties is NULL for interface: %s", interface_name);
                continue;
            }

#ifdef DEBUG_PRINT_PROPERTIES
            // --- Print all properties for this interface ---
            GVariantIter *prop_iter = nullptr;
            g_variant_get(properties, kGVariantTypeArrayStringToVariant, &prop_iter);
            const gchar *key = nullptr;
            GVariant *value = nullptr;
            while (g_variant_iter_next(prop_iter, kGVariantTypeStringToVariantRef, &key, &value)) {
                BluezPropertyValue(key, value);
                if (value)
                    g_variant_unref(value);  // Unref each value immediately after use
            }
            if (prop_iter) {
                g_variant_iter_free(prop_iter);
            }
#endif

            InterfaceType iface_type = InterfaceType::Unknown;
            const auto iface_it = kInterfaceTypeMap.find(interface_name);
            if (iface_it != kInterfaceTypeMap.end()) {
                iface_type = iface_it->second;
            }

            switch (iface_type) {
                case InterfaceType::GattCharacteristic: {
                    HandleGattCharacteristicAppearance(object, properties, self);
                    break;
                }
                case InterfaceType::Device: {
                    HandleDeviceAppeared(connection, object, properties, self);
                    break;
                }
                default: {
                    LOG_D(kLogTag, "Unknown interface appeared: %s", interface_name);
                    // For unknown interfaces, you can add logging or ignore
                    break;
                }
            }

            if (properties) {
                g_variant_unref(properties);  // Free properties variant
                properties = nullptr;
            }
        }
        if (interfaces) {
            g_variant_iter_free(interfaces);  // Free main interfaces iterator
        }
    } else {
        LOG_W(kLogTag, "OnDeviceAppearedSignal: required data are NULL");
    }
}

void DbusBTDevice::DbusDeviceImpl::OnConnectCallback(GDBusConnection *con, GAsyncResult *res, gpointer user_data) {
    if (user_data != nullptr) {
        DbusBTDevice::DbusDeviceImpl &self = *static_cast<DbusBTDevice::DbusDeviceImpl *>(user_data);

        GError *error = nullptr;
        GVariant *result = g_dbus_connection_call_finish(con, res, &error);

        {
            std::lock_guard<std::mutex> lock(self.connect_mutex_);
            self.connect_done_ = true;
            self.connect_success_ = (result != nullptr && error == nullptr);
        }

        self.connect_cv_.notify_all();

        if ((result != nullptr) && (error == nullptr)) {
            LOG_I(kLogTag, "[OnConnectCallback] Connected successfully to %s.", self.name_to_write_.c_str());
        }

        // Always print error details if error exists
        if (error) {
            LOG_E(kLogTag, "[OnConnectCallback] Error: %s (code: %d)", error->message, error->code);
            g_error_free(error);
        }
        if (result) {
            g_variant_unref(result);
        }
    } else {
        LOG_W(kLogTag, "OnConnectCallback: required data are NULL");
    }
}

/**
 * @brief Callback handler for the "InterfacesRemoved" D-Bus signal indicating a Bluetooth device or characteristic has
 * disappeared.
 *
 * When and how is this triggered?
 * -------------------------------------------------
 * - This function is registered as a callback for the "InterfacesRemoved" signal from BlueZ via D-Bus.
 * - It is triggered automatically by the D-Bus system whenever BlueZ removes a device, service, or characteristic from
 * its object model.
 * - Typical triggers include: device disconnects, device goes out of range, or is explicitly removed/unpaired.
 *
 * What does this function do?
 * -------------------------------------------------
 * - Parses the signal parameters to extract the object path and the list of interfaces that were removed.
 * - For each removed interface, checks if it is a device (by matching the interface name).
 * - If a device is found:
 *   - Extracts the MAC address from the object path.
 *   - Removes the device from the internal listeners_ map, cleaning up any associated GDBusProxy.
 *   - Logs the removal for debugging and tracking.
 * - Frees all allocated resources (interface names, iterators).
 *
 * Why is this important?
 * -------------------------------------------------
 * - Keeps the application's internal state synchronized with BlueZ, ensuring that devices which are no longer present
 * are not tracked or used.
 * - Prevents resource leaks by cleaning up proxies and internal maps.
 * - Enables robust handling of device disconnects and removals in real time.
 */

void DbusBTDevice::DbusDeviceImpl::OnDeviceDisappearedSignal(GDBusConnection *connection, const gchar *sender_name,
                                                             const gchar *object_path, const gchar *interface,
                                                             const gchar *signal_name, GVariant *parameters,
                                                             gpointer user_data) {
    // Explicitly mark unused parameters
    (void) connection;
    (void) sender_name;
    (void) object_path;
    (void) interface;
    (void) signal_name;

    if ((nullptr != user_data) && (nullptr != parameters)) {
        GVariantIter *interfaces = nullptr;
        const char *object = nullptr;
        const gchar *interface_name = nullptr;

        g_variant_get(parameters, kGVariantTypeObjectPathAndArrayString, &object, &interfaces);

        while (g_variant_iter_next(interfaces, kGVariantTypeString, &interface_name)) {
            char *lower_iface = g_ascii_strdown(interface_name, -1);
            if (lower_iface && g_strstr_len(lower_iface, -1, kDeviceString)) {  // TODO:(sunil) : optimise this
                // Remove device from listeners_ map
                DbusBTDevice::DbusDeviceImpl &self = *static_cast<DbusBTDevice::DbusDeviceImpl *>(user_data);
                {
                    std::lock_guard<std::mutex> lock(self.listeners_mutex_);
                    auto it = self.listeners_.find(object);
                    if (it != self.listeners_.end()) {
                        // Clean up proxy if needed
                        if (it->second.proxy) {
                            g_object_unref(it->second.proxy);
                        }
                        self.listeners_.erase(it);
                        LOG_D(kLogTag, "[OnDeviceDisappearedSignal] Removed device from listeners_: %s", object);
                    }
                }

                LOG_D(kLogTag, "Device %s removed", object);
            }

            if (lower_iface) {
                g_free(lower_iface);
                lower_iface = nullptr;
            }
            g_free((gpointer) interface_name);
        }

        if (interfaces) {
            g_variant_iter_free(interfaces);
        }
    } else {
        LOG_W(kLogTag, "OnDeviceDisappearedSignal: required data are NULL");
    }
}

bool DbusBTDevice::DbusDeviceImpl::WriteCharacteristic(const gchar *char_path, const guint8 *data, size_t data_len) {
    GVariant *params = nullptr;
    bool write_result = false;

    LOG_I(kLogTag, "Writing characteristic at path: %s, size: %zu", char_path, data_len);

    const std::string full_data_str = Translator::BytesToHexString(data, data_len);
    LOG_I(kLogTag, "Write data (Hex): %s", full_data_str.c_str());

    // Prepare for callback wait
    {
        std::unique_lock<std::mutex> lock(write_mutex_);
        write_done_ = false;
        write_success_ = false;
    }

    // Build the ay (byte array) variant
    GVariantBuilder data_builder;
    g_variant_builder_init(&data_builder, G_VARIANT_TYPE(kGVariantTypeArrayByte));
    for (size_t counter = 0; counter < data_len; ++counter) {
        g_variant_builder_add(&data_builder, kGVariantTypeByte, data[counter]);
    }
    GVariant *data_variant = g_variant_builder_end(&data_builder);  // floating ref

    // Build the a{sv} options variant
    GVariantBuilder options_builder;
    g_variant_builder_init(&options_builder, G_VARIANT_TYPE(kGVariantTypeArrayStringToVariant));
    g_variant_builder_add(&options_builder, kGVariantTypeStringToVariant, kGattOptionTypeKey,
                          g_variant_new_string(kGattOptionTypeValue));
    g_variant_builder_add(&options_builder, kGVariantTypeStringToVariant, kGattOptionOffsetKey,
                          g_variant_new_uint16(0));
    GVariant *options_variant = g_variant_builder_end(&options_builder);  // floating ref

    // Compose the parameters; params takes ownership of data_variant and options_variant
    params = g_variant_new(kGVariantTypeTupleArrayByteArrayStringToVariant, data_variant, options_variant);

    LOG_I(kLogTag, "Initiating D-Bus call for writing characteristic...");

    // Make the async D-Bus call; call takes a reference to params, but does not consume it
    static constexpr const char *kGattCharacteristicWriteMethod = "WriteValue";
    g_dbus_connection_call(connection_, BLUEZ_SERVICE_NAME, char_path, kBluezGattCharacteristicInterface,
                           kGattCharacteristicWriteMethod, params, NULL, G_DBUS_CALL_FLAGS_NONE,
                           kMaxDefaultConnectionCallTimeout,  // 5000 ms timeout for D-Bus calls
                           NULL, (GAsyncReadyCallback) DbusBTDevice::DbusDeviceImpl::WriteValueCallback, this);

    // Wait for WriteValueCallback
    {
        std::unique_lock<std::mutex> lock(write_mutex_);
        constexpr auto kWriteTimeout = std::chrono::seconds(kMaxDefaultCondWaitTimeout);
        if (write_cv_.wait_for(lock, kWriteTimeout, [this] { return write_done_; })) {
            write_result = write_success_;
            if (write_success_) {
                LOG_I(kLogTag, "WriteCharacteristic completed with status: success");
            } else {
                LOG_E(kLogTag, "WriteCharacteristic failed.");
            }
        } else {
            LOG_E(kLogTag, "WriteCharacteristic timed out.");
        }
    }
    return write_result;
}

void DbusBTDevice::DbusDeviceImpl::WriteValueCallback(GDBusConnection *connection, GAsyncResult *res,
                                                      gpointer user_data) {
    if (user_data != nullptr) {
        DbusBTDevice::DbusDeviceImpl &self = *static_cast<DbusBTDevice::DbusDeviceImpl *>(user_data);
        GError *error = NULL;
        GVariant *result = g_dbus_connection_call_finish(connection, res, &error);

        {
            std::lock_guard<std::mutex> lock(self.write_mutex_);
            self.write_done_ = true;
            self.write_success_ = (result != nullptr && error == nullptr);
        }
        self.write_cv_.notify_all();

        if (error) {
            LOG_E(kLogTag, "Error writing value: %s (code: %d)", error->message, error->code);
            g_error_free(error);
        } else {
            LOG_I(kLogTag, "WriteValue call successful");
        }
        if (result) {
            g_variant_unref(result);
        }
    }
}

bool DbusBTDevice::DbusDeviceImpl::DisconnectDevice(const std::string &object_path) {
    LOG_I(kLogTag, "Disconnecting device with object_path: %s", object_path.c_str());
    bool result = false;

    if (object_path.empty()) {
        LOG_W(kLogTag, "Device with MAC %s not found for disconnect.", object_path.c_str());
    } else {
        {
            std::unique_lock<std::mutex> lock(disconnect_mutex_);
            disconnect_done_ = false;
            disconnect_success_ = false;
            disconnect_object_path_ = object_path;
        }

        constexpr int kGDBusCallTimeoutMs = 10000;
        static constexpr const char *kDeviceDisconnectMethod = "Disconnect";

        g_dbus_connection_call(connection_, BLUEZ_SERVICE_NAME, object_path.c_str(), kBluezDeviceInterface,
                               kDeviceDisconnectMethod, nullptr, nullptr, G_DBUS_CALL_FLAGS_NONE,
                               kGDBusCallTimeoutMs,  // 10000 ms timeout for D-Bus calls
                               nullptr, (GAsyncReadyCallback) DbusBTDevice::DbusDeviceImpl::BluezDisconnectCallback,
                               this);

        // Wait for callback
        {
            std::unique_lock<std::mutex> lock(disconnect_mutex_);
            constexpr auto kDisconnectTimeout = std::chrono::seconds(10);
            if (disconnect_cv_.wait_for(lock, kDisconnectTimeout, [this] { return disconnect_done_; })) {
                if (!disconnect_success_) {
                    LOG_W(kLogTag, "Disconnect failed.");
                } else {
                    LOG_I(kLogTag, "Device disconnected: %s", object_path.c_str());
                }
                result = disconnect_success_;
            } else {
                LOG_W(kLogTag, "Disconnect timed out.");
            }
        }
    }

    LOG_I(kLogTag, "DisconnectDevice completed with result: %s", result ? "success" : "failure");

    return result;
}

void DbusBTDevice::DbusDeviceImpl::BluezDisconnectCallback(GDBusConnection *dbus_connection, GAsyncResult *res,
                                                           gpointer user_data) {
    if (user_data != nullptr) {
        DbusBTDevice::DbusDeviceImpl &self = *static_cast<DbusBTDevice::DbusDeviceImpl *>(user_data);
        GError *error = NULL;
        GVariant *result = g_dbus_connection_call_finish(dbus_connection, res, &error);

        {
            std::lock_guard<std::mutex> lock(self.disconnect_mutex_);
            self.disconnect_done_ = true;
            self.disconnect_success_ = (result != nullptr && error == nullptr);
        }
        self.disconnect_cv_.notify_all();

        if (error) {
            if (!strstr(error->message, "org.bluez.Error.NotConnected"))
                LOG_E(kLogTag, "bluez failed to disconnect: %s (code: %d)", error->message, error->code);
            g_error_free(error);
        } else {
            LOG_I(kLogTag, "connection terminated");
        }

        if (result != nullptr) {
            g_variant_unref(result);
        }
    }
}

std::pair<bool, std::vector<uint8_t>> DbusBTDevice::DbusDeviceImpl::ReadCharacteristic(const gchar *char_path,
                                                                                       const gchar *mac_address) {
    LOG_I(kLogTag, "ReadCharacteristic called for path: %s", char_path);
    bool read_result = false;
    std::vector<uint8_t> value_data;
    // Reset state before starting new read
    {
        std::unique_lock<std::mutex> lock(read_mutex_);
        read_done_ = false;
        read_success_ = false;
        last_read_data_.clear();
    }

    LOG_I(kLogTag, "Reading characteristic at path: %s on device: %s", char_path, mac_address);

    GVariantBuilder options_builder;
    g_variant_builder_init(&options_builder, G_VARIANT_TYPE(kGVariantTypeArrayStringToVariant));
    g_variant_builder_add(&options_builder, kGVariantTypeStringToVariant, kGattOptionOffsetKey,
                          g_variant_new_uint16(0));
    GVariant *options_variant = g_variant_builder_end(&options_builder);
    GVariant *params = g_variant_new(kGVariantTypeTupleArrayStringToVariant, options_variant);

    LOG_I(kLogTag, "Initiating D-Bus call for reading characteristic...");

    static constexpr const char *kGattCharacteristicReadMethod = "ReadValue";
    g_dbus_connection_call(connection_, BLUEZ_SERVICE_NAME, char_path, kBluezGattCharacteristicInterface,
                           kGattCharacteristicReadMethod, params, G_VARIANT_TYPE("(ay)"), G_DBUS_CALL_FLAGS_NONE,
                           kMaxDefaultConnectionCallTimeout, NULL,
                           (GAsyncReadyCallback) DbusBTDevice::DbusDeviceImpl::ReadValueCallback, this);

    LOG_I(kLogTag, "Waiting for ReadValueCallback...");

    // Wait for ReadValueCallback
    {
        std::unique_lock<std::mutex> lock(read_mutex_);
        constexpr auto kReadTimeout = std::chrono::seconds(kMaxDefaultCondWaitTimeout);
        if (read_cv_.wait_for(lock, kReadTimeout, [this] { return read_done_; })) {
            read_result = read_success_;
            if (read_success_) {
                value_data = std::move(last_read_data_);
                LOG_I(kLogTag, "ReadCharacteristic completed with status: success");
            } else {
                LOG_W(kLogTag, "ReadCharacteristic failed.");
            }
        } else {
            LOG_W(kLogTag, "ReadCharacteristic timed out.");
        }
    }

    return std::make_pair(read_result, std::move(value_data));
}

void DbusBTDevice::DbusDeviceImpl::ReadValueCallback(GDBusConnection *connection, GAsyncResult *res,
                                                     gpointer user_data) {
    if (user_data != nullptr) {
        DbusBTDevice::DbusDeviceImpl &self = *static_cast<DbusBTDevice::DbusDeviceImpl *>(user_data);
        LOG_I(kLogTag, "ReadValueCallback called.");
        GError *error = NULL;
        GVariant *result = g_dbus_connection_call_finish(connection, res, &error);

        std::vector<uint8_t> value_data;

        if ((nullptr == error) && (nullptr != result)) {
            GVariant *array = g_variant_get_child_value(result, 0);
            if (nullptr != array) {
                gsize len = 0;
                const guint8 *data =
                    static_cast<const guint8 *>(g_variant_get_fixed_array(array, &len, sizeof(guint8)));
                if (data) {
                    value_data.assign(data, data + len);
                    // Print debug info
                    const std::string full_data_str = Translator::BytesToHexString(data, len);
                    LOG_I(kLogTag, "ReadValue data (Hex): %s", full_data_str.c_str());
                    LOG_I(kLogTag, "ReadValue received %zu bytes", len);
                }
                g_variant_unref(array);
            }
        } else {
            LOG_W(kLogTag, "ReadValueCallback received NULL result, but no error.");
        }

        {
            std::lock_guard<std::mutex> lock(self.read_mutex_);
            self.read_done_ = true;
            self.read_success_ = !value_data.empty();
            self.last_read_data_ = std::move(value_data);
        }

        if (nullptr != error) {
            LOG_W(kLogTag, "Error reading value: %s (code: %d)", error->message, error->code);
            g_error_free(error);
        }

        if (nullptr != result) {
            g_variant_unref(result);
        }

        LOG_I(kLogTag, "ReadValueCallback completed with result: %s", (self.read_success_ ? "success" : "failure"));
        self.read_cv_.notify_all();
    }
}

bool DbusBTDevice::DbusDeviceImpl::SetLeDiscoveryFilter() {
    LOG_I(kLogTag, "Setting discovery filter...");
    bool result = false;
    GError *error = NULL;
    GVariant *response = NULL;

    // Local constant variables for filter keys and values for LE filters
    constexpr const char *kTransportKey = "Transport";
    constexpr const char *kDuplicateDataKey = "DuplicateData";
    constexpr const char *kTransportValue = "le";
    constexpr const char *kDiscoveryFilterMethod = "SetDiscoveryFilter";

    do {
        if (!connection_) {
            LOG_E(kLogTag, "Connection not initialized");
            break;
        }

        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE(kGVariantTypeArrayStringToVariant));

        g_variant_builder_add(&builder, kGVariantTypeStringToVariant, kTransportKey,
                              g_variant_new_string(kTransportValue));
        g_variant_builder_add(&builder, kGVariantTypeStringToVariant, kDuplicateDataKey, g_variant_new_boolean(TRUE));

        response = g_dbus_connection_call_sync(connection_, BLUEZ_SERVICE_NAME, BLUEZ_ADAPTER_PATH,
                                               BLUEZ_ADAPTER_INTERFACE, kDiscoveryFilterMethod,
                                               g_variant_new(kGVariantTypeArrayStringToVariantTuple, &builder), NULL,
                                               G_DBUS_CALL_FLAGS_NONE, kMaxDefaultConnectionCallTimeout, NULL, &error);

        if (error) {
            LOG_E(kLogTag, "Failed to set discovery filter: %s (code: %d)", error->message, error->code);
            g_error_free(error);
            break;
        }
        LOG_I(kLogTag, "Discovery filter set successfully");

        result = true;
    } while (false);

    if (response) {
        g_variant_unref(response);
    }

    return result;
}

bool DbusBTDevice::DbusDeviceImpl::StartDiscovery() {
    bool result = false;
    LOG_I(kLogTag, "Starting discovery...");
    GError *error = NULL;
    GVariant *ret = NULL;

    scanned_results_arrived_ = true;

    do {
        // First ensure adapter is powered on
        if (!IsEnabled()) {
            if (!Enable()) {
                LOG_E(kLogTag, "Failed to enable adapter");
                break;
            }
        }

        LOG_I(kLogTag, "Adapter is enabled, proceeding with discovery Filter");

        // Set discovery filter
        if (!SetLeDiscoveryFilter()) {
            LOG_E(kLogTag, "Failed to set discovery filter");
            break;
        }

        LOG_I(kLogTag, "Discovery filter set, proceeding with discovery");

        constexpr const char *kStartDiscoveryMethod = "StartDiscovery";

        // Start discovery
        ret = g_dbus_connection_call_sync(connection_, BLUEZ_SERVICE_NAME, BLUEZ_ADAPTER_PATH, BLUEZ_ADAPTER_INTERFACE,
                                          kStartDiscoveryMethod, NULL, NULL, G_DBUS_CALL_FLAGS_NONE,
                                          kMaxDefaultConnectionCallTimeout, NULL, &error);

        {
            std::unique_lock<std::mutex> lock(scan_status_mutex_);
            constexpr auto kScanTimeout = std::chrono::seconds(kMaxDefaultCondWaitTimeout);
            if (scan_status_cv_.wait_for(lock, kScanTimeout, [this] { return scan_status_; })) {
                LOG_I(kLogTag, "StartDiscovery completed with status: success");
            } else {
                LOG_W(kLogTag, "StartDiscovery timed out.");
            }
        }

        if (error) {
            LOG_W(kLogTag, "Failed to start discovery: %s", error->message);

            std::string error_msg = error->message ? error->message : "Unknown error";

            if (error_msg.find("Operation already in progress") != std::string::npos) {
                LOG_I(kLogTag, "Discovery already in progress.");
                g_error_free(error);
            } else {
                LOG_E(kLogTag, "Failed to start discovery: %s", error->message);
                g_error_free(error);
                break;
            }
        }

        LOG_I(kLogTag, "Discovery started successfully");

        result = true;
    } while (false);

    if (ret) {
        g_variant_unref(ret);
    }

    if (result) {
        if (!hci_scan_thread_.joinable()) {
            StartLeScanLoop();
        }
    }
    return result;
}

bool DbusBTDevice::StartDiscovery() {
    bool status = false;
    const std::lock_guard<std::mutex> lock(api_mutex_);
    if (dbus_device_impl_ptr_) {
        status = dbus_device_impl_ptr_->StartDiscovery();
    }
    return status;
}

bool DbusBTDevice::SetScanParameters(int scan_interval, int scan_window) {
    return false;
}

bool DbusBTDevice::DbusDeviceImpl::StopDiscovery() {
    bool result = false;
    LOG_I(kLogTag, "Stopping discovery...");
    GError *error = nullptr;

    constexpr const char *kSetDiscoveryFilterMethod = "SetDiscoveryFilter";
    constexpr const char *kStopDiscoveryMethod = "StopDiscovery";

    StopLeScanLoop();

    do {
        g_dbus_connection_call(connection_, BLUEZ_SERVICE_NAME, BLUEZ_ADAPTER_PATH, BLUEZ_ADAPTER_INTERFACE,
                               kSetDiscoveryFilterMethod, nullptr, nullptr, G_DBUS_CALL_FLAGS_NONE,
                               kMaxDefaultConnectionCallTimeout, nullptr, nullptr, &error);
        if (nullptr != error) {
            LOG_E(kLogTag, "Not able to remove discovery filter: %s (code: %d)", error->message, error->code);
            g_error_free(error);
        }

        error = nullptr;  // Reset error before next call

        g_dbus_connection_call(connection_, BLUEZ_SERVICE_NAME, BLUEZ_ADAPTER_PATH, BLUEZ_ADAPTER_INTERFACE,
                               kStopDiscoveryMethod, nullptr, nullptr, G_DBUS_CALL_FLAGS_NONE,
                               kMaxDefaultConnectionCallTimeout, nullptr, nullptr, &error);

        if (error) {
            LOG_E(kLogTag, "Not able to stop scanning: %s (code: %d)", error->message, error->code);
            g_error_free(error);
            break;
        }

        {
            std::unique_lock<std::mutex> lock(scan_status_mutex_);
            constexpr auto kScanTimeout = std::chrono::seconds(kMaxDefaultCondWaitTimeout);
            if (scan_status_cv_.wait_for(lock, kScanTimeout, [this] { return !scan_status_; })) {
                LOG_I(kLogTag, "StopDiscovery completed with status: success");
                result = true;
            } else {
                LOG_W(kLogTag, "StopDiscovery timed out.");
            }
        }
    } while (false);

    return result;
}

bool DbusBTDevice::StopDiscovery() {
    bool status = false;
    const std::lock_guard<std::mutex> lock(api_mutex_);
    if (dbus_device_impl_ptr_) {
        status = dbus_device_impl_ptr_->StopDiscovery();
    }
    return status;
}

bool DbusBTDevice::DbusDeviceImpl::ConnectToDevice(const std::string &mac_addr) {
    LOG_I(kLogTag, "ConnectToDevice called for MAC: %s", mac_addr.c_str());
    std::string object_path;
    bool result = false;  // Single return point

    do {
        if (mac_addr.empty()) {
            LOG_E(kLogTag, "MAC address is empty.");
            break;
        }

        // Set the target MAC address and reset device_found flag
        {
            std::lock_guard<std::mutex> lock(device_found_mutex_);
            target_mac_addr_ = mac_addr;
            device_found_ = false;
        }

        // Wait for device to appear with timeout
        {
            std::unique_lock<std::mutex> lock(device_found_mutex_);
            constexpr auto kDeviceFoundTimeout = std::chrono::seconds(10);
            if (device_found_cv_.wait_for(lock, kDeviceFoundTimeout, [this] { return device_found_; })) {
                LOG_I(kLogTag, "Device found: %s", mac_addr.c_str());
            } else {
                LOG_E(kLogTag, "Device with MAC %s not found after waiting.", mac_addr.c_str());
                break;
            }
        }

        // Now that we know the device exists, find its object path
        {
            std::lock_guard<std::mutex> lock(listeners_mutex_);

            object_path = DbusUtil::CreateObjectPathFromMac(mac_addr);

            if (0 != listeners_.count(object_path)) {
                LOG_I(kLogTag, "Device with MAC %s found in listeners map.", mac_addr.c_str());
            } else {
                LOG_W(kLogTag, "Device with MAC %s not found in listeners map", mac_addr.c_str());
                break;
            }
        }

        {
            std::lock_guard<std::mutex> lock(connect_mutex_);
            connect_done_ = false;
            connect_success_ = false;
        }

        constexpr int kGDBusCallTimeoutMs = 10000;
        static constexpr const char *kDeviceConnectMethod = "Connect";
        LOG_I(kLogTag, "Starting connection to device at path: %s", object_path.c_str());
        // Start async connect
        g_dbus_connection_call(connection_, BLUEZ_SERVICE_NAME, object_path.c_str(), kBluezDeviceInterface,
                               kDeviceConnectMethod, NULL, NULL, G_DBUS_CALL_FLAGS_NONE,
                               kGDBusCallTimeoutMs,  // 10000 ms timeout for D-Bus calls
                               nullptr, (GAsyncReadyCallback) DbusBTDevice::DbusDeviceImpl::OnConnectCallback, this);
        name_to_write_ = mac_addr;

        // Wait for connect callback
        {
            std::unique_lock<std::mutex> lock(connect_mutex_);
            constexpr auto kConnectTimeout = std::chrono::seconds(10);
            if (connect_cv_.wait_for(lock, kConnectTimeout, [this] { return connect_done_; })) {
                result = connect_success_;
                if (!connect_success_) {
                    LOG_W(kLogTag, "Connect failed.");
                    break;
                }
                LOG_I(kLogTag, "Device connected: %s", object_path.c_str());
            } else {
                LOG_W(kLogTag, "Connect timed out. %s", object_path.c_str());
                break;
            }
        }

    } while (false);

    return result;
}

std::string DbusBTDevice::DbusDeviceImpl::FindCharacteristicPath(const std::string &mac_addr, const std::string &uuid) {
    std::string discovered_char_path;
    const std::string object_path = DbusUtil::CreateObjectPathFromMac(mac_addr);

    std::string uuid_lower = uuid;
    std::transform(uuid_lower.begin(), uuid_lower.end(), uuid_lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    LOG_I(kLogTag, "FindCharacteristicPath called for MAC: %s UUID: %s", mac_addr.c_str(), uuid_lower.c_str());

    {
        std::lock_guard<std::mutex> lock(listeners_mutex_);
        const auto listener_itr = listeners_.find(object_path);
        if (listener_itr != listeners_.end()) {
            if (listener_itr->second.address == mac_addr) {
                const auto char_itr = listener_itr->second.properties.find(uuid_lower);
                if (char_itr != listener_itr->second.properties.end()) {
                    discovered_char_path = char_itr->second;
                }
            }
        }
    }

    if (discovered_char_path.empty()) {
        LOG_W(kLogTag, "Characteristic with UUID %s for MAC %s not found.", uuid.c_str(), mac_addr.c_str());
    }
    return discovered_char_path;
}

bool DbusBTDevice::DbusDeviceImpl::RemoveDeviceAsync(const std::string &device_path) {
    GError *error = nullptr;
    bool result = false;

    // Create proxy for adapter
    GDBusProxy *adapter_proxy = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE, NULL,
                                                              BLUEZ_SERVICE_NAME, BLUEZ_ADAPTER_PATH,
                                                              BLUEZ_ADAPTER_INTERFACE, NULL, &error);

    do {
        if (error != nullptr) {
            LOG_E(kLogTag, "Error creating proxy in %s: %s (code: %d)", __func__, error->message, error->code);
            g_error_free(error);
            break;
        }

        {
            std::lock_guard<std::mutex> lock(remove_device_mutex_);
            remove_device_done_ = false;
            remove_device_success_ = false;
        }

        static constexpr const char *kRemoveDeviceMethod = "RemoveDevice";
        static constexpr int kRemoveDeviceTimeOut = 5000;  // 5000 ms timeout for D-Bus calls
        // Start async call
        g_dbus_proxy_call(adapter_proxy, kRemoveDeviceMethod,
                          g_variant_new(kGVariantTypeObjectPath, device_path.c_str()), G_DBUS_CALL_FLAGS_NONE,
                          kRemoveDeviceTimeOut,  // Timeout in milliseconds, -1 means default timeout
                          NULL, DbusBTDevice::DbusDeviceImpl::RemoveDeviceAsyncCallback, this);

        // Wait for callback
        {
            std::unique_lock<std::mutex> lock(remove_device_mutex_);
            constexpr auto kRemoveDeviceCondTimeout = std::chrono::seconds(5);
            if (remove_device_cv_.wait_for(lock, kRemoveDeviceCondTimeout, [this] { return remove_device_done_; })) {
                result = remove_device_success_;
                LOG_I(kLogTag, "RemoveDeviceAsync completed with status: %s", result ? "success" : "failure");
            } else {
                LOG_E(kLogTag, "RemoveDeviceAsync timed out for %s", device_path.c_str());
                result = false;
            }
        }

        {
            std::lock_guard<std::mutex> lock(listeners_mutex_);
            auto path_itr = listeners_.find(device_path);
            if (path_itr != listeners_.end()) {
                // Clean up proxy if needed
                if (path_itr->second.proxy) {
                    g_object_unref(path_itr->second.proxy);
                }
                listeners_.erase(path_itr);
                LOG_I(kLogTag, "[RemoveDevicAsync] Removed device from listeners_: %s", device_path.c_str());
            }
        }

    } while (false);

    if (nullptr != adapter_proxy) {
        g_object_unref(adapter_proxy);
    }

    return result;
}

void DbusBTDevice::DbusDeviceImpl::RemoveDeviceAsyncCallback(GObject *source_object, GAsyncResult *res,
                                                             gpointer user_data) {
    if (user_data != nullptr && source_object != nullptr) {
        DbusBTDevice::DbusDeviceImpl &self = *static_cast<DbusBTDevice::DbusDeviceImpl *>(user_data);
        GError *error = nullptr;
        GVariant *result = g_dbus_proxy_call_finish(G_DBUS_PROXY(source_object), res, &error);

        {
            std::lock_guard<std::mutex> lock(self.remove_device_mutex_);
            self.remove_device_done_ = true;
            self.remove_device_success_ = (result != nullptr && error == nullptr);
        }
        self.remove_device_cv_.notify_all();

        if (nullptr != error) {
            LOG_E(kLogTag, "Error in RemoveDeviceAsyncCallback: %s (code: %d)", error->message, error->code);
            g_error_free(error);
        }
        if (nullptr != result) {
            g_variant_unref(result);
        }
    } else {
        LOG_W(kLogTag, "RemoveDeviceAsyncCallback: user_data or source_object is NULL");
    }
}

// Write data to a GATT characteristic for a given MAC and UUID
bool DbusBTDevice::DbusDeviceImpl::WriteCharacteristicData(const std::string &mac_addr, const std::string &uuid,
                                                           const std::vector<std::string> &data,
                                                           nd::interface::AddressType addr_type, void *caller_arg) {
    bool status = false;
    LOG_I(kLogTag, "WriteCharacteristicData called for MAC: %s UUID: %s", mac_addr.c_str(), uuid.c_str());

    auto start_time = std::chrono::system_clock::now();

    NDService *service_obj_ptr = nullptr;

    if (nullptr != caller_arg) {
        service_obj_ptr = static_cast<NDService *>(caller_arg);
    }

    do {
        // --- Clear previous devices and start discovery ---

        LOG_I(kLogTag, "Resetting Bluetooth radio before read operation...");
        if (DbusUtil::ResetBluetoothRadio()) {
                LOG_I(kLogTag, "Bluetooth radio reset successful.");
        } else {
                LOG_E(kLogTag, "Bluetooth radio reset failed.");
                if (service_obj_ptr) {
                    if (!service_obj_ptr->send_err_msg(SM_E_BTFV_BT_STACK_FAIL, static_cast<int>(BtStackErr::kResetBluetoothRadioFailed),
                                                        "DBUS BT module reset failed")) {
                        LOG_E(kLogTag, "SM_E_BTFV_BT_STACK_FAIL send_err_msg failed");
                    }
                }
                break;
            }

        if (Enable()) {
            LOG_I(kLogTag, "Bluetooth adapter enabled successfully before write operation.");
        } else {
            LOG_E(kLogTag, "Failed to enable Bluetooth adapter before write operation.");
        }

        if (!ClearPrevDevices(mac_addr)) {
            LOG_W(kLogTag, "Failed to clear previous devices.");
        }

        if (!StartDiscovery()) {
            LOG_E(kLogTag, "Failed to start discovery.");
            if (service_obj_ptr) {
                if (!service_obj_ptr->send_err_msg(SM_E_BTFV_BT_STACK_FAIL, static_cast<int>(BtStackErr::kScanFailed),
                                                    "DBUS BT module scan failed")) {
                    LOG_E(kLogTag, "SM_E_BTFV_BT_STACK_FAIL send_err_msg failed");
                }
            }
            break;
        }

        // 1. Connect to device
        if (!ConnectToDevice(mac_addr)) {
            const auto end_time = std::chrono::system_clock::now();
            const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
            LOG_W(kLogTag, "Could not connect to device after %ldms.", duration.count());
            break;  // If connect fails, skip all other steps
        }
        auto end_time = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        LOG_I(kLogTag, "Successfully connected to device in %ldms.", duration.count());

        if (WaitForServicesResolved(mac_addr, services_resolved_time_out)) {
            LOG_I(kLogTag, "ServicesResolved received for device %s", mac_addr.c_str());
        } else {
            LOG_W(kLogTag, "Timeout waiting for ServicesResolved for device %s", mac_addr.c_str());
        }

        // 2. Find characteristic path
        start_time = std::chrono::system_clock::now();
        const std::string char_path = FindCharacteristicPath(mac_addr, uuid);

        if (char_path.empty()) {
            end_time = std::chrono::system_clock::now();
            duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
            LOG_W(kLogTag, "Could not find characteristic path after %ldms.", duration.count());
            // Do not break; continue to disconnect and remove device
        } else {
            end_time = std::chrono::system_clock::now();
            duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
            LOG_I(kLogTag, "Found characteristic path in %ld ms.", duration.count());

            // 3. Write characteristic for each element in data
            bool write_result = false;
            for (const auto &data_str : data) {
                write_result = WriteCharacteristic(char_path.c_str(), reinterpret_cast<const guint8 *>(data_str.data()),
                                                   data_str.size());
                if (!write_result) {
                    LOG_E(kLogTag, "WriteCharacteristic failed for data: %s", data_str.c_str());
                    break;  // If write fails, skip further writes
                } else {
                    LOG_I(kLogTag, "WriteCharacteristicData: Write successful for data: %s", data_str.c_str());
                }
            }

            status = write_result;
        }

        // 4. Find object path for device
        std::string object_path = DbusUtil::CreateObjectPathFromMac(mac_addr);
        LOG_I(kLogTag, "WriteCharacteristicData: Finding object path for device with MAC: %s", mac_addr.c_str());

        // 5. Disconnect device
        if (!object_path.empty()) {
            if (!DisconnectDevice(object_path)) {
                LOG_E(kLogTag, "DisconnectDevice failed.");
            } else {
                LOG_I(kLogTag, "Device disconnected: %s", object_path.c_str());
            }

            // 6. Remove device async
            LOG_I(kLogTag, "WriteCharacteristicData: Calling RemoveDeviceAsync for %s", object_path.c_str());
            RemoveDeviceAsync(object_path);
        } else {
            LOG_E(kLogTag, "WriteCharacteristicData: Could not find object path for device removal.");
        }
    } while (false);

    // --- Stop discovery after operation ---
    if (!StopDiscovery()) {
        LOG_W(kLogTag, "Failed to stop discovery after write operation.");
    } else {
        LOG_I(kLogTag, "Discovery stopped after write operation.");
    }

    LOG_I(kLogTag, "Resetting Bluetooth radio after write operation...");
        if (DbusUtil::ResetBluetoothRadio()) {
                LOG_I(kLogTag, "Bluetooth radio reset successful.");
        } else {
                LOG_E(kLogTag, "Bluetooth radio reset failed.");
                if (service_obj_ptr) {
                    if (!service_obj_ptr->send_err_msg(SM_E_BTFV_BT_STACK_FAIL, static_cast<int>(BtStackErr::kResetBluetoothRadioFailed),
                                                        "DBUS BT module reset failed")) {
                        LOG_E(kLogTag, "SM_E_BTFV_BT_STACK_FAIL send_err_msg failed");
                    }
                }
            }

    if (Enable()) {
        LOG_I(kLogTag, "Bluetooth adapter enabled successfully after write operation.");
    } else {
        LOG_E(kLogTag, "Failed to enable Bluetooth adapter after write operation.");
        if (service_obj_ptr) {
            if (!service_obj_ptr->send_err_msg(SM_E_BTFV_BT_STACK_FAIL, static_cast<int>(BtStackErr::kEnableFailed),
                                                "DBUS BT module enable failed")) {
                LOG_E(kLogTag, "SM_E_BTFV_BT_STACK_FAIL send_err_msg failed");
            }
        }
    }

    LOG_I(kLogTag, "WriteCharacteristicData completed with status: %s", (status ? "success" : "failure"));
    return status;
}

bool DbusBTDevice::WriteCharacteristicData(const std::string &mac_addr, const std::string &uuid,
                                           const std::vector<std::string> &data, nd::interface::AddressType addr_type,
                                           void *caller_arg) {
    bool status = false;
    const std::lock_guard<std::mutex> lock(api_mutex_);
    if (dbus_device_impl_ptr_) {
        status = dbus_device_impl_ptr_->WriteCharacteristicData(mac_addr, uuid, data, addr_type, caller_arg);
    }
    return status;
}

// Read a single GATT characteristic value for a given MAC and UUID
bool DbusBTDevice::DbusDeviceImpl::ReadCharacteristicData(const std::string &mac_addr, const std::string &uuid,
                                                          std::vector<uint8_t> &result_out, void *caller_arg) {
    bool status = false;

    LOG_I(kLogTag, "ReadCharacteristicData called for MAC: %s UUID: %s", mac_addr.c_str(), uuid.c_str());

    auto start_time = std::chrono::system_clock::now();

    NDService *service_obj_ptr = nullptr;

    if (nullptr != caller_arg) {
        service_obj_ptr = static_cast<NDService *>(caller_arg);
    }

    do {
        LOG_I(kLogTag, "Resetting Bluetooth radio before read operation...");
        if (DbusUtil::ResetBluetoothRadio()) {
                LOG_I(kLogTag, "Bluetooth radio reset successful.");
        } else {
                LOG_E(kLogTag, "Bluetooth radio reset failed.");
                if (service_obj_ptr) {
                    if (!service_obj_ptr->send_err_msg(SM_E_BTFV_BT_STACK_FAIL, static_cast<int>(BtStackErr::kResetBluetoothRadioFailed),
                                                        "DBUS BT module reset failed")) {
                        LOG_E(kLogTag, "SM_E_BTFV_BT_STACK_FAIL send_err_msg failed");
                    }
                }
                break;
            }

        if (Enable()) {
            LOG_I(kLogTag, "Bluetooth adapter enabled successfully before read operation.");
        } else {
            LOG_E(kLogTag, "Failed to enable Bluetooth adapter before read operation.");
        }

        // 2. Clear previous devices and start discovery
        if (!ClearPrevDevices(mac_addr)) {
            LOG_W(kLogTag, "Failed to clear previous devices.");
        }
        if (!StartDiscovery()) {
            LOG_W(kLogTag, "Failed to start discovery.");
            if (service_obj_ptr) {
                if (!service_obj_ptr->send_err_msg(SM_E_BTFV_BT_STACK_FAIL, static_cast<int>(BtStackErr::kScanFailed),
                                                    "DBUS BT module scan failed")) {
                    LOG_E(kLogTag, "SM_E_BTFV_BT_STACK_FAIL send_err_msg failed");
                }
            }
            break;
        }

        // 3. Connect to device
        if (!ConnectToDevice(mac_addr)) {
            auto end_time = std::chrono::system_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
            LOG_W(kLogTag, "Could not connect to device after %ldms.", duration.count());
            break;
        }
        auto end_time = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        LOG_I(kLogTag, "Successfully connected to device in %ldms.", duration.count());

        if (WaitForServicesResolved(mac_addr, services_resolved_time_out)) {
            LOG_I(kLogTag, "ServicesResolved received for device %s", mac_addr.c_str());
        } else {
            LOG_W(kLogTag, "Timeout waiting for ServicesResolved for device %s", mac_addr.c_str());
        }

        // 4. Find characteristic path
        start_time = std::chrono::system_clock::now();
        const auto char_path = FindCharacteristicPath(mac_addr, uuid);
        if (char_path.empty()) {
            end_time = std::chrono::system_clock::now();
            duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
            LOG_W(kLogTag, "Could not find characteristic path after %ldms.", duration.count());
            // Continue to disconnect and remove device
        } else {
            end_time = std::chrono::system_clock::now();
            duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
            LOG_I(kLogTag, "Found characteristic path in %ld ms for UUID: %s", duration.count(), uuid.c_str());
            start_time = std::chrono::system_clock::now();

            // 5. Read characteristic value
            std::pair<bool, std::vector<uint8_t>> read_pair = ReadCharacteristic(char_path.c_str(), mac_addr.c_str());
            const bool read_result = read_pair.first;
            if (!read_result) {
                LOG_W(kLogTag, "ReadCharacteristic failed for UUID: %s on device: %s", uuid.c_str(), mac_addr.c_str());
                // Continue to disconnect and remove device
            } else {
                end_time = std::chrono::system_clock::now();
                duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
                LOG_I(kLogTag, "ReadCharacteristic took %ld ms for UUID: %s", duration.count(), uuid.c_str());
                result_out = std::move(read_pair.second);
            }
            status = read_result;
        }

        // 6. Find object path for device
        std::string object_path = DbusUtil::CreateObjectPathFromMac(mac_addr);
        LOG_I(kLogTag, "ReadCharacteristicData: Finding object path for device with MAC: %s", mac_addr.c_str());

        // 7. Disconnect device and remove from BlueZ
        if (!object_path.empty()) {
            if (!DisconnectDevice(object_path)) {
                LOG_E(kLogTag, "DisconnectDevice failed.");
            } else {
                LOG_I(kLogTag, "Device disconnected: %s", object_path.c_str());
            }

            // Remove device asynchronously
            LOG_I(kLogTag, "ReadCharacteristicData: Calling RemoveDeviceAsync for %s", object_path.c_str());
            RemoveDeviceAsync(object_path);
        } else {
            LOG_E(kLogTag, "ReadCharacteristicData: Could not find object path for device removal.");
        }
    } while (false);

    // 8. Stop discovery after operation
    if (!StopDiscovery()) {
        LOG_W(kLogTag, "Failed to stop discovery after read operation.");
    } else {
        LOG_I(kLogTag, "Discovery stopped after read operation.");
    }

    LOG_I(kLogTag, "Resetting Bluetooth radio after read operation...");
        if (DbusUtil::ResetBluetoothRadio()) {
                LOG_I(kLogTag, "Bluetooth radio reset successful.");
        } else {
                LOG_E(kLogTag, "Bluetooth radio reset failed.");
                if (service_obj_ptr) {
                    if (!service_obj_ptr->send_err_msg(SM_E_BTFV_BT_STACK_FAIL, static_cast<int>(BtStackErr::kResetBluetoothRadioFailed),
                                                        "DBUS BT module reset failed")) {
                        LOG_E(kLogTag, "SM_E_BTFV_BT_STACK_FAIL send_err_msg failed");
                    }
                }
            }

    if (Enable()) {
        LOG_I(kLogTag, "Bluetooth adapter enabled successfully after read operation.");
    } else {
        LOG_E(kLogTag, "Failed to enable Bluetooth adapter after read operation.");
        if (service_obj_ptr) {
            if (!service_obj_ptr->send_err_msg(SM_E_BTFV_BT_STACK_FAIL, static_cast<int>(BtStackErr::kEnableFailed),
                                                "DBUS BT module enable failed")) {
                LOG_E(kLogTag, "SM_E_BTFV_BT_STACK_FAIL send_err_msg failed");
            }
        }
    }

    LOG_I(kLogTag, "ReadCharacteristicData completed with status: %s", (status ? "success" : "failure"));
    return status;
}

bool DbusBTDevice::ReadCharacteristicData(const std::string &mac_addr, const std::string &uuid_str,
                                          std::vector<uint8_t> &result_out, void *caller_arg) {
    bool status = false;
    const std::lock_guard<std::mutex> lock(api_mutex_);
    if (dbus_device_impl_ptr_) {
        status = dbus_device_impl_ptr_->ReadCharacteristicData(mac_addr, uuid_str, result_out, caller_arg);
    }
    return status;
}

bool DbusBTDevice::DbusDeviceImpl::ReadCharacteristicData(
    const std::string &mac_addr, std::unordered_map<std::string, std::vector<uint8_t>> &charac_data_out,
    void *caller_arg) {
    bool status = false;

    LOG_I(kLogTag, "ReadCharacteristicData (user-specified UUIDs) called for MAC: %s", mac_addr.c_str());

    auto start_time = std::chrono::system_clock::now();

    NDService *service_obj_ptr = nullptr;

    if (nullptr != caller_arg) {
        service_obj_ptr = static_cast<NDService *>(caller_arg);
    }

    do {
        LOG_I(kLogTag, "Resetting Bluetooth radio before read operation...");
        if (DbusUtil::ResetBluetoothRadio()) {
                LOG_I(kLogTag, "Bluetooth radio reset successful.");
        } else {
                LOG_E(kLogTag, "Bluetooth radio reset failed.");
                if (service_obj_ptr) {
                    if (!service_obj_ptr->send_err_msg(SM_E_BTFV_BT_STACK_FAIL, static_cast<int>(BtStackErr::kResetBluetoothRadioFailed),
                                                        "DBUS BT module reset failed")) {
                        LOG_E(kLogTag, "SM_E_BTFV_BT_STACK_FAIL send_err_msg failed");
                    }
                }
                break;
            }

        if (Enable()) {
            LOG_I(kLogTag, "Bluetooth adapter enabled successfully before read operation.");
        } else {
            LOG_E(kLogTag, "Failed to enable Bluetooth adapter before read operation.");
        }

        // --- Clear previous devices and start discovery ---
        if (!ClearPrevDevices(mac_addr)) {
            LOG_W(kLogTag, "Failed to clear previous devices.");
        }

        if (!StartDiscovery()) {
            LOG_W(kLogTag, "Failed to start discovery.");
            if (service_obj_ptr) {
                if (!service_obj_ptr->send_err_msg(SM_E_BTFV_BT_STACK_FAIL, static_cast<int>(BtStackErr::kScanFailed),
                                                    "DBUS BT module scan failed")) {
                    LOG_E(kLogTag, "SM_E_BTFV_BT_STACK_FAIL send_err_msg failed");
                }
            }
            break;
        }

        // 1. Connect to device
        if (!ConnectToDevice(mac_addr)) {
            auto end_time = std::chrono::system_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
            LOG_E(kLogTag, "Could not connect to device after %ldms.", duration.count());
            break;
        }
        auto end_time = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        LOG_I(kLogTag, "Successfully connected to device in %ldms.", duration.count());

        if (WaitForServicesResolved(mac_addr, services_resolved_time_out)) {
            LOG_I(kLogTag, "ServicesResolved received for device %s", mac_addr.c_str());
        } else {
            LOG_W(kLogTag, "Timeout waiting for ServicesResolved for device %s", mac_addr.c_str());
        }

        // 2. For each UUID in the map, find characteristic path and read
        // Collect UUIDs to avoid iterator invalidation
        std::vector<std::string> uuids;
        for (const auto &pair : charac_data_out) {
            LOG_I(kLogTag, "Printing UUID: %s for device: %s", pair.first.c_str(), mac_addr.c_str());
            uuids.push_back(pair.first);
        }

        bool all_reads_successful = true;
        for (const auto &uuid : uuids) {
            LOG_I(kLogTag, "Processing UUID: %s for device: %s", uuid.c_str(), mac_addr.c_str());
            std::string char_path = FindCharacteristicPath(mac_addr, uuid);
            if (char_path.empty()) {
                LOG_W(kLogTag, "Could not find characteristic path for UUID: %s", uuid.c_str());
                all_reads_successful = false;
                continue;
            }

            LOG_I(kLogTag, "Found characteristic for UUID: %s on device: %s", uuid.c_str(), mac_addr.c_str());

            // 5. Read characteristic value
            std::pair<bool, std::vector<uint8_t>> read_pair = ReadCharacteristic(char_path.c_str(), mac_addr.c_str());
            bool read_result = read_pair.first;
            if (!read_result) {
                LOG_E(kLogTag, "ReadCharacteristic failed for UUID: %s on device: %s", uuid.c_str(), mac_addr.c_str());
                all_reads_successful = false;
                // Continue to disconnect and remove device
            } else {
                end_time = std::chrono::system_clock::now();
                duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
                LOG_I(kLogTag, "ReadCharacteristic took %ld ms for UUID: %s", duration.count(), uuid.c_str());
                LOG_I(kLogTag, "ReadCharacteristicData: Read successful.");
                charac_data_out[uuid] = std::move(read_pair.second);
            }
        }

        status = all_reads_successful;

        // 3. Find object path for device
        LOG_I(kLogTag, "ReadCharacteristicData: Finding object path for device with MAC: %s", mac_addr.c_str());
        std::string object_path = DbusUtil::CreateObjectPathFromMac(mac_addr);

        // 4. Disconnect device
        if (!object_path.empty()) {
            if (!DisconnectDevice(object_path)) {
                LOG_W(kLogTag, "DisconnectDevice failed.");
            } else {
                LOG_I(kLogTag, "Device disconnected: %s", object_path.c_str());
            }

            // 5. Remove device async
            LOG_I(kLogTag, "ReadCharacteristicData: Calling RemoveDeviceAsync for %s", object_path.c_str());
            RemoveDeviceAsync(object_path);
        } else {
            LOG_W(kLogTag, "ReadCharacteristicData: Could not find object path for device removal.");
        }
    } while (false);

    // --- Stop discovery after operation ---
    if (!StopDiscovery()) {
        LOG_W(kLogTag, "Failed to stop discovery after read operation.");
    } else {
        LOG_I(kLogTag, "Discovery stopped after read operation.");
    }

    LOG_I(kLogTag, "Resetting Bluetooth radio after read operation...");
        if (DbusUtil::ResetBluetoothRadio()) {
                LOG_I(kLogTag, "Bluetooth radio reset successful.");
        } else {
                LOG_E(kLogTag, "Bluetooth radio reset failed.");
                if (service_obj_ptr) {
                    if (!service_obj_ptr->send_err_msg(SM_E_BTFV_BT_STACK_FAIL, static_cast<int>(BtStackErr::kResetBluetoothRadioFailed),
                                                        "DBUS BT module reset failed")) {
                        LOG_E(kLogTag, "SM_E_BTFV_BT_STACK_FAIL send_err_msg failed");
                    }
                }
            }

    if (Enable()) {
        LOG_I(kLogTag, "Bluetooth adapter enabled successfully after read operation.");
    } else {
        LOG_E(kLogTag, "Failed to enable Bluetooth adapter after read operation.");
        if (service_obj_ptr) {
            if (!service_obj_ptr->send_err_msg(SM_E_BTFV_BT_STACK_FAIL, static_cast<int>(BtStackErr::kEnableFailed),
                                                "DBUS BT module enable failed")) {
                LOG_E(kLogTag, "SM_E_BTFV_BT_STACK_FAIL send_err_msg failed");
            }
        }
    }

    LOG_I(kLogTag, "ReadCharacteristicData (user-specified UUIDs) completed. Success: %s", status ? "true" : "false");

    return status;
}

bool DbusBTDevice::ReadCharacteristicData(const std::string &mac_addr,
                                          std::unordered_map<std::string, std::vector<uint8_t>> &charac_data_out,
                                          void *caller_arg) {
    bool status = false;
    const std::lock_guard<std::mutex> lock(api_mutex_);
    if (dbus_device_impl_ptr_) {
        status = dbus_device_impl_ptr_->ReadCharacteristicData(mac_addr, charac_data_out, caller_arg);
    }
    return status;
}

// // Read multiple GATT characteristics for multiple MAC addresses
// bool DbusBTDevice::DbusDeviceImpl::ReadCharacteristicData(
//     const std::unordered_map<std::string, std::vector<std::string>> &macToUuids,
//     std::unordered_map<std::string, std::unordered_map<std::string, std::vector<uint8_t>>> &charac_data_out,
//     void *caller_arg) {
//     bool overall_success = true;

//     LOG_I(kLogTag, "ReadCharacteristicData (multiple MACs) called");

//     // Iterate through each MAC address and its UUIDs
//     for (const auto &[mac_addr, uuids] : macToUuids) {
//         LOG_I(kLogTag, "Processing device: %s", mac_addr.c_str());

//         // Temporary map for this MAC address's results
//         std::unordered_map<std::string, std::vector<uint8_t>> mac_results;
//         bool connect_success = true;
//         bool disconnect_result = false;

//         auto start_time = std::chrono::system_clock::now();

//         do {
//             // Ensure adapter is in a clean state
//             if (!Disable()) {
//                 LOG_I(kLogTag, "Retrying to disable adapter...");
//                 std::this_thread::sleep_for(std::chrono::milliseconds(1000));  // Wait for 1 second before retrying
//                 if (!Disable()) {
//                     LOG_E(kLogTag, "Failed to disable adapter after retry.");
//                 } else {
//                     LOG_I(kLogTag, "Adapter disabled successfully.");
//                 }
//             } else {
//                 LOG_I(kLogTag, "Adapter disabled successfully.");
//             }

//             if (!Enable()) {
//                 LOG_E(kLogTag, "Failed to enable adapter before read operation.");
//                 overall_success = false;
//                 break;
//             }

//             // Clear previous devices and start discovery
//             if (!ClearPrevDevices(mac_addr)) {
//                 LOG_W(kLogTag, "Failed to clear previous devices.");
//             }
//             if (!StartDiscovery()) {
//                 LOG_E(kLogTag, "Failed to start discovery.");
//                 overall_success = false;
//                 break;
//             }

//             // Connect to device
//             if (!ConnectToDevice(mac_addr)) {
//                 auto end_time = std::chrono::system_clock::now();
//                 auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
//                 LOG_E(kLogTag, "Could not connect to device %s after %ldms.", mac_addr.c_str(), duration.count());
//                 connect_success = false;
//                 overall_success = false;
//                 break;  // Skip all other steps for this MAC
//             }

//             auto end_time = std::chrono::system_clock::now();
//             auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
//             LOG_I(kLogTag, "Successfully connected to device %s in %ldms.", mac_addr.c_str(),
//             duration.count());

//             // For each UUID, find characteristic path and read
//             for (const auto &uuid : uuids) {
//                 auto uuid_start_time = std::chrono::system_clock::now();

//                 std::string char_path = FindCharacteristicPath(mac_addr, uuid);
//                 auto uuid_end_time = std::chrono::system_clock::now();
//                 auto uuid_duration =
//                     std::chrono::duration_cast<std::chrono::milliseconds>(uuid_end_time - uuid_start_time);

//                 if (char_path.empty()) {
//                     LOG_E(kLogTag, "Could not find characteristic path for UUID: %s on device: %s", uuid.c_str(),
//                           mac_addr.c_str());
//                     overall_success = false;
//                     continue;
//                 }

//                 LOG_I(kLogTag, "Found characteristic path in %ld ms for UUID: %s", uuid_duration.count(),
//                       uuid.c_str());

//                 // Read characteristic
//                 uuid_start_time = std::chrono::system_clock::now();
//                 std::pair<bool, std::vector<uint8_t>> read_pair =
//                     ReadCharacteristic(char_path.c_str(), mac_addr.c_str());
//                 bool read_result = read_pair.first;
//                 std::vector<uint8_t> read_data = std::move(read_pair.second);

//                 if (!read_result) {
//                     LOG_E(kLogTag, "ReadCharacteristic failed for UUID: %s on device: %s", uuid.c_str(),
//                           mac_addr.c_str());
//                     overall_success = false;
//                     continue;
//                 }

//                 uuid_end_time = std::chrono::system_clock::now();
//                 uuid_duration = std::chrono::duration_cast<std::chrono::milliseconds>(uuid_end_time -
//                 uuid_start_time); LOG_I(kLogTag, "ReadCharacteristic took %ld ms for UUID: %s",
//                 uuid_duration.count(),
//                       uuid.c_str());

//                 // Print the read value
//                 std::ostringstream oss;
//                 oss << "Read value for UUID " << uuid << " on device " << mac_addr << " (" << read_data.size()
//                     << " bytes): ";
//                 for (auto b : read_data) {
//                     oss << std::hex << std::setw(2) << std::setfill('0') << (int) b << " ";
//                 }
//                 LOG_I(kLogTag, "%s", oss.str().c_str());

//                 mac_results[uuid] = std::move(read_data);
//             }

//             // Find object path for device
//             std::string object_path = DbusUtil::CreateObjectPathFromMac(mac_addr);

//             // Disconnect device
//             if (!object_path.empty()) {
//                 disconnect_result = DisconnectDevice(mac_addr);
//                 auto end_time = std::chrono::system_clock::now();
//                 auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
//                 if (!disconnect_result) {
//                     LOG_E(kLogTag, "DisconnectDevice failed for %s", mac_addr.c_str());
//                 } else {
//                     LOG_I(kLogTag, "Device %s disconnected in %ld ms", mac_addr.c_str(), duration.count());
//                     LOG_I(kLogTag, "Device %s disconnected, removing device...", mac_addr.c_str());
//                 }
//                 // Remove device async
//                 auto remove_start_time = std::chrono::system_clock::now();
//                 RemoveDeviceAsync(object_path);
//                 auto remove_end_time = std::chrono::system_clock::now();
//                 auto remove_duration =
//                     std::chrono::duration_cast<std::chrono::milliseconds>(remove_end_time - remove_start_time);
//                 LOG_I(kLogTag, "RemoveDeviceAsync took %ld ms", remove_duration.count());
//             } else {
//                 LOG_E(kLogTag, "ReadCharacteristicData: Could not find object path for device removal for %s",
//                       mac_addr.c_str());
//             }
//         } while (false);

//         // Stop discovery after operation
//         if (!StopDiscovery()) {
//             LOG_W(kLogTag, "Failed to stop discovery after read operation.");
//         } else {
//             LOG_I(kLogTag, "Discovery stopped after read operation.");
//         }

//         // Store results for this MAC address
//         charac_data_out[mac_addr] = std::move(mac_results);
//     }

//     LOG_I(kLogTag, "ReadCharacteristicData (multiple MACs) completed. Overall success: %s",
//           overall_success ? "true" : "false");

//     return overall_success;
// }

// bool DbusBTDevice::ReadCharacteristicData(
//     const std::unordered_map<std::string, std::vector<std::string>> &macToUuids,
//     std::unordered_map<std::string, std::unordered_map<std::string, std::vector<uint8_t>>> &charac_data_out,
//     void *caller_arg) {
//     bool status = false;
//     const std::lock_guard<std::mutex> lock(api_mutex_);
//     if (dbus_device_impl_ptr_) {
//         status = dbus_device_impl_ptr_->ReadCharacteristicData(macToUuids, charac_data_out, caller_arg);
//     }
//     return status;
// }

std::unique_ptr<interface::IBluetooth> BackupDeviceHelper::CreateBackUpInstance() {
    return std::make_unique<DbusBTDevice>();
}

bool DbusBTDevice::DoGattSetup() {
    LOG_I(kLogTag, "DoGattSetup: not implemented");
    return true;
}

bool DbusBTDevice::TearDownGattSetup() {
    LOG_I(kLogTag, "TearDownGattSetup: not implemented");
    return true;
}

bool DbusBTDevice::LeScanOn() {
    LOG_I(kLogTag, "LeScanOn: calling StartDiscovery");
    bool status = false;
    const std::lock_guard<std::mutex> lock(api_mutex_);
    if (dbus_device_impl_ptr_) {
        status = dbus_device_impl_ptr_->StartDiscovery();
    }
    return status;
}

bool DbusBTDevice::LeScanOff() {
    LOG_I(kLogTag, "LeScanOff: calling StopDiscovery");
    bool status = false;
    const std::lock_guard<std::mutex> lock(api_mutex_);
    if (dbus_device_impl_ptr_) {
        status = dbus_device_impl_ptr_->StopDiscovery();
    }
    return status;
}

bool DbusBTDevice::RegisterLeEventCallback(interface::LeEventCb cb) {
    bool status = false;
    const std::lock_guard<std::mutex> lock(api_mutex_);
    if (dbus_device_impl_ptr_) {
        status = dbus_device_impl_ptr_->RegisterLeEventCallback(std::move(cb));
    }
    return status;
}

bool DbusBTDevice::SupportsScanAndConnectionParallelly() {
    LOG_I(kLogTag, "SupportsScanAndConnectionParallelly: not implemented");
    return true;
}

#ifdef MAIN_TEST
int main() {
    // Initialize the DbusBTDevice object
    DbusBTDevice *bluetooth = new DbusBTDevice();
    if (!bluetooth) {
        std::cout << "Failed to allocate DbusBTDevice object" << std::endl;
        return 1;
    }

    const int argc = 7;
    char arg0[] = "./master";
    // char arg1[] = "5C:AD:54:0F:E3:2B"; // mac_to_write

    std::string mac_address;
    std::cout << "Enter MAC address (format: XX:XX:XX:XX:XX:XX): ";
    std::getline(std::cin, mac_address);

    // Convert string to char array
    char arg1[18];  // MAC address format is 17 chars + null terminator
    strncpy(arg1, mac_address.c_str(), sizeof(arg1) - 1);
    arg1[sizeof(arg1) - 1] = '\0';  // Ensure null termination

    char arg2[] = "00002a8a-0000-1000-8000-00805f9b34fb";  // characteristic_to_write
    char arg3[] = "A Very Big String";                     // data_to_write
    char arg4[] = "10";                                    // interval
    char arg5[] = "5";                                     // max_iterations
    char arg6[] = "w";                                     // todo (as single char in string)

    char *argv[argc] = {arg0, arg1, arg2, arg3, arg4, arg5, arg6};
    bluetooth->SetWriteParams(argv);

    // In main()
    if (!bluetooth->IsEnabled()) {
        if (!bluetooth->Enable()) {
            std::cout << DbusBTDevice::GetTimestamp() << " Failed to enable Bluetooth adapter" << std::endl;
            delete bluetooth;
            return 1;
        }
    }

    // bluetooth -> OnBoot();

    if (!bluetooth->ClearPrevDevices()) {
        std::cout << DbusBTDevice::GetTimestamp() << " Warning: Failed to clear previous devices" << std::endl;
    }

    if (!bluetooth->StartDiscovery()) {
        std::cout << DbusBTDevice::GetTimestamp() << " Failed to start discovery" << std::endl;
        delete bluetooth;
        return 1;
    }

    // bluetooth -> PrintListeners();
    // Test Beacon Advertising with new signature
    std::vector<uint8_t> beacon_uuid = {
        0x12, 0x34, 0x56,
        0x78,  // First 4 bytes
        0x12,
        0x34,  // Next 2 bytes
        0x12,
        0x34,  // Next 2 bytes
        0x12,
        0x34,  // Next 2 bytes
        0x12, 0x34, 0x56, 0x78, 0x9A,
        0xBC  // Last 6 bytes
    };

    int advertising_interval = 30;  // Advertise for 30 seconds
    int major = 1;
    int minor = 1;
    int8_t tx_power = -59;

    auto beacon_start_time = std::chrono::system_clock::now();
    if (bluetooth->StartBeaconAdvertising(advertising_interval, beacon_uuid, major, minor, tx_power)) {
        std::cout << DbusBTDevice::GetTimestamp() << " [TEST] Beacon advertising started successfully for "
                  << advertising_interval << " seconds." << std::endl;
        auto beacon_end_time = std::chrono::system_clock::now();
        auto beacon_duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(beacon_end_time - beacon_start_time);
        std::cout << DbusBTDevice::GetTimestamp() << " StartBeaconAdvertising took " << std::dec
                  << beacon_duration.count() << " ms" << std::endl;
    } else {
        std::cout << DbusBTDevice::GetTimestamp() << " [TEST] Failed to start beacon advertising." << std::endl;
    }

    // Start Service Advertising
    // Test Service Advertising with new signature
    std::string service_name = "TestService";
    std::vector<uint8_t> service_uuid = {
        0x00, 0x00, 0x18,
        0x00,  // First 4 bytes
        0x00,
        0x00,  // Next 2 bytes
        0x10,
        0x00,  // Next 2 bytes
        0x80,
        0x00,  // Next 2 bytes
        0x00, 0x80, 0x5F, 0x9B, 0x34,
        0xFB  // Last 6 bytes
    };
    std::string service_data_str = "Test Data";
    uint32_t service_duration = 45;  // Advertise for 45 seconds

    auto service_start_time = std::chrono::system_clock::now();
    if (bluetooth->StartServiceAdvertising(service_name, service_uuid, service_data_str, service_duration)) {
        auto service_end_time = std::chrono::system_clock::now();
        auto service_duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(service_end_time - service_start_time);
        std::cout << "service_duration: " << service_duration.count() << std::endl;
        std::cout << DbusBTDevice::GetTimestamp() << " [TEST] Service advertising started successfully for "
                  << service_duration.count() << " ms" << std::endl;
    } else {
        std::cout << DbusBTDevice::GetTimestamp() << " [TEST] Failed to start service advertising." << std::endl;
    }
    std::vector<std::string> data = {arg3};
    // bluetooth -> WriteCharacteristicData(arg1, arg2, data);

    // std::vector < uint8_t > read_value;
    // std::thread(periodic_read, bluetooth, arg1, arg2).detach();
    // if (bluetooth -> ReadCharacteristicData(arg1, arg2, read_value)) {
    //     std::cout << DbusBTDevice::GetTimestamp() << " [TEST] ReadCharacteristicData succeeded. Value: ";
    //     for (auto b: read_value) std::cout << std::hex << std::setw(2) << std::setfill('0') << (int) b << " ";
    //     std::cout << std::dec << std::endl;
    // } else {
    //     std::cout << DbusBTDevice::GetTimestamp() << " [TEST] ReadCharacteristicData failed." << std::endl;
    // }

    // Add this test code in main() after your existing code
    // Test multiple characteristic reads
    // std::vector < std::string > uuids_to_read = {
    //     "00002a8a-0000-1000-8000-00805f9b34fb" // Your first UUID
    //     // "00002a8b-0000-1000-8000-00805f9b34fb"   // Another UUID to read
    // };

    // std::unordered_map < std::string, std::vector < uint8_t >> characteristic_data;

    // if (bluetooth -> ReadCharacteristicData(arg1, uuids_to_read, characteristic_data)) {
    //     std::cout << DbusBTDevice::GetTimestamp() << " [TEST] Multiple characteristics read successfully." <<
    //     std::endl;

    //     // Print all read characteristics
    //     for (const auto & [uuid, data]: characteristic_data) {
    //         std::cout << DbusBTDevice::GetTimestamp() << " [TEST] UUID: " << uuid << " Data (" <<
    //             data.size() << " bytes): ";
    //         for (const auto & byte: data) {
    //             std::cout << std::hex << std::setw(2) << std::setfill('0') <<
    //                 static_cast < int > (byte) << " ";
    //         }
    //         std::cout << std::dec << std::endl;
    //     }
    // } else {
    //     std::cout << DbusBTDevice::GetTimestamp() << " [TEST] Failed to read multiple characteristics." << std::endl;
    // }

    // Test multiple MAC addresses and their characteristics
    std::unordered_map<std::string, std::vector<std::string>> mac_to_uuids = {
        {arg1,
         {
             // First device // 4D:02:86:64:FF:6F -> Phone
             "00002a8a-0000-1000-8000-00805f9b34fb",  // First UUID
             "00002a90-0000-1000-8000-00805f9b34fb"   // Second UUID
         }},
        // {
        //     "6D:2B:95:DC:A9:6B",
        //     { // First device // 4D:02:86:64:FF:6F -> Phone
        //         "00002a8a-0000-1000-8000-00805f9b34fb", // First UUID
        //         "00002a90-0000-1000-8000-00805f9b34fb" // Second UUID
        //     }
        // }
    };

    // Output map to store results
    std::unordered_map<std::string, std::unordered_map<std::string, std::vector<uint8_t>>> characteristic_data;

    // Call the function
    if (bluetooth->ReadCharacteristicData(mac_to_uuids, characteristic_data)) {
        std::cout << DbusBTDevice::GetTimestamp() << " [TEST] Multiple devices characteristics read successfully."
                  << std::endl;

        // Print all read characteristics for each device
        for (const auto &[mac_addr, uuid_data] : characteristic_data) {
            std::cout << DbusBTDevice::GetTimestamp() << " [TEST] Device: " << mac_addr << std::endl;

            for (const auto &[uuid, data] : uuid_data) {
                std::cout << DbusBTDevice::GetTimestamp() << " [TEST] UUID: " << uuid << " Data (" << data.size()
                          << " bytes): ";
                for (const auto &byte : data) {
                    std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte) << " ";
                }
                std::cout << std::dec << std::endl;
            }
        }
    } else {
        std::cout << DbusBTDevice::GetTimestamp() << " [TEST] Failed to read characteristics from multiple devices."
                  << std::endl;
    }

    // bluetooth -> PrintListeners();

    // Simulate a delay to allow advertisements to run
    std::this_thread::sleep_for(std::chrono::seconds(1000));

    // Stop Beacon Advertising
    if (bluetooth->StopBeaconAdvertising()) {
        std::cout << DbusBTDevice::GetTimestamp() << " [TEST] Beacon advertising stopped successfully." << std::endl;
    } else {
        std::cout << DbusBTDevice::GetTimestamp() << " [TEST] Failed to stop beacon advertising." << std::endl;
    }

    // Stop Service Advertising
    if (bluetooth->StopServiceAdvertising()) {
        std::cout << DbusBTDevice::GetTimestamp() << " [TEST] Service advertising stopped successfully." << std::endl;
    } else {
        std::cout << DbusBTDevice::GetTimestamp() << " [TEST] Failed to stop service advertising." << std::endl;
    }

    bluetooth->StopDiscovery();
    delete bluetooth;  // Clean up the DbusBTDevice object
    return 0;
}
#endif

}  // namespace device

}  // namespace nd
