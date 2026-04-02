#ifndef __QMMF_STREAMING__
#define __QMMF_STREAMING__

typedef struct qmmf_handle_t {
    void *lib_handle;
    qmmf_module_t qmmf_mod;
    qmmf_http_interface_t *qmmf_intf;
} qmmf_handle_t;


qmmf_handle_t *qmmf_open();
int32_t qmmf_connect( qmmf_handle_t *handle);
int32_t qmmf_start_camera(qmmf_handle_t *handle, int cam_num);
int32_t qmmf_create_session(qmmf_handle_t *handle, uint32_t *session_id);
int32_t qmmf_create_video_track(qmmf_handle_t *handle, int cam_num, uint32_t session_id, int32_t track_id);
int32_t qmmf_start_session(qmmf_handle_t *handle, uint32_t session_id);

int32_t qmmf_stop_session(qmmf_handle_t *handle, uint32_t session_id);
int32_t qmmf_delete_video_track(qmmf_handle_t *handle, uint32_t session_id, int32_t track_id);
int32_t qmmf_delete_session(qmmf_handle_t *handle, uint32_t session_id);
int32_t qmmf_stop_camera(qmmf_handle_t *handle, int cam_num);
int32_t qmmf_disconnect( qmmf_handle_t *handle);
#endif // __QMMF_STREAMING__
