#ifndef _GPIO_API_H__
#define _GPIO_API_H__

#define TOTAL_NUM_GPIO_CFG 8
#define GPIO_DEBUG 0

typedef enum gpio_event{
	GPIO_EVENT_NONE,
	GPIO_EVENT_RISING,
	GPIO_EVENT_FALLING,
	GPIO_EVENT_BOTH
}GPIO_EVENT_T;

/*GPIO Ioctl function*/
int gpio_direction_input(int *gpio, unsigned char size);
int gpio_direction_output(int *gpio, char *value, unsigned char size);
int gpio_get_value(int *gpio, char *value, unsigned char size);
int gpio_set_value(int *gpio, char *value, unsigned char size);
/*sysfs function*/
int register_callback(int gpio_number, GPIO_EVENT_T event);


/*sysfs internal API's*/
static int GPIOExport(int pin);
int GPIOUnexport(int pin);
static int GPIODirection(int pin, int dir);
static int GPIORead(int pin);
static int GPIOWrite(int pin, int value);
static int GPIOInterrupt(int pin, GPIO_EVENT_T event);
int sysfs_init_interrupt_enable(int gpio_number, GPIO_EVENT_T event);

#endif /*_GPIO_API_H_*/
