#ifndef SVC_COMMON_H
#define SVC_COMMON_H

#include "nd_factory.h"
#include <syslog.h>

extern ND_DeviceFactory *nd_device_obj;  // nd device object based on deviceType
extern int use_printf_log;

#define SVC_LOG_C(TAG, FORMAT_STRING, ...) \
    do { \
        common_print(LOG_LEVEL_C, TAG, __func__, __LINE__, FORMAT_STRING, ##__VA_ARGS__); \
        if (use_printf_log) { \
            syslog(LOG_INFO, "[CRITICAL][%s][%s:%d] " FORMAT_STRING, TAG, __func__, __LINE__, ##__VA_ARGS__); \
        } \
    } while(0)

#define SVC_LOG_E(TAG, FORMAT_STRING, ...) \
    do { \
        common_print(LOG_LEVEL_E, TAG, __func__, __LINE__, FORMAT_STRING, ##__VA_ARGS__); \
        if (use_printf_log) { \
            syslog(LOG_INFO, "[ERROR][%s][%s:%d] " FORMAT_STRING, TAG, __func__, __LINE__, ##__VA_ARGS__); \
        } \
    } while(0)

#define SVC_LOG_I(TAG, FORMAT_STRING, ...) \
    do { \
        common_print(LOG_LEVEL_I, TAG, __func__, __LINE__, FORMAT_STRING, ##__VA_ARGS__); \
        if (use_printf_log) { \
            syslog(LOG_INFO, "[INFO][%s][%s:%d] " FORMAT_STRING, TAG, __func__, __LINE__, ##__VA_ARGS__); \
        } \
    } while(0)

#endif
