/* Copyright (C) 2017 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Arun V <arun.vj@netradyne.com>, May 2017
 */

#ifndef SVC_INTERNAL_H
#define SVC_INTERNAL_H

#include <svc.h>

#define TAG "SVC"
#define Q_NAME "SVC"

#define SPLIT_LOGS 30 //Split logs every SPLIT_LOGS minutes
extern bool init_watchdog();
extern bool kick_watchdog();

extern bool recovery_init(int poll);
extern bool diskmon_init(int poll);
extern void diskmon_cleanup_process();

#endif

