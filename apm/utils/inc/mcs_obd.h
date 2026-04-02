/*****************************************************************************************
 *
 *	@file		:	mcs_obd.h
 *	@brief		:	It contain Macro's and function prototypes and structure
 *					definitions used in the mcs_obd.c file.
 *  @author		:	B. Venkata Durga Prasad, VVDN Technologies Pvt. Ltd.
 *  Copyright	:	(c) 2016-2017 , VVDN Technologies Pvt. Ltd.
 *  				Permission is hereby granted to everyone in VVDN Technologies
 *  				to use the Software without restriction,including without
 *  				limitation the rights to use, copy, modify, merge, publish,
 *  				distribute, distribute with modifications.
 *
 ******************************************************************************************/

#ifndef MCS_OBD_H
#define MCS_OBD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <semaphore.h>
#include <signal.h>
#include <sys/time.h>

#include "mcs_host.h"
#include "mcs_spi.h"

#define OBD_DEVICE "/dev/spidev0.0"
#define OBD_SPI_MAX_CLOCK_FREQ 2000000	/* 2Mhz */

#define VEH_PARAM_IDS_DFL_CNT 1024
#define VEHICLE_PARAM_DATA_MAX_LEN 24
#define LAMPORT_TIME_OFFSET 8
/*
 * MCS configurable gpio's
 */

#define MCS_INPUT_GPIO_0 0x0001
#define MCS_INPUT_GPIO_1 0x0002
#define MCS_INPUT_GPIO_2 0x0004
#define MCS_INPUT_GPIO_3 0x0008
#define MCS_INPUT_GPIO_4 0x0010
#define MCS_INPUT_GPIO_5 0x0020
#define MCS_INPUT_GPIO_6 0x0040
#define MCS_INPUT_GPIO_7 0x0080
#define MCS_OPENDRAIN_GPIO_8 0x0100
#define MCS_OPENDRAIN_GPIO_9 0x0200
#define MCS_OPENDRAIN_GPIO_10 0x0400
#define MCS_OPENDRAIN_GPIO_11 0x0800
#define MCS_OPENDRAIN_GPIO_12 0x1000
#define MCS_OPENDRAIN_GPIO_13 0x2000
#define MCS_OPENDRAIN_GPIO_14 0x4000
#define MCS_OPENDRAIN_GPIO_15 0x8000

#define ETIMEOUT 0xEA
#define EOBD_BUSY 0xEB

typedef enum MCS_CAPTURE_DATA_TYPE {
	/* Read/capture data from the vehicle */
	MCS_CAPTURE_LIVE_DATA,
	/* Read/capture data from the record */
	MCS_CAPTURE_RECORD_DATA,

} MCS_CAPTURE_DATA_TYPE_T;

enum protocol_type{
    J1939_AND_OBD2 =0,
    ONLY_J1939 ,
    ONLY_OBD2
};

enum can_report_t{
	INS_APP = 0,
	IDMS
};

typedef struct {
	int br_sw;
	int left_turn;
	int right_turn;
	int cruise_cntrl;
	int abs;
	double speed;
    double rpm;
    double e_hrs;
    double odo;
	double odo_prop;
	double tfu;
	double th_pos;
	double acc_pad_pos;
	double br_pad_pos;
	}eld_param_struct;

#define START_TIMER obdCtx.timer.its.it_value.tv_sec = obdCtx.timer.cmd_ack_timeout; \
					setitimer (ITIMER_REAL, &obdCtx.timer.its, 0)
#define STOP_TIMER obdCtx.timer.its.it_value.tv_sec = 0; \
					setitimer (ITIMER_REAL, &obdCtx.timer.its, 0)

#define IS_TIMEOUT obdCtx.timer.cmd_ack_timeout_flag?1:0

#define SET_STATUS_AS_CMD_ACK_TIMEOUT	mcs_err ("Ohh Noo...Timeout!! No interrupt\n");\
										status = -ETIMEOUT
#define OBD_LOCK

