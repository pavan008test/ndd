/* Copyright (C) 2022 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, October 2022
 */

#include <iostream>
#include <memory>

#include <log.h>
#include <nd_sam.h>

static const char *const kLogDir = "/home/ubuntu/.nddevice/log/nd_sam";
static const char *const kLogTag = "MAIN";

#define ROUTE_LOGS

int main () {
    if (!nd_log_init(kLogDir)) {
        std::cout << "unable to init logger :: but continue" << std::endl;
    }

#ifdef ROUTE_LOGS
    route_logs(kLogDir);
#endif

    std::unique_ptr<nd::security::AuthModule> sam_ptr = std::make_unique<nd::security::AuthModule>();
    if (sam_ptr->Init()) {
        LOG_I(kLogTag, "Init success");
        sam_ptr->Run();
    } else {
        // init failure
        LOG_E(kLogTag, "Init failed");
    }

    return 0;
}
