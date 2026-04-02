
#include "nd_prop_utils.h"
#include <nd_cb_utils.h>
#include "circular_buffer.h"
#include "sdcard_recovery.h"
#include <sys/statvfs.h>

//#define SDCARD_FSCK
#define MAX_FILLING_LIMIT_TIER 4
#define NO_SDCARD

#ifdef SDCARD_FSCK
static const int FSCK_COMMAND_TASK_TIMEOUT = 120;
static const string sdcard_device_node = "/dev/mmcblk1p1";
#endif

extern ND_DeviceFactory *nd_device_obj;
extern NDService *nd_service_obj;
extern circular_buffer_vid *CIRC_BUFF_ctx ;
extern bool MOUNT_SDCARD ;
extern int64_t FILLING_LIMIT;
extern int64_t FILLING_LIMIT_PERCENT ;
extern int FILLING_LIMIT_TIER[MAX_FILLING_LIMIT_TIER] ;
extern int64_t RESERVED_SPACE;
extern int64_t MAX_EMMC_FREE_SPACE;

static const char *TAG="CB";
static const string DEF_INI_PERCENT_CIRC_BUFF = "90";
static bool sdcard_status = true;
constexpr int DEF_STORAGE_TIER = 2;
// If we want to improve waf for 128GB as well, the filling percent can be reduced through config
constexpr int64_t DEF_FILLING_PERCENT = 90;
constexpr int64_t DEF_FILLING_PERCENT_OPTIMUM_WAF = 65;
constexpr int64_t MAX_FILLING_PERCENT_OPTIMUM_WAF = 85;
bool is_filling_percent_overridden = false;

#define KILOBYTE  1024
constexpr int64_t MEGABYTE = KILOBYTE * KILOBYTE;
constexpr int64_t GIGABYTE = KILOBYTE * MEGABYTE;
constexpr int64_t MISC_BACKUP_SPACE = 500 * MEGABYTE;  // mainly for EA_IMGS_FOLDER_SIZE 
constexpr int64_t XATTR_BACKUP_SPACE = 250 * MEGABYTE; // leave some space for xattr overheads, each files attribute are getting stored in external blocks (4KB), which can add up to significant space
constexpr int64_t LOG_OVERLAY_BACKUP_SPACE = 1 * GIGABYTE; // Backup space for log overlay, as logs are stored in external emmc
constexpr int64_t OBS_BACKUP_SPACE        = 500 * MEGABYTE;
constexpr int64_t AUTOCAM_BACKUP_SPACE    = 500 * MEGABYTE;
constexpr int64_t OTA_DWNLD_BACKUP_SPACE  = 1 * GIGABYTE;

constexpr int64_t TOTAL_BACKUP_SPACE =
    FIVE_SESSION_BACKUP_SPACE +
    XATTR_BACKUP_SPACE +
    MISC_BACKUP_SPACE +
    LOG_OVERLAY_BACKUP_SPACE +
    OBS_BACKUP_SPACE +
    AUTOCAM_BACKUP_SPACE +
    OTA_DWNLD_BACKUP_SPACE;

#ifdef SDCARD_FSCK
void run_fsck_command() {
    string cmd = "fsck -t ext4 -nf " + sdcard_device_node;
    string resp = "";

    bool ret = system_execute_with_resp("RUN_FSCK", cmd, resp);
    if(!ret) {
        LOG_E(TAG, "failed to execute fsck run command");
        exit(1);
    }

    LOG_I(TAG, "resp: %s", resp.c_str());

    // parse the fsck response 
    stringstream ss(resp);
    string prev_line = "", line = "";

    string error_msg = "";
    int aux_code = 1;

    while(std::getline(ss, line, '\n')) {
        if(line == "") {
            continue;
        }

        size_t pos = line.find("Fix?");

        if(pos == string::npos) {
            prev_line = line;
            continue;
        }

        if(pos == 0) {
            error_msg = prev_line;
        } else {
            error_msg = line.substr(0, pos);
        }

        LOG_I(TAG, "sending fsck error: %s to sm", error_msg.c_str());
        nd_service_obj->send_err_msg(SM_E_CB_FSCK_ERRORS, aux_code, error_msg);

        // increment aux code for every error message we sent so that sm do not drop back to back messages
        aux_code++;
    }

    if(aux_code == 1) {
        LOG_I(TAG, "No fsck errors detected");
    }

    exit(0);
}


