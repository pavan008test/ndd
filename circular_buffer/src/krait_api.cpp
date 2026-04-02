#include "nd_prop_utils.h"
#include <nd_cb_utils.h>
#include "circular_buffer.h"
#include "sdcard_recovery.h"
#include <sys/statvfs.h>





//#define SDCARD_FSCK

#define MAX_FILLING_LIMIT_TIER 4
#define NO_SDCARD
#define KILOBYTE  1024
constexpr int64_t MEGABYTE = KILOBYTE * KILOBYTE;
constexpr int64_t GIGABYTE = KILOBYTE * MEGABYTE;

constexpr int64_t LOG_BACKUP_SPACE        = 1 * GIGABYTE;
constexpr int64_t OBS_BACKUP_SPACE        = 500 * MEGABYTE;
constexpr int64_t AUTOCAM_BACKUP_SPACE    = 500 * MEGABYTE;
constexpr int64_t OTA_DWNLD_BACKUP_SPACE  = 1 * GIGABYTE;
constexpr int64_t MISC_BACKUP_SPACE       = 1 * GIGABYTE;
constexpr int64_t XATTR_BACKUP_SPACE = 250 * MEGABYTE; // leave some space for xattr overheads, each files attribute are getting stored in external blocks (4KB), which can add up to significant space
constexpr int64_t TOTAL_BACKUP_SPACE =
    FIVE_SESSION_BACKUP_SPACE +
    LOG_BACKUP_SPACE +
    OBS_BACKUP_SPACE +
    AUTOCAM_BACKUP_SPACE +
    OTA_DWNLD_BACKUP_SPACE +
    XATTR_BACKUP_SPACE +
    MISC_BACKUP_SPACE;


static const char *TAG="CB";
static const string DEF_INI_PERCENT_CIRC_BUFF = "95"; // Just correcting default value to avoid confusion, but this variable is not getting used. Using integer pressent just below this.
/*
    STORAGE TIER            STORAGE HOURS EXPECTED
    64GB                         ~50Hrs
    128GB                        ~100Hrs
    256GB                        ~200Hrs
    512GB                        ~400Hrs
*/

constexpr int64_t DEF_FILLING_PERCENT = 95;
constexpr int64_t DEF_FILLING_PERCENT_OPTIMUM_WAF = 65;
constexpr int MIN_STORAGE_TIER = 1;
constexpr int MAX_STORAGE_TIER = 4;
constexpr int DEF_STORAGE_TIER = 1;
static bool is_filling_percent_overridden = false;

extern NDService *nd_service_obj;
extern ND_DeviceFactory *nd_device_obj; 
extern circular_buffer_vid *CIRC_BUFF_ctx ;
extern bool MOUNT_SDCARD ;
extern int64_t FILLING_LIMIT;
extern int64_t FILLING_LIMIT_PERCENT ;
extern int FILLING_LIMIT_TIER[MAX_FILLING_LIMIT_TIER] ;
extern int64_t RESERVED_SPACE;
extern int64_t MAX_EMMC_FREE_SPACE;

static const string DEF_INI_STORAGE_TIER = "1";
int storage_tier = 1;
bool sdcard_status = true;
#ifdef SDCARD_FSCK
static const int FSCK_COMMAND_TASK_TIMEOUT = 120;

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

void handle_fsck_check_msg(){
    fork_fsck_run_process();
}

#endif // SDCARD_FSCK

