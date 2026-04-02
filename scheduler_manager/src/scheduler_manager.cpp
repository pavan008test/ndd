/* Copyright (C) 2021 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Karan Kishore <karan.kishore@netradyne.com>, August 2021
 */
#include "log.h"
#include "nd_msgq.h"
#include "nd_msg_types.h"
#include "nd_msg_utils.h"
#include "config_parser.h"

#include <string>
#include <sstream>
#include <list>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "service_utils.h"
#include "system_utils.h"
#include "nd_file_utils.h"

#define ROUTE_LOGS
static const string log_dir = "/home/ubuntu/.nddevice/log/scheduler_manager";

#define TAG "SCH_MGR"
#define Q_NAME "SCH"

static const string ND_DEVICE_REL_PATH = "/home/ubuntu/.nddevice";

NDService *nd_service_obj; //nd service object, to detect critical Errors which will be send to Health stats and cloud
static nd_msgq_t *server_q=NULL;

bool check_process(string pname){
    string resp = "";
    string command = "ps -ef | grep \""+ pname + "$\" | grep -v grep";
    system_execute_with_resp(TAG, command, resp);
    return !resp.empty();
}

static string get_msgq_name() {
    return Q_NAME;
}

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

static void process_trigger_scheduler(){
    if(check_process("scheduler")){
        LOG_I(TAG,"scheduler is running");
    }else{
        LOG_I(TAG,"scheduler is not running. Starting Now");
        int scheduler_status = system("nohup /home/ubuntu/bin/wrapper_scheduler 2>/dev/null &");
        LOG_C(TAG,"Started scheduler: %d",(scheduler_status==0));
    }
}

static void msg_loop() {
    nd_msgq_t::nd_msg_t *msg;

    while(1) {
        if( (msg = server_q->receive() ) == NULL ) {
            LOG_E(TAG, "Receive message failed" );
            continue;
        }

        msg_type_t type = get_msg_type(msg->get_buffer());
        generic_msg_t *m = (generic_msg_t *)msg->get_buffer();

        if( m == NULL ) {
            LOG_E(TAG, "Received NULL message");
            continue;
        }

        //LOG_D(TAG, "%d received", m->msg_type);

        switch( type ) {
            case TRIGGER_SCHEDULER_LEGACY:
                process_trigger_scheduler();
                break;

            default:
                LOG_E(TAG, "Unknown message: %d", type);
        }
        delete msg;
    }
}


int main(int argc, char *argv[]) {
    nd_service_obj = NDService::get_service_obj(TAG);
    printf("initilizing logger\n");
    bool status_log = nd_log_init( log_dir.c_str() );
    if(status_log == false) {
        printf("unable to init logger :: Exiting from main");
        return 1;
    }

    #ifdef ROUTE_LOGS
    route_logs( log_dir.c_str() );
    #endif
    
    LOG_I(TAG, "######Starting SchedulerManager######");

    if( init_msgq() == false ) {
        LOG_E(TAG, "Init msgq failed, exiting");
        return 1;
    }
    msg_loop();
    nd_service_obj->release_service_obj();
    return 0;
}