#include "nd_msgq.h"
#include "nd_msg_types.h"
#include "nd_msg_utils.h"

#include <signal.h>
#include <sys/time.h>

#include <iostream>
using namespace std;

#define Q_NAME "BAGHEERA"

void handle_req_gpsreg( req_speed_reg_msg_t *msg );
void unit_test();
void timer_handler (int signum);

req_speed_reg_msg_t *reg = NULL;
int speed = 0;
int msg_idx = 0;

int main() {

    //Create message queue
    nd_msgq_t::nd_msg_t *msg; 
    nd_msgq_t *server_q = nd_msgq_t::get_msgq( Q_NAME, nd_msgq_t::ND_MSGQ_SERVER);

    if( server_q == NULL ) {
	cout << "Cannot create message queue";
	return false;
    }

    while(1) {
        if( (msg = server_q->receive( )) == NULL ) {
            cout <<  "Receive message failed" << endl;
            continue;
        }

        generic_msg_t *m = (generic_msg_t *)msg->get_buffer();
	switch( get_msg_type(m) ) {
		case REQ_GPS_REG:
			handle_req_gpsreg( (req_speed_reg_msg_t *)m);
			break;
		default:
			cout << "Unknown message: " << get_msg_type(m) << " " << endl;
	}
    }

    delete msg;
}

void handle_req_gpsreg( req_speed_reg_msg_t *msg ) {
        cout << "Received registration" << endl;        
        reg = msg;

	res_gps_reg_msg_t res;
        res.handle = 0;
	res.res = STATUS_OK;

       	send_msg( (generic_msg_t *)&res, RES_GPS_REG , sizeof(res), Q_NAME , reg->client_id, msg_idx++ );

        unit_test();
}

void timer_handler (int signum) {
        if (reg != NULL) {
                res_gps_update_msg_t m;
                m.speed = speed++;
        	send_msg( (generic_msg_t *)&m, RES_GPS_UPDATE , sizeof(m), Q_NAME , reg->client_id, msg_idx++ );
                cout << "Sending speed update";
        }
}

void unit_test() {
	struct sigaction sa;
	struct itimerval timer;

	/* Install timer_handler as the signal handler for SIGVTALRM. */
	memset (&sa, 0, sizeof (sa));
	sa.sa_handler = &timer_handler;
	sigaction (SIGALRM, &sa, NULL);

	/* Configure the timer to expire after 250 msec... */
	timer.it_value.tv_sec = 1;
	timer.it_value.tv_usec = 0;
	/* ... and every 250 msec after that. */
	timer.it_interval.tv_sec = 1;
	timer.it_interval.tv_usec = 0;
	/* Start a virtual timer. It counts down whenever this process is
	executing. */
	setitimer (ITIMER_REAL, &timer, NULL);
}