/* locking mechanism */
#define OBD_MUTEX_INIT sem_init
#define OBD_MUTEX_DESTROY sem_destroy
#define OBD_ACQUIRE_LOCK sem_wait
#define OBD_RELEASE_LOCK sem_post

#define SET_STATUS_AS_INVALID_HANDLE	mcs_err ("Err: Invalid handler\n"); \
										status = -ESPI_HANDLE
#define MAX_IOSIX_FIRMWARE_SIZE 8

#define WAIT_ON_INTERRUPT_READ OBD_ACQUIRE_LOCK (&obdCtx.trans_lock)
#define WAIT_ON_INTERRUPT_READ_COMPLETE OBD_RELEASE_LOCK (&obdCtx.trans_lock)
#define WAIT_ON_DATA_COPY OBD_ACQUIRE_LOCK (&obdCtx.data_lock)
#define WAIT_ON_DATA_COPY_COMPLETE OBD_RELEASE_LOCK (&obdCtx.data_lock)

#define REGISTER_CUR_TRANSACTION(pkt) obdCtx.cur_transID = pkt.cmd_id
#define UNREGISTER_CUR_TRANSACTION obdCtx.cur_transID = 0

/*****************************************************************************************
 *
 * @structure	:	Command packet
 * @mem1		:	start_flag, is fixed 4 byte value as (0x49434443)
 * @mem2		:	pkt_type, packet type
 * @mem3		:	data_len, data length
 * @mem4		:	cmd_id, command id
 * @mem5		:	cmd_data, array of characters which is used to hold the cmd data
 *
 ****************************************************************************************/

typedef struct command_packet {
	unsigned int start_flag;
	char pkt_type;
	char data_len;
	char cmd_id;
	char cmd_data[MCS_PARAM_MAX_DATA_LEN];
} cmd_pkt_t;

/*****************************************************************************************
 *
 * @structure	:	Acknowledgement packet
 * @mem1		:	start_flag, is fixed 4 byte value as (0x49434443)
 * @mem2		:	pkt_type, packet type
 * @mem3		:	data_len, data length
 * @mem4		:	ack_id, acknowledgement id
 * @mem5		:	ack_data, pointer to hold the acknowledgement data
 *
 ****************************************************************************************/

typedef struct acknowledgement_packet {
	char pkt_type;
	short int data_len;
	char ack_id;
	char *ack_data;
} ack_pkt_t;


typedef struct broadcast_packet {
    int start_flag;
    char packet_length;
    char param_id[2];
    char data_length;
    int data;
    unsigned int timestamp;
    unsigned int sync_timestamp;
    short int checksum;
} brd_pkt_t;


/*****************************************************************************************
 *
 * @structure	:	vehicle_param_data
 * @mem1		:	param_id, parameter id related to the parameter @param_data
 * @mem2		:	param_len, parameter length, to identify the parameter length.
 * @mem3		:	param_data, is a character pointer which holds the list of
 * 					paramters. Parameters can be either SPN/PID's.
 *
 * SPN			:	Suspect Parameter Number
 * PID			:	Parameter Identifier
 *
 *****************************************************************************************/

typedef struct vehicle_param_data {
	int param_id;
	int16_t param_len;
	unsigned char param_data[VEHICLE_PARAM_DATA_MAX_LEN];
} vehicle_param_data_t;

typedef struct vehicle_param_broadcast_data {
    int param_id;
    int16_t param_len;
    unsigned char param_data[VEHICLE_PARAM_DATA_MAX_LEN];
    unsigned char can_timestamp[4];
    	uint64_t timestamp;
	int is_old_data;
	int idle_count;
	uint32_t seq_id;
} vehicle_param_broadcast_data_t;

typedef struct iosix_dtc_data{

        uint16_t mil_lamp;
        uint8_t dtc_count;
        uint8_t bus_type;
        unsigned int dtc_data[64];
	char firmware_version[MAX_IOSIX_FIRMWARE_SIZE];
}iosix_dtc_data_t;

enum vehicle_bus_type{

	J1939_BUS =1,
	J1708_BUS,
	OBDII_BUS
};

