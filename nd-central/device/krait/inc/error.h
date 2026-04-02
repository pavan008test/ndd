/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Arun V <arun.vj@netradyne.com>, October 2016
 */

#ifndef ERROR_H
#define ERROR_H

enum    component_error_t {
	WARN_CAMERA_BUFFER,
	WARN_ENCODE_BUFFER,
	WARN_UNKNOWN,

	FATAL_CAMERA_BUFFER,
	FATAL_CAMERA_OTHER,

	FATAL_ENCODE_BUFFER,
	FATAL_ENCODE_OTHER,
	
	FATAL_UNKNOWN

};

#endif

