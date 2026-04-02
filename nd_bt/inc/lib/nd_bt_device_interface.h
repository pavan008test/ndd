/* Copyright (C) 2022 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, October 2022
 */

#ifndef INC_ND_BT_DEVICE_INTERFACE_H_
#define INC_ND_BT_DEVICE_INTERFACE_H_

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace nd {

namespace interface {

using LeEventCb = std::function<void(const std::string &mac_addr, const uint8_t *const adv_data, size_t length, int rssi)>;

enum class AddressType {
    kPublic,
    kRandom,
    kAny,
};

class IBluetooth {
 public:

    virtual bool Disable() = 0;
    virtual bool DoGattSetup() = 0;
    virtual bool Enable() = 0;
    virtual bool IsEnabled() = 0;
    virtual bool IsLeScanOn() = 0;
    virtual bool LeScanOff() = 0;
    virtual bool LeScanOn() = 0;
    virtual bool RegisterLeEventCallback(LeEventCb cb) = 0;
    virtual bool ReadCharacteristicData(const std::string &mac_addr, const std::string &uuid,
                                        std::vector<uint8_t> &result_out, void *caller_arg) = 0;
    virtual bool ReadCharacteristicData(const std::string &mac_addr, std::unordered_map<std::string,
                                        std::vector<uint8_t>> &charac_data_out, void *caller_arg) = 0;
    virtual bool SetScanParameters(int scan_interval, int scan_window) = 0;
    virtual bool StartDiscovery() = 0;
    virtual bool StartBeaconAdvertising(int advertising_interval, const std::vector<uint8_t> &advertising_uuid,
                                  int major_number, int minor_number, int8_t rssi_value) = 0;
    virtual bool StopBeaconAdvertising() = 0;
    virtual bool StopDiscovery() = 0;
    virtual bool WriteCharacteristicData(const std::string& mac_addr, const std::string &uuid_str,
                                         const std::vector<std::string> &data, AddressType addr_type,
                                         void *caller_arg) = 0;
    virtual bool StartServiceAdvertising(const std::string &advertise_name, const std::vector<uint8_t> &advertise_uuid,
                                         const std::string &advertise_data, uint32_t duration) = 0;
    virtual bool StopServiceAdvertising() = 0;
    virtual bool SupportsScanAndConnectionParallelly() = 0;
    virtual bool TearDownGattSetup() = 0;
    virtual ~IBluetooth() = default;
};

}  // namespace interface

}  // namespace nd

#endif  // INC_ND_BT_DEVICE_INTERFACE_H_