bool card_stats()
{

    bool get_override_val = true;
    bool is_val_overridden = false;
    string storage_tier_str = CIRC_BUFF_ctx->bagheera_config->getConfig("sdcard","storageTier",
                    DEF_INI_STORAGE_TIER, get_override_val, is_val_overridden);
    if(false == string_to_integer(storage_tier_str, storage_tier)){
        storage_tier = DEF_STORAGE_TIER;
        LOG_E(TAG, "Invalid storage tier value read from config: %s, using default value: %d", storage_tier_str.c_str(), DEF_STORAGE_TIER);
        nd_service_obj->send_err_msg(SM_E_CB_LESS_SPACE_FOR_FILLING, storage_tier, "Invalid conversion of tier " + storage_tier_str + ", defaulting to " + to_string(storage_tier));
    }
    LOG_I(TAG, "card stats: circ_buff storage tier %d", storage_tier);

    string percent;
    percent = CIRC_BUFF_ctx->bagheera_config->getConfig("sdcard","sizeForCircularBuffer",
                    DEF_INI_PERCENT_CIRC_BUFF, get_override_val, is_filling_percent_overridden);
    if(false == string_to_int64(percent, FILLING_LIMIT_PERCENT)) {
        LOG_E(TAG, "Invalid filling limit percent value read from config: %s, using default value: %s", percent.c_str(), DEF_INI_PERCENT_CIRC_BUFF.c_str());
        FILLING_LIMIT_PERCENT = DEF_FILLING_PERCENT;
        nd_service_obj->send_err_msg(SM_E_CB_LESS_SPACE_FOR_FILLING, FILLING_LIMIT_PERCENT, "Invalid conversion of filling percent " + percent + ", defaulting to " + to_string(FILLING_LIMIT_PERCENT));
    }
    LOG_I(TAG, "card_stats: circ_buff percent %lld", FILLING_LIMIT_PERCENT);

    if(true == MOUNT_SDCARD) {
        sdcard_status = sdcard_recovery_check(); 
    }
    else {
        sdcard_status = true;
    }

    if(sdcard_status == false) {
        // clear configs
        //deinit_config();
        // Notify health mon
        string str_msg = "sdcard_status == false; Re running and Not Exiting from circ_buff";
        LOG_E(TAG, str_msg.c_str() );
        
        return false;
    }
    // This function is run during the startup time.
    // sync_directories(INTERNAL_BUFFER_PATH, nd_device_obj->get_external_eMMC_mount_path()); is not required here.
    
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

void check_and_change_filling_percent_for_waf(int card_capacity_gb, int storage_tier) {
    if(is_filling_percent_overridden) {
        LOG_I(TAG, "Filling limit percent is overridden by config to %lld, not changing it based on storage tier or card capacity", FILLING_LIMIT_PERCENT);
        return;
    }

    if((card_capacity_gb < FILLING_LIMIT_TIER[MIN_STORAGE_TIER-1]) || (storage_tier == MIN_STORAGE_TIER)) {
        FILLING_LIMIT_PERCENT = DEF_FILLING_PERCENT;
        LOG_I(TAG, "Keeping the filling limit percent to %lld as storage tier is %d and card capacity is %d GB", FILLING_LIMIT_PERCENT, storage_tier, card_capacity_gb);
    } else {
        FILLING_LIMIT_PERCENT = DEF_FILLING_PERCENT_OPTIMUM_WAF;
        LOG_I(TAG, "Reducing filling limit percent to %lld to maintain WAF and ensure longevity of SD card as storage tier is %d and card capacity is %d GB", FILLING_LIMIT_PERCENT, storage_tier, card_capacity_gb);
    }
}

bool adjust_filling_limit( vector<file_data_str_db_t> all_files_db )
{
    int count = 0;
    struct statvfs buf;

/*
 *  Filling will be calculated on /data partition.
 *  during file migration - we are excluding nd_sdcard.img as we are taking
 *  total size of /data - mmcblk0p66
 *
 */

#ifdef NO_SDCARD
    if(get_mount_status(nd_device_obj->get_external_eMMC_phy_mount_path(), CIRC_BUFF_ctx->buffer_mount_src) != MOUNTED) {
#else
    if(sdcard_get_mount_status(nd_device_obj->get_external_eMMC_mount_path()) != SDCARD_MOUNTED) {
#endif
        LOG_E(TAG, "SD card is not mounted");
        return false;
    }

    int ret = statvfs(nd_device_obj->get_external_eMMC_phy_mount_path().c_str(), &buf);
    if(ret == -1) {
       LOG_E(TAG, "Failed to get space data of given file system, err: %d", strerror(errno));
       return false;
    }

    int64_t card_capacity = (int64_t)buf.f_blocks * (int64_t)buf.f_bsize;
    int64_t card_used = (buf.f_blocks - buf.f_bfree) * (int64_t)buf.f_bsize;
    int64_t card_available = (int64_t)buf.f_bavail * (int64_t)buf.f_bsize;
    
    LOG_I(TAG, "card_capacity %lld, card_used %lld, card_avilable %lld", 
                card_capacity, card_used, card_available);

    // actual space is sum of used+avilable which is different from card capacity
    card_capacity = (int64_t)(card_used + card_available); 

    //Change the hw_emmc_size to the lowest.
    //Card capacity indicates /dev/loop0
    //TODO: Remove this code when migrating to folder.
    int card_capacity_gb = (int) (card_capacity / GIGABYTE) ;
    int storage_tier_requested_capacity_gb = FILLING_LIMIT_TIER[ storage_tier - 1];
    LOG_I(TAG, "card_capacity_gb = %d, storage_tier_requested_capacity_gb = %d, storage_tier = %d ", card_capacity_gb, storage_tier_requested_capacity_gb, storage_tier);
   
    // Check card_capacity before and storage tier before filling limit calculation.
    // If card capacity is less than 64GB or requested storage tier is 1. Then keep the filling percent to 90,
    // otherwise change to 75% as in any other scenario along with garaunting 100 hrs storage, some buffer of free space is maintained so that WAF does not go very high which can lead to faster wear and tear of the emmc.
    check_and_change_filling_percent_for_waf(card_capacity_gb, storage_tier);

    FILLING_LIMIT = (((int64_t)storage_tier_requested_capacity_gb )*GIGABYTE - TOTAL_BACKUP_SPACE ) * FILLING_LIMIT_PERCENT/100 ;
    
    if( card_capacity < (FILLING_LIMIT + TOTAL_BACKUP_SPACE ))  {
        LOG_I(TAG, "card_capacity: %lld,  (FILLING_LIMIT + TOTAL_BACKUP_SPACE) %lld ",
                card_capacity, (FILLING_LIMIT + TOTAL_BACKUP_SPACE));

        FILLING_LIMIT = (int64_t)((card_capacity) - TOTAL_BACKUP_SPACE ) * DEF_FILLING_PERCENT/100 ;
        LOG_E(TAG, "Changing the FILLING_LIMIT because of less space. TOTAL_BACKUP_SPACE: %lld new FILLING_LIMIT: %lld", TOTAL_BACKUP_SPACE, FILLING_LIMIT );
    }
    // MAX_EMMC_FREE_SPACE is used in cirbuf_sqlrequests.cpp to validate free space value
    MAX_EMMC_FREE_SPACE = card_capacity;
    // Calculate RESERVED_SPACE, to be used while checking free space before file write.
    RESERVED_SPACE = card_capacity - FILLING_LIMIT;
    if(RESERVED_SPACE < TOTAL_BACKUP_SPACE) { // This can happen in case of filling percent is more than 100%
        RESERVED_SPACE = TOTAL_BACKUP_SPACE;
    }
    LOG_I(TAG, "card_capacity %lld FILLING_LIMIT %lld FILLING_LIMIT(GB) %f RESERVED_SPACE %lld MAX_EMMC_FREE_SPACE(used for range check) %lld total_backup_space %lld", 
            card_capacity, FILLING_LIMIT, (float)FILLING_LIMIT/GIGABYTE, RESERVED_SPACE, MAX_EMMC_FREE_SPACE, TOTAL_BACKUP_SPACE);
    return true;
}

int64_t get_total_backup_space() {
    return TOTAL_BACKUP_SPACE;
}