/*****************************************************************************************
 *
 * @structure	:	diag_param_data
 * @mem1		:	param_id, parameter id related to the DTC code.
 * @mem2		:	fmi_value, FMI value related to DTC
 *
 * DTC			:	Diagnostic Trouble Code
 * FMI			:	Failure Mode Indicator
 *
 *****************************************************************************************/

typedef struct diag_param_data {
	int param_id;
	int fmi_value;
} diag_param_data_t;

/*****************************************************************************************
 *
 * @structure	:	firmware data
 * @mem1		:	part_id, partition id can be partition1 or 2.
 * @mem2		:	version, which specifies the latest firmware version
 * @mem3		:	app_blk_addr, address of the application block in fw image.
 * @mem4		:	app_blk_size, size of the application block in fw image.
 * @mem5		:	ivt_blk_addr, address of the ivt block in fw image.
 * @mem6		:	ivt_blk_size, size of the ivt block in fw image.
 * @mem7		:	crc_check_sum, check sum for the new fw image.
 *
 * FW			:	FirmWare
 * IVT			:	Interrupt Vector Table
 *
 *****************************************************************************************/

typedef struct firmware_image_header {
	unsigned char part_id;
	unsigned char version[MCS_FIRMWARE_VERSION_LEN];
	unsigned char app_blk_addr[4];
	unsigned char app_blk_size[2];
	unsigned char ivt_blk_addr[4];
	unsigned char ivt_blk_size[2];
	unsigned char checksum_size[2];
	unsigned char crc_check_sum[2];
} firmware_image_header_t;

/*****************************************************************************************
 *
 * @structure	:	partition_info
 * @mem1		:	part_id, partition id partition 1 or 2
 * @mem2		:	isValid, 1 for valid and 0 for invalid.
 * @mem3		:	version, which specifies the latest firmware version
 *
 *****************************************************************************************/

typedef struct partition_info {
	unsigned char part_id;
	unsigned char isValid;
	unsigned char version[MCS_FIRMWARE_VERSION_LEN];
} partition_info_t;

/*****************************************************************************************
*
* @structure	:	veh_paramID_info
* @mem1			:	param, Contains paramter ID
* @mem2			:	param_len, holds the parameter ID response length
*
*****************************************************************************************/

typedef struct veh_paramID_info {
	unsigned int param;
	int param_len;
} veh_paramID_info_t;

/**************	Call back Functions Prototypes *****************/

/*
 * @function	:	Error status callback
 * @param1		:	status, error status code
 * @param2		:	app, gives the data to the call back function,
 * 					which was given while registering call back.
 * @retrun		:	0 on success, and err number on failure.
 * @brief		:	To intimate the user with the registered call back
 * 					with the internal error code or vehicle related
 * 					error status codes.
 */

typedef int (*err_status_cb)(int status,void *app);

/*
 * @function	:	all param data callback
 * @param1		:	params, array of vehicle_param_data type.
 * @parma2		:	count, number of SPN/PID's.
 * @param3		:	app, gives data to the call back function,
 * 					which was given while registering call back.
 * @return		:	0 on success, and err number on failure.
 * @brief		:	To retrieve the all SPN/PID data from the
 * 					micro controller periodically.
 */

typedef int (*all_param_data_cb)(vehicle_param_data_t *params,int count, void *app);

/*
 * @function	:	diag param data callback
 * @param1		:	params, array of vehicle_param_data type.
 * @parma2		:	count, number of SPN/PID's.
 * @param3		:	app, gives data to the call back function,
 * 					which was given while registering call back.
 * @return		:	0 on success, and err number on failure.
 * @brief		:	To retrieve the DTC information from the vehicle.
 */

typedef int (*diag_param_data_cb)(diag_param_data_t *params, int count, void *app);

/*
 * @function	:	gpio_notify_callback_cb
 * @param1		:	count, tells number of input gpio's got interrupted
 * @param2		:	gpio_num, holds the gpio numbers
 * 					which got interrupted (value got changed).
 * @param3		:	val, holds the values of the gpio numbers.
 * @param4		:	app, gives data to the call back function
 * 					which was given while registering call back.
 * @return		:	0 on success, and err number on failure.
 * @brief		:	To Notify the call back function, whenever
 * 					there is a chage in gpio state (interrupted).
 */

