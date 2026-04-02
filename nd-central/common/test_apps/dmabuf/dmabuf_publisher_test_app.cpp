#include <sys/types.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <gst/gst.h>
#include <nvbuf_utils.h>
#include <assert.h>
#include "gst/app/gstappsink.h"
#include <map>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <vector>
#include <algorithm>
#include <string>
#include <fcntl.h>
#include <poll.h>

using namespace std;

#define ANALYTICS_SOCKET_PATH "/tmp/fd-share-analytics.socket"
#define SESSION_ID_MAX_LENGTH 256

struct gps_data_t_v3 {
    bool    valid;
    double  latitude;
    double  longitude;
    double  altitude;
    float   speed;
    float   bearing;
    float   accuracy; //currently not supported
    int64_t timestamp;
    int64_t system_timestamp;

    int     flags; //currently not used

    bool    privacy_enabled;
    int64_t reserved[4];
};

typedef struct {
    int        cam_id;
    int        frame_cnt;
    uint64_t   timestamp;
    uint64_t   pts;
    int        session_id_len;
    gps_data_t_v3 gps_data;
    char       session_id[SESSION_ID_MAX_LENGTH];
    char       dis[32];
    int        dis_flag;
    int        privacy;
    int        idle;
    int        ir_led_status;
    uint64_t   dts;
    int        smb_id;
    int64_t    uid;
    int64_t    session_start_epoch_outward;
    int64_t    session_start_epoch_inward;
    int64_t    nd_start_ts;
    int64_t    nd_sent_ts;
    int64_t    rt_frame_status;
    int8_t     shm_buffer_params[512];
    int64_t    session_start_epoch_dms;
    int64_t    session_start_epoch_aux[6];
    int64_t    reserved[8];
} analytics_frame_header_v3;

// shared memory specific declarations
/* structure to hold non-data section of SMB (shared memory block) */
struct sm_block_s {
    int64_t magic_header;     // magic header for each SMB
    int64_t uid;              // unique id (ever increaing number from boot)
    int64_t data_len;         // length of the data
    int64_t reserved;          // reserved field
};
map<int, sm_block_s> smbs;
int curr_smb_index = 0;
string shared_mem_file = "/dev/shm/MSGQ/shmfile/CAM8"; //testing only for DMS camera for now
string sem_file = "/dev/shm/MSGQ/semfile/CAM8"; //testing only for DMS camera for now
static const int pid_base = 65;

// socket specific declarations
int sfd, cfd; // server and client fds
bool is_client_connected = false;

struct timespec startTime, endTime; //for profiling

//Create an empty file
bool file_touch(string fname, bool change_owner)
{
    int d = open(fname.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (d == -1) {
        printf("Cannot touch file: %s error msg : %s\n", fname.c_str(),  strerror(errno));
        return false;
    }
    close(d);
    printf("Created file: %s\n", fname.c_str());
    return true;
}

static bool release_lock(int sem_id)
{
//    printf("releasing lock for sem_id: %d", sem_id);
    int ret = semctl(sem_id, 0, IPC_RMID);
    if (ret == -1) {
        printf("remove semaphore failed: %s\n", strerror(errno));
        return false;
    }

    return true;
}

void increment_smb_index(int &smb_index)
{
    if (smb_index == smbs.size() - 1) {
        smb_index = 0;
        return;
    }
    smb_index++;

    if (smb_index == curr_smb_index) {
        printf("No free SMBs found for writing. Not expected\n");
    }
}

void set_socket_non_blocking(int sockfd)
{
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL failed");
        return;
    }
    flags |= O_NONBLOCK;
    if (fcntl(sockfd, F_SETFL, flags) == -1) {
        perror("fcntl F_SETFL failed");
        return;
    }
}

