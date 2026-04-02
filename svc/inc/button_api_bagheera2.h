/******************************************************************************************************
 * @file       button_api.h
 * 
 * @brief      This file is  the include file for  button api
 *
 * @author     Sherin Jasper.s (sherinjasper.s@vvdntech.in)
 *      
 ***********************************************************************************************************/
#ifndef __BUTTON_API_H__
#define __BUTTON_API_H__


#define BUTTON_SUCCESS 0
#define BUTTON_FAILURE 1
#define BUTTON_DEBUG 0
#define BUTTON_1    1
#define BUTTON_2    2


 
/********************************************************************************
 * FUNCTION NAME:       unregisterCB_button                                               
 * DESCRIPTION:         To unregister the callback for the button                   
 * ARGS:                button number 1 or 2            	            
 * OUTPUT:              Callback if registered for the button number will be cancelled                                          
 * RETURN VALUE:        SUCESS/FALIURE                                            
 * ******************************************************************************/

int unregisterCB_button(int button);

/********************************************************************************
 * FUNCTION NAME:       registerCB_button 
 * DESCRIPTION:         Registers the callback with the callback function passed and 
 *                      creates a thread and when the interrupt triggers callback function is called
 * ARGS:                int button_no 1 or 2 
 *                      int event 0 - 3
 *                      button_cb ->callback fuction to be handled which accepts one arguments

 * OUTPUT:              Callback function will be called once the interrupt triggers.
 * RETURN VALUE:        SUCEESS/FAILURE
 * ******************************************************************************/

int registerCB_button(int button_no,int event,int (*button_cb)(void));

/* Helper Function*/

/********************************************************************************
 * function             gpio89_interrupt_thread_fn                             *
 * description:         helper function used  registercb_button 
 * ******************************************************************************/
void *gpio89_interrupt_thread_fn(void *arg);

/********************************************************************************
 * FUNCTION             gpio202_interrupt_thread_fn                             *
 * DESCRIPTION:         Helper function used  registercb_button 
 * ******************************************************************************/

/*
void *gpio202_interrupt_thread_fn(void *arg);

bool send_qcs_alive();
bool msp_qcs_alive();
*/


#endif