typedef int (*gpio_notify_callback_cb)(int count, int *gpio_num, int *val, void *app);

/******************* Function prototypes ***********************/

/*
 * @function	:	mcs_open
 * @param		:	void, None
 * @return		:	On Success, returns a valid handler
 * 					On Failure, returns NULL.
 * @brief		:	Opens the Micro controller stack.
 */

void* mcs_open (void);

/*
 * @function	:	mcs_close
 * @param1		:	handl, is returned by mcs_open
 * @return		:	0 on success, and err number on failure
 * @brief		:	Closes the Micro controller stack.
 */

int mcs_close (void *handl);

/*
 * @function	:	set_mcs_error_callback
 * @param1		:	handl, is returned by mcs_open
 * @param2		:	callback, err_status_cb type
 * @param3		:	app, passes the buffer to the call back function.
 * @return		:	0 on success, and err number on failure
 * @brief		:	Register call back function for the error status
 *
 * NOTE			:	app shouldn't be NULL, pointer should be valid.
 */

int set_mcs_error_callback(void *handl, err_status_cb callback, void *app);

/*
 * @function	:	set_mcs_diag_param_data_callback
 * @param1		:	handl, is returned by mcs_open
 * @param2		:	callback, call back function of diag_param_data_cb
 * @param3		:	app, passes the buffer to the call back function.
 * @retrun		:	0 on success, and err number on failure
 * @brief		:	Register call back function to get DTC from vehicle.
 *
 * NOTE			:	app shouldn't be NULL, pointer should be valid.
 */

int set_mcs_diag_param_data_callback(void *handl, diag_param_data_cb callback, void *app);

/*
 * @function	:	set_mcs_vehicle_param_callback
 * @param1		:	handl, is returned by mcs_open
 * @param2		:	callback, call back function of all_param_data_cb
 * @param3		:	app, passes the buffer to the call back function.
 * @retrun		:	0 on success, and err number on failure
 * @brief		:	Register call back function to get periodic update
 * 					of vehicle parameters with an interval of given time.
 *
 * NOTE			:	app shouldn't be NULL, pointer should be valid.
 */

int set_mcs_vehicle_param_callback(void *handl, all_param_data_cb callback, void *app);

/*
 * @function	:	set_mcs_interrupt_gpio_notify_callback
 * @param1		:	handl, is returned by mcs_open
 * @param2		:	callback, call back function of interrupt_gpio_notify_cb
 * @param3		:	app, passes the buffer to the call back function.
 * @return		:	0 on success, and err number on failure
 * @brief		:	Registering GPIO interrupt pins notify callback
 *
 * NOTE			:	app shouldn't be NULL, pointer should be valid.
 */

int set_mcs_interrupt_gpio_notify_callback(void *handl, gpio_notify_callback_cb callback, void *app);

/*
 * @function	:	get_mcs_partition
 * @param1		:	handl, is returned by mcs_open
 * @param2		:	count, which will store the read mcs partition.
 * @return		:	0 on success, and err number on failure.
 * @brief		:	Gets the micro controller partition.
 */

int get_mcs_partition(void *handl, int *count);

/*
 * @function	:	get_mcs_boot_state
 * @param1		:	handl, is returned by mcs_open
 * @param2		:	mode, which will store the read mcs boot state
 * @return		:	0 on success, and err number on failure.
 * @brief		:	Gets the micro controller boot state.
 * 					On success, data can be on of the following three.
 * 					MCS_STATE_BL,
 * 					MCS_STATE_AP_PARTITION_1,
 * 					MCS_STATE_AP_PARTITION_2.
 * 					On failure, data as MCS_STATE_NONE
 */

int get_mcs_boot_state (void *handl, CMD_ACK_BOOT_DATA_T *mode);
/*
 * @function	:	get_mcs_partition_info
 * @param1		:	handl, is returned by mcs_open
 * @param2		:	part_info, holds the read partition info
 * @param3		:	count, tells the available partitions count
 * @return		:	0 on success, and err number on failure.
 * @brief		:	Gets the micro controller partition info.
 */

