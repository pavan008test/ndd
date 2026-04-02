/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Pawan Kumar <pawan.kumar@netradyne.com>, October 2016
 */

#include "MediaRecorder.h"
#include <pthread.h>
#include "gst_recorder.h"
#include <iostream>
#include <log.h>
#include <string.h>
#include <config_parser.h>
#ifdef KRAIT
#include "qmmf_kinesis_interface.h"
#endif

using namespace std;

static obj_context_t  sObj[DEVICE_CAMERA_POSITION_MAX] = { {NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL},
                                                           {NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL},
                                                           {NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL},
                                                           {NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL} 
                                                         };

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; 

static void record_file_callback(record_file_event event, uint64_t raw_time_us, uint64_t epoch_time_us, uint64_t pts_time, void *app,
                                 uint64_t session_frame_count, bool is_ld, void* filename)
{
    obj_context_t *ctxt = (obj_context_t *) app;

    if ((ctxt != NULL) && (ctxt->record_cb != NULL)) {
 	
         if (event == GST_RECORD_FILE_START)
             ctxt->record_cb(RECORD_START, raw_time_us, epoch_time_us, pts_time, ctxt->app_record_ctxt, session_frame_count, is_ld, filename);
         else if (event == GST_RECORD_FILE_STOP)
             ctxt->record_cb(RECORD_STOP, raw_time_us, epoch_time_us, pts_time, ctxt->app_record_ctxt, session_frame_count, is_ld, filename);
    }

    return;
}

static void record_timestamp_callback(uint64_t time_ld, uint64_t pts_time_ld, void *app)
{
    obj_context_t *ctxt = (obj_context_t *) app;

    if ((ctxt != NULL) && (ctxt->timestamp_cb != NULL)) {
        ctxt->timestamp_cb(time_ld, pts_time_ld, ctxt->app_timestamp_ctxt);
    }

    return;
}

static void record_data_prod_callback(uint64_t epoch_time_dp, uint64_t pts_time_dp, void *app)
{
    obj_context_t *ctxt = (obj_context_t *) app;

    if ((ctxt != NULL) && (ctxt->data_prod_cb != NULL)) {
        ctxt->data_prod_cb(epoch_time_dp, pts_time_dp, ctxt->app_data_prod_ctxt);
    }

    return;
}

static void record_state_callback(record_status_event event, void *app)
{   
    int status = false;
    obj_context_t *ctxt = (obj_context_t *) app;

    if (ctxt != NULL) {
        if ((event == GST_RECORD_EVENT_PIPELINE_ERR) && (ctxt->error_cb != NULL))
             ctxt->error_cb(FATAL_ENCODE_OTHER,ctxt->app_error_ctxt);
        else if ((event == GST_RECORD_EVENT_SHM_FAILED) && (ctxt->error_cb != NULL))
             ctxt->error_cb(FATAL_SHM_BLOCK,ctxt->app_error_ctxt);
        else if ((event == GST_RECORD_EVENT_INTERNAL_ERR) && (ctxt->error_cb != NULL))
             ctxt->error_cb(FATAL_CAMERA_OTHER,ctxt->app_error_ctxt);
        else if ((event == GST_RECORD_EVENT_FIRST_FRAME_MARKER) && (ctxt->first_cb != NULL))
             ctxt->first_cb(ctxt->app_first_ctxt);
        else if ((event == GST_RECORD_EVENT_KEEP_ALIVE) && (ctxt->alive_cb != NULL))
             ctxt->alive_cb(ctxt->app_alive_ctxt);
    }
    return; 
}

static void frame_write_callback (uint64_t frame_number, uint64_t raw_time_us, uint64_t epoch_time_us, void *app)
{
    obj_context_t *ctxt = (obj_context_t *) app;
    if (ctxt->frame_write_cb)
        ctxt->frame_write_cb (frame_number, raw_time_us, epoch_time_us, ctxt->frame_write_cb_ctxt);
}

