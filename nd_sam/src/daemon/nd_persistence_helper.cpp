/* Copyright (C) 2022 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, October 2022
 */

#include <algorithm>
#include <limits>
#include <sstream>
#include <vector>

#include <boost/filesystem.hpp>

#include <log.h>
#include <nd_file_utils.h>
#include <nd_persistence_helper.h>
#include <nd_sam_constants.h>
#include <nd_persistence.h>

namespace nd {

namespace security {

using nd::utils::PersistenceDB;
using nd::utils::EncryptedDB;

static const char *const kLogTag       = "PERS_H";

static const char *const kGenDbName    = "sam_gen.db";
static const char *const kCfdDbName    = "sam_cfd.db";
static const char *const kCfdDbEncName = "sam_cfd_enc.db";  // encrypted db
static const char *const kCfdDbDecName = "sam_cfd_dec.db";  // decrypted db

static const std::string kGenDbPath    = std::string(nd::constants::kSamDbFolderPath) + kGenDbName;
static const std::string kCfdDbPath    = std::string(nd::constants::kSamDbFolderPath) + kCfdDbName;
static const std::string kCfdDbEncPath = std::string(nd::constants::kSamDbFolderPath) + kCfdDbEncName;
static const std::string kCfdDbDecPath = std::string(nd::constants::kSamDbFolderPath) + kCfdDbDecName;

static const char *const kCfbTableName = "SECRET_KEY";
static const char *const kGenTableName = "COUNTER_DATA";

std::mutex PersistenceDBHelper::db_helper_mutex_;
std::mutex PersistenceEncryptedDBHelper::enc_db_helper_mutex_;

bool PersistenceUtils::CheckPersistenceDir(const std::string &path) {
    bool status = false;
    boost::system::error_code ec;

    do {
        if (boost::filesystem::is_directory(path, ec)) {
            LOG_I(kLogTag, "directory already exists");
            status = true;
            break;
        }

        if (boost::filesystem::create_directories(path, ec)) {
            LOG_I(kLogTag, "directory created now");
            status = true;
            break;
        }

        if (0 == ec.value()) {
            LOG_I(kLogTag, "create_directories ec is success");
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

static std::string GetCfdDBPath() {
    if (!PersistenceUtils::CheckPersistenceDir(nd::constants::kSamDbFolderPath)) {
        LOG_E(kLogTag, "CheckPersistenceDir failed");
    }

    std::vector<std::string> files;
    bool cfd_db_present = false, cfd_db_enc_present = false, cfd_db_dec_present = false;
    if (get_files_from_given_type(nd::constants::kSamDbFolderPath, FILE_TYPE_REGULAR, files)) {
        for (const auto &file : files) {
            LOG_I(kLogTag, "Files: %s", file.c_str());
            if (kCfdDbEncName == file) {
                cfd_db_enc_present = true;
            } else if (kCfdDbDecName == file) {
                cfd_db_dec_present = true;
            } else if (kCfdDbName == file) {
                cfd_db_present = true;
            } else {
            }
        }
    }

    LOG_I(kLogTag, "File status:- cfd_db_enc_present: %d, cfd_db_dec_present: %d, cfd_db_present: %d",
        cfd_db_enc_present, cfd_db_dec_present, cfd_db_present);

    if (!cfd_db_present) {
        if (cfd_db_enc_present) {
            file_rename(kCfdDbEncPath, kCfdDbPath);
        } else if (cfd_db_dec_present) {
            file_rename(kCfdDbDecPath, kCfdDbPath);
        } else {
        }
    } else {
        // Think if anything needs to be added here
    }

    // TODO(sunil): consider all possibilites:
    // will all 3 files exists ? -> normal, _enc, _dec ?
    // what if enc / dec exists but not normal ?
    // what if all three exists ? does this case ever occur?

    return kCfdDbPath;
}

bool PersistenceUtils::CheckIfFileExists(const std::string &file) {
    bool status = false;
    boost::system::error_code ec;

    if (boost::filesystem::is_regular_file(file, ec)) {
        LOG_I(kLogTag, "File:%s exists", file.c_str());
        status = true;
    } else {
        LOG_E(kLogTag, "File:%s does not exist, ec: %s:%d", file.c_str(), ec.message().c_str(), ec.value());
    }
    return status;
}

std::unique_ptr<SamPwGenericData> PersistenceDBHelper::ReadSamPwGenericData() {
    bool status = false;
    bool is_db_corrupted = false;

    LOG_I(kLogTag, "Inside function: %s", __func__);

    std::unique_ptr<SamPwGenericData> data_ptr;

    const std::lock_guard<std::mutex> lock(db_helper_mutex_);

    if (PersistenceUtils::CheckIfFileExists(kGenDbPath)) {
        data_ptr = std::unique_ptr<SamPwGenericData>(new (std::nothrow) SamPwGenericData());
        std::unique_ptr<PersistenceDB> db_ptr = std::unique_ptr<PersistenceDB>(new (std::nothrow) PersistenceDB(kGenDbPath));

        if (db_ptr && data_ptr) {
            if (db_ptr->Open()) {
                do {
                    {
                        // do integrity check
                        const char * const command = "PRAGMA integrity_check;";
                        const auto ret_pair = db_ptr->ExecuteCommand(command, nullptr, nullptr);
                        if (SQLITE_OK != ret_pair.first) {
                            // error ret_pair : ret_pair.second;
                            LOG_E(kLogTag, "ExecuteCommand for : %s failed: %d - %s, DB Corrupted?",
                                command, ret_pair.first, ret_pair.second.c_str());
                            is_db_corrupted = true;
                            break;
                        }
                    }

                    {
                        // get data
                        auto callback = [](void *cb_data, int argc, char **argv, char **cols)-> int {
                            // we must have only one entry of secret key
                            int ret_value = -1;
                            if (3 == argc) {
                                // TODO(sunil) : check if this is argv or cols
                                auto data_ptr = (reinterpret_cast<SamPwGenericData *>(cb_data));
                                data_ptr->counter_ = std::stoull(argv[0], nullptr, 10);
                                data_ptr->last_change_at_ = std::stoull(argv[1], nullptr, 10);
                                data_ptr->elapsed_interval_ = std::stoull(argv[2], nullptr, 10);
                                ret_value = 0;
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
                        const std::string command = std::string("SELECT * FROM '") + kGenTableName + "' ORDER BY COUNTER DESC LIMIT 1;";
                        const auto ret_pair = db_ptr->ExecuteCommand(command, callback, data_ptr.get());
                        if (SQLITE_OK != ret_pair.first) {
                            LOG_E(kLogTag, "ExecuteCommand for : %s failed: %d - %s",
                                command.c_str(), ret_pair.first, ret_pair.second.c_str());
                            break;
                        }
                        if ((0 == data_ptr->counter_) ||
                            (std::numeric_limits<uint64_t>::max() == data_ptr->counter_) ||
                            (std::numeric_limits<uint64_t>::min() == data_ptr->counter_)) {
                                LOG_E(kLogTag, "Counter Data corrupted : %lu", data_ptr->counter_);
                            break;
                        }
                    }
                    LOG_I(kLogTag, "Data read success in %s", __func__);
                    status = true;
                } while (false);

                db_ptr->Close();
            } else {
                LOG_E(kLogTag, "db_ptr->Open failed in %s", __func__);
            }
        } else {
            // error no memory
            LOG_E(kLogTag, "Low on memory, db_ptr or/and data_ptr is/are null in %s", __func__);
        }
    } else {
        LOG_E(kLogTag, "PersistenceUtils::CheckIfFileExists failed in %s", __func__);
    }

    if (!status) {
        LOG_E(kLogTag, "Resetting data_ptr in %s", __func__);
        data_ptr.reset(nullptr);
    }

    if (is_db_corrupted) {
        boost::system::error_code ec;
        if (boost::filesystem::remove(kGenDbPath, ec)) {
            LOG_I(kLogTag, "File: %s deleted", kGenDbPath.c_str());
        } else {
            LOG_E(kLogTag, "File: %s delete failed, ec: %s:%d", kGenDbPath.c_str(), ec.message().c_str(), ec.value());
        }
    }

    return data_ptr;
}

bool PersistenceDBHelper::ClearSamGenericData() {
    bool status = false;

    LOG_I(kLogTag, "Inside function: %s", __func__);

    const std::lock_guard<std::mutex> lock(db_helper_mutex_);

    if (PersistenceUtils::CheckIfFileExists(kGenDbPath)) {
        std::unique_ptr<PersistenceDB> db_ptr = std::unique_ptr<PersistenceDB>(new (std::nothrow) PersistenceDB(kGenDbPath));

        if (db_ptr) {
            if (db_ptr->Open()) {
                const std::string drop_table_str = std::string("DROP TABLE IF EXISTS ") + kGenTableName;

                const auto ret_pair = db_ptr->ExecuteCommand(drop_table_str, nullptr, nullptr);
                if (SQLITE_OK == ret_pair.first) {
                    status = true;
                } else {
                    LOG_E(kLogTag, "ExecuteCommand for : %s failed: %d - %s",
                        drop_table_str.c_str(), ret_pair.first, ret_pair.second.c_str());
                }

                db_ptr->Close();
            } else {
                // error
                LOG_E(kLogTag, "db_ptr->Open failed in %s", __func__);
            }
        } else {
            // error
            LOG_E(kLogTag, "Low on memory, db_ptr is null in %s", __func__);
        }
    } else {
        LOG_E(kLogTag, "PersistenceUtils::CheckIfFileExists failed in %s", __func__);
    }
    return status;
}

bool PersistenceDBHelper::StoreSamPwGenericData(std::unique_ptr<SamPwGenericData> data_ptr) {
    bool status = false;

    LOG_I(kLogTag, "Inside function: %s", __func__);

    const std::lock_guard<std::mutex> lock(db_helper_mutex_);
    std::unique_ptr<PersistenceDB> db_ptr = std::unique_ptr<PersistenceDB>(new (std::nothrow) PersistenceDB(kGenDbPath));

    if (db_ptr) {
        if (db_ptr->Open()) {
            do {
                {
                    // delete table data
                    const std::string drop_table_str = std::string("DELETE FROM ") + kGenTableName;

                    const auto ret_pair = db_ptr->ExecuteCommand(drop_table_str, nullptr, nullptr);
                    if (SQLITE_OK != ret_pair.first) {
                        LOG_E(kLogTag, "ExecuteCommand for : %s failed: %d - %s",
                            drop_table_str.c_str(), ret_pair.first, ret_pair.second.c_str());
                        // Ignore the error, do not break
                    }
                }
                {
                    // create table
                    const std::string create_table_str = std::string("CREATE TABLE IF NOT EXISTS ") + kGenTableName +
                                                         "(COUNTER INTEGER PRIMARY KEY NOT NULL,"
                                                         "GEN_TIME INTEGER NOT NULL,"
                                                         "ELAPSED_TIME INTEGER NOT NULL);";

                    const auto ret_pair = db_ptr->ExecuteCommand(create_table_str, nullptr, nullptr);
                    if (SQLITE_OK != ret_pair.first) {
                        LOG_E(kLogTag, "ExecuteCommand for : %s failed: %d - %s",
                            create_table_str.c_str(), ret_pair.first, ret_pair.second.c_str());
                        break;
                    }
                }
                {
                    // insert data
                    std::ostringstream oss;
                    oss << "INSERT INTO " << kGenTableName << " VALUES (" << data_ptr->counter_ << ","
                                                           << data_ptr->last_change_at_ << ","
                                                           << data_ptr->elapsed_interval_ << ");";

                    const auto ret_pair = db_ptr->ExecuteCommand(oss.str(), nullptr, nullptr);
                    if (SQLITE_OK != ret_pair.first) {
                        LOG_E(kLogTag, "ExecuteCommand for : %s failed: %d - %s",
                            oss.str().c_str(), ret_pair.first, ret_pair.second.c_str());
                        break;
                    }
                }

                LOG_I(kLogTag, "Data store success in %s", __func__);
                status = true;
            } while(false);

            db_ptr->Close();
        } else {
            LOG_E(kLogTag, "db_ptr->Open failed in %s", __func__);
        }
    } else {
        // error
        LOG_E(kLogTag, "Low on memory, db_ptr is null in %s", __func__);
    }

    return status;
}

bool PersistenceDBHelper::UpdateDbWithPassInEffectTime(uint64_t secs) {
    // read current value and increment by 60;
    bool status = false;

    LOG_I(kLogTag, "Inside function: %s", __func__);

    const std::lock_guard<std::mutex> lock(db_helper_mutex_);

    if (PersistenceUtils::CheckIfFileExists(kGenDbPath)) {
        std::unique_ptr<PersistenceDB> db_ptr = std::unique_ptr<PersistenceDB>(new (std::nothrow) PersistenceDB(kGenDbPath));

        if (db_ptr) {
            if (db_ptr->Open()) {
                // update table
                std::ostringstream oss;
                oss << "UPDATE " << kGenTableName << " SET ELAPSED_TIME = ELAPSED_TIME + " << secs;

                const auto ret_pair = db_ptr->ExecuteCommand(oss.str(), nullptr, nullptr);
                if (SQLITE_OK == ret_pair.first) {
                    status = true;
                } else {
                    LOG_E(kLogTag, "ExecuteCommand for : %s failed: %d - %s",
                        oss.str().c_str(), ret_pair.first, ret_pair.second.c_str());
                }
                db_ptr->Close();
            } else {
                LOG_E(kLogTag, "db_ptr->Open failed in %s", __func__);
            }
        } else {
            // error
            LOG_E(kLogTag, "Low on memory, db_ptr is null in %s", __func__);
        }
    } else {
        LOG_E(kLogTag, "PersistenceUtils::CheckIfFileExists failed in %s", __func__);
    }
    return status;
}

std::unique_ptr<SamPwConfidentialData> PersistenceEncryptedDBHelper::ReadSamPwConfidentialData() {
    bool status = false;
    bool is_db_corrupted = false;

    LOG_I(kLogTag, "Inside function: %s", __func__);

    std::unique_ptr<SamPwConfidentialData> data_ptr;

    const std::lock_guard<std::mutex> lock(enc_db_helper_mutex_);
    if (PersistenceUtils::CheckIfFileExists(GetCfdDBPath())) {

        data_ptr = std::unique_ptr<SamPwConfidentialData>(new (std::nothrow) SamPwConfidentialData());
        std::unique_ptr<EncryptedDB> db_ptr = std::unique_ptr<EncryptedDB>(new (std::nothrow) EncryptedDB(kCfdDbPath));

        if (db_ptr && data_ptr) {
            if (db_ptr->Open()) {
                do {
                    {
                        // do integrity check
                        const char * const command = "PRAGMA integrity_check;";
                        const auto ret_pair = db_ptr->ExecuteCommand(command, nullptr, nullptr);
                        if (SQLITE_OK != ret_pair.first) {
                            // error ret_pair : ret_pair.second;
                            LOG_E(kLogTag, "ExecuteCommand for : %s failed: %d - %s, DB Corrupted?",
                                command, ret_pair.first, ret_pair.second.c_str());
                            is_db_corrupted = true;
                            break;
                        }
                    }

                    {
                        // get data
                        auto callback = [](void *cb_data, int argc, char **argv, char **cols)-> int {
                            int ret_val = -1;
                            // TODO(sunil) : check if this is argv or cols
                            // we must have only one entry of secret key
                            if (2 == argc) {
                                auto data_ptr = (reinterpret_cast<SamPwConfidentialData *>(cb_data));
                                data_ptr->secret_key_ = argv[0];
                                data_ptr->last_change_at_ = std::stoul(argv[1], nullptr, 10);
                                ret_val = 0;
                            } else {
                                // error
                                std::ostringstream oss;
                                for (auto counter = 0; counter < argc; ++counter) {
                                    oss << "argv[counter]:" << argv[counter];
                                }
                                LOG_E(kLogTag, "Read incorrect values from DB : %s", oss.str().c_str());
                            }
                            return ret_val;
                        };
                        const std::string command = std::string("SELECT * FROM '") + kCfbTableName + "' ORDER BY GEN_TIME DESC LIMIT 1;";
                        const auto ret_pair = db_ptr->ExecuteCommand(command, callback, data_ptr.get());
                        if (SQLITE_OK != ret_pair.first) {
                            LOG_E(kLogTag, "ExecuteCommand for : %s failed: %d - %s",
                                command.c_str(), ret_pair.first, ret_pair.second.c_str());
                            break;
                        }
                    }

                    LOG_I(kLogTag, "Data read success in %s", __func__);
                    status = true;
                } while (false);

                db_ptr->Close();
            } else {
                LOG_E(kLogTag, "db_ptr->Open failed in %s", __func__);
            }
        } else {
            // error no memory
            LOG_E(kLogTag, "Low on memory, db_ptr or/and data_ptr is/are null in %s", __func__);
        }
    } else {
        LOG_E(kLogTag, "PersistenceUtils::CheckIfFileExists failed in %s", __func__);
    }

    if (!status) {
        data_ptr.reset(nullptr);
    }

    if (is_db_corrupted) {
        boost::system::error_code ec;
        if (boost::filesystem::remove(kCfdDbPath, ec)) {
            LOG_I(kLogTag, "File: %s deleted", kCfdDbPath.c_str());
        } else {
            LOG_E(kLogTag, "File: %s delete failed, ec: %s:%d", kCfdDbPath.c_str(), ec.message().c_str(), ec.value());
        }
    }

    return data_ptr;
}

bool PersistenceEncryptedDBHelper::StoreSamPwConfidentialData(std::unique_ptr<SamPwConfidentialData> data_ptr) {
    bool status = false;

    LOG_I(kLogTag, "Inside function: %s", __func__);

    const std::lock_guard<std::mutex> lock(enc_db_helper_mutex_);
    std::unique_ptr<EncryptedDB> db_ptr = std::unique_ptr<EncryptedDB>(new (std::nothrow) EncryptedDB(GetCfdDBPath()));
    if (db_ptr) {
        if (db_ptr->Open()) {
            do {
                {
                    // delete table
                    const std::string drop_table_str = std::string("DELETE FROM ") +  kCfbTableName;
                    const auto ret_pair = db_ptr->ExecuteCommand(drop_table_str, nullptr, nullptr);
                    if (SQLITE_OK != ret_pair.first) {
                        LOG_E(kLogTag, "ExecuteCommand for : %s failed: %d - %s",
                            drop_table_str.c_str(), ret_pair.first, ret_pair.second.c_str());
                        // Ignore the error, do not break
                    }
                }
                {
                    // create table
                    const std::string create_table_str = std::string("CREATE TABLE IF NOT EXISTS ") + kCfbTableName +
                                                         "(KEY TEXT NOT NULL, GEN_TIME INTEGER NOT NULL);";
                    const auto ret_pair = db_ptr->ExecuteCommand(create_table_str, nullptr, nullptr);
                    if (SQLITE_OK != ret_pair.first) {
                        // error ret_pair : ret_pair.second;
                        LOG_E(kLogTag, "ExecuteCommand for : %s failed: %d - %s",
                            create_table_str.c_str(), ret_pair.first, ret_pair.second.c_str());
                        break;
                    }
                }
                {
                    // insert data
                    std::ostringstream oss;
                    oss << "INSERT INTO " << kCfbTableName << " VALUES ('" << data_ptr->secret_key_ << "',"
                        << data_ptr->last_change_at_ << ");";

                    const auto ret_pair = db_ptr->ExecuteCommand(oss.str(), nullptr, nullptr);
                    if (SQLITE_OK != ret_pair.first) {
                        // error ret_pair : ret_pair.second;
                        LOG_E(kLogTag, "ExecuteCommand for : %s failed: %d - %s",
                            oss.str().c_str(), ret_pair.first, ret_pair.second.c_str());
                        break;
                    }
                }
                LOG_I(kLogTag, "Data store success in %s", __func__);
                status = true;
            } while(false);

            db_ptr->Close();
        } else {
            LOG_E(kLogTag, "db_ptr->Open failed in %s", __func__);
        }
    } else {
        // error
        LOG_E(kLogTag, "Low on memory, db_ptr is null in %s", __func__);
    }

    return status;
}

bool PersistenceEncryptedDBHelper::ClearSamPwConfidentialData() {
    bool status = false;

    LOG_I(kLogTag, "Inside function: %s", __func__);

    const std::lock_guard<std::mutex> lock(enc_db_helper_mutex_);

    if (PersistenceUtils::CheckIfFileExists(GetCfdDBPath())) {
        std::unique_ptr<EncryptedDB> db_ptr = std::unique_ptr<EncryptedDB>(new (std::nothrow) EncryptedDB(kCfdDbPath));

        if (db_ptr) {
            if (db_ptr->Open()) {
                const std::string drop_table_str = std::string("DROP TABLE IF EXISTS ") + kCfbTableName;

                const auto ret_pair = db_ptr->ExecuteCommand(drop_table_str, nullptr, nullptr);
                if (SQLITE_OK == ret_pair.first) {
                    status = true;
                } else {
                    // error ret_pair : ret_pair.second;
                    LOG_E(kLogTag, "ExecuteCommand for : %s failed: %d - %s",
                        drop_table_str.c_str(), ret_pair.first, ret_pair.second.c_str());
                }
                db_ptr->Close();
            }
        } else {
            LOG_E(kLogTag, "Low on memory, db_ptr is null in %s", __func__);
        }
    } else {
        LOG_E(kLogTag, "PersistenceUtils::CheckIfFileExists failed in %s", __func__);
    }
    return status;
}

}  // namespace security

}  // namespace nd
