/* Copyright (C) 2023 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, January 2023
 */
#include <cstring>
#include <chrono>
#include <cmath>
#include <iostream>
#include <unordered_set>

#include <nd_bt_ble_observer.h>
#include <log.h>

namespace nd {

namespace helpers {

static const char * const kLogTag = "LEOBS";

// Beacon parameters
constexpr uint8_t kIBeaconManufacturerTypePos  = 5;
constexpr uint8_t kIBeaconManufacturerTypeSize = 2;
constexpr uint8_t kIBeaconAdIndicatorPos       = 7;
constexpr uint8_t kIBeaconAdIndicatorSize      = 1;
constexpr uint8_t kIBeaconDataLengthPos        = 8;
constexpr uint8_t kIBeaconDataLengthSize       = 1;
constexpr uint8_t kIBeaconUuidPos              = 9;
constexpr uint8_t kIBeaconUuidSize             = 16;
constexpr uint8_t kIBeaconMajorPos             = 25;
constexpr uint8_t kIBeaconMajorSize            = 2;
constexpr uint8_t kIBeaconMinorPos             = 27;
constexpr uint8_t kIBeaconMinorSize            = 2;
constexpr uint8_t kIBeaconTxPowerPos           = 29;
constexpr uint8_t kIBeaconTxPowerSize          = 1;

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

struct WatcherSpecifics {
    WatcherSpecifics(std::unique_ptr<ISpecification> ptr, DeviceFoundCB cb, SpecificationType type,
                     bool auto_remove, bool allow_multiple_notify, uint32_t id);
    std::unique_ptr<ISpecification> spec_ptr_;
    std::unordered_set<std::string> reported_set_;
    DeviceFoundCB cb_;
    SpecificationType type_;
    bool auto_remove_ {false};
    bool allow_multiple_notify_{false};
    uint32_t id_ {0U};
    ~WatcherSpecifics() = default;
};

WatcherSpecifics::WatcherSpecifics(std::unique_ptr<ISpecification> ptr, DeviceFoundCB cb,
                                   SpecificationType type, bool auto_remove,
                                   bool allow_multiple_notify, uint32_t id): spec_ptr_ (std::move(ptr)),
                                                                             cb_ (std::move(cb)),
                                                                             type_ (type),
                                                                             auto_remove_ (auto_remove),
                                                                             allow_multiple_notify_(allow_multiple_notify),
                                                                             id_(id) {
}

struct BleEventObserver::BleData {
    std::string mac_addr_;
    std::vector<uint8_t> adv_data_;
    int rssi_;
};

uint32_t BleEventObserver::SetFilter(SpecificationType type, std::unique_ptr<ISpecification> specification_ptr,
                               DeviceFoundCB device_cb, bool auto_remove, bool allow_multiple_notify) {

    LOG_I(kLogTag, "SetFilter type: %d, auto_remove: %d", type, (int)auto_remove);

    std::lock(api_mutex_, filter_mutex_);
    const std::lock_guard<std::mutex> lck1(api_mutex_, std::adopt_lock);
    const std::lock_guard<std::mutex> lck2(filter_mutex_, std::adopt_lock);

    const uint32_t filter_id = ++next_filter_id_;
    filter_id_map_[filter_id] = type;

    watcher_map_[type].push_back(std::unique_ptr<WatcherSpecifics>(new (std::nothrow) WatcherSpecifics(std::move(specification_ptr),
                                                                                                       device_cb, type, auto_remove,
                                                                                                       allow_multiple_notify, filter_id)));
    return filter_id;
}

BleEventObserver::BleEventObserver() {
    try {
        notifier_th_ = std::thread(&BleEventObserver::NotifyLoop, this);
        stop_processing_ble_data_ = false;
    } catch (const std::system_error &e) {
        LOG_E(kLogTag, "[%s:%d] System error what(): %s", __FUNCTION__, __LINE__, e.what());
    } catch (...) {
        LOG_E(kLogTag, "[%s:%d] Caught an exception of an undetermined type", __FUNCTION__, __LINE__);
    }
};

BleEventObserver::~BleEventObserver() {
    if (0 < notifier_th_.native_handle()) {
        stop_processing_ble_data_ = true;
        if (notifier_th_.joinable()) {
            notifier_th_.join();
            LOG_I(kLogTag, "NotifyLoop Thread joined");
        }
    } else {
        // already stopped?
        LOG_E(kLogTag, "Looks like thread is already stopped or not running in %s", __func__);
    }
}

bool BleEventObserver::ClearFilter(uint32_t filter_id) {
    std::lock(api_mutex_, filter_mutex_);
    std::lock_guard<std::mutex> lck1(api_mutex_, std::adopt_lock);
    std::lock_guard<std::mutex> lck2(filter_mutex_, std::adopt_lock);

    bool status = false;

    do {
        const auto filter_itr = filter_id_map_.find(filter_id);

        if (filter_id_map_.end() == filter_itr) {
            LOG_E(kLogTag, "No such filter: %u", filter_id);
            break;
        }

        const auto type = filter_itr->second;

        auto watcher_itr = watcher_map_.find(type);

        if (watcher_map_.end() == watcher_itr) {
            LOG_E(kLogTag, "No such type: %d", static_cast<int>(type));
            break;
        }

        std::vector<std::unique_ptr<WatcherSpecifics>> &element_vector = watcher_itr->second;
        for (auto element = element_vector.begin(); element != element_vector.end();) {
            if (*element && (*element)->id_ == filter_id) {
                element = element_vector.erase(element);
                status = true;
                LOG_I(kLogTag, "Filter found and deleted");
                break;
            } else {
                ++element;
            }
        }

        if (!status) {
            LOG_I(kLogTag, "No such filter found in vector: %u", filter_id);
        }

    } while (false);

    return status;
}

std::string BleEventObserver::ParseName(const uint8_t *data, size_t data_length) {
    std::string bt_name("unknown");
    size_t offset = 0;
    bool done = false;

    // std::cout << "data length: " << data_length << std::endl;
    while ((!done) && (offset < data_length)) {
        uint8_t field_len = data[0]; // data[0] is field length

        /* Check for the end of advertisement data */
        if ((field_len == 0) ||
            (offset + field_len > data_length)) {
            // std::cout << "breaking as either field_len is 0 or offset + field greater than data" << std::endl;
            done = true;
            break;
        }

        switch (data[1]) { // data[1] is field type
            case kNameShort:
            case kNameComplete: {
                size_t name_len = field_len - 1;
                const char *name_data = reinterpret_cast<const char *>(&data[2]); // data[2] the field data begins from here

                while ((0 < name_len) && ('\0' == name_data[name_len - 1])) {
                    --name_len;
                }

                if (0 < name_len) {
                    const std::string name(name_data, name_len);
                    bt_name = std::move(name);
                    done = true;
                }
                break;
            }
            default: {
                // // std::cout << "reached default" << std::endl;
                break;
            }
        }
        offset += field_len + 1;
        data += field_len + 1;
    }
    return bt_name;
}

double BleEventObserver::GetDistance(int power, int rssi) {
    static constexpr double kAttenuationFactor = 2.0;
    // double power = (std::abs(rssi) - std::abs(acc)) / (10 * kAttenuationFactor);
    // double power = (std::abs(rssi) - std::abs(acc)) / (10 * kAttenuationFactor);
    return std::pow(10, ((power - rssi)/(10 * kAttenuationFactor)));
}

bool BleEventObserver::ParseIBeaconInfo(const uint8_t * const data, size_t length, BeaconInfo &info_out) {
    //raw: 0x02 0x01 0x06 0x1a 0xff 0x4c 0x00 0x02 0x15 0xe2 0xc5 0x6d 0xb5 0xdf 0xfb 0x48 0xd2 0xb0 0x60 0xd0 0xf5 0xa7 0x10 0x96 0xe0 0x00 0x6f 0x00 0xde 0xc5

    // https://os.mbed.com/blog/entry/BLE-Beacons-URIBeacon-AltBeacons-iBeacon/

    // The iBeacon Prefix contains the hex data : 0x0201061AFF004C0215. This breaks down as follows:

    // 0x020106 defines the advertising packet as BLE General Discoverable and BR/EDR high-speed incompatible. Effectively it says this is only broadcasting, not connecting.
    // 0x1AFF says the following data is 26 bytes long and is Manufacturer Specific Data.
    // 0x004C is Apple’s Bluetooth Sig ID and is the part of this spec that makes it Apple-dependent.
    // 0x02 is a secondary ID that denotes a proximity beacon, which is used by all iBeacons.
    // 0x15 defines the remaining length to be 21 bytes (16+2+2+1).

    bool status = false;

    if (30 <= length &&
        0x02 == data[0] &&
        0x01 == data[1] &&
        0x06 == data[2] &&
        0x1A == data[3] &&
        0xff == data[4]) {

        info_out = {};
        info_out.type_ = SpecificationType::kBeaconUUID;
        memcpy(&info_out.prefix_, &data[0], 5);
        memcpy(&info_out.manufacturer_type_, &data[kIBeaconManufacturerTypePos], kIBeaconManufacturerTypeSize);
        memcpy(&info_out.ad_indictor_, &data[kIBeaconAdIndicatorPos], kIBeaconAdIndicatorSize);
        memcpy(&info_out.length_, &data[kIBeaconDataLengthPos], kIBeaconDataLengthSize);
        memcpy(&info_out.uuid_, &data[kIBeaconUuidPos], kIBeaconUuidSize);
        memcpy(&info_out.major_, &data[kIBeaconMajorPos], kIBeaconMajorSize);
        memcpy(&info_out.minor_, &data[kIBeaconMinorPos], kIBeaconMinorSize);
        memcpy(&info_out.tx_power_, &data[kIBeaconTxPowerPos], kIBeaconTxPowerSize);
        status = true;
    }

    return status;
}

bool BleEventObserver::IsSpecTypeClientPresent(SpecificationType type) {
    std::lock_guard<std::mutex> lock(filter_mutex_);
    const bool status = (0 < watcher_map_.count(type));
    return status;
}

void BleEventObserver::CheckAndNotifyWatchers(AdvertisementBase &data) {

    std::lock_guard<std::mutex> lock(filter_mutex_);
    auto watcher_itr = watcher_map_.find(data.type_);
    if (watcher_map_.end() != watcher_itr) {
        std::vector<std::unique_ptr<WatcherSpecifics>> &element_vector = watcher_itr->second;
        for (auto element = element_vector.begin(); element != element_vector.end();) {
            if ((*element) && ((0 == (*element)->reported_set_.count(data.mac_address_)) ||
                               ((*element)->allow_multiple_notify_))) {
                if ((*element)->spec_ptr_ && (*element)->spec_ptr_->IsSatisfied(data) && ((*element)->cb_)) {
                    (*element)->reported_set_.emplace(data.mac_address_);
                    (*element)->cb_(data);
                    if ((*element)->auto_remove_) {
                        element = element_vector.erase(element);
                    } else {
                        ++element;
                    }
                } else {
                    ++element;
                }
            } else {
                ++element;
            }
        }
    }
}

bool BleEventObserver::ParseServiceData(const uint8_t * const adv_data, size_t adv_data_size, ServiceInfo &serv_info_out) {
    bool status = false;

    constexpr uint32_t kMaxTypeLength = 20;
    constexpr uint32_t kMaxNameLength = 20;
    constexpr uint32_t kMaxPayloadLength = 20;

    constexpr uint8_t kIBeaconPrefix[2] = {0x02, 0x01};
    constexpr uint8_t kIBeaconSequence[4] = {0x4C, 0x00, 0x02, 0x15};
    constexpr uint8_t kEddystonePrefix[4] = {0x03, 0x03, 0xAA, 0xFE};
    constexpr uint8_t kEddystoneUUID[2] = {0xAA, 0xFE};
    constexpr uint32_t kIBeaconSequenceLength = 4;

    // NOTE: Currently unused code is commented out for processing optimization

/*     struct BLEData {
        char type[kMaxTypeLength];
        char name[kMaxNameLength];
        uint32_t payload_len;
        uint8_t payload[kMaxPayloadLength];
    };

    struct BLEData temp_bledata{}; */

    // parsing logic
    size_t current_pos = 0;

    // std::cout << "adv_data_size: " << adv_data_size <<"\n";

    while (current_pos < adv_data_size) {
        const uint8_t field_len = adv_data[current_pos++];
        if (0 == field_len) {
            // std::cout << "Feild length is 0\n";
            break;
        }

        const uint8_t data_length = field_len - 1;
        const uint8_t field_type = adv_data[current_pos++];

        const uint8_t * const data = &(adv_data[current_pos]);

        switch (field_type) {

/*             case kFlags: {
                // std::cout << "advertise flags: " << (int)data[0] << "\n";
                break;
            } */

            case kService16bit:
            case kService16bitComplete: {
                serv_info_out.type_ = SpecificationType::kServiceID;
                int uuid_data_pos = current_pos;
                int uuid_data_length = data_length;
                constexpr size_t kUuid16bytes = 2;
                while (0 < uuid_data_length) {
                    std::vector<uint8_t> uuid(&(adv_data[uuid_data_pos]), &(adv_data[uuid_data_pos]) + kUuid16bytes);
                    // std::cout << "found 16 bit uid: " << getHexString(uuid.data(), uuid.size()) << std::endl;
                    serv_info_out.service_uuid_.emplace_back(std::move(uuid));
                    uuid_data_pos += kUuid16bytes;
                    uuid_data_length -= kUuid16bytes;
                }
                break;
            }

            case kService32bit:
            case kService32bitComplete: {
                serv_info_out.type_ = SpecificationType::kServiceID;
                int uuid_data_pos = current_pos;
                int uuid_data_length = data_length;
                constexpr size_t kUuid32bytes = 4;
                while (0 < uuid_data_length) {
                    std::vector<uint8_t> uuid(&(adv_data[uuid_data_pos]), &(adv_data[uuid_data_pos]) + kUuid32bytes);
                    // std::cout << "found 32 bit uid: " << getHexString(uuid.data(), uuid.size()) << std::endl;
                    serv_info_out.service_uuid_.emplace_back(std::move(uuid));
                    uuid_data_pos += kUuid32bytes;
                    uuid_data_length -= kUuid32bytes;
                }
                break;
            }

            case kService128bit:
            case kService128bitComplete: {
                serv_info_out.type_ = SpecificationType::kServiceID;
                int uuid_data_pos = current_pos;
                int uuid_data_length = data_length;
                constexpr size_t kUuid128bytes = 16;
                while (0 < uuid_data_length) {
                    std::vector<uint8_t> uuid(&(adv_data[uuid_data_pos]), &(adv_data[uuid_data_pos]) + kUuid128bytes);
                    // std::cout << "found 128 bit uid: " << getHexString(uuid.data(), uuid.size()) << std::endl;
                    serv_info_out.service_uuid_.emplace_back(std::move(uuid));
                    uuid_data_pos += kUuid128bytes;
                    uuid_data_length -= kUuid128bytes;
                }
                break;
            }

/*             case kNameShort:
            case kNameComplete: {
                // const std::string name(data, data + data_length);
                // std::cout << "name in parse: " << name << "\n";
                break;
            }

            case kTxLevel: {
                // std::cout << "tx level: " << (int)data[0] << "\n";
                break;
            } */

            case kServiceData: {
                // std::cout << "kServiceData found with current_pos: " << current_pos << " data[0]:" << (int)data[0] << " data_len: " << (int)data_length << "\n";
                serv_info_out.type_ = SpecificationType::kServiceID;
                if ((9 <= current_pos) && (3 <= data_length) &&
                    (0 == memcmp(data, kEddystoneUUID, 2)) &&
                    (0 == memcmp(data - 6, kEddystonePrefix, 4))) {
/*                     if (strlen(temp_bledata.type) != 0) {
                        std::cout << "temp_bledata.type is not 0 in kServiceData\n";
                        return false;
                    } */
                    if (0x00 == data[2]) {
/*                         std::cout << "Eddystone-UID found\n";
                        snprintf(temp_bledata.type, kMaxTypeLength, "Eddystone-UID"); */
                        serv_info_out.type_ = SpecificationType::kEddyStoneUID;
                    } else if (0x10 == data[2]) {
/*                         std::cout << "Eddystone-URL found\n";
                        snprintf(temp_bledata.type, kMaxTypeLength, "Eddystone-URL"); */
                    } else if (0x20 == data[2]) {
/*                         std::cout << "Eddystone-TLM found\n";
                        snprintf(temp_bledata.type, kMaxTypeLength, "Eddystone-TLM"); */
                    } else if (0x30 == data[2]) {
/*                         std::cout << "Eddystone-EID found\n";
                        snprintf(temp_bledata.type, kMaxTypeLength, "Eddystone-EID"); */
                    } else {
/*                         std::cout << "Eddystone else\n";
                        return false; */
                    }
/*                     temp_bledata.payload_len = data_length - 3;
                    memcpy(temp_bledata.payload, data + 3, data_length - 3);
                    std::cout << "kServiceData Eddy payload: " << getHexString(temp_bledata.payload, temp_bledata.payload_len) << std::endl; */
                    const uint8_t * const eddy_data = data + 3;

                    for (size_t pos = 0; pos < data_length - 3; ++pos) {
                        serv_info_out.service_data_.emplace_back(eddy_data[pos]);
                    }
                    /* std::cout << "kServiceData service Eddy payload: " << getHexString(serv_info_out.service_data_.data(), serv_info_out.service_data_.size()) << std::endl; */
                } else {
                    if (current_pos + 1 + data_length <= adv_data_size) {
    /*                     memcpy(temp_bledata.payload, data, data_length);
                        temp_bledata.payload_len = data_length;
                        std::cout << "complete kServiceData payload: " << getHexString(temp_bledata.payload, temp_bledata.payload_len) << std::endl; */

                        // constexpr size_t kUuid16bytes = 2;
                        // 16 bit service id
                        serv_info_out.service_id_.emplace_back(data[1]);
                        serv_info_out.service_id_.emplace_back(data[0]);

                        // std::cout << "found 16 bit uid in service data: " << getHexString(serv_info_out.service_id_.data(), serv_info_out.service_id_.size()) << std::endl;

                        // const uint8_t * const serv_data = data + 2;
                        // first 2 is service id -> remaining are service data
                        std::vector<uint8_t> serv_data(data + 2, data + data_length);
                        serv_info_out.service_data_ = std::move(serv_data);
                        // std::cout << "16 bit kServiceData payload: " << getHexString(serv_info_out.service_data_.data(), serv_info_out.service_data_.size()) << std::endl;
                    }
                }

                break;
            }

/*             case kManufacturerSpecific: {
                // std::cout << "kManufacturerSpecific\n";
                if ((10 <= current_pos) && (25 <= data_length) &&
                    (0 == memcmp(data - 5, kIBeaconPrefix, 2)) &&
                    (0 == memcmp(data, kIBeaconSequence, kIBeaconSequenceLength))) {
                    // if (strlen(temp_bledata.type) != 0) {
                    //     std::cout << "temp_bledata.type is not 0 in kManufacturerSpecific\n";
                    // }
                    // snprintf(temp_bledata.type, kMaxTypeLength, "iBeacon");
                    // std::cout << "kManufacturerSpecific:iBeacon\n";
                    // temp_bledata.payload_len = data_length - kIBeaconSequenceLength;
                    // memcpy(temp_bledata.payload, data + 4, data_length - kIBeaconSequenceLength);
                    // std::cout << "kManufacturerSpecific iBeacon payload: " << getHexString(temp_bledata.payload, temp_bledata.payload_len) << std::endl;
                } else {
                    serv_info_out.manufacturer_id_.emplace_back(data[1]);
                    serv_info_out.manufacturer_id_.emplace_back(data[0]);

                    std::vector<uint8_t> serv_data(data + 2, data + data_length);
                    serv_info_out.manufacturer_specific_data_ = std::move(serv_data);
                    // std::cout << "kManufacturerSpecific:id: " << getHexString(serv_info_out.manufacturer_id_.data(), serv_info_out.manufacturer_id_.size()) << "\n";
                    // std::cout << "kManufacturerSpecific:data: " << getHexString(serv_info_out.manufacturer_specific_data_.data(), serv_info_out.manufacturer_specific_data_.size()) << "\n";
                    // std::cout << "kManufacturerSpecific:other\n";
                }

                break;
            } */

            default : {
                // std::cout << "Reached default\n";
                break;
            }
        }
        current_pos += data_length;
    }
    return status;
}

bool BleEventObserver::IsAppleBackgroundServiceID(const uint8_t * const packet, size_t length) {
    // check for apple FF4C0001 : 04 3E 21 02 01 04 01 D4 D2 D5 8C DD 66 15 14 FF 4C 00 01 00000000000000000000000000000008AE
    //                            0  1  2  3  4  5  6  7  8  9 10  11 12 13 14 15 16 17 18 19

    bool status = false;

    if ((5 <= length) &&
        (0xFF == packet[1]) &&
        (0x4C == packet[2]) &&
        (0x00 == packet[3]) &&
        (0x01 == packet[4])) {
            status = true;
        }
    return status;
}

void BleEventObserver::LeScanCallback(const std::string &mac_addr, const uint8_t *adv_data, size_t length, int rssi) {

    {
        const std::lock_guard<std::mutex> lock(data_queue_mutex_);

        BleData data{};
        data.mac_addr_ = mac_addr;
        data.adv_data_ = std::move(std::vector<uint8_t>(adv_data, adv_data + length));
        data.rssi_ = rssi;
        data_queue_.emplace(std::move(data));
    }
    data_cv_.notify_all();
}

void BleEventObserver::NotifyLoop() {
    LOG_I(kLogTag, "Entered %s", __func__);

    static constexpr uint32_t kBleDataCheckInterval = 100; // 100 milliseconds

    while (!stop_processing_ble_data_) {
        std::queue<BleEventObserver::BleData> local_data_queue;

        {
            std::unique_lock<std::mutex> lock(data_queue_mutex_);
            data_cv_.wait_for(lock, std::chrono::milliseconds(kBleDataCheckInterval));
            data_queue_.swap(local_data_queue);
        }

        while ((!local_data_queue.empty()) && (!stop_processing_ble_data_)) {
            auto data = local_data_queue.front();

            do {

                const std::string name = ParseName(data.adv_data_.data(), data.adv_data_.size());

                if (IsSpecTypeClientPresent(SpecificationType::kName)) {

                    AdvertisementBase adv_info{};
                    adv_info.mac_address_ = data.mac_addr_;
                    adv_info.rssi_ = data.rssi_;
                    adv_info.name_ = name;
                    adv_info.type_ = SpecificationType::kName;
                    CheckAndNotifyWatchers(adv_info);
                }

                if (IsSpecTypeClientPresent(SpecificationType::kMacAddress)) {

                    AdvertisementBase adv_info{};
                    adv_info.mac_address_ = data.mac_addr_;
                    adv_info.rssi_ = data.rssi_;
                    adv_info.name_ = name;
                    adv_info.type_ = SpecificationType::kMacAddress;
                    CheckAndNotifyWatchers(adv_info);
                }

                {
                    BeaconInfo beacon_info{};
                    if (ParseIBeaconInfo(data.adv_data_.data(), data.adv_data_.size(), beacon_info)) {
                        // set rssi
                        beacon_info.mac_address_ = data.mac_addr_;
                        beacon_info.name_ = name;
                        beacon_info.rssi_ = data.rssi_;
                        beacon_info.distance_ = GetDistance(static_cast<int8_t>(beacon_info.tx_power_),
                                                                static_cast<int8_t>(beacon_info.rssi_));
                        // beacon_info.timestamp_ = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                        CheckAndNotifyWatchers(beacon_info);
                        break;
                    }
                }

                {
                    ServiceInfo service_info{};
                    service_info.mac_address_ = data.mac_addr_;
                    service_info.rssi_ = data.rssi_;
                    service_info.name_ = name;

                    if (IsAppleBackgroundServiceID(data.adv_data_.data(), data.adv_data_.size())) {
                        service_info.type_ = SpecificationType::kAppleBackgroundServiceID;
                        // service_info.timestamp_ = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                        CheckAndNotifyWatchers(service_info);
                        break;
                    }

                    ParseServiceData(data.adv_data_.data(), data.adv_data_.size(), service_info);

                    if (!service_info.service_id_.empty() || !service_info.service_data_.empty() ||
                        !service_info.service_uuid_.empty() || !service_info.manufacturer_id_.empty()) {
                        service_info.rssi_ = data.rssi_;
                        // service_info.timestamp_ = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                        CheckAndNotifyWatchers(service_info);
                    }
                }

            } while (false);
            local_data_queue.pop();
        }
    }
}

}  // namespace helpers

}  // namespace nd
