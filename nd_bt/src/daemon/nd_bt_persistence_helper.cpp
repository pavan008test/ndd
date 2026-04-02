/* Copyright (C) 2022 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, October 2022
 */

#include <chrono>
#include <fstream>
#include <memory>
#include <sstream>

#include <jansson/jansson.h>
#include <boost/filesystem.hpp>

#include <log.h>
#include <nd_bt_constants.h>
#include <nd_bt_factory.h>
#include <nd_bt_persistence_helper.h>
#include <nd_curl_helper.h>
#include <nd_persistence.h>

#include <system_utils.h>

namespace nd {

namespace device {

using nd::constants::kBtDbFolderPath;
using nd::constants::kNDHomePath;
using nd::utils::PersistenceDB;
using DriverSessionData = BtPersistenceDBHelper::DriverSessionData;

static const char *const kLogTag                      = "PERS_H";

static const char *const kCloudEndPointDriverLogin    = "upload/driverLogin";

static const char *const kDriverLoginDbName            = "login.db";
static const char *const kDriverLoginTableName         = "LOGIN";
static const char *const kDriverLoginExtTableName      = "LOGIN_EXT";
static const char *const kCurrentSessionTableName      = "SESSION";

static const char *const kAdsmStateTableName           = "ADSM_STATE";

static constexpr uint32_t kCloudRetryStepInterval      = 60; // 60 seconds -> 1 minute
static constexpr uint32_t kCloudRetryReinitializeLimit = kCloudRetryStepInterval * 10; // 600 seconds -> 10 minutes
static constexpr uint32_t kDriverDataCheckLazyInterval = 300; // 300 seconds -> 5 minutes
static constexpr uint32_t kDriverDataCheckBusyInterval = 10; // 10 seconds -> short interval due to batch processing
static constexpr uint32_t kLoginBatchSize              = 10; // 10 entries from DB to upload

static constexpr const char *const kV2LoginTableDataVersion  = "v2";

static std::string getLegacyTableCreateQuery() {
    return std::string("CREATE TABLE IF NOT EXISTS ") + kDriverLoginTableName +
                       "(ID INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "DRVID TEXT NOT NULL,"
                       "START_TIME TEXT NOT NULL,"
                       "END_TIME TEXT NOT NULL);";
}

static std::string getExtendedTableCreateQuery() {
    return std::string("CREATE TABLE IF NOT EXISTS ") + kDriverLoginExtTableName +
                       "(ID INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,"
                       "START_TIME TEXT NOT NULL,"
                       "DRV_IDS TEXT NOT NULL,"
                       "DRV_DATA TEXT NOT NULL,"
                       "VERSION TEXT NOT NULL);";
}

static std::string getSessionTableCreateQuery() {
    return std::string("CREATE TABLE IF NOT EXISTS ") + kCurrentSessionTableName +
                       "(ID TEXT NOT NULL PRIMARY KEY,"
                       "START_TIME TEXT NOT NULL,"
                       "DRV_IDS TEXT NOT NULL,"
                       "DRV_DATA TEXT NOT NULL,"
                       "VERSION TEXT NOT NULL,"
                       "DO_UPLOAD TEXT NOT NULL);";
}

static std::string getAdsmStateTableCreateQuery() {
    return std::string("CREATE TABLE IF NOT EXISTS ") + kAdsmStateTableName +
                       "(ID INTEGER PRIMARY KEY CHECK (ID = 1),"
                       "STATE_JSON TEXT NOT NULL,"
                       "VERSION INTEGER NOT NULL DEFAULT 1,"
                       "LAST_UPDATED INTEGER NOT NULL);";
}

static std::string getLegacyDriverDataStr(const DriverSessionData &driver_data) {
    json_t *login_array = json_array();

    std::string driver_data_str;

    if (nullptr != login_array) {

        constexpr char kDrvIdStr[] = "id";

        for (const auto &id_map_entry : driver_data.id_map_) {

            json_t *json_str = json_string(id_map_entry.first.c_str());

            if (nullptr != json_str) {
                json_array_append_new(login_array, json_str);
            } else {
                LOG_E(kLogTag, "json_string failed in [%s:%d]", __func__, __LINE__);
            }
        }

        char *login_array_str = json_dumps(login_array, JSON_COMPACT);

        if (nullptr != login_array_str) {
            driver_data_str = login_array_str;
            free(login_array_str);
        } else {
            LOG_E(kLogTag, "json_dumps failed in %s", __func__);
        }

        json_decref(login_array);

    } else {
        LOG_E(kLogTag, "json_array failed in %s", __func__);
    }

    return driver_data_str;
}

static std::string getDriverIdsLoginTimeStr(const DriverSessionData &driver_data) {
    json_t *login_array = json_array();

    std::string driver_ids_login_time;

    if (nullptr != login_array) {

        constexpr char kDrvIdStr[] = "id";
        constexpr char kAppLoginTimeStr[]  = "app_login_time";

        for (const auto &id_map_entry : driver_data.id_map_) {
            json_error_t error{};

            json_t *id_ts = json_pack_ex(&error, 0, "{s:s, s:s}",
                                            kDrvIdStr, id_map_entry.first.c_str(),
                                            kAppLoginTimeStr, std::to_string(id_map_entry.second).c_str());

            if (nullptr != id_ts) {
                json_array_append_new(login_array, id_ts);
            } else {
                LOG_E(kLogTag, "json_pack_ex failed: %s in [%s:%d]", error.text, __func__, __LINE__);
            }
        }

        char *login_array_str = json_dumps(login_array, JSON_COMPACT);

        if (nullptr != login_array_str) {
            driver_ids_login_time = login_array_str;
            free(login_array_str);
        } else {
            LOG_E(kLogTag, "json_dumps failed in %s", __func__);
        }

        json_decref(login_array);

    } else {
        LOG_E(kLogTag, "json_array failed in %s", __func__);
    }

    return driver_ids_login_time;
}

class PersistenceUtils {
 public:
    static bool CheckPersistenceDir(const std::string &path);
    static bool CheckIfFileExists(const std::string &file);
};

bool PersistenceUtils::CheckIfFileExists(const std::string &file) {
    bool status = false;
    boost::system::error_code ec;

    if (boost::filesystem::is_regular_file(file, ec)) {
        // LOG_I(kLogTag, "File:%s exists", file.c_str());
        status = true;
    } else {
        LOG_E(kLogTag, "File:%s does not exist, ec: %s:%d", file.c_str(), ec.message().c_str(), ec.value());
    }
    return status;
}

bool PersistenceUtils::CheckPersistenceDir(const std::string &path) {
    bool status = false;
    boost::system::error_code ec;

    do {
        if (boost::filesystem::is_directory(path, ec)) {
            // LOG_I(kLogTag, "directory already exists");
            status = true;
            break;
        }

        if (boost::filesystem::create_directories(path, ec)) {
            LOG_I(kLogTag, "directory created now");
            status = true;
            break;
        }

        if (0 == ec.value()) {
            // LOG_I(kLogTag, "create_directories ec is success");
            // This is added because in bagheera 1 boost library returns false
            // but the error code is 0 for successful creation of directories
            status = true;
            break;
        }
    } while (false);

    if (!status) {
        LOG_E(kLogTag, "Cannot create directory: %s, ec: %s:%d", path.c_str(), ec.message().c_str(), ec.value());
    }

    return status;
}

class CloudInterface {
 public:
    static bool PostDriverData(std::string &&driver_data, const std::string &device_id);
};

bool CloudInterface::PostDriverData(std::string &&driver_data, const std::string &device_id) {
    bool status = false;

    LOG_I(kLogTag, "Entered %s driver_data: ", __func__, driver_data.c_str());

    // TODO(sunil.s) To be enabled once nd_auth_utils is available
    nd::utils::CurlHelper curl;
    const std::string data = "{\"logins\": [ " + driver_data + " ]" + ", \"device_id\": \"" + device_id + "\"}";
    const auto status_ptr = curl.Call(kCloudEndPointDriverLogin, data);

    do {
        if (!status_ptr) {
            LOG_E(kLogTag, "status_ptr null");
            break;
        }

        bool response = false;
        json_error_t error;
        json_t *root = json_loads(status_ptr->response_string_.c_str(), 0, &error);

        if (NULL != root) {
            const json_t *call_json_resp = json_object_get(root, "response");
            if (NULL != call_json_resp) {
                response = json_boolean_value(call_json_resp);
            } else {
                LOG_E(kLogTag, "\"response\" key not found in curl response");
            }
            json_decref(root);
        } else {
            LOG_E(kLogTag, "Error parsing JSON payload! line %d, column %d: %s",
                  error.line, error.column, error.text);
        }

        if ((CURLE_OK == status_ptr->code_) &&
            (200 == status_ptr->resp_code_) &&
            (response)) {

            status = true;
            LOG_I(kLogTag, "Response success for driver data post");
            break;
        }

        if ((CURLE_COULDNT_RESOLVE_PROXY == status_ptr->code_) ||
            (CURLE_COULDNT_RESOLVE_HOST == status_ptr->code_) ||
            (CURLE_COULDNT_CONNECT == status_ptr->code_)) {

            LOG_E(kLogTag, "Connectivity error?");
        }

    } while(false);

    return status;
}

BtPersistenceDBHelper::BtPersistenceDBHelper(const std::string &device_id, ApiVersion api_version): device_id_(device_id) {
    SetFunctionObjects(api_version);
    MoveValidDataToMainTable();
    RemoveStaleSessionEntries();
}

BtPersistenceDBHelper::~BtPersistenceDBHelper() {
    LOG_I(kLogTag, "Inside %s", __func__);
    StopProcessing();
}

void BtPersistenceDBHelper::SetFunctionObjects(ApiVersion api_version) {
    switch (api_version) {
        case ApiVersion::kLegacy: {

            LOG_I(kLogTag, "API version is Legacy");

            add_login_entry_fn_ = [this](DriverSessionData &&data, const std::string &session_id) {
                                    return AddLoginEntryImplLegacy(std::move(data), session_id);
                                  };
            update_audio_count_fn_ = [this](uint32_t audio_count, const std::string &session_id) {
                                    // dummy function
                                    LOG_I(kLogTag, "Legacy API version, not supported");
                                    return true;
                                  };
            end_session_fn_ = [this](const std::string &session_id) {
                                    // dummy function
                                    LOG_I(kLogTag, "Legacy API version, not supported");
                                    return true;
                                  };

            begin_session_fn_ = [this](DriverSessionData &&data, const std::string &session_id) {
                                    // dummy function
                                    LOG_I(kLogTag, "Legacy API version, not supported");
                                    return true;
                                  };
            break;
        }

        case ApiVersion::kV2: {

            LOG_I(kLogTag, "API version is V2");

            add_login_entry_fn_ = [this](DriverSessionData &&data, const std::string &session_id) {
                                    return AddLoginEntryImplV2(std::move(data), session_id);
                                  };

            update_audio_count_fn_ = [this](uint32_t audio_count, const std::string &session_id) {
                                    return UpdateDriverSessionAudioCountImplV2(audio_count, session_id);
                                  };
            end_session_fn_ = [this](const std::string &session_id) {
                                    return EndDriverSessionImplV2(session_id);
                                  };
            begin_session_fn_ = [this](DriverSessionData &&data, const std::string &session_id) {
                                    return BeginDriverSessionImplV2(std::move(data), session_id);
                                  };
            break;
        }

        default: {

            add_login_entry_fn_ = [this, api_version](DriverSessionData &&data, const std::string &session_id) {
                                    LOG_W(kLogTag, "Unknown API version %d", static_cast<int>(api_version));
                                    return false;
                                  };
            update_audio_count_fn_ = [this, api_version](uint32_t audio_count, const std::string &session_id) {
                                    LOG_W(kLogTag, "Unknown API version %d", static_cast<int>(api_version));
                                    return false;
                                  };
            end_session_fn_ = [this, api_version](const std::string &session_id) {
                                    LOG_W(kLogTag, "Unknown API version %d", static_cast<int>(api_version));
                                    return false;
                                  };
            begin_session_fn_ = [this, api_version](DriverSessionData &&data, const std::string &session_id) {
                                    LOG_W(kLogTag, "Unknown API version %d", static_cast<int>(api_version));
                                    return false;
                                  };
            LOG_E(kLogTag, "Unknown API version %d", static_cast<int>(api_version));

            break;
        }
    }
}

bool BtPersistenceDBHelper::BeginDriverSession(DriverSessionData &&data, const std::string &session_id) {
    return begin_session_fn_(std::move(data), session_id);
}

bool BtPersistenceDBHelper::BeginDriverSessionImplV2(DriverSessionData &&data, const std::string &session_id) {

    bool status = false;
    {
        std::lock(api_mutex_, db_mutex_);
        const std::lock_guard<std::mutex> lck1(api_mutex_, std::adopt_lock);
        const std::lock_guard<std::mutex> lck2(db_mutex_, std::adopt_lock);

        MoveValidDataToMainTable();

        if (!data.id_map_.empty()) {
            status = WriteToSessionTable(std::move(data), session_id);
        } else {
            LOG_E(kLogTag, "Driver ids empty in %s", __FUNCTION__);
        }
    }
    return status;
}

bool BtPersistenceDBHelper::EndDriverSession(const std::string &session_id) {
    return end_session_fn_(session_id);
}

bool BtPersistenceDBHelper::EndDriverSessionImplV2(const std::string &session_id) {
    bool status = false;

    {
        std::lock(api_mutex_, db_mutex_);
        const std::lock_guard<std::mutex> lck1(api_mutex_, std::adopt_lock);
        const std::lock_guard<std::mutex> lck2(db_mutex_, std::adopt_lock);
        MoveValidDataToMainTable();
    }

    StartProcessing();

    return status;
}

void BtPersistenceDBHelper::ProcessLoop() {
    LOG_I(kLogTag, "Entered %s", __func__);

    uint64_t next_post_time = 0;
    bool is_data_available = true;
    // bool nothing_to_upload = false;

    while (true) {
        std::unique_lock<std::mutex> lock(data_mutex_);

        uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();

        uint64_t timeout = kDriverDataCheckLazyInterval;

        do {

            if (!is_data_available) {
                // wait for the lazy interval
                timeout = kDriverDataCheckLazyInterval;
                break;
            }

            if (next_post_time <= now) { // either back-off time is over or first run
                // try upload immediately without wait
                timeout = 0;
                break;
            }

            // wait for the remaining back-off time
            timeout = next_post_time - now;

        } while (false);

        if (data_cv_.wait_for(lock, std::chrono::seconds(timeout), [this] { return (exit_requested_ || data_ready_); })) {

            if (exit_requested_) {
                exit_requested_  = false;
                LOG_I(kLogTag, "Exit requested");
                break;
            }

            if (data_ready_) {
                data_ready_ = false;
                is_data_available = true;
                LOG_I(kLogTag, "Data available");
            }
        }

        lock.unlock();

        now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();

        if (next_post_time > now) {
            LOG_I(kLogTag, "Next post time: %llu", next_post_time);
            continue;
        }

        bool is_legacy_table_data = false;

        // Read db entries in batch
        LoginRecord record_out;

        do {

            if (!ReadLegacyTableDriverDataAsJsonString(record_out)) {
                LOG_I(kLogTag, "No legacy DL data found");
            }

            if (!record_out.data_.empty()) {
                is_legacy_table_data = true;
                break;
            }

            if (!ReadV2DriverDataAsJsonString(record_out)) {
                LOG_I(kLogTag, "No DL data found");
            }

        } while (false);

        if (record_out.data_.empty()) {
            LOG_I(kLogTag, "No data to post");
            is_data_available = false;
            continue;
        }

        is_data_available = true;

        std::ostringstream oss;

        LOG_I(kLogTag, "Size of record_out.data_: %llu", record_out.data_.size());

        for (const auto &data : record_out.data_) {
            oss << data << ",";
        }

        std::string driver_data = oss.str();

        driver_data.pop_back();

        LOG_D(kLogTag, "Data to post %s", driver_data.c_str());

        static uint32_t retry_rate = 1;

        if (CloudInterface::PostDriverData(std::move(driver_data), device_id_)) {
            // Delete read rows
            if (is_legacy_table_data) {
                DeleteLegacyTableRows(record_out.row_ids_);
            } else {
                DeleteRows(record_out.row_ids_, record_out.start_times_);
            }

            retry_rate = 1;
            next_post_time = 0;
        } else {
            next_post_time = now + (kCloudRetryStepInterval * retry_rate++);
            if (next_post_time >= now + kCloudRetryReinitializeLimit) {
                retry_rate = 1;
                next_post_time = 0;
            }
        }
    }
}

bool BtPersistenceDBHelper::ReadLegacyTableDriverDataAsJsonString(LoginRecord &record_out) {
    bool status = false;
    const std::string driver_login_db_path = std::string(kBtDbFolderPath) + kDriverLoginDbName;
    const std::lock_guard<std::mutex> lock(db_mutex_);

    do {
        if (!PersistenceUtils::CheckPersistenceDir(kBtDbFolderPath)) {
            LOG_E(kLogTag, "CheckPersistenceDir failed for path: %s", kBtDbFolderPath);
            break;
        }

        if (!PersistenceUtils::CheckIfFileExists(driver_login_db_path)) {
            LOG_E(kLogTag, "CheckIfFileExists failed");
            break;
        }

        PersistenceDB db(driver_login_db_path);

        if (!db.Open()) {
            LOG_E(kLogTag, "db.Open failed in %s", __func__);
            break;
        }

        do {
            {
                // do integrity check
                const bool delete_on_failure = true;
                const auto integrity_status = db.IntegrityCheck(delete_on_failure);
                if (!integrity_status) {
                    LOG_E(kLogTag, "IntegrityCheck failed in %s", __func__);
                    break;
                }
            }

            {
                // get data
                auto callback = [](void *cb_data, int argc, char **argv, char **cols)-> int {
                    int ret_value = -1;
                    if (4 == argc) {
                        // TODO(sunil) : check if this is argv or cols
                        LoginRecord & record = *(reinterpret_cast<LoginRecord *>(cb_data));

                        // Validate and correct time values

                        constexpr int64_t kFourMonthsInMilliseconds = 4LL * 30LL * 24LL * 60LL * 60LL * 1000LL;

                        const auto system_now = std::chrono::duration_cast<std::chrono::milliseconds>(
                                                    std::chrono::system_clock::now().time_since_epoch()).count();

                        int64_t start_time = system_now - kFourMonthsInMilliseconds; // default value

                        int64_t end_time = start_time;

                        if (!string_to_int64(argv[2], start_time)) {
                            LOG_W(kLogTag, "string_to_int64 failed for start_time in %s", __func__);
                        }
                        if (!string_to_int64(argv[3], end_time)) {
                            LOG_W(kLogTag, "string_to_int64 failed for end_time in %s", __func__);
                        }

                        // Ensure end_time is always greater than start_time
                        if ((end_time <= start_time) || (0 > start_time) || (0 > end_time) || (start_time > system_now)) {
                            LOG_W(kLogTag, "Invalid time range: start_time=%lld, end_time=%lld. Correcting end_time.",
                                  start_time, end_time);
                            end_time = start_time + 60000;  // Add 60 seconds
                        }

                        std::string element;
                        element += std::string("{\"driver_id\":") + argv[1] + ","
                                + " \"start_time\":\"" + std::to_string(start_time) + "\","
                                + " \"end_time\":\"" + std::to_string(end_time) + "\"}";
                        ret_value = 0;
                        LOG_D(kLogTag, "Data in: %s, element: %s at: %s", __FUNCTION__, element.c_str(), argv[0]);
                        record.data_.emplace_back(std::move(element));
                        record.row_ids_.emplace_back(argv[0]);
                    } else {
                        // error
                        std::ostringstream oss;
                        for (auto counter = 0; counter < argc; ++counter) {
                            oss << "argv[counter]:" << argv[counter];
                        }
                        LOG_E(kLogTag, "Read incorrect values from DB : %s", oss.str().c_str());
                    }
                    return ret_value;
                };
                const std::string command = std::string("SELECT * FROM '") + kDriverLoginTableName +
                                            "' LIMIT " + std::to_string(kLoginBatchSize) + ";";
                const auto ret_pair = db.ExecuteCommand(command, callback, &record_out);
                if (SQLITE_OK != ret_pair.first) {
                    LOG_E(kLogTag, "ExecuteCommand for : %s failed: %d - %s",
                        command.c_str(), ret_pair.first, ret_pair.second.c_str());
                    break;
                }
            }
            LOG_I(kLogTag, "Data read success in %s", __func__);
            status = true;
        } while (false);

        db.Close();

    } while (false);

    return status;
}

bool BtPersistenceDBHelper::ReadV2DriverDataAsJsonString(LoginRecord &record_out) {
    bool status = false;

    const std::string driver_login_db_path = std::string(kBtDbFolderPath) + kDriverLoginDbName;
    const std::lock_guard<std::mutex> lock(db_mutex_);

    do {
        if (!PersistenceUtils::CheckPersistenceDir(kBtDbFolderPath)) {
            LOG_E(kLogTag, "CheckPersistenceDir failed for path: %s", kBtDbFolderPath);
            break;
        }

        if (!PersistenceUtils::CheckIfFileExists(driver_login_db_path)) {
            LOG_E(kLogTag, "CheckIfFileExists failed");
            break;
        }

        PersistenceDB db(driver_login_db_path);

        if (!db.Open()) {
            LOG_E(kLogTag, "db.Open failed in %s", __func__);
            break;
        }

        do {
            {
                // do integrity check
                const bool delete_on_failure = true;
                const auto integrity_status = db.IntegrityCheck(delete_on_failure);
                if (!integrity_status) {
                    LOG_E(kLogTag, "IntegrityCheck failed in %s", __func__);
                    break;
                }
            }

            {
                // get data
                auto callback = [](void *cb_data, int argc, char **argv, char **cols)-> int {
                    int ret_value = -1;
                    if ((5 == argc) && (nullptr != argv[0]) && (nullptr != argv[1]) &&
                        (nullptr != argv[2]) && (nullptr != argv[3]) && (nullptr != argv[4])) {
                        LoginRecord & record = *(reinterpret_cast<LoginRecord *>(cb_data));
                        std::string element;
                        std::string audio_count_str = "0";

                        std::string start_time  = argv[1];
                        std::string drv_ids     = argv[2];
                        std::string drv_data    = argv[3];
                        std::string version     = argv[4];

                        if (drv_ids.empty()) {
                            LOG_W(kLogTag, "Empty driver ids in %s", __FUNCTION__);
                            drv_ids = "[]";
                        }

                        json_error_t error;
                        json_t *root = json_loads(drv_data.c_str(), 0, &error);

                        if (nullptr != root) {
                            const json_t *call_json_resp = json_object_get(root, "audio_count");
                            if (nullptr != call_json_resp) {
                                if (json_is_integer(call_json_resp)) {
                                    audio_count_str = std::to_string(json_integer_value(call_json_resp));
                                } else {
                                    LOG_W(kLogTag, "\"audio_count\" key is not an integer");
                                }
                            } else {
                                LOG_W(kLogTag, "\"audio_count\" key not found in JSON");
                            }
                            json_decref(root);
                        } else {
                            LOG_E(kLogTag, "Error parsing JSON payload! line %d, column %d: %s",
                                  error.line, error.column, error.text);
                        }

                        element += std::string("{\"driver_id\":") + drv_ids + ","
                                + " \"start_time\":\"" + start_time + "\","
                                + " \"audio_notification\":" + audio_count_str + ","
                                + " \"version\":\"" + version + "\"}";

                        ret_value = 0;
                        LOG_D(kLogTag, "Data in: %s, element: %s at: %s", __FUNCTION__, element.c_str(), argv[0]);
                        record.data_.emplace_back(std::move(element));
                        record.row_ids_.emplace_back(argv[0]);
                        record.start_times_.emplace_back(start_time);
                    } else {
                        // error
                        std::ostringstream oss;
                        for (auto counter = 0; counter < argc; ++counter) {
                            oss << "argv[counter]:" << (argv[counter] ? argv[counter] : "NULL");
                        }
                        LOG_E(kLogTag, "Read incorrect values from DB : %s", oss.str().c_str());
                    }
                    return ret_value;
                };

                const std::string command = std::string("SELECT ID, START_TIME, DRV_IDS, DRV_DATA, VERSION FROM '") + kDriverLoginExtTableName +
                                            "' LIMIT " + std::to_string(kLoginBatchSize) + ";";
                const auto ret_pair = db.ExecuteCommand(command, callback, &record_out);
                if (SQLITE_OK != ret_pair.first) {
                    LOG_E(kLogTag, "ExecuteCommand for : %s failed: %d - %s",
                        command.c_str(), ret_pair.first, ret_pair.second.c_str());
                    break;
                }
            }
            LOG_I(kLogTag, "Data read success in %s", __func__);
            status = true;
        } while (false);

        db.Close();

    } while (false);

    return status;
}

bool BtPersistenceDBHelper::ClearDriverData() {
    LOG_I(kLogTag, "Entered %s", __func__);

    bool status = false;
    const std::string drop_table_str = std::string("DELETE FROM ") + kDriverLoginTableName;
    const std::string driver_login_db_path = std::string(kBtDbFolderPath) + kDriverLoginDbName;

    do {
        const std::lock_guard<std::mutex> lock(db_mutex_);

        if (!PersistenceUtils::CheckPersistenceDir(kBtDbFolderPath)) {
            LOG_E(kLogTag, "CheckPersistenceDir failed for path: %s", kBtDbFolderPath);
            break;
        }

        PersistenceDB db(driver_login_db_path);

        if (!db.Open()) {
            LOG_E(kLogTag, "db.Open failed in %s", __func__);
            break;
        }

        {
            // delete table
            const auto ret_pair = db.ExecuteCommand(drop_table_str, nullptr, nullptr);
            if (SQLITE_OK != ret_pair.first) {
                LOG_E(kLogTag, "ExecuteCommand for : %s failed: %d - %s",
                    drop_table_str.c_str(), ret_pair.first, ret_pair.second.c_str());
                // Ignore the error, do not break
            } else {
                LOG_I(kLogTag, "Data cleared");
            }
        }
        db.Close();
        status = true;
    } while (false);

    return status;
}

bool BtPersistenceDBHelper::DeleteLegacyTableRows(std::vector<std::string> &ids) {
    LOG_I(kLogTag, "Entered %s", __func__);

    bool status = false;

    do {

        if (ids.empty()) {
            LOG_E(kLogTag, "Empty ids in %s", __func__);
            break;
        }

        std::string id_list;

        for (const auto &id : ids) {
            id_list += id + ",";
        }

        if (id_list.empty()) {
            LOG_E(kLogTag, "Empty id_list in %s", __func__);
            break;
        }

        id_list.pop_back();

        const std::string drop_table_str = std::string("DELETE FROM ") + kDriverLoginTableName + " WHERE ID IN (" + id_list + ");";
        const std::string driver_login_db_path = std::string(kBtDbFolderPath) + kDriverLoginDbName;

        const std::lock_guard<std::mutex> lock(db_mutex_);

        if (!PersistenceUtils::CheckPersistenceDir(kBtDbFolderPath)) {
            LOG_E(kLogTag, "CheckPersistenceDir failed for path: %s", kBtDbFolderPath);
            break;
        }

        PersistenceDB db(driver_login_db_path);

        if (!db.Open()) {
            LOG_E(kLogTag, "db.Open failed in %s", __func__);
            break;
        }

        {
            // delete table
            const auto ret_pair = db.ExecuteCommand(drop_table_str, nullptr, nullptr);
            if (SQLITE_OK != ret_pair.first) {
                LOG_E(kLogTag, "ExecuteCommand for : %s failed: %d - %s",
                    drop_table_str.c_str(), ret_pair.first, ret_pair.second.c_str());
                // Ignore the error, do not break
            } else {
                LOG_I(kLogTag, "Data cleared");
            }
        }

        db.Close();
        status = true;
    } while (false);

    return status;
}


bool BtPersistenceDBHelper::DeleteRows(std::vector<std::string> &ids, std::vector<std::string> &start_times) {
    LOG_I(kLogTag, "Entered %s", __func__);

    bool status = false;

    do {

        if (ids.empty()) {
            LOG_E(kLogTag, "Empty ids in %s", __func__);
            break;
        }

        std::string id_list, start_time_list;

        for (size_t id_length = 0; id_length < ids.size(); ++id_length) {
            id_list += ids[id_length] + ",";
            start_time_list += "'" + start_times[id_length] + "',";
        }

        if (id_list.empty() || start_time_list.empty()) {
            LOG_E(kLogTag, "Empty id_list or start_time_list in %s", __func__);
            break;
        }

        id_list.pop_back();
        start_time_list.pop_back();

        const std::string drop_table_str = std::string("DELETE FROM ") +
                                           kDriverLoginExtTableName +
                                           " WHERE ID IN (" + id_list + ") AND START_TIME IN (" + start_time_list + ");";

        const std::string driver_login_db_path = std::string(kBtDbFolderPath) + kDriverLoginDbName;

        const std::lock_guard<std::mutex> lock(db_mutex_);

        if (!PersistenceUtils::CheckPersistenceDir(kBtDbFolderPath)) {
            LOG_E(kLogTag, "CheckPersistenceDir failed for path: %s", kBtDbFolderPath);
            break;
        }

        PersistenceDB db(driver_login_db_path);

        if (!db.Open()) {
            LOG_E(kLogTag, "db.Open failed in %s", __func__);
            break;
        }

        {
            // delete table
            const auto ret_pair = db.ExecuteCommand(drop_table_str, nullptr, nullptr);
            if (SQLITE_OK != ret_pair.first) {
                LOG_E(kLogTag, "ExecuteCommand for : %s failed: %d - %s",
                    drop_table_str.c_str(), ret_pair.first, ret_pair.second.c_str());
                // Ignore the error, do not break
            } else {
                LOG_I(kLogTag, "Data cleared");
            }
        }

        db.Close();
        status = true;
    } while (false);

    return status;
}

bool BtPersistenceDBHelper::RemoveStaleSessionEntries() {
    bool status = false;

    const std::string delete_query = std::string("DELETE FROM ") + kCurrentSessionTableName + "  WHERE DO_UPLOAD != 'YES';";

    const std::string driver_login_db_path = std::string(kBtDbFolderPath) + kDriverLoginDbName;
    const std::string create_main_table_str = getExtendedTableCreateQuery();

    do {

        const std::lock_guard<std::mutex> lock(db_mutex_);

        if (!PersistenceUtils::CheckPersistenceDir(kBtDbFolderPath)) {
            LOG_E(kLogTag, "CheckPersistenceDir failed for path: %s", kBtDbFolderPath);
            break;
        }

        std::unique_ptr<PersistenceDB> db_ptr = std::make_unique<PersistenceDB>(driver_login_db_path);

        if (!db_ptr) {
            LOG_E(kLogTag, "db_ptr null in %s", __func__);
            break;
        }

        if (!db_ptr->Open()) {
            LOG_E(kLogTag, "db open failed in %s", __func__);
            break;
        }

        do {
            {
                {
                    // do integrity check
                    const bool delete_on_failure = true;
                    const auto integrity_status = db_ptr->IntegrityCheck(delete_on_failure);
                    if (!integrity_status) {
                        LOG_E(kLogTag, "IntegrityCheck failed in %s", __func__);

                        db_ptr = std::make_unique<PersistenceDB>(driver_login_db_path);

                        if (!db_ptr) {
                            LOG_E(kLogTag, "db_ptr null after recreating in %s", __func__);
                            break;
                        }

                        if (!db_ptr->Open()) {
                            LOG_E(kLogTag, "db open failed after recreating in %s", __func__);
                            break;
                        }
                    }
                }
            }

            {
                // Delete data from session table
                const auto ret_pair = db_ptr->ExecuteCommand(delete_query, nullptr, nullptr);
                if (SQLITE_OK != ret_pair.first) {
                    LOG_E(kLogTag, "ExecuteCommand for : %s failed: %d - %s",
                         delete_query.c_str(), ret_pair.first, ret_pair.second.c_str());
                    break;
                }
            }

            LOG_I(kLogTag, "Data deleted successfully in %s", __func__);
            status = true;
        } while (false);

        db_ptr->Close();

    } while (false);

    return status;
}

bool BtPersistenceDBHelper::MoveValidDataToMainTable() {

    LOG_I(kLogTag, "Moving valid data to main table...");

    bool status = false;

    const std::string driver_login_db_path = std::string(kBtDbFolderPath) + kDriverLoginDbName;
    const std::string create_main_table_str = getExtendedTableCreateQuery();

    const std::string move_query = std::string("BEGIN TRANSACTION;") +
                                               "INSERT INTO " + kDriverLoginExtTableName + " (START_TIME, DRV_IDS, DRV_DATA, VERSION)"
                                               "SELECT START_TIME, DRV_IDS, DRV_DATA, VERSION "
                                               "FROM " + kCurrentSessionTableName + " WHERE DO_UPLOAD = 'YES';"
                                               "DELETE FROM " + kCurrentSessionTableName + " WHERE DO_UPLOAD = 'YES';"
                                               "COMMIT;";

    do {

        if (!PersistenceUtils::CheckPersistenceDir(kBtDbFolderPath)) {
            LOG_E(kLogTag, "CheckPersistenceDir failed for path: %s", kBtDbFolderPath);
            break;
        }

        std::unique_ptr<PersistenceDB> db_ptr = std::make_unique<PersistenceDB>(driver_login_db_path);

        if (!db_ptr) {
            LOG_E(kLogTag, "db_ptr null in %s", __func__);
            break;
        }

        if (!db_ptr->Open()) {
            LOG_E(kLogTag, "db open failed in %s", __func__);
            break;
        }

        do {
            {
                {
                    // do integrity check
                    const bool delete_on_failure = true;
                    const auto integrity_status = db_ptr->IntegrityCheck(delete_on_failure);
                    if (!integrity_status) {
                        LOG_E(kLogTag, "IntegrityCheck failed in %s", __func__);

                        db_ptr = std::make_unique<PersistenceDB>(driver_login_db_path);

                        if (!db_ptr) {
                            LOG_E(kLogTag, "db_ptr null after recreating in %s", __func__);
                            break;
                        }

                        if (!db_ptr->Open()) {
                            LOG_E(kLogTag, "db open failed after recreating in %s", __func__);
                            break;
                        }
                    }
                }
            }

            {
                // create main table
                const auto ret_pair = db_ptr->ExecuteCommand(create_main_table_str, nullptr, nullptr);
                if (SQLITE_OK != ret_pair.first) {
                    LOG_E(kLogTag, "ExecuteCommand for : %s failed: %d - %s",
                         create_main_table_str.c_str(), ret_pair.first, ret_pair.second.c_str());
                    break;
                }
            }

            {
                // Move data from auxiliary table to main table
                const auto ret_pair = db_ptr->ExecuteCommand(move_query, nullptr, nullptr);
                if (SQLITE_OK != ret_pair.first) {
                    LOG_E(kLogTag, "ExecuteCommand for : %s failed: %d - %s",
                         move_query.c_str(), ret_pair.first, ret_pair.second.c_str());
                    break;
                }
            }

            LOG_I(kLogTag, "Data moved successfully in %s", __func__);
            status = true;
        } while (false);

        db_ptr->Close();

    } while (false);

    {
        const std::lock_guard<std::mutex> lock(data_mutex_);
        data_ready_ = true;
        data_cv_.notify_all();
    }

    return status;
}

bool BtPersistenceDBHelper::WriteToSessionTable(DriverSessionData &&driver_data, const std::string &session_id) {

    LOG_I(kLogTag, "Entered %s with session id: %s", __func__, session_id.c_str());

    bool status = false;

    const std::string driver_login_db_path = std::string(kBtDbFolderPath) + kDriverLoginDbName;
    const std::string create_session_table_str = getSessionTableCreateQuery();

    do {

        if (!PersistenceUtils::CheckPersistenceDir(kBtDbFolderPath)) {
            LOG_E(kLogTag, "CheckPersistenceDir failed for path: %s", kBtDbFolderPath);
            break;
        }

        std::unique_ptr<PersistenceDB> db_ptr = std::make_unique<PersistenceDB>(driver_login_db_path);

        if (!db_ptr) {
            LOG_E(kLogTag, "db_ptr null in %s", __func__);
            break;
        }

        if (!db_ptr->Open()) {
            LOG_E(kLogTag, "db open failed in %s", __func__);
            break;
        }

        do {
            {
                // do integrity check
                const bool delete_on_failure = true;
                const auto integrity_status = db_ptr->IntegrityCheck(delete_on_failure);
                if (!integrity_status) {
                    LOG_E(kLogTag, "IntegrityCheck failed in %s", __func__);

                    db_ptr = std::make_unique<PersistenceDB>(driver_login_db_path);

                    if (!db_ptr) {
                        LOG_E(kLogTag, "db_ptr null after recreating in %s", __func__);
                        break;
                    }

                    if (!db_ptr->Open()) {
                        LOG_E(kLogTag, "db open failed after recreating in %s", __func__);
                        break;
                    }
                }
            }

            {
                // create session table if not exists
                const auto ret_pair = db_ptr->ExecuteCommand(create_session_table_str, nullptr, nullptr);
                if (SQLITE_OK != ret_pair.first) {
                    LOG_E(kLogTag, "ExecuteCommand for : %s failed: %d - %s",
                         create_session_table_str.c_str(), ret_pair.first, ret_pair.second.c_str());
                    break;
                }
            }

            {
                std::string driver_ids_login_time = getDriverIdsLoginTimeStr(driver_data);

                std::ostringstream oss;
                oss << "INSERT INTO " << kCurrentSessionTableName
                    << " (ID, START_TIME, DRV_IDS, DRV_DATA, VERSION, DO_UPLOAD) VALUES ("
                    << "'" << session_id << "'" << ","
                    << "'" << std::to_string(driver_data.start_time_) << "'" << ","
                    << "'" << driver_ids_login_time << "'" << ","
                    << "'" << "{\"audio_count\":" << driver_data.audio_count_ << "}" << "'" << ","
                    << "'" << kV2LoginTableDataVersion << "'" << ","
                    << "'" << (driver_data.do_upload_ ? "YES" : "NO") << "'" << ");";

                const auto ret_pair = db_ptr->ExecuteCommand(oss.str(), nullptr, nullptr);
                if (SQLITE_OK != ret_pair.first) {
                    LOG_E(kLogTag, "ExecuteCommand for : %s failed: %d - %s",
                          oss.str().c_str(), ret_pair.first, ret_pair.second.c_str());
                    break;
                }
            }

            LOG_I(kLogTag, "Data store success in %s", __func__);
            status = true;
        } while (false);

        db_ptr->Close();

    } while (false);

    return status;
}

bool BtPersistenceDBHelper::UpdateDriverSessionAudioCount(uint32_t audio_count, const std::string &session_id) {
    return update_audio_count_fn_(audio_count, session_id);
}

bool BtPersistenceDBHelper::UpdateDriverSessionAudioCountImplV2(uint32_t audio_count, const std::string &session_id) {

    LOG_I(kLogTag, "Entered %s with session id: %s", __func__, session_id.c_str());

    bool status = false;

    const std::string driver_login_db_path = std::string(kBtDbFolderPath) + kDriverLoginDbName;

    const std::string fetch_session_data = std::string("SELECT ROWID, DRV_DATA FROM ") + kCurrentSessionTableName +
                                                             " WHERE ID = '" + session_id + "';";

    do {

        const std::lock_guard<std::mutex> lock(db_mutex_);

        if (!PersistenceUtils::CheckPersistenceDir(kBtDbFolderPath)) {
            LOG_E(kLogTag, "CheckPersistenceDir failed for path: %s", kBtDbFolderPath);
            break;
        }

        std::unique_ptr<PersistenceDB> db_ptr = std::make_unique<PersistenceDB>(driver_login_db_path);

        if (!db_ptr) {
            LOG_E(kLogTag, "db_ptr null in %s", __func__);
            break;
        }

        if (!db_ptr->Open()) {
            LOG_E(kLogTag, "db open failed in %s", __func__);
            break;
        }

        do {
            {
                // do integrity check
                const bool delete_on_failure = true;
                const auto integrity_status = db_ptr->IntegrityCheck(delete_on_failure);
                if (!integrity_status) {
                    LOG_E(kLogTag, "IntegrityCheck failed in %s", __func__);

                    db_ptr = std::make_unique<PersistenceDB>(driver_login_db_path);

                    if (!db_ptr) {
                        LOG_E(kLogTag, "db_ptr null after recreating in %s", __func__);
                        break;
                    }

                    if (!db_ptr->Open()) {
                        LOG_E(kLogTag, "db open failed after recreating in %s", __func__);
                        break;
                    }
                }
            }

            struct RowData {
                std::string drive_data;
                int64_t rowid;
            };

            RowData row_data{};
            {

                // get data
                auto callback = [](void *cb_data, int argc, char **argv, char **cols)-> int {
                    int ret_value = -1;
                    if ((2 == argc) && (nullptr != argv[0]) &&
                        (nullptr != argv[1])) {
                        // TODO(sunil) : check if this is argv or cols
                        RowData & row_data = *(reinterpret_cast<RowData *>(cb_data));

                        row_data.drive_data = argv[1];
                        int64_t num = -1;
                        if (string_to_int64(argv[0], num)) {
                            row_data.rowid = num;
                        } else {
                            LOG_E(kLogTag, "string_to_int64 failed in %s", __func__);
                        }

                        ret_value = 0;
                        LOG_D(kLogTag, "Data in: %s, element: %s", __FUNCTION__, argv[1]);

                    } else {
                        // error
                        std::ostringstream oss;
                        for (auto counter = 0; counter < argc; ++counter) {
                            oss << "argv[counter]:" << (argv[counter] ? argv[counter] : "NULL");
                        }
                        LOG_E(kLogTag, "Read incorrect values from DB argc(%d) : %s:%s:%d",
                              argc, oss.str().c_str(), __func__, __LINE__);
                    }
                    return ret_value;
                };

                const auto ret_pair = db_ptr->ExecuteCommand(fetch_session_data , callback, &row_data);
                if (SQLITE_OK != ret_pair.first) {
                    LOG_E(kLogTag, "ExecuteCommand for : %s failed: %d - %s",
                        fetch_session_data.c_str(), ret_pair.first, ret_pair.second.c_str());
                    break;
                }
            }

            if (!row_data.drive_data.empty()) {
                json_error_t error;
                json_t *root = json_loads(row_data.drive_data.c_str(), 0, &error);

                char *updated_data = nullptr;

                if (nullptr != root) {
                    json_t *audio_count_json = json_object_get(root, "audio_count");
                    if (nullptr != audio_count_json) {
                        json_integer_set(audio_count_json, audio_count);
                    } else {
                        LOG_I(kLogTag, "\"audio_count\" key not found in json, creating it now");
                        json_object_set_new(root, "audio_count", json_integer(audio_count));
                    }
                    updated_data = json_dumps(root, JSON_COMPACT);
                    json_decref(root);
                } else {
                    LOG_E(kLogTag, "Error parsing JSON payload! line %d, column %d: %s",
                          error.line, error.column, error.text);
                }

                bool update_error = false;

                if ((nullptr != updated_data) && (0 != strlen(updated_data))) { //NOSONAR

                    const std::string update_query = std::string("UPDATE ") + kCurrentSessionTableName +
                                                     " SET DRV_DATA = '" + updated_data + "' , DO_UPLOAD = 'YES' "
                                                     " WHERE ROWID = " + std::to_string(row_data.rowid) + ";";

                    const auto ret_pair = db_ptr->ExecuteCommand(update_query, nullptr, nullptr);
                    if (SQLITE_OK != ret_pair.first) {
                        LOG_E(kLogTag, "ExecuteCommand for : %s failed: %d - %s",
                              update_query.c_str(), ret_pair.first, ret_pair.second.c_str());
                        update_error = true;
                    }
                } else {
                    LOG_E(kLogTag, "json_dumps failed in %s", __func__);
                }

                if (nullptr != updated_data) {
                    free(updated_data);
                }

                if (update_error) {
                    break;
                }

            } else {
                LOG_E(kLogTag, "drive_data empty in %s", __func__);
            }

            LOG_I(kLogTag, "Data store success in %s", __func__);
            status = true;
        } while (false);

        db_ptr->Close();

    } while (false);

    return status;
}

bool BtPersistenceDBHelper::AddLoginEntry(DriverSessionData &&driver_data, const std::string &session_id) {
    return add_login_entry_fn_(std::move(driver_data), session_id);
}

bool BtPersistenceDBHelper::AddLoginEntryImplV2(DriverSessionData &&driver_data, const std::string &session_id) {

    LOG_I(kLogTag, "Entered %s with session id: %s", __func__, session_id.c_str());

    bool status = false;

    // Add data to login table
    const std::string driver_login_db_path = std::string(kBtDbFolderPath) + kDriverLoginDbName;
    const std::string create_main_table_str = getExtendedTableCreateQuery();

    const std::string delete_query = std::string("DELETE FROM ") + kCurrentSessionTableName + " WHERE ID = '" + session_id + "';";

    do {

        const std::lock_guard<std::mutex> lock(db_mutex_);

        if (!PersistenceUtils::CheckPersistenceDir(kBtDbFolderPath)) {
            LOG_E(kLogTag, "CheckPersistenceDir failed for path: %s", kBtDbFolderPath);
            break;
        }

        std::unique_ptr<PersistenceDB> db_ptr = std::make_unique<PersistenceDB>(driver_login_db_path);

        if (!db_ptr) {
            LOG_E(kLogTag, "db_ptr null in %s", __func__);
            break;
        }

        if (!db_ptr->Open()) {
            LOG_E(kLogTag, "db open failed in %s", __func__);
            break;
        }

        do {
            {
                {
                    // do integrity check
                    const bool delete_on_failure = true;
                    const auto integrity_status = db_ptr->IntegrityCheck(delete_on_failure);
                    if (!integrity_status) {
                        LOG_E(kLogTag, "IntegrityCheck failed in %s", __func__);

                        db_ptr = std::make_unique<PersistenceDB>(driver_login_db_path);

                        if (!db_ptr) {
                            LOG_E(kLogTag, "db_ptr null after recreating in %s", __func__);
                            break;
                        }

                        if (!db_ptr->Open()) {
                            LOG_E(kLogTag, "db open failed after recreating in %s", __func__);
                            break;
                        }
                    }
                }
            }

            {
                // create main table
                const auto ret_pair = db_ptr->ExecuteCommand(create_main_table_str, nullptr, nullptr);
                if (SQLITE_OK != ret_pair.first) {
                    LOG_E(kLogTag, "ExecuteCommand for : %s failed: %d - %s",
                         create_main_table_str.c_str(), ret_pair.first, ret_pair.second.c_str());
                    break;
                }
            }

            {
                std::string driver_ids_login_time = getDriverIdsLoginTimeStr(driver_data);

                std::ostringstream insert_data_query;
                insert_data_query << "INSERT INTO " << kDriverLoginExtTableName
                                  << " (START_TIME, DRV_IDS, DRV_DATA, VERSION) VALUES ("
                                  << "'" << std::to_string(driver_data.start_time_) << "'" << ","
                                  << "'" << driver_ids_login_time << "'" << ","
                                  << "'" << "{\"audio_count\":" << driver_data.audio_count_ << "}" << "'" << ","
                                  << "'" << kV2LoginTableDataVersion << "'" << ");";

                // Add data to LOGIN_EXT table
                const auto ret_pair = db_ptr->ExecuteCommand(insert_data_query.str(), nullptr, nullptr);
                if (SQLITE_OK != ret_pair.first) {
                    LOG_E(kLogTag, "ExecuteCommand for : %s failed: %d - %s",
                         insert_data_query.str().c_str(), ret_pair.first, ret_pair.second.c_str());
                    break;
                }
            }

            {
                // Delete data from session table
                const auto ret_pair = db_ptr->ExecuteCommand(delete_query, nullptr, nullptr);
                if (SQLITE_OK != ret_pair.first) {
                    LOG_E(kLogTag, "ExecuteCommand for : %s failed: %d - %s",
                         delete_query.c_str(), ret_pair.first, ret_pair.second.c_str());
                }
            }

            LOG_I(kLogTag, "Data added successfully in %s", __func__);
            status = true;
        } while (false);

        db_ptr->Close();

    } while (false);

    // Remove session entry from session table
    // return add status

    {
        const std::lock_guard<std::mutex> lock(data_mutex_);
        data_ready_ = true;
        data_cv_.notify_all();
    }

    return status;
}

bool BtPersistenceDBHelper::AddLoginEntryImplLegacy(DriverSessionData &&driver_data, const std::string &session_id) {

    LOG_I(kLogTag, "Entered %s with session id: %s", __func__, session_id.c_str());

    bool status = false;

    // Add data to login table
    const std::string driver_login_db_path = std::string(kBtDbFolderPath) + kDriverLoginDbName;
    const std::string create_main_table_str = getLegacyTableCreateQuery();

    do {

        const std::lock_guard<std::mutex> lock(db_mutex_);

        if (!PersistenceUtils::CheckPersistenceDir(kBtDbFolderPath)) {
            LOG_E(kLogTag, "CheckPersistenceDir failed for path: %s", kBtDbFolderPath);
            break;
        }

        std::unique_ptr<PersistenceDB> db_ptr = std::make_unique<PersistenceDB>(driver_login_db_path);

        if (!db_ptr) {
            LOG_E(kLogTag, "db_ptr null in %s", __func__);
            break;
        }

        if (!db_ptr->Open()) {
            LOG_E(kLogTag, "db open failed in %s", __func__);
            break;
        }

        do {
            {
                {
                    // do integrity check
                    const bool delete_on_failure = true;
                    const auto integrity_status = db_ptr->IntegrityCheck(delete_on_failure);
                    if (!integrity_status) {
                        LOG_E(kLogTag, "IntegrityCheck failed in %s", __func__);

                        db_ptr = std::make_unique<PersistenceDB>(driver_login_db_path);

                        if (!db_ptr) {
                            LOG_E(kLogTag, "db_ptr null after recreating in %s", __func__);
                            break;
                        }

                        if (!db_ptr->Open()) {
                            LOG_E(kLogTag, "db open failed after recreating in %s", __func__);
                            break;
                        }
                    }
                }
            }

            {
                // create main table
                const auto ret_pair = db_ptr->ExecuteCommand(create_main_table_str, nullptr, nullptr);
                if (SQLITE_OK != ret_pair.first) {
                    LOG_E(kLogTag, "ExecuteCommand for : %s failed: %d - %s",
                         create_main_table_str.c_str(), ret_pair.first, ret_pair.second.c_str());
                    break;
                }
            }

            {
                std::string driver_ids = getLegacyDriverDataStr(driver_data);

                std::ostringstream insert_data_query;
                insert_data_query << "INSERT INTO " << kDriverLoginTableName
                                  << " (DRVID, START_TIME, END_TIME) VALUES ("
                                  << "'" << driver_ids << "'" << ","
                                  << "'" << std::to_string(driver_data.start_time_) << "'" << ","
                                  << "'" << std::to_string(driver_data.end_time_) << "'" << ");";

                // Add data to LOGIN table
                const auto ret_pair = db_ptr->ExecuteCommand(insert_data_query.str(), nullptr, nullptr);
                if (SQLITE_OK != ret_pair.first) {
                    LOG_E(kLogTag, "ExecuteCommand for : %s failed: %d - %s",
                         insert_data_query.str().c_str(), ret_pair.first, ret_pair.second.c_str());
                    break;
                }
            }

            LOG_I(kLogTag, "Data added successfully in %s", __func__);
            status = true;
        } while (false);

        db_ptr->Close();

    } while (false);

    // Remove session entry from session table
    // return add status

    {
        const std::lock_guard<std::mutex> lock(data_mutex_);
        data_ready_ = true;
        data_cv_.notify_all();
    }

    return status;
}

bool BtPersistenceDBHelper::StartProcessing() {
    bool status = false;
    {
        const std::lock_guard<std::mutex> lock(api_mutex_);

        if (process_thread_th_.joinable()) {
            LOG_I(kLogTag, "ProcessLoop Thread is already running");
            status = true;
        } else {
            try {
                process_thread_th_ = std::thread(&BtPersistenceDBHelper::ProcessLoop, this);
                status = true;
            } catch (const std::system_error &e) {
                LOG_E(kLogTag, "[%s:%d] System error what(): %s", __FUNCTION__, __LINE__, e.what());
            } catch (...) {
                LOG_E(kLogTag, "[%s:%d] Caught an exception of an undetermined type", __FUNCTION__, __LINE__);
            }
        }
    }

    return status;
}

bool BtPersistenceDBHelper::StopProcessing() {
    const std::lock_guard<std::mutex> api_lock(api_mutex_);
    if (process_thread_th_.joinable()) {
        {
            const std::lock_guard<std::mutex> data_lock(data_mutex_);
            exit_requested_ = true;
            data_cv_.notify_all();
        }

        process_thread_th_.join();
        LOG_I(kLogTag, "ProcessLoop Thread joined");
    } else {
        // already stopped?
        LOG_E(kLogTag, "Looks like thread is already stopped or not running in %s", __func__);
    }
    return true;
}


// BtAdsmStateManager Implementation
//
// Usage Example:
// -------------
// // Create state manager with DB file path
// const std::string state_db_path = std::string(kBtDbFolderPath) + kAdsmStateDbName;
// BtAdsmStateManager state_mgr(state_db_path);
//
// // Load existing state from DB (or initialize if not exists)
// if (state_mgr.LoadState()) {
//     LOG_I(kLogTag, "State loaded successfully");
// }
//
// // Update state
// state_mgr.SetCurrentHash("abc123");
// state_mgr.AddDriver("driver_001", timestamp);
// state_mgr.AddDriver("driver_002", timestamp);
// state_mgr.SetUptime(12345);
// state_mgr.SetReminderAudioPlaybackCount(state_mgr.GetReminderAudioPlaybackCount() + 1);
//
// // Or update all at once
// std::unordered_map<std::string, int64_t> drivers = {{"driver_001", ts1}, {"driver_002", ts2}};
// state_mgr.UpdateState("abc123", drivers, uptime, audio_count);
//
// // Save state to DB
// if (state_mgr.SaveState()) {
//     LOG_I(kLogTag, "State saved successfully");
// }
//
// // Read state
// LOG_I(kLogTag, "Current hash: %s, drivers: %zu, uptime: %lld, audio_count: %u",
//       state_mgr.GetCurrentHash().c_str(), state_mgr.GetCurrentDrivers().size(),
//       state_mgr.GetUptime(), state_mgr.GetReminderAudioPlaybackCount());

BtAdsmStateManager::BtAdsmStateManager(const std::string &state_file_path)
    : state_file_path_(state_file_path) {
    // Initialize state with default values

    const auto bt_factory_ptr = BluetoothFactory::GetInstance();

    if (bt_factory_ptr) {
        // Dummy signal handler, the object is already created in nd_bt_man.cpp
        service_ptr_ = bt_factory_ptr->GetServiceObj("", [](int signum) {});
    }

    LOG_I(kLogTag, "BtAdsmStateManager initialized with path: %s", state_file_path_.c_str());
}

BtAdsmStateManager::~BtAdsmStateManager() {
    LOG_I(kLogTag, "BtAdsmStateManager destroyed");
}

// Getters
const std::string& BtAdsmStateManager::GetCurrentHash() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return state_.current_hash_;
}

const std::unordered_map<std::string, int64_t>& BtAdsmStateManager::GetCurrentDrivers() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return state_.current_drivers_;
}