int get_mcs_partition_info(void *handl,partition_info_t *part_info,  int count);

/*
 * @function	:	set_mcs_partition_valid
 * @param1		:	handl, is returned by mcs_open
 * @param2		:	partid, partition id
 * @param		:	valid, if 1 set as valid, else set as invalid.
 * @return		:	0 on success, and err number on failure.
 * @brief		:	Sets the mcs partition as valid or invalid.
 */

int set_mcs_partition_valid (void *handl, int partid, int valid);

/*
 * @function	:	finish_mcs_boot_partition
 * @param1		:	handl, is returned by mcs_open
 * @param2		:	partid, partition id
 * @return		:	0 on success, and err number on failure.
 * @brief		:	sends command to the micro controller with
 * 					partition id which application chooses.
 */

int finish_mcs_boot_partition(void *handl, int partid);

/*
 * @function	:	get_mcs_vehicle_stack
 * @param1		:	handl, is returned by mcs_open
 * @param2		:	stack, holds the micro controller current stack
 * @return		:	0 on success, and err number on failure
 * @brief		:	gets stack supported by vehicle, it can be
 *
 * NOTE			:	Refer MCS_STACK_T for types of stacks.
 */

int get_mcs_vehicle_stack(void *handl, MCS_STACK_T *stack, int pd_id);
int get_device_id(void *handl, unsigned char* dev, int* len);
int write_device_id(void *handl, int device_id);

bool Init_J1939_protocol(void *handle);
//KRAIT
int get_uart_debug_buff(void *handl, unsigned char *data, int *len);

/*
 * @function	:	get_mcs_vehicle_param_count
 * @param1		:	handl, is returned by mcs_open
 * @param2		:	count, holds the SPN/PID count value
 * @return		:	0 on success, and err number on failure
 * @brief		:	Gets supported vehicle parameters count
 */

int get_mcs_vehicle_param_count(void *handl, int *count);

/*
 * @function	:	get_mcs_vehicle_param_ids
 * @param1		:	handl, is returned by mcs_open
 * @param2		:	id_arr, an array holds the SPN/PID values
 * @param3		:	count, tells the count of id's in the id_arr
 * @return		:	0 on success, and err number on failure
 * @brief		:	Gets the supported vehicle parameter ID's
 */

int get_mcs_vehicle_param_ids(void *handl,int *id_arr,int count);

/*
 * @function	:	get_mcs_vehicle_param_data_len
 * @param1		:	handl, is returned by mcs_open
 * @param2		:	paramid, can be a SPN or PID requested by user
 * @param3		:	len, holds the data len for the requested paramid
 * @return		:	0 on success, and err number on failure
 * @brief		:	Gets the requested parameter data length
 */

int get_mcs_vehicle_param_data_len(void *handl,int paramid, int *len,MCS_CAPTURE_DATA_TYPE_T is_live_data);

/*
 * @function	:	get_mcs_vehicle_param_data
 * @param1		:	handl, is returned by mcs_open
 * @param2		:	data, holds the requested paramter data
 * @param3		:	len, length of the requested parameter,
 * 					returned by get_mcs_vehicle_param_data_len
 * @param4		:	paramid, parameter id which is going to be read.
 * @retrun		:	0 on success, and err number on failure
 * @brief		:	Gets the requested parameter data based on the length.
 */

int get_mcs_vehicle_param_data(void *handl, char *data, int paramid, int len,unsigned int * time_stamp, unsigned int *process_time );

int get_mcs_vehicle_vin(void *handl, unsigned char *data, int *len);
int get_mcs_vehicle_vin_data(void *handl, unsigned char *data, int *len);
int get_mcs_vehicle_vin_async(void *handl, unsigned char *data, int *len);
bool send_can_fw_ver_to_ndcentral();
bool send_can_details_to_ndcentral();	
bool send_can_src_to_ndcentral();
int start_param_broadcast(void *handl);

