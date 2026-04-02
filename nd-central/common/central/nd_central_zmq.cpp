/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Y.Suresh Kumar <suresh.kumar@netradyne.com>, October 2018
 */

#include "nd_central.h"
#include "nd_file_utils.h"
#include "gst_recorder.h"
#include "device_mode.h"
#include <nd_time.h>
#include <system_utils.h>
#include <zmq.h>
#include <atomic>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

using namespace std;

//#define ENABLE_DMS_RT_YUV_DUMP

#ifdef DMS_CAMERA_SUPPORTED
#ifdef ENABLE_DMS_RT_YUV_DUMP
#include <cuda_egl_interop.h>

#define CUDA_CHECK(call)                                                   \
    {                                                                      \
        cudaError_t err = call;                                            \
        if (err != cudaSuccess) {                                          \
            fprintf(stderr, "CUDA error in file '%s' in line %i : %s.\n",  \
                    __FILE__, __LINE__, cudaGetErrorString(err));          \
            exit(EXIT_FAILURE);                                            \
        }                                                                  \
    }

#define CU_CHECK(call)                                                     \
    {                                                                      \
        CUresult err = call;                                               \
        if (err != CUDA_SUCCESS) {                                         \
            const char *errStr;                                            \
            cuGetErrorString(err, &errStr);                                \
            fprintf(stderr, "CUDA error in file '%s' in line %i : %s.\n",  \
                    __FILE__, __LINE__, errStr);                           \
            exit(EXIT_FAILURE);                                            \
        }                                                                  \
    }
#endif
#endif

extern nd_central_ctx ctx;
extern pthread_mutex_t rt_session_id_mutex;
extern pthread_mutex_t rt_gps_mutex;
extern pthread_mutex_t dis_mutex;
extern std::atomic<bool> bagheera_service_exiting;

extern NDService *nd_service_obj; //nd service object, to detect crashes

static const char *TAG="NDC_RT";

#ifdef testApp
void *shm_zmq_context;
void *shm_zmq_publisher;
#endif

#define MAX_PUBLISHER_Q_SZ 16

#define ZMQ_SOCKET_IN_RT "ipc:///dev/shm/MSGQ/6360"

#define CPU_CORE_0 0 //refers to CPU Core 0 of device to set CPU affinity for Inward recording, Outward recording and RT threads

static const int64_t ONE_MILLI_IN_MICRO = 1000;
static const int cb_print_freq = 124;
int64_t prev_time[CAMERA_POSITION_MAXIMUM];

void *zmq_context;
void *zmq_subscriber;

#ifdef DMS_CAMERA_SUPPORTED
#define CAMREC_BAGHEERA_SOCKET_PATH "/tmp/fd-share-bagheera.socket"
#define BAGHEERA_ANALYTICS_SOCKET_PATH "/tmp/fd-share-analytics.socket"

//shared memory
NdSharedMemoryWriter *shm_writer_dms;

int sfd, cfd; // server and client fds
bool is_analytics_connected = false;

pthread_mutex_t dms_shm_writer_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t dms_shm_writer_cond = PTHREAD_COND_INITIALIZER;

bool pause_dms_rt_processing = false;
int shm_buffers_dms = 30;
#endif

void execute_cmd (string cmd, string tag);

int rt_interface_version = 2;

#ifdef testApp
bool create_zmq(int cam_pos);
#endif

#ifdef DMS_CAMERA_SUPPORTED
// Function to set socket to non-blocking mode
void set_socket_non_blocking(int sockfd)
{
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1) {
        LOG_E("socket_setup", "fcntl F_GETFL api call failed with error: %s", strerror(errno));
        return;
    }
    if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
        LOG_E("socket_setup", "fcntl F_SETFL api call failed with error: %s", strerror(errno));
        return;
    }
}

/* Setting up the unix domain socket to interact with analytics service */
void *socket_setup(void *arg)
{
    struct sockaddr_un addr;

    // Create the server socket
    sfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sfd == -1) {
        LOG_E("socket_setup", "socket api call failed with error: %s", strerror(errno));
        return NULL;
    }

    // Remove the socket file if it exists
    unlink(BAGHEERA_ANALYTICS_SOCKET_PATH);

    // Set up the server address structure
    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, BAGHEERA_ANALYTICS_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    // Bind the socket
    if (bind(sfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_un)) == -1) {
        LOG_E("socket_setup", "bind api call failed with error: %s", strerror(errno));
        close(sfd);
        return NULL;
    }

    // Start listening on the socket
    if (listen(sfd, 1) == -1) {
        LOG_E("socket_setup", "listen api call failed with error: %s", strerror(errno));
        close(sfd);
        return NULL;
    }

    // Set server socket to non-blocking mode
    set_socket_non_blocking(sfd);

    // Polling setup
    struct pollfd fds[1];
    fds[0].fd = sfd;
    fds[0].events = POLLIN;  // Server socket is for accepting connections

    while (1) {
        LOG_I("socket_setup", "Polling the server socket %d for accepting connection from analytics service", sfd);
        int ret = poll(fds, 1, -1);  // -1 means infinite timeout
        if (ret < 0) {
            LOG_E("socket_setup", "poll api call failed with error: %s", strerror(errno));
            close(sfd);
            return NULL;
        }

        // Check if the server socket is ready to accept new connections
        if (fds[0].revents & POLLIN) {
            cfd = accept(sfd, NULL, NULL);
            if (cfd < 0) {
                LOG_E("socket_setup", "accept api call failed with error: %s", strerror(errno));
                continue;
            }
            set_socket_non_blocking(cfd);
            is_analytics_connected = true;
            LOG_I("socket_setup", "analytics service is connected to bagheera service over the socket with client_fd %d", cfd);

            return NULL;
        }
    }
}

void handle_analytics_disconnection(int client_fd)
{
    close(client_fd);
    close(sfd);
    is_analytics_connected = false;

    /* Set up a Unix Domain socket to accept connection from analytics service*/
    pthread_t socket_setup_th;
    pthread_create(&socket_setup_th, NULL, socket_setup, NULL);
}
#endif

bool send_msg_analytics_gps(Gps::gps_data_t val)
{
    int zmq_result = -1;

    if (ctx.zmq_publisher_gps == NULL) {
        LOG_I(TAG, "send_msg_analytics_gps() ctx.zmq_publisher_gps is NULL. returning.");
        return false;
    }
    if (bagheera_service_exiting == true) {
        LOG_I(TAG, "bagheera_service_exiting, return from send_msg_analytics_gps()");
        return true;  // Not a fail so returning true;
    }

    gps_rt_data_t_v3 rt_gps_data;
    memset(&rt_gps_data, 0x00, sizeof(rt_gps_data));

    pthread_mutex_lock(&rt_session_id_mutex);
    nd_strncpy(rt_gps_data.session_id, ctx.sessionid_rt[0].c_str(), GSTREAMER_NAME_LENGTH_MAX);
    pthread_mutex_unlock(&rt_session_id_mutex);

    rt_gps_data.session_id_len = strlen(rt_gps_data.session_id);

    if (rt_interface_version == 2) {
        memcpy(&rt_gps_data.gps_data, &val, sizeof(gps_data_t_v2));

        zmq_result = zmq_send(ctx.zmq_publisher_gps, (char *)&rt_gps_data , sizeof(gps_rt_data_t_v2), 0);
        if (zmq_result == -1) {
            LOG_E(TAG, "send_msg_analytics_gps: FAILED to send message of size %d in zmq", sizeof(gps_rt_data_t_v2));
            return false;
        }
    } else if (rt_interface_version == 3) {
        memcpy(&rt_gps_data.gps_data, &val, sizeof(gps_data_t_v3));

        zmq_result = zmq_send(ctx.zmq_publisher_gps, (char *)&rt_gps_data , sizeof(gps_rt_data_t_v3), 0);
        if (zmq_result == -1) {
            LOG_E(TAG, "send_msg_analytics_gps: FAILED to send message of size %d in zmq", sizeof(gps_rt_data_t_v3));
            return false;
        }
    }

    return true;
}

bool send_msg_analytics_gps_geo_fence(Gps::gps_data_t val)
{
    int zmq_result = -1;

    if (ctx.zmq_publisher_gps_geo_fence == NULL) {
        LOG_I(TAG, "send_msg_analytics_gps_geo_fence() ctx.zmq_publisher_gps_geo_fence is NULL. returning.");
        return false;
    }
    if (bagheera_service_exiting == true) {
        LOG_I(TAG, "bagheera_service_exiting, return from send_msg_analytics_gps_geo_fence()");
        return true;  // Not a fail so returning true;
    }

    gps_rt_data_t_v3 rt_gps_data_geo_fence;
    memset(&rt_gps_data_geo_fence, 0x00, sizeof(rt_gps_data_geo_fence));

    pthread_mutex_lock(&rt_session_id_mutex);
    nd_strncpy(rt_gps_data_geo_fence.session_id, ctx.sessionid_rt[0].c_str(), GSTREAMER_NAME_LENGTH_MAX);
    pthread_mutex_unlock(&rt_session_id_mutex);

    rt_gps_data_geo_fence.session_id_len = strlen(rt_gps_data_geo_fence.session_id);

    if (rt_interface_version == 2) {
        memcpy(&rt_gps_data_geo_fence.gps_data, &val, sizeof(gps_data_t_v2));

        zmq_result = zmq_send(ctx.zmq_publisher_gps_geo_fence, (char *)&rt_gps_data_geo_fence , sizeof(gps_rt_data_t_v2), 0);
        if (zmq_result == -1) {
            LOG_E(TAG, "send_msg_analytics_gps_geo_fence: FAILED to send message of size %d in zmq", sizeof(gps_rt_data_t_v2));
            return false;
        }
    } else if (rt_interface_version == 3) {
        memcpy(&rt_gps_data_geo_fence.gps_data, &val, sizeof(gps_data_t_v3));
        rt_gps_data_geo_fence.gps_data.privacy_enabled = false; // Geo-fence messages should not have privacy enabled

        zmq_result = zmq_send(ctx.zmq_publisher_gps_geo_fence, (char *)&rt_gps_data_geo_fence , sizeof(gps_rt_data_t_v3), 0);
        if (zmq_result == -1) {
            LOG_E(TAG, "send_msg_analytics_gps_geo_fence: FAILED to send message of size %d in zmq", sizeof(gps_rt_data_t_v3));
            return false;
        }
    }

    return true;
}