int BtAdsmStateManager::GetIgnStatus() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return state_.ign_status_;
}

bool BtAdsmStateManager::GetWakeUpStatus() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return state_.wake_up_status_;
}

uint64_t BtAdsmStateManager::GetAudioCount() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return state_.audio_count_;
}

const std::string& BtAdsmStateManager::GetBootId() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return state_.boot_id_;
}

int64_t BtAdsmStateManager::GetLastUpdated() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return state_.last_updated_;
}

// Setters
void BtAdsmStateManager::SetCurrentHash(const std::string& hash) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_.current_hash_ = hash;
}

void BtAdsmStateManager::SetCurrentDrivers(const std::unordered_map<std::string, int64_t>& drivers) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_.current_drivers_ = drivers;
}

void BtAdsmStateManager::SetIgnStatus(int status) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_.ign_status_ = status;
}

void BtAdsmStateManager::SetWakeUpStatus(bool status) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_.wake_up_status_ = status;
}

void BtAdsmStateManager::SetAudioCount(uint64_t count) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_.audio_count_ = count;
}

void BtAdsmStateManager::SetBootId(const std::string& boot_id) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    state_.boot_id_ = boot_id;
}

// Get entire state
const BtAdsmState BtAdsmStateManager::GetState() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return state_;
}

