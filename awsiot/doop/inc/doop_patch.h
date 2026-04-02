/*
This file is compact version of necessary functions from nd_core_utils/cpp/src/nd_file_utils.cpp and nd_core_utils/cpp/src/system_utils.cpp, 
with some necessary modifications to make doop binary standalone.
The TAG used at the time is "6.12"
*/

#ifndef DOOP_UTILS_H
#define DOOP_UTILS_H

#include <string>
#include <vector>

using namespace std;

// Function declarations for standalone doop utilities
// These functions are implemented in doop_patch.cpp

// Copied from nd_core_utils/cpp/src/nd_file_utils.cpp
bool file_is_present(string fname);
bool file_delete(string fname, bool use_unlink);
bool calculate_md5sum(const unsigned char* buffer, size_t len, string& checksum);
bool get_files(const string& path, vector<string>& vec);
bool createMemfdFromBuffer(const string& buffer, int& priv_key_fd, const string& label);

// Simple implementation to replace nd_core_utils/cpp/src/nd_file_utils.cpp version
bool file_mkdir(string dname, mode_t mode, bool change_owner);

// Copied from nd_core_utils/cpp/src/system_utils.cpp
bool system_execute_with_resp(string tag, string cmd, string &response, bool suppress_info_logs = false);

#endif // DOOP_UTILS_H
