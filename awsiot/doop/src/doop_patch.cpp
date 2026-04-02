/*
 * doop_patch.cpp
 * 
 * Standalone implementation of required functions for doop binary
 * This file contains copied function definitions from various source files
 * to make doop binary standalone without external dependencies as much as possible.
 * 
 * The TAG used at the time is "6.12"
 * 
 * Function definitions should match that of nd_core_utils/cpp/src/nd_file_utils.cpp because of nd_file_utils.h is included in nd_auth_openssl.h used by doop.cpp
 * 
 */

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <mutex>
#include <errno.h>

// System includes
#include <sys/stat.h>
#include <sys/syscall.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <pwd.h>

// OpenSSL for MD5
#include <openssl/md5.h>

using namespace std;

// Constants and definitions
#define TAG "DOOP_PATCH"
#define LINE_LENGTH 1024

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001
#endif

// File type enumeration - copied from nd_file_utils.h
typedef enum file_type_e {
    FILE_TYPE_REGULAR,
    FILE_TYPE_DIRECTORY,
    FILE_TYPE_BLOCK,
    FILE_TYPE_SYMBOLIC_LINK,
    FILE_TYPE_CHARACTER_DEV,
    FILE_TYPE_FIFO,
    FILE_TYPE_SOCK,
    FILE_TYPE_MAX
} files_type_e;


// Global mutex for file operations
std::mutex file_delete_lock;


// Forward declarations
bool get_files_from_given_type(string path, files_type_e file_type, vector<string>& vec);
int memfd_create_wrapper(const string& name, unsigned int flags);

/*
 * file_is_present - Check if file exists
 * Copied from: nd_core_utils/cpp/src/nd_file_utils.cpp (line 52)
 */
bool file_is_present(string fname) {
    ifstream f(fname.c_str());
    return f.good();
}

/*
 * file_delete - Delete a file
 * Copied from: nd_core_utils/cpp/src/nd_file_utils.cpp (line 358)
 * Simplified version without time tracking and CSV logging
 */
bool file_delete(string fname, bool use_unlink) {
    if(!file_is_present(fname)) {
        cout <<  TAG << " cant delete, file not exists: " << fname << endl;
        return false;
    }
    
    std::lock_guard<std::mutex> lguard(file_delete_lock);
    int ret = remove(fname.c_str());
    
    if(ret != 0) {
        cerr <<  TAG << " Failed to delete file: " << fname << ", error: " << strerror(errno) << endl;
        return false;
    }
    
    cout <<  TAG << " Successfully deleted file: " << fname << endl;
    return true;
}

/*
 * calculate_md5sum - Calculate MD5 checksum from buffer
 * Copied from: nd_core_utils/cpp/src/nd_file_utils.cpp (line 691)
 */
bool calculate_md5sum(const unsigned char* buffer, size_t len, string& checksum) {
    int MAX_RESULT_SIZE = MD5_DIGEST_LENGTH * 2 + 1;
    char result[MAX_RESULT_SIZE];
    MD5_CTX c;
    unsigned char out[MD5_DIGEST_LENGTH];
    
    MD5_Init(&c);
    MD5_Update(&c, buffer, len);
    MD5_Final(out, &c);
    
    for(int n = 0; n < MD5_DIGEST_LENGTH; n++) {
        int characters_written = snprintf(result + n * 2, MAX_RESULT_SIZE - n * 2, "%02x", out[n]);
        
        if (characters_written < 0 || characters_written >= MAX_RESULT_SIZE - n * 2) {
            cerr <<  TAG << " Error in formatting hex string" << endl;
            return false;
        }
    }
    
    checksum = string(result);
    return true;
}

/*
 * get_files_from_given_type - Get files of specific type from directory
 * Copied from: nd_core_utils/cpp/src/nd_file_utils.cpp (line 722)
 */
bool get_files_from_given_type(string path, files_type_e file_type, vector<string>& vec) {
    struct dirent *dp;
    DIR *dir_p = opendir(path.c_str());

    if (dir_p == NULL) {
        cerr <<  TAG << " directory open failed: " << path << endl;
        return false;
    }

    if(!(file_type < FILE_TYPE_MAX)) {   
        cerr <<  TAG << " Unknown file type passed" << endl;
        closedir(dir_p);
        return false;
    }

    while ((dp = readdir(dir_p)) != NULL) {
        if(dp->d_type == DT_REG && file_type == FILE_TYPE_REGULAR) {
            vec.push_back(string(dp->d_name));
        }
        else if(dp->d_type == DT_DIR && file_type == FILE_TYPE_DIRECTORY) {
            if(strcmp(dp->d_name,".") != 0 && strcmp(dp->d_name,"..") != 0)
                vec.push_back(string(dp->d_name));
        }
        else if(dp->d_type == DT_BLK && file_type == FILE_TYPE_BLOCK) {
            vec.push_back(string(dp->d_name));
        }
        else if(dp->d_type == DT_LNK && file_type == FILE_TYPE_SYMBOLIC_LINK) {
            vec.push_back(string(dp->d_name));
        }
        else if(dp->d_type == DT_CHR && file_type == FILE_TYPE_CHARACTER_DEV) {
            vec.push_back(string(dp->d_name));
        }
        else if(dp->d_type == DT_FIFO && file_type == FILE_TYPE_FIFO) {
            vec.push_back(string(dp->d_name));
        }
        else if(dp->d_type == DT_SOCK && file_type == FILE_TYPE_SOCK) {
            vec.push_back(string(dp->d_name));    
        }
    }
 
    closedir(dir_p);
    return true;
}

