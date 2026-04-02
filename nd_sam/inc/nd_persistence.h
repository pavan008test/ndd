/* Copyright (C) 2022 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, October 2022
 */

#ifndef INC_ND_PERSISTENCE_H_
#define INC_ND_PERSISTENCE_H_

#include <functional>
#include <string>
#include <utility>

#include <sqlite3.h>

namespace nd {

namespace utils {

class PersistenceDB {
 public:
    using DbCbFn = int(void *, int, char **, char **);
    explicit PersistenceDB(const std::string &path);
    PersistenceDB() = delete;
    PersistenceDB(const PersistenceDB &) = delete;
    PersistenceDB(PersistenceDB &&) = delete;
    PersistenceDB& operator=(const PersistenceDB&) = delete;
    PersistenceDB& operator=(PersistenceDB&&) = delete;
    ~PersistenceDB();

    bool Close();
    bool CreateTable(const std::string &schema) const;
    bool DropTable(const std::string &schema) const;
    std::pair<int, std::string> ExecuteCommand(const std::string &cmd, DbCbFn cb_fn, void *cb_data) const;
    bool Open();

 protected:
    std::string db_path_;
    sqlite3* db_ = nullptr;
};

class EncryptedDB : public PersistenceDB {
 public:
    explicit EncryptedDB(const std::string &path);
    bool Open();
    bool Close();
 private:
};


}  // namespace utils

}  // namespace nd

#endif  // INC_ND_PERSISTENCE_H_
