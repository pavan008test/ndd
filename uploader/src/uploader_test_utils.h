#ifndef UPLOADER_TST_H
#define UPLOADER_TST_H

static const char *TAG_TEST="UPL_TST";

void log_upload_msg_size(){
    LOG_I(TAG_TEST,"Size of req_upload_msg_t = %d bytes", sizeof(req_upload_msg_t));

}

//log upload_msg
void log_upload_msg(req_upload_msg_t *msg) {
    LOG_I(TAG_TEST,"Print msg:\n~~~~");
    LOG_I(TAG_TEST,"msg_type = %d", msg->msg_type);
    LOG_I(TAG_TEST,"length = %d", msg->length);
    LOG_I(TAG_TEST,"client_id = %s", msg->client_id);
    LOG_I(TAG_TEST,"msg_idx = %d", msg->msg_idx);
    LOG_I(TAG_TEST,"res_reqd = %d", msg->res_reqd);
    LOG_I(TAG_TEST,"level = %d", msg->level);
    LOG_I(TAG_TEST,"payload_size = %d", msg->payload_size);
    LOG_I(TAG_TEST,"fname = %s", msg->fname);
    LOG_I(TAG_TEST,"json_fname = %s", msg->json_fname);
    LOG_I(TAG_TEST,"req_id = %d", msg->req_id);
    LOG_I(TAG_TEST,"retry_count = %d", msg->retry_count);
    LOG_I(TAG_TEST,"request_time = %llu", msg->request_time);
    LOG_I(TAG_TEST,"trim = %d", msg->trim);
    LOG_I(TAG_TEST,"upload_observation = %d", msg->upload_observation);
    LOG_I(TAG_TEST,"upload_audio = %d", msg->upload_audio);
    LOG_I(TAG_TEST,"start_sec = %d", msg->start_sec);
    LOG_I(TAG_TEST,"end_sec = %d", msg->end_sec);
    LOG_I(TAG_TEST,"part_id = %d", msg->part_id);
    LOG_I(TAG_TEST,"ib_alert = %d", msg->ib_alert);
    LOG_I(TAG_TEST,"quality = %d", msg->quality);
    LOG_I(TAG_TEST,"retry_count = %d", msg->retry_count);
    LOG_I(TAG_TEST,"vod_id = %s", msg->vod_id);
    LOG_I(TAG_TEST,"cancelled = %d", msg->cancelled);
    LOG_I(TAG_TEST,"req_priority = %d", msg->req_priority);
    LOG_I(TAG_TEST,"~~~~\n");
}


//print vod queue
void print_queue(priority_queue<req_upload_msg_t*, vector<req_upload_msg_t*>, compare> *pq_vod)
{
    LOG_I(TAG_TEST, "print_queue");
    // Create a copy of the priority queue
    priority_queue<req_upload_msg_t*, vector<req_upload_msg_t*>, compare> pq_copy = *pq_vod;

    // Iterate and print elements of the copy
    LOG_I(TAG_TEST,"~~~~\n");
    while (!pq_copy.empty())
    {
        req_upload_msg_t *top_element = pq_copy.top(); // Get the top element of the copy
        LOG_I(TAG_TEST, "top_element->fname = %s priority = %d", top_element->fname, top_element->level);
        pq_copy.pop();                   // Remove the top element from the copy
    }
    LOG_I(TAG_TEST,"~~~~\n");

}

#endif