bool send_msg_analytics_imu(Imu::val_t val_a, Imu::val_t val_g, Imu::val_t val_m)
{
    int zmq_result = -1;

    if (ctx.zmq_publisher_imu == NULL)
        return false;

    if (bagheera_service_exiting == true) {
        LOG_I(TAG, "bagheera_service_exiting, return from send_msg_analytics_imu()");
        return true;  // Not a fail so returning true;
    }

    imu_rt_data_t_v3 all_imu_data;
    memset(&all_imu_data, 0x00, sizeof(all_imu_data));

    all_imu_data.accel_x = val_a.x;
    all_imu_data.accel_y = val_a.y;
    all_imu_data.accel_z = val_a.z;
    all_imu_data.accel_time = val_a.clock_time / ONE_MILLI_IN_MICRO;

    all_imu_data.gyro_x = val_g.x;
    all_imu_data.gyro_y = val_g.y;
    all_imu_data.gyro_z = val_g.z;
    all_imu_data.gyro_time = val_g.clock_time / ONE_MILLI_IN_MICRO;

    // magneto not needed at present
    //all_imu_data.magn_x = val_m.x;
    //all_imu_data.magn_y = val_m.y;
    //all_imu_data.magn_z = val_m.z;
    //all_imu_data.magn_time = val_m.clock_time / ONE_MILLI_IN_MICRO;

    pthread_mutex_lock(&rt_session_id_mutex);
    nd_strncpy(all_imu_data.session_id, ctx.sessionid_rt[0].c_str(), GSTREAMER_NAME_LENGTH_MAX);
    pthread_mutex_unlock(&rt_session_id_mutex);

    all_imu_data.session_id_len = strlen(all_imu_data.session_id);

    if (rt_interface_version == 2) {
        pthread_mutex_lock(&rt_gps_mutex);
        memcpy(&all_imu_data.gps_data, &(ctx.saved_gps), sizeof(gps_data_t_v2));
        pthread_mutex_unlock(&rt_gps_mutex);

        zmq_result = zmq_send(ctx.zmq_publisher_imu, (char *)&all_imu_data , sizeof(imu_rt_data_t_v2), 0);
        if (zmq_result == -1) {
            LOG_E(TAG, "send_msg_analytics_imu: FAILED to send message of size %d in zmq", sizeof(imu_rt_data_t_v2));
            return false;
        }
    } else if (rt_interface_version == 3) {
        pthread_mutex_lock(&rt_gps_mutex);
        memcpy(&all_imu_data.gps_data, &(ctx.saved_gps), sizeof(gps_data_t_v3));
        pthread_mutex_unlock(&rt_gps_mutex);

        zmq_result = zmq_send(ctx.zmq_publisher_imu, (char *)&all_imu_data , sizeof(imu_rt_data_t_v3), 0);
        if (zmq_result == -1) {
            LOG_E(TAG, "send_msg_analytics_imu: FAILED to send message of size %d in zmq", sizeof(imu_rt_data_t_v3));
            return false;
        }
    }

    return true;
}

int fill_rt_config(cam_pos_t cam_pos, Config_parser* bag_conf,
                    Config_parser* nd_config, realtime_camera_config_t* rt_config)
{
    string streamin_flag = "none";
    string outward_streamin_flag, inward_streamin_flag, temp;
    int drowsy_enabled = 0;
    int dms_cam_rt_streaming_enabled = 0;

    memset(rt_config, 0x00, sizeof(ctx.rt_config[0]));

    if ((cam_pos != DEVICE_CAMERA_POSITION_FRONT) && (cam_pos != DEVICE_CAMERA_POSITION_BACK) && (cam_pos != DEVICE_CAMERA_POSITION_DMS)) {
        LOG_I(TAG, "Invalid camera for RT processing");
        return 0;
    }

    streamin_flag = bag_conf->getConfig("streaming", "enable_streaming", "true");
    if (streamin_flag == "false") {
        LOG_E(TAG, "No RT processing since streamin_flag is false");
        return -1;
    }

    // MAKE out streaming true by default
    outward_streamin_flag = "true";

    bool get_override_val = true;
    bool is_val_overridden = false;
    inward_streamin_flag = bag_conf->getConfig("streaming", "inwardcam_streaming", "false", get_override_val, is_val_overridden);
    if ((cam_pos == DEVICE_CAMERA_POSITION_BACK) && (inward_streamin_flag == "false")) {
        LOG_E(TAG, "RT streaming is disabled for inward camera");
        return 0;
    }

    LOG_I(TAG, "bag_conf:: streaming_flag: %s, outward_streaming_flag: %s, inward_streaming_flag: %s",
          streamin_flag.c_str(), outward_streamin_flag.c_str(), inward_streamin_flag.c_str());

    string_to_integer(nd_config->getConfig("rt_interface", "version", "2",
                        get_override_val, is_val_overridden), rt_interface_version);

    string_to_integer(nd_config->getConfig("dms", "enabled", "0",
                        get_override_val, is_val_overridden), dms_cam_rt_streaming_enabled);

    get_override_val = true;
    is_val_overridden = false;
    string_to_integer(nd_config->getConfig("drowsy", "enabled", "0",
                        get_override_val, is_val_overridden), drowsy_enabled);

    LOG_I(TAG, "nd_config:: dms_cam_rt_streaming_enabled: %d, drowsy_enabled: %d", dms_cam_rt_streaming_enabled, drowsy_enabled);
    if ((cam_pos == DEVICE_CAMERA_POSITION_DMS) && !dms_cam_rt_streaming_enabled) {
        LOG_E(TAG, "RT streaming is disabled for dms camera");
        return 0;
    }

    if (cam_pos == DEVICE_CAMERA_POSITION_FRONT) {
        nd_strncpy(&rt_config->socket[0], outcam_rt_streaming_socket.c_str(), MAX_SOCKET_NAME_LEN);

        string_to_integer(nd_config->getConfig("outwardcam_rt_streaming", "width", "640",
                                            get_override_val, is_val_overridden), rt_config->width);
        string_to_integer(nd_config->getConfig("outwardcam_rt_streaming", "height", "360",
                                            get_override_val, is_val_overridden), rt_config->height);

        rt_config->fps = 5;

        LOG_I(TAG, "rt_outward_width: %d, rt_outward_height: %d, rt_outward_fps: %d", rt_config->width, rt_config->height, rt_config->fps);
    } else if (cam_pos == DEVICE_CAMERA_POSITION_DMS && dms_cam_rt_streaming_enabled) {
#ifdef DMS_CAMERA_SUPPORTED
        nd_strncpy(&rt_config->socket[0], dmscam_rt_streaming_socket.c_str(), MAX_SOCKET_NAME_LEN);

        string_to_integer(nd_config->getConfig("dms", "width", "1296",
                                            get_override_val, is_val_overridden), rt_config->width);
        string_to_integer(nd_config->getConfig("dms", "height", "1296",
                                            get_override_val, is_val_overridden), rt_config->height);
        int frame_rate = 30;
        string_to_integer(nd_config->getConfig("dms", "frame_rate", "30",
                                            get_override_val, is_val_overridden), frame_rate);
        int subsample_factor = 3;
        string_to_integer(nd_config->getConfig("dms_drowsy", "subsample_factor", "3",
                                                    get_override_val, is_val_overridden), subsample_factor);

        LOG_I(TAG, "dms_nrt_fps: %d, dms_rt_subsample_factor: %d", frame_rate, subsample_factor);

        rt_config->fps = (frame_rate / subsample_factor);

        string temp = nd_config->getConfig("dms", "shm_buffers", "30", get_override_val, is_val_overridden);
        if (!string_to_integer(temp.c_str(), shm_buffers_dms))
            shm_buffers_dms = 30;

        LOG_I(TAG, "dms_rt_width: %d, dms_rt_height: %d, dms_rt_fps: %d, dms_rt_shm_buffers: %d", rt_config->width, rt_config->height, rt_config->fps, shm_buffers_dms);
#endif
    } else {
        nd_strncpy(&rt_config->socket[0], incam_rt_streaming_socket.c_str(), MAX_SOCKET_NAME_LEN);

        if (dms_cam_rt_streaming_enabled) {
            string_to_integer(nd_config->getConfig("inwardcam_rt_streaming", "width", "640",
                        get_override_val, is_val_overridden), rt_config->width);
            string_to_integer(nd_config->getConfig("inwardcam_rt_streaming", "height", "360",
                        get_override_val, is_val_overridden), rt_config->height);

#ifdef BAGHEERA2
            rt_config->fps = 5;
#elif KRAIT
            rt_config->fps = 1;
#endif
        } else {
            if (drowsy_enabled) {
                string_to_integer(nd_config->getConfig("inwardcam_rt_streaming", "width_drowsy", "1920",
                            get_override_val, is_val_overridden), rt_config->width);
                string_to_integer(nd_config->getConfig("inwardcam_rt_streaming", "height_drowsy", "1080",
                            get_override_val, is_val_overridden), rt_config->height);

                int frame_rate = 15;
                string_to_integer(nd_config->getConfig("inwardRealTime", "frame_rate", "15", get_override_val, is_val_overridden), frame_rate);
                int subsample_factor_drowsy = 1;
                string_to_integer(nd_config->getConfig("inwardRealTime", "subsample_factor_drowsy", "1", get_override_val, is_val_overridden), subsample_factor_drowsy);

                LOG_I(TAG, "INWARD_RT frame_rate: %d, INWARD_RT subsample_factor_drowsy: %d", frame_rate, subsample_factor_drowsy);

                rt_config->fps = (frame_rate / subsample_factor_drowsy);

                LOG_I(TAG, "rt_inward_width: %d, rt_inward_height: %d, rt_inward_fps: %d", rt_config->width, rt_config->height, rt_config->fps);

            } else {
                string_to_integer(nd_config->getConfig("inwardcam_rt_streaming", "width", "640",
                            get_override_val, is_val_overridden), rt_config->width);
                string_to_integer(nd_config->getConfig("inwardcam_rt_streaming", "height", "360",
                            get_override_val, is_val_overridden), rt_config->height);

#ifdef BAGHEERA2
                rt_config->fps = 5;
#elif KRAIT
                rt_config->fps = 1;
#endif
            }
        }
        LOG_I(TAG, "rt_inward_width: %d, rt_inward_height: %d, rt_inward_fps: %d", rt_config->width, rt_config->height, rt_config->fps);
    }

    rt_config->enable_streaming = true;

    return 0;
}

