#include <sys/statvfs.h>

#include <log.h>
#include <config_parser.h>
#include <nd_file_utils.h>
#include <nd_task.h>
#include <system_utils.h>
#include <vector>
#include <sstream>
#include <pthread.h>
#include <storage_utils.h>
#include "service_utils.h"
#include <nd_factory.h>
#include <nd_cb_utils.h>
#include <svc_common.h>

using namespace std;
#define TAG "DISK"
#if defined(KRAIT) || defined(BAGHEERA2)
#define NO_SDCARD
#endif
extern ND_DeviceFactory *nd_device_obj;
static int poll_interval = 15*60; //default 15 mins
#if defined (KRAIT) || defined (KRAIT2)
int64_t CLEANUP_TRIGGER_ROOT = 512*1024*1024; //500MB
#else
int64_t CLEANUP_TRIGGER_ROOT = 1024*1024*1024; // 1 GB
#endif
extern int64_t CLEANUP_TRIGGER_LOG;
extern int64_t CLEANUP_TRIGGER_DATA;
static string SDCARD_MNT_PATH1 = "";

static int disk_mon_cleanup_timeout = 1 * 60;
static const string EMMC_ROOT_MOUNT_PATH = "/";
static string EMMC_DATA_MOUNT_PATH = "";
static string EMMC_DATA_LOOP_MOUNT_PATH = "";

extern NDService *nd_service_obj; //nd service object, to detect critical Errors which will be send to Health stats and cloud
typedef const enum cleanup_partition {
    ROOT = 0,
    DATA,
    PARTITION_COUNT
} cleanup_partition_t;

static const string get_mount_path (cleanup_partition_t partition_type){
    string mount_path = EMMC_ROOT_MOUNT_PATH;
    switch (partition_type){
        case DATA:
            mount_path = EMMC_DATA_MOUNT_PATH;
            break;
        case ROOT:
            mount_path = EMMC_ROOT_MOUNT_PATH;
            break;
        default:
            LOG_E(TAG, "%s: invalid partition type provided %d, using root", __func__, partition_type);
            
    }
    return mount_path;
}

