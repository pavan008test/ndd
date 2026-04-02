#ifndef ND_DEVICE_SERVICES_AWSIOT_ND_IOT_INC_ACCESSORYHANDLER_H
#define ND_DEVICE_SERVICES_AWSIOT_ND_IOT_INC_ACCESSORYHANDLER_H

#include <list>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <condition_variable>
#include <atomic>

#include <aws_iot_internal.h>

#include <nd_factory.h>
#include <service_utils.h>
struct CurlUploadData {
    std::string response_string;
    int retry_count = 0;
    uint64_t ping_id = 0;
    bool status = false;

    CurlUploadData() = default;
    CurlUploadData(std::string response, uint64_t ping, bool st)
        : response_string(std::move(response)), retry_count(0), ping_id(ping), status(st) {}
};

class AccessoryHandler {
  public:
    static AccessoryHandler& GetInstance();
    bool HandlePairUnpairRequest(request_t &req);

  private:
    AccessoryHandler() = default;
    ~AccessoryHandler();
    AccessoryHandler(const AccessoryHandler&) = delete;
    AccessoryHandler& operator=(const AccessoryHandler&) = delete;

    static size_t WriteCb(void* ptr, size_t size, size_t nmemb, void* userdata);
    bool PrepareMultipartFormCurlRequest(const std::string& data);

    void StartWorkerIfNeeded();
    void WorkerLoop();
    bool NotifyCloudRequestStatus();
    bool HandleAccessoryPairUnpair(json_t *data_array, json_t *response_json, bool pair_accessory, std::string &accessory_type);

    const int MAX_RETRY_COUNT = 3;

    // create a mutex and thread var and vector for handling async operations
    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread thread_;
    std::atomic<bool> running_worker_{false};
    std::atomic<bool> stop_worker_{false};
    std::list<CurlUploadData> response_data_list_;
};

#endif // ND_DEVICE_SERVICES_AWSIOT_ND_IOT_INC_ACCESSORYHANDLER_H