bool BtAdsmStateManager::LoadState() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    LOG_I(kLogTag, "Entered %s", __func__);

    bool status = false;

    do {
        if (!PersistenceUtils::CheckPersistenceDir(kNDHomePath)) {
            LOG_E(kLogTag, "CheckPersistenceDir failed for path: %s", kNDHomePath);
            break;
        }

        PersistenceDB db(state_file_path_);

        if (!db.Open()) {
            LOG_E(kLogTag, "db.Open failed in %s", __func__);
            if (!service_ptr_->send_err_msg(SM_E_BTFV_ADSM_FAIL, static_cast<int>(AdsmErrCode::kOpenFail),
                                            "ADSM File open failed")) {
                LOG_E(kLogTag, "SM_E_BTFV_ADSM_FAIL send_err_msg failed");
            }
            DeleteStateFile(state_file_path_);
            break;
        }

        do {
            {
                // do integrity check
                const bool delete_on_failure = true;
                const auto integrity_status = db.IntegrityCheck(delete_on_failure);
                if (!integrity_status) {
                    LOG_E(kLogTag, "IntegrityCheck failed in %s", __func__);
                    if (!service_ptr_->send_err_msg(SM_E_BTFV_ADSM_FAIL, static_cast<int>(AdsmErrCode::kIntegrityFail),
                                            "ADSM File integrity failed during load")) {
                        LOG_E(kLogTag, "SM_E_BTFV_ADSM_FAIL send_err_msg failed");
                    }
                    break;
                }
            }

            {
                // create table if not exists
                const std::string create_table_str = getAdsmStateTableCreateQuery();
                constexpr int max_retries = 2;
                constexpr int retry_delay_ms = 500;
                bool success = false;

                for (int attempt = 0; attempt < max_retries; ++attempt) {
                    if (attempt > 0) {
                        LOG_E(kLogTag, "ExecuteCommand retry attempt %d/%d after %dms delay",
                            attempt + 1, max_retries, retry_delay_ms);
                        std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay_ms));
                    }

                    const auto ret_pair = db.ExecuteCommand(create_table_str, nullptr, nullptr);
                    if (SQLITE_OK == ret_pair.first) {
                        success = true;
                        break;
                    }

                    LOG_E(kLogTag, "ExecuteCommand for : %s failed: %d - %s",
                        create_table_str.c_str(), ret_pair.first, ret_pair.second.c_str());
                }

                if (!success) {
                    if (!service_ptr_->send_err_msg(SM_E_BTFV_ADSM_FAIL, static_cast<int>(AdsmErrCode::kReadFail),
                                            "ADSM File create table failed during load")) {
                        LOG_E(kLogTag, "SM_E_BTFV_ADSM_FAIL send_err_msg failed");
                    }
                    break;
                }
            }

            {
                // read state from DB as JSON document
                auto callback = [](void *cb_data, int argc, char **argv, char **cols) -> int {
                    int ret_value = -1;
                    if ((4 == argc) && (nullptr != argv[1])) {
                        BtAdsmState &state = *(reinterpret_cast<BtAdsmState *>(cb_data));
                        std::string state_json = argv[1];

                        if (state_json.empty()) {
                            LOG_W(kLogTag, "Empty state JSON in DB");
                            return 0;
                        }

                        json_error_t error{};
                        json_t *root = json_loads(state_json.c_str(), 0, &error);
                        if (nullptr == root) {
                            LOG_E(kLogTag, "Error parsing state JSON: line %d, column %d: %s",
                                  error.line, error.column, error.text);
                            return ret_value;
                        }

                        // Extract all scalar and boolean values using json_unpack_ex
                        const char *hash_cstr = nullptr;
                        const char *boot_id_cstr = nullptr;
                        int ign_status = 0;
                        int wake = 0;
                        json_int_t audio_count = 0;

                        // Unpack with optional fields (using '?' suffix for optional)
                        if (json_unpack_ex(root, &error, 0,
                            "{s?s, s?i, s?b, s?I, s?s}",
                            "current_hash", &hash_cstr,
                            "ign_status", &ign_status,
                            "wake_up_status", &wake,
                            "audio_count", &audio_count,
                            "boot_id", &boot_id_cstr) != 0) {
                            LOG_W(kLogTag, "json_unpack_ex failed: %s (line %d, col %d)",
                                  error.text, error.line, error.column);
                            json_decref(root);
                            return ret_value;
                        }

                        // Assign unpacked values to state
                        if (hash_cstr) {
                            state.current_hash_ = hash_cstr;
                        }

                        if (boot_id_cstr) {
                            state.boot_id_ = boot_id_cstr;
                        }

                        state.ign_status_ = ign_status;
                        state.wake_up_status_ = (wake != 0);
                        state.audio_count_ = static_cast<uint64_t>(audio_count);

                        // Parse current_drivers (must be done manually as it's dynamic)
                        json_t *drivers_json = json_object_get(root, "current_drivers");
                        if (drivers_json && json_is_object(drivers_json)) {
                            state.current_drivers_.clear();
                            const char *driver_key;
                            json_t *driver_value;
                            json_object_foreach(drivers_json, driver_key, driver_value) {
                                if (json_is_integer(driver_value)) {
                                    int64_t timestamp = json_integer_value(driver_value);
                                    state.current_drivers_[driver_key] = timestamp;
                                    LOG_I(kLogTag, "Loaded driver: %s -> %lld", driver_key, timestamp);
                                }
                            }
                            LOG_I(kLogTag, "Total drivers loaded: %zu", state.current_drivers_.size());
                        }

                        json_decref(root);
                        ret_value = 0;

                        // Read LAST_UPDATED timestamp (argv[3])
                        int64_t last_updated = 0;
                        if ((nullptr != argv[3]) && string_to_int64(argv[3], last_updated) && (last_updated >= 0)) {
                            state.last_updated_ = last_updated;
                        } else {
                            state.last_updated_ = 0;
                            if (nullptr != argv[3]) {
                                if (last_updated < 0) {
                                    LOG_W(kLogTag, "Invalid negative last_updated value: %lld in %s", last_updated, __func__);
                                } else {
                                    LOG_W(kLogTag, "string_to_int64 failed for last_updated in %s", __func__);
                                }
                            }
                        }

                        // Log session data
                        LOG_I(kLogTag, "State loaded - Session: hash=%s, drivers_count=%zu",
                              state.current_hash_.c_str(), state.current_drivers_.size());

                        // Log vehicle state
                        LOG_I(kLogTag, "State loaded - Vehicle: ign=%d, wake=%d",
                              state.ign_status_, state.wake_up_status_);
                    } else {
                        LOG_E(kLogTag, "Invalid callback argc: %d", argc);
                    }
                    return ret_value;
                };

                const std::string command = std::string("SELECT * FROM '") + kAdsmStateTableName + "' WHERE ID = 1;";
                const auto ret_pair = db.ExecuteCommand(command, callback, &state_);
                if (SQLITE_OK != ret_pair.first) {
                    LOG_E(kLogTag, "ExecuteCommand for : %s failed: %d - %s",
                        command.c_str(), ret_pair.first, ret_pair.second.c_str());
                    if (!service_ptr_->send_err_msg(SM_E_BTFV_ADSM_FAIL, static_cast<int>(AdsmErrCode::kReadFail),
                                            "ADSM File read failed")) {
                        LOG_E(kLogTag, "SM_E_BTFV_ADSM_FAIL send_err_msg failed");
                    }
                    break;
                }
            }

            LOG_I(kLogTag, "State load success in %s", __func__);
            status = true;
        } while (false);

        db.Close();

    } while (false);

    return status;
}

