#include "nd_central.h"
#include <nd_time.h>
#include <log.h>

using namespace std;

extern nd_central_ctx ctx;
static const char *TAG="OBD";

bool ndmb_obddata_cb(ndmb_generic_msg_t *msg){
    if(msg == nullptr){
        LOG_E(TAG, "Received msg is null for obd data");
        return false;
    }
    Obd::obd_data_t obd_val;
    ndmbmsg_obd_data_t *ptr1;
   if (msg->topic == TOPIC_OBD_DATA){
       ptr1= reinterpret_cast<ndmbmsg_obd_data_t *>( msg );
        ndmbmsg_obd_data_t obd_data;
        memset(&obd_data, 0, sizeof(ndmbmsg_obd_data_t));
        memcpy(&obd_data, ptr1, sizeof(ndmbmsg_obd_data_t));
       LOG_D(TAG, "%p, count %d size of pointer %d", ptr1, obd_data.param_count, sizeof(obd_data));
       for (int i=0; i<(obd_data.param_count); i++){
            memcpy(&obd_val.time,obd_data.obd_timestamp+i,sizeof(obd_val.time));
            obd_val.value = obd_data.value[i];
            obd_val.spn_id = obd_data.param_id[i];
#ifdef BAGHEERA2
            //strncpy(obd_val.spn_string, obd_data.param_name[i], MAX_PARAM_LENGTH);
#elif KRAIT
            strncpy(obd_val.spn_string, obd_data.param_name[i], MAX_PROP_STRING_SIZE);
            obd_val.is_prop_param = obd_data.is_prop_param[i];
#endif
            ctx.meta_buff[ctx.session_flipflop].push(obd_val);
            LOG_D(TAG, "value %f : id %d ", obd_val.value, obd_val.spn_id);
       }
   }
   return true;
}

//#ifdef KRAIT
bool ndmb_fuelreport_cb(ndmb_generic_msg_t *msg){

    if(msg == nullptr){
        LOG_E(TAG, "Received msg is null for fuel data");
        return false;
    }
    Obd::fr_data_t fuel_rep;
    ndmbmsg_fuel_report_t *ptr1;
    if (msg->topic == TOPIC_FUEL_REPORT_DATA){
        ptr1 = reinterpret_cast<ndmbmsg_fuel_report_t *>( msg );
        ndmbmsg_fuel_report_t fuel_data;
        memset(&fuel_data, 0, sizeof(ndmbmsg_fuel_report_t));
        memcpy(&fuel_data, ptr1, sizeof(ndmbmsg_fuel_report_t));
        LOG_D(TAG, "************got fuel report data in ndcentral *********");
        fuel_rep.fuel_ts = fuel_data.fuel_ts;
        fuel_rep.odo_ts = fuel_data.odo_ts;
        fuel_rep.fuel_value = fuel_data.fuel_value;
        fuel_rep.odo_value = fuel_data.odo_value;
        fuel_rep.fuel_param = fuel_data.fuel_param;
        fuel_rep.odo_param = fuel_data.odo_param;
        fuel_rep.engine_state = fuel_data.engine_state;
        ctx.meta_buff[ctx.session_flipflop].push(fuel_rep);
   }
}

bool ndmb_idlingreport_cb(ndmb_generic_msg_t *msg){

    Obd::ir_data_t idle_rep;
    ndmbmsg_idling_report_t *ptr1;
    if (msg->topic == TOPIC_IDLING_REPORT_DATA){
        LOG_I(TAG, "************got idling report data in ndcentral *********");
        ptr1 = reinterpret_cast<ndmbmsg_idling_report_t *>( msg );
        ndmbmsg_idling_report_t idle_data;
        memset(&idle_data, 0, sizeof(ndmbmsg_idling_report_t));
        memcpy(&idle_data, ptr1, sizeof(ndmbmsg_idling_report_t));
        idle_rep.start_ts = idle_data.start_ts;
        idle_rep.end_ts = idle_data.end_ts;
        idle_rep.fuel_start = idle_data.fuel_start;
        idle_rep.fuel_end = idle_data.fuel_end;
        idle_rep.odo_start = idle_data.odo_start;
        idle_rep.odo_end = idle_data.odo_end;
        idle_rep.duration = idle_data.duration;
        idle_rep.fuel_param = idle_data.fuel_param;
        idle_rep.odo_param = idle_data.odo_param;
        ctx.meta_buff[ctx.session_flipflop].push(idle_rep);
   }

}
//#endif