static const char* record_fname_cb( void *app, uint64_t session_first_frame_epoch_time_ms ) {

    int status = false;
    obj_context_t *ctxt = (obj_context_t *) app;

    if (ctxt != NULL) {
        if ( ctxt->fname_cb != NULL ) {
            string f = ctxt->fname_cb(ctxt->app_fname_ctxt, session_first_frame_epoch_time_ms);
            return strdup(f.c_str());
        }
    }

    return "";
}

static void record_appsink_cb(int64_t cb_pts, int cam_num, uint64_t raw_time_ns, uint64_t epoch_time_ns,
                              uint64_t raw_frame_sent_time_ms, int64_t frame_size, int smb_id, int64_t uid, void *app )
{
    obj_context_t *ctxt = (obj_context_t *) app;
    if (ctxt != NULL) {
        if ( ctxt->appsink_cb != NULL ) {
            ctxt->appsink_cb(cb_pts, cam_num, raw_time_ns, epoch_time_ns, raw_frame_sent_time_ms, frame_size, smb_id, uid);
            return ;
        }
    }
    return ;
}

static void record_live_stream_frame_drop_cb( void *app, int cam_num)
{
    obj_context_t *ctxt = (obj_context_t *) app;
    if (ctxt != NULL) {
        if (ctxt->livestream_framedrop_cb != NULL) {
            LOG_I("LIVE_STREAM", "record_live_stream_frame_drop_cb failed");
            ctxt->livestream_framedrop_cb(cam_num);
            return ;
        }
    }
    return;
}

static bool record_qr_scan_cb(void *app, unordered_map<string, vector<string>> qr_scan_out, int num_qr_codes_detected, uint64_t qr_frame_ts, qr_scan_status qrscan_status)
{
    obj_context_t *ctxt = (obj_context_t *) app;
    bool ret = false;
    if (ctxt != NULL) {
        if (ctxt->qrscan_cb != NULL) {
            LOG_D("QR_SCAN", "record_qr_scan_cb called");
            ret = ctxt->qrscan_cb(qr_scan_out, num_qr_codes_detected, qr_frame_ts, qrscan_status);
        }
    }
    return ret;
}

MediaRecorder::MediaRecorder(native_camera_config_t native_cam_config, realtime_camera_config_t rt_config={0})
        :Component(native_cam_config.name)
{
    isActive = false;
    this->cam_pos = native_cam_config.cam_pos;
    string name = native_cam_config.name;

    LOG_I(name.c_str(), "Media Recorder Constructor \n");
    comptag = get_name();

    plat_handle = init_camera_record_platform(native_cam_config, &rt_config);
    if (plat_handle != NULL) {
        LOG_I(name.c_str(), "Set record callback to plaform \n");

        //enabling frame_write_callback for only front cam
        if ((cam_pos == DEVICE_CAMERA_POSITION_FRONT)) {
            set_record_callback_platform(plat_handle,
                    record_file_callback,
                    record_state_callback,
                    frame_write_callback,
                    record_timestamp_callback,
					record_data_prod_callback,
                    (void *)&sObj[cam_pos]);
        } else {
            set_record_callback_platform(plat_handle,
                    record_file_callback,
                    record_state_callback,
                    NULL,
                    record_timestamp_callback,
					NULL,
                    (void *)&sObj[cam_pos]);
        }

        set_fname_callback_platform(plat_handle,
                &record_fname_cb,
                (void *)&sObj[cam_pos]);

        set_appsink_callback_platform( plat_handle, &record_appsink_cb );

        set_live_stream_frame_drop_callback_platform(plat_handle, &record_live_stream_frame_drop_cb);

#ifdef KRAIT
        if ((cam_pos == DEVICE_CAMERA_POSITION_BACK)) {
            set_qr_scan_callback_platform(plat_handle, &record_qr_scan_cb);
        }
#endif
    }
    
    return;
}

bool MediaRecorder::isInit()
{
    return (NULL == plat_handle ? false : true);  
}

bool MediaRecorder::register_error_callback(component_error_cb_t *cb, void *app)
{
   bool status = false;

   if (NULL == plat_handle || isActive || NULL == cb)
       return false;

   pthread_mutex_lock(&mutex);

   LOG_I(comptag.c_str(), "register error callback \n"); 

   for (int i= 0 ; i < DEVICE_CAMERA_POSITION_MAX ; i++) {
         if (sObj[i].mobj == this) {
             sObj[i].error_cb = cb;
             sObj[i].app_error_ctxt = app;
             status = true;
             break;
         }
   }

   pthread_mutex_unlock(&mutex);

   return status;
}