std::vector<std::pair<string, uint32_t>> unknown_files_to_check = {
                                                            {"/data/USB_logs.txt", (10 * 1024 * 1024) },
                                                            {"/data/NMEA_data.txt", (10 * 1024 * 1024) },
                                                            {"/data/USt", (10 * 1024 * 1024) }
};
static const string cleanup_cmds_root[] = {
        // keep total of 20 MB from both the places /var/log and /var/log/nd_archive/syslog;from 25MB the quota is reduced to 20MB because
        //we are now keeping additional main syslog file as gz (total 25 MB is still there;)
        "find /var/log -type f \\( -name '*syslog*.gz' \\) -printf '%T@ %p %s\\n' | sort -nr | awk 'BEGIN { total = 0 } { total += $3; if (total > 20 * 1024 * 1024) { print $2 } }' | xargs rm -f",
        // truncate all files except all syslog gz files in /var/log and /var/log/nd_archive/syslog
#ifdef BAGHEERA2
        //bagheera2, bagheera3 and octo devices will use this command
        "logrotate -f /etc/logrotate.d/rsyslog", //rotate logs manually to preserve the main syslog file as gz
#else
        //krait devices will use this command
        "logrotate -f /etc/logrotate.d/syslog_rotate.conf",
#endif
        "find /var/log \\( -path /var/log/nd_archive/syslog -o -name 'syslog*.gz' \\) -prune -o -type f -print0 | xargs -0 truncate -s0", //in shield, keep_alive_manager code
        //in line no 826 we are renaming and moving syslog files of the format syslog*.gz only,if any one mistakenly places any other file in /var/log/nd_archive/syslog then
        //that will not be truncated
        "find /var/log -maxdepth 1 -type f -name '*.gz' ! -name 'syslog*.gz' -exec rm -f {} +", // remove all gz files from /var/log except syslog*.gz
        "rm -rf /var/log/*.1",
#ifdef BAGHEERA2
        "find /var/log -maxdepth 1 -type f -name '*log*' ! -name 'syslog*' -exec truncate -s 0 {} +", // this is a redundant operation and needs to be removed
        "find /home/ubuntu/.nddevice/log/ -type f -size +1M -name \"*log*\" -delete",
        "for dir in /home/ubuntu/.nddevice/log/[^.]*/; do ls -t \"${dir}\" | tail -n +5 | xargs -rI {} rm -- \"${dir}{}\"; done",
        "find /home/ubuntu/.nddevice/db/ -type f -name \"*.db\" -size +10000k -delete",
        "find /home/ubuntu/.nddevice/inertial_obs_temp -name \"*.json\" -type f -delete",
        "find /home/ubuntu/.nddevice/inertial_obs_temp -name \"*.jpg\" -type f -delete",
        "echo 0 > /home/ubuntu/.nddevice/log/inertialLineNo.txt",
        "echo 0 > /home/ubuntu/.nddevice/log/visionLineNo.txt",
        "echo 0 > /home/ubuntu/.nddevice/log/newUploadLineNo.txt",
        "echo 0 > /home/ubuntu/.nddevice/log/deleteLineNo.txt",
        "rm -f /home/ubuntu/.nddevice/logfile.json",
        "rm -f /var/lib/logrotate/status",
        "find /var/lib/NetworkManager/ -type f -name \"*.lease\" -delete",
        "find /var/lib/NetworkManager/ -type f -name \"NetworkManager.state.*\" -delete",
        "find /home/ubuntu/.nddevice/log/archive -type f \\( -name '*.gz' -o -name '*.7z' -o -name '*.zip' \\) -printf '%T@ %p %s\\n' | sort -nr | awk 'BEGIN { total = 0 } { total += $3; if (total > 500 * 1024 * 1024) { print $2 } }' | xargs rm -f",
        "find /home/ubuntu/.nddevice/sign_crops -type f \\( -name '*.jpg' -o -name '*.7z' \\) -printf '%T@ %p %s\\n' | sort -nr | awk 'BEGIN { total = 0 } { total += $3; if (total > 100*1024*1024) { print $2 } }' | xargs rm -f",
        "find /media/data/nd_sdcard/ea/ -type f -printf '%T@ %p %s\\n' | sort -nr | awk 'BEGIN { total = 0 } { total += $3; if (total > " + to_string(EA_IMGS_FOLDER_SIZE) + ") { print $2 } }' | xargs rm -f",
#endif
        "rm -f /var/lib/logrotate.status"
};
static const string cleanup_cmds_data[] = {
        "find.findutils /home/ubuntu/.nddevice/log/ -type f -size +1M -delete",
        "for dir in /home/ubuntu/.nddevice/log/[^.]*/; do ls -t \"${dir}\" | tail -n +5 | xargs -rI {} rm -- \"${dir}{}\"; done",
        "find.findutils /home/ubuntu/.nddevice/db/ -type f -name \"*.db\" -size +10000k -delete",
        "find.findutils /home/ubuntu/.nddevice/inertial_obs_temp -name \"*.json\" -type f -delete",
        "find.findutils /home/ubuntu/.nddevice/inertial_obs_temp -name \"*.jpg\" -type f -delete",
#ifdef BAGHEERA2
        "find /media/data/ntdi_bag2_logs/misc_logs -type f -name \"system_crash_*\" -delete",
#endif
        "rm -f /data/nd_files/state_files/logfile.json",
#ifdef KRAIT
        "rm -rf /data/coredump/*",
        "find /data/nd_files/sign_crops -type f \\( -name '*.jpg' -o -name '*.7z' \\) -printf '%T@ %p %s\\n' | sort -nr | awk 'BEGIN { total = 0 } { total += $3; if (total > 100*1024*1024) { print $2 } }' | xargs rm -f",
        "find /data/nd_files/nd_sdcard/ea/ -type f -printf '%T@ %p %s\\n' | sort -nr | awk 'BEGIN { total = 0 } { total += $3; if (total >" + to_string(EA_IMGS_FOLDER_SIZE) + ") { print $2 } }' | xargs rm -f",
#endif
        "find /home/ubuntu/.nddevice/log/archive -type f \\( -name '*.gz' -o -name '*.7z' -o -name '*.zip' \\) -printf '%T@ %p %s\\n' | sort -nr | awk 'BEGIN { total = 0 } { total += $3; if (total > 500 * 1024 * 1024) { print $2 } }' | xargs rm -f"
};
static const string *cleanup_cmds[] = {cleanup_cmds_root, cleanup_cmds_data};
static string data_dir_size_cmd;
vector<string> stats_cmds;