int set_rt_status(cam_pos_t cam_pos, realtime_camera_config_t* rt_config)
{
    if ((cam_pos != DEVICE_CAMERA_POSITION_FRONT) && (cam_pos != DEVICE_CAMERA_POSITION_BACK)) {
        LOG_I(TAG, "Invalid camera for RT streaming over ZMQ socket");
        return true;
    }

    if ((cam_pos == DEVICE_CAMERA_POSITION_FRONT) && (rt_config->enable_streaming == false)) {
        LOG_C(TAG, "RT streaming is disabled for front camera");
        return false;
    }

    if ((cam_pos == DEVICE_CAMERA_POSITION_BACK) && (rt_config->enable_streaming == false)) {
        LOG_C(TAG, "RT streaming is disabled for back camera");
        return true;
    }

#ifdef testApp
    create_zmq(cam_pos);
#endif

    LOG_I(TAG, "Setting socket options for cam %d, context->rt_config.socket %s", cam_pos, rt_config->socket);
    ctx.zmq_context[cam_pos] = zmq_ctx_new ();
    if( ctx.zmq_context[cam_pos] == NULL ) {
        LOG_E(TAG, "ctx.zmq_context[cam_pos] == NULL; errno: %d, zmq_error:   %s", errno, zmq_strerror(zmq_errno()));
        return false;
    }
    ctx.zmq_publisher[cam_pos] = zmq_socket (ctx.zmq_context[cam_pos], ZMQ_PUB);
    if( ctx.zmq_publisher[cam_pos] == NULL ) {
        LOG_E(TAG, "ctx.zmq_publisher[cam_pos] == NULL; errno: %d, zmq_error:   %s", errno, zmq_strerror(zmq_errno()));
        return false;
    }
    int max_q_sz = MAX_PUBLISHER_Q_SZ;
    int linger_val = 100 ; // milisecond
    if( zmq_setsockopt(ctx.zmq_publisher[cam_pos], ZMQ_SNDHWM, &max_q_sz, sizeof(int)) ) {
        LOG_E(TAG, "zmq_setsockopt failed; errno: %d, zmq_error:   %s", errno, zmq_strerror(zmq_errno()));
        return false;
    }
    if( zmq_setsockopt (ctx.zmq_publisher[cam_pos], ZMQ_LINGER, &linger_val, sizeof(int)) ) {
        LOG_E(TAG, "zmq_setsockopt ZMQ_LINGER failed; errno: %d, zmq_error:   %s", errno, zmq_strerror(zmq_errno()));
        return false;
    }

    int trail_count = 0, rc = -1;
    do {
        trail_count++;
        usleep(100*1000); // sleep for 100MS before binding : can this avoid FAILED TO CREATE with err 98 ?
        rc = zmq_bind (ctx.zmq_publisher[cam_pos], rt_config->socket);
        if(rc != 0) {
            LOG_E(TAG, "FAILED TO CREATE zmq_socket %d; errno: %d, zmq_error:   %s", rc, errno, zmq_strerror(zmq_errno()));
            rt_config->enable_streaming = false;
#if ZMQ_TCP
            string portnotmp = rt_config->socket;

            string key_field = "tcp://*:";
            int index = portnotmp.find(key_field, 0);
            if(index != std::string::npos) {
                index = index + key_field.size();
                portnotmp = portnotmp.substr(index, portnotmp.size()-index );
                LOG_I(TAG, "portnotmp %s", portnotmp.c_str());
                string cmdtmp = "lsof -i:"+portnotmp;
                execute_cmd (cmdtmp, "ZMQ_ERROR");
            }
#endif
        }
        else {
            LOG_I(TAG, "SUCCESS IN CREATE zmq_socket");
            rt_config->enable_streaming = true;
            break;
        }
    } while( trail_count < 5 );

    if (strncmp(rt_config->socket, "ipc://", sizeof("ipc://") - 1) == 0)
    {
        LOG_I(TAG, "chmod for file %s" , rt_config->socket + sizeof("ipc://") - 1);
        if(chmod(rt_config->socket + sizeof("ipc://") - 1 , 0777) < 0)
        {
            LOG_E(TAG, "Could not create socket permissions %s", strerror(errno));
        }
    }

    if( rc != 0 ) {
        LOG_C(TAG, "after %d RE trails FAILED TO CREATE zmq_socket rc: %d ", trail_count, rc);

        zmq_close (ctx.zmq_publisher[cam_pos]);
        zmq_ctx_destroy (ctx.zmq_context[cam_pos]);
        ctx.zmq_publisher[cam_pos] = NULL;
        ctx.zmq_context[cam_pos] = NULL;

        return false;
    }
    return true;
}

#ifdef testApp
bool send_msg_testapp(char* msg_pointer, int msg_length, void *zmq_publisher, int flag)
{
    if(zmq_publisher == NULL)
        return false;
    int zmq_result=-1;
    LOG_D(TAG, "sending message of size %d", msg_length);

    zmq_result = zmq_send(zmq_publisher, msg_pointer, msg_length, flag);
    if(zmq_result == -1) {
       LOG_E(TAG, "FAILED to send message of size %d in zmq", msg_length);
       return false;
    }
    LOG_D(TAG, "SUCCESS in send message of size %d in zmq result %d", msg_length, zmq_result);
    return true;
}
#endif

bool send_msg_analytics_cam(char* msg_pointer, int msg_length, void *zmq_publisher, int flag)
{
    if (zmq_publisher == NULL)
        return false;

    if (bagheera_service_exiting == true) {
        LOG_I(TAG, "bagheera_service_exiting, return from send_msg_analytics_cam()");
        return true;  // Not a fail so returning true;
    }

    int zmq_result=-1;
    LOG_D(TAG, "sending message of size %d", msg_length);

    zmq_result = zmq_send(zmq_publisher, msg_pointer, msg_length, flag);
    if (zmq_result == -1) {
       LOG_E(TAG, "FAILED to send message of size %d in zmq", msg_length);
       return false;
    }
    LOG_D(TAG, "SUCCESS in send message of size %d in zmq result %d", msg_length, zmq_result);
    return true;
}

#ifdef DMS_CAMERA_SUPPORTED
bool send_msg_analytics_dms_cam(void *data_ptr, int data_len, int dmabuf_fd)
{
    struct iovec io = {
        .iov_base = data_ptr,
        .iov_len = data_len
    };

    // Control message buffer for sending file descriptor
    char control_buf[CMSG_SPACE(sizeof(int))];
    memset(control_buf, '\0', sizeof(control_buf));
    struct msghdr msg = {0};
    msg.msg_iov = &io;
    msg.msg_iovlen = 1;
    msg.msg_control = control_buf;
    msg.msg_controllen = sizeof(control_buf);

    // Set up the control message header to send file descriptor
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy((int *) CMSG_DATA(cmsg), &dmabuf_fd, sizeof(int));

    int bytes_sent = sendmsg(cfd, &msg, 0);
    if (bytes_sent < 0) {
        LOG_E(TAG, "sendmsg api call failed with error: %s", strerror(errno));
        handle_analytics_disconnection(cfd);
        return false;
    }

    LOG_D(TAG, "DMABUF FD %d sent to analytics process", dmabuf_fd);

    return true;
}
#endif

bool send_drop_session_msg_v2(int cb_cam_pos)
{
    analytics_frame_header_v2 messg_data;

    memset(&messg_data, 0x00, sizeof(messg_data));
    messg_data.cam_id  = cb_cam_pos;
    messg_data.frame_cnt = ctx.yuv_frame_count[cb_cam_pos];;
    messg_data.timestamp = -(ctx.crank_level_RT_thread*10+ctx.idle_mode_RT_thread)-1;
    LOG_I(TAG, "send_drop_session_msg_v2 messg_data.timestamp %lld", messg_data.timestamp);
    messg_data.pts = -1;

    pthread_mutex_lock(&rt_session_id_mutex);
    strncpy(messg_data.session_id, ctx.sessionid_rt[cb_cam_pos].c_str(), GSTREAMER_NAME_LENGTH_MAX);
    pthread_mutex_unlock(&rt_session_id_mutex);
    messg_data.session_id_len = strlen(messg_data.session_id);

    int msg_send_status = true;
    msg_send_status *= send_msg_analytics_cam((char *)&messg_data, sizeof(analytics_frame_header_v2),
                                               ctx.zmq_publisher[cb_cam_pos], 0);

    return msg_send_status;
}

bool send_drop_session_msg_v3(int cb_cam_pos, uint32_t rt_frame_status, uint64_t epoch_time_ms, int64_t buf_pts_ns, int dmabuf_fd=0)
{
    analytics_frame_header_v3 messg_data;
    memset(&messg_data, 0x00, sizeof(messg_data));

    messg_data.cam_id  = cb_cam_pos;
    messg_data.frame_cnt = ctx.yuv_frame_count[cb_cam_pos];;
    messg_data.timestamp = epoch_time_ms;
    messg_data.pts = buf_pts_ns;
    messg_data.rt_frame_status = rt_frame_status;

    pthread_mutex_lock(&rt_session_id_mutex);
    strncpy(messg_data.session_id, ctx.sessionid_rt[cb_cam_pos].c_str(), GSTREAMER_NAME_LENGTH_MAX);
    pthread_mutex_unlock(&rt_session_id_mutex);
    messg_data.session_id_len = strlen(messg_data.session_id);

    bool msg_send_status = false;
    if (cb_cam_pos != DEVICE_CAMERA_POSITION_DMS) {
        msg_send_status = send_msg_analytics_cam((char *)&messg_data, sizeof(analytics_frame_header_v3),
                                              ctx.zmq_publisher[cb_cam_pos], 0);
    } else {
#ifdef DMS_CAMERA_SUPPORTED
        msg_send_status = send_msg_analytics_dms_cam((void *)&messg_data, sizeof(analytics_frame_header_v3), dmabuf_fd);
#endif
    }

    if (msg_send_status == true) {
        LOG_I(TAG, "Successfully sent drop message to analytics for cam_pos: %d, session_id: %s and epoch_time_ms: %llu", cb_cam_pos, messg_data.session_id, epoch_time_ms);
    } else {
        LOG_E(TAG, "Failed to send drop message to analytics for cam_pos: %d, session_id: %s and epoch_time_ms: %llu", cb_cam_pos, messg_data.session_id, epoch_time_ms);
    }

    return msg_send_status;
}

#ifdef testApp
bool create_zmq(int cam_pos)
{
    //string socket;
    //if (cam_pos == 0)
    string socket = "ipc:///dev/shm/MSGQ/6377";
    //else if (cam_pos == 1)
    //socket = "ipc:///dev/shm/MSGQ/6379";
    int rc = -1;

    shm_zmq_context = zmq_ctx_new ();
    if( shm_zmq_context == NULL ) {
        LOG_E(TAG, "shm_zmq_context == NULL; errno: %d, zmq_error:   %s", errno, zmq_strerror(zmq_errno()));
        return false;
    }

    shm_zmq_publisher = zmq_socket (shm_zmq_context, ZMQ_PUB);
    if( shm_zmq_publisher == NULL ) {
        LOG_E(TAG, "shm_zmq_publisher == NULL; errno: %d, zmq_error:   %s", errno, zmq_strerror(zmq_errno()));
        return false;
    }
    int max_q_sz = MAX_PUBLISHER_Q_SZ;
    int linger_val = 100 ; // milisecond
    if( zmq_setsockopt(shm_zmq_publisher, ZMQ_SNDHWM, &max_q_sz, sizeof(int)) ) {
        LOG_E(TAG, "zmq_setsockopt failed; errno: %d, zmq_error:   %s", errno, zmq_strerror(zmq_errno()));
        return false;
    }
    if( zmq_setsockopt (shm_zmq_publisher, ZMQ_LINGER, &linger_val, sizeof(int)) ) {
        LOG_E(TAG, "zmq_setsockopt ZMQ_LINGER failed; errno: %d, zmq_error:   %s", errno, zmq_strerror(zmq_errno()));
        return false;
    }

    int trial_count = 0;
    do {
        trial_count++;
        usleep(100*1000); // sleep for 100MS before binding : can this avoid FAILED TO CREATE with err 98 ?

        rc = zmq_bind (shm_zmq_publisher, socket.c_str());

        if (strncmp(socket.c_str(), "ipc://", sizeof("ipc://") - 1) == 0)
        {
            LOG_I(TAG, "chmod for file %s" , socket.c_str() + sizeof("ipc://") - 1);
            if(chmod(socket.c_str() + sizeof("ipc://") - 1 , 0777) < 0)
            {
                LOG_E(TAG, "Could not create socket permissions %s", strerror(errno));
            }
        }
    } while(trial_count < 5);

    if (rc != 0) {
        LOG_C(TAG, "after %d retries FAILED TO CREATE zmq_socket rc: %d ", trial_count, rc);
    }
}
#endif

