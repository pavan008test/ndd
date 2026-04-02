/* Copyright (C) 2022 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, October 2022
 */

#ifndef INC_ND_BT_PLATFORM_SPECIFIC_H_
#define INC_ND_BT_PLATFORM_SPECIFIC_H_

#include <nd_msg_types.h>
#include <service_utils.h>

namespace nd {

namespace platform_specific {

bool IndicateInstallerActivity(const char *src, const char *dest, std::atomic<int> &msg_id);

std::string FetchMsgClientName(void *msg);

bool FetchIgnitionStatus(void *msg, ignition_status_t &ign_status, bool &is_wakeup_out);
bool FetchWriteCharData(void *msg, std::string &mac_out, std::string &uuid_out, std::vector<std::string> &data_out);
bool FetchIdleEventUpdate(void *msg, int &handle, bool &idle_on);
bool FetchSpeedEventUpdate(void *msg, int &handle, int &speed);

bool SendCreateHotspotMsg(bool enable, int band_type, const std::string &addr, const char *src, const char *dest,
                          std::atomic<int> &msg_id);
bool SendDriverLoginAudioNotification(const std::string &file, const char *src, const char *dest,
                                      std::atomic<int> &msg_id);
bool SendDriverLoginQueryResponse(std::vector<std::string> ids, std::vector<std::string> timestamps, bool idle_on,
                                  bool privacy_mode, const char *src, const char *dest, std::atomic<int> &msg_id);
bool SendDeviceWriteCharResponse(bool write_status, BTErrCode err_code, const char *src, const char *dest,
                                 std::atomic<int> &msg_id);
bool SendPrivacyModeToObservers(bool privacy_mode, const char *src, const char *dest, std::atomic<int> &msg_id);

// bool SendDriverAppLoginStatusToObservers(bool login, const char *src, const char *dest, std::atomic<int> &msg_id);
bool SendEngineIdleStatusToObservers(bool idle, const char *src, const char *dest, std::atomic<int> &msg_id);

bool SendDriverIdsToObservers(std::vector<std::string> ids, std::vector<std::string> timestamps,
                              const char *src, const char *dest, std::atomic<int> &msg_id);

bool SendInternalEventMessage(const char *msg_name, const char* q_name, std::atomic<int> &msg_id, int32_t event);
bool FetchInternalEventMessage(void *msg, int32_t &event);

bool FetchSpeedEventRegistrationResponse(void *msg, int &handle, speed_reg_type_t &type);
bool FetchIdleEventRegistrationResponse(void *msg, int &handle, idle_reg_type_t &type);
bool SendSpeedRegistrationRequest(int speed, int secs, speed_reg_type_t type,
                                 const char *src, const char *dest, std::atomic<int> &msg_id);
bool SendFrStatusToObservers(bool fr_status, const char *src, const char *dest, std::atomic<int> &msg_id);

bool SendUnregisterSpeedEventHandle(int handle, const char *src, const char *dest, std::atomic<int> &msg_id);
bool SendIdleRegistrationRequest(int speed, int secs, idle_reg_type_t type,
                                 const char *src, const char *dest, std::atomic<int> &msg_id);
bool SendUnregisterIdleEventHandle(int handle, const char *src, const char *dest, std::atomic<int> &msg_id);
bool SendDriverLoginUpdate(bool login_status, const char *src, const char *dest, std::atomic<int> &msg_id);
bool SendAntennaTimeResponse(const char *src, const char *dest, std::atomic<int> &msg_id);
bool SendGenericMessage(msg_type_t type, const char *msg_name, const char *src, const char *dest, std::atomic<int> &msg_id);
bool SendVbusAvailabilityMessage(bool detected, const char *src, const char *dest, std::atomic<int> &msg_id);
bool SendUserAlertMessage(int button, uint64_t tstamp, const std::string &alert_src,
                          const char *src, const char *dest, std::atomic<int> &msg_id);
bool SendGetIgnitionStatus(const char *src, const char *dest, std::atomic<int> &msg_id);
bool SendAlertBeaconPairedMessage(const char *src, const char *dest, std::atomic<int> &msg_id);
bool SendLedBlinkMessage(const led_blink_status_t led_status, const uint16_t interval_sec,
                         const char *src, const char *dest, std::atomic<int> &msg_id);
NDService * CreateServiceObj(const std::string &name, std::function<void(int signum)> sig_usr_fn_cb);
bool SendStartQRScanMsg(const std::vector<std::string> &qr_login_tags, const char* src,
                        const char* dest, std::atomic<int> &msg_id);
bool SendStopQRScanMsg(const char* src, const char* dest, std::atomic<int> &msg_id);
bool FetchQRScanStartStatus(void *msg, QrScanStatusCodes &status_code, std::string &reason);
bool FetchQRScanStopStatus(void *msg, QrScanStatusCodes &status_code, std::string &reason);
bool SendInternalEventMessage(const char *msg_name, const char* q_name, std::atomic<int> &msg_id, int32_t event);
bool FetchInternalEventMessage(void *msg, int32_t &event);
bool GetCurrentIgnitionStatus(ignition_status_t &ign_status, bool &is_wakeup_out);
}  // namespace platform_specific

}  // namespace nd

#endif  // INC_ND_BT_PLATFORM_SPECIFIC_H_
