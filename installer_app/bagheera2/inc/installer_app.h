#ifndef INSTALLER_APP_H
#define INSTALLER_APP_H

#include <bits/stdc++.h>
#include "nd_msgq.h"
#include "nd_msg_types.h"
#include "nd_msg_utils.h"
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <linux/errno.h>
//namespace std;

// this file contains common declration

#define TAG "INSTLR"
#define INET_ADDRSTRLEN   16
#define ERROR       -1


extern string log_dir;
//extern string device_ip;
//extern  string port;
extern string device_ssid;
extern string device_type;
extern string app_version;
extern char str[INET_ADDRSTRLEN];
extern void *reader_thread_main(void *dummpy);
extern void *response_thread_main(void *dummpy);
bool response_command_ack(int sequence_no);

const string response_file = "/home/ubuntu/.nddevice/response_generic_cmd.txt";
const string command_file = "/home/ubuntu/.nddevice/generic_cmd.txt";
const string obd_info_file = "/home/ubuntu/.nddevice/obd_protocol_info.txt";
const string obd_temp_file = "/dev/shm/obd_installer.txt";
const string device_capabilities_file = "/home/ubuntu/.nddevice/latest/capabilities.json";

extern nd_msgq_t *msg_response_q;

enum camera_types{

     BACK_CAM,
     LEFT_CAM,
     RIGHT_CAM,
     OUT_CAM,
     EXT_CAM_1,
     EXT_CAM_2,
     EXT_CAM_3,
     EXT_CAM_4
};
enum regex_match_krait2_idx_t {
    KRT2_CURR_TIME      = 2,
    KRT2_TEMP           = 4,
    KRT2_MOD_MIT_LEV    = 6,
    KRT2_MODPROC_MIT_LEV= 8,
    KRT2_RST_COUNTER    = 10,
    KRT2_MODE           = 12,
    KRT2_SYS_MODE       = 14,
    KRT2_PS_STATE       = 16,
    KRT2_IMS_REG_STATE  = 18,
    KRT2_IMS_MODE       = 20,
    KRT2_IMS_SRV_STATE  = 22,
    KRT2_LTE_BAND       = 24,
    KRT2_LTE_BW         = 26,
    KRT2_LTE_RX_CHAN    = 28,
    KRT2_LTE_TX_CHAN    = 30,
    KRT2_LTE_CA_STATE   = 32,
    KRT2_EMM_STATE      = 34,
    KRT2_RRC_STATE      = 36,
    KRT2_PCC_RXM_RSSI   = 38,
    KRT2_RXM_RSRP       = 40,
    KRT2_PCC_RXD_RSSI   = 42,
    KRT2_RXD_RSRP       = 44,
    KRT2_TX_PWR         = 46,
    KRT2_TAC            = 48,
    KRT2_RSRQ           = 50,
    KRT2_CELL_ID        = 52,
    KRT2_SINR           = 54,
    KRT2_LTE_MAX        = 55
};

enum regex_match_idx_t {
    MODE_IDX = 4,
    SYSTEM_IDX = 6,
    PS_STATE_IDX = 8,
    BAND_IDX = 10,
    LTE_BW_IDX = 12,
    WCDMA_CH_IDX = 12,
    LTE_CH_IDX = 14,
    LTE_RSSI_IDX = 21,
    WCDMA_RXM_RSSI0_IDX = 24,
    LTE_RSRP_IDX = 25,
    WCDMA_RXD_RSSI0_IDX = 26,
    WCDMA_RXM_RSSI1_IDX = 28,
    LTE_RSRQ_IDX = 29,
    WCDMA_RXD_RSSI1_IDX = 30,
    MAX_WCDMA_REGEX_IDX = 30,
    LTE_SINR_IDX = 33,
    MAX_LTE_REGEX_IDX = 33
};


#endif
