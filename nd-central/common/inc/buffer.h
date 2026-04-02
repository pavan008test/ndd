/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Arun V <arun.vj@netradyne.com>, October 2016
 */

#ifndef BUFFER_H
#define BUFFER_H

/**
 * @brief Generic buffer, needs rework
 *
 */
struct buffer_t {
	void *buff;
	int  size;
};

#endif

