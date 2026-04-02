#include <sys/types.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <cuda_egl_interop.h>
#include <nvbuf_utils.h>
#include <fcntl.h>
#include <assert.h>
#include <zmq.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <string>
#include <poll.h>

using namespace std;

#define FILE_DUMP

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


typedef struct _my_struct {
    int recv_dmabuf_fd;
    int imp_dmabuf_fd;
    int smb_id;
    int64_t uid;
    int sent_dmabuf_fd;
    bool is_filled;
} my_struct;

my_struct my_instances[15]; //Create as many instances as there are DMA buffers

// shared memory specific declarations
enum read_smb_ret_t {
    SMB_READ_FAILURE,
    SMB_DATA_CORRUPTED,
    SMB_DATA_OVERWRITTEN,
    SMB_READ_CB_FAILURE,
    SMB_READ_SUCCESS
};
/* structure to hold non-data section of SMB (shared memory block) */
struct sm_block_s {
    int64_t magic_header;     // magic header for each SMB
    int64_t uid;              // unique id (ever increaing number from boot)
    int64_t data_len;         // length of the data
    int64_t reserved;          // reserved field
};
string shared_mem_file = "/dev/shm/MSGQ/shmfile/CAM8"; //testing only for DMS camera for now
string sem_file = "/dev/shm/MSGQ/semfile/CAM8"; //testing only for DMS camera for now
static const int pid_base = 65;

pthread_cond_t rw_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t rw_lock = PTHREAD_MUTEX_INITIALIZER;

int read_index = 0;

static bool release_lock(int sem_id)
{
//    printf("releasing lock for sem_id: %d", sem_id);
    int ret = semctl(sem_id, 0, IPC_RMID);
    if (ret == -1) {
        printf("remove semaphore failed: %s", strerror(errno));
        return false;
    }

    return true;
}

void copyFromDMABufToCudaUsingEGL(int dmabuf_fd)
{
    int width = 1296;
    int height = 1296;
    int luma_size = width * height;
    int chroma_size = (width * height) / 2;

    // 1. Create an EGL display connection
    EGLDisplay eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(eglDisplay, NULL, NULL);

    // 2. Create an EGL image from the DMA-BUF FD using NvEGLImageFromFd
    EGLImageKHR eglImage = NvEGLImageFromFd(eglDisplay, dmabuf_fd);
    if (eglImage == EGL_NO_IMAGE_KHR) {
        fprintf(stderr, "Failed to create EGL image from FD using NvEGLImageFromFd.\n");
        exit(EXIT_FAILURE);
    }

    // 3. Create a CUDA EGL resource from the EGL image
    cudaGraphicsResource_t cudaResource;
    CUDA_CHECK(cudaGraphicsEGLRegisterImage(&cudaResource, eglImage, cudaGraphicsRegisterFlagsReadOnly));

    // 4. Retrieve the mapped EGL frame using cuGraphicsResourceGetMappedEglFrame
    CUeglFrame eglFrame;
    CU_CHECK(cuGraphicsResourceGetMappedEglFrame(&eglFrame, (CUgraphicsResource)cudaResource, 0, 0));
    
#ifdef FILE_DUMP
    // Verification: Copy data back from CUDA device memory to host and write to a file
    unsigned char *verify_host_y = (unsigned char *)malloc(luma_size);
    unsigned char *verify_host_uv = (unsigned char *)malloc(chroma_size);

    // Check the frame type and copy data accordingly
    if (eglFrame.frameType == CU_EGL_FRAME_TYPE_ARRAY) {
        //printf("Cuda buffer type is array\n");
        // The frame uses CUDA arrays, so use cudaMemcpyFromArray
        CUDA_CHECK(cudaMemcpyFromArray(verify_host_y, (cudaArray_const_t)eglFrame.frame.pArray[0], 0, 0, luma_size, cudaMemcpyDeviceToHost));
        CUDA_CHECK(cudaMemcpyFromArray(verify_host_uv, (cudaArray_const_t)eglFrame.frame.pArray[1], 0, 0, chroma_size, cudaMemcpyDeviceToHost));
    } else if (eglFrame.frameType == CU_EGL_FRAME_TYPE_PITCH) {
        printf("Cuda buffer type is pitch, width: %d, height: %d, pitch: %d\n", eglFrame.width, eglFrame.height, eglFrame.pitch);
        // The frame uses pitch-linear memory, so use cudaMemcpy
        CUDA_CHECK(cudaMemcpy2D(verify_host_y, width, eglFrame.frame.pPitch[0], eglFrame.pitch, width, height, cudaMemcpyDeviceToHost));
        CUDA_CHECK(cudaMemcpy2D(verify_host_uv, width, eglFrame.frame.pPitch[1], eglFrame.pitch, width, height / 2, cudaMemcpyDeviceToHost));

    } else {
        fprintf(stderr, "Unknown CUeglFrame type.\n");
        exit(EXIT_FAILURE);
    }

    // Write the verified data to a file
    FILE *verify_file = fopen("/home/ubuntu/dmabuf/yuv_cuda_verification_dump.raw", "ab");
    if (verify_file) {
        fwrite(verify_host_y, sizeof(unsigned char), luma_size, verify_file);
        fwrite(verify_host_uv, sizeof(unsigned char), chroma_size, verify_file);
        fclose(verify_file);
        //printf("Verification file written successfully.\n");
    } else {
        fprintf(stderr, "Failed to open verification output file.\n");
    }

    // Free host memory
    free(verify_host_y);
    free(verify_host_uv);
#endif
    // Destroy the EGL image using NvDestroyEGLImage
    NvDestroyEGLImage(eglDisplay, eglImage);
    eglTerminate(eglDisplay);
}

