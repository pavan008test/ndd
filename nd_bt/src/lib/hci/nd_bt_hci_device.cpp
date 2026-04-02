/* Copyright (C) 2022 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, October 2022
 */
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include <gattlib/gattlib.h>

#include <jansson/jansson.h>

#include <nd_bt_hci_device.h>
#include <nd_utils.h>

#include <nd_ext_cam_utils.h>
#include <nd_file_utils.h>

#include <config_parser.h>
#include <log.h>
#include <service_utils.h>
#include <system_utils.h>
#include <nd_net_utils.h>

namespace nd {

namespace device {

static constexpr char kBagheeraConfigFile[]= "/home/ubuntu/.nddevice/latest/bagheera_config.ini";
static constexpr char kLogTag[]           = "HCI";
static constexpr char kRadioFreqBin[]     = "/usr/sbin/rfkill";
static constexpr char kHciConfig[]        = "/bin/hciconfig";
static constexpr char kBluetoothCtlPath[] = "/home/ubuntu/.nddevice/latest/service/nd_bt/bluetoothctl";
static constexpr char kBtFwFlashed[]      = "/dev/shm/bt_firmware.done";

enum BleFieldTypes : uint8_t {
    kFlags                 = 0x01,
    kService16bit          = 0x02, // Incomplete List of 16-bit Service Class UUID
    kService16bitComplete  = 0x03, // Complete List of 16-bit Service Class UUIDs
    kService32bit          = 0x04, // Incomplete List of 32-bit Service Class UUIDs
    kService32bitComplete  = 0x05, // Complete List of 32-bit Service Class UUIDs
    kService128bit         = 0x06, // Incomplete List of 128-bit Services
    kService128bitComplete = 0x07, // Complete List of 128-bit Services
    kNameShort             = 0x08,
    kNameComplete          = 0x09,
    kTxLevel               = 0x0A, // Tx Power Level
    kServiceData           = 0x16,
    kManufacturerSpecific  = 0xFF,
};

HciDevice::HciDevice() {

    Config_parser bag_conf(kBagheeraConfigFile);

    bool is_val_overridden = false;
    {
        if (bag_conf.getParseStatus()) {
            std::string scan = bag_conf.getConfig("nd_bt", "scan", "1", true, is_val_overridden);
            std::string interval = bag_conf.getConfig("nd_bt", "interval", "18", true, is_val_overridden);
            std::string window = bag_conf.getConfig("nd_bt", "window", "18", true, is_val_overridden);
            std::string addr = bag_conf.getConfig("nd_bt", "addr", "0", true, is_val_overridden);

            // Detect WiFi module type and adjust scan parameters accordingly
            WifiModuleVendorType wifi_module = getWifiModuleVendorType();
            if (wifi_module == WIFI_MODULE_REALTEK) {
                LOG_I(kLogTag, "Realtek WiFi module detected, using scan interval/window: 160ms");
                scan_interval_ = 256; // 256 * 0.625ms = 160ms
                scan_window_ = 256; // 256 * 0.625ms = 160ms
                scan = "1"; // Enable active scanning
            } else {
                LOG_I(kLogTag, "Non-Realtek WiFi module detected, using config values - interval: %s, window: %s", interval.c_str(), window.c_str());
                scan_interval_ = std::stoi(interval);
                scan_window_ = std::stoi(window);
            }

#ifdef DO_FIRMWARE_FLASH
            std::string fw_toggle = bag_conf.getConfig("nd_bt", "toggle_fw_flash", "0", true, is_val_overridden);
            if (1 == std::stoi(fw_toggle)) {
                toggle_firmware_flash_ = true;
            }
#endif
            if ("true" == bag_conf.getConfig("vehicle_data", "iosix_enabled", "false", true, is_val_overridden)) {
                will_wifi_configure_in_ap_mode_ = true;
            }

            if ("true" == bag_conf.getConfig("driverlogin", "driveri_app_login", "false", true, is_val_overridden)) {
                is_seamless_char_ops_required_ = true;
            }

            if (0 == std::stoi(scan)) {
                scan_type_ = 0;
            }

            if (1 == std::stoi(addr)) {
                address_type_ = LE_RANDOM_ADDRESS;
            }
        }
    }

    will_wifi_configure_in_ap_mode_ |= is_ext_cam_feature_enabled();

    LOG_I(kLogTag, "scan_type_: %d, scan_interval_: %d, scan_window_: %d, "
                    "address_type_: %d, toggle_firmware_flash_: %d, is_seamless_char_ops_required_: %d",
                    scan_type_, scan_interval_, scan_window_, address_type_, toggle_firmware_flash_, is_seamless_char_ops_required_);

#ifdef DO_FIRMWARE_FLASH
    if (is_seamless_char_ops_required_) {
        LOG_I(kLogTag, "Seamless char ops required, firmware will not be erased");
        toggle_firmware_flash_ = false;
    } else {
        if (toggle_firmware_flash_) {
            LOG_C(kLogTag, "Seamless char ops not required, firmware will be erased, legacy login will not work");
            TearDownGattSetup();

            constexpr char kCommandTag[] = "HCI_ATTACH";

            const std::string kHciAttachCmds[2] = {
                "pkill -9 hciattach",
                "timeout 1 hciattach /dev/ttyTHS3 any 115200 flow"
            };

            for(const auto &cmd : kHciAttachCmds) {
                if (0 != system_execute(kCommandTag, cmd)) {
                    LOG_E(kLogTag, "HCI_ATTACH failed cmd: %s", cmd);
                }
            }
        }
    }
#endif
}

bool HciDevice::DoGattSetup() {

    bool fw_flash_failed = false;
#ifdef DO_FIRMWARE_FLASH
    const std::lock_guard<std::mutex> lock(api_mutex_);

    if (toggle_firmware_flash_) {
        if (0 > get_file_size(kBtFwFlashed)) {

            LOG_I(kLogTag, "Inside %s", __func__);

            constexpr char kCommandTag[] = "FW_FLASH";

            const std::string kFlashCmds[3] = {
                "pkill -9 hciattach",
                "timeout 30 /bin/vendor/cypress_brcm -d --baudrate 115200 --use_baudrate_for_download --patchram /lib/firmware/cypress/CYW4354_003.001.012.0433.1422.hcd --no2bytes /dev/ttyTHS3",
                "timeout 1 hciattach /dev/ttyTHS3 any 115200 flow"
            };

            // if (0 != system_execute(kCommandTag, kFlashCmds[0])) {
            //     LOG_E(kLogTag, "FW flash failed cmd: %s", kFlashCmds[0]);
            //     fw_flash_failed = true;
            //     // break;
            // }

            // if (!nd::utils::CommandExecutors::Execute("/usr/bin/timeout",
            //                                          {"30", "/bin/vendor/cypress_brcm", "-d", "--baudrate", "115200",
            //                                           "--use_baudrate_for_download", "--patchram",
            //                                           "/lib/firmware/cypress/CYW4354_003.001.012.0433.1422.hcd",
            //                                           "--no2bytes", "/dev/ttyTHS3" },
            //                                           35)) {
            //     LOG_E(kLogTag, "[%s:%d] ERROR: Execute failed for command: %s", __FUNCTION__, __LINE__, "timeout");
            // }

            // // std::this_thread::sleep_for(std::chrono::milliseconds(500));

            // if (!nd::utils::CommandExecutors::Execute("/usr/bin/timeout",
            //                                           {"1", "hciattach", "/dev/ttyTHS3",
            //                                            "any", "115200", "flow"},
            //                                            5)) {
            //     LOG_E(kLogTag, "[%s:%d] ERROR: Execute failed for command: %s", __FUNCTION__, __LINE__, "nohup");
            // }

            for(const auto &cmd : kFlashCmds) {
                if (0 != system_execute(kCommandTag, cmd)) {
                    LOG_E(kLogTag, "FW flash failed cmd: %s", cmd);
                    fw_flash_failed = true;
                    // break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }

            if (!file_touch(kBtFwFlashed)) {
                LOG_E(kLogTag, "Failed to create file: %s", kBtFwFlashed);
            }

        } else {
            LOG_I(kLogTag, "Firmware already flashed");
        }
    } else {
        LOG_I(kLogTag, "Firmware flash / erase not enabled");
    }
#endif
    return !fw_flash_failed;
}

bool HciDevice::TearDownGattSetup() {

    bool fw_erase_failed = false;
#ifdef DO_FIRMWARE_FLASH
    const std::lock_guard<std::mutex> lock(api_mutex_);

    if (toggle_firmware_flash_) {
        if (0 <= get_file_size(kBtFwFlashed)) {
            LOG_I(kLogTag, "Inside %s", __func__);

            constexpr char kCommandTag[] = "FW_ERASE";

            const std::string kEraseCmds[2] = {
                "gpio_test -n 396 -o 0",
                "gpio_test -n 396 -o 1"
            };

            for(const auto &cmd : kEraseCmds) {
                if (0 != system_execute(kCommandTag, cmd)) {
                    LOG_E(kLogTag, "FW erase failed cmd: %s", cmd);
                    fw_erase_failed = true;
                    // break;
                }
            }

            if (0 != remove(kBtFwFlashed)) {
                LOG_E(kLogTag, "Failed to remove file: %s, errno: %d, errstr: %s", kBtFwFlashed, errno, strerror(errno));
            }

            LOG_I(kLogTag, "Firmware erased");

        } else {
            LOG_I(kLogTag, "Firmware already erased");
        }
    } else {
        LOG_I(kLogTag, "Firmware flash / erase not enabled");
    }
#endif
    return !fw_erase_failed;
}

bool HciDevice::Disable() {
    const std::lock_guard<std::mutex> lock(api_mutex_);
    bool status = false;

    do {
        constexpr uint32_t kTimeoutSecs = 5;

        if (!nd::utils::CommandExecutors::Execute(kBluetoothCtlPath, {"power", "off"}, kTimeoutSecs)) {
            LOG_E(kLogTag, "[%s:%d] ERROR: Execute failed for command: %s", __FUNCTION__, __LINE__, kBluetoothCtlPath);
        }

        if (!nd::utils::CommandExecutors::Execute(kRadioFreqBin, {"block", "bluetooth"}, kTimeoutSecs)) {
            LOG_E(kLogTag, "[%s:%d] ERROR: Execute failed for command: %s", __FUNCTION__, __LINE__, kRadioFreqBin);
            break;
        }

        status = true;
    } while (false);

    if (status) {
        LOG_I(kLogTag, "BT disabled successfully");
        is_bt_enabled_ = false;
    } else {
        LOG_E(kLogTag, "BT disable failed");
    }
    return status;
}

bool HciDevice::Enable() {
    const std::lock_guard<std::mutex> lock(api_mutex_);

    bool status = false;

    do {
        constexpr uint32_t kTimeoutSecs = 5;

        if (!nd::utils::CommandExecutors::Execute(kRadioFreqBin, {"unblock", "bluetooth"}, kTimeoutSecs)) {
            LOG_E(kLogTag, "[%s:%d] ERROR: Execute failed for command: %s", __FUNCTION__, __LINE__, kRadioFreqBin);
            break;
        }

        if (!nd::utils::CommandExecutors::Execute(kBluetoothCtlPath, {"power", "on"}, kTimeoutSecs * 2)) {
            LOG_E(kLogTag, "[%s:%d] ERROR: Execute failed for command: %s", __FUNCTION__, __LINE__, kBluetoothCtlPath);
            break;
        }

        status = true;
    } while (false);

    if (status) {
        LOG_I(kLogTag, "BT enabled successfully");
        is_bt_enabled_ = true;
    } else {
        LOG_E(kLogTag, "BT enable failed");
    }
    return status;
}

bool HciDevice::IsEnabled() {
    const std::lock_guard<std::mutex> lock(api_mutex_);
    return is_bt_enabled_;
}

bool HciDevice::ReadCharacteristicData(const std::string& mac_addr, const std::string &uuid_str, std::vector<uint8_t> &result_out, void *caller_arg) {
    const std::lock_guard<std::mutex> lock(api_mutex_);
    bool status = false;

    LOG_I(kLogTag, "Reading characteristic for mac: %s and uuid: %s", mac_addr.c_str(), uuid_str.c_str());

    NDService * service_obj_ptr = nullptr;

    if (nullptr != caller_arg) {
        service_obj_ptr = static_cast<NDService *>(caller_arg);
    }

    auto read_fn = [&mac_addr, &uuid_str, &service_obj_ptr]() -> std::string {

        if (nullptr != service_obj_ptr) {
            const bool raise_critical_alert = false;
            service_obj_ptr->raise_alert_on_crash(raise_critical_alert);
        }

        std::string result;

        gatt_connection_t *gatt_connection = gattlib_connect(NULL, mac_addr.c_str(),
                                                            GATTLIB_CONNECTION_OPTIONS_LEGACY_BDADDR_LE_RANDOM |
                                                            GATTLIB_CONNECTION_OPTIONS_LEGACY_BT_SEC_LOW |
                                                            GATTLIB_CONNECTION_OPTIONS_LEGACY_PSM(0));

        do {
            if (nullptr == gatt_connection) {
                LOG_E(kLogTag, "Connection to bt device: %s failed", mac_addr.c_str());
                break;
            }

            LOG_I(kLogTag, "Connection to bt device: %s success", mac_addr.c_str());

            size_t read_char_uuid_len = uuid_str.size();
            uuid_t uuid{};

            if (GATTLIB_SUCCESS != gattlib_string_to_uuid(uuid_str.c_str(), read_char_uuid_len + 1, &uuid)) {
                LOG_E(kLogTag, "Conversion to uuid failed");
                break;
            }

            uint8_t *buffer = nullptr;
            size_t buffer_len = 0;

            if ((GATTLIB_SUCCESS == gattlib_read_char_by_uuid(gatt_connection, &uuid, (void **)&buffer, &buffer_len)) &&
                (nullptr != buffer)) {
                // serialize data
                result = R"({"data":")" + nd::utils::Translator::BytesToHexString(buffer, buffer_len) + R"("})";

                LOG_I(kLogTag, "Read characteristic success with buffer length: %lu and result: %s",
                      buffer_len, result.c_str());

                // LOG_I(kLogTag, "value: ");
                // for (int i = 0; i < buffer_len; i++) {
                // 	printf("%02x ", buffer[i]);
                // }
                // printf("\n");

                free(buffer);
            } else {
                LOG_E(kLogTag, "gattlib_read_char_by_uuid failed");
                break;
            }

        } while (false);

        if (nullptr != gatt_connection) {
            gattlib_disconnect(gatt_connection);
        }

        return result;
    };

    std::string read_data;
    constexpr uint32_t kTimeoutSecs = 10;

    if (nd::utils::CommandExecutors::Execute(std::move(read_fn), read_data, kTimeoutSecs)) {

        json_error_t error;
        json_t* root = json_loads(read_data.c_str(), 0, &error);

        if (nullptr != root) {
            const auto status_json = json_object_get(root, "data");

            if (nullptr != status_json) {
                const auto status_str = json_string_value(status_json);
                if (nullptr != status_str) {
                    result_out = std::move(nd::utils::Translator::HexStringToBytes(status_str));
                    status = true;
                }
            }
        }
    } else {
        LOG_E(kLogTag, "Read CommandExecutors::Execute failed");
    }

    return status;
}

bool HciDevice::ReadCharacteristicData(const std::string &mac_addr, std::unordered_map<std::string, std::vector<uint8_t>> &charac_data_out, void *caller_arg) {
    const std::lock_guard<std::mutex> lock(api_mutex_);
    bool status = false;

    LOG_I(kLogTag, "Reading multiple characteristics from mac: %s", mac_addr.c_str());

    NDService * service_obj_ptr = nullptr;

    if (nullptr != caller_arg) {
        service_obj_ptr = static_cast<NDService *>(caller_arg);
    }

    auto read_fn = [&mac_addr, &charac_data_out, &service_obj_ptr]() -> std::string {

        if (nullptr != service_obj_ptr) {
            const bool raise_critical_alert = false;
            service_obj_ptr->raise_alert_on_crash(raise_critical_alert);
        }

        std::string result;

        gatt_connection_t *gatt_connection = gattlib_connect(NULL, mac_addr.c_str(),
                                                            GATTLIB_CONNECTION_OPTIONS_LEGACY_BDADDR_LE_RANDOM |
                                                            GATTLIB_CONNECTION_OPTIONS_LEGACY_BT_SEC_LOW |
                                                            GATTLIB_CONNECTION_OPTIONS_LEGACY_PSM(0));

        do {
            if (nullptr == gatt_connection) {
                LOG_E(kLogTag, "Connection to bt device: %s failed", mac_addr.c_str());
                break;
            }

            LOG_I(kLogTag, "Connection to bt device: %s success", mac_addr.c_str());

            result = R"({)";

            for (const auto & uuid_data : charac_data_out) {
                size_t read_char_uuid_len = uuid_data.first.size();
                uuid_t uuid{};

                if (GATTLIB_SUCCESS != gattlib_string_to_uuid(uuid_data.first.c_str(), read_char_uuid_len + 1, &uuid)) {
                    LOG_E(kLogTag, "Conversion to uuid failed for : %s", uuid_data.first.c_str());
                    continue;
                }

                uint8_t *buffer = nullptr;
                size_t buffer_len = 0;

                if ((GATTLIB_SUCCESS == gattlib_read_char_by_uuid(gatt_connection, &uuid, (void **)&buffer, &buffer_len)) &&
                    (nullptr != buffer)) {
                    // serialize data
                    const auto read_data =  nd::utils::Translator::BytesToHexString(buffer, buffer_len);
                    result += R"(")" + uuid_data.first + R"(":")" + read_data + R"(")";
                    result += R"(,)";
                    LOG_I(kLogTag, "Read characteristic success for: %s with buffer length: %lu and read_data: %s",
                          uuid_data.first.c_str(), buffer_len, read_data.c_str());

                    // LOG_I(kLogTag, "value: ");
                    // for (int i = 0; i < buffer_len; i++) {
                    // 	printf("%02x ", buffer[i]);
                    // }
                    // printf("\n");

                    free(buffer);
                } else {
                    LOG_E(kLogTag, "gattlib_read_char_by_uuid failed");
                    continue;
                }
            }

            if (',' == result.back()) {
                result.pop_back();
            }

            result += R"(})";

            LOG_I(kLogTag, "Read result: %s", result.c_str());

        } while (false);

        if (nullptr != gatt_connection) {
            gattlib_disconnect(gatt_connection);
        }

        return result;
    };

    std::string read_data;
    constexpr uint32_t kTimeoutSecs = 10;

    if (nd::utils::CommandExecutors::Execute(std::move(read_fn), read_data, kTimeoutSecs)) {

        json_error_t error;
        json_t* root = json_loads(read_data.c_str(), 0, &error);

        if (nullptr != root) {
            bool data_error = false;
            for (auto & uuid_data : charac_data_out) {
                const auto status_json = json_object_get(root, uuid_data.first.c_str());

                if (nullptr != status_json) {
                    const auto status_str = json_string_value(status_json);
                    if (nullptr != status_str) {
                        uuid_data.second = std::move(nd::utils::Translator::HexStringToBytes(status_str));
                    }
                } else {
                    LOG_E(kLogTag, "Read CommandExecutors::Execute failed for uuid: %s", uuid_data.first.c_str());
                    data_error = true;
                }
            }
            status = !data_error;
        } else {
            LOG_E(kLogTag, "\"%s\" Parse failed: line %d, column %d: %s",
                  read_data.c_str(), error.line, error.column, error.text);
        }
    } else {
        LOG_E(kLogTag, "Read CommandExecutors::Execute failed");
    }

    return status;
}

bool HciDevice::WriteCharacteristicData(const std::string& mac_addr, const std::string &uuid_str,
                                        const std::vector<std::string> &data, nd::interface::AddressType addr_type, void *caller_arg) {

    const std::lock_guard<std::mutex> lock(api_mutex_);
    bool status = false;
    LOG_I(kLogTag, "Writing characteristic for mac: %s and uuid: %s", mac_addr.c_str(), uuid_str.c_str());

    do {

        if (data.empty()) {
            LOG_E(kLogTag, "Nothing to write");
            break;
        }

        NDService * service_obj_ptr = nullptr;

        if (nullptr != caller_arg) {
            service_obj_ptr = static_cast<NDService *>(caller_arg);
        }

        auto write_fn = [&mac_addr, &uuid_str, &data, &addr_type, &service_obj_ptr]() -> std::string {
            bool write_status = false;

            if (nullptr != service_obj_ptr) {
                const bool raise_critical_alert = false;
                service_obj_ptr->raise_alert_on_crash(raise_critical_alert);
            }

            gatt_connection_t *gatt_connection = nullptr;

            do {

                unsigned long options = 0;

                switch (addr_type) {
                    case nd::interface::AddressType::kPublic : {
                        options = GATTLIB_CONNECTION_OPTIONS_LEGACY_BDADDR_LE_PUBLIC |
                                  GATTLIB_CONNECTION_OPTIONS_LEGACY_BT_SEC_LOW |
                                  GATTLIB_CONNECTION_OPTIONS_LEGACY_PSM(0);
                        break;
                    }

                    case nd::interface::AddressType::kRandom : {
                        options = GATTLIB_CONNECTION_OPTIONS_LEGACY_BDADDR_LE_RANDOM |
                                  GATTLIB_CONNECTION_OPTIONS_LEGACY_BT_SEC_LOW |
                                  GATTLIB_CONNECTION_OPTIONS_LEGACY_PSM(0);
                        break;
                    }

                    case nd::interface::AddressType::kAny : {
                        options = GATTLIB_CONNECTION_OPTIONS_LEGACY_DEFAULT;
                        break;
                    }

                    default: {
                        options = GATTLIB_CONNECTION_OPTIONS_LEGACY_DEFAULT;
                        break;
                    }
                }

                gatt_connection = gattlib_connect(NULL, mac_addr.c_str(), options);

                if (nullptr == gatt_connection) {
                    LOG_E(kLogTag, "Connection to bt device: %s failed with options: %lu", mac_addr.c_str(), options);
                    break;
                }

                LOG_I(kLogTag, "Connection to bt device: %s success", mac_addr.c_str());

                size_t write_char_uuid_len = uuid_str.size();
                uuid_t uuid{};

                if (GATTLIB_SUCCESS != gattlib_string_to_uuid(uuid_str.c_str(), write_char_uuid_len + 1, &uuid)) {
                    LOG_E(kLogTag, "Conversion to uuid failed");
                    break;
                }

                LOG_I(kLogTag, "UUID converted successfully");

                bool data_write_fail = false;

                for (const auto &data_element : data) {
                    LOG_I(kLogTag, "Attempting to write: %s", data_element.c_str());
                    const auto ret = gattlib_write_char_by_uuid(gatt_connection, &uuid, data_element.c_str(), data_element.size());
                    if (GATTLIB_SUCCESS != ret) {
                        LOG_E(kLogTag, "Write data failed: %d", ret);
                        data_write_fail = true;
                        break;
                    }
                }

                if (data_write_fail) {
                    break;
                }

                LOG_I(kLogTag, "Write char success");
                write_status = true;

            } while (false);

            if (nullptr != gatt_connection) {
                gattlib_disconnect(gatt_connection);
            }

            // serialize data
            const std::string result = R"({"status":)" + std::string((write_status ? "true" : "false")) + R"(})";

            LOG_I(kLogTag, "exit %s", __func__);

            return result;
        };

        std::string write_status;
        constexpr uint32_t kTimeoutSecs = 15;

        if (!nd::utils::CommandExecutors::Execute(std::move(write_fn), write_status, kTimeoutSecs)) {
            LOG_E(kLogTag, "Write CommandExecutors::Execute failed");
            break;
        }

        json_error_t error;
        json_t* root = json_loads(write_status.c_str(), 0, &error);

        if (nullptr != root) {
            const auto status_json = json_object_get(root, "status");
            if (nullptr != status_json) {
                status = json_is_true(status_json);
            }
        }

    } while (false);

    return status;
}

bool HciDevice::StartDiscovery() {
    const std::lock_guard<std::mutex> lock(api_mutex_);
    LOG_I(kLogTag, "%s not supported yet", __FUNCTION__);
    return false;
}

bool HciDevice::StopDiscovery() {
    const std::lock_guard<std::mutex> lock(api_mutex_);
    LOG_I(kLogTag, "%s not supported yet", __FUNCTION__);
    return false;
}

bool HciDevice::SetScanParameters(int scan_interval, int scan_window) {
    bool status = false;

    if ((0 < scan_interval) && (0 < scan_window) && (scan_window <= scan_interval)) {
        scan_interval_ = scan_interval;
        scan_window_ = scan_window;
        LOG_I(kLogTag, "New scan parameters: scan_interval: %d, scan_window: %d", scan_interval, scan_window);
        status = true;
    } else {
        LOG_E(kLogTag, "Invalid scan parameters: scan_interval: %d, scan_window: %d", scan_interval, scan_window);
    }

    return status;
}

bool HciDevice::StartBeaconAdvertising(int advertising_interval, const std::vector<uint8_t> &advertising_uuid, int major_number,
                                 int minor_number, int8_t rssi_value) {
    // Call stop advertising before start
    StopBeaconAdvertising();

    LOG_I(kLogTag, "Inside %s", __func__);

    bool status = false;
    int device_handle = -1;

    const std::lock_guard<std::mutex> lock(api_mutex_);

    do {
        const auto device_id = hci_get_route(NULL);

        if (device_id < 0) {
            LOG_E(kLogTag, "[%s:%d] ERROR: Invalid device: %s", __FUNCTION__, __LINE__, strerror(errno));
            break;
        }

        // Setup device
        device_handle = hci_open_dev(device_id);

        if (0 > device_handle) {
            LOG_E(kLogTag, "[%s:%d] Could not open device: %s", __FUNCTION__, __LINE__, strerror(errno));
            break;
        }

        // advertising parameters

        le_set_advertising_parameters_cp adv_params_cp{};
        adv_params_cp.min_interval = htobs(advertising_interval);
        adv_params_cp.max_interval = htobs(advertising_interval);
        adv_params_cp.advtype = 0x03; // non-connectable undirected advertising
        adv_params_cp.chan_map = 0x07;// all 3 channels

        {
            uint8_t status_out = 0;
            hci_request req{};
            req.ogf = OGF_LE_CTL;
            req.ocf = OCF_LE_SET_ADVERTISING_PARAMETERS;
            req.cparam = &adv_params_cp;
            req.clen = LE_SET_ADVERTISING_PARAMETERS_CP_SIZE;
            req.rparam = &status_out;
            req.rlen = 1;

            const int ret = hci_send_req(device_handle, &req, 1000);
            if (0 > ret) {
                LOG_E(kLogTag, "[%s:%d] Can't send request %s (%d)", __FUNCTION__, __LINE__, strerror(errno), errno);
                break;
            }
            if (0 != status_out) {
                LOG_E(kLogTag, "[%s:%d] LE set advertise params returned status: %d", __FUNCTION__, __LINE__, status_out);
                break;
            }
        }

        // advertising data

        le_set_advertising_data_cp adv_data_cp{};

        uint8_t segment_length = 1;
        adv_data_cp.data[adv_data_cp.length + segment_length++] = htobs(BleFieldTypes::kFlags);
        adv_data_cp.data[adv_data_cp.length + segment_length++] = htobs(0x1A);
        adv_data_cp.data[adv_data_cp.length] = htobs(segment_length - 1);
        adv_data_cp.length += segment_length;

        segment_length = 1;
        adv_data_cp.data[adv_data_cp.length + segment_length++] = htobs(BleFieldTypes::kManufacturerSpecific);
        adv_data_cp.data[adv_data_cp.length + segment_length++] = htobs(0x4C);
        adv_data_cp.data[adv_data_cp.length + segment_length++] = htobs(0x00);
        adv_data_cp.data[adv_data_cp.length + segment_length++] = htobs(0x02);
        adv_data_cp.data[adv_data_cp.length + segment_length++] = htobs(0x15);

        for (size_t pos = 0; pos < advertising_uuid.size(); ++pos) {
            adv_data_cp.data[adv_data_cp.length + segment_length++] = htobs(advertising_uuid.at(pos));
        }

        // Major number
        adv_data_cp.data[adv_data_cp.length + segment_length++] = htobs(major_number >> 8 & 0x00FF);
        adv_data_cp.data[adv_data_cp.length + segment_length++] = htobs(major_number & 0x00FF);

        // Minor number
        adv_data_cp.data[adv_data_cp.length + segment_length++] = htobs(minor_number >> 8 & 0x00FF);
        adv_data_cp.data[adv_data_cp.length + segment_length++] = htobs(minor_number & 0x00FF);

        // RSSI calibration
        adv_data_cp.data[adv_data_cp.length + segment_length++] = htobs(static_cast<uint8_t>(rssi_value));

        adv_data_cp.data[adv_data_cp.length] = htobs(segment_length - 1);

        adv_data_cp.length += segment_length;

        {
            uint8_t status_out = 0;
            hci_request req{};
            req.ogf = OGF_LE_CTL;
            req.ocf = OCF_LE_SET_ADVERTISING_DATA;
            req.cparam = &adv_data_cp;
            req.clen = LE_SET_ADVERTISING_DATA_CP_SIZE;
            req.rparam = &status_out;
            req.rlen = 1;

            const int ret = hci_send_req(device_handle, &req, 1000);
            if (0 > ret) {
                LOG_E(kLogTag, "[%s:%d] Can't send request %s (%d)", __FUNCTION__, __LINE__, strerror(errno), errno);
                break;
            }
            if (0 != status_out) {
                LOG_E(kLogTag, "[%s:%d] LE set advertise data returned status: %d", __FUNCTION__, __LINE__, status_out);
                break;
            }
        }

        le_set_advertise_enable_cp advertise_cp{};
        advertise_cp.enable = 0x01;

        {
            hci_request req{};
            uint8_t status_out = 0;
            req.ogf = OGF_LE_CTL;
            req.ocf = OCF_LE_SET_ADVERTISE_ENABLE;
            req.cparam = &advertise_cp;
            req.clen = LE_SET_ADVERTISE_ENABLE_CP_SIZE;
            req.rparam = &status_out;
            req.rlen = 1;

            const int ret = hci_send_req(device_handle, &req, 1000);
            if (0 > ret) {
                LOG_E(kLogTag, "[%s:%d] Can't send request %s (%d)", __FUNCTION__, __LINE__, strerror(errno), errno);
                break;
            }
            if (0 != status_out) {
                LOG_E(kLogTag, "[%s:%d] LE set advertise enable returned status: %d", __FUNCTION__, __LINE__, status_out);
                break;
            }
        }
        status = true;
    } while (false);

    if (-1 != device_handle) {
        hci_close_dev(device_handle);
    }

    return status;
}

bool HciDevice::StopBeaconAdvertising() {

    LOG_I(kLogTag, "Inside %s", __func__);

    bool status = false;
    int device_handle = -1;

    const std::lock_guard<std::mutex> lock(api_mutex_);

    do {
        const auto device_id = hci_get_route(NULL);

        if (device_id < 0) {
            LOG_E(kLogTag, "[%s:%d] ERROR: Invalid device: %s", __FUNCTION__, __LINE__, strerror(errno));
            break;
        }

        // Setup device
        device_handle = hci_open_dev(device_id);

        if (0 > device_handle) {
            LOG_E(kLogTag, "[%s:%d] Could not open device: %s", __FUNCTION__, __LINE__, strerror(errno));
            break;
        }

        le_set_advertise_enable_cp advertise_cp{};

        hci_request req{};
        uint8_t status_out = 0;
        req.ogf = OGF_LE_CTL;
        req.ocf = OCF_LE_SET_ADVERTISE_ENABLE;
        req.cparam = &advertise_cp;
        req.clen = LE_SET_ADVERTISE_ENABLE_CP_SIZE;
        req.rparam = &status_out;
        req.rlen = 1;

        const int ret = hci_send_req(device_handle, &req, 1000);
        if (0 > ret) {
            LOG_E(kLogTag, "[%s:%d] Can't send request %s (%d)", __FUNCTION__, __LINE__, strerror(errno), errno);
            break;
        }
        if (0 != status_out) {
            LOG_I(kLogTag, "[%s:%d] LE set advertise disable returned status: %d", __FUNCTION__, __LINE__, status_out);
            break;
        }

        status = true;
    } while (false);

    if (-1 != device_handle) {
        hci_close_dev(device_handle);
    }

    return status;
}

bool HciDevice::RegisterLeEventCallback(nd::interface::LeEventCb cb) {
    const std::lock_guard<std::mutex> lock(api_mutex_);
    le_event_cb_ = std::move(cb);
    return true;
}

bool HciDevice::LeScanOn() {
    constexpr uint32_t kScanWaitTimeout = 10;
    bool status = false;
    const std::lock_guard<std::mutex> lock(api_mutex_);
    if (!stop_scan_) {
        LOG_I(kLogTag, "Scan has already begun");
        status = true;
    } else {
        std::promise<bool> scan_start_promise;
        std::future<bool> scan_start_future = scan_start_promise.get_future();
        stop_scan_ = false;
        scan_thread_th_ = std::thread(&HciDevice::LeScanLoop, this, std::move(scan_start_promise));

        if (scan_start_future.valid() && scan_start_future.wait_for(std::chrono::seconds(kScanWaitTimeout)) == std::future_status::ready) {
            status = scan_start_future.get();
        } else {
            LOG_E(kLogTag, "Timeout, scan could not start");
        }
    }
    return status;
}

void HciDevice::LeScanLoop(std::promise<bool> &&scan_start_promise) {

    Config_parser bag_conf(kBagheeraConfigFile);

    int scan_type = 1;
    int scan_interval = 0x0012;
    int scan_window = 0x0012;
    int addr_type = LE_PUBLIC_ADDRESS;

    bool is_val_overridden = false;
    {
        if (bag_conf.getParseStatus()) {
            std::string scan = bag_conf.getConfig("nd_bt", "scan", "1", true, is_val_overridden);
            std::string interval = bag_conf.getConfig("nd_bt", "interval", "18", true, is_val_overridden);
            std::string window = bag_conf.getConfig("nd_bt", "window", "18", true, is_val_overridden);
            std::string addr = bag_conf.getConfig("nd_bt", "addr", "0", true, is_val_overridden);

            scan_type = std::stoi(scan);
            scan_interval = std::stoi(interval);
            scan_window = std::stoi(window);
            addr_type = std::stoi(addr);
        }
    }

    int device_handle = -1;
    hci_filter original_filter{};
    bool original_filter_set = false;

    bool status = false;
    do {

        int device_id = hci_get_route(NULL);

        if (0 > device_id) {
            LOG_E(kLogTag, "ERROR: Invalid device: %d:%s", errno, strerror(errno));
            break;
        }

        // Setup device
        device_handle = hci_open_dev(device_id);

        if (0 > device_handle) {
            LOG_E(kLogTag, "Could not open device: %s", strerror(errno));
            break;
        }

        // Disable scan
        if (0 > hci_le_set_scan_enable(device_handle, 0x00, 1, 1000)) {
            LOG_I(kLogTag, "Disable scan: %s", strerror(errno));
        }

        // Set scan parameters
        if (0 > hci_le_set_scan_parameters(device_handle, scan_type_, htobs(scan_interval_), htobs(scan_window_),
                                           address_type_, 0x00, 1000)) {
            LOG_E(kLogTag, "Failed to set scan parameters: %s", strerror(errno));
            break;
        }

        // Enable scan
        if (0 > hci_le_set_scan_enable(device_handle, 0x01, 0x00, 1000)) {
            LOG_E(kLogTag, "Failed to enable scan: %s", strerror(errno));
            break;
        }

        // Save the current HCI filter
        socklen_t olen = sizeof(original_filter);
        if (0 > getsockopt(device_handle, SOL_HCI, HCI_FILTER, &original_filter, &olen)) {
            LOG_E(kLogTag, "Could not get socket options: %s", strerror(errno));
            break;
        }

        original_filter_set = true;

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
        is_scan_on_ = true;

        bool done = false;
        LOG_I(kLogTag, "Scan started");

        while (!done && !stop_scan_) {
            ssize_t len = 0;

            uint8_t buf[HCI_MAX_EVENT_SIZE] = {};

            constexpr int kPollTimeout_ms = 100;

            struct pollfd fds[1] = {};
            fds[0].fd = device_handle;
            fds[0].events = POLLIN;

            const nfds_t nfds = 1;

            const int ready = poll(fds, nfds, kPollTimeout_ms);

            if (-1 == ready) {
                LOG_E(kLogTag, "poll() failed errno: %d, err: %s", errno, strerror(errno));
                break;
            }

            if (0 == ready) {
                if (stop_scan_) {
                    done = true;
                    LOG_I(kLogTag, "Breaking loop as stop_scan_ is set");
                    break;
                } else {
                    continue;
                }
            }

            if (fds[0].revents & POLLIN) {
                while ((len = read(device_handle, buf, HCI_MAX_EVENT_SIZE)) < 0) {

                    if (stop_scan_) {
                        done = true;
                        LOG_I(kLogTag, "Breaking loop as stop_scan_ is set");
                        break;
                    }

                    if ((EINTR == errno) || (EAGAIN == errno)) {
                        continue;
                    } else {
                        LOG_E(kLogTag, "Breaking loop due to error-> %d:%s", errno, strerror(errno));
                        done = true;
                        break;
                    }
                }

                if (done) {
                    break;
                }
            } else {
                LOG_E(kLogTag, "poll() returned with unexpected events: %d", fds[0].revents);
                continue;
            }

            if ((4 > len) || (HCI_EVENT_PKT != buf[0]) || (EVT_LE_META_EVENT != buf[1]) || (len - 3 != buf[2])) {
                continue;
            }

            const evt_le_meta_event *meta = (evt_le_meta_event*)(buf + 1 + HCI_EVENT_HDR_SIZE);

            if (EVT_LE_ADVERTISING_REPORT != meta->subevent) {  // -> buf + 3
                continue;
            }

            const uint8_t *meta_data = meta->data;
            uint8_t reports_count = meta_data[0];  // -> buf + 4
            const void* offset = meta_data + 1;

            while (reports_count-- > 0) {
                const le_advertising_info *info = (le_advertising_info *) (offset);

                if (0 == info->length){
                    continue;
                }

                std::string mac_addr;
                mac_addr.resize(17);

                ba2str(&info->bdaddr, const_cast<char *>(mac_addr.c_str()));
                const int rssi = static_cast<int8_t>(info->data[info->length]);

                if (le_event_cb_) {
                    le_event_cb_(mac_addr, info->data, info->length, rssi);
                }

                offset = info->data + info->length + 2;
            }
        }
        status = true;
    } while (false);

    is_scan_on_ = false;

    if (!status) {
        scan_start_promise.set_value(false);
    }

    if (-1 < device_handle) {
        if (original_filter_set) {
            if (0 > setsockopt(device_handle, SOL_HCI, HCI_FILTER, &original_filter, sizeof(original_filter))) {
                LOG_E(kLogTag, "Could not set socket options for original filter: %s", strerror(errno));
            }
        }
        // Disable scan
        if (0 > hci_le_set_scan_enable(device_handle, 0x00, 1, 1000)){
            LOG_E(kLogTag, "Disable scan failed during exit: %s", strerror(errno));
        }
        if (0 > hci_close_dev(device_handle)) {
            LOG_E(kLogTag, "Failed to close device handle: %s", strerror(errno));
        }
    }

    LOG_I(kLogTag, "Exit: %s", __func__);
}

bool HciDevice::LeScanOff() {
    const std::lock_guard<std::mutex> lock(api_mutex_);
    is_scan_on_ = false;
    if (0 < scan_thread_th_.native_handle()) {
        stop_scan_ = true;
        if (scan_thread_th_.joinable()) {
            scan_thread_th_.join();
            LOG_I(kLogTag, "LeScanLoop Thread joined");
        }
    } else {
        // already stopped?
        LOG_E(kLogTag, "Looks like thread is already stopped or not running in %s", __func__);
    }
    return true;
}

HciDevice::~HciDevice() {
    LOG_I(kLogTag, "Inside %s", __func__);
    LeScanOff();
}

bool HciDevice::StartServiceAdvertising(const std::string &advertise_name, const std::vector<uint8_t> &advertise_uuid,
                                        const std::string &advertise_data, uint32_t duration) {
    LOG_I(kLogTag, "Inside %s", __func__);

    const std::string uuid = "0x" + nd::utils::Translator::BytesToHexString(advertise_uuid.data(), advertise_uuid.size());

    const std::vector<std::string> child_entry_inputs {std::string("advertise.name")    + " " + advertise_name + "\n",
                                                       std::string("advertise.service") + " " + uuid + " " + advertise_data + "\n",
                                                       std::string("advertise on")      + "\n"};

    const std::vector<std::string> child_exit_inputs {"advertise off\n",
                                                      "exit\n"};

    constexpr uint32_t kTimeoutSecs = 5;

    const bool status = nd::utils::CommandExecutors::Execute(kBluetoothCtlPath, {}, child_entry_inputs,
                                                             child_exit_inputs, duration, duration + kTimeoutSecs);

    return status;
}

bool HciDevice::StopServiceAdvertising() {
    LOG_I(kLogTag, "%s not implemented", __func__);
    return true;
}

bool HciDevice::SupportsScanAndConnectionParallelly() {
    return false;
}

bool HciDevice::IsLeScanOn() {
    const std::lock_guard<std::mutex> lock(api_mutex_);
    return is_scan_on_;
}

std::unique_ptr<interface::IBluetooth> DeviceHelper::CreateConcreteInstance() {
    return std::make_unique<HciDevice>();
}

}  // namespace device

}  // namespace nd