void *socket_setup(void *arg)
{
    struct sockaddr_un addr;

    // Create the server socket
    sfd = socket(AF_UNIX, SOCK_STREAM, 0); 
    if (sfd == -1) {
        perror("socket");
        return NULL;
    }

    // Remove the socket file if it exists
    unlink(ANALYTICS_SOCKET_PATH);

    // Set up the server address structure
    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, ANALYTICS_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    // Bind the socket
    if (bind(sfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_un)) == -1) {
        perror("bind");
        close(sfd);
        return NULL;
    }

    // Start listening on the socket
    if (listen(sfd, 1) == -1) {
        perror("listen");
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
        int ret = poll(fds, 1, -1);  // -1 means infinite timeout
        if (ret < 0) {
            perror("Poll failed");
            close(sfd);
            return NULL;
        }

        // Check if the server socket is ready to accept new connections
        if (fds[0].revents & POLLIN) {
            cfd = accept(sfd, NULL, NULL);
            if (cfd < 0) {
                perror("Accept failed");
                continue;
            }
            set_socket_non_blocking(cfd);
            is_client_connected = true;
            return NULL;
        }
    }
}

void copy_dma_buffer(int src_dmabuf_fd, int dst_dmabuf_fd)
{
    NvBufferParams src_params, dst_params;								

    NvBufferGetParams(src_dmabuf_fd, &src_params);											  
    NvBufferGetParams(dst_dmabuf_fd, &dst_params);											  

    NvBufferTransformParams trans_params;
    memset(&trans_params, 0, sizeof(trans_params));
#if 0
    trans_params.src_rect.top = 0;
    trans_params.src_rect.left = 0;
    trans_params.src_rect.width = src_params.width[0];
    trans_params.src_rect.height = src_params.height[0];
    //trans_params.src_rect = {0, 0, src_params.width[0], src_params.height[0]};

    trans_params.dst_rect.top = 0;
    trans_params.dst_rect.left = 0;
    trans_params.dst_rect.width = dst_params.width[0];
    trans_params.dst_rect.height = dst_params.height[0];
    //trans_params.dst_rect = {0, 0, dst_params.width[0], dst_params.height[0]};
#endif
    trans_params.transform_flag = NVBUFFER_TRANSFORM_FILTER;
    trans_params.transform_filter = NvBufferTransform_Filter_Nearest;

    // Perform the copy using NvBufferTransform
    int ret = NvBufferTransform(src_dmabuf_fd, dst_dmabuf_fd, &trans_params); //Profiling required
    if (ret != 0) {
        fprintf(stderr, "NvBufferTransform failed: %d\n", ret);
        return;
    }
}

void shm_write_cb(int src_dmabuf_fd, int dst_dmabuf_fd)
{
    copy_dma_buffer(src_dmabuf_fd, dst_dmabuf_fd);
}

bool get_free_smb(int &ret_smb_id, int64_t &ret_uid, int smb_data_size, int &dst_dmabuf_fd, int src_dmabuf_fd)
{
    int smb_index = curr_smb_index;
    increment_smb_index(smb_index);

    // loop across all smbs
    while (smb_index != curr_smb_index) {
        key_t sem_key = ftok(sem_file.c_str(), pid_base + smb_index);
        if (sem_key < 0) {
            printf("failed to get sem_key, error: %s\n", strerror(errno));
            increment_smb_index(smb_index);
            continue;
        }
        int sem_id = semget(sem_key, 1, 0666|IPC_CREAT|IPC_EXCL);
        if (sem_id < 0) {
            printf("failed to get sem_id or other process might have got it: %s:smbid: %d\n", strerror(errno), smb_index);
            increment_smb_index(smb_index);
            continue;
        }
        key_t shm_key = ftok(shared_mem_file.c_str(), pid_base + smb_index);
        if (shm_key < 0) {
            printf("failed to get shm_key, error: %s\n", strerror(errno));
            release_lock(sem_id);
            increment_smb_index(smb_index);
            continue;
        }
        int shm_id = shmget(shm_key, smb_data_size + sizeof(sm_block_s), 0);
        if (shm_id == -1) {
            printf("failed to get shm_id, error: %s\n", strerror(errno));
            release_lock(sem_id);
            increment_smb_index(smb_index);
            continue;
        }
        uint8_t *smb_block_ptr = (uint8_t *) shmat(shm_id, (void*)0, 0);
        if (!smb_block_ptr) {
            printf("failed to attach to shared memory\n");
            release_lock(sem_id);
            increment_smb_index(smb_index);
            continue;
        }
        sm_block_s *smb_block = (sm_block_s *) smb_block_ptr;
        if (smb_block->magic_header != 0xDEADBEAF) {
            printf("magic header mismatch, data corrupted !!\n");
            shmdt(smb_block_ptr);
            release_lock(sem_id);
            increment_smb_index(smb_index);
            continue;
        }
        smb_block->uid++;

        dst_dmabuf_fd = *(int *)(smb_block_ptr + sizeof(sm_block_s));
        printf("locked_fd: %d\n", dst_dmabuf_fd);
        
        shm_write_cb(src_dmabuf_fd, dst_dmabuf_fd);
        
        ret_smb_id = smb_index;
        ret_uid = smb_block->uid;

        shmdt(smb_block_ptr);
        release_lock(sem_id);
        
        break;
    }
    curr_smb_index = smb_index;

    return true;
}