static string kern_log1 = "/var/log/kern.log.1";
static string kern_log1_gz = "/var/log/kern.log.1.gz";
static string sys_log1 = "/var/log/syslog.1";
static string sys_log1_gz = "/var/log/syslog.1.gz";

static void startup_logcleanup() {
    //if syslog.1 is present
    if( file_is_present(sys_log1) ) {
        LOG_I(TAG, "Found syslog.1 file");
        if( file_is_present(sys_log1_gz) ) {
            LOG_I(TAG, "Found syslog.1.gz file. Probably corrupted. Deleting to make sure");
            file_delete(sys_log1_gz);
            // If /var/log/syslog.1 is present, only then check for /var/log/syslog.1.gz
        }
        file_create_gz (sys_log1);
        if( file_is_present(sys_log1_gz) ) {
            LOG_I (TAG, "Created new %s file", sys_log1_gz.c_str());
        }
        else {
            SVC_LOG_E (TAG, "Failed to create new %s file", sys_log1_gz.c_str());
        }
        file_delete(sys_log1);
    }
    //if kern.log.1 is present
    if( file_is_present(kern_log1) ) {
        LOG_I(TAG, "Found kern.log.1 file");
        //Most probably corrupt file, get rid of the log file and zip
        if( file_is_present(kern_log1_gz) ) {
            LOG_I(TAG, "Found kern.log.1.gz file. Probably corrupted. Deleting to make sure");
            file_delete(kern_log1_gz);
        }
        file_create_gz (kern_log1);
        if( file_is_present(kern_log1_gz) ) {
            LOG_I (TAG, "Created new %s file", kern_log1_gz.c_str());
        }
        else {
            LOG_E (TAG, "Failed to create %s file", kern_log1_gz.c_str());
        }
        file_delete(kern_log1);
        // If /var/log/kern.log.1 is present, only then check for /var/log/kern.log.1.gz
    }
}

static const int cleanup_cmds_sz[PARTITION_COUNT] = {sizeof(cleanup_cmds_root)/sizeof(string),sizeof(cleanup_cmds_data)/sizeof(string) };

//Run command
static bool run_cmd(string cmd) {
    FILE *in;
    char buff[512];

    if(!(in = popen(cmd.c_str(), "r"))){
        SVC_LOG_E(TAG, "Cannot execute command: %s", cmd.c_str());
        return false;
    }

    while(fgets(buff, sizeof(buff), in)!=NULL){
        SVC_LOG_I(TAG, "%s", buff);
    }

    pclose(in);
    return true;
}

