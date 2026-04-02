/* Copyright (C) 2023 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, January 2023
 */

#include <nd_bt_factory.h>
#include <nd_bt_device.h>
#include <nd_bt_device_interface.h>
#include <nd_bt_platform_specific.h>
#include <nd_bt_backup_device.h>

#include <nd_factory.h>

#include <log.h>

namespace nd {

namespace device {

static const char * const kLogTag = "FCTRY";

std::mutex BluetoothFactory::factory_instance_mutex_;

std::shared_ptr<interface::IBluetooth> BluetoothFactory::CreateBtInterface() {

    static std::shared_ptr<interface::IBluetooth> interface_ptr;

    if (!interface_ptr) {

        const std::lock_guard<std::mutex> lock(bt_interface_mutex_);

        if (!interface_ptr) {
            try {
                interface_ptr = DeviceHelper::CreateConcreteInstance();
            } catch (const std::exception& e) {
                LOG_E(kLogTag, "[%s:%d] Allocation failed what(): %s", __FUNCTION__, __LINE__, e.what());
            } catch (...) {
                LOG_E(kLogTag, "[%s:%d] Caught an exception of an undetermined type", __FUNCTION__, __LINE__);
            }
        }
    }

    return interface_ptr;
}

std::shared_ptr<interface::IBluetooth> BluetoothFactory::CreateBackUpBtInterface() {

    static std::shared_ptr<interface::IBluetooth> interface_ptr;

    if (!interface_ptr) {

        const std::lock_guard<std::mutex> lock(bt_interface_mutex_);

        if (!interface_ptr) {
            try {
                interface_ptr = BackupDeviceHelper::CreateBackUpInstance();
            } catch (const std::exception& e) {
                LOG_E(kLogTag, "[%s:%d] Allocation failed what(): %s", __FUNCTION__, __LINE__, e.what());
            } catch (...) {
                LOG_E(kLogTag, "[%s:%d] Caught an exception of an undetermined type", __FUNCTION__, __LINE__);
            }
        }
    }

    return interface_ptr;
}

NDService* BluetoothFactory::GetServiceObj(const std::string &name, std::function<void(int signum)> sig_usr_fn_cb) {
    static NDService* service_ptr = nullptr;

    if (!service_ptr) {
        const std::lock_guard<std::mutex> lock(device_factory_mutex_);
        if (!service_ptr) {
            try {
                service_ptr = nd::platform_specific::CreateServiceObj(name, std::move(sig_usr_fn_cb));
            } catch (const std::exception& e) {
                LOG_E(kLogTag, "[%s:%d] Allocation failed what(): %s", __FUNCTION__, __LINE__, e.what());
            } catch (...) {
                LOG_E(kLogTag, "[%s:%d] Caught an exception of an undetermined type", __FUNCTION__, __LINE__);
            }
        }
    }

    return service_ptr;
}

ND_DeviceFactory* BluetoothFactory::GetDeviceFactoryObj() {

    static ND_DeviceFactory* instance_ptr = nullptr;

    if (!instance_ptr) {

        const std::lock_guard<std::mutex> lock(device_factory_mutex_);

        if (!instance_ptr) {
            try {
                instance_ptr = ND_DeviceFactory::Create_NDDevice();
            } catch (const std::exception& e) {
                LOG_E(kLogTag, "[%s:%d] Allocation failed what(): %s", __FUNCTION__, __LINE__, e.what());
            } catch (...) {
                LOG_E(kLogTag, "[%s:%d] Caught an exception of an undetermined type", __FUNCTION__, __LINE__);
            }
        }
    }

    return instance_ptr;
}

std::shared_ptr<BluetoothFactory> BluetoothFactory::GetInstance() {

    static std::shared_ptr<BluetoothFactory> instance_ptr;

    if (!instance_ptr) {

        const std::lock_guard<std::mutex> lock(factory_instance_mutex_);

        if (!instance_ptr) {
            try {
                instance_ptr = std::make_shared<BluetoothFactory>();
            } catch (const std::exception& e) {
                LOG_E(kLogTag, "[%s:%d] Allocation failed what(): %s", __FUNCTION__, __LINE__, e.what());
            } catch (...) {
                LOG_E(kLogTag, "[%s:%d] Caught an exception of an undetermined type", __FUNCTION__, __LINE__);
            }
        }
    }

    return instance_ptr;
}

}  // namespace device

}  // namespace nd