// V2 version of the callback
void appsink_camera_callback_v2(int64_t cb_pts, int cb_cam_pos, uint64_t raw_time_ns, uint64_t epoch_time_ns,
                                 uint64_t raw_frame_sent_time_ms, int64_t data_size, int smb_id, int64_t uid)
{
    int disable_process = 0;
    int64_t rt_frames_target_time = 0;

    if (cb_cam_pos == DEVICE_CAMERA_POSITION_FRONT)
        rt_frames_target_time = ctx.send_rt_frames_till_time_outward;
    else if (cb_cam_pos == DEVICE_CAMERA_POSITION_BACK)
        rt_frames_target_time = ctx.send_rt_frames_till_time;

    disable_process = (get_system_monotonic_time() > rt_frames_target_time)
                            || (ctx.idle_mode_RT_thread == 1);

    if (cb_cam_pos == DEVICE_CAMERA_POSITION_FRONT) {
        if (ctx.off_duty_privacy || ctx.geofence_privacy ||
            (!ctx.privacy_params.enhanced_privacy && ctx.fused_privacy &&
             ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_FRONT])) {
            disable_process = 1;
        }
    } else if (cb_cam_pos == DEVICE_CAMERA_POSITION_BACK) {
        if (ctx.off_duty_privacy || ctx.geofence_privacy ||
            (!ctx.privacy_params.enhanced_privacy && ctx.fused_privacy &&
             ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK])) {
            disable_process = 1;
        }
    }

    // if supercap_status is true, power is disconnected
    // We have to stop sending frames
    // DO not change the order of this code because supercap should always supercede
    if (ctx.supercap_status == 1) { //Since this is only applicable to Krait, supercap_status is always 0 for B2
        disable_process = 1;
    }

    if (disable_process) {
        // send drop only for first frame of session
        if (ctx.sessio_drop_message[cb_cam_pos] == 0) {
            LOG_I(TAG, "calling send_drop_session_msg_v2 crank_level_RT_thread %d and idle_mode_RT_thread %d, supercap_status = %d, cam_num = %d",
                         ctx.crank_level_RT_thread, ctx.idle_mode_RT_thread, ctx.supercap_status, cb_cam_pos);
            LOG_I(TAG, "off_duty_privacy: %d, geofence_privacy: %d, fused_privacy = %d", ctx.off_duty_privacy, ctx.geofence_privacy, ctx.fused_privacy);

            send_drop_session_msg_v2(cb_cam_pos);
            ctx.sessio_drop_message[cb_cam_pos]++;
        }
        ctx.yuv_frame_count[cb_cam_pos]++;
        return;
    }

    int64_t pres_time = get_system_time();
    if (epoch_time_ns != 0) {
        pres_time = epoch_time_ns / 1000000;
    }
    int64_t delay = pres_time - prev_time[cb_cam_pos];
    if (delay <= 0) {
        pres_time = pres_time + (-delay) + 1;
        LOG_I(TAG,"road delay %lld", delay);
        LOG_I(TAG, "ROAD :@: Received Frame - count %d, PTS of buffer %f, prestime %lld ::delay %d::",
                ctx.yuv_frame_count[cb_cam_pos], (float)cb_pts / 1000000000.0, pres_time, pres_time - prev_time[cb_cam_pos]);
    }
    if ((ctx.yuv_frame_count[cb_cam_pos] % cb_print_freq) == 0) {
        LOG_I(TAG, "Received Frame from Cam %d - count %d, PTS of buffer %f, prestime %lld, ::delay %d::",
            cb_cam_pos, ctx.yuv_frame_count[cb_cam_pos], (float)cb_pts / 1000000000.0, pres_time, pres_time - prev_time[cb_cam_pos]);
    }
    prev_time[cb_cam_pos] = pres_time;

    analytics_frame_header_v2 msg_data;

    memset(&msg_data, 0x00, sizeof(msg_data));
    LOG_D(TAG, "size of msg_data: %d", sizeof(msg_data));

    msg_data.smb_id = smb_id;
    msg_data.uid = uid;
    msg_data.cam_id = cb_cam_pos;
    msg_data.frame_cnt = ctx.yuv_frame_count[cb_cam_pos];
    msg_data.timestamp = pres_time;
    msg_data.pts = cb_pts;
    msg_data.session_start_epoch_outward = ctx.firstframe_time[DEVICE_CAMERA_POSITION_FRONT];
    msg_data.session_start_epoch_inward = ctx.firstframe_time[DEVICE_CAMERA_POSITION_BACK];

    pthread_mutex_lock(&rt_gps_mutex);
    memcpy(&msg_data.gps_data, &ctx.saved_gps, sizeof(gps_data_t_v2));
    pthread_mutex_unlock(&rt_gps_mutex);

    pthread_mutex_lock(&rt_session_id_mutex);
    strncpy(msg_data.session_id, ctx.sessionid_rt[cb_cam_pos].c_str(), GSTREAMER_NAME_LENGTH_MAX);
    pthread_mutex_unlock(&rt_session_id_mutex);

    pthread_mutex_lock(&dis_mutex);
    strncpy(msg_data.dis, ctx.dis_uid_string.c_str(), 32);
    pthread_mutex_unlock(&dis_mutex);

    msg_data.dis_flag = ctx.dis_status;
    /* we want to generate alert card for inward as well in case of enhanced privacy */
    if (ctx.privacy_params.enhanced_privacy)
        msg_data.privacy = false;
    else {
        if (cb_cam_pos == DEVICE_CAMERA_POSITION_DMS)
            msg_data.privacy = ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK];
        else
            msg_data.privacy = ctx.fused_privacy && ctx.privacy_params.cam_privacy[cb_cam_pos];
    }
    msg_data.idle = ctx.idle_mode_RT_thread;
    msg_data.ir_led_status = ctx.ir_led_status;
    msg_data.nd_start_ts = (int64_t)pres_time;
    msg_data.nd_sent_ts = (int64_t)raw_frame_sent_time_ms;

    LOG_D(TAG, "cb_cam_pos %d session ID:%s", cb_cam_pos, msg_data.session_id);
    msg_data.session_id_len = strlen(msg_data.session_id);

    if ((cb_cam_pos == DEVICE_CAMERA_POSITION_BACK) && (ctx.is_driverlogin_qr_enabled == true)) {
        msg_data.qr_detect_decode_info = ctx.qr_code_scan_status;
    } else {
        msg_data.qr_detect_decode_info = 0;
    }

    int msg_send_status = send_msg_analytics_cam((char *)&msg_data, sizeof(analytics_frame_header_v2),
                                            ctx.zmq_publisher[cb_cam_pos], 0);

#ifdef testApp
    int zmq_result = zmq_send(shm_zmq_publisher, (char *)&msg_data, sizeof(analytics_frame_header_v2), 0);
    if (zmq_result == -1) {
        LOG_E(TAG, "FAILED to send message of size %d in zmq", sizeof(analytics_frame_header_v2));
    }
#endif

    if (!msg_send_status) {
        LOG_E(TAG, "Failed to send message to analytics for cam_pos: %d", cb_cam_pos);
    }

    ctx.yuv_frame_count[cb_cam_pos]++;
    return;
}

// V3 version of the callback
void appsink_camera_callback_v3(int64_t cb_pts, int cb_cam_pos, uint64_t raw_time_ns, uint64_t epoch_time_ns,
                             uint64_t raw_frame_sent_time_ms, int64_t data_size, int smb_id, int64_t uid)
{
    uint32_t rt_frame_status = 0;
    int64_t rt_frames_target_time = 0;

    if (cb_cam_pos == DEVICE_CAMERA_POSITION_FRONT) {
        rt_frames_target_time = ctx.send_rt_frames_till_time_outward;
    }
    else if (cb_cam_pos == DEVICE_CAMERA_POSITION_BACK || DEVICE_CAMERA_POSITION_DMS) {
        rt_frames_target_time = ctx.send_rt_frames_till_time;
    }

    if (get_system_monotonic_time() > rt_frames_target_time)
        rt_frame_status |= RT_FRAME_DROP_POST_IGNITION_OFF_TIME_EXPIRED;

    if (ctx.idle_mode_RT_thread == 1)
        rt_frame_status |= RT_FRAME_DROP_IDLE_MODE_ENABLED;

    if (ctx.geofence_privacy) {
        rt_frame_status |= RT_FRAME_DROP_GEO_FENCE_PRIVACY_ENABLED;
    } 
    else if (ctx.off_duty_privacy) {
        rt_frame_status |= RT_FRAME_DROP_OFF_DUTY_PRIVACY_ENABLED;
    } else if (!ctx.privacy_params.enhanced_privacy && ctx.fused_privacy) {
        int cam_pos = cb_cam_pos;

        if (cam_pos == DEVICE_CAMERA_POSITION_DMS)
            cam_pos = DEVICE_CAMERA_POSITION_BACK;

        if (ctx.privacy_params.cam_privacy[cam_pos])
            rt_frame_status |= RT_FRAME_DROP_FUSED_PRIVACY_ENABLED;
    }

    /* If supercap_status is true, power is disconnected and RT frames
     * should not be sent to analytics service.
     */
    if (ctx.supercap_status) {
        rt_frame_status |= RT_FRAME_DROP_SUPERCAP_ENABLED;
    }

    uint64_t pres_time = get_system_time();
    if (epoch_time_ns != 0) {
        pres_time = epoch_time_ns / 1000000;
    }
    int64_t delay = pres_time - prev_time[cb_cam_pos];
    if (delay <= 0) {
        pres_time = pres_time + (-delay) + 1;
        LOG_I(TAG, "road delay %lld", delay);
        LOG_I(TAG, "ROAD :@: Received Frame - count %d, PTS of buffer %f, prestime %llu ::delay %lld::",
                ctx.yuv_frame_count[cb_cam_pos], (float)cb_pts / 1000000000.0, pres_time, pres_time - prev_time[cb_cam_pos]);
    }
    if ((ctx.yuv_frame_count[cb_cam_pos] % cb_print_freq) == 0) {
        LOG_I(TAG, "Received Frame from Cam %d - count %d, PTS of buffer %f, prestime %llu, ::delay %lld::",
            cb_cam_pos, ctx.yuv_frame_count[cb_cam_pos], (float)cb_pts / 1000000000.0, pres_time, pres_time - prev_time[cb_cam_pos]);
    }
    prev_time[cb_cam_pos] = pres_time;

    if (rt_frame_status != 0) {
        // send drop indicator only for first frame of drop interval
        if (ctx.sessio_drop_message[cb_cam_pos] == 0) {
            // Provide ignition state also as part of RT frame status for dropped frames
            if (ctx.crank_level_RT_thread == CRANK_LOW) {
                rt_frame_status |= RT_FRAME_DROP_IGNITION_OFF_STATE; // CRANK_LOW flag in bit 27
            }
            LOG_I(TAG, "Dropping RT frame of camera: %d with rt_frame_status: %d, epoch_time_ms: %llu and pts: %f", cb_cam_pos, rt_frame_status, pres_time, (float)cb_pts / 1000000000.0);

            if (send_drop_session_msg_v3(cb_cam_pos, rt_frame_status, pres_time, cb_pts))
                ctx.sessio_drop_message[cb_cam_pos] = 1;
        }
        ctx.yuv_frame_count[cb_cam_pos]++;
        return;
    }

    if (ctx.sessio_drop_message[cb_cam_pos]) {
        LOG_I(TAG, "Reset drop indicator to zero to track multiple drop intervals within the same session.");
        ctx.sessio_drop_message[cb_cam_pos] = 0;
    }

    analytics_frame_header_v3 msg_data;
    memset(&msg_data, 0x00, sizeof(msg_data));

    LOG_D(TAG, "size of msg_data: %d", sizeof(msg_data));

    msg_data.smb_id = smb_id;
    msg_data.uid = uid;
    msg_data.cam_id = cb_cam_pos;
    msg_data.frame_cnt = ctx.yuv_frame_count[cb_cam_pos];
    msg_data.timestamp = pres_time;
    msg_data.pts = cb_pts;
    msg_data.session_start_epoch_outward = ctx.firstframe_time[DEVICE_CAMERA_POSITION_FRONT];
    msg_data.session_start_epoch_inward = ctx.firstframe_time[DEVICE_CAMERA_POSITION_BACK];

    pthread_mutex_lock(&rt_gps_mutex);
    memcpy(&msg_data.gps_data, &ctx.saved_gps, sizeof(gps_data_t_v3));
    pthread_mutex_unlock(&rt_gps_mutex);

    pthread_mutex_lock(&rt_session_id_mutex);
    strncpy(msg_data.session_id, ctx.sessionid_rt[cb_cam_pos].c_str(), GSTREAMER_NAME_LENGTH_MAX);
    pthread_mutex_unlock(&rt_session_id_mutex);

    pthread_mutex_lock(&dis_mutex);
    strncpy(msg_data.dis, ctx.dis_uid_string.c_str(), 32);
    pthread_mutex_unlock(&dis_mutex);

    msg_data.dis_flag = ctx.dis_status;

    /* we want to generate alert card for inward as well in case of enhanced privacy */
    if (ctx.privacy_params.enhanced_privacy)
        msg_data.privacy = false;
    else {
        if (cb_cam_pos == DEVICE_CAMERA_POSITION_DMS)
            msg_data.privacy = ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK];
        else
            msg_data.privacy = ctx.fused_privacy && ctx.privacy_params.cam_privacy[cb_cam_pos];
    }
    msg_data.idle = ctx.idle_mode_RT_thread;
    msg_data.ir_led_status = ctx.ir_led_status;
    msg_data.nd_start_ts = (int64_t)pres_time;
    msg_data.nd_sent_ts = (int64_t)raw_frame_sent_time_ms;
    msg_data.session_id_len = strlen(msg_data.session_id);
    msg_data.rt_frame_status = rt_frame_status;

    if ((cb_cam_pos == DEVICE_CAMERA_POSITION_BACK) && (ctx.is_driverlogin_qr_enabled == true)) {
        msg_data.qr_detect_decode_info = ctx.qr_code_scan_status;
    } else {
        msg_data.qr_detect_decode_info = 0;
    }
        
    LOG_D(TAG, "cb_cam_pos: %d session_id: %s", cb_cam_pos, msg_data.session_id);

    bool msg_send_status = send_msg_analytics_cam((char *)&msg_data, sizeof(analytics_frame_header_v3), ctx.zmq_publisher[cb_cam_pos], 0);
    if (msg_send_status == false) {
        LOG_E(TAG, "Failed to send message to analytics for cam_pos: %d, session_id: %s and epoch_time_ms: %llu", cb_cam_pos, msg_data.session_id, pres_time);
    }

