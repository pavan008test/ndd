#include <accessoryhandler.h>

#include <atomic>
#include <config_parser.h>
#include <jansson/jansson.h>
#include <log.h>
#include <mutex>
#include <thread>
#include <curl/curl.h>
#include <condition_variable>

#include <nd_accessory_db.h>
#include <aws_iot_internal.h>

static const char* TAG = "AccessoryHandler";

#pragma region Object Instance

AccessoryHandler& AccessoryHandler::GetInstance() {
  static AccessoryHandler instance;
  return instance;
}

AccessoryHandler::~AccessoryHandler() {
    stop_worker_ = true;
    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

#pragma endregion



#pragma region Curl Helpers

size_t AccessoryHandler::WriteCb(void* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* data = static_cast<std::string*>(userdata);
    data->append(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

bool AccessoryHandler::PrepareMultipartFormCurlRequest(const std::string& data) {

    CURL* curl = nullptr;
    curl_slist* header = nullptr;
    curl_mime* multipart = nullptr;
    std::string response_data;
    bool ret = false;

    do {
        curl = curl_easy_init();
        if (!curl) {
            LOG_E(TAG, "curl_easy_init failed");
            break;
        }

        std::string auth_header;
        if (!get_auth_header(auth_header)) {
            LOG_E(TAG, "Corrupted jwt, Not connecting to cloud !");
            break;
        }

        const std::string x_dev_type = "X-DeviceType: " + get_device_type();
        const std::string x_dev_id = "X-DeviceId: " + get_device_id();

        header = curl_slist_append(header, x_dev_type.c_str());
        header = curl_slist_append(header, x_dev_id.c_str());
        header = curl_slist_append(header, auth_header.c_str());

        multipart = curl_mime_init(curl);
        curl_mimepart* part = curl_mime_addpart(multipart);
        curl_mime_name(part, "data");
        curl_mime_data(part, data.c_str(), CURL_ZERO_TERMINATED);

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
        curl_easy_setopt(curl, CURLOPT_URL, get_server_address().c_str());
        curl_easy_setopt(curl, CURLOPT_MIMEPOST, multipart);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, AccessoryHandler::WriteCb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            LOG_E(TAG, "curl call failed: %s", curl_easy_strerror(res));
            break;
        }

        LOG_I(TAG, "response_data: %s", response_data.c_str());

        if (response_data.find(RESPONSE_JWT_SIG_INVALID) != std::string::npos ||
            response_data.find(RESPONSE_JWT_ALG_INVALID) != std::string::npos ||
            response_data.find(RESPONSE_JWT_HEADER_MISSING) != std::string::npos) {
            LOG_E(TAG, "JWT key corrupted? Notify");
            notify_key_corruption();
            break;
        }

        if (response_data.find("true") != std::string::npos) {
            LOG_I(TAG, "response_data: true");
            ret = true;
        } else {
            LOG_E(TAG, "response_data: false");
            ret = false;
        }
    } while (false);

    if (curl) curl_easy_cleanup(curl);
    if (header) curl_slist_free_all(header);
    if (multipart) curl_mime_free(multipart);

    return ret;
}

#pragma endregion

#pragma region Thread Handling

void AccessoryHandler::StartWorkerIfNeeded() {
    LOG_I(TAG, "Inside StartWorkerIfNeeded");
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_worker_) {
        LOG_I(TAG, "Worker already running");
        return;
    }
    // Join any previous finished thread before reusing thread_ object
    if (thread_.joinable()) {
        LOG_I(TAG, "Joining previous worker thread before starting new one");
        thread_.join();
    }
    stop_worker_ = false;
    running_worker_ = true;
    LOG_I(TAG, "Starting AccessoryHandler worker thread");
    thread_ = std::thread(&AccessoryHandler::WorkerLoop, this);
    LOG_I(TAG, "AccessoryHandler worker thread started");
}

void AccessoryHandler::WorkerLoop() {

    while (stop_worker_ == false) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]{ return stop_worker_ || !response_data_list_.empty(); });
        }
        NotifyCloudRequestStatus();
    }
    running_worker_ = false;
}

#pragma endregion



#pragma region Accessory Handling

