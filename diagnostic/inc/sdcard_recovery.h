/* Copyright (C) 2019 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sri Hari Haran Seenivasan <hari.seenivasan@netradyne.com>,July 2019
 */


#ifndef SDCARD_RECOVERY_H
#define SDCARD_RECOVERY_H


#include <stdio.h>
#include <cstdlib>
#include <mutex>
#include <condition_variable>

enum sdcard_recovery_stage {
    SDCARD_RECOVERY_NOOP = 0,
    SDCARD_RECOVERY_REMOVE_INSERT,
    SDCARD_RECOVERY_ERROR
};

class sdcard_recovery_ctx
{
public:

    //Singleton
    static sdcard_recovery_ctx* get_sdcard_recovery( string name, string mount_path );
    
    // Variables
    uint sdcard_recovery_retry_count = 0;
    sdcard_recovery_stage current_recovery_stage;
    static bool get_unmount_hanged() ; 
    static void set_unmount_hanged(bool) ; 

    //Methods

    static bool umount_sdcard(void*);
    bool check_sdcard_status_attribute();
    sdcard_recovery_stage get_sdcard_recovery_stage();
    void clear_sdcard_recovery_stage();
    int get_card_status();
private:
    //Variables
    int card_status = 1;
    string name;
    string mount_path;
    static bool unmount_hanged;    
    //Methods
    bool check_set_attributes();
    bool send_sdcard_ro_reboot_powermon ();
    void sdcard_recovery();
    int check_sdcard_status();
    void set_sdcard_recovery_stage (sdcard_recovery_stage recovery_stage);

    //Constructor
    sdcard_recovery_ctx( string name, string mount_path );
    //deconstructor
    ~sdcard_recovery_ctx();
};
bool sdcard_recovery_check();
int sdcard_recovery_native();
#endif
