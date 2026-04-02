/* Copyright (C) 2022 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, October 2022
 */

#ifndef INC_ND_BT_BLE_OBSERVER_H_
#define INC_ND_BT_BLE_OBSERVER_H_

#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace nd {

namespace helpers {

struct WatcherSpecifics;

enum class SpecificationType {
    kEddyStoneUID,
    kServiceID,
    kAppleBackgroundServiceID,
    kMacAddress,
    kName,
    kBeaconUUID,
};

struct AdvertisementBase {
    std::string mac_address_;
    std::string name_;
    uint8_t rssi_;
    SpecificationType type_;
    // uint64_t timestamp_;
    virtual ~AdvertisementBase() = default;
};

struct BeaconInfo : public AdvertisementBase {
    // explicit BeaconInfo(std::string addr, std::string name, SpecificationType type);
    uint8_t prefix_[5];
    uint8_t manufacturer_type_[2];
    uint8_t ad_indictor_;
    uint8_t length_;
    uint8_t uuid_[16];
    uint8_t major_[2];
    uint8_t minor_[2];
    uint8_t tx_power_;
    double distance_;
};

struct ServiceInfo : public AdvertisementBase {
    std::vector<std::vector<uint8_t>> service_uuid_;
    std::vector<uint8_t> service_id_;
    std::vector<uint8_t> service_data_;
    std::vector<uint8_t> manufacturer_specific_data_;
    std::vector<uint8_t> manufacturer_id_;
};

class ISpecification {
 public:
    virtual bool IsSatisfied(const AdvertisementBase &device_attr_ptr) = 0;
    virtual ~ISpecification() = default;
};

using DeviceFoundCB = std::function<void(const AdvertisementBase &)>;

class BleEventObserver {

 public:
    BleEventObserver();
    BleEventObserver(const BleEventObserver &) = default;
    BleEventObserver(BleEventObserver &&) = default;
    BleEventObserver &operator=(const BleEventObserver &) = default;
    BleEventObserver & operator=(BleEventObserver &&) = default;
    ~BleEventObserver();

    bool ClearFilter(uint32_t filter_id);
    void LeScanCallback(const std::string &mac_addr, const uint8_t *adv_data, size_t length, int rssi);
    uint32_t SetFilter(SpecificationType type, std::unique_ptr<ISpecification> specification_ptr,
                       DeviceFoundCB device_cb, bool auto_remove, bool allow_multiple_notify);

 private:
    struct BleData;
    void CheckAndNotifyWatchers(AdvertisementBase &data);
    double GetDistance(int power, int rssi);
    bool IsAppleBackgroundServiceID(const uint8_t * const packet, size_t length);
    bool IsSpecTypeClientPresent(SpecificationType type);
    void NotifyLoop();
    bool ParseIBeaconInfo(const uint8_t * const data, size_t length, BeaconInfo &info_out);
    std::string ParseName(const uint8_t *data, size_t data_length);
    bool ParseServiceData(const uint8_t * const adv_data, size_t adv_data_size, ServiceInfo &serv_info_out);

    std::unordered_map<SpecificationType, std::vector<std::unique_ptr<WatcherSpecifics>>> watcher_map_;
    std::unordered_map<uint32_t, SpecificationType> filter_id_map_;

    std::atomic<uint32_t> next_filter_id_{0};
    std::atomic<bool> stop_processing_ble_data_{true};

    std::condition_variable data_cv_;

    std::mutex api_mutex_;
    std::mutex data_queue_mutex_;
    std::mutex filter_mutex_;

    std::queue<BleData> data_queue_;

    std::thread notifier_th_;
};

}  // namespace helpers

}  // namespace nd

#endif  // INC_ND_BT_BLE_OBSERVER_H_
