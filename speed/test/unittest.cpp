#include "nd_msgq.h"
#include "nd_msg_types.h"

#include <iostream>
using namespace std;

#define Q_NAME "UniUpload"

void handle_req_upload( req_upload_msg_t *msg );

int main() {

    //Create message queue
    nd_msgq_t *client_q = nd_msgq_t::get_msgq( Q_NAME, nd_msgq_t::ND_MSGQ_CLIENT);

    if( client_q == NULL ) {
	cout << "Cannot create message queue";
	return false;
    }

    cout << "Message Queue created" << endl;
    req_upload_msg_t msg;
    msg.msg_type = REQ_UPLOAD;
    msg.length = sizeof(msg);
    strcpy( msg.client_id, "AwsIot");
    msg.msg_idx = 1;
    msg.res_reqd = 0;
    strcpy(msg.fname, "fname.mp4");
    msg.json_data_len = 0;

    nd_msgq_t::nd_msg_t smsg((char *)&msg, sizeof(msg), false);
    client_q->send(smsg,nd_msgq_t::ND_MSG_MED);
    cout << "Message send" << endl;
    cout << "Exiting" << endl;


}

