/* Copyright (C) 2019 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Suresh Kumar <suresh.kumar@netradyne.com>, Spet 2019
 */
#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include <nd_net_utils.h>
#include <nd_time.h>
#include <regex>
#include <nd_task.h>
#include <nd_msgq.h>
#include <nd_msg_utils.h>
#include <nd_msg_types.h>
#include <log.h>
#include <config_parser.h>
#include "service_utils.h"

void *udid_thread (void *arg);

#endif //#ifndef TIME_SYNC_H