void fork_fsck_run_process() {

    pid_t pid = fork();

    if (pid < 0) {
        LOG_E(TAG, "Failed to fork process for fsck command run");
        return;
    }

    if (pid == 0){
        run_fsck_command();
    } else {
        LOG_I(TAG, "fsck run child process launched with pid = %d", pid);
        task_status_t tc_process_status = nd_set_timeout_for_task(pid, FSCK_COMMAND_TASK_TIMEOUT);
        switch (tc_process_status){
            case TASK_STATUS_SUCCESS:
                LOG_I(TAG, "fsck run child process %d successfully completed", pid);
                break;
            case TASK_STATUS_FAILED:
                LOG_E(TAG, "fsck run child process %d failed", pid);
                nd_service_obj->send_err_msg(SM_E_CB_FSCK_CMD_FAIL, NDService::UNUSED_ERR_AUX_CODE, "fsck command run failed");
                break;
            case TASK_STATUS_KILLED:
                LOG_E(TAG, "fsck run child process %d killed", pid);
                nd_service_obj->send_err_msg(SM_E_CB_FSCK_CMD_FAIL, NDService::UNUSED_ERR_AUX_CODE, "fsck command run killed");
                break;
            default:
                LOG_E(TAG, "fsck run child process %d unexpected return. Assuming it failed", pid);
                break;
        }
    }
}

static bool run_dumpe2fs_command_tt(void *args) {
    string cmd = "dumpe2fs -h " + sdcard_device_node;
    string resp = "";

    bool ret = system_execute_with_resp("RUN_dumpe2fs", cmd, resp);
    if(!ret) {
        LOG_E(TAG, "failed to execute dumpe2fs run command");
        exit(1);
    }
    LOG_I(TAG, "resp: %s", resp.c_str());
    // parse the dumpe2fs response
    stringstream ss(resp);
    string line = "";
    while(std::getline(ss, line, '\n')) {
        if(line == "") {
            continue;
        }
        size_t pos = line.find("Filesystem state:");
        /*Search for Filesystem state in output of dumpe2fs*/
        if(pos != string::npos) {
            pos = line.find("clean");
             /*if value of Filesystem state is clean, function returns true*/
            if(pos != string::npos) {
            pos = line.find("error");
             /*search for error in Filesystem state output, to prevent return true in case of "clean with errors"*/
            if (pos != string::npos)
                return false;
            else
                return true;
        }
            else
                return false;
        } else
            continue;
    }
    return false;
}

static bool run_dumpe2fs_command() {
    task_result_t task_result = nd_timed_task (run_dumpe2fs_command_tt, DUMPE2FS_COMMAND_TASK_TIMEOUT, NULL, "dumpe2fs command");
    if (task_result == TASK_TIMEOUT)
        LOG_E (TAG, "nd_timed_task for run_dumpe2fs_command_tt timedout");
    return (task_result == TASK_SUCCESS);
}


void handle_fsck_check_msg(){
    if(!run_dumpe2fs_command()){
        LOG_I(TAG, "creating thread to run fsck command to check for filesystem errors");
        fork_fsck_run_process();
    }

}

#endif // SDCARD_FSCK

