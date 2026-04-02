/* Copyright (C) 2018 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Arun V <arun.vj@netradyne.com>, Apr 2018
 */

#ifndef PM_INTERNAL_H
#define PM_INTERNAL_H

#define TAG "SM"
#define Q_NAME "SM"

#define SPLIT_LOGS 30 //Split logs every SPLIT_LOGS minutes

bool add_err_log(uint64_t timestamp, string process_name, int code, int code_aux, string desc);

#endif

