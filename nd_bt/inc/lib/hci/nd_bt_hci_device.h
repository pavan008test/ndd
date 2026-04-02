/* Copyright (C) 2022 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, October 2022
 */

#ifndef INC_ND_BT_HCI_DEVICE_H_
#define INC_ND_BT_HCI_DEVICE_H_

#include <atomic>
#include <future>
#include <mutex>
#include <memory>
#include <thread>

#include <nd_bt_device_interface.h>

namespace nd {

namespace device {

class DeviceHelper {
    public:
    static std::unique_ptr<interface::IBluetooth> CreateConcreteInstance();
};


class HciDevice : public interface::IBluetooth {
 public:
    HciDevice();
    HciDevice(const HciDevice &) = delete;
    HciDevice(HciDevice &&) = delete;
    HciDevice& operator=(const HciDevice&) = delete;
    HciDevice& operator=(HciDevice&&) = delete;
    ~HciDevice();

    bool Disable() override;
    bool DoGattSetup() override;
    bool DoFwFlash() const;
    bool Enable() override;
    bool IsEnabled() override;
    bool IsLeScanOn() override;
    bool LeScanOff() override;
    bool LeScanOn() override;
    bool RegisterLeEventCallback(nd::interface::LeEventCb cb) override;
    bool ReadCharacteristicData(const std::string& mac_addr, const std::string &uuid_str,
                                std::vector<uint8_t> &result_out, void *caller_arg) override;
    bool ReadCharacteristicData(const std::string &mac_addr, std::unordered_map<std::string,
                                std::vector<uint8_t>> &charac_data_out, void *caller_arg) override;
    bool SetScanParameters(int scan_interval, int scan_window) override;
    bool StartBeaconAdvertising(int advertising_interval, const std::vector<uint8_t> &advertising_uuid, int major_number,
                          int minor_number, int8_t rssi_value) override;
    bool StartDiscovery() override;
    bool StartServiceAdvertising(const std::string &advertise_name, const std::vector<uint8_t> &advertise_uuid,
                                 const std::string &advertise_data, uint32_t duration) override;
    bool StopBeaconAdvertising() override;
    bool StopDiscovery() override;
    bool StopServiceAdvertising() override;
    bool SupportsScanAndConnectionParallelly() override;
    bool TearDownGattSetup() override;
    bool WriteCharacteristicData(const std::string& mac_addr, const std::string &uuid_str,
                                 const std::vector<std::string> &data, nd::interface::AddressType addr_type,
                                 void *caller_arg) override;

 private:
    void LeScanLoop(std::promise<bool> &&scan_start_promise);
    std::atomic<bool> stop_scan_{true};
    std::mutex api_mutex_;
    std::thread scan_thread_th_;
    nd::interface::LeEventCb le_event_cb_;
    int scan_type_{1}; // 1 - active , 0 passive
    int scan_interval_{0x0012};  // 18 in decimal -> 18 * 0.625ms = 11.25ms
    int scan_window_{0x0012};    // 18 in decimal -> 18 * 0.625ms = 11.25ms
    int address_type_{0x00};     // LE_PUBLIC_ADDRESS = 0x00, LE_RANDOM_ADDRESS = 0x01
    bool is_bt_enabled_{false};
    bool is_scan_on_{false};
    bool toggle_firmware_flash_{false};
    bool will_wifi_configure_in_ap_mode_{false};
    bool is_seamless_char_ops_required_{false};
};

}  // namespace device

}  // namespace nd

#endif  // INC_ND_BT_HCI_DEVICE_H_