#ifdef testApp
    int zmq_result = zmq_send(shm_zmq_publisher, (char *)&msg_data, sizeof(analytics_frame_header_v3), 0);
    if (zmq_result == -1) {
        LOG_E(TAG, "FAILED to send message of size %d in zmq", sizeof(analytics_frame_header_v3));
    }
#endif

    ctx.yuv_frame_count[cb_cam_pos]++;
    return;
}

// Wrapper function that calls the appropriate version
void appsink_camera_callback(int64_t cb_pts, int cb_cam_pos, uint64_t raw_time_ns, uint64_t epoch_time_ns,
                             uint64_t raw_frame_sent_time_ms, int64_t data_size, int smb_id, int64_t uid)
{
    if (rt_interface_version == 2) {
        appsink_camera_callback_v2(cb_pts, cb_cam_pos, raw_time_ns, epoch_time_ns,
                                   raw_frame_sent_time_ms, data_size, smb_id, uid);
    } else if (rt_interface_version == 3) {
        appsink_camera_callback_v3(cb_pts, cb_cam_pos, raw_time_ns, epoch_time_ns,
                                   raw_frame_sent_time_ms, data_size, smb_id, uid);
    }
}

#ifdef DMS_CAMERA_SUPPORTED
void appsink_dms_camera_callback(int64_t cb_pts, int cb_cam_pos, uint64_t raw_time_ns, uint64_t epoch_time_ns,
                             uint64_t raw_frame_sent_time_ms, int smb_id, int64_t uid, int pool_dmabuf_fd)
{
    uint32_t rt_frame_status = 0;
    int64_t rt_frames_target_time = 0;

    if (cb_cam_pos == DEVICE_CAMERA_POSITION_FRONT) {
        rt_frames_target_time = ctx.send_rt_frames_till_time_outward;
    }
    else if (cb_cam_pos == DEVICE_CAMERA_POSITION_BACK || DEVICE_CAMERA_POSITION_DMS) {
        rt_frames_target_time = ctx.send_rt_frames_till_time;
    }

    if (get_system_monotonic_time() > rt_frames_target_time)
        rt_frame_status |= RT_FRAME_DROP_POST_IGNITION_OFF_TIME_EXPIRED;

    if (ctx.idle_mode_RT_thread == 1)
        rt_frame_status |= RT_FRAME_DROP_IDLE_MODE_ENABLED;

    if (ctx.geofence_privacy) {
        rt_frame_status |= RT_FRAME_DROP_GEO_FENCE_PRIVACY_ENABLED;
    } 
    else if (ctx.off_duty_privacy) {
        rt_frame_status |= RT_FRAME_DROP_OFF_DUTY_PRIVACY_ENABLED;
    } else if (!ctx.privacy_params.enhanced_privacy && ctx.fused_privacy) {
        int cam_pos = cb_cam_pos;

        if (cam_pos == DEVICE_CAMERA_POSITION_DMS)
            cam_pos = DEVICE_CAMERA_POSITION_BACK;

        if (ctx.privacy_params.cam_privacy[cam_pos])
            rt_frame_status |= RT_FRAME_DROP_FUSED_PRIVACY_ENABLED;
    }

    /* If supercap_status is true, power is disconnected and RT frames
     * should not be sent to analytics service.
     */
    if (ctx.supercap_status) {
        rt_frame_status |= RT_FRAME_DROP_SUPERCAP_ENABLED;
    }

    uint64_t pres_time = get_system_time();
    if (epoch_time_ns != 0) {
        pres_time = epoch_time_ns / 1000000;
    }
    int64_t delay = pres_time - prev_time[cb_cam_pos];
    if (delay <= 0) {
        pres_time = pres_time + (-delay) + 1;
        LOG_I(TAG, "road delay %lld", delay);
        LOG_I(TAG, "ROAD :@: Received Frame - count %d, PTS of buffer %f, prestime %llu ::delay %lld::",
                ctx.yuv_frame_count[cb_cam_pos], (float)cb_pts / 1000000000.0, pres_time, pres_time - prev_time[cb_cam_pos]);
    }
    if ((ctx.yuv_frame_count[cb_cam_pos] % cb_print_freq) == 0) {
        LOG_I(TAG, "Received Frame from Cam %d - count %d, PTS of buffer %f, prestime %llu, ::delay %lld::",
            cb_cam_pos, ctx.yuv_frame_count[cb_cam_pos], (float)cb_pts / 1000000000.0, pres_time, pres_time - prev_time[cb_cam_pos]);
    }
    prev_time[cb_cam_pos] = pres_time;

    if (rt_frame_status != 0) {
        // send drop indicator only for first frame of drop interval
        if (ctx.sessio_drop_message[cb_cam_pos] == 0) {
            // Provide ignition state also as part of RT frame status for dropped frames
            if (ctx.crank_level_RT_thread == CRANK_LOW) {
                rt_frame_status |= RT_FRAME_DROP_IGNITION_OFF_STATE; // CRANK_LOW flag in bit 27
            }
            LOG_I(TAG, "Dropping RT frame of camera: %d with rt_frame_status: %d, epoch_time_ms: %llu and pts: %f", cb_cam_pos, rt_frame_status, pres_time, (float)cb_pts / 1000000000.0);
            
            if (send_drop_session_msg_v3(cb_cam_pos, rt_frame_status, pres_time, cb_pts, pool_dmabuf_fd))
                ctx.sessio_drop_message[cb_cam_pos] = 1;
        }
        ctx.yuv_frame_count[cb_cam_pos]++;
        return;
    }

    if (ctx.sessio_drop_message[cb_cam_pos]) {
        LOG_I(TAG, "Reset drop indicator to zero to track multiple drop intervals within the same session.");
        ctx.sessio_drop_message[cb_cam_pos] = 0;
    }

    analytics_frame_header_v3 msg_data;
    memset(&msg_data, 0x00, sizeof(msg_data));
    LOG_D(TAG, "size of msg_data: %d", sizeof(msg_data));
    msg_data.smb_id = smb_id;
    msg_data.uid = uid;
    msg_data.cam_id  = cb_cam_pos;
    msg_data.frame_cnt = ctx.yuv_frame_count[cb_cam_pos];
    msg_data.timestamp = pres_time;
    msg_data.pts = cb_pts;
    msg_data.session_start_epoch_outward = ctx.firstframe_time[DEVICE_CAMERA_POSITION_FRONT];
    msg_data.session_start_epoch_inward = ctx.firstframe_time[DEVICE_CAMERA_POSITION_BACK];

    pthread_mutex_lock(&rt_gps_mutex);
    memcpy(&msg_data.gps_data, &ctx.saved_gps, sizeof(gps_data_t_v3));
    pthread_mutex_unlock(&rt_gps_mutex);

    pthread_mutex_lock(&rt_session_id_mutex);
    strncpy(msg_data.session_id, ctx.sessionid_rt[cb_cam_pos].c_str(), GSTREAMER_NAME_LENGTH_MAX);
    pthread_mutex_unlock(&rt_session_id_mutex);

    pthread_mutex_lock(&dis_mutex);
    strncpy(msg_data.dis, ctx.dis_uid_string.c_str(), 32);
    pthread_mutex_unlock(&dis_mutex);

    msg_data.dis_flag = ctx.dis_status;
    /* we want to generate alert card for inward as well in case of enhanced privacy */
    if (ctx.privacy_params.enhanced_privacy)
        msg_data.privacy = false;
    else
        msg_data.privacy = ctx.fused_privacy && ctx.privacy_params.cam_privacy[DEVICE_CAMERA_POSITION_BACK];

    msg_data.idle = ctx.idle_mode_RT_thread;
    msg_data.ir_led_status = ctx.ir_led_status;
    msg_data.nd_start_ts = (int64_t)pres_time;
    msg_data.nd_sent_ts = (int64_t)raw_frame_sent_time_ms;
    msg_data.session_id_len = strlen(msg_data.session_id);

    //Special handling for DMABUF FD as DMS-SP10 and dev_6.14 branches were merged.
    NvBufferParamsEx paramsEx;
    NvBufferGetParamsEx(pool_dmabuf_fd, &paramsEx);
    memcpy(&msg_data.shm_buffer_params, &paramsEx, sizeof(NvBufferParamsEx));
    msg_data.rt_frame_status = rt_frame_status;

    if (bagheera_service_exiting == true) {
        LOG_I(TAG, "Not sending msg to analytics service since bagheera service is about to exit");
        return;
    }

    bool msg_send_status = send_msg_analytics_dms_cam((char *)&msg_data, sizeof(analytics_frame_header_v3), pool_dmabuf_fd);
    if (msg_send_status == false)
        LOG_E(TAG, "Failed to send message to analytics for cam_pos: %d", cb_cam_pos);

    ctx.yuv_frame_count[cb_cam_pos]++;

    return;
}
#endif

