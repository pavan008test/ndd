/* Copyright (C) 2022 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, October 2022
 */

#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <functional>
#include <fstream>
#include <limits>
#include <list>
#include <vector>

#include <jansson/jansson.h>
#include <jwt-cpp/base.h>

#include <config_parser.h>
#include <log.h>
#include <nd_curl_helper.h>
#include <nd_msg_types.h>
#include <nd_msg_utils.h>
#include <nd_persistence_helper.h>
#include <nd_sam.h>
#include <nd_sam_constants.h>
#include <nd_security_crypt.h>
#include <nd_security_data.h>
#include <nd_security_shadow.h>
#include <nd_timer.h>
#include <service_utils.h>
#include <system_utils.h>

namespace nd {

namespace security {

static const char *const kLogTag = "ND_SAM";

// ========= Constants =========
// Message Queue names
static const char *const kSAM_Server_MQ_Name = "MQ_SAM";
static const char *const kAWS_MQ_Name        = "AWSIOT";
// Config files
// const char * const kDeviceConfigFile = "/home/ubuntu/config/deviceconfig.ini";
static const char *const kSamConfigFile         = "/home/ubuntu/.nddevice/latest/sam_config.ini";

static constexpr uint64_t kDefaultEventRetryInterval = 60; // 60 seconds
static constexpr uint64_t kDefaultPassChangeInterval = 24; // 24 hours

static const char *const kSecretRegistrationApi = "device/secret-key";

static constexpr size_t kRandomKeySize(32);

// Sample sam ini file
    // [version]
    // security_version = 0.0.1

    // [pass]
    // uname = ubuntu
    // interval_s = 2592000
    // rotate = false

