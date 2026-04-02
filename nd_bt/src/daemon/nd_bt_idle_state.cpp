/* Copyright (C) 2023 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, January 2023
 */

#include <vector>

#include <log.h>
#include <nd_msg_types.h>

#include <nd_bt_idle_state.h>
#include <nd_bt_man.h>
#include <nd_bt_platform_specific.h>

namespace nd {
namespace device {

static const char *const kLogTag = "IDLE";

IdleState::IdleState(BTManager *bt_man_ptr): bt_man_ptr_(bt_man_ptr) {
}

void IdleState::OnMessage(void *msg) {
    nd_msgq_t::nd_msg_t *message = reinterpret_cast<nd_msgq_t::nd_msg_t *>(msg);
    generic_msg_t *g_msg = reinterpret_cast<generic_msg_t *>(message->get_buffer());

    const auto client_name = nd::platform_specific::FetchMsgClientName(g_msg);

    switch(g_msg->msg_type) {
        case RES_IDLE_UPDATE: {
            LOG_I(kLogTag, "Received RES_IDLE_UPDATE msg from: %s", client_name.c_str());
            bt_man_ptr_->HandleIdleEventUpdateInIdleState(g_msg);
            break;
        }

        case RES_SPEED_UPDATE: {
            LOG_I(kLogTag, "Received RES_SPEED_UPDATE msg from: %s", client_name.c_str());
            bt_man_ptr_->HandleSpeedEventUpdateInIdleState(g_msg);
            break;
        }

        case POWERMON_IGNITION: {
            LOG_I(kLogTag, "Received POWERMON_IGNITION msg from: %s", client_name.c_str());
            bt_man_ptr_->HandleIdleIgnitionStatus(g_msg);
            break;
        }

        case BUTTON_LONG_PRESS_INST: {
            LOG_I(kLogTag, "Received BUTTON_LONG_PRESS_INST msg from: %s", client_name.c_str());
            const bool do_led_blink = true;
            bt_man_ptr_->BeginInstallerActivity(do_led_blink);
            break;
        }

        case START_INSTALLER_SCAN: {
            LOG_I(kLogTag, "Received START_INSTALLER_SCAN msg from: %s", client_name.c_str());
            bt_man_ptr_->ReInitiateInstallerActivity();
            break;
        }

        case RESTART_BLUETOOTH: {
            LOG_I(kLogTag, "Received RESTART_BLUETOOTH msg from: %s", client_name.c_str());
            bt_man_ptr_->RestartBluetoothActivities();
            break;
        }

        case INSTALLER_WRITE_CHAR: {
            LOG_I(kLogTag, "Received INSTALLER_WRITE_CHAR msg from: %s", client_name.c_str());
            bt_man_ptr_->DoInstallerAppWriteChar();
            break;
        }

        case INSTALLER_APP_DETECTED: {
            LOG_I(kLogTag, "Received INSTALLER_APP_DETECTED msg from: %s", client_name.c_str());
            bt_man_ptr_->VerifyAndInitiateWiFiHotspot();
            break;
        }

        case INSTALLER_APP_UPDATE_DETECTED: {
            LOG_I(kLogTag, "Received INSTALLER_APP_UPDATE_DETECTED msg from: %s", client_name.c_str());
            bt_man_ptr_->AdvertiseUpdateCheckResponse();
            break;
        }

        // case INSTALLER_APP_DETECTION_TIMEOUT: {
        //     LOG_I(kLogTag, "Received INSTALLER_APP_DETECTION_TIMEOUT msg from: %s", client_name.c_str());
        //     bt_man_ptr_->HandleInstallerAppDetectionTimeout();
        //     break;
        // }

        // case DRIVER_LOGIN_APP_QR_SCAN_COMPLETE: {
        //     LOG_I(kLogTag, "Received DRIVER_LOGIN_APP_QR_SCAN_COMPLETE msg from: %s", client_name.c_str());
        //     bt_man_ptr_->DoDriverLoginAppQrActivity();
        //     break;
        // }

        case DRIVER_LOGIN_SCAN_COMPLETE: {
            LOG_I(kLogTag, "Received DRIVER_LOGIN_SCAN_COMPLETE msg from: %s", client_name.c_str());
            // Ignore entries as the device is in idle state
            bt_man_ptr_->HandleLoginCompleteInIdle();
            break;
        }

        case BTFV_INTERNAL_RETRY_EVENT: {
            LOG_I(kLogTag, "Received BTFV_INTERNAL_RETRY_EVENT msg from: %s", client_name.c_str());
            bt_man_ptr_->HandleRetryEventsInIdleState();
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

void IdleState::ActionOnEntry() {
}

void IdleState::ActionOnExit() {
}

bool IdleState::MeetsExitCriteria() {
    // NOTE: What if installer app is detected and speed is also triggered? -> Whichever comes *first*
    bool status = true;
    return status;
}

bool IdleState::MeetsEntryCriteria() {
    bool status = true;
    return status;
}

} // namespace device

} // namespace nd
