
/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Arun V <arun.vj@netradyne.com>, February 2016
 */

#ifndef AWS_IOT_MSG
#define AWS_IOT_MSG

enum awsmsg_type_t
{
    INTERNAL_INVALID_MSG = PRIVATE_MSG,

    INTERNAL_AWS_POLL,
    INTERNAL_AWS_RECV,
    INTERNAL_AWS_DO,
    INTERNAL_AWS_DONE,
    INTERNAL_AWS_DELETE,
    INTERNAL_AWS_PUBLISH_GPS,
    INTERNAL_AWS_PROCESS_SHADOW_UPDATE
};

struct awsiot_internal_msg_t
{

    GENERIC_MSG
};

struct pub_gps_info_msg_t
{
    GENERIC_MSG

    double latitude;
    double longitude;
    double accuracy;
    double altitude;
    float speed;
    int64_t timestamp;
    double bearing;
    int ignition;
    bool valid;
};
#endif