//Cleanup routine
static void cleanup(const cleanup_partition_t partition_type) {
    struct statvfs buf;
    int64_t emmc_available_before = 0;
    int64_t emmc_capacity ;
    int64_t emmc_used ;
    stringstream emmc_cleanup_status;
    
    if ( partition_type < ROOT || partition_type > DATA ) {
        LOG_E(TAG, "Invalid partition type passed.");
        return;
    }
    const string partition_mount_path = get_mount_path(partition_type);
    //get free space on internal emmc
    int ret = statvfs(partition_mount_path.c_str(), &buf);
    if(ret == -1) {
       LOG_E(TAG, "DISKMON Failed to get space data of given file system, err: %s", strerror(errno));
    }
    else{
        emmc_capacity = (int64_t)buf.f_blocks * (int64_t)buf.f_bsize;
        emmc_used = (int64_t)(buf.f_blocks - buf.f_bfree) * (int64_t)buf.f_bsize;
        emmc_available_before = (int64_t)buf.f_bavail * (int64_t)buf.f_bsize;
        emmc_cleanup_status <<"Info::emmc_available_before: "<< emmc_available_before ;
        LOG_I(TAG, "statvfs before: emmc_capacity %lld, emmc_used %lld, %s ",
                    emmc_capacity, emmc_used, emmc_cleanup_status.str().c_str() );

    }

    //Execute cleanup commands given
    for(int i=0; i<cleanup_cmds_sz[partition_type]; i++) {
        LOG_I( TAG, "Executing cmd: %s", cleanup_cmds[partition_type][i].c_str() );
        run_cmd(cleanup_cmds[partition_type][i].c_str());
    }
    sleep(1);

    //delete all but last 2 sessions' files from /media/data/nd_sdcard/ when the external eMMC is unmounted
#ifdef BAGHEERA2
    string internal_sdcard_devnode = nd_device_obj->get_external_eMMC_dev_node();
    if(get_mount_status( EMMC_DATA_MOUNT_PATH, internal_sdcard_devnode) != MOUNTED) {
        //2 sessions generate 14 files including LD videos
        string cmd_delete_session_files = "ls -d -1tr /media/data/nd_sdcard/* | head -n -14 |  xargs -d '\n' rm -f";
        LOG_I( TAG, "Executing cmd: %s", cmd_delete_session_files.c_str() );
        run_cmd(cmd_delete_session_files.c_str());
    }
#endif

    //get free space on internal emmc
    ret = statvfs(partition_mount_path.c_str(), &buf);
    int64_t freed_bytes = -1 ;
    if(ret == -1) {
       LOG_E(TAG, "DISKMON Failed to get space data of given file system, err: %s", strerror(errno));
    }
    else{
        emmc_capacity = (int64_t)buf.f_blocks * (int64_t)buf.f_bsize;
        emmc_used = (int64_t)(buf.f_blocks - buf.f_bfree) * (int64_t)buf.f_bsize;
        int64_t emmc_available_after = (int64_t)buf.f_bavail * (int64_t)buf.f_bsize;
        emmc_cleanup_status <<",  emmc_available_after: "<< emmc_available_after ;
        LOG_I(TAG, "statvfs after: emmc_capacity %lld, emmc_used %lld, %s ",
                    emmc_capacity, emmc_used, emmc_cleanup_status.str().c_str() );
        freed_bytes = emmc_available_after -emmc_available_before ;
        if (freed_bytes > 0) {
            SVC_LOG_I( TAG, "CleanUP() freed %lld bytes", freed_bytes);
        } else {
            SVC_LOG_E( TAG, "CleanUP() failed to freeup space %lld bytes added since cleanup", -freed_bytes);
        }
    }
    nd_service_obj->send_err_msg(SM_I_SVC_EMMC_CLEANUP_INFO, freed_bytes, emmc_cleanup_status.str() );

}

//Print some stats regarding internal space
static void stats_space() {
    for(int i=0; i<stats_cmds.size(); i++) {
        SVC_LOG_I( TAG, "Executing cmd: %s", stats_cmds[i].c_str() );
        run_cmd(stats_cmds[i].c_str());
    }
}

bool check_data_free_space(const cleanup_partition_t partition_type, const int64_t cleanup_trigger)
{
    const string partition_mount_path = get_mount_path(DATA);
    string response = "";
    int64_t size = 0, size_in_byte = 0;
    if(!system_execute_with_resp("GET_LOG_DIR_SIZE", data_dir_size_cmd, response)) {
        LOG_E(TAG, "Failed to get log dir size");
        return false;
    }
    string_to_int64(response, size);
    size_in_byte = size * 1024;
    LOG_I(TAG,"/data consumed size(excluding nd_sdcard.img and nd_sdcard directory) : %lld (kb)", size);
    if(size_in_byte > cleanup_trigger) {
        return true;
    }
    return false;
}

static void check_and_clean(const cleanup_partition_t partition_type, const int64_t cleanup_trigger ){
    //Check for free space
    const string partition_string = get_mount_path(partition_type);
    bool clean_up = false;
    bool do_clean_up = false;
    int64_t fspace = file_getfreespace(partition_string);
    
    LOG_I(TAG, "Free space at %s : %lld", partition_string.c_str(), fspace);

    if(partition_type == DATA) {
        //This is to limit log folder size post removing loopmount
        clean_up = check_data_free_space(partition_type, CLEANUP_TRIGGER_LOG);
    }
    //If freespace less than trigger, then do cleanup
    if(fspace <= cleanup_trigger) {
        do_clean_up = true;
        LOG_E(TAG, "Triggering cleanup - available space is below %lld", cleanup_trigger);
    } else if(clean_up == true) {
        do_clean_up = true;
        LOG_E(TAG, "Triggering cleanup - /data has consumed space more than %lld", CLEANUP_TRIGGER_LOG);
    }

    if(do_clean_up == true) {
        cleanup(partition_type);
        fspace = file_getfreespace(partition_string);
        LOG_I(TAG, "Free space after cleanup : %lld", fspace);
    }

}

