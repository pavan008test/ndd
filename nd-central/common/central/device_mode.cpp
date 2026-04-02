#include "device_mode.h"
#include "system_utils.h"
#include "nd_time.h"
#include "nd_factory.h"
#include "nd_file_utils.h"
#include <set>
#include <cstring>
#include <sstream>
#include <fstream>
#include <mutex>
//#include <nd_power_utilis.h>

#define TAG "DM"
#define BAGHEERACONFIG_INI "/home/ubuntu/.nddevice/latest/bagheera_config.ini"

extern ND_DeviceFactory *nd_device_obj;

static const string privacy_state_file = "/dev/shm/nd_files_c/privacy_state.bin";
static const string ignition_state_file = "/dev/shm/nd_files_c/ignition_state.bin";

static const string ignition_status_file = "/dev/shm/nd_files_c/ignition_status.bin";
static const string dms_connection_status_file = "/dev/shm/nd_files_c/dms_connection_status.bin";

static const string fname_trip_string = "_part";
static map <string, device_mode_t> device_mode_info_global;
static std::mutex device_mode_mutex;
static bool enable_ignition_based_privacy = true;
bool button_long_press_required_for_privacy = true;
static string get_idx_from_fname (string fname);

static map <string, irled_mode_t> irled_mode_info_global;
static std::mutex irled_mode_mutex;

extern nd_central_ctx ctx;

static bool set_device_mode(string idx, const device_mode_t &new_entry)
{
    device_mode_t temp;

    std::lock_guard<std::mutex> lock(device_mode_mutex);
    map <string, device_mode_t> *device_mode_info;

    device_mode_info = &device_mode_info_global;

    if (idx == "") {
        LOG_I (TAG,"Index can't be empty");
        return false;
    }
    if (device_mode_info->count(idx) != 0) {
        temp = (*device_mode_info)[idx];
        //Reset only if nobody has referenced this entry yet.
        //Otherwise there will be conflicts.
        if (temp.ref_count == new_entry.ref_count) {
            LOG_I (TAG,"Resetting device mode information for %s",idx.c_str());
            LOG_I (TAG,"Old values: privacy: %d engine_idle:%d ref_count:%d",
                    temp.privacy_status, temp.engine_idle, temp.ref_count);
        }
        else {
            LOG_I (TAG,"Not resetting device mode information for %s because current refcount is %d"
                    "and new entry has refcount %d",idx.c_str(), temp.ref_count, new_entry.ref_count);
            return false;
        }
    }
    (*device_mode_info)[idx] = new_entry;
    LOG_I (TAG,"Set device_mode for %s as privacy: %d engine_idle:%d ref_count:%d",
            idx.c_str(), (*device_mode_info)[idx].privacy_status, (*device_mode_info)[idx].engine_idle, (*device_mode_info)[idx].ref_count);
    return true;
}

bool read_ignition_status_file(int &ignition_status)
{
    if (!file_is_present(ignition_status_file)) {
        LOG_I(TAG, "ignition_status_file %s NOT present;", ignition_status_file.c_str());
        return false;
    }
    LOG_E(TAG, "ignition_status_file %s present; must be an NDCentral restart", ignition_status_file.c_str());
    ifstream status_file_fp;
    status_file_fp.open(ignition_status_file.c_str());
    string line;
    if (status_file_fp.is_open()) {
        getline(status_file_fp, line);
        string_to_integer(line, ignition_status);
    }
    status_file_fp.close();
    LOG_I(TAG, "previous ignition status %d ", ignition_status);
    return true;
}

bool write_ignition_status_file(int ignition_status)
{
    ofstream status_file_fp;
    status_file_fp.open(ignition_status_file.c_str());

    if (status_file_fp.is_open()) {
        status_file_fp << ignition_status << endl;
    }
    status_file_fp.close();
    return true;
}

bool read_dms_connection_status_file(int &dms_connection_status)
{
    if (!file_is_present(dms_connection_status_file)) {
        LOG_I(TAG, "dms_connection_status_file %s NOT present;", dms_connection_status_file.c_str());
        return false;
    }
    LOG_E(TAG, "dms_connection_status_file %s present; must be an NDCentral restart", dms_connection_status_file.c_str());
    ifstream status_file_fp;
    status_file_fp.open(dms_connection_status_file.c_str());
    string line;
    if (status_file_fp.is_open()) {
        getline(status_file_fp, line);
        string_to_integer(line, dms_connection_status);
    }
    status_file_fp.close();
    LOG_I(TAG, "previous dms connection status %d ", dms_connection_status);
    return true;
}

bool write_dms_connection_status_file(int dms_connection_status)
{
    ofstream status_file_fp;
    status_file_fp.open(dms_connection_status_file.c_str());

    if (status_file_fp.is_open()) {
        status_file_fp << dms_connection_status << endl;
    }
    status_file_fp.close();
    return true;
}

bool is_dms_connection_status_file_present()
{
    if (!file_is_present(dms_connection_status_file)) {
        LOG_I(TAG, "dms_connection_status_file %s NOT present;", dms_connection_status_file.c_str());
        return false;
    }
    return true;
}