void shm_read_cb(int recv_dmabuf_fd)
{
    copyFromDMABufToCudaUsingEGL(recv_dmabuf_fd);
}

read_smb_ret_t read_smb(int smb_id, int64_t uid, int imported_dmabuf_fd)
{
    int smb_data_size = sizeof(int);
    key_t sem_key = ftok(sem_file.c_str(), pid_base + smb_id);
    if (sem_key < 0) {
        printf("failed to get sem_key, error: %s\n", strerror(errno));
        return SMB_READ_FAILURE;
    }
    int sem_id = semget(sem_key, 1, 0666|IPC_CREAT|IPC_EXCL);
    if (sem_id < 0) {
        printf("failed to get sem_id or other process might be busy using it. error: %s\n", strerror(errno));
        return SMB_READ_FAILURE;
    }
    key_t shm_key = ftok(shared_mem_file.c_str(), pid_base + smb_id);
    if (shm_key < 0) {
        printf("failed to get shm_key, error: %s\n", strerror(errno));
        release_lock(sem_id);
        return SMB_READ_FAILURE;
    }
    int shm_id = shmget(shm_key, smb_data_size + sizeof(sm_block_s), 0);
    if (shm_id == -1) {
        printf("failed to get shm_id, error: %s\n", strerror(errno));
        release_lock(sem_id);
        return SMB_READ_FAILURE;
    }
    uint8_t *smb_start_ptr = (uint8_t *) shmat(shm_id, (void*)0, 0);
    if (!smb_start_ptr) {
        printf("failed to attach to shared memory\n");
        release_lock(sem_id);
        return SMB_READ_FAILURE;
    }
    sm_block_s smb_block;
    memcpy(&smb_block, smb_start_ptr, sizeof(smb_block));
    if (smb_block.magic_header != 0xDEADBEAF) {
        printf("magic header mismatch, data corrupted !!\n");
        shmdt(smb_start_ptr);
        release_lock(sem_id);
        return SMB_DATA_CORRUPTED;
    }
    if (smb_block.uid != uid) {
        printf("uid mismatch [got: %ld, expected: %ld]. looks like data got overwritten\n", smb_block.uid, uid);
        shmdt(smb_start_ptr);
        release_lock(sem_id);
        return SMB_DATA_OVERWRITTEN;
    }
    shm_read_cb(imported_dmabuf_fd);
    
    shmdt(smb_start_ptr);
    release_lock(sem_id);

    return SMB_READ_SUCCESS;
}