void handle_client_disconnection(int client_fd)
{
    printf("Client disconnected or crashed (fd: %d)\n", client_fd);
    close(client_fd);
    close(sfd);
    is_client_connected = false;

    /* Set up a Unix Domain socket to accept connection from analytics service*/
    pthread_t socket_setup_th;
    pthread_create(&socket_setup_th, NULL, socket_setup, NULL);
}

void send_fd_to_analytics(int dmabuf_fd)
{
    int dmabuf_fd_new;
    NvBufferParams params;
    NvBufferParamsEx paramsEx;
    NvBufferGetParams(dmabuf_fd, &params);
    NvBufferGetParamsEx(dmabuf_fd, &paramsEx);
        
    struct pollfd fds[1];
    fds[0].fd = cfd;
    fds[0].events = POLLOUT;
   // printf("Before poll, fds[0].revents: %d\n", fds[0].revents);
    int ret = poll(fds, 1, 0);  // Poll with zero timeout
    if (ret < 0) {
        perror("Poll failed");
        handle_client_disconnection(fds[0].fd);
        return;
    }
  //  printf("After poll, fds[0].revents: %d\n", fds[0].revents);

    if (fds[0].revents & POLLERR) {
        // Error on the socket, such as client crash or unexpected disconnection
        printf("Error on client socket (fd: %d)\n", fds[0].fd);
        handle_client_disconnection(fds[0].fd);
        return;
    } else if (fds[0].revents & POLLHUP) {
        // The file descriptor has been closed (hung up)
        printf("fd %d has been hung up (closed)\n", fds[0].fd);
        handle_client_disconnection(fds[0].fd);
        return;
    }

    int smb_id = -1;
    int64_t uid = -1;
    int smb_data_size = sizeof(int);
    get_free_smb(smb_id, uid, smb_data_size, dmabuf_fd_new, dmabuf_fd);

    analytics_frame_header_v3 messg_data;
    memset(&messg_data, 0x00, sizeof(messg_data));
    messg_data.smb_id = smb_id;
    messg_data.uid = uid;


    NvBufferParamsEx paramsExTemp;
    NvBufferGetParamsEx(dmabuf_fd_new, &paramsExTemp);
    memcpy(messg_data.shm_buffer_params, &paramsExTemp, sizeof(paramsExTemp));

    struct iovec io = {
        .iov_base = &messg_data,
        .iov_len = sizeof(messg_data)
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
    memcpy((int *) CMSG_DATA(cmsg), &dmabuf_fd_new, sizeof(int));

    int bytes_sent = sendmsg(cfd, &msg, 0);
    if (bytes_sent < 0) {
        perror("sendmsg failed");
        handle_client_disconnection(cfd);
        return;
    }
    //printf("sendmsg: bytes_sent = %d\n", bytes_sent);

    struct timeval tv;
    gettimeofday(&tv, NULL);
    long long epoch_time_ms = (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;

    printf("%lld: DMABUF FD %d sent to analytics process.\n", epoch_time_ms, dmabuf_fd_new);
    
    return;
}

int OUTWARD_MIN_INTRA_FRAME_INTERVAL_NS = ((1000 / 1) * 1000 * 1000) - ((1000 / (30 * 2)) * 1000 * 1000);
static void appsink_roadfacing_cb(GstAppSink *object, gpointer user_data)
{
    static volatile uint64_t prev_pts = 0;
    static bool first_cb = true;
    uint64_t pts_diff = 0;
    uint64_t curr_pts = 0;
    int dmabuf_fd = 0;

    GstMapInfo map = {0};

    GstAppSink* app_sink = (GstAppSink*) object;
    GstSample *sample = gst_app_sink_pull_sample(app_sink);
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    gst_buffer_map (buffer, &map, GST_MAP_READ);

    curr_pts = GST_BUFFER_PTS (buffer);
    pts_diff = curr_pts - prev_pts;
    if (first_cb || (pts_diff > OUTWARD_MIN_INTRA_FRAME_INTERVAL_NS)) {
        first_cb = false;
        prev_pts = curr_pts;
    } else {
        gst_buffer_unmap(buffer, &map);
        gst_sample_unref(sample);
        return;
    }

    ExtractFdFromNvBuffer((void *)map.data, &dmabuf_fd);
    //g_print("DMA-BUF FD: %d\n", dmabuf_fd);

    // Pass dmabuf_fd to analytics process
    if (is_client_connected ==  true)
         send_fd_to_analytics(dmabuf_fd);
    else 
        printf("Dropping since client is not connected\n");
   
    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);
 
    return;
}

int create_shm_writer(int64_t smb_data_size, int num_smbs)
{
    if (!file_touch(shared_mem_file, false)) {
        printf("Failed to create shared_mem_file\n");
        return -1;
    }

    if (!file_touch(sem_file, false)) {
        printf("Failed to create sem_file\n");
        return -1;
    }

    for (int i = 0; i < num_smbs; i++) {
        key_t shm_key = ftok(shared_mem_file.c_str(), pid_base + i);
        if (shm_key < 0) {
            printf("failed to get shm_key, error: %s\n", strerror(errno));
            continue;
        }
        int shm_id = shmget(shm_key, smb_data_size + sizeof(sm_block_s), 0666 | IPC_CREAT);
        if (shm_id == -1) {
            printf("failed to get shm_id, error: %s\n", strerror(errno));
            continue;
        }
    //    printf("i: %d, shm_id: %d\n", i, shm_id);
        
        // create new smb and write to shared mem
        sm_block_s smb_block;
        smb_block.magic_header = 0xDEADBEAF;
        smb_block.uid = 0;
        smb_block.data_len = smb_data_size;
        smbs.insert(std::make_pair(i, smb_block));

        // write smb to shared memory
        uint8_t *smb_start_ptr = (uint8_t *) shmat(shm_id, (void*)0, 0);
        if (!smb_start_ptr) {
            printf("failed to attach shared memory segment\n");
            continue;
        }
        memcpy(smb_start_ptr, &smb_block, sizeof(smb_block));

        shmdt(smb_start_ptr);
    }
    return 0;
}

void create_buffer_pool()
{
    NvBufferCreateParams create_params;
    int *ptr = NULL;
    int dmabuf_fd;
    
    int smb_data_size = sizeof(int);
    int num_of_smbs = 15;

    /* Create shared memory writer */
    int ret = create_shm_writer(smb_data_size, num_of_smbs);
    
    for (int i = 0; i < num_of_smbs; i++) {
        memset(&create_params, 0, sizeof(NvBufferCreateParams));
        create_params.width = 1296;
        create_params.height = 1296;
        create_params.payloadType = NvBufferPayload_SurfArray;
        create_params.layout = NvBufferLayout_Pitch;
        create_params.colorFormat = NvBufferColorFormat_NV12;

        // Allocating hardware buffers
        NvBufferCreateEx(&dmabuf_fd, &create_params);
        
        key_t shm_key = ftok(shared_mem_file.c_str(), pid_base + i);
        int shm_id = shmget(shm_key, smb_data_size + sizeof(sm_block_s), 0666|IPC_CREAT);
        if (shm_id == -1) {
            printf("failed to get shm_id, error: %s\n", strerror(errno));
            continue;
        }
      //  printf("i: %d, shm_id: %d\n", i, shm_id);
#if 0
        NvBufferParams params; 
        NvBufferParamsEx paramsEx;
        NvBufferGetParams(dmabuf_fd, &params);
        NvBufferGetParamsEx(dmabuf_fd, &paramsEx);
        
        printf("dmabuf_fd: %u, nv_buffer: %p, payloadType: %d, pixel_format: %d, num_planes: %d\n", params.dmabuf_fd, params.nv_buffer, params.payloadType, params.pixel_format, params.num_planes);
#endif
        uint8_t *smb_block_ptr = (uint8_t *) shmat(shm_id, (void*)0, 0);
        ptr = (int *)(smb_block_ptr + sizeof(sm_block_s));
        printf("dmabuf_fd: %d added to shared memory\n", dmabuf_fd);
        *ptr = dmabuf_fd; //writing dmabuf fds to shared memory

        shmdt(smb_block_ptr);
    }
}

int main(int argc, char *argv[])
{
    /* Pre-allocate a pool of NV buffers for copying data from NVMM buffer
     * through DMABUF FD and pass the same to analytics process
     */
    create_buffer_pool();
    
    /* Set up a Unix Domain socket to accept connection from analytics service*/
    pthread_t socket_setup_th;
    pthread_create(&socket_setup_th, NULL, socket_setup, NULL);

    gst_init(&argc, &argv);

    // Create GStreamer pipeline for outward camera
    //GstElement *pipeline = gst_parse_launch("nvv4l2camerasrc name=outward_camera device=/dev/video0 ! video/x-raw(memory:NVMM), format=UYVY, width=1920, height=1080 ! nvvidconv name=vidconv_rt ! video/x-raw(memory:NVMM), format=NV12, width=1296, height=1296, framerate=30/1 ! appsink name=appsink_rt emit-signals=TRUE max-buffers=3 async=FALSE drop=TRUE sync=FALSE", NULL);

    //DMS camera pipeline
    GstElement *pipeline = gst_parse_launch("v4l2src name=dms_camera device=/dev/dms_h264 ! video/x-h264, stream-format=(string)byte-stream ! nvv4l2decoder ! video/x-raw(memory:NVMM), format=NV12, width=1296, height=1296, framerate=30/1 ! appsink name=appsink_rt emit-signals=TRUE max-buffers=3 async=FALSE drop=TRUE sync=FALSE", NULL);
    //GstElement *pipeline = gst_parse_launch("v4l2src name=dms_camera device=/dev/dms_h264 ! video/x-h264, stream-format=(string)byte-stream ! nvv4l2decoder ! nvvidconv ! video/x-raw(memory:NVMM), format=NV12, width=1296, height=1296, framerate=30/1 ! appsink name=appsink_rt emit-signals=TRUE max-buffers=3 async=FALSE drop=TRUE sync=FALSE", NULL);
    
    // Get the nvvidconv element from the pipeline
    GstElement *elem = gst_bin_get_by_name(GST_BIN(pipeline), "appsink_rt");
    g_signal_connect (elem, "new-sample", G_CALLBACK (appsink_roadfacing_cb), NULL);
    g_object_unref (elem);

    // Start the pipeline
    int ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    printf("\ngst_element_set_state to PLAYING returns %d\n", ret);

    gst_element_get_state(pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

    // Create a GStreamer bus and listen for messages
    GstBus *bus = gst_element_get_bus(pipeline);
    gboolean terminate = FALSE;
    while (!terminate) {
        GstMessage *msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE, (GstMessageType)(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
        if (msg != NULL) {
            GError *err;
            gchar *debug_info;

            switch (GST_MESSAGE_TYPE(msg)) {
                case GST_MESSAGE_ERROR:
                    gst_message_parse_error(msg, &err, &debug_info);
                    g_printerr("Error received from element %s: %s\n", GST_OBJECT_NAME(msg->src), err->message);
                    g_printerr("Debugging information: %s\n", debug_info ? debug_info : "none");
                    g_clear_error(&err);
                    g_free(debug_info);
                    terminate = TRUE;
                    break;
                case GST_MESSAGE_EOS:
                    g_print("End-Of-Stream reached.\n");
                    terminate = TRUE;
                    break;
                default:
                    // Unhandled message
                    break;
            }
            gst_message_unref(msg);
        }
    }

    // Clean up
    gst_object_unref(bus);

    // Stop the pipeline
    ret = gst_element_set_state(pipeline, GST_STATE_NULL);
    printf("\ngst_element_set_state to NULL returns %d\n", ret);

    gst_object_unref(pipeline);

    close(sfd);
    if (unlink("/tmp/fd-share.socket") == -1 && errno != ENOENT) {
        printf("Removing socket file failed.\n");
    }
    return 0;
}
