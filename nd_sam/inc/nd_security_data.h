/* Copyright (C) 2022 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, October 2022
 */

#ifndef INC_ND_SECURITY_DATA_H_
#define INC_ND_SECURITY_DATA_H_

#include <string>

namespace nd {

namespace security {

struct SamPwConfidentialData {
    std::string secret_key_;
    uint64_t last_change_at_;
};

struct SamPwGenericData {
    uint64_t counter_;
    uint64_t last_change_at_;
    uint64_t elapsed_interval_;
};

}  // namespace security

}  // namespace nd

#endif  // INC_ND_SECURITY_SHADOW_H_
