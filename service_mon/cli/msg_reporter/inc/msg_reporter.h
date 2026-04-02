/* Copyright (C) 2025 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Siva <sivanandha.barathy@netradyne.com> December 2025
 */


#ifndef ND_DEVICE_SERVICES_SERVICE_MON_CLI_MSG_REPORTER_INC_MSG_REPORTER_H
#define ND_DEVICE_SERVICES_SERVICE_MON_CLI_MSG_REPORTER_INC_MSG_REPORTER_H

#include <string>

#include <service_utils.h>

namespace nd::utils {

enum class ErrorCode {
    kDefault = -1,
    kSuccess = 0,
    kInvalidMessage = 1,
    kJsonParseError = 2,
    kSendError = 3
};

class MessageReporter {
  public:
    MessageReporter(const std::string& serviceName);
    ~MessageReporter();

    ErrorCode SendCriticalInfoMessage(const int error_code, const std::string& message);
    ErrorCode SendHealthInfoMessage(const std::string& message);

  private:
    std::string serviceName;
    NDService* nd_service_obj;
};

} // namespace nd::utils

#endif // ND_DEVICE_SERVICES_SERVICE_MON_CLI_MSG_REPORTER_INC_MSG_REPORTER_H
