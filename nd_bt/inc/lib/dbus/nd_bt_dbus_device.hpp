/* Copyright (C) 2025 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, July 2025
 * Written by Deepak Chaurasiya <deepak.chaurasiya@netradyne.com>, July 2025
 */

#ifndef INC_ND_BT_DBUS_DEVICE_HPP_
#define INC_ND_BT_DBUS_DEVICE_HPP_

#include <nd_bt_device_interface.h>

#include <chrono>

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace nd {

namespace device {

class BackupDeviceHelper {
   public:
    static std::unique_ptr<interface::IBluetooth> CreateBackUpInstance();
};

class DbusBTDevice : public interface::IBluetooth {
   public:
    DbusBTDevice();  // Original: DBUS_BT

    DbusBTDevice(const DbusBTDevice &) = delete;
    DbusBTDevice(DbusBTDevice &&) = delete;
    DbusBTDevice &operator=(const DbusBTDevice &) = delete;
    DbusBTDevice &operator=(DbusBTDevice &&) = delete;
    ~DbusBTDevice();  // Original: ~DBUS_BT

    bool DoGattSetup() override;
    bool TearDownGattSetup() override;
    bool LeScanOn() override;
    bool LeScanOff() override;
    bool RegisterLeEventCallback(interface::LeEventCb cb) override;
    bool SupportsScanAndConnectionParallelly() override;  // yes

    bool Enable() override;
    bool IsEnabled() override;
    bool Disable() override;

    bool SetScanParameters(int scan_interval, int scan_window) override;

    bool StartBeaconAdvertising(int advertising_interval, const std::vector<uint8_t> &advertising_uuid,
                                int major_number, int minor_number, int8_t rssi_value) override;
    bool StopBeaconAdvertising() override;

    bool StartServiceAdvertising(const std::string &advertise_name, const std::vector<uint8_t> &advertise_uuid,
                                 const std::string &advertise_data, uint32_t duration) override;
    bool StopServiceAdvertising() override;
    // Original: char ** argv -> char** args // No Changes as of now
    bool StartDiscovery() override;
    bool StopDiscovery() override;

    bool IsLeScanOn() override;

    bool WriteCharacteristicData(const std::string &mac_addr, const std::string &uuid,
                                 const std::vector<std::string> &data, nd::interface::AddressType addr_type,
                                 void *caller_arg) override;

    bool ReadCharacteristicData(const std::string &mac_addr, const std::string &uuid, std::vector<uint8_t> &result_out,
                                void *caller_arg) override;

    // In the public section of the DbusBTDevice class
    bool ReadCharacteristicData(const std::string &mac_addr,
                                std::unordered_map<std::string, std::vector<uint8_t>> &charac_data_out,
                                void *caller_arg) override;

    // bool ReadCharacteristicData(
    //     const std::unordered_map < std::string, std::vector < std::string >> & macToUuids,
    //         std::unordered_map < std::string, std::unordered_map < std::string, std::vector < uint8_t >>> &
    //         charac_data_out, void *caller_arg);

   private:
    class DbusDeviceImpl;
    std::unique_ptr<DbusDeviceImpl> dbus_device_impl_ptr_;
    std::mutex api_mutex_;
};

}  // namespace device

}  // namespace nd

#endif  // INC_ND_BT_DBUS_DEVICE_HPP_
