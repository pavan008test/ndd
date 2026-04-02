/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Arun V <arun.vj@netradyne.com>, October 2016
 */

#include <config.h>
#include <log.h>
#include "nd_msgq.h"
#include "nd_msg_types.h"
#include "nd_msg_utils.h"

#include <string>
#include <sstream>
#include <list>
#include <stdlib.h>
#include <stdio.h>

#include <signal.h>
#include <sys/time.h>

using namespace std;

#define TAG "UT"
#define Q_NAME "BAGHEERA"

static int handle_gps_reg(generic_msg_t *msg);
static bool handle_gps_unreg(generic_msg_t *msg);
static bool handle_fname_reg(generic_msg_t *msg);
static bool handle_fname_unreg(generic_msg_t *msg);

static void send_handle_gps_reg_res(generic_msg_t *msg, int res);
static void send_handle_gps_unreg_res(generic_msg_t *msg, bool res);
static void send_handle_fname_reg_res(generic_msg_t *msg, int res);
static void send_handle_fname_unreg_res(generic_msg_t *msg, bool res);

static string get_msgq_name();


static nd_msgq_t *server_q=NULL;
static int msg_idx = 0;
static int handles = 0;

struct gps_reg_t {
	string client;
	int handle;
};
static list<gps_reg_t> gps_regs;

struct fname_reg_t {
	string client;
	int handle;
};
static list<fname_reg_t> fname_regs;

void unit_test();

bool init_msgq() {

    //Create message queue
    server_q = nd_msgq_t::get_msgq( get_msgq_name(), nd_msgq_t::ND_MSGQ_SERVER );

    if( server_q == NULL ) {
	LOG_E(TAG, "Cannot create message queue");
	return false;
    }
    
    LOG_I(TAG, "Message queue created");

    return true;
}

void msg_loop() {
    nd_msgq_t::nd_msg_t *msg; 
    int res;

    unit_test();

    while(1) {
        if( (msg = server_q->receive( )) == NULL ) {
            LOG_E(TAG, "Receive message failed" );
            continue;
        }

	msg_type_t type = get_msg_type(msg->get_buffer());
	generic_msg_t *m = (generic_msg_t *)msg->get_buffer();
	
	if( m == NULL ) {
		LOG_E(TAG, "Received NULL message");
		continue;
	}

	LOG_I(TAG, "%d received", m->msg_type);

	switch( type ) {
		case REQ_GPS_REG:
			res = handle_gps_reg(m);
			send_handle_gps_reg_res(m, res);
			break;

		case REQ_GPS_UNREG:
			res = handle_gps_unreg(m);
			send_handle_gps_unreg_res(m, res);
			break;

		case REQ_FNAME_REG:
			res = handle_fname_reg(m);
			send_handle_fname_reg_res(m, res);
			break;

		case REQ_FNAME_UNREG:
			res = handle_fname_unreg(m);
			send_handle_fname_unreg_res(m, res);
			break;

		default:
			LOG_E(TAG, "Unknown message: %d", type);
	}
	
	delete msg;
    }
}

int main() {
	LOG_I(TAG, "starting...");

	init_msgq();
	msg_loop();

	return 0;
}

string get_msgq_name() {
	return Q_NAME;
}

int handle_gps_reg(generic_msg_t *msg) {
	req_gps_reg_msg_t *m = (req_gps_reg_msg_t *)msg;

	gps_reg_t reg;
	reg.handle = handles++;
	reg.client = m->client_id;

	if( reg.client == "" ) {
		LOG_E(TAG, "GPS registration failed: %s", reg.client.c_str());
		return -1;
	}

	gps_regs.push_back(reg);
	LOG_I(TAG, "GPS registration done: %s", reg.client.c_str());
	return reg.handle;
}

bool handle_gps_unreg(generic_msg_t *msg) {
	req_gps_unreg_msg_t *m = (req_gps_unreg_msg_t *)msg;

	list<gps_reg_t>::iterator iter=gps_regs.begin(), end=gps_regs.end();

	while(  iter != end ) {
		if( iter->client == m->client_id && iter->handle == m->handle ) {
			//Client and Handle matching, erase item			
			iter = gps_regs.erase(iter);
			LOG_I(TAG, "GPS unregistration success: %s %d", m->client_id, m->handle);
			return true;
		}
		iter++;
	}

	LOG_E(TAG, "GPS unregistration failed: %s %d", m->client_id, m->handle);
	return false; 
}

bool handle_fname_reg(generic_msg_t *msg) {
	req_fname_reg_msg_t *m = (req_fname_reg_msg_t *)msg;

	fname_reg_t reg;
	reg.handle = handles++;
	reg.client = m->client_id;

	if( reg.client == "" ) {
		LOG_E(TAG, "Fname registration failed: %s", reg.client.c_str());
		return -1;
	}

	fname_regs.push_back(reg);
	LOG_I(TAG, "Fname registration done: %s", reg.client.c_str());
	return reg.handle;
}