bool fill_rt_config_gps(string socket_name = gps_streaming_socket)
{
    // Flag to check if this is geo fence streaming setup or normal GPS streaming setup
    bool is_geo_fence = (socket_name == gps_geo_fence_streaming_socket);
    string rt_gps_socket = socket_name;

    // Geo fence is unconditionally enabled. Config is not involved.
    // For normal GPS streaming, read config to see if enabled.
    if (!is_geo_fence) {
        bool get_override_val = true;
        bool is_val_overridden = false;
        Config_parser nd_conf_analytics(ND_CONFIG_ANALYTICS);
        get_override_val = true;
        is_val_overridden = false;
        string rt_flag_v2 = nd_conf_analytics.getConfig("device_overspeed_v2", "enabled", "0", get_override_val, is_val_overridden);
        ctx.enable_rt_gps = (rt_flag_v2 == "1");
        LOG_I(TAG, "rt_flag_v2: %s, ctx.enable_rt_gps : %d, is_val_overridden: %d", rt_flag_v2.c_str(), ctx.enable_rt_gps, is_val_overridden);
        LOG_I(TAG, "rt_gps_socket %s", rt_gps_socket.c_str());
        if(rt_gps_socket == "") {
            ctx.enable_rt_gps = false;
            LOG_E(TAG, "Error in reading socket for RT inertial streaming disabling RT inertials");
        }

        if( ctx.enable_rt_gps != true ) {
            LOG_E(TAG, "ctx.enable_rt_gps != true ; returning false from fill_rt_config_gps");
            return false;
        }
    }

    if (is_geo_fence) {
        LOG_I(TAG, "rt_gps_socket_geo_fence %s", rt_gps_socket.c_str());
        if(rt_gps_socket == "") {
            ctx.enable_rt_gps_geo_fence = false;
            LOG_E(TAG, "Error in reading socket for RT geo fence inertial streaming disabling RT inertial for geo fence");
            return false;
        }
    }

    // ZMQ setup for both geo fence and normal GPS streaming
    void** zmq_context_ptr = is_geo_fence ? &ctx.zmq_context_gps_geo_fence : &ctx.zmq_context_gps;
    void** zmq_publisher_ptr = is_geo_fence ? &ctx.zmq_publisher_gps_geo_fence : &ctx.zmq_publisher_gps;

    if ((*zmq_publisher_ptr == NULL) && (*zmq_context_ptr == NULL))
    {
        *zmq_context_ptr = zmq_ctx_new ();
        if( *zmq_context_ptr == NULL ){
            LOG_E(TAG, "%s == NULL; errno: %d, zmq_error:   %s",
                  is_geo_fence ? "ctx.zmq_context_gps_geo_fence" : "ctx.zmq_context_gps",
                  errno, zmq_strerror(zmq_errno()));
            return false;
        }
        *zmq_publisher_ptr = zmq_socket (*zmq_context_ptr, ZMQ_PUB);
        if( *zmq_publisher_ptr == NULL ){
            LOG_E(TAG, "%s == NULL; errno: %d, zmq_error:   %s",
                  is_geo_fence ? "ctx.zmq_publisher_gps_geo_fence" : "ctx.zmq_publisher_gps",
                  errno, zmq_strerror(zmq_errno()));
            return false;
        }

        int max_q_sz = MAX_PUBLISHER_Q_SZ;
        int linger_val = 100 ; // milisecond
        if( zmq_setsockopt(*zmq_publisher_ptr, ZMQ_SNDHWM, &max_q_sz, sizeof(int)) ) {
            LOG_E(TAG, "zmq_setsockopt failed; errno: %d, zmq_error:   %s", errno, zmq_strerror(zmq_errno()));
            return false;
        }
        if( zmq_setsockopt (*zmq_publisher_ptr, ZMQ_LINGER, &linger_val, sizeof(int)) ) {
            LOG_E(TAG, "zmq_setsockopt ZMQ_LINGER failed; errno: %d, zmq_error:   %s", errno, zmq_strerror(zmq_errno()));
            return false;
        }

        int trail_count = 0, rc = -1;
        do {
            trail_count++;
            usleep(100*1000); // sleep for 100MS before binding : can this avoid FAILED TO CREATE with err 98 ?
            rc = zmq_bind (*zmq_publisher_ptr, rt_gps_socket.c_str());
            if(rc != 0) {
                LOG_E(TAG, "FAILED TO CREATE zmq_socket rc: %d , errno: %d,   zmq_error no:   %s",
                            rc, errno, zmq_strerror(zmq_errno()));
                if (is_geo_fence) {
                    ctx.enable_rt_gps_geo_fence = false;
                } else {
                    ctx.enable_rt_gps = false;
                }
            }
            else {
                LOG_I(TAG, "SUCCESS IN CREATE zmq_socket");
                if (is_geo_fence) {
                    ctx.enable_rt_gps_geo_fence = true;
                } else {
                    ctx.enable_rt_gps = true;
                }
                break;
            }
        } while( trail_count < 5 );

        if (strncmp(&rt_gps_socket[0u], "ipc://", sizeof("ipc://") - 1) == 0)
        {
            LOG_I(TAG, "chmod for file %s" , (&rt_gps_socket[0u] + sizeof("ipc://") - 1));
            if(chmod((&rt_gps_socket[0u] + sizeof("ipc://") - 1) , 0777) < 0)
            {
                LOG_E(TAG, "Could not create socket permissions %s", strerror(errno));
            }
        }

        if( rc != 0 ) {
            LOG_C(TAG, "after %d re trails FAILED TO CREATE zmq_socket rc: %d ", trail_count, rc);

            zmq_close (*zmq_publisher_ptr);
            zmq_ctx_destroy (*zmq_context_ptr);
            *zmq_publisher_ptr = NULL;
            *zmq_context_ptr = NULL;

            return false;
        }
    }
    LOG_I(TAG, "exiting fill_rt_config_gps () for %s", is_geo_fence ? "geo fence RT setup" : "GPS RT setup");
    return true;
}


bool fill_rt_config_imu()
{
    bool override_val = true, val_override = false;
    string rt_flag = ctx.bagheera_config->getConfig("streaming", "inertial_streaming", "true");
    string rt_inertial_socket = inertial_streaming_socket;
    ctx.enable_rt_inertial = (rt_flag == "true");
    if(rt_inertial_socket == "") {
        ctx.enable_rt_inertial = false;
        LOG_E(TAG, "Error in reading socket for RT inertial streaming disabling RT inertials");
    }

    LOG_I(TAG, "enable_rt_inertial %d", ctx.enable_rt_inertial );
    LOG_I(TAG, "rt_inertial_socket %s", rt_inertial_socket.c_str());

    if( ctx.enable_rt_inertial != true ) {
        LOG_E(TAG, "ctx.enable_rt_inertial != true ; returning flase from fill_rt_config_imu");
        return false;
    }

    if ((ctx.zmq_publisher_imu == NULL) && (ctx.zmq_context_imu == NULL))
    {
        ctx.zmq_context_imu = zmq_ctx_new ();
        if( ctx.zmq_context_imu == NULL ){
            LOG_E(TAG, "ctx.zmq_context_imu == NULL; errno: %d, zmq_error:   %s", errno, zmq_strerror(zmq_errno()));
            return false;
        }
        ctx.zmq_publisher_imu = zmq_socket (ctx.zmq_context_imu, ZMQ_PUB);
        if( ctx.zmq_publisher_imu == NULL ){
            LOG_E(TAG, "ctx.zmq_publisher_imu == NULL; errno: %d, zmq_error:   %s", errno, zmq_strerror(zmq_errno()));
            return false;
        }

        int max_q_sz = MAX_PUBLISHER_Q_SZ;
        int linger_val = 100 ; // milisecond
        if( zmq_setsockopt(ctx.zmq_publisher_imu, ZMQ_SNDHWM, &max_q_sz, sizeof(int)) ) {
            LOG_E(TAG, "zmq_setsockopt failed; errno: %d, zmq_error:   %s", errno, zmq_strerror(zmq_errno()));
            return false;
        }
        if( zmq_setsockopt (ctx.zmq_publisher_imu, ZMQ_LINGER, &linger_val, sizeof(int)) ) {
            LOG_E(TAG, "zmq_setsockopt ZMQ_LINGER failed; errno: %d, zmq_error:   %s", errno, zmq_strerror(zmq_errno()));
            return false;
        }

        int trail_count = 0, rc = -1;
        do {
            trail_count++;
            usleep(100*1000); // sleep for 100MS before binding : can this avoid FAILED TO CREATE with err 98 ?
            rc = zmq_bind (ctx.zmq_publisher_imu, rt_inertial_socket.c_str());
            if(rc != 0) {
                LOG_E(TAG, "FAILED TO CREATE zmq_socket rc: %d , errno: %d,   zmq_error no:   %s",
                            rc, errno, zmq_strerror(zmq_errno()));
                ctx.enable_rt_inertial = false;
#if ZMQ_TCP
                string portnotmp = rt_inertial_socket;

                string key_field = "tcp://*:";
                int index = portnotmp.find(key_field, 0);
                if(index != std::string::npos) {
                    index = index + key_field.size();
                    portnotmp = portnotmp.substr(index, portnotmp.size()-index );
                    LOG_I(TAG, "portnotmp %s", portnotmp.c_str());
                    string cmdtmp = "lsof -i:"+portnotmp;
                    execute_cmd (cmdtmp, "ZMQ_ERROR");
                }
#endif
            }
            else {
                LOG_I(TAG, "SUCCESS IN CREATE zmq_socket");
                ctx.enable_rt_inertial = true;
                break;
            }
        } while( trail_count < 5 );

        if (strncmp(&rt_inertial_socket[0u], "ipc://", sizeof("ipc://") - 1) == 0)
        {
            LOG_I(TAG, "chmod for file %s" , (&rt_inertial_socket[0u] + sizeof("ipc://") - 1));
            if(chmod((&rt_inertial_socket[0u] + sizeof("ipc://") - 1) , 0777) < 0)
            {
                LOG_E(TAG, "Could not create socket permissions %s", strerror(errno));
            }
        }

        if( rc != 0 ) {
            LOG_C(TAG, "after %d re trails FAILED TO CREATE zmq_socket rc: %d ", trail_count, rc);

            zmq_close (ctx.zmq_publisher_imu);
            zmq_ctx_destroy (ctx.zmq_context_imu);
            ctx.zmq_publisher_imu = NULL;
            ctx.zmq_context_imu = NULL;

            return false;
        }
    }
    return true;
}

const uint32_t g_timeout_value = 70*1000;
const uint32_t g_rcvhwm_value = 16;

