#include "log.h"
#include "nd_time.h"
#include "nd_msgq.h"
#include "nd_msg_types.h"
#include "nd_msg_utils.h"
#include <nd_task.h>
#include <svc.h>
#include <svc_internal.h>
#include <config_parser.h>
#include <service_utils.h>
#include "nd_msp_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <button_api_bagheera2.h>
#include "nd_gpio.h"

#ifdef __cplusplus
}
#endif

//bool button_init() { ;  }



bool proces_button_event(int button) ;

static int button_cb0() {
    proces_button_event(0);
    return BUTTON_SUCCESS;
}
static int button_cb1() {
    proces_button_event(1);
    return BUTTON_SUCCESS;
}
bool button_init() {
    // 0 - 3 0-->None 1-->rising 2 --> falling 3 ->both rising and falling
    int events = 3;

    if( BUTTON_SUCCESS != registerCB_button(1, events, button_cb0) ) {
        LOG_E(TAG, "Button 1 callback registration failed");
        return false;
    }
    if( BUTTON_SUCCESS != registerCB_button(2, events, button_cb1) ) {
        LOG_E(TAG, "Button 2 callback registration failed");
        return false;
    }

    return true;
}



