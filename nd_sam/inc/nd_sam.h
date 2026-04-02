/* Copyright (C) 2022 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, October 2022
 */

#ifndef INC_ND_SECURITY_AUTH_MODULE_H_
#define INC_ND_SECURITY_AUTH_MODULE_H_

#include <atomic>
#include <memory>
#include <set>

class nd_msgq_t;
class NDService;

namespace nd {
    namespace utils {
        class TimerTick;
    }

    namespace security {
        struct SamPwConfidentialData;
        struct SamPwGenericData;
    }
}

namespace nd {

namespace security {

class AuthModule {
 public:
    AuthModule();
    AuthModule(const AuthModule &) = default;
    AuthModule(AuthModule &&) = default;
    AuthModule &operator=(const AuthModule &) = default;
    AuthModule & operator=(AuthModule &&) = default;
    ~AuthModule();

    bool Init();
    void Run();

 private:

    struct ConfigData {
        std::string device_id_;
        std::string version_;
        std::string user_name_;
        uint64_t rotation_interval_{0U};
        uint64_t event_retry_interval_{0U};
        bool rotate_switch_{false};
        bool service_enable_{false};
        uint64_t audit_log_level_{0U};
    };

    enum class RetryEventKind {
        kSecretChange,
        kPasswordChange,
        kCounterSync,
        kHealthReport,
        kNone,
    };

    bool ApplyPassword(const std::string &pass) const;
    bool ChangePassword();
    void ClearPersistenceEntries() const;
    bool GenerateAndApplyPassword(const std::string &secret_key, uint64_t counter) const;
    bool GeneratePassword(const std::string &secret, const std::string &data, std::string &pass_out) const;
    bool GetNewSecretAndRegisterWithCloud(std::string &secret_key) const;
    bool GetNewSecretKey(std::string &hex_key) const;
    bool InitMQ();
    bool IsConfidentialDataValid(std::unique_ptr<SamPwConfidentialData> &ptr);
    bool IsGenericDataValid(std::unique_ptr<SamPwGenericData> &ptr);
    void MsgLoop();
    void NotifyCurrentCounter(const std::string &client);
    void OnIotCounterSyncResponse(const std::string &client, bool status);
    void OnPasswordChangeRequest(const std::string &client);
    void OnRetryEventTimeout(const std::string &client);
    void OnSecretChangeRequest(const std::string &client);
    void PasswordTimerTickCB(uint64_t steady_now, uint64_t system_now);
    bool PersistConfidentialData(std::unique_ptr<SamPwConfidentialData> con_data_ptr) const;
    bool PersistGenericData(std::unique_ptr<SamPwGenericData> gen_data_ptr) const ;
    bool ReadConfigData();
    bool ReadPersistedConfidentialData(std::unique_ptr<SamPwConfidentialData> &ptr) const;
    bool ReadPersistedGenericData(std::unique_ptr<SamPwGenericData> &ptr) const;
    bool RegisterSecretKeyWithCloud(const std::string &key) const;
    bool ReportDataToHealthStats(uint64_t counter, uint64_t changed_at, uint64_t elapsed_interval);
    void ResetEventForRetry(RetryEventKind event);
    void ResetPassInEffectTimer();
    bool RetryEvent(RetryEventKind event);
    bool RetryEventClear(RetryEventKind event);
    void RetryEventClearAll();
    void RetryEventTimerTickCB(uint64_t steady_now, uint64_t system_now);
    void SetEventForRetry(RetryEventKind event);
    bool StartPassTimeoutThread();
    bool StartRetryEventTimeoutThread();
    bool StopPassTimeoutThread();
    bool StopRetryEventTimeoutThread();
    bool SyncCounterWithCloud(uint64_t counter);

    std::unique_ptr<ConfigData> config_data_;
    std::atomic<uint64_t> elapsed_retry_event_interval_{0U};
    std::unique_ptr<nd::utils::TimerTick> event_notifier_tick_;
    std::atomic<int> msg_id_{0};
    std::unique_ptr<nd::utils::TimerTick> password_tick_;
    std::atomic<uint64_t> pass_in_effect_interval_{0U};
    std::set<RetryEventKind> events_to_retry_;
    nd_msgq_t *server_mq_ = nullptr;
    NDService *service_obj_ptr_ = nullptr;

    static constexpr uint64_t kTick_interval_ = 60U; // 60 seconds for TimerTick

    enum class PassErrCode {
        kOk = 0,
        kCloudError,
        kPassChangeError,
        kGenericError,
    };

    enum class InitErrCode {
        kOk = 0,
        kConfigFail,
        kServiceDisabled,
        kInvalidUser,
        kMQServerFail,
        kPersistenceFail,
    };
};

}  // namespace security

}  // namespace nd

#endif  // INC_ND_SECURITY_AUTH_MODULE_H_