bool AccessoryHandler::HandlePairUnpairRequest(request_t &req) {
    bool ret = false;
    json_t *root = nullptr, *data_array = nullptr, *response_json = nullptr, *errors = nullptr;
    json_error_t jerror;
    std::string accessory_type;
    bool is_pair = req.is_pair;
    uint64_t ping_id = req.id;
    std::string response_string;

    do {
        LOG_I(TAG, "HandlePairUnpairRequest: Request ID: %llu", req.id);
        {
            LOG_I(TAG, "Checking for duplicate request ID: %llu", req.id);
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& existing_req : response_data_list_) {
                if (existing_req.ping_id == req.id) {
                    LOG_I(TAG, "Duplicate request found: %llu, skipping add", req.id);
                    ret = true;
                    break;
                }
            }
            if (ret) {
                LOG_I(TAG, "Request %llu already exists, skipping processing", req.id);
                break;
            }
        }

        LOG_I(TAG, "Parsing accessory data for request ID: %llu", req.id);
        root = json_loads(req.accessory_data.c_str(), 0, &jerror);
        if (!root) {
            LOG_E(TAG, "json_loads failed: %s", jerror.text);
            break;
        }
        const char* accessory_type_cstr = nullptr;
        if (json_unpack(root, "{s:s,s:o,s:I}",
                        "accessory_type", &accessory_type_cstr,
                        "data", &data_array,
                        "ping_id", &ping_id) != 0) {
            LOG_E(TAG, "json_unpack failed");
            break;
        }
        accessory_type = accessory_type_cstr;
        if (accessory_type.empty() || json_typeof(data_array) != JSON_ARRAY) {
            LOG_E(TAG, "json_unpack failed or missing fields");
            break;
        }

        errors = json_object();
        if (!errors) {
            LOG_E(TAG, "json_object failed");
            break;
        }
        // Placeholder for accessory specific handling
        if (!HandleAccessoryPairUnpair(data_array, errors, is_pair, accessory_type)) {
            LOG_E(TAG, "HandleAccessoryPairUnpair failed for ping_id=%llu, accessory_type=%s", ping_id, accessory_type.c_str());
            break;
        }

        LOG_I(TAG, "Preparing response JSON for ping_id=%llu", ping_id);
        response_json = json_pack_ex(&jerror, 0,
            "{s:s,s:s,s:s,s:I,s:s,s:s,s:o}",
            "deviceversion", get_device_ota_version().c_str(),
            "device_id", get_device_id().c_str(),
            "devicetype", get_device_type().c_str(),
            "ping_id", ping_id,
            "command", is_pair ? "pair" : "unpair",
            "accessory_type", accessory_type.c_str(),
            "data", data_array
        );
        if (!response_json) {
            LOG_E(TAG, "json_pack_ex failed: %s", jerror.text);
            break;
        }

        json_object_update_missing(response_json, errors);

        char* data_char = json_dumps(response_json, 0);
        if (data_char == nullptr) {
            LOG_E(TAG, "json dumps failed for data");
            break;
        }
        response_string = std::string(data_char);
        free(data_char);

        CurlUploadData resp_data(response_string, ping_id, true); // status true for successful processing
        {
            std::lock_guard<std::mutex> lock(mutex_);
            response_data_list_.emplace_back(std::move(resp_data));
        }
        cv_.notify_one();
        StartWorkerIfNeeded();

        ret = true;
    } while(false);

    if (root) json_decref(root);
    if (errors) json_decref(errors);
    if (response_json) json_decref(response_json);

    if (!ret) {
        CurlUploadData resp_data(response_string, ping_id, false); // status false for failed processing
        {
            std::lock_guard<std::mutex> lock(mutex_);
            response_data_list_.emplace_back(std::move(resp_data));
        }
        cv_.notify_one();
        StartWorkerIfNeeded();
    }

    return ret;
}

bool AccessoryHandler::HandleAccessoryPairUnpair(json_t *data_array, json_t *response_json, bool pair_accessory, std::string &accessory_type) {
    bool ret = false;
    json_t *err_code_array = json_array();
    json_t *success_array = json_array();

    do {
        size_t data_array_size = json_array_size(data_array);
        for (size_t i = 0; i < data_array_size; i++) {
            std::string accessory_id;
            int err = 1;
            do {
                json_t *data = json_array_get(data_array, i);
                if (data == nullptr) {
                    LOG_E(TAG, "config value not present in json");
                    break;
                }
                if (json_is_string(json_object_get(data, "accessory_id")) == false) {
                    LOG_E(TAG, "accessory_id not present in json");
                    break;
                }
                accessory_id = json_string_value(json_object_get(data, "accessory_id"));
                if (accessory_id.empty()) {
                    LOG_E(TAG, "accessory_id is empty");
                    break;
                }
                char* data_char = json_dumps(data, JSON_COMPACT);
                if (data_char == nullptr) {
                    LOG_E(TAG, "json dump failed for data");
                    break;
                }
                std::string data_string = data_char;
                free(data_char);
                if (!nd::device::update_accessories_db(accessory_id, accessory_type, data_string, pair_accessory)) {
                    LOG_E(TAG, "accessory data not updated to db for accessory_id: %s", accessory_id.c_str());
                    err = 2;
                    break;
                }
                err = 0;
            } while(false);

            if (err != 0) {
                json_t *error_code_jobj = json_object();
                json_object_set_new(error_code_jobj, "accessory_id", json_string(accessory_id.c_str()));
                json_object_set_new(error_code_jobj, "err", json_integer(err));
                json_array_append_new(err_code_array, error_code_jobj);
            } else {
                json_array_append_new(success_array, json_string(accessory_id.c_str()));
            }
        }
        json_object_set_new(response_json, "errors", err_code_array);
        if (pair_accessory) {
            json_object_set_new(response_json, "paired_ids", success_array);
        } else {
            json_object_set_new(response_json, "unpaired_ids", success_array);
        }
        ret = true;
    } while(false);

    return ret;
}

bool AccessoryHandler::NotifyCloudRequestStatus() {
    while (true) {
        CurlUploadData response_data;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (response_data_list_.empty()) {
                stop_worker_ = true;
                break;
            }
            response_data = std::move(response_data_list_.front());
            response_data_list_.pop_front();
        }
        if (response_data.status) {
            if (!PrepareMultipartFormCurlRequest(response_data.response_string)) {
                LOG_E(TAG, "Failed to send accessory response ping_id=%llu", response_data.ping_id);
                if (response_data.retry_count < MAX_RETRY_COUNT) {
                    response_data.retry_count++;
                    std::lock_guard<std::mutex> lock(mutex_);
                    response_data_list_.emplace_back(std::move(response_data));
                } else {
                    LOG_E(TAG, "Max retry reached for accessory response ping_id=%llu", response_data.ping_id);
                }
                continue;
            }
        }
        if (!update_request_status(response_data.ping_id, response_data.status ? STATUS_DONE : STATUS_ERR)) {
            LOG_E(TAG, "Failed to update request status for ping_id=%llu", response_data.ping_id);
        } else {
            send_internal_msg(INTERNAL_AWS_DONE);
        }
    }
    return true;
}

#pragma endregion