void *do_cuda_processing(void *arg)
{
    int recv_dmabuf_fd;
    int imp_dmabuf_fd;
    int sent_dmabuf_fd;
    int smb_id;
    int64_t uid;

    while (1) {
        pthread_mutex_lock(&rw_lock);
        while (my_instances[read_index].is_filled == false) {
           pthread_cond_wait(&rw_cond, &rw_lock); // do we need to add timedwait?
        }
        recv_dmabuf_fd = my_instances[read_index].recv_dmabuf_fd;
        imp_dmabuf_fd = my_instances[read_index].imp_dmabuf_fd;
        smb_id = my_instances[read_index].smb_id;
        uid = my_instances[read_index].uid;
        sent_dmabuf_fd = my_instances[read_index].sent_dmabuf_fd;
        my_instances[read_index].is_filled = false;

        pthread_cond_signal(&rw_cond);
        pthread_mutex_unlock(&rw_lock);

        printf("smb_id: %d, uid: %ld, recv_dmabuf_fd: %d, imp_dmabuf_fd: %d, sent_dmabuf_fd: %d\n", smb_id, uid, recv_dmabuf_fd, imp_dmabuf_fd, sent_dmabuf_fd);
        int ret_read = read_smb(smb_id, uid, imp_dmabuf_fd);
        
        NvBufferDestroy(imp_dmabuf_fd);
        close(recv_dmabuf_fd);

        read_index++;
        if (read_index == 15)
            read_index = 0;
    }
}

// Function to set socket to non-blocking mode
void set_socket_non_blocking(int sockfd)
{
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL failed");
        exit(EXIT_FAILURE);
    }
    if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL failed");
        exit(EXIT_FAILURE);
    }
}

void print_shm_buffer_params(const int8_t shm_buffer_params[512]) {
    int sizeNVM = sizeof(NvBufferParamsEx);
    char buf[sizeNVM*2 + 1]; // 2 chars per byte + 1 for null terminator
    int pos = 0;
    for (int i = 0; i < sizeNVM; ++i) {
        pos += snprintf(buf + pos, sizeof(buf) - pos, "%02x", (uint8_t)shm_buffer_params[i]);
        if (pos >= (int)sizeof(buf) - 3) break; // avoid overflow
    }
    buf[sizeof(buf) - 1] = '\0';
    printf("shm_buffer_params (hex): %s\n", buf);
}

void print_NvBufferParams_printf(const NvBufferParams& params) {
    printf("NvBufferParams:\n");
    printf("  dmabuf_fd: %u\n", params.dmabuf_fd);
    printf("  nv_buffer: %p\n", params.nv_buffer);
    printf("  payloadType: %d\n", params.payloadType);
    printf("  memsize: %d\n", params.memsize);
    printf("  nv_buffer_size: %u\n", params.nv_buffer_size);
    printf("  pixel_format: %d\n", params.pixel_format);
    printf("  num_planes: %u\n", params.num_planes);
    for (unsigned int i = 0; i < params.num_planes && i < MAX_NUM_PLANES; ++i) {
        printf("  Plane %u:\n", i);
        printf("    width: %u\n", params.width[i]);
        printf("    height: %u\n", params.height[i]);
        printf("    pitch: %u\n", params.pitch[i]);
        printf("    offset: %u\n", params.offset[i]);
        printf("    psize: %u\n", params.psize[i]);
        printf("    layout: %u\n", params.layout[i]);
    }
}

void print_NvBufferParamsEx(const NvBufferParamsEx& paramsEx) {
    printf("NvBufferParamsEx:\n");
    print_NvBufferParams_printf(paramsEx.params);
}

