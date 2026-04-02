/*****************************************************************************************
 *
 *	@file		:	mcs_dbg.h
 *	@brief		:	It contain Debug level options and macros used to 
 *					enable/disable the debug functionality.
 *  @author		:	B. Venkata Durga Prasad, VVDN Technologies Pvt. Ltd.
 *  Copyright	:	(c) 2016-2017 , VVDN Technologies Pvt. Ltd.
 *  				Permission is hereby granted to everyone in VVDN Technologies
 *  				to use the Software without restriction,including without
 *  				limitation the rights to use, copy, modify, merge, publish,
 *  				distribute, distribute with modifications.
 *
 ******************************************************************************************/

#ifndef MCS_DBG_H
#define MCS_DBG_H

#define MCS_DEBUG

/*
 * @Macro	:	MCS_DEBUG
 * @brief	:	To print function level debug messages
 * 				If enabled print function level debug messages
 * 				If disabled print only error messages
 */

#ifdef MCS_DEBUG
#define mcs_dbg printf
#else
#define mcs_dbg(...)
#endif

/* By default all error messages are enabled */
#define mcs_err printf

#endif
