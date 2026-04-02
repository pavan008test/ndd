/* Copyright (C) 2024 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Harshita Jaka <harshita.jaka@netradyne.com>, July 2024
 */

#include <nd_dta.h>
using namespace std;
#define TAG "DTA"
#define Q_NAME "DTA"
static const string log_dir = "/home/ubuntu/.nddevice/log/nd_dta";
static bool dta_enable = false;


void nd_dta::sendBroadcastMessage(nd_server *sock, nlohmann::json broadcastMessage) {
    support_dta support_dta_obj;
    int udpSocket = sock->createUdpSocket();

    if (udpSocket < 0) {
        return;
    }

    if (!sock->enableBroadcast(udpSocket)) {
        close(udpSocket);
        return;
    }

    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(12345);

    std::string message = broadcastMessage.dump();
    string device_type = broadcastMessage["deviceType"];

    while (true) {
        std::string broadcastAddress = support_dta_obj.getBroadcastAddress(device_type);
        if (broadcastAddress.empty()) {
            LOG_I(TAG, "Broadcast address not found, sleeping for 10 seconds");
            std::this_thread::sleep_for(std::chrono::seconds(10));
            continue;
        }

        serverAddr.sin_addr.s_addr = inet_addr(broadcastAddress.c_str());
        int sentBytes = sendto(udpSocket, message.c_str(), message.length(), 0,
                               (struct sockaddr *)&serverAddr, sizeof(serverAddr));

        if (sentBytes < 0) {
            if (errno != ENETUNREACH) {
                LOG_I(TAG, "Error sending broadcast message %s", strerror(errno));
                close(udpSocket);
                break;
            }
            LOG_I(TAG, "Network is unreachable, trying again");
        }

        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}
void nd_dta::receive_from_automation(nd_server *sock){
    int tcpSocket = sock->createTcpSocket();
    if (tcpSocket < 0) {
        return;
    }
    int tcpPort = 12347;
    {
        support_dta support_dta_obj;
        if(string_to_integer(support_dta_obj.get_config("AutomationConfig", "test_automation", "tcp_port", "", false, false), tcpPort)) {
            LOG_I(TAG,"TCP port for automation is %d", tcpPort);
        } else {
            LOG_I(TAG,"Using default TCP port %d for automation", tcpPort);
        }
    }
    if (!sock->bindAndListenTcpSocket(tcpSocket, tcpPort)) {
        close(tcpSocket);
        return;
    }
    LOG_I(TAG,"TCP server is now listening for incoming connections on port %d" ,tcpPort);

    while (true) {
        int clientSocket = sock->acceptTcpConnection(tcpSocket);
        if (clientSocket < 0) {
            close(tcpSocket);
            return;
        }
        // Create an instance of data_handler
        support_dta *sd = new support_dta();

        // Create a new thread for each client
        thread clientThread([sd](int clientSocket){
            sd->receiveData(clientSocket);
            delete sd;  // delete the data_handler object when done
        }, clientSocket);
        clientThread.detach();
    }
}

void* nd_dta::start()
{
    LOG_I(TAG, "start_support_dta");
    support_dta support_dta_obj;
    string device_ssid = support_dta_obj.get_config("DeviceConfig", "identity", "deviceid", "", false, false);
    string device_type = support_dta_obj.get_config("DeviceConfig", "identity", "devicetype", "", false, false);
    string raspberrypi_id = support_dta_obj.get_config("AutomationConfig", "test_automation", "raspberry_id", "", false, false);
    string ignition_relay_no = support_dta_obj.get_config("AutomationConfig", "test_automation", "ignition_relay_no", "", false, false);
    string relay_id = support_dta_obj.get_config("AutomationConfig", "test_automation", "ignition_relay_id", "", false, false);
    nd_server sock;
    nlohmann::json broadcastMessage = nlohmann::json();
    broadcastMessage["deviceId"] = device_ssid;
    broadcastMessage["deviceType"] = device_type;
    broadcastMessage["raspberryPiId"] = raspberrypi_id;
    broadcastMessage["relayId"] = relay_id;
    broadcastMessage["ignitionRelayNo"] = ignition_relay_no;
    thread broadcastThread(&nd_dta::sendBroadcastMessage, this, &sock, broadcastMessage);
    thread tcpThread(&nd_dta::receive_from_automation, this, &sock);
    support_dta support_dta;
    support_dta.get_udid();
    broadcastThread.join();
    tcpThread.join();

}

nd_dta::nd_dta(string tag, string log_dir, string msg_q_name) : ndBase(tag, log_dir, msg_q_name) {
    LOG_I(TAG, "nd_dta Constructor");
}


int main() {
    nd_dta dta(TAG, log_dir, Q_NAME);
    dta_enable = isAutomationEnabled();
    if (!dta_enable)
    {
        LOG_E(TAG, "Test automation not enabled or device not in staging");
        bool isDisabled = disable_service("nd_dta.service",true);
        if (!isDisabled) {
            LOG_E(TAG, "Failed to disable nd_dta service");
            return 1;
        }
        else {
            LOG_I(TAG, "nd_dta service disabled successfully");
            return 0;
        }

    }

    dta.start();

    return 0;

}