void print_analytics_frame_header_v3(const analytics_frame_header_v3& msg) {
    printf("analytics_frame_header_v3:\n");
    printf("  cam_id: %d\n", msg.cam_id);
    printf("  frame_cnt: %d\n", msg.frame_cnt);
    printf("  timestamp: %lu\n", msg.timestamp);
    printf("  pts: %lu\n", msg.pts);
    printf("  session_id_len: %d\n", msg.session_id_len);
    printf("  gps_data: { valid: %d, lat: %lf, lon: %lf, alt: %lf, speed: %f, bearing: %f, accuracy: %f, timestamp: %ld, system_timestamp: %ld, flags: %d, privacy_enabled: %d }\n",
        msg.gps_data.valid, msg.gps_data.latitude, msg.gps_data.longitude, msg.gps_data.altitude,
        msg.gps_data.speed, msg.gps_data.bearing, msg.gps_data.accuracy, msg.gps_data.timestamp,
        msg.gps_data.system_timestamp, msg.gps_data.flags, msg.gps_data.privacy_enabled);
    printf("  session_id: %.*s\n", msg.session_id_len, msg.session_id);
    printf("  dis: %s\n", msg.dis);
    printf("  dis_flag: %d\n", msg.dis_flag);
    printf("  privacy: %d\n", msg.privacy);
    printf("  idle: %d\n", msg.idle);
    printf("  ir_led_status: %d\n", msg.ir_led_status);
    printf("  dts: %lu\n", msg.dts);
    printf("  smb_id: %d\n", msg.smb_id);
    printf("  uid: %ld\n", msg.uid);
    printf("  session_start_epoch_outward: %ld\n", msg.session_start_epoch_outward);
    printf("  session_start_epoch_inward: %ld\n", msg.session_start_epoch_inward);
    printf("  nd_start_ts: %ld\n", msg.nd_start_ts);
    printf("  nd_sent_ts: %ld\n", msg.nd_sent_ts);
    printf("  rt_frame_status: %ld\n", msg.rt_frame_status);
    printf("  session_start_epoch_dms: %ld\n", msg.session_start_epoch_dms);
    printf("  reserved[0]: %ld\n", msg.reserved[0]);

    print_shm_buffer_params(msg.shm_buffer_params);
   
}


void receive_and_process_fd()
{
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, ANALYTICS_SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
    int client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    int rcvbuf_size;
    socklen_t optlen = sizeof(rcvbuf_size);
   
#if 0 
    // Get socket options
    if (getsockopt(client_fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf_size, &optlen) == -1) {
        perror("getsockopt");
    }
    printf("SO_RCVBUF: orig_rcvbuf_size = %d\n", rcvbuf_size);

    // Set socket options
    int size = 1024 * 1024;  // 1 MB
    if (setsockopt(client_fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)) == -1) {
        perror("setsockopt");
    }
    
    // Get socket options
    if (getsockopt(client_fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf_size, &optlen) == -1) {
        perror("getsockopt");
    }
    printf("SO_RCVBUF: new_rcvbuf_size = %d\n", rcvbuf_size);
