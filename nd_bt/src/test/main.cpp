/* Copyright (C) 2022 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, October 2022
 */

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

#include <log.h>
#include <nd_bt_hci_device.h>
#include <nd_utils.h>

static const char *const kLogDir = "/home/ubuntu/.nddevice/log/btfv";
static const char *const kLogTag = "BTTEST";

#define ROUTE_LOGS

int main () {

    if (!nd_log_init(kLogDir)) {
        std::cout << "unable to init logger :: but continue" << std::endl;
    }

// #ifdef ROUTE_LOGS
//     route_logs(kLogDir);
// #endif

    using nd::device::HciDevice;
    using nd::utils::Translator;

#ifdef TEST_ADVERTISE
    HciDevice device;
    static constexpr int32_t kDriverLoginLegacyBeaconInterval = 0x00A0; // 0xA0 * 0.625ms = 100ms
    static const char *kDriverLegacyLoginBeaconUuid = "71B2F81728754835A94EF9B6C31A5A24";
    const auto advertise_bytes = Translator::HexStringToBytes(kDriverLegacyLoginBeaconUuid);
    constexpr int32_t kAdvertiseMajor = 0x007B; // -> 123
    constexpr int32_t kAdvertiseMinor = 0x01C8; // -> 456
    constexpr int8_t kAdvertiseRssi = 0xC8;
    bool status = device.StartBeaconAdvertising(kDriverLoginLegacyBeaconInterval, advertise_bytes,
                                                            kAdvertiseMajor, kAdvertiseMinor, kAdvertiseRssi);

    LOG_I (kLogTag, "StartBeaconAdvertising status : %d", status);

    std::this_thread::sleep_for(std::chrono::seconds(10));

    status = device.StopBeaconAdvertising();

    LOG_I (kLogTag, "StopBeaconAdvertising status : %d", status);

#endif /*TEST_ADVERTISE*/

#ifdef TEST_LESCAN
    HciDevice device;
    device.Disable();
    device.Enable();
    nd::interface::LeEventCb callback = [](const std::string &mac_addr, const uint8_t *const adv_data, size_t length, int rssi) {
            LOG_I (kLogTag, "mac: %s data: %s rssi: %d", mac_addr.c_str(), Translator::BytesToHexString(adv_data, length).c_str(), rssi);
    };
    device.RegisterLeEventCallback(callback);
    device.LeScanOn();

    std::this_thread::sleep_for(std::chrono::seconds(10));

#endif /*TEST_LESCAN*/

    return 0;
}
