/* Copyright (C) 2018 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Arun V <arun.vj@netradyne.com>, Apr 2018
 */

#include "log.h"
#include "nd_time.h"
#include "nd_msgq.h"
#include "nd_msg_types.h"
#include "nd_msg_utils.h"
#include "config_parser.h"

#include <system_utils.h>

#include "sm_internal.h"
#include "nd_factory.h"

using namespace std;

//typedef unsigned long long uint64ll_t;

#define ROUTE_LOGS


NDService *nd_service_obj = NULL; //nd service object, to detect crashes

ND_DeviceFactory *nd_device_obj = NULL; //nd device factory object, to use factory class

static const string log_dir = "/home/ubuntu/.nddevice/log/service_mon";
static const string config_file = "/home/ubuntu/.nddevice/latest/bagheera_config.ini";
static nd_msgq_t *server_q=NULL;
static bool sm_enabled=true;

static const string SECTION="process_mon";
static const string REPORT_KEY="error_report";

static const int RESTART_DELAY = 60;

//Function to retrieve queue name
static string get_msgq_name() {
    return Q_NAME;
}

//Function to retrieve configuration from config ini file
bool get_config() {

    //Parse config file
    Config_parser config(config_file);

    //Check for config file validity
    if( false == config.getParseStatus() ) {
        LOG_E(TAG, "Cannot parse config: %s", config_file.c_str());
        LOG_I(TAG, "sm_enabled: %d", sm_enabled);
        return false;
    }

    //Check if reporting should be enabled
    if (config.isPresent (SECTION, REPORT_KEY)) {
        if (config.getConfig(SECTION, REPORT_KEY,"") == "false") {
            sm_enabled = false;
        }
    } else {
        LOG_I(TAG, "Config does not have section or key");
    }

    LOG_I(TAG, "sm_enabled: %d", sm_enabled);

    return true;
}

//Function initialize message queue
static bool init_msgq() {

    //Create message queue
    server_q = nd_msgq_t::get_msgq( get_msgq_name(), nd_msgq_t::ND_MSGQ_SERVER );

    if( server_q == NULL ) {
        LOG_E(TAG, "Cannot create message queue");
        return false;
    }

    LOG_I(TAG, "Message queue created");

    return true;
}

//Handle error message from services
static void handle_sm_err( service_mon_err_msg_t *msg ) {
    if( msg == NULL ) {
        return;
    }

    uint64_t time = msg->time1;
    time = (((uint64_t)time << 32) & 0xFFFFFFFF00000000);
    time |= ((uint64_t)msg->time2 & 0x00000000FFFFFFFF);

    uint64_t uptime = msg->systemUpTimeMono1;
    uptime = (((uint64_t)uptime << 32) & 0xFFFFFFFF00000000);
    uptime |= ((uint64_t)msg->systemUpTimeMono2 & 0x00000000FFFFFFFF);

    LOG_E(TAG, "Service error: %s : %llu : %d : %d : %s : %llu", msg->sname, time, \
            msg->err_code, msg->err_code_aux, msg->err_msg, uptime);

    //Add the error log to json
    add_err_log(time, msg->sname, msg->err_code, msg->err_code_aux, msg->err_msg, uptime);
}

//Handle start message from services
static void handle_sm_start( service_mon_start_stop_msg_t *msg ) {
    if( msg == NULL ) {
        return;
    }

    uint64_t time = msg->time;

     LOG_I(TAG, "Service started: %s : %llu", msg->sname, time);
}
//Handle stop message from services
static void handle_sm_stop( service_mon_start_stop_msg_t *msg ) {
    if( msg == NULL ) {
        return;
    }

    uint64_t time = msg->time;
    
    LOG_I(TAG, "Service stopped: %s : %llu", msg->sname, time);
}

//Message loop to receive and process messages
static void msg_loop() {
    nd_msgq_t::nd_msg_t *msg; 
    bool res;

    while(1) {
        //Receive message
        msg = server_q->receive();
        if( msg == NULL ) {
            LOG_E(TAG, "Receive message failed");
            continue;
        }

        //Cast to generic message to get message type
        msg_type_t type = get_msg_type(msg->get_buffer());
        generic_msg_t *m = (generic_msg_t *)msg->get_buffer();
        if( m == NULL ) {
            LOG_E(TAG, "Received NULL message");
            continue;
        }

        LOG_I(TAG, "%d received", m->msg_type);

        //Switch based on message type        
        switch( type ) {

            case REQ_SM_START: //Service start message received
                handle_sm_start( (service_mon_start_stop_msg_t *)m );
                break;

            case REQ_SM_STOP: //Service stop message received
                handle_sm_stop( (service_mon_start_stop_msg_t *)m );
                break;

            case REQ_SM_ERR: //Service error message received
                handle_sm_err( (service_mon_err_msg_t *)m );
                break;

            default: //Unknown message
                LOG_E(TAG, "Unknown message: %d", type);
        }

        //Free received message
        delete msg;
    }
}

int main() {

    //Initialize logging
    printf("initilizing logger\n");
    bool status_log = nd_log_init( log_dir.c_str() );
    if(status_log == false) {
        printf("unable to init logger");
    }

    //Route logs to file
#ifdef ROUTE_LOGS
    route_logs( log_dir.c_str() );
#endif


//    nd_device_obj_init();
    //Read configuration
    if( get_config() == false ) {
        LOG_E(TAG, "Error reading config, loaded defaults");
    } else {
        LOG_I(TAG, "Config load success");
    }

    if( sm_enabled == false ) {
        LOG_I(TAG, "Service monitor disabled by config");
        while(1) { sleep(100); }
    }

    //Initialize message queue
    if( init_msgq() == false ) {
        //Critical error, return and allow systemd to restart
        LOG_E(TAG, "Init msgq failed");
        sleep(RESTART_DELAY);
        LOG_E(TAG, "Exiting");
        return false;
    }

    //Get into a message loop, never exit
    msg_loop();

    return 0;
}

