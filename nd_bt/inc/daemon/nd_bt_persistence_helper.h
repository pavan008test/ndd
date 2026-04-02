/* Copyright (C) 2022 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, October 2022
 */

#ifndef INC_ND_BT_PERSISTENCE_HELPER_H_
#define INC_ND_BT_PERSISTENCE_HELPER_H_

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <vector>
#include <thread>
#include <unordered_map>

class NDService;

namespace nd {

namespace device {

class BtPersistenceDBHelper {
 public:

    enum class ApiVersion {
        kLegacy,
        kV2,
        kMax
    };

    explicit BtPersistenceDBHelper(const std::string &device_id, ApiVersion api_version);
    BtPersistenceDBHelper() = delete;
    BtPersistenceDBHelper(const BtPersistenceDBHelper &) = delete;
    BtPersistenceDBHelper(BtPersistenceDBHelper &&) = delete;
    BtPersistenceDBHelper& operator=(const BtPersistenceDBHelper&) = delete;
    BtPersistenceDBHelper& operator=(BtPersistenceDBHelper&&) = delete;
    ~BtPersistenceDBHelper();

    struct DriverSessionData {
        std::unordered_map<std::string, int64_t> id_map_;
        uint64_t start_time_;
        uint64_t end_time_;
        uint64_t audio_count_;
        bool do_upload_;
    };

    bool BeginDriverSession(DriverSessionData &&data, const std::string &session_id);
    bool UpdateDriverSessionAudioCount(uint32_t audio_count, const std::string &session_id);
    bool EndDriverSession(const std::string &session_id);
    bool StartProcessing();
    bool StopProcessing();
    bool AddLoginEntry(DriverSessionData &&driver_data, const std::string &session_id);

 private:

    struct LoginRecord {
        std::vector<std::string> row_ids_;
        std::vector<std::string> data_;
        std::vector<std::string> start_times_;
    };

    bool ClearDriverData();
    bool DeleteLegacyTableRows(std::vector<std::string> &ids);
    bool DeleteRows(std::vector<std::string> &ids, std::vector<std::string> &start_times);
    void ProcessLoop();
    bool FetchLegacyTableData(LoginRecord &record_out);
    bool IsLegacyTableEmpty();
    bool ReadV2DriverDataAsJsonString(LoginRecord &record_out);
    bool ReadLegacyTableDriverDataAsJsonString(LoginRecord &record_out);
    bool MoveValidDataToMainTable();
    bool RemoveStaleSessionEntries();
    bool WriteToSessionTable(DriverSessionData &&driver_data, const std::string &session_id);
    void SetFunctionObjects(ApiVersion api_version);

    bool BeginDriverSessionImplV2(DriverSessionData &&data, const std::string &session_id);
    bool UpdateDriverSessionAudioCountImplV2(uint32_t audio_count, const std::string &session_id);
    bool EndDriverSessionImplV2(const std::string &session_id);
    bool AddLoginEntryImplV2(DriverSessionData &&driver_data, const std::string &session_id);

    bool AddLoginEntryImplLegacy(DriverSessionData &&driver_data, const std::string &session_id);

    std::function<bool(DriverSessionData &&, const std::string &)> add_login_entry_fn_;
    std::function<bool(DriverSessionData &&, const std::string &)> begin_session_fn_;
    std::function<bool(uint32_t audio_count, const std::string &session_id)> update_audio_count_fn_;
    std::function<bool(const std::string &)> end_session_fn_;

    std::mutex db_mutex_;
    std::mutex api_mutex_;
    bool exit_requested_{false};
    bool data_ready_{false};
    std::thread process_thread_th_;
    std::condition_variable data_cv_;
    std::mutex data_mutex_;
    const std::string device_id_;
};

struct BtAdsmState {
    // Driver attributes
    std::string current_hash_;
    std::unordered_map<std::string, int64_t> current_drivers_;

    // vehicle state attributes
    int ign_status_;
    bool wake_up_status_;

    // session tracking
    uint64_t audio_count_;

    // boot tracking
    std::string boot_id_;

    // timestamp tracking
    int64_t last_updated_;
};

class BtAdsmStateManager {
 public:
    explicit BtAdsmStateManager(const std::string &state_file_path);
    BtAdsmStateManager() = delete;
    BtAdsmStateManager(const BtAdsmStateManager &) = delete;
    BtAdsmStateManager(BtAdsmStateManager &&) = delete;
    BtAdsmStateManager& operator=(const BtAdsmStateManager&) = delete;
    BtAdsmStateManager& operator=(BtAdsmStateManager&&) = delete;
    ~BtAdsmStateManager();

    bool LoadState();
    bool SaveState();

    // Static utility
    static bool DeleteStateFile(const std::string& state_file_path);
    static bool CopyStateFile(const std::string& source_path, const std::string& dest_path);
    static bool MoveStateFile(const std::string& source_path, const std::string& dest_path);

    // Getters
    const std::string& GetCurrentHash() const;
    const std::unordered_map<std::string, int64_t>& GetCurrentDrivers() const;
    int GetIgnStatus() const;
    bool GetWakeUpStatus() const;
    uint64_t GetAudioCount() const;
    const std::string& GetBootId() const;
    int64_t GetLastUpdated() const;

    // Setters
    void SetCurrentHash(const std::string& hash);
    void SetCurrentDrivers(const std::unordered_map<std::string, int64_t>& drivers);
    void SetIgnStatus(int status);
    void SetWakeUpStatus(bool status);
    void SetAudioCount(uint64_t count);
    void SetBootId(const std::string& boot_id);

    // Get entire state
    const BtAdsmState GetState() const;

 private:
    BtAdsmState state_{};
    std::string state_file_path_;
    mutable std::mutex state_mutex_;
    NDService* service_ptr_;

    enum class AdsmErrCode {
        kOpenFail,
        kIntegrityFail,
        kReadFail,
        kWriteFail,
        kOther
    };
};

class FileSystemHandler {
 public:
    // Check if file exists
    static bool FileExists(const std::string& file_path);

    // Check if directory exists
    static bool DirectoryExists(const std::string& dir_path);

    // Create directory (including parent directories)
    static bool CreateDirectories(const std::string& dir_path);

    // Delete file
    static bool DeleteFile(const std::string& file_path);

    // Copy file from source to destination (overwrites if exists)
    static bool CopyFile(const std::string& source_path, const std::string& dest_path);

    // Move/rename file from source to destination
    static bool MoveFile(const std::string& source_path, const std::string& dest_path);

    // Check if path is a regular file
    static bool IsRegularFile(const std::string& file_path);
};

}  // namespace device

}  // namespace nd

#endif  // INC_ND_BT_PERSISTENCE_HELPER_H_