// returns true if /dev/shm/nd_files_c/privacy_state.bin file present else false
// updates prev_privacy with file content
bool read_privacy_state_file(bool &prev_privacy, int64_t &monotonic_time) {
    // if not sure; make privacy ON
    int temp_privacy = true;
    string privacy_state_file_local;

    privacy_state_file_local = privacy_state_file;

    if (!file_is_present(privacy_state_file_local)) {
        LOG_I(TAG, "privacy_state_file %s NOT present;", privacy_state_file_local.c_str());
        return false;
    }
    LOG_E(TAG, "privacy_state_file %s present; must be an NDCentral restart", privacy_state_file_local.c_str());
    ifstream state_file_fp;
    state_file_fp.open(privacy_state_file_local.c_str());
    string line;
    if (state_file_fp.is_open()) {
        getline(state_file_fp, line);
        string_to_integer(line, temp_privacy);

        getline(state_file_fp, line);
        string_to_int64(line, monotonic_time);
    }
    state_file_fp.close();
    prev_privacy = (temp_privacy != 0);

    LOG_I(TAG, "previous privacy is %d temp_privacy %d", prev_privacy, temp_privacy);
    return true;
}

bool write_privacy_state_file(int privacy)
{
    ofstream state_file_fp;
    state_file_fp.open(privacy_state_file.c_str());

    if (state_file_fp.is_open()) {
        state_file_fp << privacy << endl;
        state_file_fp << get_system_monotonic_time() << endl;
    }
    state_file_fp.close();
    return true;
}

// returns true if /dev/shm/nd_files_c/ignition_state.bin file present else false
// updates prev_ignition_state with file content
bool read_ignition_state_file(int &prev_ignition_state, int64_t &monotonic_time)
{
    string ignition_state_file_local;

    ignition_state_file_local = ignition_state_file;

    if(!file_is_present(ignition_state_file_local)) {
        LOG_I(TAG, "ignition_state_file %s NOT present;", ignition_state_file_local.c_str());
        return false;
    }
    LOG_E(TAG, "ignition_state_file %s present; must be an NDCentral restart", ignition_state_file_local.c_str());
    ifstream state_file_fp;
    state_file_fp.open(ignition_state_file_local.c_str());
    string line;
    if(state_file_fp.is_open()) {
        getline(state_file_fp, line);
        string_to_integer(line, prev_ignition_state);

        getline(state_file_fp, line);
        string_to_int64(line, monotonic_time);
    }
    state_file_fp.close();

    LOG_I(TAG, "previous ignition state is %d", prev_ignition_state);

    return true;
}

void write_ignition_state_file(int ignition_state)
{
    ofstream state_file_fp;
    state_file_fp.open(ignition_state_file.c_str());

    if(state_file_fp.is_open()) {
        state_file_fp << ignition_state << endl;
        state_file_fp << get_system_monotonic_time() << endl;
    }
    state_file_fp.close();
}

static bool update_privacy_array(privacy_state_t* states_array, int& states_len, int& privacy_status,
                                 privacy_state_t* new_states_array, const char* privacy_type_name,
                                 const string& idx, device_mode_t& new_entry,
                                 map<string, device_mode_t>* device_mode_info)
{
    if (states_len != 0) {
        device_mode_t present_entry = (*device_mode_info)[idx];
        LOG_I(TAG, "Updating device mode information (%s) for %s", privacy_type_name, idx.c_str());
        int temp_states_len = states_len;
        if ((temp_states_len < 1) || (temp_states_len > (MAX_PRIVACY_STATES - 1))) {
            LOG_E(TAG, "temp_states_%s_len %d ; Something wrong here", privacy_type_name, temp_states_len);
            return false;
        }

        LOG_I(TAG, "Old values: privacy_status_%s:%d engine_idle:%d ref_count:%d, event_state:%d, privacy_reason: %d",
            privacy_type_name, privacy_status, present_entry.engine_idle, present_entry.ref_count,
            states_array[temp_states_len-1].event_state,
            states_array[temp_states_len-1].privacy_reason);

        bool present_state = states_array[temp_states_len-1].event_state;
        privacy_reason_t present_reason = states_array[temp_states_len-1].privacy_reason;
        // If the new entry is same as the present entry, no need to append
        if (present_state == new_states_array[0].event_state && present_reason == new_states_array[0].privacy_reason) {
            LOG_I(TAG, "present_%s_state %d, present_reason %d and new_entry state %d, new_reason %d are equal, no need to append",
                privacy_type_name, present_state, present_reason, new_states_array[0].event_state, new_states_array[0].privacy_reason);
            return true;
        }
        LOG_C(TAG, "present_%s_state %d, present_reason %d and new_entry state %d, new_reason %d are NOT equal, state changed within session",
            privacy_type_name, present_state, present_reason, new_states_array[0].event_state, new_states_array[0].privacy_reason);
        if (present_state != new_states_array[0].event_state) {
            // making privacy_status mixed privacy because within session, there is a change in privacy state
            string_to_integer(PRIVACY_MIXED_STR, privacy_status);
        }
    } else {
        privacy_status = (int)new_states_array[0].event_state;
    }
    int index = states_len;
    states_array[index] = new_states_array[0];
    (*device_mode_info)[idx].engine_idle = new_entry.engine_idle;
    (*device_mode_info)[idx].ref_count = new_entry.ref_count;
    LOG_I(TAG, "states_len %d", states_len);
    LOG_I(TAG, "Appending %s device_mode for %s as privacy_status: %d engine_idle: %d ref_count: %d event_state: %d, event_time_monotonic: %lld, privacy_reason: %d",
        privacy_type_name, idx.c_str(), privacy_status, (*device_mode_info)[idx].engine_idle,
        (*device_mode_info)[idx].ref_count, states_array[index].event_state,
        states_array[index].event_time_monotonic, states_array[index].privacy_reason);
    states_len++;
    return true;
}

