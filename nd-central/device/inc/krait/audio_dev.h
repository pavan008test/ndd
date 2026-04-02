/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Arun V <arun.vj@netradyne.com>, October 2016
 */

#ifndef AUDIO_DEV_H
#define AUDIO_DEV_H

#include <audio_record.h>

bool audio_dev_reg_cb(Audio::audio_callback_t *cb, Audio::audio_err_callback_t *err_cb );

bool audio_start_recorder();

bool audio_stop_recorder();

bool audio_encode_dev(string input, string output);

#endif