void *check_inward_cam_RT_thread(void *)
{
    NDDeviceTypeT type = ND_DeviceFactory::getBuildDeviceType();
    if((NDDeviceTypeT::bagheera2 == type) || (NDDeviceTypeT::bagheera3 == type)) {
        int n_cpus = get_nprocs();
        int n_cpus_conf = get_nprocs_conf();
        LOG_I(TAG,"n_cpus : %d , n_cpus_conf : %d", n_cpus, n_cpus_conf);
        if(n_cpus == n_cpus_conf) {
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            vector<int> cores_to_be_set = {CPU_CORE_0};
            for(int i = 0; i < cores_to_be_set.size(); i++){
                CPU_SET(cores_to_be_set[i], &cpuset);
            }
            pid_t threadID = syscall(SYS_gettid);
            sched_setaffinity(threadID, sizeof(cpuset), &cpuset);
            cpu_set_t get_cpuset;
            CPU_ZERO(&get_cpuset);
            vector<int> confirm_set;
            sched_getaffinity(threadID, sizeof(get_cpuset), &get_cpuset);
            for (int i = 0; i < CPU_SETSIZE; i++) {
                if (CPU_ISSET(i, &get_cpuset)) {
                    confirm_set.push_back(i);
                    LOG_I(TAG, "CPU set for current thread: %d", i);
                }
            }
            if(cores_to_be_set != confirm_set){
                NDService::get_service_obj(TAG)->send_err_msg(SM_E_CPU_CORE_ERROR, DEVICE_CAMERA_POSITION_BACK, "Failed to set affinity for inward_cam_RT_thread");
            }
        }else {
            string err_msg = "Cores available don't match cores online. n_cpus : " + to_string(n_cpus) + " n_cpus_conf : " + to_string(n_cpus_conf);
            NDService::get_service_obj(TAG)->send_err_msg(SM_E_CPU_CORE_ERROR, DEVICE_CAMERA_POSITION_BACK, err_msg);

        }
    }
    uint64_t buf_pts = 0;
    int nBytes = 0;
    int64_t buf_size = (ctx.rt_config[DEVICE_CAMERA_POSITION_BACK].width * ctx.rt_config[DEVICE_CAMERA_POSITION_BACK].height * 3) / 2;
    int smb_id;
    int64_t uid;

    cam_RT_metadata incam_rt_metadata;

    while (1) {

        ctx.stop_inward_cam_zmqsub = false;

        while (ctx.is_camrec_zmqpub_created == false) {
            LOG_I(TAG, "Waiting for ZMQ publisher to be created by cam_rec service for publishing inward camera RT data");
            usleep(100000);
        }

        zmq_context = zmq_ctx_new ();
        if (zmq_context == NULL) {
            LOG_E(TAG, "zmq_context == NULL, zmq_error: %s", zmq_strerror(zmq_errno()));
            return NULL;
        }

        zmq_subscriber = zmq_socket (zmq_context, ZMQ_SUB);
        if (zmq_subscriber == NULL) {
            LOG_E(TAG, "zmq_subscriber == NULL, zmq_error: %s", zmq_strerror(zmq_errno()));
            return NULL;
        }

        int rc = zmq_connect(zmq_subscriber, ZMQ_SOCKET_IN_RT);
        if (rc == 0) {
            LOG_I(TAG, "Successfully connect the socket to ZMQ_SOCKET_IN_RT");
            zmq_setsockopt(zmq_subscriber, ZMQ_RCVTIMEO, &g_timeout_value, sizeof(uint32_t));
            zmq_setsockopt(zmq_subscriber, ZMQ_RCVHWM, &g_rcvhwm_value, sizeof(uint32_t));
            zmq_setsockopt(zmq_subscriber, ZMQ_SUBSCRIBE, "", 0);
        } else {
            LOG_I(TAG, "Failed to connect the socket to ZMQ_SOCKET_IN_RT, zmq_error: %s", zmq_strerror(zmq_errno()));
            return NULL;
        }

        while (1) {
            if ((ctx.stop_inward_cam_zmqsub == true) || (ctx.is_camrec_zmqpub_created == false)) {

                LOG_I(TAG, "Stop receiving inward camera RT data over zmq socket and close the corresponding subscriber");
                zmq_close (zmq_subscriber);
                zmq_ctx_destroy (zmq_context);

                zmq_subscriber = NULL;
                zmq_context = NULL;

                break;
            }

            nBytes = zmq_recv(zmq_subscriber, &incam_rt_metadata, sizeof(cam_RT_metadata), 0);
            if (nBytes == 0) {
                zmq_subscriber = zmq_socket (zmq_context, ZMQ_SUB);

                zmq_setsockopt(zmq_subscriber, ZMQ_RCVTIMEO, &g_timeout_value, sizeof(uint32_t));
                zmq_setsockopt(zmq_subscriber, ZMQ_RCVHWM, &g_rcvhwm_value, sizeof(uint32_t));
                zmq_setsockopt(zmq_subscriber, ZMQ_SUBSCRIBE, "", 0);

                zmq_connect(zmq_subscriber, ZMQ_SOCKET_IN_RT);

                continue;
            }
            buf_pts = incam_rt_metadata.pts;
            uid = incam_rt_metadata.uid;
            smb_id = incam_rt_metadata.smb_id;

            //LOG_I(TAG, "buf_pts: %llu, uid: %lld, smb_id: %d", buf_pts, uid, smb_id);

            uint64_t epoch_time_ns = get_system_time() * 1000 * 1000;
            uint64_t raw_frame_sent_time_ms = get_system_time();
            appsink_camera_callback(buf_pts, DEVICE_CAMERA_POSITION_BACK, 0, epoch_time_ns, raw_frame_sent_time_ms, buf_size, smb_id, uid);
        }
    }
}

#ifdef DMS_CAMERA_SUPPORTED
void send_fd_to_analytics(int recv_dmabuf_fd, dmscam_RT_metadata dms_rt_meta)
{
    int imp_dmabuf_fd;
    int pool_dmabuf_fd;
    int smb_id = -1;
    int64_t uid = -1;

    struct pollfd fds[1];
    fds[0].fd = cfd;
    fds[0].events = POLLOUT;
    // printf("Before poll, fds[0].revents: %d\n", fds[0].revents);
    int ret = poll(fds, 1, 0);  // Poll with zero timeout
    if (ret < 0) {
        LOG_E(TAG, "poll api call failed with error: %s", strerror(errno));
        handle_analytics_disconnection(cfd);
        return;
    }
    //  printf("After poll, fds[0].revents: %d\n", fds[0].revents);

    if (fds[0].revents & POLLERR) {
        // Error on the socket, such as client crash or unexpected disconnection
        LOG_I(TAG, "Error on client socket fd %d", cfd);
        handle_analytics_disconnection(cfd);
        return;
    } else if (fds[0].revents & POLLHUP) {
        // The file descriptor has been closed (hung up)
        LOG_I(TAG, "client socket fd %d has been hung up (closed)", cfd);
        handle_analytics_disconnection(cfd);
        return;
    }

    /* imp_dmabuf_fd corresponding to NV buffer created by importing
     * the recv_dmabuf_fd received from cam_rec service over the socket.
     */
    NvBufferImportFd(recv_dmabuf_fd, &imp_dmabuf_fd, &(dms_rt_meta.paramsEx));

    /* dmabuf_fd to be pulled from the the buffer pool and contents of imp_dmabuf_fd
     * will be copied to pool_dmabuf_fd, which is then shared with analytics service.
     */
    shm_writer_dms->get_free_smb_dms(smb_id, uid, pool_dmabuf_fd, imp_dmabuf_fd);

    /* Destroy the imported dma buffer */
    NvBufferDestroy(imp_dmabuf_fd);

    uint64_t epoch_time_ns = get_system_time() * 1000 * 1000;
    uint64_t raw_frame_sent_time_ms = get_system_time();
    appsink_dms_camera_callback(dms_rt_meta.pts, DEVICE_CAMERA_POSITION_DMS, 0, epoch_time_ns, raw_frame_sent_time_ms, smb_id, uid, pool_dmabuf_fd);

    return;
}

#ifdef ENABLE_DMS_RT_YUV_DUMP
void dump_dmabuf(int dmabuf_fd, int width, int height)
{
    int luma_size = width * height;
    int chroma_size = (width * height) / 2;

    // Create an EGL display connection
    EGLDisplay eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(eglDisplay, NULL, NULL);

    // Create an EGL image from the DMA-BUF FD using NvEGLImageFromFd
    EGLImageKHR eglImage = NvEGLImageFromFd(eglDisplay, dmabuf_fd);

    // Create a CUDA EGL resource from the EGL image
    cudaGraphicsResource_t cudaResource;
    CUDA_CHECK(cudaGraphicsEGLRegisterImage(&cudaResource, eglImage, cudaGraphicsRegisterFlagsReadOnly));

    // Retrieve the mapped EGL frame using cuGraphicsResourceGetMappedEglFrame
    CUeglFrame eglFrame;
    CU_CHECK(cuGraphicsResourceGetMappedEglFrame(&eglFrame, (CUgraphicsResource)cudaResource, 0, 0));

    // Verification: Copy data back from CUDA device memory to host and write to a file
    unsigned char *verify_host_y = (unsigned char *)malloc(luma_size);
    unsigned char *verify_host_uv = (unsigned char *)malloc(chroma_size);

    // Check the frame type and copy data accordingly
    if (eglFrame.frameType == CU_EGL_FRAME_TYPE_ARRAY) {
        // The frame uses CUDA arrays, so use cudaMemcpyFromArray
        CUDA_CHECK(cudaMemcpyFromArray(verify_host_y, (cudaArray_const_t)eglFrame.frame.pArray[0], 0, 0, luma_size, cudaMemcpyDeviceToHost));
        CUDA_CHECK(cudaMemcpyFromArray(verify_host_uv, (cudaArray_const_t)eglFrame.frame.pArray[1], 0, 0, chroma_size, cudaMemcpyDeviceToHost));
    } else if (eglFrame.frameType == CU_EGL_FRAME_TYPE_PITCH) {
        // The frame uses pitch-linear memory, so use cudaMemcpy
        CUDA_CHECK(cudaMemcpy(verify_host_y, eglFrame.frame.pPitch[0], luma_size, cudaMemcpyDeviceToHost));
        CUDA_CHECK(cudaMemcpy(verify_host_uv, eglFrame.frame.pPitch[1], chroma_size, cudaMemcpyDeviceToHost));
    }

    // Write yuv data to a file
    FILE *verify_file = fopen("/media/data/nd_sdcard/dmscam_rt_dump.yuv", "ab");
    if (verify_file) {
        fwrite(verify_host_y, sizeof(unsigned char), luma_size, verify_file);
        fwrite(verify_host_uv, sizeof(unsigned char), chroma_size, verify_file);
        fclose(verify_file);
    } else {
        printf("Failed to open the file for dumping dma buffers\n");
    }

    // Free host memory
    free(verify_host_y);
    free(verify_host_uv);

    // Unmap the CUDA graphics resource
    cuGraphicsUnmapResources(1, &cudaResource, 0);

    // Unregister the CUDA graphics resource
    cudaGraphicsUnregisterResource(cudaResource);

    // Destroy the EGL image using NvDestroyEGLImage
    NvDestroyEGLImage(eglDisplay, eglImage);

    eglTerminate(eglDisplay);
}
#endif

/* Thread to receive DMABUF FD from cam_rec service over the socket.
 * Once received, DMABUF FD is sent to analytics service through
 * a different socket.
 */
