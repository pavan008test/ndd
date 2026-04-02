#include <stdio.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <errno.h>

#include "log.h"

#define SUCCESS 0
#define ERROR -1
#define CAM0_ADDR 0x24
#define CAM1_ADDR 0x3C
#define CAM2_ADDR 0x36
#define CAM3_ADDR 0x36
#define CAM0_FD_NAME "/dev/i2c-2"
#define CAM1_FD_NAME "/dev/i2c-2"
#define CAM2_FD_NAME "/dev/i2c-2"
#define CAM3_FD_NAME "/dev/i2c-7"
#define READ 0
#define WRITE 1
#define FORCE 1
//#define DEBUG           1

#define BIT_8           8
#define BIT_16          16

#define TAG "CAM_I2C"

static int set_slave_addr(int file,int address,int force) 
{
       /* With force, let the user read from/write to the registers
          even when a driver is also running */
       if (ioctl(file, force ? I2C_SLAVE_FORCE : I2C_SLAVE, address) < 0) {
              LOG_E(TAG,
                     "Error: Could not set address to 0x%02x: %s\n",
                     address, strerror(errno));
              return -errno;
       }
       return SUCCESS;
}

static int i2c_write(int camera, int file, unsigned char *wbuf,int size) 
{
    struct i2c_rdwr_ioctl_data packets;
    struct i2c_msg messages[1];

    if(camera == 0) {
	    messages[0].addr  = CAM0_ADDR;
    } else if(camera == 1) {
	    messages[0].addr  = CAM1_ADDR;
    } else if(camera == 2) {
	    messages[0].addr  = CAM2_ADDR;
    } else if(camera == 3) {
	    messages[0].addr  = CAM3_ADDR;
    } else {
	    return -1;
    }

    messages[0].flags = 0;
    messages[0].len   = size;
    messages[0].buf   = wbuf;

    /* Transfer the i2c packets to the kernel and verify it worked */
    packets.msgs  = messages;
    packets.nmsgs = 1;
    if(ioctl(file, I2C_RDWR, &packets) < 0) {
        LOG_E(TAG, "Unable to send data\n");
        return ERROR;
    }

    return SUCCESS;
}

static int i2c_rw(int camera, int file,unsigned char *wbuf,unsigned char *rbuf,int wsize,int rsize) 
{
    struct i2c_rdwr_ioctl_data packets;
    struct i2c_msg messages[2];

    if(camera == 0) {
	    messages[0].addr  = CAM0_ADDR;
	    messages[1].addr  = CAM0_ADDR;
    }
    else if(camera == 1) {
	    messages[0].addr  = CAM1_ADDR;
	    messages[1].addr  = CAM1_ADDR;

    } else if(camera == 2) {
	    messages[0].addr  = CAM2_ADDR;
	    messages[1].addr  = CAM2_ADDR;

    } else if(camera == 3) {
	    messages[0].addr  = CAM3_ADDR;
	    messages[1].addr  = CAM3_ADDR;

    } else {
	    return -1;
    }

    messages[0].flags = 0;
    messages[0].len   = wsize;
    messages[0].buf   = wbuf;

    /* The data will get returned in this structure */
    messages[1].flags = I2C_M_RD;
    messages[1].len   = rsize;
    messages[1].buf   = rbuf;

    /* Send the request to the kernel and get the result back */
    packets.msgs      = messages;
    packets.nmsgs     = 2;
    if(ioctl(file, I2C_RDWR, &packets) < 0) {
        LOG_E(TAG, "Unable to send data\n");
        return ERROR;
    }

    return SUCCESS;
}

static void i2c_fill_buf(unsigned char *buf, int addr, int value, int reg_addr_bit, int *num_bytes)
{
    if (reg_addr_bit == BIT_8) {
        buf[0] = (addr & 0xff);
        buf[1] = value;
        *num_bytes = 2;
    }
    else if (reg_addr_bit == BIT_16) {
        buf[0] = (addr >> 8) & 0xff;
        buf[1] = (addr & 0xff);
        buf[2] = value;
        *num_bytes = 3;
    }
}

static int i2c_transfer(int camera, unsigned char op, int reg, int value, unsigned char *out_buf)
{

	int res, file;
	unsigned char buf[3];
	int reg_addr_bit = BIT_16;
	int num_bytes_written;

	if(camera == 0) {
		file = open(CAM0_FD_NAME, O_RDWR);
		if(file == -1 || set_slave_addr(file, CAM0_ADDR, FORCE)) {
			LOG_E(TAG, "Unable to open i2c control file\n");
			return ERROR;
		}
	} else if(camera == 1) {
		file = open(CAM1_FD_NAME, O_RDWR);
		if(file == -1 || set_slave_addr(file, CAM1_ADDR, FORCE)) {
			LOG_E(TAG, "Unable to open i2c control file\n");
			return ERROR;
		}
		reg_addr_bit = BIT_8;
	} else if(camera == 2) {
		file = open(CAM2_FD_NAME, O_RDWR);
		if(file == -1 || set_slave_addr(file, CAM2_ADDR, FORCE)) {
			LOG_E(TAG, "Unable to open i2c control file\n");
			return ERROR;
		}
	} else if(camera == 3) {
		file = open(CAM3_FD_NAME, O_RDWR);
		if(file == -1 || set_slave_addr(file, CAM3_ADDR, FORCE)) {
			LOG_E(TAG, "Unable to open i2c control file\n");
			return ERROR;
		}
	} else {
		LOG_E(TAG,"Invalid camera\n");
		return ERROR;
	}

	i2c_fill_buf(buf, reg, value, reg_addr_bit, &num_bytes_written);

	if(op == WRITE) {
		res = i2c_write(camera, file, buf, num_bytes_written);
		if (res != SUCCESS) {
			LOG_E(TAG, "i2c_write FAILED \n");
		}
#if DEBUG
		if (res == SUCCESS) {
			if (reg_addr_bit == BIT_16) {
				LOG_I(TAG, "WRITE ====> addr: 0x%02x%02x \t value: 0x%02x \n", buf[0], buf[1], buf[2]);
			} else if (reg_addr_bit == BIT_8) {
				LOG_I(TAG, "WRITE ====> addr: 0x%02x \t value: 0x%02x \n", buf[0], buf[1]);
			}
		}
#endif
	} else {
		res = i2c_rw(camera, file, buf, out_buf, (num_bytes_written - 1), 1);
		if (res != SUCCESS) {
			LOG_E(TAG, "i2c_rw FAILED \n");
		}

#if DEBUG
		if (res == SUCCESS) {
			if (reg_addr_bit == BIT_16) {
				LOG_I(TAG, "READ ====> addr: 0x%02x%02x \t value: 0x%02x \n", buf[0], buf[1], out_buf[0]);
			} else if (reg_addr_bit == BIT_8) {
				LOG_I(TAG, "READ ====> addr: 0x%02x \t value: 0x%02x \n", buf[0], out_buf[0]);
			}
		}
#endif
	}

	close(file);
	return res;
}

int cam_reg_write(int cam_num, int reg, unsigned char value)
{
	int res = i2c_transfer(cam_num, WRITE,reg,value,NULL);
	return res;
}

int cam_reg_read(int cam_num, int reg, unsigned char *value)
{
	int res = i2c_transfer(cam_num, READ,reg,0x00,value);
	return res;
}
