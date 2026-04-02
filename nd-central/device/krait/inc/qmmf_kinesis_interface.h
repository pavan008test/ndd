/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Pawan Kumar <pawan.kumar@netradyne.com>, October 2016
 */

#ifndef QMMF_KINESIS_INT_H
#define QMMF_KINESIS_INT_H

#include <stdint.h>
#include <com/amazonaws/kinesis/video/cproducer/Include.h>

#define DEFAULT_RETENTION_PERIOD            2 * HUNDREDS_OF_NANOS_IN_AN_HOUR
#define DEFAULT_BUFFER_DURATION             120 * HUNDREDS_OF_NANOS_IN_A_SECOND
#define DEFAULT_CALLBACK_CHAIN_COUNT        5
#define DEFAULT_KEY_FRAME_INTERVAL          45
#define DEFAULT_FPS_VALUE                   25
#define DEFAULT_STREAM_DURATION             20 * HUNDREDS_OF_NANOS_IN_A_SECOND
#define DEFAULT_STORAGE_SIZE                20 * 1024 * 1024
#define RECORDED_FRAME_AVG_BITRATE_BIT_PS   3800000
#define QMMF_FRAME_AVG_BITRATE_BIT_PS       5000000
#define AWS_KINESIS_FPS                     10
#define MAX_FRAME_DROP_ERROR_CNT            12

#define IOT_CERT_PATH                                      (PCHAR) "/home/ubuntu/.nddevice/certificate/certificate.pem.crt"
#define IOT_CERT_PRIVATE_KEY_PATH                          (PCHAR) "/home/ubuntu/.nddevice/certificate/private.pem.key"
#define CA_CERT_PATH                                       (PCHAR) "/home/ubuntu/.nddevice/certificate/cacert.pem"
#define IOT_ROLE_ALIAS                                     (PCHAR) "KvsCameraIoTRoleAlias"

#define DEVICE_STATE                                        (PCHAR) "staging"
#define MAX_FRAME_BUFFER_SIZE                               640 * 480 * 3 * 2
#define MAX_RETRY_ATTEMPTS                                  3

static const string ND_DEVICE_REL_PATH = "/home/ubuntu/.nddevice";

#define FILE_LOGGING_BUFFER_SIZE            (100 * 1024)
#define MAX_NUMBER_OF_LOG_FILES             5
#define QMMF_FILE_LOGGER_LOG_FILE_DIRECTORY_PATH         "/data/kvs"
typedef struct aws_kinesis_stream_info
{
    PDeviceInfo pDeviceInfo = NULL;
    PStreamInfo pStreamInfo = NULL;
    PClientCallbacks pClientCallbacks = NULL;
    PStreamCallbacks pStreamCallbacks = NULL;
    CLIENT_HANDLE clientHandle = INVALID_CLIENT_HANDLE_VALUE;
    STREAM_HANDLE streamHandle = INVALID_STREAM_HANDLE_VALUE;
    Frame frame;
    UINT32 frameIndex;
    req_livestreaming_data_t req_stream;
    bool first_time;
    char iot_str[100];
}aws_kinesis_stream_info_t;

bool stopkinesis (void *data, req_livestreaming_data_t *kinesis_req_msg);
bool startkinesis (void *data, req_livestreaming_data_t *req_msg);

#endif


