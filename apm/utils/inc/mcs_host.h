/*****************************************************************************************
 *
 *	@file		:	mcs_host.h
 *	@brief		:	It contain Macro's and function prototypes and structure
 *					definitions related to the mcs controller.
 *  @author		:	B. Venkata Durga Prasad, VVDN Technologies Pvt. Ltd.
 *  Copyright	:	(c) 2016-2017 , VVDN Technologies Pvt. Ltd.
 *  				Permission is hereby granted to everyone in VVDN Technologies
 *  				to use the Software without restriction,including without
 *  				limitation the rights to use, copy, modify, merge, publish,
 *  				distribute, distribute with modifications.
 *
 ******************************************************************************************/

#ifndef MCS_HOST_H
#define MCS_HOST_H

#ifdef __cplusplus
extern "C" {
#endif

#define MCS_FIRMWARE_VERSION_LEN (8)
#define MCS_PARAM_MAX_DATA_LEN (512)

#define CMD_START_FLAG 0x49434443	/* command start flag: fixed 4 Bytes */
#define END_OF_PKT_FLAG 0x26112611

/*
 * Acknowledgement status codes
 */

typedef enum ACK_STATUS {
	ACK_SUCCESS=0xB0,
	ACK_UNKNOWN_ID,
	ACK_UNKNOWN_PKT,
	ACK_INVALID_DATA,

} ACK_STATUS_T;

/*
 * Command status codes
 */

typedef enum CMD_STATUS {
	CMD_SUCCESS=0xA0,
	CMD_UNKNOWN_ID,
	CMD_UNKNOWN_PKT,
	CMD_INVALID_DATA,

} CMD_STATUS_T;

/*
 * Control status codes
 */

typedef enum CONTROL_STATUS {
	CONTROL_FREE = 0x05,
	CONTROL_BUSY = -100,
	CONTROL_FATAL_ERROR = -101,
	CONTROL_CPU_OVERLOAD = -102,
	CONTROL_MEMORY_LEAK = -103,
	CONTROL_INCORRECT_STATE = -104,
	CONTROL_FIRMWARE_CORRUPTED = -105,

} CONTROL_STATUS_T;

/*
 * Packet types
 */

typedef enum PACKET_TYPE {
	SYS_WR_PKT = 0x10,
	SYS_RD_PKT,
	VEH_WR_PKT,
	VEH_RD_PKT,
	INT_RD_PKT,
	GPIO_INT_PKT,
	CMD_STATUS_PKT,
	CONTROL_STATUS_PKT,
	RESPONSE_RDY_PKT,
	FW_UP_PKT,
        BROADCAST_CONF_PKT,

} PACKET_TYPE_T;

/*
 * Command/Acknowledgement ID's
 */

typedef enum CMD_ACK_ID {
	/* System related */
	GPIO_GET_CONFIG = 0xA0,
	GPIO_SET_CONFIG,
	GPIO_SET,
	GPIO_GET,
	GPIO_INTR,
#ifdef KRAIT
	MCS_POR = 0xAF,
#endif
	SW_CONFIG=0xB0,
	STACK_CONFIG,
#ifdef KRAIT
    GET_ADC_DATA,
	GET_ADC_HISTORY,
    SET_LOW_POWER_MODE,
#endif
	STACK_CONFIG_J1939,
	STACK_CONFIG_OBD2,
    SET_DEVICE_ID,
	GET_DEVICE_ID,
    ENABLE_J1939_DM_REQ,
    STACK_INIT_J1939,
	SYS_PARAM_SET_TIMEO = 0xC0, /* command timeout at controller */

        /* Broadcast message related */
        /* Start Broadcast message */
        BRD_MSG_START = 0xC5,
        /* configure J1939 SPNs */
        BRD_CONF_J1939,
        /* configure proprietary CAN IDs */
        BRD_CONF_PROP,
        /* configure OBD PIDs Fast */
        BRD_CONF_OBD_FAST,
        /* configure OBD PIDs Medium */
        BRD_CONF_OBD_MED,
        /* configure OBD PIDs Slow */
        BRD_CONF_OBD_SLOW,
        /*configure can bus bw limit*/
        BRD_CONF_CAN_BW_LIMIT,
        /*dtc config for OBD*/
        BRD_CONF_OBD_DTC,
        /*dtc config for J1939*/
         BRD_CONF_J1939_DTC,


	/* Vehicle related */
	VEH_PARAM_SET=0xD0,
	VEH_PARAM_GET_ALL_PARAM,	/* get all parameters values */
	VEH_PARAM_GET_LIST_PARAM,	/* get list of supported params	*/
	VEH_PARAM_GET_SINGLE_PARAM,	/* get single param value */
	VEH_PARAM_SET_PARAM_UPDATE_TIME, /* Enable periodic update of parameters with time specified */
	VEH_PARAM_FETCH_LIST_START,	/* fetch list start packet ack id */
	VEH_PARAM_FETCH_LIST_INTER,	/* fetch list inter packet ack id */
	VEH_PARAM_FETCH_LIST_END,	/* fetch list end packet ack id */
	VEH_PARAM_GET_ALL_DATA_START,	/* Get all param data start packet ack id */
	VEH_PARAM_GET_ALL_DATA_INTER,	/* Get all param data inter packet ack id */
	VEH_PARAM_GET_ALL_DATA_END,	/* Get all param data end packet ack id */
//    VEH_PARAM_GET_SINGLE_PARAM_TIMESTAMP, /* Get single parameter data with timestamp */
	VEH_PARAM_GET_VIN,
	GET_UART_DEBUG_BUFFER,
    VEH_PARAM_GET_VIN_DATA = 0xDD,
    VEH_PARAM_GET_VIN_ASYNC = 0xDE,
     GET_REQUEST_BASED_SPN = 0xDF,
	DIAG_CODE_CMD = 0xE0,		/* Diagnostic trouble code command */

	CONTROL_STATUS_CMD,

	/*Boot related */
	CONTROL_BOOT_GET_MODE,
	CONTROL_BOOT_PARTITION,
	CONTROL_BOOT_GET_LIST_PARTITION,
	CONTROL_BOOT_SET_VALID_PARTITION,

	/* Firmware update related */
	FW_IMG_SETUP_CMD = 0xF0,
	FW_IMG_START_CMD,
	FW_IMG_INTER_CMD,
	FW_IMG_END_CMD,

} CMD_ACK_ID_T;

/*
 * System Command data
 */

typedef enum SYS_CMD_DATA {
	CUR_STACK=0xBE,
	SW_VERSION=0xBF,

} SYS_CMD_DATA_T;

/*
 * Command/Acknowledgement boot data
 */

typedef enum CMD_ACK_BOOT_DATA {
	CONTROL_BOOT_STATE_BL = 0x20,
	CONTROL_BOOT_STATE_AP_1,
	CONTROL_BOOT_STATE_AP_2,
	CONTROL_BOOT_PARTITION_1,
	CONTROL_BOOT_PARTITION_2,
	CONTROL_BOOT_STATE_UNKNOWN = 0xFF,

} CMD_ACK_BOOT_DATA_T;

/*
 * System status
 */

typedef enum SYSTEM_STATUS {
	PROCESS_SUCCESS = 200,
	PROCESS_CAN_BUS_ERROR,
	PROCESS_FLASH_RW_ERROR,
	PROCESS_CRC_CHECK_ERROR,
	PROCESS_UART_RW_ERROR,
	PROCESS_INTERNAL_TIMEOUT,
	PROCESS_FETCH_SPN_ERROR,
	PROCESS_GPIO_CONFIG_ERROR,

} SYSTEM_STATUS_T;

/*
 * Status codes
 */

typedef enum MCS_STATUS {
	MCS_SUCCESS = 0x0,
	MCS_FAILURE = -1,
	MCS_ERR_INCORRECT_STATE = -10,
	MCS_ERR_INSUFFICIENT_RESOURCE = -13,
	MCS_ERR_INVALID_PARAMS = -14,
	MCS_ERR_PARTITION_CORRUPT = -15,
	MCS_ERR_FIRMWARE_CORRUPT = -16,
	MCS_ERR_CHECKSUM_FAIL = -17,
	MCS_ERR_CONTROL_FATAL = -18,
	MCS_ERR_CONTROL_TIMEOUT = -19,
	MCS_ERR_CONTROLE_STACK_PANIC = -20,
	MCS_ERR_VEHICLE_DIAGNOSTIC_FAIL = -21,
	MCS_ERR_VEHICLE_COMMUNICATION_FAIL = -22,

} MCS_STATUS_T;

/*
 * List of supported stacks
 */
#if 0
typedef enum MCS_STACK {
	MCS_STACK_J1939 = 0x20,
	MCS_STACK_OBD,
	MCS_STACK_UNKNOWN,
        MCS_CAN_DETECT_ERR,

} MCS_STACK_T;
#endif

typedef enum MCS_STACK
{
    MCS_INIT_PROTOCOL =0U,
    MCS_NO_CAN_BUS_ACTIVITY,
    MCS_OBD_500K,
    MCS_OBD_250K,
    MCS_EXOBD_500K,
    MCS_EXOBD_250K,
    MCS_J1939_250K,
    MCS_J1939_500K,
    MCS_NO_STACK_FOUND

} MCS_STACK_T;

/*
 * Micro controller Bootup stages
 */

typedef enum MCS_BOOT_STATE {
	MCS_STATE_BL = 0x10,
	MCS_STATE_AP_PARTITION_1,
	MCS_STATE_AP_PARTITION_2,
	MCS_STATE_NONE,

} MCS_BOOT_STATE_T;

#ifdef __cplusplus
}
#endif

#endif
