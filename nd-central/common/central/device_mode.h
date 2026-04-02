/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 */

#ifndef DEVICE_MODE_H
#define DEVICE_MODE_H

#include <stdint.h>
#include <string>
#include <cstdio>
#include <map>
#include <log.h>

#include <iostream>
#include <config_parser.h>
#include <vector>

#include "MediaRecorder.h"
#include "nd_central.h"

using namespace std;

static const int IRLED_OFF = 0;
static const int IRLED_ON = 1;
static const int IRLED_MIXED = 2;

static const string IRLED_OFF_STR = "0";
static const string IRLED_ON_STR = "1";
static const string IRLED_MIXED_STR = "2";

static const int PRIVACY_OFF = 0;
static const int PRIVACY_ON = 1;
static const int PRIVACY_MIXED = 2;

static const string PRIVACY_OFF_STR = "0";
static const string PRIVACY_ON_STR = "1";
static const string PRIVACY_MIXED_STR = "2";

typedef enum {
    SOURCE_SPEED = 0,
    SOURCE_IGNITION,
    SOURCE_BUTTON,
    SOURCE_MAX

} privacy_source_t;

#define MAX_IRLED_STATES 60
#define MAX_PRIVACY_STATES 60

typedef struct {
    bool event_state;
    int64_t event_time_epoch;
    int64_t event_time_monotonic;
    privacy_reason_t privacy_reason;
}privacy_state_t;

typedef struct {
    bool cam_privacy[DEVICE_CAMERA_POSITION_MAX];
    bool ext_cam_privacy;
    bool driveri_audio_privacy;
    bool ext_cam_audio_privacy;
    bool gps_privacy;
    bool off_duty_privacy;
    bool geofence_privacy;
    bool enhanced_privacy;
    bool save_user_alert_video;
    bool has_user_alert;
    bool upload_video[DEVICE_CAMERA_POSITION_MAX];
    bool upload_video_ext_cam;
}partial_privacy_params_t;

typedef struct {
    int edit_status_for_cams;
    int copy_status_for_cams;
    int remove_status_for_cams;
} session_status_t;

typedef struct {
    int privacy_status; // This is for entire session so will have values enabled(1), disabled(0) or mixed (2)
    privacy_state_t individual_states[MAX_PRIVACY_STATES];
    int individual_states_len;
    int privacy_status_offduty; // This is for entire session so will have values 1(entire session in offduty), 0(no offduty in session) or 2(mixed)
    privacy_state_t individual_states_offduty[MAX_PRIVACY_STATES];
    int individual_states_offduty_len;
    int privacy_status_geofence; // This is for entire session so will have values 1(entire session in geofence), 0(no geofence in session) or 2(mixed)
    privacy_state_t individual_states_geofence[MAX_PRIVACY_STATES];
    int individual_states_geofence_len;
    bool engine_idle;
    int ref_count;
    partial_privacy_params_t partial_privacy_params;
    session_status_t session_status;
}device_mode_t;

typedef struct {
    bool status; // possible values: 0 (OFF), 1 (ON)
    int64_t time; // approximately indicates the time at which IRLED transition occurs
} irled_state_t;

typedef struct {
    int irled_status; // This is for entire session, so will have values ON(1) or OFF(0) or MIXED(2)
    irled_state_t irled_states[MAX_IRLED_STATES]; // Worst-case scenario, we can have 60 IRLED transitions in a session
    int irled_states_len;
} irled_mode_t;

bool get_default_privacy_speed();
bool get_default_privacy_ignition();
bool fuse_privacy(privacy_source_t source, bool current_privacy_val, bool new_privacy_val);
bool get_device_mode_for_fname (std::string fname_prefix, device_mode_t &dev_mode);
bool peek_device_mode_for_fname (std::string fname_prefix, device_mode_t &dev_mode);
bool update_engine_idle_for_fname (std::string fname_prefix, bool engine_idle, int num_cams_enabled);
bool set_device_mode_for_fname (std::string fname_prefix, bool privacy, bool engine_idle, int num_cams_enabled, privacy_reason_t privacy_reason);
bool update_device_mode_for_fname(string fname_prefix, device_mode_t device_mode);

bool set_irled_mode_for_fname(string fname_prefix, bool new_irled_status);
bool get_irled_mode_for_fname(std::string fname_prefix, irled_mode_t &irled_mode);

/* These APIs are to handle the bagheera service restart cases, where we can get the
 * ignition, privacy and dms connection status after restart */
bool read_ignition_state_file(int &prev_ignition_state, int64_t &monotonic_time);
void write_ignition_state_file(int ignition_state);
bool read_privacy_state_file(bool &prev_privacy, int64_t &monotonic_time);
bool write_privacy_state_file(int privacy);
bool read_ignition_status_file(int &ignition_status);
bool write_ignition_status_file(int ignition_status);
bool read_dms_connection_status_file(int &dms_connection_status);
bool write_dms_connection_status_file(int dms_connection_status);
bool is_dms_connection_status_file_present();
void get_final_privacy_states_info(device_mode_t &device_mode);

#endif