static bool set_device_mode_privacy(string idx, device_mode_t &new_entry)
{
    std::lock_guard<std::mutex> lock(device_mode_mutex);
    map <string, device_mode_t> *device_mode_info;

    device_mode_info = &device_mode_info_global;

    bool is_reason_offduty = false;
    bool is_reason_geofence = false;
    if (new_entry.individual_states_offduty[0].privacy_reason == REASON_OFFDUTY) {
        is_reason_offduty = true;
    }
    if (new_entry.individual_states_geofence[0].privacy_reason == REASON_GEOFENCE) {
        is_reason_geofence = true;
    }

    if (idx == "") {
        LOG_E(TAG,"Index can't be empty");
        return false;
    }

    if (device_mode_info->count(idx) == 0) {
        device_mode_t device_mode;
        device_mode.engine_idle = new_entry.engine_idle;
        device_mode.ref_count = new_entry.ref_count;
        device_mode.individual_states_len = 0;
        device_mode.individual_states_offduty_len = 0;
        device_mode.individual_states_geofence_len = 0;
        (*device_mode_info)[idx] = device_mode;
    }

    // Process based on privacy reason type
    if (is_reason_geofence) {
        return update_privacy_array((*device_mode_info)[idx].individual_states_geofence,
                                    (*device_mode_info)[idx].individual_states_geofence_len,
                                    (*device_mode_info)[idx].privacy_status_geofence,
                                    new_entry.individual_states_geofence,
                                    "geofence",
                                    idx, new_entry, device_mode_info);
    } else if (is_reason_offduty) {
        return update_privacy_array((*device_mode_info)[idx].individual_states_offduty,
                                    (*device_mode_info)[idx].individual_states_offduty_len,
                                    (*device_mode_info)[idx].privacy_status_offduty,
                                    new_entry.individual_states_offduty,
                                    "offduty",
                                    idx, new_entry, device_mode_info);
    } else {
        return update_privacy_array((*device_mode_info)[idx].individual_states,
                                    (*device_mode_info)[idx].individual_states_len,
                                    (*device_mode_info)[idx].privacy_status,
                                    new_entry.individual_states,
                                    "regular",
                                    idx, new_entry, device_mode_info);
    }
}

bool set_device_mode_for_fname(string fname_prefix, bool privacy, bool engine_idle, int num_cams_enabled, privacy_reason_t privacy_reason)
{
    string idx = get_idx_from_fname(fname_prefix);
    if (idx == "") {
        LOG_I(TAG, "%s:%d Invalid name :%s", __func__, __LINE__, idx.c_str());
        return false;
    }
    device_mode_t device_mode;
    device_mode.individual_states[0].privacy_reason = static_cast<privacy_reason_t>(-1);
    device_mode.individual_states_offduty[0].privacy_reason = static_cast<privacy_reason_t>(-1);
    device_mode.individual_states_geofence[0].privacy_reason = static_cast<privacy_reason_t>(-1);

    if(privacy_reason == REASON_GEOFENCE) {
	device_mode.individual_states_geofence[0].event_state = privacy;
        device_mode.individual_states_geofence[0].event_time_epoch = get_system_time();
        device_mode.individual_states_geofence[0].event_time_monotonic = get_system_monotonic_time();
        device_mode.individual_states_geofence[0].privacy_reason = privacy_reason;
    } else if (privacy_reason == REASON_OFFDUTY) {
        device_mode.individual_states_offduty[0].event_state = privacy;
        device_mode.individual_states_offduty[0].event_time_epoch = get_system_time();
        device_mode.individual_states_offduty[0].event_time_monotonic = get_system_monotonic_time();
        device_mode.individual_states_offduty[0].privacy_reason = privacy_reason;
    } else {
        device_mode.individual_states[0].event_state = privacy;
        device_mode.individual_states[0].event_time_epoch = get_system_time();
        device_mode.individual_states[0].event_time_monotonic = get_system_monotonic_time();
        device_mode.individual_states[0].privacy_reason = privacy_reason;
    }
    device_mode.engine_idle = engine_idle;
    device_mode.ref_count = num_cams_enabled;
    return set_device_mode_privacy(idx, device_mode);
}

bool update_device_mode_for_fname(string fname_prefix, device_mode_t device_mode) {
    string idx = get_idx_from_fname(fname_prefix);
    if (idx == "") {
        LOG_I(TAG, "%s:%d Invalid name :%s", __func__, __LINE__, idx.c_str());
        return false;
    }
    std::lock_guard<std::mutex> lock(device_mode_mutex);
    // Copy individual_states array element by element
    for (int i = 0; i < device_mode.individual_states_len; i++) {
        (device_mode_info_global)[idx].individual_states[i] = device_mode.individual_states[i];
    }
    // Copy individual_states_offduty array element by element
    for (int i = 0; i < device_mode.individual_states_offduty_len; i++) {
        (device_mode_info_global)[idx].individual_states_offduty[i] = device_mode.individual_states_offduty[i];
    }
    (device_mode_info_global)[idx].individual_states_len = device_mode.individual_states_len;
    (device_mode_info_global)[idx].privacy_status = device_mode.privacy_status;
    (device_mode_info_global)[idx].individual_states_offduty_len = device_mode.individual_states_offduty_len;
    (device_mode_info_global)[idx].privacy_status_offduty = device_mode.privacy_status_offduty;
    (device_mode_info_global)[idx].session_status = device_mode.session_status;

    LOG_I(TAG, "Updated device_mode for %s", idx.c_str());
    return true;
}

