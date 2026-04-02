#ifndef AWS_ELD_H
#define AWS_ELD_H

#include <component.h>
#include <nd_msg_types.h>
#include "ndmb/nd_msg_interface.h"

typedef struct elddata
{
    int64_t     timestamp;
    double  value;
    char        spn_string[MAX_ELD_STRING_SIZE];
    int        is_old_data;
}eld_data_t;

typedef struct ign_status_buff
{
    int64_t timestamp;
    int     status;
}ign_status_buff_t;

bool ndmb_elddata_cb(ndmb_generic_msg_t *msg);
bool read_eld_config();
void *eld_publish_thread(void* arg);
void fillIgnitionStatus(int ignition_status,int64_t timestamp);
#endif