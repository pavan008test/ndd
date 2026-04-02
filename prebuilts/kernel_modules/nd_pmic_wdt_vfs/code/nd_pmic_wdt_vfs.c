/*
 * 
 *
 * PMIC WDT module for Bagheera 
 *
 * Copyright (c) 2020-2021, Netradyne. All rights reserved.
 *
 * 
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/timer.h>
#include <linux/uaccess.h>
#include <linux/i2c.h>

#define I2C_4_ADAPTER_FILE                  "/dev/i2c-4"// i2c adapter for communication.

#define PMIC_I2C_SLAVE_ADDR                 0x3C        // i2c slave address for max77620 PMIC ic 

#define PMIC_ONOFFCNFG2_ADDR                0x42        // ON/Off configuration register address
#define PMIC_ONOFFCNFG2_BIT_WD_RST_WK          6        // WD_RST_WK bit position, reset on Watchdog timeout

#define PMIC_CNFGGLBL2_ADDR                 0x01        // Global configuration Register address
#define PMIC_CNFGGLBL2_BIT_WDTEN               2        // WDTEN Bit Position, watchdog enable 
#define PMIC_CNFGGLBL2_TWD_VALUE            0x03        // System Watchdog Timer Period
                                                        // 0b00=2s
                                                        // 0b01=16s
                                                        // 0b10=64s
                                                        // 0b11=128s
#define PMIC_CNFGGLBL3_ADDR                 0x02        // Global configuration Register address

#define PMIC_CNFGGLBL3_WDTC_VALUE           0x01        // System Watchdog Timer Clear
                                                        // 0b00=the system watchdog timer is not cleared
                                                        // 0b01=the system watchdog timer is cleared
                                                        // 0b10=the system watchdog timer is not cleared
                                                        // 0b11=the system watchdog timer is not cleared

struct file* nd_driver_file_open(const char *fpath )
{
        struct file *nd_filp = NULL;
        mm_segment_t    nd_oldfs;
        nd_oldfs   = get_fs();
        set_fs(get_ds());
        nd_filp = filp_open(fpath, O_RDWR, S_IRWXU|S_IRWXG|S_IRWXO);
        set_fs(nd_oldfs);
        return (nd_filp);
}
void nd_driver_file_close(struct file *nd_filp)
{
        filp_close(nd_filp, NULL);
}

static int max77620_pmic_wdt_op(const char* adapter_file, unsigned short slave_addr, unsigned short set) {

    struct file * fp;
    struct i2c_client *pmic_client;
    static u8 set_ONOFFCNFG2_reg_value = 0, ONOFFCNFG2_reg_value = 0x0;
    static u8 set_CNFGGLBL2_reg_value = 0, CNFGGLBL2_reg_value = 0x0;

    fp = nd_driver_file_open(adapter_file);
    if(fp == NULL) {
        pr_err("file open error %s", adapter_file);
        return -1;
    }
    pmic_client = (struct i2c_client *)fp->private_data;
    if(pmic_client == NULL) {
        pr_err("i2c client init error");
        return -1;
    }

    pmic_client->addr = slave_addr;
    
    set_ONOFFCNFG2_reg_value = 0xFF & i2c_smbus_read_byte_data(pmic_client,PMIC_ONOFFCNFG2_ADDR);
    set_CNFGGLBL2_reg_value = 0xFF & i2c_smbus_read_byte_data(pmic_client,PMIC_CNFGGLBL2_ADDR);

    if (set){
            // Expected value is 0x5b for ONOFFCNFG2 to make sure WDT expire reboots.
            set_ONOFFCNFG2_reg_value |= (1 << PMIC_ONOFFCNFG2_BIT_WD_RST_WK);

            //Expected value  is 0x07 for enabling timer and setting 128s as time.
            set_CNFGGLBL2_reg_value  |= (1 << PMIC_CNFGGLBL2_BIT_WDTEN);
            set_CNFGGLBL2_reg_value  |= (PMIC_CNFGGLBL2_TWD_VALUE);
    }
    else {
            // Expected value is 0x1b to disable reboot for WDT expire.
            set_ONOFFCNFG2_reg_value &=  ~(1 << PMIC_ONOFFCNFG2_BIT_WD_RST_WK);

            // Expected value is 0x03 for disabling PMIC WDT.
            set_CNFGGLBL2_reg_value  &=  ~(1 << PMIC_CNFGGLBL2_BIT_WDTEN);
    }
    
    // Set Watchdog timer expire to reboot
    i2c_smbus_write_byte_data(pmic_client,PMIC_ONOFFCNFG2_ADDR,set_ONOFFCNFG2_reg_value);
    // Clear watchdog timer
    i2c_smbus_write_byte_data(pmic_client,PMIC_CNFGGLBL3_ADDR, PMIC_CNFGGLBL3_WDTC_VALUE);
    // Setup or clear the watchdog timer
    i2c_smbus_write_byte_data(pmic_client,PMIC_CNFGGLBL2_ADDR, set_CNFGGLBL2_reg_value);

    ONOFFCNFG2_reg_value =  i2c_smbus_read_byte_data(pmic_client,PMIC_ONOFFCNFG2_ADDR);
    CNFGGLBL2_reg_value  =  i2c_smbus_read_byte_data(pmic_client,PMIC_CNFGGLBL2_ADDR);

    nd_driver_file_close(fp);
    
    pr_info("nd_pmic_wdt_vfs PMIC WDT register values ONOFFCNFG2 : %x CNFGGLBL2 : %x \n",
                 ONOFFCNFG2_reg_value, CNFGGLBL2_reg_value);
    
    // Check if values being calculated was set
    if (!((ONOFFCNFG2_reg_value == set_ONOFFCNFG2_reg_value) && 
         (CNFGGLBL2_reg_value == set_CNFGGLBL2_reg_value))){
        return -1;
    }

    return 0;

}

static int nd_pmic_wdt_vfs_enable(void){
    int ret=-1;

    pr_info("nd_pmic_wdt_vfs initializing");
    ret = max77620_pmic_wdt_op(I2C_4_ADAPTER_FILE, PMIC_I2C_SLAVE_ADDR,1);
    if ( 0 != ret ) {
        pr_err("nd_pmic_wdt_vfs failed while setting PMIC wdt. ret %x", ret);
    }

    return ret;
}


static void nd_pmic_wdt_vfs_disable(void){
    int ret = -1;
    pr_info("nd_pmic_wdt_vfs uninitializing");
    ret = max77620_pmic_wdt_op(I2C_4_ADAPTER_FILE, PMIC_I2C_SLAVE_ADDR,0);
    if ( 0 != ret ) {
        pr_err("nd_pmic_wdt_vfs failed while clearing PMIC wdt. ret %x",ret);
    }
    
    return;
}

static int __init nd_pmic_wdt_init(void)
{

    return nd_pmic_wdt_vfs_enable();
}

static void __exit nd_pmic_wdt_exit(void)
{
    nd_pmic_wdt_vfs_disable();
    return;
}

module_init(nd_pmic_wdt_init);
module_exit(nd_pmic_wdt_exit);

MODULE_AUTHOR("Netradyne");
MODULE_DESCRIPTION("ND PMIC Watchdog Driver");


MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:nd_pmic_wdt");