static bool peek_device_mode_internal (string idx, device_mode_t &t, map <string, device_mode_t> *device_mode_info)
{
    if (device_mode_info->count(idx) == 0)
    {
        LOG_I (TAG,"No entry present for %s",idx.c_str());
        return false;
    }
    else
    {
        t = (*device_mode_info)[idx];
        LOG_I (TAG,"device_mode for %s: privacy: %d engine_idle:%d ref_count:%d",
            idx.c_str(), t.privacy_status, t.engine_idle, t.ref_count);
        return true;
    }
}

static bool peek_device_mode (string idx, device_mode_t &t)
{
    std::lock_guard<std::mutex> lock(device_mode_mutex);
    map <string, device_mode_t> *device_mode_info;

    device_mode_info = &device_mode_info_global;

    return peek_device_mode_internal(idx, t, device_mode_info);
}

static bool get_device_mode (string idx, device_mode_t &t)
{
    std::lock_guard<std::mutex> lock(device_mode_mutex);
    map <string, device_mode_t> *device_mode_info;

    device_mode_info = &device_mode_info_global;

    if (peek_device_mode_internal (idx, t, device_mode_info))
    {
        (*device_mode_info)[idx].ref_count--;
        if ((*device_mode_info)[idx].ref_count == 0)
        {
            LOG_I (TAG,"Deleting entry for %s", idx.c_str());
            (*device_mode_info).erase (idx);
        }
    }
    return true;
}

bool fuse_privacy(privacy_source_t source, bool current_privacy, bool new_privacy) {

	// skip updating if there is no change in privacy
	if (current_privacy == new_privacy) {
        return current_privacy;
	}

    // skip updating if speed privacy feature is disabled
    if ((source == SOURCE_SPEED) && (current_privacy == true) && (ctx.privacy_params.privacy_deactivate_params.speed_based_privacy == false)) {
        return current_privacy;
    }

    // skip updating if speed privacy feature is disabled
    if ((source == SOURCE_SPEED) && (current_privacy == false) && (ctx.privacy_params.privacy_activate_params.speed_based_privacy == false)) {
        return current_privacy;
    }

    // skip updating if ignition privacy feature is disabled
    if ((source == SOURCE_IGNITION) && (current_privacy == true) && (ctx.privacy_params.privacy_deactivate_params.ignition_based_privacy == false)) {
        return current_privacy;
    }

    // skip updating if ignition privacy feature is disabled
    if ((source == SOURCE_IGNITION) && (current_privacy == false) && (ctx.privacy_params.privacy_activate_params.ignition_based_privacy == false)) {
        return current_privacy;
    }

    // skip updating if button privacy feature is disabled
    if ((source == SOURCE_BUTTON) && (current_privacy == false) && (ctx.privacy_params.privacy_activate_params.button_based_privacy == false)) {
        return current_privacy;
    }

    // Return new privacy
    return new_privacy;
}

static string get_idx_from_fname (string fname)
{
   size_t pos = fname.find_last_of ('_');
   if (pos == string::npos) {
       LOG_I(TAG,"%s:%d fname is not a valid:%s",__func__,__LINE__,fname.c_str());
       return "";
   }
   LOG_I (TAG,"fname is %s",fname.c_str());
   pos = fname.find_last_of ('/');
   if (pos != string::npos) {
       fname = fname.substr (pos+1);
   }

   size_t beg, end;
   //Skip all characters until end of trip string
   //fname can start with '<cam_num>_' or just '_'
   beg = fname.find (fname_trip_string);
   beg += fname_trip_string.length();
   fname = fname.substr (beg);

   beg = fname.find ("_");
   beg++;
   end = fname.find("_y") - beg;
   fname = fname.substr (beg, end);

   // LOG_I (TAG,"Index retrieved is %s",fname.c_str());
   return fname;
}

bool get_default_privacy_speed()
{
    Config_parser c (BAGHEERACONFIG_INI);
    bool get_override_val = true;
    bool is_val_overridden = false;

    if (c.getParseStatus() == false)
    {
        LOG_E (TAG,"Failed to parse bagheera_config.ini");
        return false;
    }
    else
    {
        if (c.isPresent ("privacy_mode", "default_privacy_v3"))
        {
            if ("true" == c.getConfig("privacy_mode", "default_privacy_v3", "", get_override_val, is_val_overridden))
            {
                LOG_I (TAG, "default privacy: ON");
                return true;
            }
            else
            {
                LOG_I (TAG, "default privacy: OFF");
                return false;
            }
        }
    }
    return true;
}

