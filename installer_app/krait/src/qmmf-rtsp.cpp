#include <log.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <stdint.h>
#include <unistd.h>
#include <qmmf_http_interface.h>
#include <qmmf_streaming_api.h>

#define TAG "RTSP"
#define LIB_PATH "libhttp_interface.so"
#define MODULE_NAME "QMMF_MODULE"
#if 0
qmmf_handle_t *qmmf_open();
int32_t qmmf_connect( qmmf_handle_t *handle);
int32_t qmmf_start_camera(qmmf_handle_t *handle, int cam_num);
int32_t qmmf_create_session(qmmf_handle_t *handle, uint32_t *session_id);
int32_t qmmf_create_video_track(qmmf_handle_t *handle, int cam_num, uint32_t session_id);
int32_t qmmf_start_session(qmmf_handle_t *handle, uint32_t session_id);

int32_t qmmf_stop_session(qmmf_handle_t *handle, uint32_t session_id);
int32_t qmmf_delete_video_track(qmmf_handle_t *handle, uint32_t session_id, int32_t track_id);
int32_t qmmf_delete_session(qmmf_handle_t *handle, uint32_t session_id);
int32_t qmmf_stop_camera(qmmf_handle_t *handle, int cam_num);
int32_t qmmf_disconnect( qmmf_handle_t *handle);

qmmf_handle_t *qmmf_handle;
int ret;
int cam_num = 0;
uint32_t session_id = 0;
int track_id = 1;
int main(  )
{

	qmmf_handle = qmmf_open();
	ret = qmmf_connect( qmmf_handle );
	
	if(ret != 0)
	{
		LOG_E(TAG, "qmmf connect failed");
		exit(0);
	}
		
	ret = qmmf_start_camera(qmmf_handle, cam_num);
	if(ret != 0)
	{
		LOG_E(TAG, "qmmf start failed");
		exit(0);
	}

	ret = qmmf_create_session(qmmf_handle, &session_id);
	if(ret != 0)
	{
		LOG_E(TAG, "qmmf create session failed");
		exit(0);
	}

	ret = qmmf_create_video_track(qmmf_handle, cam_num, session_id);
	if(ret != 0)
	{
		LOG_E(TAG, "qmmf create video track failed");
		exit(0);
	}

	ret = qmmf_start_session(qmmf_handle, session_id);
	if(ret != 0)
	{
		LOG_E(TAG, "qmmf start session failed");
		exit(0);
	}
	
    sleep(30);

	ret = qmmf_stop_session(qmmf_handle, session_id);
	if(ret != 0)
	{
		LOG_E(TAG, "qmmf stop session failed");
		exit(0);
	}

	ret = qmmf_delete_video_track(qmmf_handle, session_id, track_id);
	if(ret != 0)
	{
		LOG_E(TAG, "qmmf delete video track failed, ret = %d", ret);
		exit(0);
	}

   	ret = qmmf_delete_session(qmmf_handle, session_id);
	if(ret != 0)
	{
		LOG_E(TAG, "qmmf delete session failed");
		exit(0);
	}

	ret = qmmf_stop_camera(qmmf_handle, cam_num);
	if(ret != 0)
	{
		LOG_E(TAG, "qmmf stop failed");
		exit(0);
	}

	ret = qmmf_disconnect( qmmf_handle );
	
	if(ret != 0)
	{
		LOG_E(TAG, "qmmf connect failed");
		exit(0);
	}
    if(qmmf_handle)
    {
        free(qmmf_handle);
    }
    qmmf_handle = NULL;

}

#endif
qmmf_handle_t *qmmf_open() {
    qmmf_handle_t *ret = NULL;
    void *handle;
    struct qmmf_http_interface_t *qmmf_intf;
    int32_t status;

    handle = dlopen(LIB_PATH, RTLD_NOW);
    if (NULL == handle) {
        LOG_E(TAG, "%s: Unable to open web interface library: %s!\n",
            __func__, dlerror());
        errno = -1;
        goto EXIT;
    }

    qmmf_intf = (struct qmmf_http_interface_t *) dlsym(handle, MODULE_NAME);
    if (NULL == qmmf_intf) {
        LOG_E(TAG, "%s: Cannot find web interface: %s!\n",
            __func__, dlerror());
        errno = -1;
        goto EXIT;
    }

    ret = (qmmf_handle_t *) malloc(sizeof(qmmf_handle_t));
    if (NULL == ret) {
        LOG_E(TAG, "%s: No resources for QMMF handle!", __func__);
        errno = -ENOMEM;
        goto EXIT;
    }

    status = qmmf_intf->open(&ret->qmmf_mod);
    if (0 != status) {
        LOG_E(TAG, "%s: Module open failed: %d", __func__, status);
        errno = status;
        goto EXIT;
    }

    ret->qmmf_intf = qmmf_intf;
    ret->lib_handle = handle;

    return ret;

EXIT:

    if (handle) {
        dlclose(handle);
    }

    if (ret) {
        free(ret);
    }

    return NULL;
}