int mcs_por_cmd();

int get_veh_adc_data_history(unsigned char *data, int *len);

int j1939_param_configuration(void *handl, int cmd_len, unsigned short int *paramid);

int can_bus_bw_limit_configuration(void *handl, int cmd_len, unsigned short int paramid);

int set_obd_prop_configuration(void *handl, int cmd_len, unsigned int *paramid);

int set_obd_fast_configuration(void *handl, int cmd_len, unsigned short int paramid);

int set_obd_mid_configuration(void *handl, int cmd_len, unsigned short int *paramid);

int set_obd_slow_configuration(void *handl, int cmd_len, unsigned short int *paramid);

int read_broadcast_data(void *handl, vehicle_param_broadcast_data_t *params, int *index, unsigned int *sync_time);
int response_for_obd_lpm_keep_alive(void);
int get_veh_adc_data(char *data);

int enable_j1939_request_spn(void);
int enable_j1939_dm_pgn(void);

/*
 * @function	:	get_mcs_vehicle_all_param_data
 * @param1		:	handl, is returned by mcs_open
 * @param2		:	count, indicactes the number of parameters,
 * 					returned by get_mcs_vehicle_param_count
 * @param3		:	params, array of paramters of type vehicle_param_data_t
 * @return		:	0 on success, and err number on failure
 * @brief		:	Gets all the  vehicle parameter data from the vehicle.
 */

int get_mcs_vehicle_all_param_data(void *handl, int count , vehicle_param_data_t *params);

/*
 * @function	:	set_mcs_enable_vehicle_param_update
 * @param1		:	handl, is returned by mcs_open
 * @param2		:	enable, It can have values
 * 					enable = 1; Enable periodic update
 * 					enable = 0; Disable periodic update
 * @param3		:	time_sec, periodic update interval time in seconds
 * @return		:	0 on success, and err number on failure
 * @brief		:	Configures the periodic update of parameters
 * 					based on the enable value.
 */

int set_mcs_enable_vehicle_param_update(void *handl,unsigned char enable, int time_sec);

/*
 * @fucntion	:	set_mcs_host_command_timeout
 * @param1		:	handl, is returned by mcs_open
 * @param2		:	timeout_val, timeout value in milli seconds
 * @return		:	0 on success, and err number on failure
 * @brief		:	Set the timeout value for the command response
 * 					timeout used between the host and mcs controller.
 */

int set_mcs_host_command_timeout(void *handl, int timeout_val);

/*
 * @function	:	mcs_flash_firmware
 * @param1		:	handl, is returned by mcs_open
 * @param2		:	info, contain firmware image header info
 * @param3		:	data, fimrware data which has to flash
 * @param4		:	size, total length of the firmware in data.
 * @return		:	0 on success, and err number on failure
 * @brief		:	To update the micro-controller with the new firmware
 */

int mcs_flash_firmware(void *handl, firmware_image_header_t *info, unsigned char *data, int size);

/*
 * @function	:	get_mcs_config_gpio
 * @param1		:	handl, is returned by mcs_open
 * @param2		:	inter_gpio_num, interruptable gpio number
 * @param3		:	opendrain_gpio_num, opendrain gpio number
 * @return		:	0 on success,and err number on failure
 * @brief		:	Gets the current gpio configuration.
 *
 * NOTE			:	Bitmask set to 1 at position indicates that gpio
 * 					number is selected for its corresponding state.
 *
 * 	Interrupt	:	In current design GPIO's 0-7 are Input GPIO's
 * 					GPIO 3 can be selected as "set 4th bit (index 3) to 1 in gpio num"
 * 					inter_gpio_num = 0x00000008 --> select the gpio 3
 *
 * 	Open drain	:	In Current design GPIO's 8 to 15 are Output GPIO's
 * 					GPIO 9 can be selected as "set 10th bit (index 9) to 1 in gpio num"
 * 					opendrain_gpio_num = 0x00000200 --> select the gpio 9.
 */

int get_mcs_config_gpio(void *handl, int *inter_gpio_num , int *opendrain_gpio_num);

