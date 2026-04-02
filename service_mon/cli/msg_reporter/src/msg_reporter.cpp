/* Copyright (C) 2025 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Siva <sivanandha.barathy@netradyne.com> December 2025
 */

#include <iostream>
#include <stdexcept>

#include <jansson/jansson.h>

#include <service_utils.h>
#include <system_utils.h>

#include <msg_reporter.h>

namespace nd::utils {

MessageReporter::MessageReporter(const std::string& serviceName)
    : serviceName(serviceName), nd_service_obj(nullptr) {
    const std::string TAG = serviceName;
    nd_service_obj = NDService::get_service_obj(TAG);
    if (nd_service_obj == nullptr) {
        throw std::runtime_error("Failed to get service object");
    }
}

MessageReporter::~MessageReporter() {
    if (nd_service_obj) {
        nd_service_obj->release_service_obj();
    }
}

ErrorCode MessageReporter::SendCriticalInfoMessage(const int error_code, const std::string& message) {
    ErrorCode ret = ErrorCode::kDefault;
    do {
        std::cout << "Sending critical info message for service: " << serviceName << std::endl;
        if (message.empty()) {
            std::cerr << "Error: Invalid critical info message" << std::endl;
            ret = ErrorCode::kInvalidMessage;
            break;
        }
        std::string trimmed_message = nd_trim(message);

        if (!nd_service_obj->send_err_msg(static_cast<err_code_t>(error_code), NDService::UNUSED_ERR_AUX_CODE, trimmed_message)) {
            std::cout << "Error: Failed to send critical info message" << std::endl;
            ret = ErrorCode::kSendError;
            break;
        }
        ret = ErrorCode::kSuccess;
    } while (false);
    std::cout << "Critical info message sent status: " << static_cast<int>(ret) << std::endl;
    return ret;
}

ErrorCode MessageReporter::SendHealthInfoMessage(const std::string& message) {
    ErrorCode ret = ErrorCode::kDefault;
    std::cout << "Sending health info message for service: " << serviceName << std::endl;

    do {
        if (message.empty()) {
            std::cerr << "Error: Health stats message cannot be empty" << std::endl;
            ret = ErrorCode::kInvalidMessage;
            break;
        }

        std::string trimmed_message = nd_trim(message);

        json_error_t error;
        json_t* jsonMessage = json_loads(trimmed_message.c_str(), 0, &error);
        if (!jsonMessage) {
            std::cerr << "Error: Invalid JSON format in health stats message: " << error.text << std::endl;
            ret = ErrorCode::kJsonParseError;
            break;
        } else {
            json_decref(jsonMessage);
        }

        std::cout << "Data: " << trimmed_message << std::endl;

        const char* msg_char = trimmed_message.c_str();
        size_t messageLength = trimmed_message.length();

        if (!nd_service_obj->send_msg_healthstats(const_cast<char*>(msg_char), messageLength)) {
            std::cerr << "Error: Failed to send health stats message" << std::endl;
            ret = ErrorCode::kSendError;
            break;
        }

        ret = ErrorCode::kSuccess;
    } while (false);
    std::cout << "Health info message sent status: " << static_cast<int>(ret) << std::endl;

    return ret;
}

} // namespace nd::utils