bool BtAdsmStateManager::SaveState() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    LOG_I(kLogTag, "Entered %s", __func__);

    bool status = false;

    do {
        if (!PersistenceUtils::CheckPersistenceDir(kNDHomePath)) {
            LOG_E(kLogTag, "CheckPersistenceDir failed for path: %s", kNDHomePath);
            break;
        }

        PersistenceDB db(state_file_path_);

        if (!db.Open()) {
            LOG_E(kLogTag, "db.Open failed in %s", __func__);
            if (!service_ptr_->send_err_msg(SM_E_BTFV_ADSM_FAIL, static_cast<int>(AdsmErrCode::kOpenFail),
                                                "ADSM File creation failed")) {
                LOG_E(kLogTag, "SM_E_BTFV_ADSM_FAIL send_err_msg failed");
            }
            DeleteStateFile(state_file_path_);
            break;
        }

        do {
            {
                // do integrity check
                const bool delete_on_failure = true;
                const auto integrity_status = db.IntegrityCheck(delete_on_failure);
                if (!integrity_status) {
                    LOG_E(kLogTag, "IntegrityCheck failed in %s", __func__);
                    if (!service_ptr_->send_err_msg(SM_E_BTFV_ADSM_FAIL, static_cast<int>(AdsmErrCode::kIntegrityFail),
                                                "ADSM File integrity failed")) {
                        LOG_E(kLogTag, "SM_E_BTFV_ADSM_FAIL send_err_msg failed");
                    }
                    break;
                }
            }

            {
                // create table if not exists
                const std::string create_table_str = getAdsmStateTableCreateQuery();
                constexpr int max_retries = 2;
                constexpr int retry_delay_ms = 500;
                bool success = false;

                for (int attempt = 0; attempt < max_retries; ++attempt) {
                    if (attempt > 0) {
                        LOG_E(kLogTag, "ExecuteCommand retry attempt %d/%d after %dms delay",
                            attempt + 1, max_retries, retry_delay_ms);
                        std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay_ms));
                    }

                    const auto ret_pair = db.ExecuteCommand(create_table_str, nullptr, nullptr);
                    if (SQLITE_OK == ret_pair.first) {
                        success = true;
                        break;
                    }

                    LOG_E(kLogTag, "ExecuteCommand for : %s failed: %d - %s",
                        create_table_str.c_str(), ret_pair.first, ret_pair.second.c_str());
                }

                if (!success) {
                    if (!service_ptr_->send_err_msg(SM_E_BTFV_ADSM_FAIL, static_cast<int>(AdsmErrCode::kWriteFail),
                                                "ADSM File create table failed")) {
                        LOG_E(kLogTag, "SM_E_BTFV_ADSM_FAIL send_err_msg failed");
                    }
                    break;
                }
            }

            {
                // Build current_drivers as nested object (must be done separately as json_pack doesn't support dynamic objects)
                json_t *drivers_obj = json_object();
                if (nullptr == drivers_obj) {
                    LOG_E(kLogTag, "json_object failed for drivers in %s", __func__);
                    break;
                }
                for (const auto &driver_entry : state_.current_drivers_) {
                    if (json_object_set_new(drivers_obj, driver_entry.first.c_str(), json_integer(driver_entry.second)) != 0) {
                        LOG_E(kLogTag, "json_object_set_new failed for driver entry in %s", __func__);
                        json_decref(drivers_obj);
                        break;
                    }
                }

                // Serialize entire state to single JSON document using json_pack_ex
                json_error_t error{};
                json_t *state_json = json_pack_ex(&error, 0,
                    "{s:s, s:o, s:i, s:b, s:I, s:s}",
                    "current_hash", state_.current_hash_.c_str(),
                    "current_drivers", drivers_obj,  // 'o' steals the reference
                    "ign_status", state_.ign_status_,
                    "wake_up_status", state_.wake_up_status_,
                    "audio_count", (json_int_t)state_.audio_count_,
                    "boot_id", state_.boot_id_.c_str()
                );

                if (nullptr == state_json) {
                    LOG_E(kLogTag, "json_pack_ex failed in %s: %s (line %d, col %d)",
                          __func__, error.text, error.line, error.column);
                    // drivers_obj was already freed by json_pack_ex on failure (it doesn't steal on error)
                    json_decref(drivers_obj);
                    break;
                }

                // Convert to string
                char *state_json_str = json_dumps(state_json, JSON_COMPACT);
                json_decref(state_json);  // Free state_json immediately after dumping

                if (nullptr == state_json_str) {
                    LOG_E(kLogTag, "json_dumps failed in %s", __func__);
                    break;
                }

                // Get current timestamp
                uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();

                // Insert or replace state in DB as single row
                std::ostringstream oss;
                oss << "INSERT OR REPLACE INTO " << kAdsmStateTableName
                    << " (ID, STATE_JSON, VERSION, LAST_UPDATED) VALUES ("
                    << "1, '" << state_json_str << "', 1, " << now << ");";

                free(state_json_str);

                const auto ret_pair = db.ExecuteCommand(oss.str(), nullptr, nullptr);
                if (SQLITE_OK != ret_pair.first) {
                    LOG_E(kLogTag, "ExecuteCommand for : %s failed: %d - %s",
                          oss.str().c_str(), ret_pair.first, ret_pair.second.c_str());
                    if (!service_ptr_->send_err_msg(SM_E_BTFV_ADSM_FAIL, static_cast<int>(AdsmErrCode::kWriteFail),
                                                "ADSM File Data write failed")) {
                        LOG_E(kLogTag, "SM_E_BTFV_ADSM_FAIL send_err_msg failed");
                    }
                    break;
                }

                LOG_I(kLogTag, "State saved: hash=%s, drivers_count=%zu, ign_status=%d, wake_up_status=%d, audio_count=%llu, boot_id=%s",
                      state_.current_hash_.c_str(), state_.current_drivers_.size(),
                      state_.ign_status_, state_.wake_up_status_, state_.audio_count_, state_.boot_id_.c_str());
            }

            LOG_I(kLogTag, "State save success in %s", __func__);
            status = true;
        } while (false);

        db.Close();

    } while (false);

    return status;
}