/*
 * @function	:	set_mcs_config_gpio
 * @param1		:	handl, is returned by mcs_open
 * @param2		:	inter_gpio, based on bit mask select an interruptable gpio
 * @param3		:	opendrain_gpio, based on bitmask select opendrain gpio
 * @param4		:	opendrain_value, based on bitmask uses an opendrain value
 * 					for the opendrain gpio.
 * @return		:	0 on success, and err number on failure
 * @brief		:	Sets the GPIO configuration as per the request.
 */

int set_mcs_config_gpio(void *handl, int inter_gpio, int opendrain_gpio, int opendrain_gpio_value);

/*
 * @function	:	set_mcs_gpio_value
 * @param1		:	handl, is returned by mcs_open
 * @param2		:	gpio_num, select gpio based on bitmask
 * @param3		:	val, value which has to be set for requested gpio
 * @return		:	0 on success,and err number on failure
 * @brief		:	Sets given value to the requested gpio.
 *
 * NOTE			:	gpio number should be open drain gpio.
 */

int set_mcs_gpio_value(void *handl, int gpio_num, int val);

/*
 * @function	:	get_mcs_gpio_value
 * @param1		:	handl, is returned by mcs_open
 * @param2		:	gpio_num, select gpio based on bitmask
 * @param3		:	val, holds the read value from the requested gpio
 * @return		:	0 on success, and err number on failure
 * @brief		:	Gets the gpio value from the requested gpio
 * 					value can be either 0 or 1.
 *
 * NOTE			:	gpio number should be Input gpio.
 */

int get_mcs_gpio_value(void *handl, int gpio_num, int *val);

/*
 * OBD cmd-ack timer
 */

typedef struct MCS_CMD_ACK_TIMER_HANDLE {
	timer_t timerid;
	int cmd_ack_timeout;
	int cmd_ack_timeout_flag;
	struct itimerval its;
} MCS_CMD_ACK_TIMER_HANDLE_T;

typedef struct MCS_OBD_CTX
{
	int state;
	int cur_transID;
	int trans_status;
	int param_count;
	veh_paramID_info_t *param_ids;
	vehicle_param_data_t *auto_update_data_ptr;
	int auto_update;
	int auto_update_time;
	int gpio_fd;
	MCS_CMD_ACK_TIMER_HANDLE_T timer;
	obd_spi_handle_t spiHandle;
	obd_spi_config_t spiConfig;
#ifdef OBD_LOCK
	sem_t obd_lock;
#endif
	sem_t trans_lock;
	sem_t data_lock;
	ack_pkt_t rx_pkt;
	err_status_cb err_stat_cb;
	all_param_data_cb veh_param_cb;
	diag_param_data_cb diag_param_cb;
	gpio_notify_callback_cb gpio_notify_cb;
	void *err_stat_cb_data;
	void *veh_param_cb_data;
	void *diag_param_cb_data;
	void *gpio_notify_cb_data;
	int user_id;
	int handle;
} MCS_OBD_CTX_T;

/*
 * OBD state
 */

typedef enum OBD_STATE {
	OBD_UNINITIALIZED=0x30,
	OBD_INITIALIZED,
	OBD_IDLE,
	OBD_CMD_SND,
	OBD_CMD_SND_FAIL,
	OBD_WAIT_ON_INTR,
	OBD_ACK_GET,
	OBD_ACK_GET_FAIL,
	OBD_TRANS_COMPLETE,
	OBD_CMD_ACK_TIMEOUT,

} OBD_STATE_T;

extern MCS_OBD_CTX_T obdCtx;
extern char rx_buff[MCS_PARAM_MAX_DATA_LEN];

/* Function prototypes */
int create_handle(void);
int is_valid_handle (void *handl);
void interrupt_read_cb(void);
static void timer_cb (int sig);
void *interrupt_thread_cb(void *arg);
void mcs_reset (void);
bool get_eld_publish_freq(unsigned int *eld_freq);
void set_eld_publish_freq(unsigned int eld_freq);
#ifdef __cplusplus
}
#endif

#endif
