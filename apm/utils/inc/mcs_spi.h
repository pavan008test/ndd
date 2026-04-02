/*****************************************************************************************
 *
 *	@file		:	mcs_spi.h
 *	@brief		:	It contain Macro's and function prototypes and structure
 *					definitions used in the mcs_spi.c file.
 *  @author		:	B. Venkata Durga Prasad, VVDN Technologies Pvt. Ltd.
 *  Copyright	:	(c) 2016-2017 , VVDN Technologies Pvt. Ltd.
 *  				Permission is hereby granted to everyone in VVDN Technologies
 *  				to use the Software without restriction,including without
 *  				limitation the rights to use, copy, modify, merge, publish,
 *  				distribute, distribute with modifications.
 *
 ******************************************************************************************/

#ifndef MCS_SPI_H
#define MCS_SPI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "mcs_dbg.h"

/*
 * OBD SPI modes
 */

typedef enum OBD_SPI_MODE {
	OBD_SPI_MODE_0,
	OBD_SPI_MODE_1,
	OBD_SPI_MODE_2,
	OBD_SPI_MODE_3,

} OBD_SPI_MODE_T;

/*
 * OBS SPI word length
 */

typedef enum OBD_SPI_WORD {
	OBD_SPI_WORD_8 = 8,
	OBD_SPI_WORD_16 = 16,

} OBD_SPI_WORD_T;

/*
 * OBD SPI Byte order
 */

typedef enum OBD_SPI_BYTE_ORDER {
	OBD_SPI_MSB_FIRST,
	OBD_SPI_LSB_FIRST,

} OBD_SPI_BYTE_ORDER_T;

/*
 * OBD SPI error status
 */

typedef enum OBD_SPI_ERROR_STATUS {
	SPI_SUCCESS,
	ESPI_DEVICE = 0x50,
	ESPI_PORT,
	ESPI_HANDLE,
	ESPI_WRITE,
	ESPI_READ,
	ESPI_NOMEM,

} OBD_SPI_ERROR_STATUS_T;

/*****************************************************************************************
 *
 * @structure	:	obd_spi_config
 * @mem1		:	mode, spi mode --> can be mode 0,1,2 and 3
 * @mem2		:	word_len, bits per word
 * @mem3		:	clock, spi clock
 * @mem4		:	byte_order, either msb first or lsb first
 *
 *****************************************************************************************/

typedef struct obd_spi_config {
	uint8_t mode;
	uint8_t word_len;
	uint32_t clock;
	uint8_t byte_order;
} obd_spi_config_t;

/*****************************************************************************************
 *
 * @structure	:	obd_spi_handle
 * @mem1		:	port, spi port
 * @mem2		:	device, device name
 *
 *****************************************************************************************/

typedef struct obd_spi_handle {
	int port;
	char device[20];
} obd_spi_handle_t;

/* function prototypes */

int obd_spiopen (obd_spi_handle_t *handle);
void obd_spiclose (obd_spi_handle_t *handle);
int obd_spiconfig (obd_spi_handle_t handle, obd_spi_config_t config);
int obd_spiwrite (obd_spi_handle_t handle, unsigned char *buffer, unsigned int len);
int obd_spiread (obd_spi_handle_t handle, unsigned char *buffer, unsigned int len);

#ifdef __cplusplus
}
#endif

#endif