bool handle_fname_unreg(generic_msg_t *msg) {
	req_fname_unreg_msg_t *m = (req_fname_unreg_msg_t *)msg;

	list<fname_reg_t>::iterator iter=fname_regs.begin(), end=fname_regs.end();

	while(  iter != end ) {
		if( iter->client == m->client_id && iter->handle == m->handle ) {
			//Client and Handle matching, erase item			
			iter = fname_regs.erase(iter);
			LOG_I(TAG, "Fname unregistration success: %s %d", m->client_id, m->handle);
			return true;
		}
		iter++;
	}

	LOG_E(TAG, "Fname unregistration failed: %s %d", m->client_id, m->handle);
	return false; 
}

void send_handle_gps_reg_res(generic_msg_t *msg, int res) {
	req_gps_reg_msg_t *m = (req_gps_reg_msg_t *)msg;
	string dest = m->client_id;

	res_gps_reg_msg_t res_msg;

	res_msg.handle = res;
	if( res < 0 ) {
		res_msg.res = STATUS_ERR_OTHER;
	}
	else {
		res_msg.res = STATUS_OK;
	}

	send_msg( (generic_msg_t *)&res_msg, RES_GPS_REG, sizeof(res_msg), get_msgq_name(), dest, msg_idx++ );
}

void send_handle_gps_unreg_res(generic_msg_t *msg, bool res) {
	req_gps_unreg_msg_t *m = (req_gps_unreg_msg_t *)msg;
	string dest = m->client_id;

	res_gps_unreg_msg_t res_msg;

	res_msg.handle = res;
	if( res ) {
		res_msg.res = STATUS_OK;
	}
	else {
		res_msg.res = STATUS_ERR_OTHER;
	}

	send_msg( (generic_msg_t *)&res_msg, RES_GPS_UNREG, sizeof(res_msg), get_msgq_name(), dest, msg_idx++ );
	
}

void send_handle_fname_reg_res(generic_msg_t *msg, int res) {
	req_fname_reg_msg_t *m = (req_fname_reg_msg_t *)msg;
	string dest = m->client_id;

	res_fname_reg_msg_t res_msg;

	res_msg.handle = res;
	if( res < 0 ) {
		res_msg.res = STATUS_ERR_OTHER;
	}
	else {
		res_msg.res = STATUS_OK;
	}

	send_msg( (generic_msg_t *)&res_msg, RES_FNAME_REG, sizeof(res_msg), get_msgq_name(), dest, msg_idx++ );
}

void send_handle_fname_unreg_res(generic_msg_t *msg, bool res) {
	req_fname_unreg_msg_t *m = (req_fname_unreg_msg_t *)msg;
	string dest = m->client_id;

	res_fname_unreg_msg_t res_msg;

	res_msg.handle = res;
	if( res ) {
		res_msg.res = STATUS_OK;
	}
	else {
		res_msg.res = STATUS_ERR_OTHER;
	}

	send_msg( (generic_msg_t *)&res_msg, RES_FNAME_UNREG, sizeof(res_msg), get_msgq_name(), dest, msg_idx++ );
}

char *filename[] = {
        "/home/ubuntu/1_trip1_part1_91_181_0.0_1487397506631_y.mp4",
        "/home/ubuntu/1_trip1_part1_91_181_0.0_1487397506632_y.mp4"
};

int fcnt = 0;

void timer_handler (int signum) {
	static int count = 0;
	static int speed = 0;

	for( list<gps_reg_t>::iterator iter=gps_regs.begin(), end = gps_regs.end(); iter!=end; iter++) {

		res_gps_update_msg_t m;
		m.handle = iter->handle;
		m.lat = 0;
		m.lon = 0;
		m.alt = 0;
		m.speed = speed++; 

		bool res = send_msg( (generic_msg_t *)&m, RES_GPS_UPDATE, sizeof(m), get_msgq_name(), iter->client, msg_idx++ );
		LOG_I( TAG, "Sending message %d to %s status: %d", RES_GPS_UPDATE, (iter->client).c_str(), res);
	}

	count ++;

	if( count % 60 != 0 ) {
		return;
	}

	static char *fname = filename[fcnt%2];
        fcnt++;
        
	for( list<fname_reg_t>::iterator iter=fname_regs.begin(), end = fname_regs.end(); iter!=end; iter++) {

		res_fname_update_msg_t m;
		m.handle = iter->handle;
		strncpy(m.fname, fname, sizeof(m.fname)); 

		bool res = send_msg( (generic_msg_t *)&m, RES_FNAME_UPDATE, sizeof(m), get_msgq_name(), iter->client, msg_idx++ );
		LOG_I( TAG, "Sending message %d to %s status: %d", RES_FNAME_UPDATE, (iter->client).c_str(), res);
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
	LOG_I(TAG, "Starting timer");
}


