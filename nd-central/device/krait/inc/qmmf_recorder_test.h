/*
* Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#pragma once

#include <map>
#include <mutex>
#include <vector>

#include <cutils/properties.h>
#include <cutils/trace.h>
#include <linux/input.h>
#include <qmmf-sdk/qmmf_buffer.h>
#include <qmmf-sdk/qmmf_codec.h>

#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "MediaRecorder.h"

#if 0
#include "common/utils/qmmf_condition.h"
#include "recorder/test/samples/qmmf_recorder_test_wav.h"
#include "recorder/test/samples/qmmf_recorder_test_aac.h"
#include "recorder/test/samples/qmmf_recorder_test_amr.h"
#include "recorder/test/samples/qmmf_recorder_test_mpegh.h"
#endif

#include <qmmf-sdk/qmmf_recorder.h>
#include <qmmf-sdk/qmmf_recorder_params.h>
#include <qmmf-sdk/qmmf_recorder_extra_param_tags.h>

#include "qmmf_kinesis_interface.h"

//#define DEBUG
//Logging related defines
#define TEST_INFO(fmt, args...)  ALOGD(fmt, ##args)
#define TEST_ERROR(fmt, args...) ALOGE(fmt, ##args)
#ifdef DEBUG
#define TEST_DBG  TEST_INFO
#else
#define TEST_DBG(...) ((void)0)
#endif

// Enable this define to dump encoded bit stream data.
#define DUMP_BITSTREAM

#define TEXT_SIZE                   40

#define GSTREAMER_NAME_LENGTH_MAX (512)

using namespace qmmf;
using namespace recorder;
using namespace overlay;
using namespace android;
//using namespace qcamera;

#define MAX_NUM_CAMERAS 3

#define DEFAULT_DUMP_FRAME_FREQ  "200"

// Prop to enable YUV data dumping from YUV track
#define PROP_DUMP_YUV          "persist.qmmf.rec.test.dumpyuv"
// Prop to enable encoded bitstream data dumping
#define PROP_DUMP_BITSTREAM    "persist.qmmf.rec.test.dumpstrm"
// Prop to enable JPEG (BLOB) dumping
#define PROP_DUMP_JPEG         "persist.qmmf.rec.gtest.dumpjpeg"
// Prop to enable RAW Snapshot dumping
#define PROP_DUMP_RAW          "persist.qmmf.rec.gtest.dumpraw"
// Prop to set frequency of YUV data dumping
#define PROP_DUMP_FRAME_FREQ   "persist.qmmf.rec.test.dumpfreq"
// Prop to set scaler type
#define PROP_SCALER_TYPE       "persist.qmmf.rescaler.type"
// Prop to set UBWC stream
#define PROP_UBWC_ENABLE       "persist.qmmf.ubwcstream.enable"

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

#ifndef CLIP
#define CLIP(X, L, U) MIN(MAX((X), (L)), (U))
#endif

#define  VIDEO_FRAME_INFO_KEYFRAME 24

typedef int32_t qmmf_status_t;
typedef enum camera_pos
{                   
  CAMERA_POSITION_FRONT = 0,
  CAMERA_POSITION_BACK,
  CAMERA_POSITION_MAX
}camera_pos;

typedef struct cam_record_ctxt
{
  int            width;
  int            height;
  camera_pos      cam_pos;

  // realtime
  realtime_camera_config_t rt_config;
  int             YUV360P_SZ;
  int            analytics_width;
  int            analytics_height;
  int            analytics_width_driv;
  int            analytics_height_driv;

  unsigned long    total_frame_count;
  record_file_callback_t   record_cb;
  record_status_callback_t  event_cb;
  record_fname_cb_t         fname_cb;
  frame_write_callback_t      frame_write_cb;
  record_timestamp_callback_t timestamp_cb;
  record_data_prod_callback_t data_prod_cb;
  record_appsink_cb_t       appsink_cb;
  live_stream_frame_drop_cb_t live_stream_fram_drop_cb;
  qr_scan_cb_t qr_scan_cb;

  void            *app_cb;
  void            *app_fname;
  char            cur_file_name[GSTREAMER_NAME_LENGTH_MAX];
  char            cur_file_name_ld[GSTREAMER_NAME_LENGTH_MAX];
  char            next_file_name[GSTREAMER_NAME_LENGTH_MAX];  
  unsigned int     cam_session_no;

  volatile  uint64_t  session_epoch_time;
  volatile  uint64_t  session_start_pts;
  volatile  uint64_t  first_frame_pts;

  // LD parameters
  volatile  uint64_t  session_epoch_time_ld;
  volatile  uint64_t  session_start_pts_ld;

  uint64_t            cam_frame_count;
  uint64_t            session_frame_count;
  pthread_mutex_t     cam_frame_mutex;
  uint64_t            rt_frame_timestamp;
  pthread_mutex_t     rt_timestamp_mutex;
//  volatile  int       first_session;

  // RT shared memory specific
  NdSharedMemoryWriter *shm_writer;
  void                 *frame_data_ptr;
  int                  shm_buffers;
  pthread_mutex_t      shm_writer_mutex;
  string               shm_writer_name;

  aws_kinesis_stream_info_t kinesis_stream_info;
  volatile int        kinesis_req_cam_id;
  bool                privacy;
  bool                blackout_hd;
  bool                blackout_ld;
  bool                is_I_frame;
  bool                prevFrameState;
  bool                ld_enabled;
} cam_record_ctxt;

static char log_tag[CAMERA_POSITION_MAX][GSTREAMER_NAME_LENGTH_MAX] = {0};

int32_t init_params(int cam_num, realtime_camera_config_t* rt_config);

enum class TrackType {
  kNone,
  kVideoYUV,
  kVideoAVC,
  kVideoHEVC
};

struct TrackInfo_t {
  uint32_t  width;
  uint32_t  height;
  float     fps;
  float     focal_length;
  TrackType track_type;
  int32_t   ltr_count;
  uint32_t  session_id;
  uint32_t  track_id;
  int32_t   camera_id;
  uint32_t  low_power_mode;
  DeviceId  device_id;
  AVCParams avcparams;
  HEVCParams hevcparams;

  TrackInfo_t()
      : width(3840),
        height(2160),
        fps(30),
        focal_length(0),
        track_type(TrackType::kVideoAVC),
        ltr_count(0),
        session_id(-1),
        track_id(1),
        camera_id(0),
        low_power_mode(0),
        device_id(0),
        avcparams(),
        hevcparams() {}

  TrackInfo_t(uint32_t width, uint32_t height, float fps, float focal_length,
            TrackType track_type, int32_t ltr_count, uint32_t session_id,
            uint32_t track_id, int32_t camera_id, uint32_t low_power_mode,
            DeviceId device_id, AVCParams avcparams, HEVCParams hevcparams)
      : width(width),
        height(height),
        fps(fps),
        focal_length(focal_length),
        track_type(track_type),
        ltr_count(ltr_count),
        session_id(session_id),
        track_id(track_id),
        camera_id(camera_id),
        low_power_mode(low_power_mode),
        device_id(device_id),
        avcparams(avcparams),
        hevcparams(hevcparams) {}
};

class TestTrack;

 struct CameraInitInfo {
   int32_t                           camera_id;
   uint32_t                          camera_fps;
   uint32_t                          numStream;
   CameraInitInfo():
        camera_id(-1),
        camera_fps(0),
        numStream(0) {};
 };

#if 0
class TestInitParams {
public:
    int32_t                           num_cameras;
    uint32_t                          recordTime;
    std::vector<CameraInitInfo*>      cam_init_infos;
    TestInitParams() :
            num_cameras(2),
            recordTime(0){};

    ~TestInitParams() {
      for(std::vector<uint32_t>::size_type i = 0;
          i < cam_init_infos.size(); i++) {
        delete cam_init_infos.at(i);
      }
      cam_init_infos.clear();
    }
};
#endif
typedef struct StreamDumpInfo {
  VideoFormat   format;
  uint32_t      track_id;
  int32_t       width;
  int32_t       height;
  int32_t       cam_num;
} StreamDumpInfo;

class DumpBitStream {
 public:
  DumpBitStream() : file_fd_(-1) {};

  ~DumpBitStream() {};

  qmmf_status_t SetUp(const StreamDumpInfo& dumpinfo, TrackInfo_t track_info, uint64_t timestamp);

  void Close();

  qmmf_status_t Dump(const std::vector<BufferDescriptor>& buffers, cam_record_ctxt *context, TrackInfo_t track_info);

  int32_t file_fd_;

  std::string session_name;

  time_t session_time;

  //bool session_change;

  int32_t cam_num;
};

class RecorderTest {
 public:
  RecorderTest();

  ~RecorderTest();

  void RecorderEventCallbackHandler(EventType event_type, void *event_data,
                                    size_t event_data_size);

  void SessionCallbackHandler(EventType event_type,
                              void *event_data, size_t event_data_size);

  qmmf_status_t DumpFrameToFile(BufferDescriptor& buffer,
                           CameraBufferMetaData& meta_data, std::string& file_name, int cam_num);

  void CameraResultCallbackHandler(uint32_t camera_id,
                                   const CameraMetadata &result);

  Recorder& GetRecorder() { return recorder_; }
  bool is_dump_yuv_enabled_;
  bool is_dump_raw_enabled_;
  bool is_dump_bitstream_enabled_;
  bool is_dump_jpg_enabled_;
  uint32_t dump_frame_freq_;

  Recorder recorder_;

  uint32_t camera_id_;
  bool session_enabled_;

  std::mutex   error_lock_;
  bool         camera_error_;

  cam_record_ctxt cam_ctxt[2];

 private:

  std::map<uint32_t, std::vector<TestTrack*> > sessions_;
};

// Track can be types of Audio or Video, this class is responsible for creating
// tracks, setting required parameters, registering the data/event callback,
// dumping the data for verification purpose etc.
class TestTrack {

 public:
  TestTrack(RecorderTest* recorder_test);

  ~TestTrack();

  TrackType& GetTrackType() { return track_info_.track_type; }

  qmmf_status_t SetUp(TrackInfo_t& track_info);

  // Set up file to dump track data.
  qmmf_status_t Prepare(uint64_t timestamp = 0);

  // Clean up file.
  qmmf_status_t CleanUp();

 private:

  void TrackEventCB(uint32_t track_id, EventType event_type, void *event_data,
                    size_t event_data_size);

  void TrackDataCB(uint32_t track_id, std::vector<BufferDescriptor> buffers,
                   std::vector<MetaData> meta_buffers);

  TrackInfo_t track_info_;

  // One track can have multiple overlay objects.
  std::vector<uint32_t> overlay_ids_;

  RecorderTest* recorder_test_;

  DumpBitStream dump_bitstream_;
};

//  void* init_camera_record_platform(int cam_num);
//  int init_record_session_platform(int cam_num);
//  int start_record_session_platform(int cam_num);


