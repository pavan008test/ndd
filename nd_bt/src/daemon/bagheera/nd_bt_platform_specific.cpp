/* Copyright (C) 2023 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, January 2023
 */

#include <atomic>
#include <chrono>
#include <string>
#include <vector>

#include <log.h>
#include <nd_msg_types.h>
#include <nd_msg_utils.h>
#include <system_utils.h>
#include <nd_factory.h>

#define nd_device_obj (ND_DeviceFactory::Create_NDDevice())

namespace nd {

namespace platform_specific {

static const char *const kLogTag = "BTPLSP";

bool FetchIdleEventRegistrationResponse(void *msg, int &handle, idle_reg_type_t &type) {
    bool status = false;
    const res_idle_reg_msg_t *idle_reg_msg = reinterpret_cast<res_idle_reg_msg_t *>(msg);

    switch (idle_reg_msg->reg_type) {

        case IDLE_REG_ENGINE: {
            LOG_I(kLogTag, "IDLE_REG_ENGINE Idle Registration with handle: %d status: %d",
                  idle_reg_msg->handle, idle_reg_msg->res);
            if (STATUS_OK == idle_reg_msg->res) {
                status = true;
                handle = idle_reg_msg->handle;
                type = idle_reg_msg->reg_type;
            } else {
                LOG_E(kLogTag, "IDLE_REG_ENGINE Registration failed");
            }
            break;
        }

        case IDLE_REG_DL_FR: {
            LOG_I(kLogTag, "IDLE_REG_DL_FR Idle Registration with handle: %d status: %d",
                  idle_reg_msg->handle, idle_reg_msg->res);
            if (STATUS_OK == idle_reg_msg->res) {
                status = true;
                handle = idle_reg_msg->handle;
                type = idle_reg_msg->reg_type;
            } else {
                LOG_E(kLogTag, "IDLE_REG_DL_FR Registration failed");
            }
            break;
        }

        case IDLE_REG_DL: {
            LOG_I(kLogTag, "IDLE_REG_DL Idle Registration with handle: %d status: %d",
                  idle_reg_msg->handle, idle_reg_msg->res);
            if (STATUS_OK == idle_reg_msg->res) {
                status = true;
                handle = idle_reg_msg->handle;
                type = idle_reg_msg->reg_type;
            } else {
                LOG_E(kLogTag, "IDLE_REG_DL Registration failed");
            }
            break;
        }

        case IDLE_REG_PRIVACY: {
            LOG_I(kLogTag, "IDLE_REG_PRIVACY Idle Registration with handle: %d status: %d",
                  idle_reg_msg->handle, idle_reg_msg->res);
            if (STATUS_OK == idle_reg_msg->res) {
                status = true;
                handle = idle_reg_msg->handle;
                type = idle_reg_msg->reg_type;
            } else {
                LOG_E(kLogTag, "IDLE_REG_PRIVACY Registration failed");
            }
            break;
        }

        case IDLE_REG_QR: {
            LOG_I(kLogTag, "IDLE_REG_QR Idle Registration with handle: %d status: %d",
                  idle_reg_msg->handle, idle_reg_msg->res);
            if (STATUS_OK == idle_reg_msg->res) {
                status = true;
                handle = idle_reg_msg->handle;
                type = idle_reg_msg->reg_type;
            } else {
                LOG_E(kLogTag, "IDLE_REG_QR Registration failed");
            }
            break;
        }

        default: {
            LOG_E(kLogTag, "Unknown Idle Registration received: %d with handle: %d status: %d",
                  idle_reg_msg->reg_type, idle_reg_msg->handle, idle_reg_msg->res);
            break;
        }
    }

    return status;
}

bool SendIdleRegistrationRequest(int speed, int secs, idle_reg_type_t type,
                                 const char *src, const char *dest, std::atomic<int> &msg_id) {
    bool status = false;
    req_idle_reg_msg_t req_msg{};
    req_msg.speed = speed;
    req_msg.idle_secs = secs;
    req_msg.reg_type = type;

    if (send_msg(reinterpret_cast<generic_msg_t *>(&req_msg), REQ_IDLE_REG, sizeof(req_idle_reg_msg_t),
                 src, dest, ++msg_id)) {
        status = true;
        LOG_I(kLogTag, "Register with speed service for idle event %d", static_cast<int32_t>(type));
    } else {
        LOG_E(kLogTag, "Failed to register with speed service for idle event %d", static_cast<int32_t>(type));
    }
    return status;
}

bool FetchSpeedEventRegistrationResponse(void *msg, int &handle, speed_reg_type_t &type) {
    bool status = false;
    const res_speed_reg_msg_t *speed_reg_msg = reinterpret_cast<res_speed_reg_msg_t *>(msg);

    switch (speed_reg_msg->reg_type) {

        case SPEED_REG_DRV_LOGIN: {
            LOG_I(kLogTag, "SPEED_REG_DRV_LOGIN Speed Registration with handle: %d status: %d",
                  speed_reg_msg->handle, speed_reg_msg->res);
            if (STATUS_OK == speed_reg_msg->res) {
                status = true;
                handle = speed_reg_msg->handle;
                type = speed_reg_msg->reg_type;
            } else {
                LOG_E(kLogTag, "SPEED_REG_DRV_LOGIN Registration failed");
            }
            break;
        }

        case SPEED_REG_PRIVACY: {
            LOG_I(kLogTag, "SPEED_REG_PRIVACY Speed Registration with handle: %d status: %d",
                  speed_reg_msg->handle, speed_reg_msg->res);
            if (STATUS_OK == speed_reg_msg->res) {
                status = true;
                handle = speed_reg_msg->handle;
                type = speed_reg_msg->reg_type;
            } else {
                LOG_E(kLogTag, "SPEED_REG_PRIVACY Registration failed");
            }
            break;
        }

        case SPEED_REG_QR: {
            LOG_I(kLogTag, "SPEED_REG_QR Speed Registration with handle: %d status: %d",
                  speed_reg_msg->handle, speed_reg_msg->res);
            if (STATUS_OK == speed_reg_msg->res) {
                status = true;
                handle = speed_reg_msg->handle;
                type = speed_reg_msg->reg_type;
            } else {
                LOG_E(kLogTag, "SPEED_REG_QR Registration failed");
            }
            break;
        }

        default: {
            LOG_E(kLogTag, "Unknown Speed Registration received: %d with handle: %d status: %d",
                  speed_reg_msg->reg_type, speed_reg_msg->handle, speed_reg_msg->res);
            break;
        }
    }
    return status;
}

bool SendSpeedRegistrationRequest(int speed, int secs, speed_reg_type_t type,
                                 const char *src, const char *dest, std::atomic<int> &msg_id) {
    bool status = false;
    req_speed_reg_msg_t req_msg{};

    req_msg.speed = speed;
    req_msg.contig_secs = secs;
    req_msg.reg_type = type;

    if (send_msg(reinterpret_cast<generic_msg_t *>(&req_msg), REQ_SPEED_REG, sizeof(req_speed_reg_msg_t),
                 src, dest, ++msg_id)) {
        status = true;
        LOG_I(kLogTag, "Register with speed service for speed event %d", static_cast<int32_t>(type));
    } else {
        LOG_E(kLogTag, "Sending ide fr status to nd-central failed");
    }
    return status;
}

bool SendFrStatusToObservers(bool fr_status, const char *src, const char *dest, std::atomic<int> &msg_id) {
    bool status = false;
    driver_login_fr_update_msg_t msg{};
    msg.status = fr_status;
    msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    if (send_msg(reinterpret_cast<generic_msg_t *>(&msg), DRIVER_LOGIN_UPDATE, sizeof(driver_login_fr_update_msg_t),
                 src, dest, ++msg_id)) {
        status = true;
    } else {
        LOG_E(kLogTag, "Sending ide fr status to nd-central failed");
    }
    return status;
}

bool SendUnregisterSpeedEventHandle(int handle, const char *src, const char *dest, std::atomic<int> &msg_id) {
    bool status = false;
    req_speed_unreg_msg_t req_msg{};

    req_msg.handle = handle;

    if (send_msg(reinterpret_cast<generic_msg_t *>(&req_msg), REQ_SPEED_UNREG, sizeof(req_speed_unreg_msg_t),
                 src, dest, ++msg_id)) {
        status = true;
        LOG_I(kLogTag, "Unregister with speed service for handle %d", handle);
    } else {
        LOG_E(kLogTag, "Failed to unregister with speed service for handle %d", handle);
    }
    return status;
}

bool SendUnregisterIdleEventHandle(int handle, const char *src, const char *dest, std::atomic<int> &msg_id) {
    bool status = false;
    req_idle_unreg_msg_t req_msg{};

    req_msg.handle = handle;

    if (send_msg(reinterpret_cast<generic_msg_t *>(&req_msg), REQ_IDLE_UNREG, sizeof(req_idle_unreg_msg_t),
                 src, dest, ++msg_id)) {
        status = true;
        LOG_I(kLogTag, "Unregister with idle service for handle %d", handle);
    } else {
        LOG_E(kLogTag, "Failed to unregister with idle service for handle %d", handle);
    }
    return status;
}

bool SendDriverIdsToObservers(std::vector<std::string> ids, std::vector<std::string> timestamps,
                              const char *src, const char *dest, std::atomic<int> &msg_id) {
    bool status = false;

    drv_id_update_msg_t msg{};
    msg.num_drvids = ids.size();

    for (size_t pos = 0; pos < ids.size(); ++pos) {
        nd_strncpy(msg.drv_ids[pos], ids[pos].c_str(), MAX_DRV_ID_LEN);
        nd_strncpy(msg.login_time[pos], timestamps[pos].c_str(), MAX_APP_LOGIN_TIME_LEN);
    }

    if (send_msg(reinterpret_cast<generic_msg_t *>(&msg), DRVID_UPDATE, sizeof(drv_id_update_msg_t),
                 src, dest, ++msg_id)) {
        LOG_I(kLogTag, "Sent driver ids to observers");
        status = true;
    } else {
        LOG_E(kLogTag, "Sending driver ids to observers failed");
    }

    return status;
}

bool SendEngineIdleStatusToObservers(bool idle, const char *src, const char *dest, std::atomic<int> &msg_id) {
    bool status = false;

    eng_idle_update_msg_t msg{};
    msg.idle_on = idle;

    if (send_msg(reinterpret_cast<generic_msg_t *>(&msg), ENG_IDLE_UPDATE, sizeof(eng_idle_update_msg_t),
                 src, dest, ++msg_id)) {
        LOG_I(kLogTag, "Sent engine idle status to observers");
        status = true;
    } else {
        LOG_E(kLogTag, "Sending engine idle status to observers failed");
    }

    return status;
}

// bool SendDriverAppLoginStatusToObservers(bool login, const char *src, const char *dest, std::atomic<int> &msg_id) {
//     bool status = false;

//     driveri_app_login_update_msg_t msg{};
//     msg.status = login;

//     if (send_msg(reinterpret_cast<generic_msg_t *>(&msg), DRIVER_LOGIN_UPDATE, sizeof(driveri_app_login_update_msg_t),
//                  src, dest, ++msg_id)) {
//         LOG_I(kLogTag, "Sent driver app login status to observers");
//         status = true;
//     } else {
//         LOG_E(kLogTag, "Sending driver app login status to observers failed");
//     }

//     return status;
// }

bool SendPrivacyModeToObservers(bool privacy_mode, const char *src, const char *dest, std::atomic<int> &msg_id) {
    bool status = false;

    privacy_mode_update_msg_t msg{};
    msg.privacy_on = privacy_mode;

    if (send_msg(reinterpret_cast<generic_msg_t *>(&msg), PRIVACY_MODE_UPDATE, sizeof(privacy_mode_update_msg_t),
                 src, dest, ++msg_id)) {
        LOG_I(kLogTag, "Sent privacy mode to observers");
        status = true;
    } else {
        LOG_E(kLogTag, "Sending privacy mode to observers failed");
    }

    return status;
}

bool SendDriverLoginAudioNotification(const std::string &file, const char *src, const char *dest,
                                      std::atomic<int> &msg_id) {
    bool status = false;
    driver_login_audio_notify_msg_t msg{};

    nd_strncpy(msg.file, file.c_str(), FNAME_LEN);

    if (send_msg(reinterpret_cast<generic_msg_t *>(&msg), DRIVER_LOGIN_AUDIO_NOTIFY,
                 sizeof(driver_login_audio_notify_msg_t), src, dest, ++msg_id)) {
        LOG_I(kLogTag, "Sent driver login audio play notify file: %s", file.c_str());
        status = true;
    } else {
        LOG_E(kLogTag, "Sending driver login audio play notify failed");
    }
    return status;
}

bool SendDriverLoginQueryResponse(std::vector<std::string> ids, std::vector<std::string> timestamps,
                                  bool idle_on, bool privacy_mode, const char *src,
                                  const char *dest, std::atomic<int> &msg_id) {
    bool status = false;

    resp_drv_login_query resp{};
    resp.num_drvids = ids.size();
    resp.idle_on = idle_on;
#ifdef ENABLE_PRIVACY_BT
    resp.privacy_on = privacy_mode;
#endif
    for (size_t pos = 0; pos < ids.size(); ++pos) {
        nd_strncpy(resp.drv_ids[pos], ids[pos].c_str(), MAX_DRV_ID_LEN);
        nd_strncpy(resp.login_time[pos], timestamps[pos].c_str(), MAX_APP_LOGIN_TIME_LEN);
    }

    if (send_msg(reinterpret_cast<generic_msg_t *>(&resp), RES_DRV_LOGIN_QUERY,
                 sizeof(resp_drv_login_query), src, dest, ++msg_id)) {
        LOG_I(kLogTag, "Sent driver login query response");
        status = true;
    } else {
        LOG_E(kLogTag, "Sending driver login query response failed");
    }

    return status;
}

bool SendDeviceWriteCharResponse(bool write_status, BTErrCode err_code, const char *src, const char *dest,
                                 std::atomic<int> &msg_id) {
    bool status = false;

    bt_device_write_char_response_t resp{};
    resp.response = write_status;
    resp.code = err_code;

    if (send_msg(reinterpret_cast<generic_msg_t *>(&resp), DEVICE_WRITE_CHAR_RESPONSE,
                 sizeof(bt_device_write_char_response_t), src, dest, ++msg_id)) {
        LOG_I(kLogTag, "Sent device write char response message");
        status = true;
    } else {
        LOG_E(kLogTag, "Sending device write char response message failed");
    }

    return status;
}

bool SendCreateHotspotMsg(bool enable, int band_type, const std::string &addr, const char *src, const char *dest,
                          std::atomic<int> &msg_id) {
    bool status = false;

    create_installer_hotspot_t req_msg{};

    req_msg.enable = enable;
    req_msg.band_type = band_type;

    constexpr size_t kMacAddressLength = 18;
    nd_strncpy(req_msg.mac_addr, addr.c_str(), kMacAddressLength);

    if (send_msg(reinterpret_cast<generic_msg_t *>(&req_msg), CREATE_INSTALLER_HOTSPOT, sizeof(create_installer_hotspot_t),
                 src, dest, ++msg_id)) {
        status = true;
        LOG_E(kLogTag, "Sent create hotspot request");
    } else {
        LOG_E(kLogTag, "Sending create hotspot request failed");
    }
    return status;
}

bool FetchIdleEventUpdate(void *msg, int &handle, bool &idle_on) {
    bool status = false;

    const res_idle_update_msg_t *idle_update_msg = reinterpret_cast<res_idle_update_msg_t *>(msg);

    handle = idle_update_msg->handle;
    idle_on = idle_update_msg->idle_on;

    status = true;

    return status;
}

bool FetchSpeedEventUpdate(void *msg, int &handle, int &speed) {
    bool status = false;

    const res_speed_update_msg_t *speed_update_msg = reinterpret_cast<res_speed_update_msg_t *>(msg);

    handle = speed_update_msg->handle;
    speed = speed_update_msg->speed;

    status = true;

    return status;
}

bool FetchIgnitionStatus(void *msg, ignition_status_t &ign_status, bool &is_wakeup_out) {
    bool status = false;
    is_wakeup_out = false;

    powermon_ignition_msg_t *ignition_status_msg = reinterpret_cast<powermon_ignition_msg_t *>(msg);

    if (power_crank_levels_t::CRANK_HIGH == ignition_status_msg->status) {
        ignition_status_msg->lpw_status = static_cast<int64_t>(lpw_state_t::eLPW_OFF);
    } else {
        ignition_status_msg->lpw_status = get_status_from_sysfs_source(PowermonParam::eLPW_STAT);
        int wake_up_value = 0;
        std::string wake_up_reason;

        if (!nd_device_obj->get_reset_wake_reason(wake_up_value, wake_up_reason)) {
            LOG_E(kLogTag, "get_reset_wake_reason failed");
        } else {
            LOG_I(kLogTag, "Wake up reason: %s", wake_up_reason.c_str());

            if (wake_up_value & WAKE_ON_MOT_IMU_MASK) {
                is_wakeup_out = true;
                LOG_I(kLogTag, "Power on reason is WoM");
            }
        }
    }

    LOG_I(kLogTag, "Ign status = %lld, crank_change_time = %lld, lpw_status = %lld, is_wakeup = %d",
          ignition_status_msg->status, ignition_status_msg->crank_change_time,
          ignition_status_msg->lpw_status, is_wakeup_out);

    ign_status = static_cast<ignition_status_t>(ignition_status_msg->status);

    status = true;

    return status;
}

bool FetchWriteCharData(void *msg, std::string &mac_out, std::string &uuid_out, std::vector<std::string> &data_out) {
    bool status = false;

    const bt_device_write_char_msg_t *bt_device_write_char_msg = reinterpret_cast<bt_device_write_char_msg_t *>(msg);

    mac_out = bt_device_write_char_msg->mac_addr;
    uuid_out = bt_device_write_char_msg->uuid;

    std::vector<std::string> data_copy{};
    data_copy.reserve(MAX_BT_WRITE_CHAR_ARRAY_SIZE);

    for (size_t data_itr = 0; data_itr < MAX_BT_WRITE_CHAR_ARRAY_SIZE; ++data_itr) {

        if (0 == bt_device_write_char_msg->size_array[data_itr]) {
            continue;
        }

        const std::string data_element(bt_device_write_char_msg->data_array[data_itr],
                                       bt_device_write_char_msg->size_array[data_itr]);

        data_copy.emplace_back(std::move(data_element));

        // if (0 != strlen(bt_device_write_char_msg->data[data_itr])) {
        //     data_copy.emplace_back(bt_device_write_char_msg->data[data_itr]);
        //     LOG_I(kLogTag, "Data[%lu]: %s", data_itr, bt_device_write_char_msg->data[data_itr]);
        // } else {
        //     LOG_I(kLogTag, "Data[%lu]: NULL", data_itr);
        // }
    }

    data_out = std::move(data_copy);

    status = true;

    return status;
}

std::string FetchMsgClientName(void *msg) {
    const generic_msg_t *generic_msg = reinterpret_cast<generic_msg_t *>(msg);
    // const std::string name(generic_msg->client_id, CLIENT_ID_LEN);
    const std::string name(generic_msg->client_id);
    return name;
}

bool IndicateInstallerActivity(const char *src, const char *dest, std::atomic<int> &msg_id) {
    bool status = false;

    generic_msg_t msg{};

    if (send_msg(&msg, INSTALLER_SCAN_INDICATE, sizeof(generic_msg_t), src, dest, ++msg_id)) {
        LOG_I(kLogTag, "Sent INSTALLER_SCAN_INDICATE message");
        status = true;
    } else {
        LOG_E(kLogTag, "Sending INSTALLER_SCAN_INDICATE message failed");
    }

    return status;
}

// bool FetchBleAlertConfigData(void *msg, std::string &label_out, std::string &uuid_out, std::string &mac_addr_out) {
//     bool status = true;

//     const ble_accessory_data_t *accessory_msg = reinterpret_cast<ble_accessory_data_t *>(msg);

//     label_out = accessory_msg->label;
//     uuid_out = accessory_msg->uuid;
//     mac_addr_out = accessory_msg->mac_addr;

//     return status;
// }

bool SendGetIgnitionStatus(const char *src, const char *dest, std::atomic<int> &msg_id) {
    bool status = false;

    generic_msg_t msg{};
    if (send_msg(reinterpret_cast<generic_msg_t *>(&msg), GET_POWERMON_IGNITION_STATUS, sizeof(msg),
                 src, dest, ++msg_id)) {
        LOG_I(kLogTag, "Sent GET_POWERMON_IGNITION_STATUS message");
        status = true;
    } else {
        LOG_E(kLogTag, "Sending GET_POWERMON_IGNITION_STATUS message failed");
    }

    return status;
}

bool SendAntennaTimeResponse(const char *src, const char *dest, std::atomic<int> &msg_id) {
    bool status = false;

    generic_msg_t msg{};
    if (send_msg(reinterpret_cast<generic_msg_t *>(&msg), REQUEST_ANTENNA_TIME_RESPONSE, sizeof(msg),
                 src, dest, ++msg_id)) {
        LOG_I(kLogTag, "Sent REQUEST_ANTENNA_TIME_RESPONSE message");
        status = true;
    } else {
        LOG_E(kLogTag, "Sending REQUEST_ANTENNA_TIME_RESPONSE message failed");
    }

    return status;
}

bool SendGenericMessage(msg_type_t type, const char *msg_name, const char *src, const char *dest, std::atomic<int> &msg_id) {
    bool status = false;

    generic_msg_t msg{};

    if (send_msg(&msg, type, sizeof(generic_msg_t), src, dest, ++msg_id)) {
        LOG_I(kLogTag, "Sent %s message", msg_name);
        status = true;
    } else {
        LOG_E(kLogTag, "Sending %s message failed", msg_name);
    }

    return status;
}

bool SendInternalEventMessage(const char *msg_name, const char* q_name, std::atomic<int> &msg_id, int32_t event) {
    bool status = false;
    nd_bt_internal_event_t msg{};

    msg.event_ = event;

    if (send_msg(reinterpret_cast<generic_msg_t *>(&msg), BT_INTERNAL_EVENT,
                 sizeof(nd_bt_internal_event_t), q_name, q_name, ++msg_id)) {
        LOG_I(kLogTag, "Sent %s message", msg_name);
        status = true;
    } else {
        LOG_E(kLogTag, "Sending %s message failed", msg_name);
    }

    return status;
}

bool FetchInternalEventMessage(void *msg, int32_t &event) {
    bool status = false;

    const nd_bt_internal_event_t *internal_event_msg = reinterpret_cast<nd_bt_internal_event_t *>(msg);
    event = internal_event_msg->event_;
    status = true;

    return status;
}

bool SendUserAlertMessage(int button, uint64_t tstamp, const std::string &alert_src,
                          const char *src, const char *dest, std::atomic<int> &msg_id) {
    bool status = false;

    user_alert_msg_t msg{};
    msg.button = button;
    msg.timestamp = tstamp;
    nd_strncpy(msg.source, alert_src.c_str(), sizeof(msg.source));

    if (send_msg(reinterpret_cast<generic_msg_t *>(&msg), USER_ALERT, sizeof(user_alert_msg_t), src, dest, ++msg_id)) {
        LOG_I(kLogTag, "Sent USER_ALERT msg");
        status = true;
    } else {
        LOG_E(kLogTag, "USER_ALERT message failed to be sent");
    }

    return status;
}

bool SendVbusAvailabilityMessage(bool detected, const char *src, const char *dest, std::atomic<int> &msg_id) {
    bool status = false;

    vbus_bt_availability_t msg{};
    msg.bt_available = detected;

    if (send_msg(reinterpret_cast<generic_msg_t *>(&msg), VBUS_BT_AVAILABILITY, sizeof(vbus_bt_availability_t),
                 src, dest, ++msg_id)) {
        LOG_I(kLogTag, "Sent VBUS_BT_AVAILABILITY msg");
        status = true;
    } else {
        LOG_E(kLogTag, "VBUS_BT_AVAILABILITY message failed to be sent");
    }

    return status;
}

bool SendAlertBeaconPairedMessage(const char *src, const char *dest, std::atomic<int> &msg_id) {
    bool status = false;
    generic_msg_t msg{};
    if (send_msg(reinterpret_cast<generic_msg_t *>(&msg), BLE_ALERT_DEVICE_PAIRED, sizeof(msg), src, dest,
                ++msg_id) ) {
        LOG_I(kLogTag, "BLE_ALERT_DEVICE_PAIRED send_msg success");
        status = true;
    } else {
        LOG_E(kLogTag, "BLE_ALERT_DEVICE_PAIRED send_msg failed");
    }
    return status;
}

bool SendLedBlinkMessage(const led_blink_status_t led_status, const uint16_t timeout_sec,
                         const char *src, const char *dest, std::atomic<int> &msg_id) {
    bool status = false;

    led_blink_req_msg_t msg{};
    msg.blink_status = led_status;
    msg.blink_timeout_sec = timeout_sec;
    LOG_I(kLogTag, "Sending LED_BLINK_REQ msg with status: %d, interval sec: %llu src: %s dest: %s",
          led_status, timeout_sec, src, dest);
    if (send_msg(reinterpret_cast<generic_msg_t *>(&msg), INSTALLER_SCAN_INDICATE, sizeof(led_blink_req_msg_t),
                 src, dest, ++msg_id)) {
        status = true;
    } else {
        LOG_E(kLogTag, "LED_BLINK_REQ message failed to be sent");
    }

    return status;
}

bool SendStartQRScanMsg(const std::vector<std::string> &qr_login_tags, const char* src,
                        const char* dest, std::atomic<int> &msg_id) {
    bool status = false;
    req_qr_login_scan_msg_t msg{};

    for (size_t pos = 0; pos < qr_login_tags.size() && pos < kQRScanMaxTagPatterns; ++pos) {
        nd_strncpy(msg.tags[pos], qr_login_tags[pos].c_str(), kQRScanMaxTagPatternDataLen);
    }

    if (send_msg(reinterpret_cast<generic_msg_t *>(&msg), START_QR_SCAN_REQ, sizeof(req_qr_login_scan_msg_t),
                                                      src, dest, ++msg_id) ) {
        LOG_I(kLogTag, "START_QR_SCAN_REQ send_msg success");
        status = true;
    } else {
        LOG_E(kLogTag, "START_QR_SCAN_REQ send_msg failed");
    }

    return status;
}

bool SendStopQRScanMsg(const char* src, const char* dest, std::atomic<int> &msg_id) {
    bool status = false;

    generic_msg_t msg{};

    if (send_msg(reinterpret_cast<generic_msg_t *>(&msg), STOP_QR_SCAN_REQ, sizeof(generic_msg_t),
                 src, dest, ++msg_id)) {
        LOG_I(kLogTag, "STOP_QR_SCAN_REQ send_msg success");
        status = true;
    } else {
        LOG_E(kLogTag, "STOP_QR_SCAN_REQ send_msg failed");
    }

    return status;
}

bool FetchQRScanStartStatus(void *msg, QrScanStatusCodes &status_code, std::string &reason) {
    bool status = false;

    const resp_qr_login_scan_status_msg_t *qr_scan_status_msg = reinterpret_cast<resp_qr_login_scan_status_msg_t *>(msg);

    LOG_I(kLogTag, "QR Scan Status: %d Reason: %s", qr_scan_status_msg->status,
          qr_scan_status_msg->reason);

    status_code = qr_scan_status_msg->status;

    reason = qr_scan_status_msg->reason;

    status = true;

    return status;
}

bool FetchQRScanStopStatus(void *msg, QrScanStatusCodes &status_code, std::string &reason) {
    bool status = false;

    const resp_qr_login_scan_status_msg_t *qr_scan_status_msg = reinterpret_cast<resp_qr_login_scan_status_msg_t *>(msg);

    LOG_I(kLogTag, "QR Scan Stop Status: %d Reason: %s", qr_scan_status_msg->status,
          qr_scan_status_msg->reason);

    status_code = qr_scan_status_msg->status;

    reason = qr_scan_status_msg->reason;

    status = true;

    return status;
}

bool GetCurrentIgnitionStatus(ignition_status_t &ign_status, bool &is_wakeup_out) {
    bool status = false;

    is_wakeup_out = false;
    ign_status = static_cast<ignition_status_t>(nd_device_obj->get_crank_level());

    if (power_crank_levels_t::CRANK_HIGH == ign_status) {
        LOG_I(kLogTag, "Current Ignition Status = CRANK_HIGH");
    } else {
        int wake_up_value = 0;
        std::string wake_up_reason;

        if (!nd_device_obj->get_reset_wake_reason(wake_up_value, wake_up_reason)) {
            LOG_E(kLogTag, "get_reset_wake_reason failed");
        } else {
            LOG_I(kLogTag, "Wake up reason: %s", wake_up_reason.c_str());

            if (wake_up_value & WAKE_ON_MOT_IMU_MASK) {
                is_wakeup_out = true;
                LOG_I(kLogTag, "Power on reason is WoM");
            }
        }
    }

    LOG_I(kLogTag, "Current Ignition Status = %d, is_wakeup = %d", static_cast<int32_t>(ign_status), is_wakeup_out);

    status = true;

    return status;
}

NDService* CreateServiceObj(const std::string &name, std::function<void(int signum)> sig_usr_fn_cb) {
    return NDService::get_service_obj(name, false, std::move(sig_usr_fn_cb));
}

}  // namespace platform_specific

}  // namespace nd
