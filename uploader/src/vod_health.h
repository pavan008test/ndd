#include <string>
#include "nd_msg_types.h"

using namespace std;

namespace vod_health {

struct vod_req_status_t {
    bool status;
    int64_t ts;
};

struct add_req_db_t {
    int64_t ts;
    int32_t rank;
};

struct upload_attempt_t {
    int64_t start_ts;
    bool status;
    int32_t retry_count;
    bool video_available;
    string api_resp_txt;
    int64_t end_ts;
};

struct ext_vod_req_t {
    int64_t ts;
};

struct ext_vod_ack_t {
    int64_t ts;
};

struct ext_vod_resp_t {
    int64_t ts;
    bool status;
    string reason;
    string rgb_analysis;
    int32_t is_vod_available;
};

struct vod_failure_t {
    int64_t ts;
    string reason;
};

struct vod_success_t {
    int64_t ts;
    int32_t retry_count;
};

struct pending_vod_count_t {
    int64_t ts;
    int32_t count;
};

enum vod_health_data_type {
    REQ_ADD_UPLOAD_Q = 0,
    UPLOAD_ATTEMPT,
    EXT_VOD_REQ,
    EXT_VOD_ACK,
    EXT_VOD_RESP,
    VOD_FAILURE,
    VOD_SUCCESS
};
class VodHealth {
    public:
        static bool send_vod_health(const req_upload_msg_t *vod_req,const vod_health_data_type type, void *data);
        static bool send_vod_count(const pending_vod_count_t *pending_vod_count);
};

} // namespace vod_health