bool get_default_privacy_ignition()
{
    Config_parser c (BAGHEERACONFIG_INI);
    bool get_override_val = true;
    bool is_val_overridden = false;

    if (c.getParseStatus() == false)
    {
        LOG_E (TAG,"Failed to parse bagheera_config.ini");
        return false;
    }
    if ("false" == c.getConfig("privacy_mode", "button_long_press_for_privacy", "false",
                get_override_val, is_val_overridden)) {
        LOG_I(TAG, "button_long_press_for_privacy from bagheera_config is false");
        button_long_press_required_for_privacy = false;
    }
    else{
        LOG_I(TAG, "button_long_press_for_privacy from bagheera_config is true");
    }

    if ("false" == c.getConfig("privacy_mode", "enable_ignition_based_privacy", "true",
                 get_override_val, is_val_overridden)) {
        LOG_I(TAG, "enable_ignition_based_privacy set to false; meaning ignition based privacy is disabled");
        enable_ignition_based_privacy = false;
        return false;
    }
    LOG_I(TAG, "enable_ignition_based_privacy set to true; ignition based privacy is ACTIVATED");
    enable_ignition_based_privacy = true;
    return true;
}

bool update_engine_idle_for_fname (string fname_prefix, bool engine_idle, int num_cams_enabled)
{
    string idx = get_idx_from_fname (fname_prefix);
    if (idx == "") {
        LOG_I(TAG, "%s:%d Invalid name :%s", __func__, __LINE__, idx.c_str());
        return false;
    }
    device_mode_t device_mode;
    LOG_I (TAG, "update_engine_idle_for_fname: idx - %s", idx.c_str());
    if (!peek_device_mode (idx, device_mode))
    {
        LOG_E (TAG, "Couldn't update engine idle status, peek_device_mode false for %s", idx.c_str());
        return false;
    }

    if (device_mode.engine_idle == engine_idle)
    {
        LOG_E (TAG, "engine idle is already %d for %s. How did this happen?", engine_idle, idx.c_str());
        return true;
    }

    // For makng log analysis easier
    if (!engine_idle && (device_mode.ref_count < num_cams_enabled))
    {
        LOG_I (TAG, "Warning!! %d of %s files might not have copied because of engine idle",
                (num_cams_enabled - device_mode.ref_count), fname_prefix.c_str());
    }
    device_mode.engine_idle = engine_idle;
    LOG_I (TAG, "Updating engine idle status for %s", idx.c_str());
    return (set_device_mode(idx, device_mode));
}