void *rcv_dmabuf_fd_thread(void *)
{
    if((nullptr != ND_DeviceFactory::Create_NDDevice()) && (eBagheera_3 == ND_DeviceFactory::Create_NDDevice()->getNDDeviceType())) {
        int n_cpus = get_nprocs();
        int n_cpus_conf = get_nprocs_conf();
        LOG_I(TAG,"n_cpus : %d , n_cpus_conf : %d", n_cpus, n_cpus_conf);
        if(n_cpus == n_cpus_conf) {
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            vector<int> cores_to_be_set = {CPU_CORE_0};
            for(int i = 0; i < cores_to_be_set.size(); i++){
                CPU_SET(cores_to_be_set[i], &cpuset);
            }
            pid_t threadID = syscall(SYS_gettid);
            sched_setaffinity(threadID, sizeof(cpuset), &cpuset);
            cpu_set_t get_cpuset;
            CPU_ZERO(&get_cpuset);
            vector<int> confirm_set;
            sched_getaffinity(threadID, sizeof(get_cpuset), &get_cpuset);
            for (int i = 0; i < CPU_SETSIZE; i++) {
                if (CPU_ISSET(i, &get_cpuset)) {
                    confirm_set.push_back(i);
                    LOG_I(TAG, "CPU set for current thread: %d", i);
                }
            }
            if(cores_to_be_set != confirm_set){
                NDService::get_service_obj(TAG)->send_err_msg(SM_E_CPU_CORE_ERROR, DEVICE_CAMERA_POSITION_DMS, "Failed to set affinity for dms_cam_RT_thread");
            }
        }else {
            string err_msg = "Cores available don't match cores online. n_cpus : " + to_string(n_cpus) + " n_cpus_conf : " + to_string(n_cpus_conf);
            NDService::get_service_obj(TAG)->send_err_msg(SM_E_CPU_CORE_ERROR, DEVICE_CAMERA_POSITION_DMS, err_msg);
        }
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, CAMREC_BAGHEERA_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    int client_fd = socket(AF_UNIX, SOCK_STREAM, 0);

    while (1) {
        // connect to the server
        int ret = connect(client_fd, (struct sockaddr *)&addr, sizeof(addr));
        if (ret < 0) {
            LOG_D(TAG, "connect api call failed with error: %s, sleep for 1s and then reconnect", strerror(errno));
            sleep(1); // server is not yet up, sleep for 1s and then reconnect
            continue;
        }
        LOG_I(TAG, "bagheera service with client_fd %d connected to cam_rec service", client_fd);

        // Set client socket to non-blocking mode
        set_socket_non_blocking(client_fd);

        // Buffer to receive the control message
        char cmsgbuf[CMSG_SPACE(sizeof(int))];
        struct msghdr msg;
        dmscam_RT_metadata dms_rt_meta;
        int dmabuf_fd;
        while (1) {
            pthread_mutex_lock(&dms_shm_writer_mutex);
            if (pause_dms_rt_processing == true) {
                LOG_I(TAG, "Pausing DMS RT processing as shared memory will be recreated on account of analytics service restart");
                pthread_cond_wait(&dms_shm_writer_cond, &dms_shm_writer_mutex);
            }
            pthread_mutex_unlock(&dms_shm_writer_mutex);

            struct iovec io = {
                .iov_base = &dms_rt_meta,
                .iov_len = sizeof(dmscam_RT_metadata)
            };
            memset(cmsgbuf, '\0', sizeof(cmsgbuf));

            memset(&msg, 0, sizeof(msg));
            msg.msg_iov = &io;
            msg.msg_iovlen = 1;
            msg.msg_control = cmsgbuf;
            msg.msg_controllen = sizeof(cmsgbuf);

            // Poll setup for client
            struct pollfd fds[1];
            fds[0].fd = client_fd;
            fds[0].events = POLLIN;

            //printf("Before poll, fds[0].revents: %d\n", fds[0].revents);
            int ret = poll(fds, 1, -1);  // Block indefinitely on poll
            if (ret < 0) {
                LOG_E(TAG, "poll api call failed with error: %s, closing client_fd %d", strerror(errno), client_fd);
                close(client_fd);
                client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
                break;
            }
            //printf("After poll, fds[0].revents: %d\n", fds[0].revents);

            if (fds[0].revents & POLLHUP) {
                LOG_E(TAG, "bagheera service with client_fd %d is no longer connected to cam_rec service, probably cam_rec service has crashed", client_fd);
                close(client_fd);
                client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
                break;
            } else if (fds[0].revents & POLLIN) {
                // Receive the message
                if (recvmsg(client_fd, &msg, 0) <= 0) {
                    LOG_E(TAG, "recvmsg api call failed with error: %s, closing client_fd %d", strerror(errno), client_fd);
                    close(client_fd);
                    client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
                    break;
                }
                struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
                if (cmsg == NULL) {
                    LOG_E(TAG, "No control message received from cam_rec service, look for next message");
                    continue;
                }
                memcpy(&dmabuf_fd, (int *) CMSG_DATA(cmsg), sizeof(int));

#ifdef ENABLE_DMS_RT_YUV_DUMP
                int imp_dmabuf_fd;
                int pool_dmabuf_fd;
                int smb_id = -1;
                int64_t uid = -1;

                NvBufferImportFd(dmabuf_fd, &imp_dmabuf_fd, &(dms_rt_meta.paramsEx));
            //    shm_writer_dms->get_free_smb_dms(smb_id, uid, pool_dmabuf_fd, imp_dmabuf_fd);
                int frame_width = dms_rt_meta.paramsEx.params.width[0];
                int frame_height = dms_rt_meta.paramsEx.params.height[0];
                dump_dmabuf(imp_dmabuf_fd, frame_width, frame_height);
                //dump_dmabuf(pool_dmabuf_fd, frame_width, frame_height);
                NvBufferDestroy(imp_dmabuf_fd);
#endif
                // Pass dmabuf_fd to analytics process
                if (is_analytics_connected ==  true)
                    send_fd_to_analytics(dmabuf_fd, dms_rt_meta);
                else
                    LOG_D(TAG, "Dropping DMS RT frames since analytics is not connected through socket");

                close(dmabuf_fd);
            }
        }
    }
}

bool copy_dma_buffer(int src_dmabuf_fd, int dst_dmabuf_fd)
{
    NvBufferTransformParams trans_params;
    memset(&trans_params, 0, sizeof(trans_params));
    trans_params.transform_flag = NVBUFFER_TRANSFORM_FILTER;
    trans_params.transform_filter = NvBufferTransform_Filter_Nearest;

    // Perform the copy using NvBufferTransform
    int ret = NvBufferTransform(src_dmabuf_fd, dst_dmabuf_fd, &trans_params);
    if (ret != 0) {
        LOG_E(TAG, "NvBufferTransform failed: %d", ret);
        return false;
    }
    return true;
}

// Destroy the hardware buffer allocated in the form of DMA buffer FD
bool destroy_dmabuf_fd(void *data_ptr)
{
    int dmabuf_fd = *(int *)data_ptr;

    int ret = NvBufferDestroy(dmabuf_fd);
    if (ret == -1) {
        LOG_I(TAG, "Failed to destroy the hardware buffer with dmabuf_fd %d", dmabuf_fd);
        return false;
    }
    LOG_I(TAG, "dmabuf_fd %d destroyed", dmabuf_fd);

    return true;
}

/* Allocate a pool of hardware buffers and write the corresponding
 * DMABUF FDs in the created shared memory.
 */
void create_dmscam_rt_shared_memory(realtime_camera_config_t* rt_config)
{
    NvBufferCreateParams create_params;
    int32_t dmabuf_fd;
    stringstream cam_name;
    cam_name << "CAM" << DEVICE_CAMERA_POSITION_DMS;
    string name = cam_name.str();
    int smb_data_size = sizeof(dmabuf_fd);

    shm_writer_dms = new NdSharedMemoryWriter(name, smb_data_size, shm_buffers_dms);
    if (shm_writer_dms) {
        LOG_I(TAG, "Shared Memory Writer 0x%x created successfully for DMS RT pipeline with smb_data_size: %d and shm_buffers: %d", shm_writer_dms, smb_data_size, shm_buffers_dms);

        shm_writer_dms->set_log_frequency(100);
        shm_writer_dms->register_dms_write_callback(copy_dma_buffer);
        shm_writer_dms->register_dms_clear_callback(destroy_dmabuf_fd);
    }

    LOG_I(TAG, "Allocating a pool of hardware buffers and writing the corresponding dmabuf_fds in the shared memory");
    for (int i = 0; i < shm_buffers_dms; i++) {
        memset(&create_params, 0, sizeof(NvBufferCreateParams));

        create_params.width = rt_config->width;
        create_params.height = rt_config->height;
        create_params.payloadType = NvBufferPayload_SurfArray;
        create_params.layout = NvBufferLayout_Pitch;
        create_params.colorFormat = NvBufferColorFormat_NV12;

        // Allocating a hardware buffer
        NvBufferCreateEx(&dmabuf_fd, &create_params);
        
        // writing dmabuf fd to shared memory
        shm_writer_dms->write_data_to_specific_smb(i, smb_data_size, &dmabuf_fd);

        LOG_I(TAG, "dmabuf_fd %d added to shared memory", dmabuf_fd);
    }
}

/* Reallocate a pool of hardware buffers and write the corresponding
 * DMABUF FDs in the created shared memory.
 */
void recreate_dmscam_rt_shared_memory(realtime_camera_config_t* rt_config)
{
    pthread_mutex_lock(&dms_shm_writer_mutex);

    pause_dms_rt_processing = true;

    LOG_I(TAG, "Deleting older pool of DMA buffers");
    shm_writer_dms->clear_data_from_smbs();

    LOG_I(TAG, "SHM_DEBUG :: Deleting older instance of DMS camera shm_writer 0x%x", shm_writer_dms);
    delete shm_writer_dms;
    shm_writer_dms = NULL;

    int smb_data_size = sizeof(int);
    stringstream cam_name;
    cam_name << "CAM" << DEVICE_CAMERA_POSITION_DMS;
    string name = cam_name.str();

    LOG_I(TAG, "Reallocating a pool of hardware buffers and writing the corresponding dmabuf_fds in the shared memory");
    shm_writer_dms = new NdSharedMemoryWriter(name, smb_data_size, shm_buffers_dms);
    if (shm_writer_dms) {
        LOG_I(TAG, "Shared Memory Writer 0x%x recreated successfully for DMS RT pipeline with smb_data_size: %d and shm_buffers: %d", shm_writer_dms, smb_data_size, shm_buffers_dms);

        shm_writer_dms->set_log_frequency(100);
        shm_writer_dms->register_dms_write_callback(copy_dma_buffer);
        shm_writer_dms->register_dms_clear_callback(destroy_dmabuf_fd);

        nd_service_obj->send_err_msg(SM_E_NDC_CAM_SHM_RECREATED, DEVICE_CAMERA_POSITION_DMS, "RT shared memory recreated for DMS camera");
    }

    NvBufferCreateParams create_params;
    int dmabuf_fd;
    for (int i = 0; i < shm_buffers_dms; i++) {
        memset(&create_params, 0, sizeof(NvBufferCreateParams));

        create_params.width = rt_config->width;
        create_params.height = rt_config->height;
        create_params.payloadType = NvBufferPayload_SurfArray;
        create_params.layout = NvBufferLayout_Pitch;
        create_params.colorFormat = NvBufferColorFormat_NV12;

        // Allocating a hardware buffer
        NvBufferCreateEx(&dmabuf_fd, &create_params);
        
        // writing dmabuf fd to shared memory
        shm_writer_dms->write_data_to_specific_smb(i, smb_data_size, &dmabuf_fd);

        LOG_I(TAG, "dmabuf_fd %d added to recreated shared memory", dmabuf_fd);

        pause_dms_rt_processing = false;

        pthread_cond_signal(&dms_shm_writer_cond);
        pthread_mutex_unlock(&dms_shm_writer_mutex);
    }
}
#endif