bool MediaRecorder::register_fname_callback(fname_cb_t cb, void *app)
{
   bool status = false;

   if (NULL == plat_handle || isActive || NULL == cb)
       return false;

   pthread_mutex_lock(&mutex);

   LOG_D(comptag.c_str(), "register fname callback \n"); 

   for (int i= 0 ; i < DEVICE_CAMERA_POSITION_MAX ; i++) {
         if (sObj[i].mobj == this) {
             sObj[i].fname_cb = cb;
             sObj[i].app_fname_ctxt = app;
             status = true;
             break;
         }
   } 

   pthread_mutex_unlock(&mutex);

   return status;
}

bool MediaRecorder::register_timestamp_callback(timestamp_cb_t cb, void *app)
{
    bool status = false;

    if (NULL == plat_handle || isActive || NULL == cb) {
        return false;
    }

    pthread_mutex_lock(&mutex);

    LOG_D(comptag.c_str(), "register timestamp callback \n");

    for (int i= 0 ; i < DEVICE_CAMERA_POSITION_MAX ; i++) {
        if (sObj[i].mobj == this) {
            sObj[i].timestamp_cb = cb;
            sObj[i].app_timestamp_ctxt = app;
            status = true;
            break;
        }
    }

    pthread_mutex_unlock(&mutex);

    return status;
}

bool MediaRecorder::register_data_prod_callback(data_prod_cb_t cb, void *app)
{
   bool status = false;

   if (NULL == plat_handle || isActive || NULL == cb) {
       return false;
   }

   pthread_mutex_lock(&mutex);

   LOG_D(comptag.c_str(), "register data product callback \n");

   for (int i= 0 ; i < DEVICE_CAMERA_POSITION_MAX ; i++) {
       if (sObj[i].mobj == this) {
           sObj[i].data_prod_cb = cb;
           sObj[i].app_data_prod_ctxt = app;
           status = true;
           break;
       }
   }

   pthread_mutex_unlock(&mutex);

   return status;
}

bool MediaRecorder::register_appsink_callback(appsink_cb_t cb, void *app)
{
   bool status = false;

   if (NULL == plat_handle || isActive || NULL == cb)
       return false;

   pthread_mutex_lock(&mutex);

   LOG_D(comptag.c_str(), "register_appsink_callback callback \n"); 

   //for (int i= 0 ; i < DEVICE_CAMERA_POSITION_MAX ; i++) 
   int cam_num = *((int*)(&app));
   {
         if (sObj[cam_num].mobj == this) {
             sObj[cam_num].appsink_cb = cb;
             //sObj[cam_num].app_appsink_ctxt = app;
             status = true;
             //break;
         }
   } 
   pthread_mutex_unlock(&mutex);
   return status;
}

bool MediaRecorder::register_livestream_framedrop_callback(livestream_framedrop_cb_t cb, void *app)
{
   bool status = false;

   if (NULL == plat_handle || isActive || NULL == cb)
       return false;

   pthread_mutex_lock(&mutex);

   LOG_D(comptag.c_str(), "register_livestream_framedrop callback \n");

   int cam_num = *((int*)(&app));

   if (sObj[cam_num].mobj == this) {
       sObj[cam_num].livestream_framedrop_cb = cb;
       status = true;
   }

   pthread_mutex_unlock(&mutex);
   return status;
}

bool MediaRecorder::register_first_camera_buffer_notify_callback(first_frame_notify_t cb,
                                                                 void *app_cb)
{

   bool status = false;

   if (NULL == plat_handle || isActive || NULL == cb)
       return false;

   pthread_mutex_lock(&mutex);

   LOG_D(comptag.c_str(), "register first frame notify calllback \n"); 

   for (int i= 0 ; i < DEVICE_CAMERA_POSITION_MAX ; i++) {
         if (sObj[i].mobj == this) {
             sObj[i].first_cb = cb;
             sObj[i].app_first_ctxt = app_cb;
             status = true;
             break;
         }
   } 

   pthread_mutex_unlock(&mutex);

   return status;

}

