#include "component_base.h"
#include "nd_msg_types.h"

#include "nd_mmc_cmds.h"

typedef enum {
    not_detected = 0,
    sandisk = 1,
    toshiba = 2,
    micron = 3,
    kingston = 4,
}storage_manufacturer;

typedef enum {
    EMMC = 1,
    SDCARD = 2,
}storage_type;

typedef enum {
    NATIVE_RECOVERY = 0,
    FSCK,
    E2FSCK,
    CARD_REMOVE_INSERT,
    NO_RECOVERY,
}recovery_method;

struct memory_info {
    float rem_life_emmc;
    float rem_life_sd;
    float bad_block_emmc;
    float bad_block_sd;
    float spare_block_emmc;
    float spare_block_sd;
};


class SdCard : public ComponentBase {
    void send_sdcard_hs_to_critical_info(enum err_code_t err_code, string msg);
    bool get_sdcard_hs_from_oem_tool_output(storage_type st_type) ;
    string last_recovery_method = "";
    int avgCopyTime = 0 ;
    int avgDeleteTime = 0; 
    storage_manufacturer eMMC;
    storage_manufacturer SDcard;
    int64_t last_recovery_method_epochTime = 0;
    public:
    SdCard (std::string name, int interval_time, int start_time) : ComponentBase(name, interval_time, start_time)
    {
        recovery_method = NATIVE_RECOVERY ;
        eMMC = detect_storage_manufacturer(EMMC);
        SDcard = detect_storage_manufacturer(SDCARD);
    }
    int diagnosis(void *args);
    void recover();

    storage_manufacturer detect_storage_manufacturer(storage_type type);
    bool execute_health_check(storage_type type, storage_manufacturer manufacturer);
    bool get_average_copy_time(string source_path ) ;
    bool get_average_delete_time(string source_path ) ;
    bool get_remaining_life_bad_block_spare_block( memory_info &mem_info);
    bool check_emmc_lifetime();
    string check_last_recovery_method(); 
    int get_average_copy_time(); 
    int get_average_delete_time(); 
    ~SdCard(){};
    static bool mount_sdcard(void*);
    static bool umount_sdcard(void*);
    private:
    int substate ;
    storage_manufacturer manufacturer_sdcard;
    storage_manufacturer manufacturer_emmc;
    string oem_tool_info;   // Specific to SanDisk sdcard health status

    int slc_percentage;    // Specific to SanDisk sdcard health status
    int mlc_percentage;    // Specific to SanDisk sdcard health status
    struct ppeu_data ppeu;  // Specific to micron sdcard health status.
    struct ecc_data ecc;    // Toshiba emmc health status.
    struct wsz_data wsz;    // Toshiba emmc health status.
};


