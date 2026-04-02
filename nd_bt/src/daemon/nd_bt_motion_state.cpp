/* Copyright (C) 2023 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, January 2023
 */

#include <vector>

#include <log.h>
#include <nd_msg_types.h>

#include <nd_bt_motion_state.h>
#include <nd_bt_man.h>
#include <nd_bt_platform_specific.h>

namespace nd {
namespace device {

static const char *const kLogTag = "MOTION";

MotionState::MotionState(BTManager *bt_man_ptr): bt_man_ptr_(bt_man_ptr) {
}

void MotionState::OnMessage(void *msg) {
    nd_msgq_t::nd_msg_t *message = reinterpret_cast<nd_msgq_t::nd_msg_t *>(msg);
    generic_msg_t *g_msg = reinterpret_cast<generic_msg_t *>(message->get_buffer());

    const auto client_name = nd::platform_specific::FetchMsgClientName(g_msg);

    switch(g_msg->msg_type) {

        case RES_IDLE_UPDATE: {
            LOG_I(kLogTag, "Received RES_IDLE_UPDATE msg from: %s", client_name.c_str());
            bt_man_ptr_->HandleIdleEventUpdateInMotionState(g_msg);
            break;
        }

        case RES_SPEED_UPDATE: {
            LOG_I(kLogTag, "Received RES_SPEED_UPDATE msg from: %s", client_name.c_str());
            bt_man_ptr_->HandleSpeedEventUpdateInMotionState(g_msg);
            break;
        }

        // case DRIVER_LOGIN_APP_QR_SCAN_COMPLETE: {
        //     LOG_I(kLogTag, "Received DRIVER_LOGIN_APP_QR_SCAN_COMPLETE msg from: %s", client_name.c_str());
        //     bt_man_ptr_->DoDriverLoginAppQrActivity();
        //     break;
        // }

        case DRIVER_LOGIN_SCAN_COMPLETE: {
            LOG_I(kLogTag, "Received DRIVER_LOGIN_SCAN_COMPLETE msg from: %s", client_name.c_str());
            bt_man_ptr_->DoDriverLoginActivity();
            break;
        }

        case BTFV_INTERNAL_RETRY_EVENT: {
            LOG_I(kLogTag, "Received BTFV_INTERNAL_RETRY_EVENT msg from: %s", client_name.c_str());
            bt_man_ptr_->HandleRetryEventsInMotionState();
            break;
        }

        default: {
            if (!bt_man_ptr_->HandleGenericEvent(g_msg)) {
                LOG_I(kLogTag, "%d message remained unhandled", g_msg->msg_type);
            }
            break;
        }
    }

}

void MotionState::ActionOnEntry() {
    // TODO(sunil.s): Check if this needs to be called?
    bt_man_ptr_->BeginLegacyLoginActivityIfEnabled();
    bt_man_ptr_->TriggerDriverLoginAudio();
}

void MotionState::ActionOnExit() {
}

bool MotionState::MeetsExitCriteria() {
    bool status = true;
    return status;
}

bool MotionState::MeetsEntryCriteria() {
    bool status = true;
    return status;
}

} // namespace device

} // namespace nd