bool BtAdsmStateManager::DeleteStateFile(const std::string& state_file_path) {
    LOG_I(kLogTag, "Attempting to delete state file: %s", state_file_path.c_str());

    bool status = false;

    do {
        if (state_file_path.empty()) {
            LOG_E(kLogTag, "Empty state file path provided");
            break;
        }

        try {
            if (boost::filesystem::exists(state_file_path)) {
                if (boost::filesystem::remove(state_file_path)) {
                    LOG_I(kLogTag, "Successfully deleted state file: %s", state_file_path.c_str());
                    status = true;
                } else {
                    LOG_W(kLogTag, "Failed to delete state file: %s", state_file_path.c_str());
                }
            } else {
                LOG_W(kLogTag, "State file does not exist: %s", state_file_path.c_str());
                status = true;  // Consider non-existent file as success
            }
        } catch (const boost::filesystem::filesystem_error& e) {
            LOG_W(kLogTag, "Exception while deleting state file: %s - %s", state_file_path.c_str(), e.what());
            break;
        }
    } while (false);

    return status;
}

bool BtAdsmStateManager::CopyStateFile(const std::string& source_path, const std::string& dest_path) {
    bool status = false;

    do {
        if (source_path.empty() || dest_path.empty()) {
            LOG_E(kLogTag, "Empty source or destination path provided");
            break;
        }

        try {
            if (!boost::filesystem::exists(source_path)) {
                LOG_E(kLogTag, "Source file does not exist: %s", source_path.c_str());
                break;
            }

            if (!boost::filesystem::is_regular_file(source_path)) {
                LOG_E(kLogTag, "Source is not a regular file: %s", source_path.c_str());
                break;
            }

            // Create destination directory if it doesn't exist
            boost::filesystem::path dest_file_path(dest_path);
            boost::filesystem::path dest_dir = dest_file_path.parent_path();

            if (!dest_dir.empty() && !boost::filesystem::exists(dest_dir)) {
                if (!boost::filesystem::create_directories(dest_dir)) {
                    LOG_E(kLogTag, "Failed to create destination directory: %s", dest_dir.c_str());
                    break;
                }
            }

            // Copy file, overwriting if it exists
            boost::filesystem::copy_file(source_path, dest_path,
                                        boost::filesystem::copy_option::overwrite_if_exists);

            LOG_I(kLogTag, "Successfully copied state file from %s to %s",
                  source_path.c_str(), dest_path.c_str());
            status = true;

        } catch (const boost::filesystem::filesystem_error& e) {
            LOG_E(kLogTag, "Exception while copying state file from %s to %s: %s",
                  source_path.c_str(), dest_path.c_str(), e.what());
            break;
        }
    } while (false);

    return status;
}