void get_final_privacy_states_info(device_mode_t &device_mode) {
    // The time for first entry in the regular privacy states and offduty privacy and geofence states array might have
    // very small difference. To avoid any discrepancy, setting all times to minimum of the three
    int64_t start_time_monotonic = device_mode.individual_states[0].event_time_monotonic;
    int64_t start_time_epoch = device_mode.individual_states[0].event_time_epoch;
    
    if (device_mode.individual_states_offduty_len > 0) {
        start_time_monotonic = min(start_time_monotonic, device_mode.individual_states_offduty[0].event_time_monotonic);
        start_time_epoch = min(start_time_epoch, device_mode.individual_states_offduty[0].event_time_epoch);
    }
    
    if (device_mode.individual_states_geofence_len > 0) {
        start_time_monotonic = min(start_time_monotonic, device_mode.individual_states_geofence[0].event_time_monotonic);
        start_time_epoch = min(start_time_epoch, device_mode.individual_states_geofence[0].event_time_epoch);
    }

    device_mode.individual_states[0].event_time_monotonic = start_time_monotonic;
    device_mode.individual_states[0].event_time_epoch = start_time_epoch;
    
    if (device_mode.individual_states_offduty_len > 0) {
        device_mode.individual_states_offduty[0].event_time_monotonic = start_time_monotonic;
        device_mode.individual_states_offduty[0].event_time_epoch = start_time_epoch;
    }
    
    if (device_mode.individual_states_geofence_len > 0) {
        device_mode.individual_states_geofence[0].event_time_monotonic = start_time_monotonic;
        device_mode.individual_states_geofence[0].event_time_epoch = start_time_epoch;
    }

    // Handling case when enhanced privacy is enabled
    if (ctx.privacy_params.enhanced_privacy == true) {
        LOG_C(TAG, "Enhanced privacy path - merging geofence+offduty arrays (geofence_len=%d, offduty_len=%d)",
            device_mode.individual_states_geofence_len, device_mode.individual_states_offduty_len);
        // First merge geofence and offduty with proper priority (geofence > offduty)
        std::set<std::pair<int64_t, int64_t>> time_points_enhanced;
        for (int i = 0; i < device_mode.individual_states_offduty_len; i++) {
            time_points_enhanced.insert({device_mode.individual_states_offduty[i].event_time_monotonic, 
                                         device_mode.individual_states_offduty[i].event_time_epoch});
        }
        for (int i = 0; i < device_mode.individual_states_geofence_len; i++) {
            time_points_enhanced.insert({device_mode.individual_states_geofence[i].event_time_monotonic, 
                                         device_mode.individual_states_geofence[i].event_time_epoch});
        }

        device_mode_t device_mode_enhanced;
        int enhanced_index = 0, curr_offduty_idx = -1, curr_geofence_idx = -1;
        bool offduty_state = PRIVACY_OFF, geofence_state = PRIVACY_OFF;
        privacy_reason_t offduty_reason = REASON_NO_PRIVACY, geofence_reason = REASON_NO_PRIVACY;

        for (auto it = time_points_enhanced.begin(); it != time_points_enhanced.end(); ++it) {
            int64_t current_time_monotonic = it->first;
            int64_t current_time_epoch = it->second;

           
            if (curr_geofence_idx + 1 < device_mode.individual_states_geofence_len &&
                device_mode.individual_states_geofence[curr_geofence_idx + 1].event_time_monotonic == current_time_monotonic) {
                curr_geofence_idx++;
                geofence_state = device_mode.individual_states_geofence[curr_geofence_idx].event_state;
                geofence_reason = device_mode.individual_states_geofence[curr_geofence_idx].privacy_reason;
            }

             if (curr_offduty_idx + 1 < device_mode.individual_states_offduty_len &&
                device_mode.individual_states_offduty[curr_offduty_idx + 1].event_time_monotonic == current_time_monotonic) {
                curr_offduty_idx++;
                offduty_state = device_mode.individual_states_offduty[curr_offduty_idx].event_state;
                offduty_reason = device_mode.individual_states_offduty[curr_offduty_idx].privacy_reason;
            }

            // Priority: geofence > offduty, then apply enhanced privacy when both are OFF
            if (geofence_state == PRIVACY_ON) {
                device_mode_enhanced.individual_states[enhanced_index].event_state = geofence_state;
                device_mode_enhanced.individual_states[enhanced_index].privacy_reason = geofence_reason;
                LOG_I(TAG, "Enhanced merge at time %lld - GEOFENCE wins (geofence=%d, offduty=%d)",
                    current_time_monotonic, geofence_state, offduty_state);
            } else if (offduty_state == PRIVACY_ON) {
                device_mode_enhanced.individual_states[enhanced_index].event_state = offduty_state;
                device_mode_enhanced.individual_states[enhanced_index].privacy_reason = offduty_reason;
            } else {
                // Both geofence and offduty are OFF, apply enhanced privacy
                device_mode_enhanced.individual_states[enhanced_index].event_state = PRIVACY_OFF; // CHECK this
                device_mode_enhanced.individual_states[enhanced_index].privacy_reason = REASON_ENHANCED;
            }
            device_mode_enhanced.individual_states[enhanced_index].event_time_epoch = current_time_epoch;
            device_mode_enhanced.individual_states[enhanced_index].event_time_monotonic = current_time_monotonic;
            enhanced_index++;
        }


        // Copy merged result to device_mode.individual_states and update privacy status
	device_mode.privacy_status = device_mode_enhanced.individual_states[0].event_state;
        for (int k = 0; k < enhanced_index; k++) {
            device_mode.individual_states[k] = device_mode_enhanced.individual_states[k];
	    if(k > 0 && (device_mode.individual_states[k].event_state != device_mode.individual_states[k-1].event_state)) {
	        device_mode.privacy_status = PRIVACY_MIXED;	
	    }
        }
        device_mode.individual_states_len = enhanced_index;
        return;
    }

    // Handle case when enhanced privacy is not enabled
    LOG_C(TAG, "Regular privacy path - merging geofence+offduty+regular arrays (geofence_len=%d, offduty_len=%d, regular_len=%d)",
        device_mode.individual_states_geofence_len, device_mode.individual_states_offduty_len, device_mode.individual_states_len);
    std::set<std::pair<int64_t, int64_t>> time_points;
    for (int i = 0; i < device_mode.individual_states_len; i++) {
        time_points.insert({device_mode.individual_states[i].event_time_monotonic, device_mode.individual_states[i].event_time_epoch});
    }
    for (int i = 0; i < device_mode.individual_states_offduty_len; i++) {
        time_points.insert({device_mode.individual_states_offduty[i].event_time_monotonic, device_mode.individual_states_offduty[i].event_time_epoch});
    }
    for (int i = 0; i < device_mode.individual_states_geofence_len; i++) {
        time_points.insert({device_mode.individual_states_geofence[i].event_time_monotonic, device_mode.individual_states_geofence[i].event_time_epoch});
    }

    device_mode_t device_mode_temp;
    int temp_index = 0, curr_offduty_index = -1, curr_regular_index = -1, curr_geofence_index = -1;
    bool regular_privacy_state = PRIVACY_OFF, offduty_privacy_state = PRIVACY_OFF, geofence_privacy_state = PRIVACY_OFF;
    privacy_reason_t regular_privacy_reason = REASON_NO_PRIVACY, offduty_privacy_reason = REASON_NO_PRIVACY, geofence_privacy_reason = REASON_NO_PRIVACY;

    for (auto it = time_points.begin(); it != time_points.end(); ++it) {
        int64_t current_time_monotonic = it->first;
        int64_t current_time_epoch = it->second;

        if (curr_regular_index + 1 < device_mode.individual_states_len &&
            device_mode.individual_states[curr_regular_index + 1].event_time_monotonic == current_time_monotonic) {
                curr_regular_index++;
                regular_privacy_state = device_mode.individual_states[curr_regular_index].event_state;
                regular_privacy_reason = device_mode.individual_states[curr_regular_index].privacy_reason;
            }

        if (curr_offduty_index + 1 < device_mode.individual_states_offduty_len &&
            device_mode.individual_states_offduty[curr_offduty_index + 1].event_time_monotonic == current_time_monotonic) {
                curr_offduty_index++;
                offduty_privacy_state = device_mode.individual_states_offduty[curr_offduty_index].event_state;
                offduty_privacy_reason = device_mode.individual_states_offduty[curr_offduty_index].privacy_reason;
            }

        if (curr_geofence_index + 1 < device_mode.individual_states_geofence_len &&
            device_mode.individual_states_geofence[curr_geofence_index + 1].event_time_monotonic == current_time_monotonic) {
                curr_geofence_index++;
                geofence_privacy_state = device_mode.individual_states_geofence[curr_geofence_index].event_state;
                geofence_privacy_reason = device_mode.individual_states_geofence[curr_geofence_index].privacy_reason;
            }

        // Determine final privacy state based on priority: geofence > offduty > regular
        if (geofence_privacy_state == PRIVACY_ON) {
            device_mode_temp.individual_states[temp_index].event_state = geofence_privacy_state;
            device_mode_temp.individual_states[temp_index].privacy_reason = geofence_privacy_reason;
            LOG_I(TAG, "Regular merge at time %lld - GEOFENCE wins (geofence=%d, offduty=%d, regular=%d)",
                current_time_monotonic, geofence_privacy_state, offduty_privacy_state, regular_privacy_state);
        } else if (offduty_privacy_state == PRIVACY_ON) {
            device_mode_temp.individual_states[temp_index].event_state = offduty_privacy_state;
            device_mode_temp.individual_states[temp_index].privacy_reason = offduty_privacy_reason;
        } else {
            device_mode_temp.individual_states[temp_index].event_state = regular_privacy_state;
            device_mode_temp.individual_states[temp_index].privacy_reason = regular_privacy_reason;
        }
        device_mode_temp.individual_states[temp_index].event_time_epoch = current_time_epoch;
        device_mode_temp.individual_states[temp_index].event_time_monotonic = current_time_monotonic;
        temp_index++;
    }
    device_mode_temp.individual_states_len = temp_index;
    temp_index = 0;
    // Removing entries with duration less than 300 ms difference and adding final entries to device_mode.individual_states
    for (int i = 0; i < device_mode_temp.individual_states_len; i++) {
        if (i == 0 || i == device_mode_temp.individual_states_len - 1) {
            device_mode.individual_states[temp_index] = device_mode_temp.individual_states[i];
            temp_index++;
        } else {
            if ((device_mode_temp.individual_states[i+1].event_time_monotonic - device_mode_temp.individual_states[i].event_time_monotonic) >= 300) {
                device_mode.individual_states[temp_index] = device_mode_temp.individual_states[i];
                temp_index++;
            }
        }
    }
    device_mode.individual_states_len = temp_index;

    bool zero_offduty_privacy_present = false, one_offduty_privacy_present = false;
    bool zero_regular_privacy_present = false, one_regular_privacy_present = false;
    bool zero_geofence_privacy_present = false, one_geofence_privacy_present = false;
    bool offduty_overriden_by_geofence = false;
    for (int i = 0; i < device_mode.individual_states_len; i++) {
        if (device_mode.individual_states[i].event_state == PRIVACY_OFF) {
            zero_regular_privacy_present = true;
        } else {
            one_regular_privacy_present = true;
        }

	offduty_overriden_by_geofence = (i > 0 && device_mode.individual_states[i-1].privacy_reason == REASON_OFFDUTY && device_mode.individual_states[i].privacy_reason == REASON_GEOFENCE);
	if (device_mode.individual_states[i].privacy_reason == REASON_OFFDUTY) {
            one_offduty_privacy_present = true;
        } else if (!offduty_overriden_by_geofence) {
            zero_offduty_privacy_present = true;
        }

        if (device_mode.individual_states[i].privacy_reason == REASON_GEOFENCE) {
            one_geofence_privacy_present = true;
        } else {
            zero_geofence_privacy_present = true;
        }
    }

    if (zero_geofence_privacy_present && one_geofence_privacy_present) {
        string_to_integer(PRIVACY_MIXED_STR, device_mode.privacy_status_geofence);
        LOG_C(TAG, "Final privacy_status_geofence = MIXED (both ON and OFF states present)");
    } else if (one_geofence_privacy_present) {
        device_mode.privacy_status_geofence = PRIVACY_ON;
        LOG_C(TAG, "Final privacy_status_geofence = ON (only ON states present)");
    } else {
        device_mode.privacy_status_geofence = PRIVACY_OFF;
        LOG_C(TAG, "Final privacy_status_geofence = OFF (no geofensce events or only OFF states)");
    }

    if (zero_regular_privacy_present && one_regular_privacy_present) {
        string_to_integer(PRIVACY_MIXED_STR, device_mode.privacy_status);
    } else if (one_regular_privacy_present) {
        device_mode.privacy_status = PRIVACY_ON;
    } else {
        device_mode.privacy_status = PRIVACY_OFF;
    }

    if (zero_offduty_privacy_present && one_offduty_privacy_present) {
        string_to_integer(PRIVACY_MIXED_STR, device_mode.privacy_status_offduty);
    } else if (one_offduty_privacy_present) {
        device_mode.privacy_status_offduty = PRIVACY_ON;
    } else {
        device_mode.privacy_status_offduty = PRIVACY_OFF;
    }

    return;
}

