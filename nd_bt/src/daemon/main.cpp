/* Copyright (C) 2022 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, October 2022
 */

#include <iostream>
#include <memory>

#include <log.h>
#include <nd_bt_man.h>

static const char *const kLogDir = "/home/ubuntu/.nddevice/log/btfv";
static const char *const kLogTag = "MAIN";

#define ROUTE_LOGS

int main () {

    try {
        if (!nd_log_init(kLogDir)) {
            std::cout << "unable to init logger :: but continue" << std::endl;
        }

#ifdef ROUTE_LOGS
        route_logs(kLogDir);
#endif

        auto bt_man_ptr = std::make_unique<nd::device::BTManager>();
        if (bt_man_ptr->Init()) {
            LOG_I(kLogTag, "Init success");
            bt_man_ptr->Run();
        } else {
            // init failure
            LOG_E(kLogTag, "Init failed");
        }

    } catch (const std::bad_alloc& e) {
        LOG_E(kLogTag, "Allocation failed in %s what(): %s", __func__, e.what());
    } catch (const std::exception& e){
        LOG_E(kLogTag, "Allocation failed in %s what(): %s", __func__, e.what());
    } catch (...) {
        LOG_E(kLogTag, "Caught an exception of an undetermined type in %s", __func__);
	}

    LOG_I(kLogTag, "nd_bt_man exit");

    return 0;
}