bool BtAdsmStateManager::MoveStateFile(const std::string& source_path, const std::string& dest_path) {
    bool status = false;

    do {
        if (source_path.empty() || dest_path.empty()) {
            LOG_E(kLogTag, "Empty source or destination path provided");
            break;
        }

        try {
            if (!boost::filesystem::exists(source_path)) {
                LOG_E(kLogTag, "Source file does not exist: %s", source_path.c_str());
                break;
            }

            if (!boost::filesystem::is_regular_file(source_path)) {
                LOG_E(kLogTag, "Source is not a regular file: %s", source_path.c_str());
                break;
            }

            // Create destination directory if it doesn't exist
            boost::filesystem::path dest_file_path(dest_path);
            boost::filesystem::path dest_dir = dest_file_path.parent_path();

            if (!dest_dir.empty() && !boost::filesystem::exists(dest_dir)) {
                if (!boost::filesystem::create_directories(dest_dir)) {
                    LOG_E(kLogTag, "Failed to create destination directory: %s", dest_dir.c_str());
                    break;
                }
            }

            // Remove destination file if it exists
            if (boost::filesystem::exists(dest_path)) {
                boost::filesystem::remove(dest_path);
                LOG_I(kLogTag, "Removed existing destination file: %s", dest_path.c_str());
            }

            // Move/rename file
            boost::filesystem::rename(source_path, dest_path);

            LOG_I(kLogTag, "Successfully moved state file from %s to %s",
                  source_path.c_str(), dest_path.c_str());
            status = true;

        } catch (const boost::filesystem::filesystem_error& e) {
            LOG_E(kLogTag, "Exception while moving state file from %s to %s: %s",
                  source_path.c_str(), dest_path.c_str(), e.what());
            break;
        }
    } while (false);

    return status;
}

