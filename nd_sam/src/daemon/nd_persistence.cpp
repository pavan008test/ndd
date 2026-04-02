/* Copyright (C) 2022 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, October 2022
 */

#include <nd_auth_openssl.h>
#include <nd_file_utils.h>
#include <nd_persistence.h>
#include <system_utils.h>

namespace nd {

namespace utils {

static const char *const kLogTag = "PERS_DB";

PersistenceDB::PersistenceDB(const std::string &path):db_path_(path) {
}

PersistenceDB::~PersistenceDB(){
    Close();
}

bool PersistenceDB::Open() {
    bool status = false;
    const auto ret_code = sqlite3_open(db_path_.c_str(), &db_);
    if (SQLITE_OK == ret_code) {
        LOG_I(kLogTag, "SQL DB Successfully opened for: %s with auto commit status: %d", db_path_.c_str(), sqlite3_get_autocommit(db_));
        status = true;
    } else {
        LOG_E(kLogTag, "Error opening sql db: %s, error: %s", db_path_.c_str(), sqlite3_errmsg(db_));
        sqlite3_close(db_);
        db_ = nullptr;
    }
    return status;
}

bool PersistenceDB::Close() {
    bool status = false;
    if (db_) {
        const auto ret_code = sqlite3_close(db_);
        if (SQLITE_OK == ret_code) {
            LOG_I(kLogTag, "Closed DB successfully: %s, %d", db_path_.c_str(), ret_code);
            status = true;
        } else {
            // error
            LOG_E(kLogTag, "Error Closing db: %d", ret_code);
        }
    } else {
        // already null
        LOG_E(kLogTag, "DB already NULL: %s", db_path_.c_str());
    }

    db_ = nullptr;
    return status;
}

bool PersistenceDB::CreateTable(const std::string &schema) const {
    bool status = false;
    char *err_msg = nullptr;

    const auto ret_code = sqlite3_exec(db_, schema.c_str(), nullptr, 0, &err_msg);

    if (SQLITE_OK == ret_code) {
        LOG_I(kLogTag, "Created table successfully: %s, %d", db_path_.c_str(), ret_code);
        status = true;
    } else {
        const std::string err(err_msg);
        if (std::string::npos != err.find("already exists")) {
            LOG_I(kLogTag, "Table already exists: %s, %d, %s", db_path_.c_str(), ret_code, err.c_str());
            status = true;
        } else {
            LOG_E(kLogTag, "Table creation error: %s, %d, %s", db_path_.c_str(), ret_code, err.c_str());
        }
        sqlite3_free(err_msg);
    }

    return status;
}

bool PersistenceDB::DropTable(const std::string &schema) const {
    bool status = false;
    char *err_msg = nullptr;

    const auto ret_code = sqlite3_exec(db_, schema.c_str(), nullptr, 0, &err_msg);

    if (SQLITE_OK == ret_code) {
        LOG_I(kLogTag, "Table dropped successfully: %s, %d", db_path_.c_str(), ret_code);
        status = true;
    } else {
        if (nullptr != err_msg) {
            LOG_E(kLogTag, "Table drop error: %s, %d, %s", db_path_.c_str(), ret_code, err_msg);
        } else {
            LOG_E(kLogTag, "Table drop error: %s, %d", db_path_.c_str(), ret_code);
        }
        sqlite3_free(err_msg);
    }

    return status;
}

std::pair<int, std::string> PersistenceDB::ExecuteCommand(const std::string &cmd, DbCbFn cb_fn, void *cb_data) const {
    char *err_msg = nullptr;
    std::string err;

    const auto ret_code = sqlite3_exec(db_, cmd.c_str(), cb_fn, cb_data, &err_msg);

    if (SQLITE_OK == ret_code) {
        LOG_I(kLogTag, "sqlite3_exec success: %s, %d", db_path_.c_str(), ret_code);
    } else {
        // log error
        if (nullptr != err_msg) {
            err = err_msg;
            sqlite3_free(err_msg);
        }
        LOG_E(kLogTag, "sqlite3_exec failure: %s, %d, %s", db_path_.c_str(), ret_code, err.c_str());
    }

    return {ret_code, err};
}

bool EncryptedDB::Open() {
    // decrypt the file if exists
    // open with sql
    bool status = false;
    const char * const dec_tag = "_dec";

    LOG_I(kLogTag, "In %s db_path: %s", __func__, db_path_.c_str());

    if (0 < get_file_size(db_path_)) {
        // std::string file_name_without_extension = db_path_;
        // enc_dec_path = file_name_without_extension + dec_tag;

        std::string enc_dec_path;

        std::string::size_type const pos(db_path_.find_last_of('.'));
        if (std::string::npos != pos) {
            std::string file_name_without_extension = db_path_.substr(0, pos);
            enc_dec_path = file_name_without_extension + dec_tag + db_path_.substr(pos);
        } else {
            enc_dec_path = db_path_ + dec_tag;
        }

        const auto result = nd_file_reoperate_to_file(db_path_.c_str(), enc_dec_path.c_str(), true);

        if (ND_AUTH_SUCCESS == result) {
            LOG_I(kLogTag, "nd_file_reoperate_to_file success, src: %s, dest: %s", db_path_.c_str(), enc_dec_path.c_str());

            if (!file_rename(enc_dec_path, db_path_)) {
                LOG_E(kLogTag, "file_rename failed, src: %s, dest: %s", enc_dec_path.c_str(), db_path_.c_str());
            }
            status = true;
        } else {
            LOG_E(kLogTag, "nd_file_reoperate_to_file failed, src: %s, dest: %s, result: %d",
                  db_path_.c_str(), enc_dec_path.c_str(), result);
        }
    } else {
        // no file, create new
        status = true;
        LOG_I(kLogTag, "New DB file: %s", db_path_.c_str());
    }

    if (status) {
        if(PersistenceDB::Open()) {
            status = true;
        } else {
            // failure
            status = false;
        }
    }

    return status;
}

bool EncryptedDB::Close() {
    bool status = false;
    // close with sql
    // encrypt the file if exists

    status = PersistenceDB::Close();

    if (0 < get_file_size(db_path_)) {
        std::string enc_dec_path;

        std::string::size_type const pos(db_path_.find_last_of('.'));
        if (std::string::npos != pos) {
            const std::string file_name_without_extension = db_path_.substr(0, pos);
            enc_dec_path = file_name_without_extension + "_enc" + db_path_.substr(pos);
        } else {
            enc_dec_path = db_path_ + "_enc";
        }

        const auto result = nd_file_operate_to_file(db_path_.c_str(), enc_dec_path.c_str());

        if (ND_AUTH_SUCCESS == result) {
            LOG_I(kLogTag, "nd_file_operate_to_file success, src: %s, dest: %s", db_path_.c_str(), enc_dec_path.c_str());

            if (!file_rename(enc_dec_path, db_path_)) {
                LOG_E(kLogTag, "file_rename failed, src: %s, dest: %s", enc_dec_path.c_str(), db_path_.c_str());
            }
            status = true;
        } else {
            LOG_E(kLogTag, "nd_file_operate_to_file failed, src: %s, dest: %s, result: %d",
                  db_path_.c_str(), enc_dec_path.c_str(), result);
            status = false;
        }
    }
    return status;
}

EncryptedDB::EncryptedDB(const std::string &path):PersistenceDB(path) {
}

}  // namespace utils

}  // namespace nd
