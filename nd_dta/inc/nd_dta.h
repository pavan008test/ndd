#ifndef ND_DTA_H
#define ND_DTA_H

#include "config_parser.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "nd_msg_utils.h"
#include "nd_msg_types.h"
#include <thread>
#include <system_utils.h>
#include <nd_base.h>
#include <support_dta.h>
#include <log.h>
#include <nlohmann/json.hpp> //nlohmann/json library for JSON parsing
#include <signal.h>
#include <iostream>
#include <ctime>
using namespace std;

class nd_dta : public ndBase 
{

    public:
        nd_dta(string tag, string log_dir, string msg_q_name); 
        ~nd_dta(){}
        void* start();
        void sendBroadcastMessage(nd_server *sock, nlohmann::json broadcastMessage);
        void receive_from_automation(nd_server *sock);

};
#endif