bool get_device_mode_for_fname(string fname_prefix, device_mode_t &dev_mode)
{
    string idx = get_idx_from_fname (fname_prefix);
    if (idx == "") {
        LOG_I(TAG, "%s:%d Invalid name :%s", __func__, __LINE__, idx.c_str());
        return false;
    }
    return get_device_mode (idx, dev_mode);
}

bool peek_device_mode_for_fname (string fname_prefix, device_mode_t &dev_mode)
{
    string idx = get_idx_from_fname (fname_prefix);
    if (idx == "") {
        LOG_I(TAG, "%s:%d Invalid name :%s", __func__, __LINE__, idx.c_str());
        return false;
    }
    return peek_device_mode (idx, dev_mode);
}

bool set_irled_mode_for_fname(string fname_prefix, bool new_irled_status)
{
    string idx = get_idx_from_fname(fname_prefix);
    if (idx == "") {
        LOG_I(TAG, "%s:%d Invalid name :%s", __func__, __LINE__, idx.c_str());
        return false;
    }

    std::lock_guard<std::mutex> lock(irled_mode_mutex);
    map <string, irled_mode_t> *irled_mode_info = &irled_mode_info_global;

    if (irled_mode_info->count(idx) != 0) {
        irled_mode_t present_entry = (*irled_mode_info)[idx];

        int irled_states_len = present_entry.irled_states_len;
        if ((irled_states_len < 1) || (irled_states_len > (MAX_IRLED_STATES - 1))) {
            LOG_E(TAG, "irled_states_len %d; Something wrong here", irled_states_len);
            return false;
        }
        LOG_I(TAG, "irled_states_len %d", irled_states_len);
        bool present_irled_status = present_entry.irled_states[irled_states_len - 1].status;
        if (present_irled_status == new_irled_status) {
            LOG_I(TAG, "No new irled state added as there is no change in irled status for %s", idx.c_str());
            return true;
        }
        LOG_C(TAG, "Adding new irled state for %s as there is a change in irled status from %d to %d",
            idx.c_str(), present_irled_status, new_irled_status);

        (*irled_mode_info)[idx].irled_states[irled_states_len].status = new_irled_status;
        (*irled_mode_info)[idx].irled_states[irled_states_len].time = get_system_time();

        LOG_I(TAG, "appending irled_mode for %s as irled_status: %d, time: %lld",
            idx.c_str(), (*irled_mode_info)[idx].irled_states[irled_states_len].status,
            (*irled_mode_info)[idx].irled_states[irled_states_len].time);

        // making IRLED status as MIXED because there is a change in irled_state within the same session
        (*irled_mode_info)[idx].irled_status = IRLED_MIXED;
        (*irled_mode_info)[idx].irled_states_len++;

        return true;
    }

    (*irled_mode_info)[idx].irled_states[0].status = new_irled_status;
    (*irled_mode_info)[idx].irled_states[0].time = get_system_time();

    LOG_I(TAG, "Set irled_mode for %s as irled_status: %d, time: %lld",
            idx.c_str(), (*irled_mode_info)[idx].irled_states[0].status,
            (*irled_mode_info)[idx].irled_states[0].time);

    (*irled_mode_info)[idx].irled_status = new_irled_status ? IRLED_ON : IRLED_OFF;
    (*irled_mode_info)[idx].irled_states_len = 1;

    return true;
}