// FileSystemHandler Implementation

bool FileSystemHandler::FileExists(const std::string& file_path) {
    bool status = false;

    try {
        if (!file_path.empty() && boost::filesystem::exists(file_path)) {
            status = true;
        }
    } catch (const boost::filesystem::filesystem_error& e) {
        LOG_W(kLogTag, "Exception in FileExists for %s: %s", file_path.c_str(), e.what());
    }

    return status;
}

bool FileSystemHandler::DirectoryExists(const std::string& dir_path) {
    bool status = false;

    try {
        if (!dir_path.empty() && boost::filesystem::exists(dir_path) &&
            boost::filesystem::is_directory(dir_path)) {
            status = true;
        }
    } catch (const boost::filesystem::filesystem_error& e) {
        LOG_W(kLogTag, "Exception in DirectoryExists for %s: %s", dir_path.c_str(), e.what());
    }

    return status;
}

bool FileSystemHandler::CreateDirectories(const std::string& dir_path) {
    bool status = false;

    try {
        if (!dir_path.empty()) {
            if (boost::filesystem::exists(dir_path)) {
                if (boost::filesystem::is_directory(dir_path)) {
                    status = true;
                } else {
                    LOG_E(kLogTag, "Path exists but is not a directory: %s", dir_path.c_str());
                }
            } else {
                if (boost::filesystem::create_directories(dir_path)) {
                    LOG_I(kLogTag, "Created directory: %s", dir_path.c_str());
                    status = true;
                } else {
                    LOG_E(kLogTag, "Failed to create directory: %s", dir_path.c_str());
                }
            }
        } else {
            LOG_E(kLogTag, "Empty directory path provided");
        }
    } catch (const boost::filesystem::filesystem_error& e) {
        LOG_W(kLogTag, "Exception in CreateDirectories for %s: %s", dir_path.c_str(), e.what());
    }

    return status;
}

