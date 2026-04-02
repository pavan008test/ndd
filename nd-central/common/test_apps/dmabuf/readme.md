# DMA Buffer Test Applications

## Overview

Two test applications for demonstrating DMA buffer sharing between processes on NVIDIA Jetson platforms:

1. **dmabuf_publisher_test_app** - Captures DMS camera frames and sends DMA buffer FDs via Unix socket
2. **dmabuf_receiver_test_app** - Receives DMA buffer FDs, imports them, and dumps frames using CUDA/EGL

---

## Quick Start

⚠️ **CRITICAL**: Bagheera must be restarted within 1 minute of stopping. You have **60 seconds** to complete this test!

### Steps

1. **Setup output directory**
   ```bash
   mkdir -p /home/ubuntu/dmabuf/
   chmod 777 /home/ubuntu/dmabuf/
   ```

2. **Stop services**
   ```bash
   systemctl stop analyticsService cam_rec bagheera
   ```
   ⏱️ **Timer starts now - 60 seconds!**

3. **Run applications** (in two separate terminals)
   
   Terminal 1:
   ```bash
   ./dmabuf_publisher_test_app
   ```
   
   Terminal 2:
   ```bash
   ./dmabuf_receiver_test_app
   ```

4. **Wait 10-20 seconds**, then stop both (Ctrl+C)

5. **Verify output**
   ```bash
   # Check file exists
   ls -lh /home/ubuntu/dmabuf/yuv_cuda_verification_dump.raw
   
   # View frames (optional)
   ffplay -f rawvideo -pixel_format nv12 -video_size 1296x1296 /home/ubuntu/dmabuf/yuv_cuda_verification_dump.raw
   OR
   vooya yuv_cuda_verification_dump.raw
   ```

6. **Restart bagheera**
   ```bash
   systemctl start bagheera
   ```

---

## Build Instructions

### Publisher
```bash
g++ -g dmabuf_publisher_test_app.cpp -o dmabuf_publisher_test_app \
$(pkg-config --cflags --libs gstreamer-1.0 gstreamer-app-1.0) \
-I/usr/local/cuda/include \
-I/home/ubuntu/gst/Linux_for_Tegra/source/public \
-L/usr/local/cuda-10.2/targets/aarch64-linux/lib/ \
-L/usr/lib/aarch64-linux-gnu/tegra/ \
-L/usr/lib/aarch64-linux-gnu/ \
-L/home/ubuntu/bagheera2_essentials/lib_for_device/tegra/ \
-lcuda -lcudart -lnvbuf_utils -lnvbufsurface \
-lgstallocators-1.0 -lgstvideo-1.0 -lglib-2.0 -lEGL -lGLESv2 -lGL
```

### Receiver
```bash
g++ -g dmabuf_receiver_test_app.cpp -o dmabuf_receiver_test_app \
$(pkg-config --cflags --libs gstreamer-1.0) \
-I/usr/local/cuda/include \
-I/home/ubuntu/gst/Linux_for_Tegra/source/public \
-L/usr/local/cuda-10.2/targets/aarch64-linux/lib/ \
-L/usr/lib/aarch64-linux-gnu/tegra/ \
-L/usr/lib/aarch64-linux-gnu/ \
-L/home/ubuntu/bagheera2_essentials/lib_for_device/tegra/ \
-lcuda -lcudart -lnvbuf_utils -lnvbufsurface \
-lglib-2.0 -lgstallocators-1.0 -lgstvideo-1.0 -lEGL -lGLESv2 -lGL
```

---

## Configuration

### Communication Paths (Pre-configured)
- **Socket**: `/tmp/fd-share-analytics.socket`
- **Shared Memory**: `/dev/shm/MSGQ/shmfile/CAM8`
- **Semaphore**: `/dev/shm/MSGQ/semfile/CAM8`

### Key Parameters
- **Camera**: DMS H.264 (`/dev/dms_h264`)
- **Resolution**: 1296x1296 NV12
- **Buffer Pool**: 15 DMA buffers
- **Frame Rate**: 10 fps

---

## Expected Output

### Publisher Terminal
```
Created file: /dev/shm/MSGQ/shmfile/CAM8
Created file: /dev/shm/MSGQ/semfile/CAM8
client_fd 5 connected to server
1640000000123: DMABUF FD 15 sent to analytics process.
1640000000156: DMABUF FD 16 sent to analytics process.
...
```

### Receiver Terminal
```
client_fd 4 connected to server
smb_id: 0, uid: 1, recv_dmabuf_fd: 15, imp_dmabuf_fd: 20, sent_dmabuf_fd: 15
smb_id: 1, uid: 2, recv_dmabuf_fd: 16, imp_dmabuf_fd: 21, sent_dmabuf_fd: 16
...
```

---

## Troubleshooting

| Issue | Solution |
|-------|----------|
| "Address already in use" | Stop bagheera: `systemctl stop bagheera` |
| No connection between apps | Check both apps are running and socket path matches |
| No output file | Verify `FILE_DUMP` macro enabled in receiver source |
| Permission denied | Run `chmod 777 /home/ubuntu/dmabuf/` |
| Missing libraries | Install: `apt install libgstreamer1.0-dev` |
| Bagheera won't restart | Check logs: `journalctl -u bagheera -n 50` |

---

## Alternative: Test with Bagheera

Instead of the test publisher, you can use bagheera as the sender:

1. Stop analytics service only: `systemctl stop analyticsService`
2. Ensure bagheera is running: `systemctl restart bagheera`
3. Run receiver: `./dmabuf_receiver_test_app`
4. Frames from bagheera will be processed and dumped

**Note**: Only ONE sender (bagheera OR test publisher) can bind to the socket at a time.