bool get_irled_mode_for_fname(string fname_prefix, irled_mode_t &irled_mode)
{
    string idx = get_idx_from_fname(fname_prefix);
    if (idx == "") {
        LOG_I(TAG, "%s:%d Invalid name :%s", __func__, __LINE__, idx.c_str());
        return false;
    }

    std::lock_guard<std::mutex> lock(irled_mode_mutex);
    map <string, irled_mode_t> *irled_mode_info = &irled_mode_info_global;

    if (irled_mode_info->count(idx) == 0) {
        LOG_I (TAG, "No entry present for %s", idx.c_str());
        return false;
    } else {
        irled_mode = (*irled_mode_info)[idx];
        LOG_I (TAG, "irled_mode for %s: irled_status: %d, irled_states_len: %d",
            idx.c_str(), irled_mode.irled_status, irled_mode.irled_states_len);

        LOG_I (TAG, "Deleting irled_mode entry for %s", idx.c_str());
        (*irled_mode_info).erase (idx);

        return true;
    }
}

 #ifdef TEST
int main()
{
    device_mode_t t;
    t.privacy_status = true;
    t.engine_idle = true;
    t.ref_count = 2;
    string s = "first";
    set_device_mode(s,t);
    t.ref_count = 1;
    set_device_mode(string("first"),t);
    t.privacy_status = false;
    t.engine_idle = false;
    t.ref_count = 2;
    set_device_mode("second",t);
    set_device_mode("",t);

    device_mode_t res;
    get_device_mode ("first",res);
    cout << "first: " <<res.privacy_status<<res.engine_idle<<res.ref_count<<endl;
    get_device_mode ("first",res);
    cout << "first: " <<res.privacy_status<<res.engine_idle<<res.ref_count<<endl;
    get_device_mode ("",res);
    cout << "first: " <<res.privacy_status<<res.engine_idle<<res.ref_count<<endl;
    get_device_mode ("second",res);
    cout << "second: " <<res.privacy_status<<res.engine_idle<<res.ref_count<<endl;
    get_device_mode ("dummy",res);
    //string s1 = get_idx_from_fname ("0_trip1_part1_91.0000_181.0000_0.0_1505256567708_y");
    string s1 = get_idx_from_fname ("/home/irisl/ND_INPUT/0_trip1_part1_91.0000_181.0000_0.0_1505256567708_y");
    LOG_I (TAG,"%s",s1.c_str());
}
#endif