bool FileSystemHandler::DeleteFile(const std::string& file_path) {
    bool status = false;

    try {
        if (!file_path.empty()) {
            if (boost::filesystem::exists(file_path)) {
                if (boost::filesystem::remove(file_path)) {
                    LOG_I(kLogTag, "Successfully deleted file: %s", file_path.c_str());
                    status = true;
                } else {
                    LOG_W(kLogTag, "Failed to delete file: %s", file_path.c_str());
                }
            } else {
                LOG_W(kLogTag, "File does not exist: %s", file_path.c_str());
                status = true;  // Consider non-existent file as success
            }
        } else {
            LOG_E(kLogTag, "Empty file path provided");
        }
    } catch (const boost::filesystem::filesystem_error& e) {
        LOG_W(kLogTag, "Exception in DeleteFile for %s: %s", file_path.c_str(), e.what());
    }

    return status;
}

bool FileSystemHandler::CopyFile(const std::string& source_path, const std::string& dest_path) {
    bool status = false;

    try {
        if (!source_path.empty() && !dest_path.empty()) {
            if (boost::filesystem::exists(source_path)) {
                if (boost::filesystem::is_regular_file(source_path)) {
                    // Create destination directory if needed
                    boost::filesystem::path dest_file_path(dest_path);
                    boost::filesystem::path dest_dir = dest_file_path.parent_path();
                    bool can_proceed = true;

                    if (!dest_dir.empty() && !boost::filesystem::exists(dest_dir)) {
                        if (!boost::filesystem::create_directories(dest_dir)) {
                            LOG_E(kLogTag, "Failed to create destination directory: %s", dest_dir.c_str());
                            can_proceed = false;
                        }
                    }

                    if (can_proceed) {
                        // Copy file, overwriting if it exists
                        boost::filesystem::copy_file(source_path, dest_path,
                                                    boost::filesystem::copy_option::overwrite_if_exists);

                        LOG_I(kLogTag, "Successfully copied file from %s to %s",
                              source_path.c_str(), dest_path.c_str());
                        status = true;
                    }
                } else {
                    LOG_E(kLogTag, "Source is not a regular file: %s", source_path.c_str());
                }
            } else {
                LOG_E(kLogTag, "Source file does not exist: %s", source_path.c_str());
            }
        } else {
            LOG_E(kLogTag, "Empty source or destination path provided");
        }
    } catch (const boost::filesystem::filesystem_error& e) {
        LOG_E(kLogTag, "Exception in CopyFile from %s to %s: %s",
              source_path.c_str(), dest_path.c_str(), e.what());
    }

    return status;
}

bool FileSystemHandler::MoveFile(const std::string& source_path, const std::string& dest_path) {
    bool status = false;

    try {
        if (!source_path.empty() && !dest_path.empty()) {
            if (boost::filesystem::exists(source_path)) {
                if (boost::filesystem::is_regular_file(source_path)) {
                    // Create destination directory if needed
                    boost::filesystem::path dest_file_path(dest_path);
                    boost::filesystem::path dest_dir = dest_file_path.parent_path();
                    bool can_proceed = true;

                    if (!dest_dir.empty() && !boost::filesystem::exists(dest_dir)) {
                        if (!boost::filesystem::create_directories(dest_dir)) {
                            LOG_E(kLogTag, "Failed to create destination directory: %s", dest_dir.c_str());
                            can_proceed = false;
                        }
                    }

                    if (can_proceed) {
                        // Remove destination file if it exists
                        if (boost::filesystem::exists(dest_path)) {
                            boost::filesystem::remove(dest_path);
                            LOG_I(kLogTag, "Removed existing destination file: %s", dest_path.c_str());
                        }

                        // Try to move/rename file directly
                        try {
                            boost::filesystem::rename(source_path, dest_path);
                            LOG_I(kLogTag, "Successfully moved file from %s to %s",
                                  source_path.c_str(), dest_path.c_str());
                            status = true;
                        } catch (const boost::filesystem::filesystem_error& rename_error) {
                            // If rename fails (e.g., cross-device), fallback to copy + delete
                            LOG_W(kLogTag, "Rename failed (likely cross-device), falling back to copy+delete: %s",
                                  rename_error.what());

                            try {
                                // Copy file
                                boost::filesystem::copy_file(source_path, dest_path,
                                                            boost::filesystem::copy_option::overwrite_if_exists);

                                // Delete source file after successful copy
                                if (boost::filesystem::remove(source_path)) {
                                    LOG_I(kLogTag, "Successfully moved file (via copy+delete) from %s to %s",
                                          source_path.c_str(), dest_path.c_str());
                                    status = true;
                                } else {
                                    LOG_E(kLogTag, "Failed to delete source file after copy: %s", source_path.c_str());
                                    // Copy succeeded but delete failed - still consider it a partial success
                                    // The file is at destination but also remains at source
                                }
                            } catch (const boost::filesystem::filesystem_error& copy_error) {
                                LOG_E(kLogTag, "Copy fallback also failed: %s", copy_error.what());
                            }
                        }
                    }
                } else {
                    LOG_E(kLogTag, "Source is not a regular file: %s", source_path.c_str());
                }
            } else {
                LOG_E(kLogTag, "Source file does not exist: %s", source_path.c_str());
            }
        } else {
            LOG_E(kLogTag, "Empty source or destination path provided");
        }
    } catch (const boost::filesystem::filesystem_error& e) {
        LOG_E(kLogTag, "Exception in MoveFile from %s to %s: %s",
              source_path.c_str(), dest_path.c_str(), e.what());
    }

    return status;
}

bool FileSystemHandler::IsRegularFile(const std::string& file_path) {
    bool status = false;

    try {
        if (!file_path.empty() && boost::filesystem::exists(file_path) &&
            boost::filesystem::is_regular_file(file_path)) {
            status = true;
        }
    } catch (const boost::filesystem::filesystem_error& e) {
        LOG_E(kLogTag, "Exception in IsRegularFile for %s: %s", file_path.c_str(), e.what());
    }

    return status;
}

}  // namespace device

}  // namespace nd
