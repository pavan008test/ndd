/* Copyright (C) 2025 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Siva <sivanandha.barathy@netradyne.com> December 2025
 */

#include <iostream>
#include <string>

#include <system_utils.h>

#include <msg_reporter.h>

namespace {
    enum class ExitCode { 
        kDefault = -1,
        kSuccess = 0,
        kErrorNoMessageType,
        kErrorInvalidCriticalInfoArgs,
        kErrorConvertingErrorCode,
        kErrorSendCriticalInfo,
        kErrorInvalidHealthInfoArgs,
        kErrorSendHealthInfo,
        kErrorInvalidMessageType,
        kErrorException
    };
}

int main(int argc, char* argv[]) {
    int ret = static_cast<int>(ExitCode::kDefault);
    do {
        if (argc <= 2) {
            std::cerr << "Error: No message type provided" << std::endl;
            ret = static_cast<int>(ExitCode::kErrorNoMessageType);
            break;
        }
        std::string messageType = argv[1];
        std::string serviceName = argv[2];

        try {
            using namespace nd::utils;
            MessageReporter reporter(serviceName);

            if (messageType == "critical_info") {
                std::cout << "Processing critical_info message" << std::endl;

                if (argc != 5) {
                    std::cerr << "Error: Invalid number of arguments for critical info message" << std::endl;
                    ret = static_cast<int>(ExitCode::kErrorInvalidCriticalInfoArgs);
                    break;
                }
                int error_code = static_cast<int>(ErrorCode::kDefault);
                if (!string_to_integer(argv[3], error_code)) {
                    std::cerr << "Error: Invalid error code for critical info message" << std::endl;
                    ret = static_cast<int>(ExitCode::kErrorConvertingErrorCode);
                    break;
                }
                std::string message = argv[4];

                ErrorCode result = reporter.SendCriticalInfoMessage(error_code, message);
                if (result != ErrorCode::kSuccess) {
                    std::cerr << "Error: Failed to send critical info message" << std::endl;
                    ret = static_cast<int>(ExitCode::kErrorSendCriticalInfo);
                    break;
                }
            } else if (messageType == "health_info") {
                std::cout << "Processing health_info message" << std::endl;

                if (argc != 4) {
                    std::cerr << "Error: Invalid number of arguments for health info message" << std::endl;
                    ret = static_cast<int>(ExitCode::kErrorInvalidHealthInfoArgs);
                    break;
                }
                std::string message = argv[3];

                ErrorCode result = reporter.SendHealthInfoMessage(message);
                if (result != ErrorCode::kSuccess) {
                    std::cerr << "Error: Failed to send health info message" << std::endl;
                    ret = static_cast<int>(ExitCode::kErrorSendHealthInfo);
                    break;
                }
            } else {
                std::cerr << "Error: Invalid message type '" << messageType << "'" << std::endl;
                ret = static_cast<int>(ExitCode::kErrorInvalidMessageType);
                break;
            }
            ret = static_cast<int>(ExitCode::kSuccess);
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            ret = static_cast<int>(ExitCode::kErrorException);
            break;
        }
    } while (false);

    return ret;
}
