/* Copyright (C) 2022 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, October 2022
 */

#ifndef INC_ND_BT_QCOM_DEVICE_H_
#define INC_ND_BT_QCOM_DEVICE_H_

#include <memory>
#include <mutex>

#include <nd_bt_device_interface.h>

namespace nd {

namespace device {

class DeviceHelper {
 public:
    static std::unique_ptr<interface::IBluetooth> CreateConcreteInstance();
};

class BackupDeviceHelper {
 public:
    static std::unique_ptr<interface::IBluetooth> CreateBackUpInstance();
};

class QcomDevice : public interface::IBluetooth {
 public:
    QcomDevice();
    QcomDevice(const QcomDevice &) = delete;
    QcomDevice(QcomDevice &&) = delete;
    QcomDevice &operator=(const QcomDevice &) = delete;
    QcomDevice &operator=(QcomDevice &&) = delete;
    ~QcomDevice();

    bool Disable() override;
    bool DoGattSetup() override;
    bool Enable() override;
    bool IsEnabled() override;
    bool IsLeScanOn() override;
    bool LeScanOff() override;
    bool LeScanOn() override;
    bool RegisterLeEventCallback(nd::interface::LeEventCb cb) override;
    bool ReadCharacteristicData(const std::string &mac_addr,
                               const std::string &uuid_str,
                               std::vector<uint8_t> &result_out, void *caller_arg) override;
    bool ReadCharacteristicData(const std::string &mac_addr, std::unordered_map<std::string,
                                std::vector<uint8_t>> &charac_data_out, void *caller_arg) override;
    bool SetScanParameters(int scan_interval, int scan_window) override;
    bool StartBeaconAdvertising(int advertising_interval,
                               const std::vector<uint8_t> &advertising_uuid,
                               int major_number, int minor_number,
                               int8_t rssi_value) override;
    bool StartDiscovery() override;
    bool StartServiceAdvertising(const std::string &advertise_name,
                                 const std::vector<uint8_t> &advertise_uuid,
                                 const std::string &advertise_data,
                                 uint32_t duration) override;
    bool StopBeaconAdvertising() override;
    bool StopDiscovery() override;
    bool StopServiceAdvertising() override;
    bool SupportsScanAndConnectionParallelly() override;
    bool WriteCharacteristicData(const std::string &mac_addr,
                                 const std::string &uuid_str,
                                 const std::vector<std::string> &data,
                                 nd::interface::AddressType addr_type, void *caller_arg) override;
    bool TearDownGattSetup() override;

 private:
    class QcomDeviceImpl;
    std::unique_ptr<QcomDeviceImpl> device_impl_ptr_;
    std::mutex api_mutex_;
    // bool Init();
    // bool DeInit();
    // hw_module_t *module = NULL;
};

} // namespace device

} // namespace nd

#endif // INC_ND_BT_QCOM_DEVICE_H_