int32_t qmmf_connect(qmmf_handle_t *handle) {
    if (NULL == handle) {
        LOG_E(TAG, "%s: Invalid handle!\n", __func__);
        return -EINVAL;
    }

    return handle->qmmf_mod.connect(&handle->qmmf_mod);
}

int32_t qmmf_disconnect(qmmf_handle_t *handle) {
    if (NULL == handle) {
        LOG_E(TAG, "%s: Invalid handle!\n", __func__);
        return -EINVAL;
    }

    return handle->qmmf_mod.disconnect(&handle->qmmf_mod);
}

int32_t qmmf_start_camera(qmmf_handle_t *handle, int cam_num) {
    if (NULL == handle) {
        LOG_E(TAG, "%s: Invalid handle!\n", __func__);
        return -EINVAL;
    }
    qmmf_camera_start_param params;
    params.zsl_width = 1920;
    params.zsl_height = 1080;
    params.frame_rate = 30;
    params.zsl_mode = 0;
    params.zsl_queue_depth = 8;
    params.flags = 0;

    return handle->qmmf_mod.start_camera(&handle->qmmf_mod, cam_num, params);
}

int32_t qmmf_stop_camera(qmmf_handle_t *handle, int cam_num) {
    if (NULL == handle) {
        LOG_E(TAG, "%s: Invalid handle!\n", __func__);
        return -EINVAL;
    }

    return handle->qmmf_mod.stop_camera(&handle->qmmf_mod, cam_num);
}

int32_t qmmf_create_session(qmmf_handle_t *handle, uint32_t *session_id) {
    if (NULL == handle) {
        LOG_E(TAG, "%s: Invalid handle!\n", __func__);
        return -EINVAL;
    }

    return handle->qmmf_mod.create_session(&handle->qmmf_mod, session_id);
}

int32_t qmmf_delete_session(qmmf_handle_t *handle, uint32_t session_id) {
    if (NULL == handle) {
        LOG_E(TAG, "%s: Invalid handle!\n", __func__);
        return -EINVAL;
    }
    
    return handle->qmmf_mod.delete_session(&handle->qmmf_mod, session_id);
}

int32_t qmmf_create_video_track(qmmf_handle_t *handle, int cam_num, uint32_t session_id, int32_t track_id) {
    if (NULL == handle) {
        LOG_E(TAG, "%s: Invalid handle!\n", __func__);
        return -EINVAL;
    }

    qmmf_video_track_param_t params;
    params.camera_id = cam_num;
    params.track_id = track_id;
    params.session_id = session_id;
    params.width = 1280;
    params.height = 720;
    params.framerate = 30;
    params.codec = CODEC_HEVC;
    params.output = 0;
    params.bitrate = 300000;
    params.low_power_mode = 0;
    params.link_track_id = 0;

    return handle->qmmf_mod.create_video_track(&handle->qmmf_mod, params);
}

int32_t qmmf_delete_video_track(qmmf_handle_t *handle, uint32_t session_id, int32_t track_id) {
    if (NULL == handle) {
        LOG_E(TAG, "%s: Invalid handle!\n", __func__);
        return -EINVAL;
    }

    return handle->qmmf_mod.delete_video_track(&handle->qmmf_mod, session_id, track_id);

}
int32_t qmmf_start_session(qmmf_handle_t *handle, uint32_t session_id) {
    if (NULL == handle) {
        LOG_E(TAG, "%s: Invalid handle!\n", __func__);
        return -EINVAL;
    }

    return handle->qmmf_mod.start_session(&handle->qmmf_mod, session_id);
}

int32_t qmmf_stop_session(qmmf_handle_t *handle, uint32_t session_id) {
    if (NULL == handle) {
        LOG_E(TAG, "%s: Invalid handle!\n", __func__);
        return -EINVAL;
    }

    return handle->qmmf_mod.stop_session(&handle->qmmf_mod, session_id, 1);
}

#if 0
int main(void)
{
	start_rtsp(0);
}
#endif