bool card_stats()
{

    string percent;
    bool get_override_val = true;
    bool is_val_overridden = false;
    percent = CIRC_BUFF_ctx->bagheera_config->getConfig("sdcard","sizeForCircularBuffer",
                    DEF_INI_PERCENT_CIRC_BUFF, get_override_val, is_filling_percent_overridden);
    if(false == string_to_int64(percent, FILLING_LIMIT_PERCENT)) {
        FILLING_LIMIT_PERCENT = DEF_FILLING_PERCENT;
        LOG_E(TAG, "Invalid filling limit percent value read from config: %s, using default value: %lld", percent.c_str(), DEF_FILLING_PERCENT);
        nd_service_obj->send_err_msg(SM_E_CB_LESS_SPACE_FOR_FILLING, FILLING_LIMIT_PERCENT, "Invalid conversion of filling percent " + percent + ", defaulting to " + to_string(FILLING_LIMIT_PERCENT));
    }

    LOG_I(TAG, "card stats: circ_buff percent %lld", FILLING_LIMIT_PERCENT);
    sdcard_status = sdcard_recovery_check();

    if(sdcard_status == false) {
        // clear configs
        // Notify health mon
        string str_msg = "sdcard_status == false; Re running and Not Exiting from circ_buff";
        LOG_E(TAG, str_msg.c_str() );

        return false;
    }
    //// clean Memorycard for miscs files and DB for missing files
    LOG_I(TAG, "card stats: getting dir and db details");
    vector<file_data_str_t> all_files_dir;
    vector<file_data_str_db_t> all_files_db;
    if (get_details(all_files_dir, all_files_db) != true)
    {
        // Notify health mon
        string str_msg = "get_details failed inside card_stats";
        LOG_E(TAG, str_msg.c_str() );
        nd_service_obj->send_err_msg(SM_E_CB_SD_CARD_GET_FILE_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
        return false;
    }
    LOG_I(TAG, "card stats: calling adjust_filling_limit");
    bool ret = adjust_filling_limit( all_files_db);

    all_files_dir.clear();
    all_files_db.clear();

    return ret;
}


bool adjust_filling_limit( vector<file_data_str_db_t> all_files_db)
{
    int count = 0;
    struct statvfs buf;

#ifdef NO_SDCARD
    if(get_mount_status(nd_device_obj->get_external_eMMC_phy_mount_path(), CIRC_BUFF_ctx->buffer_mount_src) != MOUNTED) {
#else
    if(sdcard_get_mount_status(nd_device_obj->get_external_eMMC_mount_path()) != SDCARD_MOUNTED) {
#endif
        LOG_E(TAG, "SD card is not mounted");
        return false;
    }

    int ret = statvfs(nd_device_obj->get_external_eMMC_mount_path().c_str(), &buf);
    if(ret == -1) {
       LOG_E(TAG, "Failed to get space data of given file system, err: %d", strerror(errno));
       return false;
    }

    int64_t card_capacity = buf.f_blocks * buf.f_bsize;
    int64_t card_used = (buf.f_blocks - buf.f_bfree) * buf.f_bsize;
    int64_t card_available = buf.f_bavail * buf.f_bsize;
    LOG_I(TAG, "card_capacity %lld, card_used %lld, card_avilable %lld",
                card_capacity, card_used, card_available);

    // actual space is sum of used+avilable which is different from card capacity
    card_capacity = card_used + card_available;

#if 0
    int64_t filesize_db = 0;
    count = 0;
    while(count < all_files_db.size()) {
        // calulate size only for non deleted files
        // In case of file is not present directory size will be -1, so adding a check to only add positive file_size
        if((all_files_db[count].file_data.file_type != CIRCULAR_BUFFER_STATUS_DELETE) && (all_files_db[count].file_data.file_size > 0)) {
            filesize_db += all_files_db[count].file_data.file_size;
        }
        count++;
    }
    LOG_I(TAG, "all_files_db.size() %d count %d filesize_db %lld",
             all_files_db.size(), count, filesize_db);
#endif
    int card_capacity_gb = (int) (card_capacity / GIGABYTE) ;
    LOG_I(TAG, "card_capacity_gb %d", card_capacity_gb);
    if( (false == is_filling_percent_overridden) && (card_capacity_gb > FILLING_LIMIT_TIER[DEF_STORAGE_TIER-1]) ) {
        FILLING_LIMIT_PERCENT = MAX_FILLING_PERCENT_OPTIMUM_WAF;
        LOG_I(TAG, "card_capacity_gb %d is greater than tier %d threshold %d, setting filling percent for optimum WAF %lld", card_capacity_gb, DEF_STORAGE_TIER, FILLING_LIMIT_TIER[DEF_STORAGE_TIER-1], FILLING_LIMIT_PERCENT);
    }else if( (true == is_filling_percent_overridden) && (card_capacity_gb > FILLING_LIMIT_TIER[DEF_STORAGE_TIER-1]) && (FILLING_LIMIT_PERCENT > MAX_FILLING_PERCENT_OPTIMUM_WAF) ) {
        LOG_I(TAG, "Filling limit percent overridden by config to %lld which is greater than maximum optimum WAF filling limit percent %lld, changing it to %lld to maintain WAF", FILLING_LIMIT_PERCENT, MAX_FILLING_PERCENT_OPTIMUM_WAF, MAX_FILLING_PERCENT_OPTIMUM_WAF);
        FILLING_LIMIT_PERCENT = MAX_FILLING_PERCENT_OPTIMUM_WAF;
    }

    FILLING_LIMIT = ((int64_t)card_capacity - TOTAL_BACKUP_SPACE)*FILLING_LIMIT_PERCENT/100 ;
    // This check will help in case if filling percent is set to value which is more than 100%, for 128GB.
    // In those scenario, filling limit will be kept to ~90%
    if(  card_capacity < (FILLING_LIMIT + TOTAL_BACKUP_SPACE)  ) {
        LOG_C(TAG, "card_capacity %lld,  (FILLING_LIMIT + TOTAL_BACKUP_SPACE) %lld ",
                card_capacity, (FILLING_LIMIT + TOTAL_BACKUP_SPACE));
        LOG_E(TAG, "Changing the FILLING_LIMIT because of less space");


        FILLING_LIMIT = (card_capacity - 3 * TOTAL_BACKUP_SPACE );
    }
    // Removing this check, if free space goes below (60% of 5 session backup space), then 2 oldest sessions will be deleted while adding a new file

    // MAX_EMMC_FREE_SPACE is used in cirbuf_sqlrequests.cpp to validate free space value
    MAX_EMMC_FREE_SPACE = card_capacity;
    // Calculate RESERVED_SPACE, to be used while checking free space before file write.
    RESERVED_SPACE = card_capacity - FILLING_LIMIT;
    if(RESERVED_SPACE < TOTAL_BACKUP_SPACE) { // This can happen in case of filling percent is more than 100%
        RESERVED_SPACE = TOTAL_BACKUP_SPACE;
    }
    LOG_C(TAG, "card_capacity %lld FILLING_LIMIT %lld FILLING_LIMIT(GB) %f RESERVED_SPACE %lld MAX_EMMC_FREE_SPACE(used for range check) %lld total_backup_space %lld",
            card_capacity, FILLING_LIMIT, (float)FILLING_LIMIT/(GIGABYTE), RESERVED_SPACE, MAX_EMMC_FREE_SPACE, TOTAL_BACKUP_SPACE);

    return true;
}

int64_t get_total_backup_space() {
    return TOTAL_BACKUP_SPACE;
}