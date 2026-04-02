#ifndef __GPIO_ICDC_H__
#define __GPIO_ICDC_H__

#define TX1_CAM1_PWDN           151     //PS7 -->high
#define TX1_CAM2_PWDN           152     //PT0   -->high
#define TX1_CAM_RST             148     //PS4   -->high
#define TX1_CAM_AF_EN           149     //PS5   -->high
#define TX1_CAM1_STROBE         153     //PT1   ->high
#define TX1_CAM_FLASH_EN        150     //PS6   -->low,in
#define TX1_GPIO_X1_AUD         219     //PBB3  -->high,In
#define TX1_GPIO_PE6            38      //PE6   --> --,In
#define TX1_GPIO_PL1            89      //PL1 -->INT.HIGH
#define TX1_TOUCH_INT           185     //PX1   -->INT ,HIGH
#define TX1_GPS_EN              66      //PI2 --> out,low
#define TX1_ALS_PROX_INT        187     //PX3 --> INT,High
#define TX1_MOTION_INT          186     //PX2 --> INT,High
#define TX1_TOUCH_RST           174     //PV6 --> out,low
#define TX1_AP_WAKE_NFC         63      //PH7 -->INt/output,low
#define TX1_NFC_INT             65      //PI1   --> int,high
#define TX1_GPIO_PH6            62      //PH6 -->int,high
#define TX1_GPIO_PZ2            202     //PZ2 -->int_high
#define TX1_GPIO_PK5            85      //PK5 -->out,high
#define TX1_AP_READY            173     //PV5 --> in
#define TX1_MODEM_WAKE_AP       184     //PX0 -->in
#define TX1_GPIO_PK4            84      //PK4 -->in
#define TX1_GPIO_PK6            86      //PK6 -->in
//#define TX1_LCD_BL_PWM        168     //PV0 --> PWM


#define TX1_GPIO_END        255
#define TCA_EXPANDER_A_GPIO_START         (1016)

#define TCA9538PW_EXPANDER1_GPIO_A0    	(TCA_EXPANDER_A_GPIO_START)
#define TCA9538PW_EXPANDER1_GPIO_A1	(TCA_EXPANDER_A_GPIO_START + 1)
#define TCA9538PW_EXPANDER1_GPIO_A2     (TCA_EXPANDER_A_GPIO_START + 2)
#define TCA9538PW_EXPANDER1_GPIO_A3     (TCA_EXPANDER_A_GPIO_START + 3)
#define TCA9538PW_EXPANDER1_GPIO_A4     (TCA_EXPANDER_A_GPIO_START + 4)
#define TCA9538PW_EXPANDER1_GPIO_A5     (TCA_EXPANDER_A_GPIO_START + 5)
#define TCA9538PW_EXPANDER1_GPIO_A6     (TCA_EXPANDER_A_GPIO_START + 6)
#define TCA9538PW_EXPANDER1_GPIO_A7     (TCA_EXPANDER_A_GPIO_START + 7)

#define TCA_EXPANDER_B_GPIO_START         (1000)

#define TCA9538PW_EXPANDER2_GPIO_A0     (TCA_EXPANDER_B_GPIO_START)
#define TCA9538PW_EXPANDER2_GPIO_A1     (TCA_EXPANDER_B_GPIO_START + 1)
#define TCA9538PW_EXPANDER2_GPIO_A2   	(TCA_EXPANDER_B_GPIO_START + 2)
#define TCA9538PW_EXPANDER2_GPIO_A3     (TCA_EXPANDER_B_GPIO_START + 3)
#define TCA9538PW_EXPANDER2_GPIO_A4     (TCA_EXPANDER_B_GPIO_START + 4)
#define TCA9538PW_EXPANDER2_GPIO_A5     (TCA_EXPANDER_B_GPIO_START + 5)
#define TCA9538PW_EXPANDER2_GPIO_A6     (TCA_EXPANDER_B_GPIO_START + 6)
#define TCA9538PW_EXPANDER2_GPIO_A7     (TCA_EXPANDER_B_GPIO_START + 7)