#endif

    while (1) {
        // connect to the server
        int ret = connect(client_fd, (struct sockaddr *)&addr, sizeof(addr));
        if (ret < 0) {
            perror("connect");
            sleep(1);
            continue;
        }
        printf("client_fd %d connected to server\n", client_fd);

        // Set client socket to non-blocking mode
        set_socket_non_blocking(client_fd);
    
        analytics_frame_header_v3 messg_data;
    
        // Buffer to receive the control message
        char cmsgbuf[CMSG_SPACE(sizeof(int))];
        struct msghdr msg;
        int write_index = 0;
        int recv_dmabuf_fd, imp_dmabuf_fd;

        while (1) {
            memset(&messg_data, 0, sizeof(analytics_frame_header_v3));

            struct iovec io = {
                .iov_base = &messg_data,
                .iov_len = sizeof(messg_data)
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
                perror("Poll failed");
                read_index = 0; //this is done to make cuda_processing thread wait
                my_instances[0].is_filled = false;
                close(client_fd);
                printf("client_fd %d closed\n", client_fd);
                client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
                break;
            }
            //printf("After poll, fds[0].revents: %d\n", fds[0].revents);

            if (fds[0].revents & POLLHUP) {
                printf("client_fd %d is no longer connected to the server, probably server has crashed\n", client_fd);
                read_index = 0; //this is done to make cuda_processing thread wait
                my_instances[0].is_filled = false;
                close(client_fd);
                printf("client_fd %d closed\n", client_fd);
                client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
                break;
            } else if (fds[0].revents & POLLIN) {
                // Receive the message
                if (recvmsg(client_fd, &msg, 0) <= 0) {
                    perror("recvmsg");
                    read_index = 0; //this is done to make cuda_processing thread wait
                    my_instances[0].is_filled = false;
                    close(client_fd);
                    printf("client_fd %d closed\n", client_fd);
                    client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
                    break;
                }   

                struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
                if (cmsg == NULL) {
                    fprintf(stderr, "No control message received\n");
                    continue;
                }   
                if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) {
                    fprintf(stderr, "Invalid control message level/type\n");
                    continue;
                }
                //analytics should drop the session over here after receiving 
                //drop_session_message from bagheera

                print_analytics_frame_header_v3(messg_data);

                // Skip processing if rt_frame_status is not 0
                if (messg_data.rt_frame_status != 0) {
                    // Extract the fd to close it and prevent fd leak
                    memcpy(&recv_dmabuf_fd, (int *) CMSG_DATA(cmsg), sizeof(int));

                    printf("Skipping frame processing: rt_frame_status = %ld, recv_dmabuf_fd: %d\n", messg_data.rt_frame_status, recv_dmabuf_fd);
                    close(recv_dmabuf_fd);
                    continue;
                }
 
                memcpy(&recv_dmabuf_fd, (int *) CMSG_DATA(cmsg), sizeof(int));
                
                NvBufferParamsEx paramsEx;
                // memset(&paramsEx, 0, sizeof(paramsEx));
                memcpy(&paramsEx, &messg_data.shm_buffer_params, sizeof(paramsEx));
                // printf("Received dmabuf_fd: %d\n", recv_dmabuf_fd
                NvBufferImportFd(recv_dmabuf_fd, &imp_dmabuf_fd, &paramsEx);
                printf("extracted nvbuf params: recv_dmabuf_fd: %d, imp_dmabuf_fd: %d, sent_dmabuf_fd: %d\n",
                     recv_dmabuf_fd, imp_dmabuf_fd, paramsEx.params.dmabuf_fd);
                print_NvBufferParamsEx(paramsEx);

                pthread_mutex_lock(&rw_lock);
                while (my_instances[write_index].is_filled == true) {
                    pthread_cond_wait(&rw_cond, &rw_lock);
                }
                my_instances[write_index].recv_dmabuf_fd = recv_dmabuf_fd;
                my_instances[write_index].imp_dmabuf_fd = imp_dmabuf_fd;
                my_instances[write_index].smb_id = messg_data.smb_id;

                my_instances[write_index].uid = messg_data.uid;
                my_instances[write_index].sent_dmabuf_fd = paramsEx.params.dmabuf_fd;
                my_instances[write_index].is_filled = true;

                pthread_cond_signal(&rw_cond);
                pthread_mutex_unlock(&rw_lock);

                write_index++;

                if (write_index == 15)
                    write_index = 0;

#if 0 //For Testing
                static bool first_call = true;
                if (first_call) {
                    sleep(250);
                    first_call = false;
                }
#endif
            }
        }
    }
}

int main()
{
    pthread_t cuda_process_th;
    pthread_create(&cuda_process_th, NULL, do_cuda_processing, NULL);  
    receive_and_process_fd();
    return 0;
}
