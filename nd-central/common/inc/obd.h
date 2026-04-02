#ifndef OBD_H
#define OBD_H

#include <component.h>
#include <nd_msg_types.h>
#include "ndmb/nd_msg_interface.h"

#define OBD_DATA_PRECISION 4

using namespace std;

class ObdCtx;

class Obd{
public:

    typedef struct genmeta_obddata
    {
        int64_t     time;
        int         spn_id;
        double       value;
        char        spn_string[MAX_PARAM_LENGTH];
        int         is_prop_param;
    }obd_data_t;

    typedef struct genmeta_fuelreport
    {
        uint64_t fuel_ts;
        uint64_t odo_ts;
        double fuel_value;
        double odo_value;
        int fuel_param;
        int odo_param;
        int engine_state;
    }fr_data_t;

    typedef struct genmeta_idlingreport
    {
        uint64_t start_ts;
        uint64_t end_ts;
        double fuel_start;
        double fuel_end;
        double odo_start;
        double odo_end;
        int duration;
        int fuel_param;
        int odo_param;
    }ir_data_t;

};

bool ndmb_obddata_cb(ndmb_generic_msg_t *msg);
bool ndmb_fuelreport_cb(ndmb_generic_msg_t *msg);
bool ndmb_idlingreport_cb(ndmb_generic_msg_t *msg);

#endif