/* Tegra Tx1 GPIOs for ICDC board*/
#define JTX1_CAM0_PWR           TX1_CAM1_PWDN
#define JTX1_CAM1_PWR           TX1_CAM2_PWDN
#define JTX1_CAM0_RST           TX1_CAM_RST
#define JTX1_INWARD_CAM_PWR     TX1_CAM_AF_EN
//#define RESET_OV2775          TX1_CAM1_STROBE
#define JTX1_GPIO_RESET         TX1_CAM_FLASH_EN
#define MC_JTX1_STATUS          TX1_TOUCH_INT
#define SPI_LT_EN               TX1_TOUCH_RST
#define JTX1_MOTION_INT         TX1_MOTION_INT
#define ALS_INT                 TX1_ALS_PROX_INT        
#define RESET_AUDIO             TX1_GPIO_X1_AUD         
#define AUD_INT                 TX1_GPIO_PE6
#define JTX1_WP7504_GPS_DSYNC   TX1_NFC_INT
#define JTX1_MOD_POWER_OFF      TX1_GPS_EN
#define JTX1_AUTO_PWR_ON        TX1_GPIO_PH6
#define JTX1_WP7504_WK_INT      TX1_GPIO_PK5
#define WP7504_WK_ON_WAN        TX1_AP_READY
#define WP7504_S_PWR_RMVE       TX1_MODEM_WAKE_AP
#define JTX1_CAR_PWR_ON         TX1_GPIO_PK4
#define WP7504_RI               TX1_GPIO_PK6
#define GPIO_BUTTON1            TX1_GPIO_PL1
#define GPIO_BUTTON2            TX1_GPIO_PZ2
//#define JTX1_FSIN             TX1_LCD_BL_PWM
#define JTX1_WP7504_ULPM_WK     TX1_AP_WAKE_NFC

/* GPIO expander */
/* GPIO expander pins of TCA9538PW_EXP1 */
#define WP7504_TP1             TCA9538PW_EXPANDER1_GPIO_A0
#define WP7504_W_DIS           TCA9538PW_EXPANDER1_GPIO_A1
#define JTX1_WP7504_PWR_ON     TCA9538PW_EXPANDER1_GPIO_A2
#define RESET_WP7504           TCA9538PW_EXPANDER1_GPIO_A3
#define BOARD_REV1             TCA9538PW_EXPANDER1_GPIO_A4
#define BOARD_REV2             TCA9538PW_EXPANDER1_GPIO_A5
#define IOE_JTX1_FAN_DIS       TCA9538PW_EXPANDER1_GPIO_A6
#define MC_RESET               TCA9538PW_EXPANDER1_GPIO_A7

/* GPIO expander pins of TCA9538PW_EXP2 */
#define SYS_LED1_R             TCA9538PW_EXPANDER2_GPIO_A0
#define SYS_LED1_G             TCA9538PW_EXPANDER2_GPIO_A1
#define SYS_LED2_R             TCA9538PW_EXPANDER2_GPIO_A2
#define SYS_LED2_G             TCA9538PW_EXPANDER2_GPIO_A3
#define ENABLE_IR1             TCA9538PW_EXPANDER2_GPIO_A4
#define ENABLE_IR2             TCA9538PW_EXPANDER2_GPIO_A5
#define ENABLE_IR3             TCA9538PW_EXPANDER2_GPIO_A6
#define NOT_USED_PIN7          TCA9538PW_EXPANDER2_GPIO_A7

#define SIZE_8  8
#define SIZE_1  1
#define HI      1
#define LOW     0

/* IOCTLs */
#define GPIO_IOCTL_BASE         'G'
#define GPIO_IOC_DIR_IN         _IOW(GPIO_IOCTL_BASE, 1, int)
#define GPIO_IOC_DIR_OUT        _IOW(GPIO_IOCTL_BASE, 2, int)
#define GPIO_IOC_SET_VAL        _IOW(GPIO_IOCTL_BASE, 3, int)
#define GPIO_IOC_GET_VAL        _IOR(GPIO_IOCTL_BASE, 4, int)
#define GPIO_IOC_EN_INTR        _IOR(GPIO_IOCTL_BASE, 5, int)
#define GPIO_IOC_DIS_INTR       _IOR(GPIO_IOCTL_BASE, 6, int)

#define GPIO_DEV_NAME           "gpio_icdc"
typedef struct gpio_ioc {
        int gpio[8];
        int value[8];
        int flag;
}GPIO_IOC_T;

extern int gpio_dir_input(GPIO_IOC_T *gpio);
extern int gpio_dir_output(GPIO_IOC_T*gpio);
extern GPIO_IOC_T* gpio_get_val(GPIO_IOC_T *gpio);
extern void gpio_set_val(GPIO_IOC_T *gpio);
extern int gpio_to_irq(int gpio);
extern int gpio_enable_irq(GPIO_IOC_T *gpio);
extern int gpio_disable_irq(GPIO_IOC_T *gpio);
extern int irq_to_gpio(int irq);
extern int gpio_interrupt_happened(int gpio);

#endif /* __GPIO_ICDC_H__ */