static void checkAndRemoveUnknownFiles() {
    string err_msg = "";
    for (const auto& file : unknown_files_to_check) {
        struct stat st;
        if (stat(file.first.c_str(), &st) != 0) {
            continue;
        }

        uint64_t size = static_cast<uint64_t>(st.st_size);

        err_msg.clear();
        int size_to_send = size/(1024*1024); //converting size to MB
        if (size > file.second) {
            LOG_E(TAG, "Deleting: %s : Size : %lld(bytes) : exceeds : %lu(bytes)",  file.first.c_str(), size, file.second);
            if (remove(file.first.c_str()) != 0) {
                perror(("Failed to delete: " + file.first).c_str());
                err_msg = "Identified-delete fail " + file.first + ", size : " + to_string(size);
            } else {
                err_msg = "Deleting " + file.first + ", size : " + to_string(size);
            }
            nd_service_obj->send_err_msg(SM_I_SVC_EMMC_CLEANUP_INFO, size_to_send, err_msg);
        } else {
            LOG_E(TAG,"File is present and size is within limit: %s (size : %lu(bytes))", file.first.c_str(), size);
        }
    }
}

static void check_and_remove_sdcard_img() {
    string file_path = nd_device_obj->get_external_eMMC_loop_mount_path();
    string command = "rm -f /data/nd_files/*.img*", resp = "", count = "";
    string df_command = "df -h";
    string ls_cmd = "";
    string ls_count_cmd = "ls -1 /data/nd_files/*.img*  | wc -l";
    string get_file_list = "ls -1tr /data/nd_files/*.img*", file_list = "";
    vector<string> file;

    system_execute_with_resp("file_list", get_file_list, file_list);
    
    file = split_by_delim(file_list, "\n");
    for( auto iter = file.rbegin(), end = file.rend(); iter!=end; ++iter ) {
        if((*iter).empty()) {
            continue;
        }
        if(access((*iter).c_str(), F_OK) != -1) {

            system_execute_with_resp("df_command", df_command, resp);
            LOG_E(TAG, "df -h output before removing nd_sdcard.img : %s", resp.c_str());
            resp.clear();

            ls_cmd = "ls -ltrh " + *iter + " | awk '{print $5}'";
            string err_msg = "";
            if(system_execute_with_resp("size_of_img", ls_cmd, resp)) {
                err_msg = "deleting nd_sdcard.img, size : " + resp;
            } else {
                err_msg = "deleting nd_sdcard.img";
            }
            if(system_execute_with_resp("count_of_img", ls_count_cmd, count)) {
                LOG_E(TAG, "Sending critical info : %s", err_msg.c_str());
                nd_service_obj->send_err_msg(SM_E_NDSDCARD_IMG_PRESENT, atoi(count.c_str()), err_msg);
            } else {
                LOG_E(TAG, "Sending critical info : %s", err_msg.c_str());
                nd_service_obj->send_err_msg(SM_E_NDSDCARD_IMG_PRESENT, NDService::UNUSED_ERR_AUX_CODE, err_msg);
            }

            LOG_E(TAG, "size of nd_sdcard.img : %s , count : %s", resp.c_str(), count.c_str());

            resp.clear();
            int retry_count = 3;
            while(retry_count) {

                LOG_E(TAG, "nd_sdcard.img is present");
                system_execute_with_resp("remove_ndsdcard.img", command, resp);
                LOG_E(TAG, "resp : %s", resp.c_str());
                int status = access((*iter).c_str(), F_OK);
                if(status != -1) {
                    LOG_E(TAG, "File status after delete : %d, retry count : %d", status, retry_count);
                    --retry_count;
                    continue;
                } else {
                    LOG_I(TAG, "nd_sdcard.img file delete successful");
                    break;
                }
            }

            system_execute_with_resp("df_command", df_command, resp);
            LOG_E(TAG, "df -h output after removing nd_sdcard.img : %s", resp.c_str());
            resp.clear();
        }
    }

    return;
}

