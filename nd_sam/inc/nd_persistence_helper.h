/* Copyright (C) 2022 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, October 2022
 */

#ifndef INC_ND_PERSISTENCE_HELPER_H_
#define INC_ND_PERSISTENCE_HELPER_H_

#include <memory>
#include <mutex>

#include <nd_security_data.h>

namespace nd {

namespace security {

class PersistenceUtils {
 public:
    static bool CheckPersistenceDir(const std::string &path);
    static bool CheckIfFileExists(const std::string &file);
};

class PersistenceDBHelper {
 public:
    static std::unique_ptr<SamPwGenericData> ReadSamPwGenericData();
    static bool StoreSamPwGenericData(std::unique_ptr<SamPwGenericData>);
    static bool UpdateDbWithPassInEffectTime(uint64_t secs = kDefault_increment_);
    static bool ClearSamGenericData();
 private:
    static const uint64_t kDefault_increment_ = 60U; // 60 seconds
    static std::mutex db_helper_mutex_;
};

class PersistenceEncryptedDBHelper {
 public:
    static std::unique_ptr<SamPwConfidentialData> ReadSamPwConfidentialData();
    static bool StoreSamPwConfidentialData(std::unique_ptr<SamPwConfidentialData>);
    static bool ClearSamPwConfidentialData();
 private:
    static std::mutex enc_db_helper_mutex_;
};

}  // namespace security

}  // namespace nd

#endif // INC_ND_PERSISTENCE_HELPER_H_
