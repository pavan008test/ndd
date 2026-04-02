/* Copyright (C) 2022 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, October 2022
 */

#include <shadow.h>
#include <crypt.h>
#include <cstring>

#include <log.h>
#include <nd_security_shadow.h>

namespace nd {

namespace security {

static const char *const kLogTag = "SHDW";

bool ShadowUtil::DoesUserPasswordMatch(const std::string &user, const std::string &password) {

    LOG_I(kLogTag, "Inside %s", __func__);

    bool match = false;
    constexpr int kBufferSize = 256;
    char buffer[kBufferSize] = {};
    struct spwd *spwd = nullptr;
    struct spwd spwd_buf = {};

    const auto ret_code = getspnam_r(user.c_str(), &spwd_buf, buffer, kBufferSize, &spwd);
    if (0 == ret_code) {
        struct crypt_data data = {};
        const char *enc_pass = crypt_r(password.c_str(), spwd->sp_pwdp, &data);

        if (nullptr != enc_pass) {
            if (0 == strncmp(enc_pass,  spwd->sp_pwdp, strlen(enc_pass))) {
                LOG_I(kLogTag, "password matched");
                match = true;
            } else {
                LOG_E(kLogTag, "password does not match");
            }
        } else {
            LOG_E(kLogTag, "crpt_r fail, errno: %d", errno);
        }
    } else {
        LOG_E(kLogTag, "getspnam fail: %d, errno: %d", ret_code, errno);
    }
    return match;
}

bool ShadowUtil::DoesUserExistWithPassword(const std::string &user) {
    LOG_I(kLogTag, "Inside %s", __func__);

    bool status = false;
    constexpr int kBufferSize = 256;
    char buffer[kBufferSize] = {};
    struct spwd *spwd = nullptr;
    struct spwd spwd_buf = {};

    const auto ret_code = getspnam_r(user.c_str(), &spwd_buf, buffer, kBufferSize, &spwd);
    if (0 == ret_code) {
        if (nullptr != spwd) {
            if (nullptr != spwd->sp_pwdp) {
                if (('!' != spwd->sp_pwdp[0]) &&
                    ('*' != spwd->sp_pwdp[0])) {
                    status = true;
                    LOG_I(kLogTag, "Valid user, password can be changed");
                } else {
                    LOG_E(kLogTag, "User: %s passworc cannot be changed", user.c_str());
                }
            } else {
                LOG_E(kLogTag, "User: %s has no password set", user.c_str());
            }
        } else {
            LOG_E(kLogTag, "No such user: %s", user.c_str());
        }
    } else {
        LOG_E(kLogTag, "getspnam fail: %d, errno: %d", ret_code, errno);
    }
    return status;
}

}

}