    // [common]
    // event_retry_interval_s = 60

// helper functions

static void SyncCounterWithIssueFile(uint64_t counter) {
    const char *const issue = "/etc/issue";
    const char *const count_key = "count";

    std::ifstream ifs(issue, std::fstream::in);
    if (ifs.is_open()) {
        LOG_I(kLogTag, "issue file opened");
        std::list<std::string> contents;
        std::string line;
        while (getline(ifs, line)) {
            // do not consider if count line is present
            if (std::string::npos == line.find(count_key)) {
                contents.emplace_back(line);
            }
            line.clear();
        }
        ifs.close();
        std::ofstream ofs(issue, std::ios::out | std::ios::trunc);
        if (ofs.is_open()) {
            for (const auto &line_out : contents) {
                ofs << line_out << "\n";
            }
            // append the new count value at the end
            ofs << count_key << ": " << counter << "\n";
            ofs.flush();
            ofs.close();
        } else {
            LOG_E(kLogTag, "failed to open issue file for writing");
        }
    } else {
        LOG_E(kLogTag, "failed to open issue file for reading");
    }
}

static bool IsStringAlphanumeric(const std::string &data) {
    return std::all_of(data.cbegin(), data.cend(), ::isalnum);
}

static bool ExecuteCommandInChild(const std::string &path, const std::string &child_stdin, const std::vector<char *> &child_args) {
    LOG_I(kLogTag, "Inside %s", __func__);
    bool status = false;
    int fds[2] = {};

    if (-1 == pipe(fds)) {
        LOG_E(kLogTag, "Error creating pipes: %d:%s", errno, strerror(errno));
        return status;
    }

    const pid_t pid = fork();

    if (0 < pid) {
        LOG_I(kLogTag, "Entered parent");
        // parent
        close(fds[0]);

        // const char * pass = "ubuntu:EKM2800123Netra";

        if (static_cast<ssize_t>(child_stdin.size()) != write(fds[1], child_stdin.c_str(), child_stdin.size())) {
            LOG_E(kLogTag, "WRITE ERROR PIPE: %d:%s", errno, strerror(errno));
        }

        close(fds[1]);
        int child_status = -1;

        do {
            const pid_t ret_pid = waitpid(pid, &child_status, 0);
            if (ret_pid == pid) {
                if (0 == child_status) {
                    status = true;
                    LOG_I(kLogTag, "child ended successfully");
                } else {
                    LOG_E(kLogTag, "child ended in failure %d", child_status);
                }
                break;

            } else {
                LOG_E(kLogTag, "waitpid received for: %d", ret_pid);
            }
        } while(true);

    } else if (0 == pid) {
        LOG_I(kLogTag, "Entered Child");
        close(fds[1]);
        if (STDIN_FILENO != dup2(fds[0], STDIN_FILENO)) {
            LOG_E(kLogTag, "dup2 error to stdin child");
        }
        // child
        // char *binary_path = "/usr/sbin/chpasswd";
        // char *const child_args[] = {const_cast<char *> (path.c_str()), NULL};
        const auto ret_code = execv(path.c_str(), child_args.data());
        LOG_E(kLogTag, "execv error: %d", ret_code);
    } else {
        LOG_E(kLogTag, "create child error");
    }
    return status;
}

static std::string ConvertUcharsToString(const std::vector<unsigned char> &bytes) {
    static const char * const hex_digits = "0123456789ABCDEF";
    std::string hex_str;
    std::for_each(bytes.begin(), bytes.end(), [&hex_str](unsigned char one_byte) {
                    hex_str.push_back(hex_digits[one_byte >> 4]);
                    hex_str.push_back(hex_digits[one_byte & 0x0F]);
                });
    return hex_str;
}

static uint8_t HexToNibble(char hex) {
    hex = ::toupper(hex);

    if (hex >= 'A' && hex <= 'F') { // A-F case
        return 10 + (hex - 'A');
    } else { // 0-9 case
        return hex - '0';
    }
}

static std::vector<unsigned char> ConvertStringToBytes(const std::string &in_hex_str) {
    std::vector<unsigned char> binary_data;
    if (in_hex_str.size() % 2 == 0) {
        binary_data.resize(in_hex_str.size() / 2 , 0);

        size_t length = in_hex_str.length();
        size_t pos = 0;
        size_t opos = 0;

        while (pos < length && opos < binary_data.size()) {
            uint8_t c1 = HexToNibble(in_hex_str.at(pos++));
            uint8_t c2 = HexToNibble(in_hex_str.at(pos++));
            binary_data[opos++] = (c1 << 4) | c2;
        }
    }

    return binary_data;
}

AuthModule::AuthModule() = default;

AuthModule::~AuthModule() {
    StopPassTimeoutThread();
    StopRetryEventTimeoutThread();
}

bool AuthModule::InitMQ() {
    bool status = false;
    server_mq_ = nd_msgq_t::get_msgq(kSAM_Server_MQ_Name, nd_msgq_t::ND_MSGQ_SERVER);
    if (nullptr != server_mq_) {
        status = true;
        LOG_I(kLogTag, "SAM_Server_MQ created successfully");
    } else {
        LOG_E(kLogTag, "SAM_Server_MQ creation failed");
    }
    return status;
}

bool AuthModule::ReportDataToHealthStats(uint64_t counter, uint64_t changed_at, uint64_t elapsed_interval) {
    LOG_I(kLogTag, "Entered %s", __func__);

    bool status = false;
    char *json_data = nullptr;
    // timestamp, counter, elapsed time, interval
    json_t *root = json_object();
    json_t *sam_counter_info = json_object();
    if ((nullptr != root) && (nullptr != sam_counter_info)) {
        auto const now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        json_object_set_new(sam_counter_info, "ts", json_integer(now));
        json_object_set_new(sam_counter_info, "ct", json_integer(counter));
        json_object_set_new(sam_counter_info, "at", json_integer(changed_at * 1000)); //converting to milliseconds for consistency in HS
        json_object_set_new(sam_counter_info, "et", json_integer(elapsed_interval));
        json_object_set_new(sam_counter_info, "ri", json_integer(config_data_->rotation_interval_));
        json_object_set_new(root, "isArray", json_string("true"));
        json_object_set_new(root, "sam_cnt_info", sam_counter_info);
        json_data = json_dumps(root, 0);
        json_decref(root);
        LOG_I(kLogTag, "Health payload created");
    } else {
        LOG_E(kLogTag, "json pointer null, Low on memory?");
        json_decref(root);
        json_decref(sam_counter_info);
    }

    if (nullptr != json_data) {
        if (service_obj_ptr_->send_msg_healthstats(json_data, strlen(json_data))) {
            LOG_I(kLogTag, "Data reported successfully to HealthStats");
            RetryEventClear(RetryEventKind::kHealthReport);
            status = true;
        } else {
            LOG_E(kLogTag, "Data failed to be reported to HealthStats");
            RetryEvent(RetryEventKind::kHealthReport);
        }
        free(json_data);
    } else {
        LOG_E(kLogTag, "json_data is null, something wrong");
    }

    return status;
}

bool AuthModule::SyncCounterWithCloud(uint64_t counter) {
    bool status = false;
    sam_pass_counter_sync_msg_t counter_msg{};
    counter_msg.counter_ = counter;
    if (send_msg((generic_msg_t *)&counter_msg, SYNC_COUNTER_WITH_IOT, sizeof(counter_msg),
                            kSAM_Server_MQ_Name, kAWS_MQ_Name, msg_id_++)) {
        status = true;
        LOG_I(kLogTag, "SYNC_COUNTER_WITH_IOT message sent successfully, counter: %llu", counter);
    } else {
        RetryEvent(RetryEventKind::kCounterSync);
        LOG_E(kLogTag, "SYNC_COUNTER_WITH_IOT failed to be sent, counter: %llu", counter);
    }
    return status;
}

bool AuthModule::RegisterSecretKeyWithCloud(const std::string &key) const {
    bool status = false;

    LOG_I(kLogTag, "Entered %s", __func__);
    nd::utils::CurlHelper curl;
    const std::string data = "{\"secret\":\"" + key + "\",\"counter\":1}";
    const auto status_ptr = curl.Call(kSecretRegistrationApi, data);

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
            LOG_I(kLogTag, "Response success for secret registration");
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

bool AuthModule::GetNewSecretKey(std::string &hex_key) const {
    bool status(false);
    std::vector<unsigned char> rbytes(kRandomKeySize);

    // Generate random key
    unsigned long err_code = 0;

    if (RandomBytes::Get(rbytes, err_code)) {
        // convert to hex
        hex_key = ConvertUcharsToString(rbytes);
        status = true;
    } else {
        LOG_E(kLogTag, "RandomBytes::Get() failed");
    }
    return status;
}

bool AuthModule::PersistConfidentialData(std::unique_ptr<SamPwConfidentialData> con_data_ptr) const {
    bool status = false;
    if (PersistenceEncryptedDBHelper::StoreSamPwConfidentialData(std::move(con_data_ptr))) {
        status = true;
        LOG_I(kLogTag, "PersistenceEncryptedDBHelper::StoreSamPwConfidentialData success");
    } else {
        LOG_E(kLogTag, "PersistenceEncryptedDBHelper::StoreSamPwConfidentialData failed");
    }
    return status;
}

bool AuthModule::PersistGenericData(std::unique_ptr<SamPwGenericData> gen_data_ptr) const {
    bool status = false;
    if (PersistenceDBHelper::StoreSamPwGenericData(std::move(gen_data_ptr))) {
        status = true;
        LOG_I(kLogTag, "PersistenceDBHelper::StoreSamPwGenericData success");
    } else {
        LOG_E(kLogTag, "PersistenceDBHelper::StoreSamPwGenericData failed");
    }
    return status;
}

void AuthModule::ClearPersistenceEntries() const {
    LOG_I(kLogTag, "Entered %s", __func__);
    if (!PersistenceEncryptedDBHelper::ClearSamPwConfidentialData()) {
        LOG_E(kLogTag, "PersistenceEncryptedDBHelper::ClearSamPwConfidentialData failed");
    }

    if (!PersistenceDBHelper::ClearSamGenericData()) {
        LOG_E(kLogTag, "PersistenceDBHelper::ClearSamGenericData failed");
    }
}

bool AuthModule::ReadPersistedConfidentialData(std::unique_ptr<SamPwConfidentialData> &ptr) const {
    bool status = false;
    auto data_ptr = PersistenceEncryptedDBHelper::ReadSamPwConfidentialData();
    if (data_ptr) {
        ptr = std::move(data_ptr);
        status = true;
        LOG_I(kLogTag, "PersistenceEncryptedDBHelper::ReadSamPwConfidentialData success");
    } else {
        LOG_E(kLogTag, "PersistenceEncryptedDBHelper::ReadSamPwConfidentialData failed");
    }
    return status;
}

bool AuthModule::ReadPersistedGenericData(std::unique_ptr<SamPwGenericData> &ptr) const {
    bool status = false;
    auto data_ptr = PersistenceDBHelper::ReadSamPwGenericData();
    if (data_ptr) {
        ptr = std::move(data_ptr);
        status = true;
        LOG_I(kLogTag, "AuthModule::ReadPersistedGenericData success");
    } else {
        LOG_E(kLogTag, "AuthModule::ReadPersistedGenericData failed");
    }
    return status;
}

bool AuthModule::ReadConfigData() {
    bool status = false;
    LOG_I(kLogTag, "Entered %s", __func__);
    const char *const kConfigParamEnable  = "1";
    const char *const kConfigParamDisable = "0";

    do {
        config_data_ = std::unique_ptr<ConfigData>(new (std::nothrow) ConfigData());
        if (config_data_) {
            {
                // Get device id
                Config_parser device_conf(nd::constants::kDeviceConfigFile);
                if (device_conf.getParseStatus()) {
                    const std::string device_id = device_conf.getConfig("identity", "deviceId", "");
                    if (!device_id.empty()) {
                        LOG_I(kLogTag, "device_id: %s", device_id.c_str());
                        config_data_->device_id_ = std::move(device_id);
                    } else {
                        LOG_E(kLogTag, "device_id is empty");
                        break;
                    }
                } else {
                    LOG_E(kLogTag, "Unable to parse %s", nd::constants::kDeviceConfigFile);
                    break;
                }
            }

            {
                // Get version, rotation switch, interval, user_name, event_retry_interval
                Config_parser sam_conf(kSamConfigFile);
                bool is_val_overridden = false;
                // setting default rotation interval
                config_data_->rotation_interval_= kDefaultPassChangeInterval;
                if (sam_conf.getParseStatus()) {
                    {
                        std::string interval_str = sam_conf.getConfig("sam", "pass_interval_h",
                                                                      std::to_string(kDefaultPassChangeInterval),
                                                                      true, is_val_overridden);

                        int64_t interval_hours = 0;
                        if (string_to_int64(interval_str, interval_hours)) {
                            LOG_I(kLogTag, "interval_hours: %lld", interval_hours);
                        } else {
                            interval_hours = 0;
                            LOG_E(kLogTag, "string_to_int64 failed for interval_str from sam_conf %s", interval_str.c_str());
                        }

                        if (static_cast<int64_t>(kDefaultPassChangeInterval) > interval_hours) {
                            LOG_E(kLogTag, "invalid interval_hours from sam_conf, using default %llu", kDefaultPassChangeInterval);
                            interval_hours = kDefaultPassChangeInterval;
                        }

                        //TODO(sunil): should we do max check here ?
                        config_data_->rotation_interval_= interval_hours * 3600U; // convert hours to seconds

                        if (kConfigParamEnable == sam_conf.getConfig("sam", "pass_rotate", kConfigParamEnable, true, is_val_overridden)) {
                            config_data_->rotate_switch_ = true;
                            LOG_I(kLogTag, "pass_rotate is true");
                        }
                    }

                    config_data_->user_name_ = sam_conf.getConfig("sam", "uname", "", true, is_val_overridden);

                    if (config_data_->user_name_.empty()) {
                        LOG_E(kLogTag, "uname empty in config");
                        // error
                        break;
                    }

                    config_data_->version_ = sam_conf.getConfig("sam", "version", "", true, is_val_overridden);

                    {
                        std::string retry_interval_str = sam_conf.getConfig("sam", "event_retry_interval_s",
                                                                            std::to_string(kDefaultEventRetryInterval), true,
                                                                            is_val_overridden);

                        int64_t retry_interval = 0;
                        if (string_to_int64(retry_interval_str, retry_interval)) {
                            if (0 >= retry_interval) {
                                LOG_E(kLogTag, "invalid retry_interval from sam_conf");
                            } else {
                                //TODO(sunil): should we do max check here ?
                                config_data_->event_retry_interval_= retry_interval;
                                LOG_I(kLogTag, "event_retry_interval: %lld", retry_interval);
                            }
                        } else {
                            LOG_E(kLogTag, "string_to_int64 failed for retry_interval_str from sam_conf %s", retry_interval_str.c_str());
                        }

                        if (config_data_->event_retry_interval_ <= 0) {
                            config_data_->event_retry_interval_ = kDefaultEventRetryInterval;
                            LOG_E(kLogTag, "event_retry_interval_ is made %llu since value read is not valid", kDefaultEventRetryInterval);
                        }
                    }

                    {
                        if (kConfigParamEnable == sam_conf.getConfig("sam", "enabled", kConfigParamDisable, true, is_val_overridden)) {
                            config_data_->service_enable_ = true;
                            LOG_I(kLogTag, "enable_service is true");
                        }

                        std::string audit_log_str = sam_conf.getConfig("sam", "audit_log_enabled", kConfigParamDisable, true, is_val_overridden);

                        int64_t audit_log_level = 0;
                        if (string_to_int64(audit_log_str, audit_log_level)) {
                            if (0 >= audit_log_level) {
                                LOG_E(kLogTag, "invalid audit_log_level from sam_conf: %lld", audit_log_level);
                            } else {
                                //TODO(sunil): should we do max check here ?
                                config_data_->audit_log_level_= audit_log_level;
                                LOG_I(kLogTag, "audit_log_level: %lld", audit_log_level);
                            }
                        } else {
                            LOG_E(kLogTag, "string_to_int64 failed for audit_log_enabled from sam_conf %s", audit_log_str.c_str());
                        }
                    }

                } else {
                    LOG_E(kLogTag, "Unable to parse %s", kSamConfigFile);
                    break;
                }
            }
            status = true;
            LOG_I(kLogTag, "ReadConfigData success");
        } else {
            // ptr null
            LOG_E(kLogTag, "config_data_ null");
            break;
        }
    } while (false);

    return status;
}

bool AuthModule::GeneratePassword(const std::string &secret, const std::string &data, std::string &pass_out) const {
    LOG_I(kLogTag, "Entered %s", __func__);
    bool status = false;

    const auto bytes = ConvertStringToBytes(secret);

    const auto uchar_vec_hash = HMAC256Evp::Compute(bytes.data(), bytes.size(),
                                                    reinterpret_cast<const unsigned char *>(data.c_str()), data.size());
    if (!uchar_vec_hash.empty()) {

        constexpr size_t pass_length = 16; // password length
        const std::string hash_str(uchar_vec_hash.begin(), uchar_vec_hash.end());
        const std::string pass_url = jwt::base::trim<jwt::alphabet::base64url>(jwt::base::encode<jwt::alphabet::base64url>(hash_str));
        pass_out = pass_url.substr(0, pass_length);

        status = true;
        LOG_I(kLogTag, "Password generated");
    } else {
        LOG_E(kLogTag, "uchar_vec_hash is empty");
    }

    return status;
}

bool AuthModule::GetNewSecretAndRegisterWithCloud(std::string &secret_key) const {
    LOG_I(kLogTag, "Entered %s", __func__);

    bool status = false;
    std::string random_key;
    if (GetNewSecretKey(random_key)) {
        if (RegisterSecretKeyWithCloud(random_key)) {
            secret_key = std::move(random_key);
            status = true;
        } else {
            // failure, retry again after sometime ?
            LOG_E(kLogTag, "RegisterSecretKeyWithCloud failed");
        }
    } else {
        LOG_E(kLogTag, "GetNewSecretKey failed");
    }
    return status;
}

bool AuthModule::GenerateAndApplyPassword(const std::string &secret_key, uint64_t counter) const {
    LOG_I(kLogTag, "Entered %s", __func__);

    bool status = false;
    const std::string data = config_data_->device_id_ + "-" + std::to_string(counter);
    std::string pass;

    if (GeneratePassword(secret_key, data, pass)) {
        if (ApplyPassword(pass)) {
            const std::string err_msg = "Password changed successfully with counter: " + std::to_string(counter);
            if (!service_obj_ptr_->send_err_msg(SM_E_SAM_PASS_CHANGE,
                                                static_cast<int>(PassErrCode::kOk),
                                                err_msg.c_str())) {
                LOG_E(kLogTag, "SM_E_SAM_PASS_CHANGE kOk send_err_msg failed");
            }
            status = true;
        } else {
            LOG_E(kLogTag, "ApplyPassword failed");
            if (!service_obj_ptr_->send_err_msg(SM_E_SAM_PASS_CHANGE,
                                                static_cast<int>(PassErrCode::kPassChangeError),
                                                "Password change failed")) {
                LOG_E(kLogTag, "SM_E_SAM_PASS_CHANGE kPassChangeError send_err_msg failed");
            }
        }
    } else {
        LOG_E(kLogTag, "GeneratePassword failed");
    }
    return status;
}

bool AuthModule::ChangePassword() {
    LOG_I(kLogTag, "Entered %s", __func__);

    bool status = false;
    bool has_secret_changed = false;

    std::unique_ptr<SamPwConfidentialData> con_data_ptr = nullptr;
    std::unique_ptr<SamPwGenericData> gen_data_ptr = nullptr;

    // if key exists, use it with counter, also check for overflow of counter
    if (ReadPersistedConfidentialData(con_data_ptr) && IsConfidentialDataValid(con_data_ptr) &&
        ReadPersistedGenericData(gen_data_ptr) && IsGenericDataValid(gen_data_ptr)) {

        LOG_I(kLogTag, "ReadPersistedConfidentialData & ReadPersistedGenericData, incrementing counter");

        using namespace std::chrono;

        const uint64_t current_time = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
        gen_data_ptr->last_change_at_ = current_time;
        gen_data_ptr->elapsed_interval_ = 0;
        ++(gen_data_ptr->counter_);

        status = true;

    } else {

        LOG_I(kLogTag, "Either ReadPersistedConfidentialData or ReadPersistedGenericData "
                       "should have failed or the counter may be out of limits");

        has_secret_changed = true;
        // Failure on read from persistence
        con_data_ptr = std::unique_ptr<SamPwConfidentialData>(new (std::nothrow) SamPwConfidentialData());
        gen_data_ptr = std::unique_ptr<SamPwGenericData>(new (std::nothrow) SamPwGenericData());

        if (con_data_ptr && gen_data_ptr) {
            if (GetNewSecretAndRegisterWithCloud(con_data_ptr->secret_key_)) {
                using namespace std::chrono;

                const uint64_t current_time = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
                con_data_ptr->last_change_at_ = current_time;
                gen_data_ptr->last_change_at_ = current_time;
                gen_data_ptr->elapsed_interval_ = 0;
                gen_data_ptr->counter_ = 1; // reset counter to 1

                LOG_I(kLogTag, "New secret registered, resetting the counter to 1");

                status = true;
            } else {
                LOG_E(kLogTag, "GetNewSecretAndRegisterWithCloud failed");
            }
        } else {
            LOG_E(kLogTag, "either con_data_ptr or gen_data_ptr is null");
        }
    }

    if (status) {
        if (GenerateAndApplyPassword(con_data_ptr->secret_key_, gen_data_ptr->counter_)) {

            if (config_data_->rotate_switch_) {
                StopPassTimeoutThread();
                ResetPassInEffectTimer();
                StartPassTimeoutThread();
            }

            SyncCounterWithCloud(gen_data_ptr->counter_);
            ReportDataToHealthStats(gen_data_ptr->counter_, gen_data_ptr->last_change_at_, gen_data_ptr->elapsed_interval_);

            if (has_secret_changed) {
                LOG_I(kLogTag, "Secret has changed, persisting it");
                PersistConfidentialData(std::move(con_data_ptr));
            }

            SyncCounterWithIssueFile(gen_data_ptr->counter_);
            LOG_I(kLogTag, "Password change, persisting generic data");
            PersistGenericData(std::move(gen_data_ptr));
        } else {
            status = false;
            LOG_E(kLogTag, "GenerateAndApplyPassword failed");
        }
    }

    if (!status) {
        LOG_E(kLogTag, "PasswordChange event to be retried later");
        RetryEvent(RetryEventKind::kPasswordChange);
    } else {
        // if lower priority retry event set then cancel it else retain the timer
        LOG_I(kLogTag, "PasswordChange to be cleared");
        RetryEventClear(RetryEventKind::kPasswordChange);
    }

    return status;
}

bool AuthModule::ApplyPassword(const std::string &pass) const {
    LOG_I(kLogTag, "Entered %s", __func__);
    if (0 < config_data_->audit_log_level_) {
        LOG_I(kLogTag, "Password to apply: %s", pass.c_str());
    }

    bool status = false;
    const std::string path = "/usr/sbin/chpasswd";
    // const std::string user = "ubuntu"; // krait has root user

    // "ubuntu:EKM2800123Netra"
    // const std::string uname_pass = user + ':' + pass;
    const std::string uname_pass = config_data_->user_name_ + ':' + pass;

    if (ExecuteCommandInChild(path, uname_pass, {const_cast<char *>(path.c_str()), nullptr})) {
        LOG_I(kLogTag, "ExecuteCommandInChild success");
        if (ShadowUtil::DoesUserPasswordMatch(config_data_->user_name_, pass)) {
            LOG_I(kLogTag, "DoesUserPasswordMatch success");
            status = true;
        } else {
            LOG_E(kLogTag, "DoesUserPasswordMatch failed");
        }
    }
    return status;
}

bool AuthModule::Init() {
    LOG_I(kLogTag, "Entered %s", __func__);

    bool status = false;
    do {
        service_obj_ptr_ = NDService::get_service_obj("nd_sam");
        if (nullptr == service_obj_ptr_) {
            LOG_E(kLogTag, "service_obj_ptr_ is null");
            break;
        }

        if (!ReadConfigData()) {
            LOG_E(kLogTag, "ReadConfigData failed");
            if (!service_obj_ptr_->send_err_msg(SM_E_SAM_INIT_FAIL, static_cast<int>(InitErrCode::kConfigFail),
                                                "Config Read failed")) {
                LOG_E(kLogTag, "SM_E_SAM_INIT_FAIL send_err_msg failed");
            }
            break;
        }

        if (!config_data_->service_enable_) {
            LOG_E(kLogTag, "Service not enabled");
            if (!service_obj_ptr_->send_err_msg(SM_E_SAM_INIT_FAIL, static_cast<int>(InitErrCode::kServiceDisabled),
                                                "Service disabled")) {
                LOG_E(kLogTag, "SM_E_SAM_INIT_FAIL send_err_msg failed");
            }
            break;
        }

        if(!ShadowUtil::DoesUserExistWithPassword(config_data_->user_name_)) {
            LOG_E(kLogTag, "DoesUserExistWithPassword failed");
            if (!service_obj_ptr_->send_err_msg(SM_E_SAM_INIT_FAIL, static_cast<int>(InitErrCode::kInvalidUser),
                                                "Invalid user")) {
                LOG_E(kLogTag, "SM_E_SAM_INIT_FAIL send_err_msg failed");
            }
            break;
        }

        if (!InitMQ()) {
            LOG_E(kLogTag, "InitMQ failed");
            if (!service_obj_ptr_->send_err_msg(SM_E_SAM_INIT_FAIL, static_cast<int>(InitErrCode::kMQServerFail),
                                                "MQ server fail")) {
                LOG_E(kLogTag, "SM_E_SAM_INIT_FAIL send_err_msg failed");
            }
            break;
        }

        if (!PersistenceUtils::CheckPersistenceDir(nd::constants::kSamDbFolderPath)) {
            LOG_E(kLogTag, "CheckPersistenceDir failed");
            if (!service_obj_ptr_->send_err_msg(SM_E_SAM_INIT_FAIL, static_cast<int>(InitErrCode::kPersistenceFail),
                                                "Persistence fail")) {
                LOG_E(kLogTag, "SM_E_SAM_INIT_FAIL send_err_msg failed");
            }
            break;
        }

        status = true;
        LOG_I(kLogTag, "Init success");
    } while (false);

    return status;
}

void AuthModule::ResetPassInEffectTimer() {
    LOG_I(kLogTag, "Resetting pass_in_effect_interval");
    pass_in_effect_interval_ = 0U;
}

void AuthModule::PasswordTimerTickCB(uint64_t steady_now, uint64_t system_now) {
    LOG_I(kLogTag, "%s called", __func__);

    pass_in_effect_interval_ += kTick_interval_;
    if (!PersistenceDBHelper::UpdateDbWithPassInEffectTime(kTick_interval_)) {
        LOG_E(kLogTag, "UpdateDbWithPassInEffectTime failed");
    }

    if (config_data_->rotation_interval_ <= pass_in_effect_interval_) {
        // trigger self message to change password
        LOG_I(kLogTag, "Password timeout cb triggered at: %llu", system_now);

        generic_msg_t gen_msg{};
        if (!send_msg((generic_msg_t *)&gen_msg, PASSWORD_CHANGE_TIMEOUT, sizeof(gen_msg),
                      kSAM_Server_MQ_Name, kSAM_Server_MQ_Name, msg_id_++)) {
            LOG_E(kLogTag, "PASSWORD_CHANGE_TIMEOUT msg failed to be sent");
        }
    } else {
        const int64_t interval = config_data_->rotation_interval_ - pass_in_effect_interval_;
        LOG_I(kLogTag, "Password remaining time: %lld as on: %llu", interval, system_now);
    }
}

bool AuthModule::StartPassTimeoutThread() {
    LOG_I(kLogTag, "Entered %s", __func__);

    bool status = false;
    if (password_tick_) {
        status = true;
    } else {
        password_tick_.reset(new (std::nothrow) nd::utils::TimerTick());
        if (password_tick_) {
            auto timer_cb = [&](uint64_t steady_now, uint64_t system_now) {
                this->PasswordTimerTickCB(steady_now, system_now);
            };
            password_tick_->RegisterCB(timer_cb);
            password_tick_->SetInterval(kTick_interval_);
            password_tick_->Start();
            status = true;
            LOG_I(kLogTag, "PasswordTimerTickCB, thread created");
        } else {
            LOG_E(kLogTag, "password_tick_ is null");
        }
    }

    return status;
}

bool AuthModule::StopPassTimeoutThread() {
    LOG_I(kLogTag, "Entered %s", __func__);

    bool status(false);
    if (password_tick_) {
        status = password_tick_->Stop();
        password_tick_.reset(nullptr);
    } else {
        LOG_E(kLogTag, "password_tick_ is null");
    }
    ResetPassInEffectTimer();
    return status;
}

void AuthModule::Run() {
    // init message queue either here or call separately
    // ReadPersistedGenericData for last pass time
    // Generate and Apply pass if current time is > last pass time + interval
    // create timer thread for remainder time, once triggered, create timer for next timer interval
    // go to message loop

    LOG_I(kLogTag, "Entered %s", __func__);

    std::unique_ptr<SamPwGenericData> gen_data_ptr = nullptr;
    if (ReadPersistedGenericData(gen_data_ptr) && IsGenericDataValid(gen_data_ptr)) {

        if (0 < config_data_->audit_log_level_) {
            LOG_I(kLogTag, "Values from Generic sam DB - counter: %llu, changed at: %llu, elapsed time: %llu",
                  gen_data_ptr->counter_, gen_data_ptr->last_change_at_, gen_data_ptr->elapsed_interval_);
        }

        LOG_I(kLogTag, "On start, sync counter");
        pass_in_effect_interval_ = gen_data_ptr->elapsed_interval_;
        SyncCounterWithCloud(gen_data_ptr->counter_);
        ReportDataToHealthStats(gen_data_ptr->counter_, gen_data_ptr->last_change_at_, gen_data_ptr->elapsed_interval_);
        if (config_data_->rotate_switch_) {
            StartPassTimeoutThread();
        }

        std::unique_ptr<SamPwConfidentialData> con_data_ptr = nullptr;

        if (0 < config_data_->audit_log_level_) {
            if (ReadPersistedConfidentialData(con_data_ptr) && IsConfidentialDataValid(con_data_ptr)) {
                LOG_I(kLogTag, "Values from confidential DB - secret: %s, changed at: %llu",
                      con_data_ptr->secret_key_.c_str(), con_data_ptr->last_change_at_);
            } else {
                LOG_E(kLogTag, "ReadPersistedConfidentialData failed on start");
            }
        }

        SyncCounterWithIssueFile(gen_data_ptr->counter_);
    } else {
        LOG_I(kLogTag, "Persistent generic files missing? Apply password");
        ChangePassword(); //TODO(sunil.s): Check if this has to be done by default on start ?
    }

    // if everything is success then start message loop
    MsgLoop();
}

void AuthModule::MsgLoop() {
    LOG_I(kLogTag, "Entered %s", __func__);

    nd_msgq_t::nd_msg_t *msg = nullptr;

    while (true) {
        if ((msg = server_mq_->receive()) == nullptr) {
            LOG_E(kLogTag, "Receive message failed" );
            continue;
        }

        generic_msg_t *g_msg = reinterpret_cast<generic_msg_t *>(msg->get_buffer());
        if (nullptr == g_msg) {
            LOG_E(kLogTag, "msg->get_buffer() returned NULL");
            continue;
        }

        switch(g_msg->msg_type) {
            case GET_CURRENT_COUNTER: {
                LOG_I(kLogTag, "Received GET_CURRENT_COUNTER msg from: %s", g_msg->client_id);
                NotifyCurrentCounter(g_msg->client_id);
                break;
            }

            case ON_DEMAND_SECRET_CHANGE : {
                LOG_I(kLogTag, "Received ON_DEMAND_SECRET_CHANGE msg from: %s", g_msg->client_id);
                OnSecretChangeRequest(g_msg->client_id);
                break;
            }

            case PASSWORD_CHANGE_TIMEOUT: {
                LOG_I(kLogTag, "Received PASSWORD_CHANGE_TIMEOUT msg from: %s", g_msg->client_id);
                OnPasswordChangeRequest(g_msg->client_id);
                break;
            }

            case ON_DEMAND_PASS_CHANGE : {
                LOG_I(kLogTag, "Received ON_DEMAND_PASS_CHANGE msg from: %s", g_msg->client_id);
                OnPasswordChangeRequest(g_msg->client_id);
                break;
            }

            case RETRY_EVENT_TIMEOUT : {
                LOG_I(kLogTag, "Received RETRY_EVENT_TIMEOUT msg from: %s", g_msg->client_id);
                OnRetryEventTimeout(g_msg->client_id);
                break;
            }

            case SYNC_COUNTER_RESPONSE : {
                LOG_I(kLogTag, "Received SYNC_COUNTER_RESPONSE msg from: %s", g_msg->client_id);
                sam_pass_counter_sync_resp_msg_t *resp_msg = reinterpret_cast<sam_pass_counter_sync_resp_msg_t *>(g_msg);
                OnIotCounterSyncResponse(resp_msg->client_id, resp_msg->status_);
                break;
            }

            default : {
                LOG_I(kLogTag, "Entered default, received %d msg from: %s", g_msg->msg_type, g_msg->client_id);
                break;
            }
        }

        delete msg;
    }
}

void AuthModule::RetryEventTimerTickCB(uint64_t steady_now, uint64_t system_now) {
    elapsed_retry_event_interval_ += kTick_interval_;

    if (config_data_->event_retry_interval_ <= elapsed_retry_event_interval_) {
        elapsed_retry_event_interval_ = 0;
        LOG_I(kLogTag, "Retry Event cb triggered at: %llu", system_now);
        generic_msg_t gen_msg{};
        if (!send_msg((generic_msg_t *)&gen_msg, RETRY_EVENT_TIMEOUT, sizeof(gen_msg),
                     kSAM_Server_MQ_Name, kSAM_Server_MQ_Name, msg_id_++)) {
            LOG_E(kLogTag, "RETRY_EVENT_TIMEOUT msg failed to be sent");
        }
    } else {
        const int64_t interval = config_data_->event_retry_interval_ - elapsed_retry_event_interval_;
        LOG_I(kLogTag, "Retry Event cb remaining time: %lld as on: %llu", interval, system_now);
    }
}

bool AuthModule::StopRetryEventTimeoutThread() {
    LOG_I(kLogTag, "Entered %s", __func__);
    bool status(false);
    if (event_notifier_tick_) {
        status = event_notifier_tick_->Stop();
        event_notifier_tick_.reset(nullptr);
        LOG_I(kLogTag, "event_notifier_tick stop response: %d", status);
    } else {
        LOG_E(kLogTag, "event_notifier_tick_ is null");
    }
    elapsed_retry_event_interval_ = 0;
    return status;
}

void AuthModule::ResetEventForRetry(RetryEventKind event) {
    LOG_I(kLogTag, "Entered %s, event to clear: %d", __func__, static_cast<int>(event));

    if (!events_to_retry_.empty()) {
        if (0 < events_to_retry_.erase(event)) {
            LOG_I(kLogTag, "Event: %d cleared", static_cast<int>(event));
        } else {
            LOG_I(kLogTag, "No such event: %d", static_cast<int>(event));
        }
    } else {
        LOG_I(kLogTag, "No retry events set to clear");
    }
}

bool AuthModule::RetryEvent(RetryEventKind event) {
    LOG_I(kLogTag, "Entered %s, event to retry: %d", __func__, static_cast<int>(event));
    SetEventForRetry(event);
    return StartRetryEventTimeoutThread();
}

void AuthModule::RetryEventClearAll() {
    events_to_retry_.clear();
    StopRetryEventTimeoutThread();
}

bool AuthModule::RetryEventClear(RetryEventKind event) {
    LOG_I(kLogTag, "Entered %s", __func__);
    bool status = false;

    ResetEventForRetry(event);

    if (events_to_retry_.empty()) {
        status = StopRetryEventTimeoutThread();
        LOG_I(kLogTag, "StopRetryEventTimeoutThread status: %d", status);
    }
    return status;
}

void AuthModule::SetEventForRetry(RetryEventKind event) {
    LOG_I(kLogTag, "Entered %s, event to retry: %d", __func__, static_cast<int>(event));

    events_to_retry_.emplace(event);
    LOG_I(kLogTag, "events_to_retry size: %u", events_to_retry_.size());
}

bool AuthModule::StartRetryEventTimeoutThread() {
    LOG_I(kLogTag, "Entered %s", __func__);

    bool status = false;
    if (event_notifier_tick_) {
        LOG_I(kLogTag, "event_notifier_tick_ already started");
        status = true;
    } else {
        event_notifier_tick_.reset(new (std::nothrow) nd::utils::TimerTick());
        if (event_notifier_tick_) {
            auto timer_cb = [&](uint64_t steady_now, uint64_t system_now) {
                this->RetryEventTimerTickCB(steady_now, system_now);
            };
            event_notifier_tick_->RegisterCB(timer_cb);
            event_notifier_tick_->SetInterval(kTick_interval_);
            event_notifier_tick_->Start();
            status = true;
            LOG_I(kLogTag, "event_notifier_tick_ started successfully");
        } else {
            LOG_E(kLogTag, "event_notifier_tick_ is null, low on memory?");
        }
    }

    return status;
}

void AuthModule::OnIotCounterSyncResponse(const std::string &client, bool status) {
    LOG_I(kLogTag, "Entered %s, counter sync response: %d", __func__, status);

    if (status) {
        RetryEventClear(RetryEventKind::kCounterSync);
    } else {
        RetryEvent(RetryEventKind::kCounterSync);
    }

    // notifier_tick_
    // if failure, check if thread not created then create one
    // if success, end a thread if created earlier
}

void AuthModule::OnSecretChangeRequest(const std::string &client) {
    LOG_I(kLogTag, "Entered %s", __func__);

    bool status(false);
    // clear current db entries and stop time thread since we have to change the secret
    StopPassTimeoutThread();
    ClearPersistenceEntries();

    std::unique_ptr<SamPwConfidentialData> con_data_ptr =
                                    std::unique_ptr<SamPwConfidentialData>(new (std::nothrow) SamPwConfidentialData());
    std::unique_ptr<SamPwGenericData> gen_data_ptr =
                                    std::unique_ptr<SamPwGenericData>(new (std::nothrow) SamPwGenericData());

    if (con_data_ptr && gen_data_ptr) {
        // check client
        std::string pass;
        if (GetNewSecretAndRegisterWithCloud(con_data_ptr->secret_key_)) {
            gen_data_ptr->counter_ = 1;
            if (GenerateAndApplyPassword(con_data_ptr->secret_key_, gen_data_ptr->counter_)) {
                using namespace std::chrono;
                // password successfully changed
                const uint64_t current_time = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
                con_data_ptr->last_change_at_ = current_time;
                gen_data_ptr->last_change_at_ = current_time;
                gen_data_ptr->elapsed_interval_ = 0;

                SyncCounterWithCloud(gen_data_ptr->counter_);
                ReportDataToHealthStats(gen_data_ptr->counter_, gen_data_ptr->last_change_at_, gen_data_ptr->elapsed_interval_);
                SyncCounterWithIssueFile(gen_data_ptr->counter_);
                PersistConfidentialData(std::move(con_data_ptr));
                PersistGenericData(std::move(gen_data_ptr));

                if (config_data_->rotate_switch_) {
                    LOG_I(kLogTag, "rotate switch is true, starting timeout thread");
                    ResetPassInEffectTimer();
                    StartPassTimeoutThread();
                } else {
                    LOG_I(kLogTag, "rotate switch is false, is this \"ON demand request\"?");
                }

                status = true;
            }
        }
    } else {
        LOG_E(kLogTag, "con_data_ptr or gen_data_ptr is null, low on memory?");
    }
    if (!status) {
        RetryEvent(RetryEventKind::kSecretChange);
    } else {
        RetryEventClear(RetryEventKind::kSecretChange);
    }
}

void AuthModule::OnRetryEventTimeout(const std::string &client) {
    LOG_I(kLogTag, "Entered %s", __func__);

    const auto retry_event_itr = events_to_retry_.cbegin();

    // Secret change and Password change are considered as higher priority events.
    // Whereas, Counter sync and Health report are lower priority events.
    // In case there is a higher priority event in the set then the remaining events such as
    // countersync, health status will happen eventually during the course of its operation flow.
    // If the top event itself is low priority events then the remaining low priority events have to be initialted forcibly
    // since they would not have any relaed flow.

    // RetryEvent
    if (events_to_retry_.cend() != retry_event_itr) {
        const auto event = *retry_event_itr;
        switch(event) {
            case RetryEventKind::kSecretChange : {
                // Since this is the higher priority event, clear other pass change events if set
                RetryEventClear(RetryEventKind::kPasswordChange);
                // RetryEventClear(RetryEventKind::kCounterSync);
                OnSecretChangeRequest(client);
                break;
            }

            case RetryEventKind::kPasswordChange : {
                // RetryEventClear(RetryEventKind::kCounterSync);
                OnPasswordChangeRequest(client);
                break;
            }

            case RetryEventKind::kCounterSync : {
                std::unique_ptr<SamPwGenericData> gen_data_ptr = nullptr;
                if (ReadPersistedGenericData(gen_data_ptr) && IsGenericDataValid(gen_data_ptr)) {
                    SyncCounterWithCloud(gen_data_ptr->counter_);
                    if (0 != events_to_retry_.count(RetryEventKind::kHealthReport)) {
                        ReportDataToHealthStats(gen_data_ptr->counter_, gen_data_ptr->last_change_at_,
                                                gen_data_ptr->elapsed_interval_);
                    }
                } else {
                    LOG_E(kLogTag, "ReadPersistedGenericData failed");
                }
                break;
            }

            case RetryEventKind::kHealthReport : {
                std::unique_ptr<SamPwGenericData> gen_data_ptr = nullptr;
                if (ReadPersistedGenericData(gen_data_ptr) && IsGenericDataValid(gen_data_ptr)) {
                    ReportDataToHealthStats(gen_data_ptr->counter_, gen_data_ptr->last_change_at_, gen_data_ptr->elapsed_interval_);
                } else {
                    LOG_E(kLogTag, "ReadPersistedGenericData failed");
                }
                break;
            }

            default: {
                LOG_E(kLogTag, "Entered default : OnRetryEventTimeout");
                RetryEventClearAll();
                break;
            }
        }
    } else {
        RetryEventClearAll();
    }
}

void AuthModule::OnPasswordChangeRequest(const std::string &client) {
    LOG_I(kLogTag, "Entered %s", __func__);
    ChangePassword();
}

void AuthModule::NotifyCurrentCounter(const std::string &client) {
    LOG_I(kLogTag, "Entered %s", __func__);

    std::unique_ptr<SamPwGenericData> gen_data_ptr = nullptr;
    if (ReadPersistedGenericData(gen_data_ptr) && IsGenericDataValid(gen_data_ptr)) {
        sam_pass_counter_notify_msg_t counter_msg{};
        counter_msg.counter_ = gen_data_ptr->counter_;
        if (!send_msg((generic_msg_t *)&counter_msg, NOTIFY_CURRENT_COUNTER, sizeof(counter_msg),
                      kSAM_Server_MQ_Name, client, msg_id_++)) {
            LOG_E(kLogTag, "NOTIFY_CURRENT_COUNTER msg failed to be sent to: %s", client.c_str());
        }
    } else {
        LOG_E(kLogTag, "ReadPersistedGenericData failed in NotifyCurrentCounter");
    }
}

bool AuthModule::IsConfidentialDataValid(std::unique_ptr<SamPwConfidentialData> &ptr) {
    // Key when stored as string doubles its size: ConvertUcharsToString
    return (IsStringAlphanumeric(ptr->secret_key_) && ((kRandomKeySize * 2) == ptr->secret_key_.size()));
}

bool AuthModule::IsGenericDataValid(std::unique_ptr<SamPwGenericData> &ptr) {
    return (ptr->counter_ < std::numeric_limits<uint64_t>::max() - 1);
}

}  // namespace security

}  // namespace nd
