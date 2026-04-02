/* Copyright (C) 2022 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, October 2022
 */

#ifndef INC_ND_BT_FACTORY_H_
#define INC_ND_BT_FACTORY_H_

#include <functional>
#include <memory>
#include <mutex>

class NDService;
class ND_DeviceFactory;
namespace nd {

namespace interface {
    class IBluetooth;
}

namespace device {

class BluetoothFactory {
 public:
    std::shared_ptr<interface::IBluetooth> CreateBtInterface();
    std::shared_ptr<interface::IBluetooth> CreateBackUpBtInterface();
    static std::shared_ptr<BluetoothFactory> GetInstance();
    NDService* GetServiceObj(const std::string &name, std::function<void(int signum)> sig_usr_fn_cb);
    ND_DeviceFactory* GetDeviceFactoryObj();
 private:
    std::mutex bt_interface_mutex_;
    std::mutex device_factory_mutex_;
    static std::mutex factory_instance_mutex_;
};

}  // namespace device

}  // namespace nd

#endif  // INC_ND_BT_FACTORY_H_