bool MediaRecorder::register_frame_write_callback (frame_write_cb_t cb, void *app)
{
   bool status = false;

   if (NULL == plat_handle || isActive || NULL == cb)
       return false;

   pthread_mutex_lock(&mutex);

   LOG_D(comptag.c_str(), "register frame capture calllback \n");

   for (int i= 0 ; i < DEVICE_CAMERA_POSITION_MAX ; i++) {
         if (sObj[i].mobj == this) {
             sObj[i].frame_write_cb = cb;
             sObj[i].frame_write_cb_ctxt = app;
             status = true;
             break;
         }
   }
   pthread_mutex_unlock(&mutex);

   return status;

}

bool MediaRecorder::register_alive_callback(component_alive_cb_t *cb , void *app )
{
   bool status = false;

   if (NULL == plat_handle || isActive || NULL == cb)
       return false;

   pthread_mutex_lock(&mutex);

   LOG_D(comptag.c_str(), "register keep alive callback \n"); 

   for (int i= 0 ; i < DEVICE_CAMERA_POSITION_MAX ; i++) {
         if (sObj[i].mobj == this) {
             sObj[i].alive_cb = cb;
             sObj[i].app_alive_ctxt = app;
             status = true;
             break;
         }
   } 

   pthread_mutex_unlock(&mutex);

   return status;
}

bool MediaRecorder::register_qrscan_callback(qrscan_cb_t cb, void *app)
{
   bool status = false;

   if ((plat_handle == NULL) || (isActive == true) || (cb == NULL))
       return false;

   pthread_mutex_lock(&mutex);

   LOG_I(comptag.c_str(), "Registering QR scan callback");

   int cam_num = *((int*)(&app));

   if (sObj[cam_num].mobj == this) {
       sObj[cam_num].qrscan_cb = cb;
       status = true;
   }

   pthread_mutex_unlock(&mutex);
   return status;
}

vector< pair< string, string > > MediaRecorder::get_capabilities()
{
    if (NULL == plat_handle)
        return param;

    /** To be implemented **/

    return param;  
}


vector< pair< string, string > > MediaRecorder::dump_stats()
{
   /** To be implemented **/

   return (param);
}


bool MediaRecorder::configure(vector< pair< string, string > > &config)
{
   /** To be implemented **/

   return false;
}

bool MediaRecorder::configure(string key, string value)
{
   /** To be implemented **/

   return false;
}

string MediaRecorder::get_config(string key)
{
  /** To be implemented */

  return (string)"";
}

bool MediaRecorder::register_camera_buffer_callback(void *cb)
{
   /** To be implemented **/

   return false; 
}


bool MediaRecorder::release_camera_buffer(cam_buffer_t *buff)
{
   /** To be implemented **/

   return false;
}


bool MediaRecorder::set_record_state_cb(record_state_callback_t state_cb, void *app)
{
   bool status = false;

   if (NULL == plat_handle || isActive || NULL == state_cb)
       return false;

   pthread_mutex_lock(&mutex);

   LOG_D(comptag.c_str(), "Set record state callback \n"); 

   for (int i= 0 ; i < DEVICE_CAMERA_POSITION_MAX ; i++) {
         if (sObj[i].mobj == this) {
             sObj[i].record_cb = state_cb;
             sObj[i].app_record_ctxt = app;
             status = true;
             break;
         }
   } 

   pthread_mutex_unlock(&mutex);

   return status;
}

MediaRecorder::~MediaRecorder()
{
    isActive = false;

    LOG_D(comptag.c_str(), "Destructor \n"); 

    destroy_camera_record_platform(plat_handle);

    cam_pos = DEVICE_CAMERA_POSITION_MAX;

    plat_handle = NULL;
    
    return;
}

bool MediaRecorder::init_camera_record_session ()
{
    bool result = false;
   
    if (NULL == plat_handle)
        return false;

    LOG_I(comptag.c_str(), "init record session \n");

    result = init_record_session_platform (plat_handle);

    return result;
}