/*
 * get_files - Get regular files from directory
 * Copied from: nd_core_utils/cpp/src/nd_file_utils.cpp (line 811)
 */
bool get_files(const string& path, vector<string>& vec) {
    return get_files_from_given_type(path, FILE_TYPE_REGULAR, vec);
}

/*
 * system_execute_with_resp - Execute system command and capture response
 * Copied from: nd_core_utils/cpp/src/system_utils.cpp (line 133)
 */
bool system_execute_with_resp(string tag, string cmd, string &response, bool suppress_info_logs = false) {
    char buffer[LINE_LENGTH] = {'\0'};
    FILE *fp = NULL;
    int ret = -1; // FAIL

    if (!suppress_info_logs) {
        cout <<  TAG << " system_execute_with_resp cmd: " << cmd << endl;
    }

    fp = popen(cmd.c_str(), "r");

    if (fp == NULL) {
        cerr <<  TAG << " Done with system_execute: popen execution return status : " 
             << strerror(errno) << "(" << errno << "), command exit code:(" << ret << ")" << endl;
        return false;
    }

    while (fgets(buffer, sizeof(buffer), fp)) {
        cerr << TAG << " " << buffer;
        response += string(buffer);
    }
    
    // Upon successful return, pclose() shall return the termination status($?) of the command language interpreter.
    // Otherwise, pclose() shall return -1 and set errno to indicate the error.
    ret = pclose(fp);
    
    if (WIFEXITED(ret) && !WEXITSTATUS(ret)) {
        if (!suppress_info_logs) {
            cout <<  TAG << " Done with system_execute: pclose execution return status : " 
                 << strerror(errno) << "(" << errno << "), command exit code:(" << ret << ")" << endl;
        }
    } else {
        cerr <<  TAG << " Done with system_execute: pclose execution return status : " 
             << strerror(errno) << "(" << errno << "), command exit code:(" << ret << ")" << endl;
    }
    
    return ((0 == ret) ? true : false);
}

/*
 * memfd_create_wrapper - Wrapper for memfd_create syscall
 * Copied from: nd_core_utils/cpp/src/nd_file_utils.cpp (line 1236)
 */
int memfd_create_wrapper(const string& name, unsigned int flags) {
    return syscall(SYS_memfd_create, name.c_str(), flags);
}

/*
 * createMemfdFromBuffer - Create memory file descriptor from buffer
 * Copied from: nd_core_utils/cpp/src/nd_file_utils.cpp (line 1240)
 */
bool createMemfdFromBuffer(const string& buffer, int& priv_key_fd, const string& label) {
    int fd = memfd_create_wrapper(label, MFD_CLOEXEC);
    if (fd == -1) {
        cerr <<  TAG << " memfd_create failed for " << label << ": " << strerror(errno) << endl;
        return false;
    }

    ssize_t written = write(fd, buffer.data(), buffer.size());
    if (written < 0 || static_cast<size_t>(written) != buffer.size()) {
        close(fd);
        cerr <<  TAG << " Failed to write to memfd for " << label << ": " << strerror(errno) << endl;
        return false;
    }

    priv_key_fd = fd;
    
    return true; 
}

/*
 * file_mkdir - Create directory with specified permissions
 * Simple implementation to replace nd_file_utils version
 */
bool file_mkdir(string dname, mode_t mode, bool change_owner) {
    int ret = mkdir(dname.c_str(), mode);
    if (ret == 0 || errno == EEXIST) {
        if (change_owner) {
            // Simple ownership change - change to ubuntu user if requested
            struct passwd *pwd = getpwnam("ubuntu");
            if (pwd != NULL) {
                chown(dname.c_str(), pwd->pw_uid, pwd->pw_gid);
            }
        }
        return true;
    }
    
    cerr << TAG << " Failed to create directory: " << dname << ", error: " << strerror(errno) << endl;
    return false;
}