//Polling thread that monitors disk usage
static void *fn_poll_thread(void *ptr) {


    startup_logcleanup();
    stats_space();

#if defined (KRAIT)
    //Adding this API call to check and remove unknown files present in /data
    //this has been added as a part of DT-2012
    checkAndRemoveUnknownFiles();
#endif

    while(1) {

#if defined (KRAIT)
        //check_and_remove_sdcard_img : this function added to remove
        //nd_sdcard.img file for the newly installed krait devices.
        check_and_remove_sdcard_img();    
        check_and_clean(ROOT, CLEANUP_TRIGGER_ROOT);
        check_and_clean(DATA, CLEANUP_TRIGGER_DATA);
#elif defined(BAGHEERA2)
               //Check for free space
        long fspace = file_getfreespace("/");
    
        LOG_I(TAG, "Free space : %ld", fspace);

        //If freespace less than trigger, then do cleanup
        if( fspace <= CLEANUP_TRIGGER_ROOT ) { 
            LOG_E(TAG, "Triggering cleanup free space:%ld < trigger:%ld", fspace, CLEANUP_TRIGGER_ROOT);
            cleanup(ROOT);
            fspace = file_getfreespace("/");
            LOG_I(TAG, "Free space after cleanup : %ld", fspace);
        }
#endif
        sleep(poll_interval);
    }
}

// diskmon_cleanup_process: a non static function to trigger cleanup_cmds
void diskmon_cleanup_process() {
    // Disk should be out of space for any new log messages at this point.
    // Fork a child process to do cleanup and wait in parent for child to complete.
    pid_t pid = fork();
    if (pid < 0 ){
        SVC_LOG_C (TAG, "diskmon_cleanup_process cannot create a child. Fork Status %d", pid);
        return;
    } else if (pid == 0) {
        cleanup(DATA);
        // Log files have been deleted thus even having a log here will not help untill next log rotation.
    } else {
        task_status_t cleanup_process_status = nd_set_timeout_for_task(pid, disk_mon_cleanup_timeout);
        if (cleanup_process_status != TASK_STATUS_SUCCESS)
        {
            SVC_LOG_E (TAG, "Trigger Cleanup task was not succesful, status %d", cleanup_process_status);
        }
    }
}

//Initialize structures
bool diskmon_init(int poll) {
    LOG_I(TAG, "Init diskmon");
    poll_interval = poll;

    pthread_t poll_thread;

    //Create polling thread
    if(pthread_create(&poll_thread, NULL, fn_poll_thread, NULL)) {
        SVC_LOG_E(TAG, "Error creating thread\n");
        return false;
    }

    return true;
}

void init_var_diskmon() {
    // Initialization of global variables

    EMMC_DATA_MOUNT_PATH = nd_device_obj->get_external_eMMC_phy_mount_path();
    SDCARD_MNT_PATH1 = nd_device_obj->get_external_eMMC_mount_path();
    EMMC_DATA_LOOP_MOUNT_PATH = nd_device_obj->get_external_eMMC_loop_mount_path();

    data_dir_size_cmd = "du -s " + EMMC_DATA_MOUNT_PATH + " --exclude "+ EMMC_DATA_LOOP_MOUNT_PATH + " --exclude /data/nd_files/nd_sdcard";

    // Storing stats commands in vector for stats execution
    stats_cmds.push_back("du -Sh --exclude " + SDCARD_MNT_PATH1 + " / 2> /dev/null | sort -rh 2> /dev/null | head -20");
    stats_cmds.push_back("find / -type f -not -path \""+ SDCARD_MNT_PATH1 + "*\" -exec du -Sh {} + 2> /dev/null | sort -rh 2> /dev/null | head -20");
    stats_cmds.push_back("ls -lhrt /var/log/");
#ifdef KRAIT
    stats_cmds.push_back("cat /var/lib/logrotate.status");
#elif defined(BAGHEERA2) || defined(BAGHEERA3)
    stats_cmds.push_back("cat /var/lib/logrotate/status");
    stats_cmds.push_back("tune2fs -l " + nd_device_obj->get_external_eMMC_dev_node());
    stats_cmds.push_back("tune2fs -l /dev/mmcblk0p1");
#endif
}