bool MediaRecorder::stop_record_session ()
{
   bool result = false;

   if (!isActive || NULL == plat_handle)
        return false;

   LOG_I(comptag.c_str(), "stop record session \n");

   result = stop_record_session_platform(plat_handle);

   if (result)
       isActive = false;

   return result;
}

bool MediaRecorder::start_record_session ()
{
   bool result = false;

   if (isActive || NULL == plat_handle)
        return false;

   LOG_I(comptag.c_str(), "start record session \n");

   result = start_record_session_platform(plat_handle);

   if (result)
       isActive = true;

   return result;
}

bool MediaRecorder::start_kinesis_session ( req_livestreaming_data_t *start_kinesis_msg )
{
   bool result = false;
   if ( NULL == plat_handle )
        return false;
   LOG_I(comptag.c_str(), "start kinesis session \n");
   result = startkinesis(plat_handle, start_kinesis_msg);
   return result;
}

bool MediaRecorder::stop_kinesis_session (req_livestreaming_data_t *stop_kinesis_msg )
{
   bool result = false;
   if ( NULL == plat_handle )
        return false;
   LOG_I(comptag.c_str(), "stop kinesis session \n");
   result = stopkinesis(plat_handle, stop_kinesis_msg);
   return result;
}

MediaRecorder *  MediaRecorder::create_media_recorder(native_camera_config_t native_cam_config, realtime_camera_config_t rt_config={0})
{
   MediaRecorder *obj = NULL;
   cam_pos_t cam_pos = native_cam_config.cam_pos;
   string name = native_cam_config.name;

   if (cam_pos < DEVICE_CAMERA_POSITION_FRONT || cam_pos >= DEVICE_CAMERA_POSITION_MAX)
       return NULL;

   pthread_mutex_lock(&mutex);

   if (NULL == sObj[cam_pos].mobj) {
       LOG_I(name.c_str(), "create media recorder \n");

       obj = new MediaRecorder(native_cam_config, rt_config);

       if (!obj->isInit()) {

           LOG_E(name.c_str(), "create media recorder failed \n");
           delete obj; 

           obj = NULL;     
    
       } else {

          sObj[cam_pos].mobj = obj;        
       }
   }

   obj = sObj[cam_pos].mobj;

   pthread_mutex_unlock(&mutex);
 
   return obj;   
}

bool MediaRecorder::release_media_recorder(MediaRecorder *media)
{
    bool result = false , found = false;

    pthread_mutex_lock(&mutex);

    for (int i= 0 ; i < DEVICE_CAMERA_POSITION_MAX ; i++) {
         if (sObj[i].mobj == media) {
             sObj[i].mobj = NULL;
             sObj[i].record_cb = NULL;
             sObj[i].error_cb = NULL;
             sObj[i].fname_cb = NULL;
             sObj[i].frame_write_cb = NULL;
             sObj[i].appsink_cb = NULL;
             sObj[i].app_record_ctxt = NULL;
             sObj[i].app_error_ctxt = NULL;
             sObj[i].app_fname_ctxt = NULL;
             sObj[i].livestream_framedrop_cb = NULL;
             found = true;
             break;
         }
    }

    pthread_mutex_unlock(&mutex);

    if (found) {
         media->stop_record_session ();
         delete media;
         result = true;
    }

    return result;
}

void MediaRecorder::end_record_session(bool restart_pipeline_required)
{
#ifdef BAGHEERA2
	restart_record_session_platform(plat_handle, restart_pipeline_required);
#endif
}

bool MediaRecorder::set_privacy(int cam_num, int privacy)
{
    return device_set_privacy(cam_num, privacy);
}

void MediaRecorder::recreate_rt_shared_mem()
{
    recreate_rt_shared_memory(plat_handle);
}

bool MediaRecorder::start_QR_scan(char tags[kQRScanMaxTagPatterns][kQRScanMaxTagPatternDataLen])
{
#ifdef KRAIT
    return device_start_QR_scan(tags);
#endif
}

bool MediaRecorder::stop_QR_scan()
{
#ifdef KRAIT
    return device_stop_QR_scan();
#endif
}
