#include <log.h>
#include <config_parser.h>
#include <nd_file_utils.h>

#include <vector>
#include <sstream>
#include <pthread.h>
#include <iostream>
#include <fstream>
#include <string>
#include <svc_common.h>
#include "nd_factory.h"

using namespace std;

#define TAG "RCVRY"

enum nd_config_error_aux {
    MANIFEST_CONFIG_FILE_NOT_AVAILABLE = 1,
    MANIFEST_CONFIG_PARSE_FAIL,
    MANIFEST_LOCALE_CONFIG_FIELD_NOT_AVAILABLE,
    LOCALE_CONFIG_LOCALE_FIELD_EMPTY,
    LOCALE_CONFIG_DEFAULT_LOCALE_FIELD_EMPTY,
    EMPTY_REGION,
    CONFIG_FILE_NOT_AVAILABLE,
    ND_CONFIG_RECOVERY_WITH_DEFAULT_LOCALE_FAIL,
    ND_CONFIG_RECOVERY_WITH_DEFAULT_LOCALE_PASS,
    ND_CONFIG_RECOVERY_WITH_DEFAULT_LOCALE_BACKUP_FAIL,
    ND_CONFIG_RECOVERY_WITH_DEFAULT_LOCALE_BACKUP_PASS,
    ND_CONFIG_RECOVERY_DEFAULT_FAIL,
    ND_CONFIG_RECOVERY_DEFAULT_PASS,
    ND_CONFIG_RECOVERY_FAIL, 
};

static const string BACKUP_PATH = "/home/ubuntu/backup";
// Subdirectory MD5_BACKUP_DIR under BACKUP_PATH used specifically for md5sum-based backups.
// This keeps the md5 backups separate from other backup files and optimizes
// directory scans by limiting them to this subdirectory.
static const string MD5_BACKUP_DIR = BACKUP_PATH + "/md5";
static const string DEVICECONFIG_PATH = "/home/ubuntu/config/deviceconfig.ini";
static const string NDDEVICE_PATH = "/home/ubuntu/.nddevice/nddevice.ini";
static const string LINK_LATEST = "/home/ubuntu/.nddevice/latest";
static const string BAGHEERA_CONFIG_PATH = LINK_LATEST + "/bagheera_config.ini";
static const string CLOUDCONFIG_PATH = LINK_LATEST + "/cloudconfig.ini";

// nd_config.ini has been replaced with nd_config_recovery.ini for DT-2272 :
// Analytics locale based config update changes
static const string ND_CONFIG_RECOVERY_PATH = LINK_LATEST + "/nd_config_recovery.ini";
static const string ND_CONFIG_PREFIX = LINK_LATEST + "/nd_config_";
static const string ND_CONFIG_FILE_EXTENSION = ".ini";
static const string MANIFEST_CONFIG_FILE = "/home/ubuntu/.nddevice/latest/package_manifest.ini";

//Constants
static const string section_version = "version";
static const string section_identity = "identity";
static const string section_upgrade = "upgrade";
static const string section_vehicle = "vehicle";
static const string section_cleanup = "cleanup";

static const string key_nddevice = "nddevice";
static const string key_deviceid = "deviceid";
static const string key_sessionid = "sessionid";
static const string key_devicetype = "devicetype";
static const string key_devicesubtype = "devicesubtype";
static const string key_vehclass = "vehclass";
static const string key_lanecal = "lanecal";
static const string key_savemp4 = "savemp4";
static const string key_state = "state";
static const string key_packagetype = "package_type";

static const string defvalue_devicetype = "bagheera";
static const string defvalue_subtype = "dvt2";
static const string defvalue_vehclass = "Class1";
static const string defvalue_lanecal = "none";
static const string defvalue_savemp4 = "delete";
static const string defvalue_state = "STABLE";
static const string defvalue_upgradestate = "NONE";
static const string defvalue_packagetype = "OTA";


static int poll_interval = 15*60; //15 mins

#define nd_service_obj (NDService::get_service_obj("SVC"))

//Class hierarchy:
//
//Recovery     ->     RecoveryIni       ->    RecoveryIni1
//                                      ->    RecoveryIni2
//      
//             ->     RecoveryJson(tbd) ->    RecoveryJson1
//                                      ->    RecoveryJson2
//
//             ->     RecoveryMd5sum
                      

//Virtual Base class that sets has minimum functions
class Recovery {    
    public:
        //Constructor
        Recovery(string path);

        //Used to add keys that need to checked
        bool add_keys( pair<string,string> key );

        //Check file in 'path' for corruption
        virtual bool check()=0;
        //Check file in fname for corruption
        virtual bool check(string fname)=0;

        //Returns true if a backup needs to be taken
        virtual bool needs_update();
        //Take backup of file path to rpath
        virtual bool backup();
        //Recover file
        virtual bool recover();
    
        //Getter functions
        string get_path() { return path; }
        string get_rpath() { return rpath; }
        vector< pair<string,string> > get_keys() { return keys; }

    protected:
        //Primary recovery strategy
        virtual bool recover_primary();
        //Secondary recovery strategy
        virtual bool recover_secondary()=0;

    private:
        string path;
        string rpath;
        vector< pair<string,string> > keys;

};


static const pair<string,string> keys_nddevice[] = {
                                                      make_pair(section_version,key_nddevice),
                                                      make_pair(section_upgrade,key_nddevice),
                                                      make_pair(section_upgrade,key_state)
                                                   };
static const pair<string,string> keys_deviceconfig[] = { 
                                                         make_pair(section_identity,key_deviceid),
                                                         make_pair(section_identity,key_sessionid),
                                                         make_pair(section_identity,key_devicetype),
                                                         make_pair(section_identity,key_devicesubtype),
                                                         make_pair(section_vehicle ,key_vehclass)
                                                       };

//List of recovery objects
static vector<Recovery *> recovery_list;

static bool recovery_check_sanity();
static bool recovery_recover();
extern bool recovery_take_backup();

//Base class defining interfaces for sanity checking, backup and recovery mechanism
Recovery::Recovery(string path) : path(path) {
    stringstream ss;
    const string fname = path;
    string trpath = fname;

    //Extract just filename
    const size_t last_slash_idx = trpath.find_last_of("\\/");
    if (std::string::npos != last_slash_idx) {
        trpath.erase(0, last_slash_idx + 1);
    }

    //Get full path for backup destination
    ss<<BACKUP_PATH<<"/"<<trpath.c_str();
    rpath = ss.str();
}

bool Recovery::add_keys( pair<string,string> key ) {
    if ( key.first == "" ) {
        LOG_E( TAG, "Empty section" );
        return false;
    }

    if( key.second == "" ) {
        LOG_E( TAG, "Empty key" );
        return false;
    }

    keys.push_back(key);
    return true;
}

bool Recovery::recover() {

    //Try the primary backup strategry
    if( true == recover_primary() ) {
        LOG_I(TAG, "Primary recovery success");
    }
    else {
        LOG_E(TAG, "Primary recovery failed");
        //Try ini specific secondary recovery strategy
        if( false == recover_secondary() ) {
            LOG_I(TAG, "Secondary recovery failed");
            return false;
        }
        LOG_I(TAG, "Secondary recovery success");
    }

    return true;
}

//Take a backup of files, always check for corruption before calling this
bool Recovery::backup() {
    
    string fname_chm = "";
    string dfname_chm = "";
    
    //Create backup folder if not already created
    if( false == file_mkdir(BACKUP_PATH) ) {
        LOG_E(TAG, "Cannot create backup directory: %s", BACKUP_PATH.c_str());
        return false;
    }
    string fname = get_path();
    string dfname = get_rpath();

    //Take a backup by copying
    if( false == file_copy(fname, dfname) ) {
        LOG_E(TAG, "%s backup failed", fname.c_str() );
        return false;
    }

    if( !(calculate_md5sum(fname, fname_chm)) || !(calculate_md5sum(dfname, dfname_chm)) ) 
    {
        LOG_E(TAG, "Failed to calculate checksum for files");
    }

    //Verify checksum after copying
    if( fname_chm != dfname_chm ) {
        LOG_E(TAG, "%s backup failed, checksum error", fname.c_str() );
        file_delete(dfname);
        return false;
    }

    LOG_I( TAG, "%s backup success", fname.c_str() );
    return true;
}

//Check if the file under monitor is corrupted
bool Recovery::needs_update() {

    const string fname = get_path();
    string dfname = get_rpath();
    string fname_chm = "";
    string dfname_chm = "";

    //Check if the source file is corrupted
    if( false == check() ) {
        //Looks like file is corrupted, do not take backup
        SVC_LOG_E(TAG, "Corruption in file detected, do not take backup");
        return false;
    }

    if( false == file_is_present(dfname) ) {
        SVC_LOG_E(TAG, "%s not present, take backup", fname.c_str());
        return true;
    }
 
    if( !(calculate_md5sum(fname, fname_chm)) || !(calculate_md5sum(dfname, dfname_chm)) ) 
    {
        SVC_LOG_E(TAG, "Failed to calculate checksum for files");
    }

    //If checksums do not match, we should TRY to take backup
    if( fname_chm != dfname_chm ) {
        SVC_LOG_E(TAG, "checksums do not match %s, %s; backup required", fname.c_str(), dfname.c_str() );
    } else {
        LOG_I(TAG, "No backup required. checksums match %s, %s", fname.c_str(), dfname.c_str() );
        return false;
    }

    //Checksums differ and source file is valid, take backup
    return true;
}

bool Recovery::recover_primary() {
    //file to be recovered
    string path = get_path();
    //location of backup
    string rpath = get_rpath();

    LOG_I(TAG, "Starting primary recovery for file: %s", rpath.c_str());

    //File not present, return error
    if( false == file_is_present(rpath) ){
        SVC_LOG_E(TAG, "Recovery file not present: %s", rpath.c_str());
        return false;
    }

    //Verify Sanity of recovery file    
    if( false == check(rpath) ) {
        LOG_E(TAG, "Recovery file not valid: %s", rpath.c_str());
        return false;
    }
    
    //Recover file by copying
    if( false == file_copy(rpath,path) ) {
        LOG_E(TAG, "Error in copying recovery file, src:%s, dest:%s", rpath.c_str(), path.c_str());
        return false;
    }

    LOG_I(TAG, "Primary recovery for file: %s success", rpath.c_str());
    return true;
}

//Specialized class for Ini Recovery
class RecoveryIni : public Recovery {
    public:
        RecoveryIni(string path): Recovery(path) { }
        bool check();
        bool check(string fname);
};

bool RecoveryIni::check() {
    return check(get_path());
}

bool pathEndsWith(const string& filePath, const string& targetSuffix) {
    size_t suffixLength = targetSuffix.length();
    size_t filePathLength = filePath.length();
    if (suffixLength > filePathLength) {
        return false;
    }

    for (size_t i = 0; i < suffixLength; i++) {
        if (filePath[filePathLength - suffixLength + i] != targetSuffix[i]) {
            return false;
        }
    }
    return true;
}

string readFileToString(const string& filename) {
    ifstream file(filename);
    if (!file_is_present(filename)) {
        LOG_I(TAG, "File not present: %s", filename.c_str());
        return "";
    }

    string content((istreambuf_iterator<char>(file)),
                        istreambuf_iterator<char>());

    file.close();
    // Remove newlines from the end of the content
    while (!content.empty() && content.back() == '\n') {
        content.pop_back();
    }
    return content;
}

bool RecoveryIni::check(string path) {

    //Return error if file is not present
    if( false == file_is_present(path) ) {
        LOG_E(TAG, "File not present: %s", path.c_str());
        return false;
    }

    bool isNdDevice = pathEndsWith(path, "nddevice.ini");
    if (isNdDevice){
        string searchString = "RUN_STATE";
        string fileContent = readFileToString(nd_device_obj->get_otacheck_state_path());
        bool searchStringFound = (fileContent.find(searchString) != string::npos);
        if(searchStringFound){
            LOG_E(TAG, "Otacheck or Update Engine running, returning true");
            return true;
        }
    }

    //Return error if file size is zero
    if( 0 == file_size(path) ) {
        LOG_E(TAG, "File size Zero: %s", path.c_str());
        return false;
    }

    //Try to parse the file, return on error
    Config_parser c(path);
    if (c.getParseStatus() != true) {
        LOG_E (TAG,"Error in parsing: %s", path.c_str());
        return false;
    }

    //Check if non empty entries are present for each required key
    vector< pair<string,string> > k = get_keys();
    for(  vector< pair<string,string> >::iterator iter = k.begin(), end = k.end();
                        iter != end;
                        iter++ ) {
        const string section = iter->first;
        const string key = iter->second;

        if ( false == c.isPresent (section,key) ) {
            LOG_E(TAG, "section: %s, key: %s not found in file: %s", section.c_str(), key.c_str(), path.c_str());
            return false;
        }

        string value = c.getConfig (section,key,"");
        if (value == "") {
            LOG_E(TAG, "value of section: %s, key: %s is empty in file: %s", section.c_str(), key.c_str(), path.c_str());
            return false;
        }
    }

    return true;
}

//Specialized class for Recovery
class RecoveryIni1 : public RecoveryIni {
    public:
        RecoveryIni1(string path): RecoveryIni(path) { }
        virtual bool recover_secondary();
    private:
        string get_ota_version();
};

//Get ota version
string RecoveryIni1::get_ota_version() {
    const char *symlinkpath = LINK_LATEST.c_str();
    char *actualpath;
    string version;

    //Get the actual file path from link
    actualpath = realpath(symlinkpath, NULL);
    if (actualpath == NULL) {
        LOG_E(TAG, "Version could not be recovered");
        return "";
    }

    LOG_I(TAG, "Path: %s", actualpath);

    version = actualpath;
    free(actualpath);

    //Extract OTA foldername
    const size_t last_slash_idx = version.find_last_of("\\/");
    if (std::string::npos != last_slash_idx) {
        version.erase(0, last_slash_idx + 1);
    }
    LOG_I(TAG, "Recovered version: %s", version.c_str());

    if( "" == version ) {
        LOG_E(TAG, "Version could not be recovered");
    }

    return version;
}

bool RecoveryIni1::recover_secondary() {
    const string fname = get_path();

    LOG_I(TAG, "Secondary recovery called for %s", fname.c_str());

    //Get OTA version
    string version = get_ota_version();
    if( version == "" ) {
        LOG_E(TAG, "Cannot get version");
        return false;
    }

    //Remove the file to start fresh, this will help with corrupt files    
    if( false == file_truncate(fname) ) {
        LOG_E(TAG, "Truncate failed");

        //Truncate failed, probably file not preset. Create the config file
        if( false == file_touch(fname) ) {
            LOG_E(TAG, "Cannot create file: %s", fname.c_str());
            return false;
        }
    }

    //Open in config parser and start adding fields
    Config_parser *cp = new Config_parser(get_path());

    //Add default configurations
    if( ! cp->addConfig(section_version, key_nddevice, version) ) {
        LOG_E(TAG, "Error adding section: %s key: %s value:%s", section_version.c_str(), key_nddevice.c_str(), version.c_str());
        goto err;
    }

    if( ! cp->addConfig(section_version, key_state, defvalue_state) ) {
        LOG_E(TAG, "Error adding section: %s key: %s value:%s", section_version.c_str(), key_state.c_str(), defvalue_state.c_str());
        goto err;
    }

    if( ! cp->addConfig(section_upgrade, key_nddevice, version) ) {
        LOG_E(TAG, "Error adding section: %s key: %s value:%s", section_upgrade.c_str(), key_nddevice.c_str(), version.c_str());
        goto err;
    }

    if( ! cp->addConfig(section_upgrade, key_state, defvalue_upgradestate) ) {
        LOG_E(TAG, "Error adding section: %s key: %s value:%s", section_upgrade.c_str(), key_state.c_str(), defvalue_upgradestate.c_str());
        goto err;
    }

    if( ! cp->addConfig(section_upgrade, key_packagetype, defvalue_packagetype) ) {
        LOG_E(TAG, "Error adding section: %s key: %s value:%s", section_upgrade.c_str(), key_packagetype.c_str(), defvalue_packagetype.c_str());
        goto err;
    }

    //Delete config parser to close file
    delete cp;
    return true;

err:
    //Close config file on error
    delete cp;
    //Delete config file to stop further damage
    file_delete(fname);
    return false;
}

//Specialized class for Recovery
class RecoveryIni2 : public RecoveryIni {
    public:
        RecoveryIni2(string path): RecoveryIni(path) { }
        virtual bool recover_secondary();
    private:
        string get_device_id();
};

string RecoveryIni2:: get_device_id() {
    string cmd = "sys_tx1read | grep \"Product serial_num\" | cut -f2 -d':'";
    char buffer[1024];

    FILE *fp = popen(cmd.c_str(), "r");
    if (fp == NULL) {
        LOG_E(TAG, "Failed to copy files to card :: %s" , cmd.c_str() );
    }

    while (fgets(buffer, sizeof(buffer)-1, fp) != NULL) {
        LOG_I(TAG, "readbuffer: %s", buffer);
    }
    pclose(fp);

    string serial_no = buffer;
    LOG_I( TAG, "serial_no: %s", serial_no.c_str() );

    return serial_no;
}

bool RecoveryIni2::recover_secondary() {
    const string fname = get_path();

    LOG_I(TAG, "Secondary recovery called for %s", fname.c_str());

    //Get the device serial number
    string sno = get_device_id();
    if( "" == sno ) {
        LOG_E(TAG, "Cannot read serial number");
        return false;
    }

    //Clean the file to start fresh, this will help with corrupt files    
    if( false == file_truncate(fname) ) {
        LOG_E(TAG, "Truncate failed");

        //Truncate failed, probably file not preset. Create the config file
        if( false == file_touch(fname) ) {
            LOG_E(TAG, "Cannot create file: %s", fname.c_str());
            return false;
        }
    }

    //Open in config parser and start adding fields
    Config_parser *cp = new Config_parser(get_path());

    //Add default configurations
    if( ! cp->addConfig(section_identity, key_deviceid, sno) ) {
        LOG_E(TAG, "Error adding section: %s key: %s value:%s", section_identity.c_str(), key_deviceid.c_str(), sno.c_str());
        goto err;
    }

    if( ! cp->addConfig(section_identity, key_sessionid, sno) ) {
        LOG_E(TAG, "Error adding section: %s key: %s value:%s", section_identity.c_str(), key_sessionid.c_str(), sno.c_str());
        goto err;
    }

    if( ! cp->addConfig(section_identity, key_devicetype, defvalue_devicetype) ) {
        LOG_E(TAG, "Error adding section: %s key: %s value:%s", section_identity.c_str(), key_devicetype.c_str(), defvalue_devicetype.c_str());
        goto err;
    }

    if( ! cp->addConfig(section_identity, key_devicesubtype, defvalue_subtype) ) {
        LOG_E(TAG, "Error adding section: %s key: %s value:%s", section_identity.c_str() , key_devicesubtype.c_str(), defvalue_subtype.c_str());
        goto err;
    }

    if( ! cp->addConfig(section_vehicle , key_vehclass, defvalue_vehclass) ) {
        LOG_E(TAG, "Error adding section: %s key: %s value:%s", section_vehicle.c_str(), key_vehclass.c_str(), defvalue_vehclass.c_str());
        goto err;
    }

    if( ! cp->addConfig(section_cleanup, key_lanecal, defvalue_lanecal) ) {
        LOG_E(TAG, "Error adding section: %s key: %s value:%s", section_cleanup.c_str(), key_lanecal.c_str(), defvalue_lanecal.c_str());
        goto err;
    }

    if( ! cp->addConfig(section_cleanup, key_savemp4, defvalue_savemp4) ) {
        LOG_E(TAG, "Error adding section: %s key: %s value:%s", section_cleanup.c_str(), key_savemp4.c_str(), defvalue_savemp4.c_str());
        goto err;
    }

    delete cp;
    return true;
err:
    //Close config file on error
    delete cp;
    //Remove file to stop further damage
    file_delete(fname);
    return false;

}

//Specialized class for Recovery of bagheera_config.ini, cloudconfig.ini, nd_config.ini
class RecoveryIni3 : public RecoveryIni {
    public:
        RecoveryIni3(string path): RecoveryIni(path) { }
        virtual bool recover_secondary();
    //private:
    //    string get_ota_version();
};

bool RecoveryIni3::recover_secondary() {

    LOG_I(TAG, "%s: Dummy function for %s", __func__, get_path().c_str());
    return true;
}

// New specialized class for files whose backups are md5-suffixed.
class RecoveryMd5sum : public Recovery {
public:
    explicit RecoveryMd5sum(const std::string &path)
        : Recovery(path) {
        const std::string &live_path = get_path();
        const size_t last_slash_idx = live_path.find_last_of("\\/");
        if (std::string::npos == last_slash_idx) {
            base_name = live_path;
        } else {
            base_name = live_path.substr(last_slash_idx + 1);
        }
    }

    // Override backup decision: need backup if md5-suffixed file exists in latest.
    virtual bool needs_update() {
        const std::string live_path = get_path();
        // Live must exist; otherwise, no point taking backup.
        if (!file_is_present(live_path)) {
            LOG_E(TAG, "Md5sum live file not present for needs_update: %s", live_path.c_str());
            return false;
        }

        std::string live_md5;
        if (!calculate_md5sum(live_path, live_md5)) {
            LOG_E(TAG, "Failed to calculate md5sum in needs_update for: %s", live_path.c_str());
            return false;
        }

        // If md5-suffixed file is present in latest, we should take backup.
        const std::string latest_backup = LINK_LATEST + "/" + base_name + "." + live_md5;
        if (file_is_present(latest_backup)) {
            LOG_I(TAG, "Md5-suffixed backup present in latest for %s, backup needed", live_path.c_str());
            new_backup_md5 = live_md5;
            return true;
        }

        LOG_I(TAG, "No md5-suffixed backup present in latest for %s, no backup needed", live_path.c_str());
        return false;
    }

    // Override backup: move (not copy) md5-suffixed file from latest to MD5_BACKUP_DIR and make it read-only.
    virtual bool backup() {
        const std::string live_path = get_path();

        // Ensure the backup directory exists.
        if (!file_mkdir(BACKUP_PATH)) {
            LOG_E(TAG, "Cannot create parent backup directory: %s", BACKUP_PATH.c_str());
            return false;
        }

        // Ensure the md5-specific subdirectory exists.
        if (!file_mkdir(MD5_BACKUP_DIR)) {
            LOG_E(TAG, "Cannot create md5 backup directory: %s", MD5_BACKUP_DIR.c_str());
            return false;
        }

        // Before creating a new backup, delete any old md5-suffixed backups for this base file.
        bool deleted_any = false;
        if (!scan_backup_dir(BackupScanMode::DELETE_ALL, deleted_any)) {
            LOG_E(TAG, "Failed to clean old md5 backups in directory: %s", MD5_BACKUP_DIR.c_str());
        }

        // Backup file name in MD5_BACKUP_DIR will also be md5-suffixed.
        const std::string backup_path = MD5_BACKUP_DIR + "/" + base_name + "." + new_backup_md5;
        const std::string latest_backup = LINK_LATEST + "/" + base_name + "." + new_backup_md5;
        
        if (file_is_present(latest_backup)) {
            // Move (rename) from latest to backup dir (no copy).
            if (rename(latest_backup.c_str(), backup_path.c_str()) != 0) {
                LOG_E(TAG, "Failed to move md5 backup from latest to backup: %s -> %s", latest_backup.c_str(), backup_path.c_str());
                return false;
            }
        } else {
            LOG_E(TAG, "Md5-suffixed new file not present in latest for backup: %s", latest_backup.c_str());
            return false;
        }

        // Ensure read-only permissions on backup file.
        if (chmod(backup_path.c_str(), 0444) != 0) {
            LOG_E(TAG, "Failed to set read-only permissions on md5 backup: %s", backup_path.c_str());
            // not fatal for backup existence, but log it.
        }

        LOG_I(TAG, "Moved md5 backup from latest to backup and set read-only: %s -> %s", latest_backup.c_str(), backup_path.c_str());
        return true;
    }

    // Check that live file md5 has a matching md5-suffixed backup in MD5_BACKUP_DIR.
    virtual bool check() {
        return check(get_path());
    }

    virtual bool check(string fname) {
        if (!file_is_present(fname)) {
            LOG_E(TAG, "Md5sum live file not present for check: %s", fname.c_str());
            return false;
        }
        if (0 == file_size(fname)) {
            LOG_E(TAG, "Md5sum live file size zero for check: %s", fname.c_str());
            return false;
        }

        std::string live_md5;
        if (!calculate_md5sum(fname, live_md5)) {
            LOG_E(TAG, "Failed to calculate md5sum in check for: %s", fname.c_str());
            return false;
        }

        // Now expect md5-suffixed backup in the md5 subdirectory under MD5_BACKUP_DIR.
        const std::string expected_backup = MD5_BACKUP_DIR + "/" + base_name + "." + live_md5;
        if (!file_is_present(expected_backup)) {
            if( !has_backup() ) {
                LOG_W(TAG, "No md5-suffixed backup present at all for base file %s; Skipping check.", base_name.c_str());
                return true;
            }
            LOG_E(TAG, "Corruption detected : file: %s md5sum: %s", base_name.c_str(), live_md5.c_str());
            return false;
        }

        LOG_I(TAG, "Md5sum check passed for %s against backup %s", fname.c_str(), expected_backup.c_str());
        return true;
    }

protected:
    // Primary recovery: copy to transient file in latest and then rename to live; ensure read-only.
    virtual bool recover_primary();

    // Secondary left as no-op; primary is our main path.
    virtual bool recover_secondary() { return true; }

private:
    std::string base_name;
    std::string new_backup_md5;

    enum class BackupScanMode {
        ANY_EXISTS,        // just check if any backup exists for base_name
        DELETE_ALL,        // delete all backups for base_name
        FIND_ONE_PATH      // find one backup path for base_name (first match)
    };

    // Common helper to iterate over MD5_BACKUP_DIR and operate on files matching
    // prefix <base_name>.
    // - ANY_EXISTS: sets outFound=true if any such file exists.
    // - DELETE_ALL: deletes all matching files; outFound indicates if any were deleted.
    // - FIND_ONE_PATH: sets outPath to first matching file path and outFound accordingly.
    bool scan_backup_dir(BackupScanMode mode, bool &outFound, std::string *outPath = NULL) {
        outFound = false;
        if (outPath) {
            outPath->clear();
        }

        DIR *dp = opendir(MD5_BACKUP_DIR.c_str());
        if (!dp) {
            LOG_E(TAG, "Failed to open md5 backup directory for scan: %s", MD5_BACKUP_DIR.c_str());
            return false;
        }

        struct dirent *de;
        const std::string prefix = base_name + ".";
        while ((de = readdir(dp)) != NULL) {
            std::string name = de->d_name;
            if (name == "." || name == "..") {
                continue;
            }
            if (name.rfind(prefix, 0) != 0) {
                continue;
            }

            const std::string full_path = MD5_BACKUP_DIR + "/" + name;

            switch (mode) {
                case BackupScanMode::ANY_EXISTS:
                    outFound = true;
                    closedir(dp);
                    return true;

                case BackupScanMode::DELETE_ALL:
                    if (file_is_present(full_path)) {
                        LOG_I(TAG, "Deleting old md5 backup: %s", full_path.c_str());
                        file_delete(full_path);
                        outFound = true;
                    }
                    break;

                case BackupScanMode::FIND_ONE_PATH:
                    if (outPath) {
                        *outPath = full_path;
                    }
                    outFound = true;
                    closedir(dp);
                    return true;
            }
        }

        closedir(dp);
        return true;
    }

    bool has_backup() {
        bool found = false;
        if (!scan_backup_dir(BackupScanMode::ANY_EXISTS, found)) {
            // scan failed; conservatively report no backup
            return false;
        }
        return found;
    }
};

bool RecoveryMd5sum::recover_primary() {
    const std::string live_path = get_path();

    if (!file_is_present(live_path)) {
        LOG_E(TAG, "Md5sum live file not present for primary recovery path resolution: %s", live_path.c_str());
    }

    // Search MD5_BACKUP_DIR for any file with prefix <base_name>.
    std::string backup_path;
    bool found = false;
    if (!scan_backup_dir(BackupScanMode::FIND_ONE_PATH, found, &backup_path) || !found) {
        const std::string prefix = base_name + ".";
        LOG_E(TAG, "No md5-suffixed backup found for primary recovery with prefix %s in %s", prefix.c_str(), MD5_BACKUP_DIR.c_str());
        return false;
    }

    if (!file_is_present(backup_path)) {
        LOG_E(TAG, "Selected backup file not present for primary recovery: %s", backup_path.c_str());
        return false;
    }

    // Verify sanity of backup file by comparing its own md5sum with the name
    std::string expected_md5 = backup_path.substr(backup_path.rfind('.') + 1);
    std::string actual_md5;
    if (!calculate_md5sum(backup_path, actual_md5)) {
        LOG_E(TAG, "Failed to calculate md5sum of backup file during primary recovery: %s", backup_path.c_str());
        return false;
    }
    if (expected_md5 != actual_md5) {
        LOG_E(TAG, "Md5sum mismatch for backup file during primary recovery: expected %s, got %s for file %s",
              expected_md5.c_str(), actual_md5.c_str(), backup_path.c_str());
        return false;
    }

    // Copy into transient file in latest.
    const std::string tmp_path = live_path + ".restore.tmp";
    if (!file_copy(backup_path, tmp_path)) {
        LOG_E(TAG, "Failed to copy md5 backup to temp for primary recovery: %s -> %s", backup_path.c_str(), tmp_path.c_str());
        return false;
    }

    // Atomic rename to live path.
    if (rename(tmp_path.c_str(), live_path.c_str()) != 0) {
        LOG_E(TAG, "Failed to rename temp to live in primary recovery: %s -> %s", tmp_path.c_str(), live_path.c_str());
        file_delete(tmp_path);
        return false;
    }

    // Ensure read-only permissions on the restored live file.
    if (chmod(live_path.c_str(), 0444) != 0) {
        LOG_E(TAG, "Failed to set read-only permissions on restored live file: %s", live_path.c_str());
        // not fatal to recovery; file is in place.
    }

    file_fd_sync(live_path);
    LOG_I(TAG, "Md5sum primary recovery succeeded for file: %s using backup: %s", live_path.c_str(), backup_path.c_str());
    return true;
}

static void *fn_poll_thread(void *ptr) {
    while(1) {

        //Check for sanity
        if( true == recovery_check_sanity() ) {
            //If sanity passed, be happy
            LOG_I(TAG, "Sanity passed");
        } else {
            //Recover the corrupted files
            LOG_E(TAG, "Sanity failed, starting recovery");
            bool res = recovery_recover();
            LOG_I(TAG, "Recovery status: %d", res);
            //Check for sanity to be sure
            res = recovery_check_sanity();
            LOG_I(TAG, "Sanity result: %d", res);

            //Sync to commit change to FS, safe to perform,
            //as it is only done once at bootup
            file_sync();

        }

        sleep( poll_interval );
    }
}

bool check_and_update_default_recovery(const std::string& default_locale) {
    string recovery_backup_file = BACKUP_PATH + "/nd_config_recovery.ini";
    string default_locale_file = ND_CONFIG_PREFIX + default_locale + ND_CONFIG_FILE_EXTENSION;
    string default_backup_locale = BACKUP_PATH + "/nd_config_" + default_locale + ND_CONFIG_FILE_EXTENSION;
    string default_nd_config_backup_file = BACKUP_PATH + "/nd_config.ini";
    string err_msg = "";
    LOG_I(TAG, "Checking and updating default recovery locale: %s", default_locale.c_str());
    if(file_is_present(ND_CONFIG_RECOVERY_PATH) == true) {
        LOG_I(TAG, "Recovery file already exists: %s", ND_CONFIG_RECOVERY_PATH.c_str());
        return true;
    }
    if(file_is_present(recovery_backup_file) == true) {
        LOG_I(TAG, "Recovery backup file present: %s", recovery_backup_file.c_str());
        //If backup file is present, not action required
        return true;
    }

    LOG_E(TAG, "nd_config_recovery.ini file not present in backup as well as latest, updating default_locale as recovery file: %s", default_locale_file.c_str());
    if( file_is_present(default_locale_file) == true) {
        if( false == file_copy(default_locale_file, ND_CONFIG_RECOVERY_PATH) ) {
            LOG_E(TAG, "Error in copying recovery file, src:%s, dest:%s", default_locale.c_str(), ND_CONFIG_RECOVERY_PATH.c_str());
            string err_msg = "nd_config_recovery.ini: copy failed from default: " + default_locale;
            nd_service_obj->send_err_msg(SM_E_CONFIG_INIT_ERROR, ND_CONFIG_RECOVERY_WITH_DEFAULT_LOCALE_FAIL, err_msg);
            return false;
        } else {
            string err_msg = "default_locale copied: " + default_locale;
            nd_service_obj->send_err_msg(SM_E_CONFIG_INIT_ERROR, ND_CONFIG_RECOVERY_WITH_DEFAULT_LOCALE_PASS, err_msg);
            return true;
        }
    } else if(file_is_present(default_backup_locale) == true) {
        LOG_E(TAG, "default_locale %s is not present, copying default locale from backup as nd_config_recovery.ini", default_locale_file.c_str());
        if( false == file_copy(default_backup_locale, ND_CONFIG_RECOVERY_PATH) ) {
            LOG_E(TAG, "Error in copying recovery file, src:%s, dest:%s", default_backup_locale.c_str(), ND_CONFIG_RECOVERY_PATH.c_str());
            string err_msg = "nd_config_recovery.ini: copy failed from backup: " + default_locale;
            nd_service_obj->send_err_msg(SM_E_CONFIG_INIT_ERROR, ND_CONFIG_RECOVERY_WITH_DEFAULT_LOCALE_BACKUP_FAIL, err_msg);
            return false;
        } else {
            string err_msg = "default_locale copied from backup: " + default_locale;
            nd_service_obj->send_err_msg(SM_E_CONFIG_INIT_ERROR, ND_CONFIG_RECOVERY_WITH_DEFAULT_LOCALE_BACKUP_PASS, err_msg);
            return true;
        }
    } else if(file_is_present(default_nd_config_backup_file) == true) {
        LOG_E(TAG, "default_locale file is not present in latest and backup, copying nd_config.ini from backup to nd_config_recovery.ini");
        //Changing permission here because nd_config.ini in back has read write
        //permission.
        if( false == file_copy(default_nd_config_backup_file, ND_CONFIG_RECOVERY_PATH, false, 0444, true) ) {
            LOG_E(TAG, "Error in copying recovery file, src:%s, dest:%s", default_nd_config_backup_file.c_str(), ND_CONFIG_RECOVERY_PATH.c_str());
            string err_msg = "Recovery configs not available: " + default_locale;
            nd_service_obj->send_err_msg(SM_E_CONFIG_INIT_ERROR, ND_CONFIG_RECOVERY_DEFAULT_FAIL, err_msg);
            return false;
        } else {
            string err_msg = "nd_config_recovery missing — restored nd_config from backup";
            nd_service_obj->send_err_msg(SM_E_CONFIG_INIT_ERROR, ND_CONFIG_RECOVERY_DEFAULT_PASS, err_msg);
            return true;
        }
    } else {
        LOG_E(TAG, "No config file present to recover nd_config_recovery: %s, %s, %s", default_locale_file.c_str(), default_backup_locale.c_str(), default_nd_config_backup_file.c_str());
        string err_msg = "nd_config recovery failed: configs missing: " + default_locale_file;
        nd_service_obj->send_err_msg(SM_E_CONFIG_INIT_ERROR, ND_CONFIG_RECOVERY_FAIL, err_msg);
        return false;
    }

    return true;
}

/*
 * read_nd_config_locale_ini : this function will read the region from
 * package_manifest.ini file
 * Based on the config, recovery objects will be created
 * Expected output from the config : US, CA, MX etc
 * based on this the file will be generated : nd_config_US.ini, nd_config_CA.ini
 *
 */
bool read_nd_config_locale_ini(std::vector<std::string>& fileList) {
    // Input validation
    if (!file_is_present(MANIFEST_CONFIG_FILE)) {
        LOG_E(TAG, "Manifest config file not present: %s", MANIFEST_CONFIG_FILE.c_str());
        string err_msg = "manifest config unavailable";
        nd_service_obj->send_err_msg(SM_E_CONFIG_INIT_ERROR, MANIFEST_CONFIG_FILE_NOT_AVAILABLE, err_msg);
        return false;
    }

    Config_parser config(MANIFEST_CONFIG_FILE);
    if (config.getParseStatus() != true) {
        LOG_E(TAG, "Cannot parse config: %s", MANIFEST_CONFIG_FILE.c_str());
        string err_msg = "manifest parsing failed for recovery list";
        nd_service_obj->send_err_msg(SM_E_CONFIG_INIT_ERROR, MANIFEST_CONFIG_PARSE_FAIL, err_msg);
        return false;
    }

    if (config.isPresent("nd_config_locale", "locale") != true) {
        LOG_E(TAG, "nd_config_locale section or locale key not available in config manifest file: %s", MANIFEST_CONFIG_FILE.c_str());
        string err_msg = "Empty locale key/section in nd_config_local";
        nd_service_obj->send_err_msg(SM_E_CONFIG_INIT_ERROR, MANIFEST_LOCALE_CONFIG_FIELD_NOT_AVAILABLE, err_msg);
        return false;
    }

    string locale = config.getConfig("nd_config_locale", "locale", "");
    if (locale.empty()) {
        LOG_E(TAG, "Locale configuration is empty in file: %s", MANIFEST_CONFIG_FILE.c_str());
        string err_msg = "locale field is empty";
        nd_service_obj->send_err_msg(SM_E_CONFIG_INIT_ERROR, LOCALE_CONFIG_LOCALE_FIELD_EMPTY, err_msg);
        return false;
    }

    string default_recovery_locale = config.getConfig("nd_config_locale", "default_locale", "");
    if (default_recovery_locale.empty()) {
        LOG_E(TAG, "default_recovery_locale configuration is empty in file: %s", MANIFEST_CONFIG_FILE.c_str());
        string err_msg = "default_recovery_locale is empty";
        nd_service_obj->send_err_msg(SM_E_CONFIG_INIT_ERROR, LOCALE_CONFIG_DEFAULT_LOCALE_FIELD_EMPTY, err_msg);
    } else {

        // Check and update default recovery locale
        // This will ensure that the nd_config_recovery.ini file is created if not present in latest and backup
        // This is required to ensure that the recovery process has a valid fallback configuration
        if (check_and_update_default_recovery(default_recovery_locale)) {
            LOG_I(TAG, "Default recovery locale updated successfully");
        } else {
            LOG_E(TAG, "Failed to update default recovery locale");
        }
    }

    LOG_I(TAG, "Found locale configuration: %s", locale.c_str());
    LOG_I(TAG, "Found default_recovery_locale configuration: %s", default_recovery_locale.c_str());

    // Parse comma-separated locale values
    std::vector<std::string> regions;
    std::stringstream ss(locale);
    std::string token;

    while (true) {
        try {
            // Clear token before processing to avoid any leftover data
            token.clear();

            // Attempt to read the next token - this could throw exceptions
            if (!std::getline(ss, token, ',')) {
                break; // End of stream reached normally
            }

            // Trim whitespace from token
            token.erase(0, token.find_first_not_of(" \t"));
            token.erase(token.find_last_not_of(" \t") + 1);

            if (token.empty()) {
                LOG_I(TAG, "Empty token found in locale configuration, skipping");
                continue;
            }
            regions.push_back(token);
            LOG_I(TAG, "Parsed locale region: %s", token.c_str());
        } catch (const std::ios_base::failure& e) {
            LOG_E(TAG, "I/O error during getline operation: %s", e.what());
            string err_msg = "Stream I/O error during locale parsing: " + string(e.what());
            nd_service_obj->send_err_msg(SM_E_CONFIG_INIT_ERROR, EMPTY_REGION, err_msg);
            break; // Stop processing on I/O errors
        } catch (const std::exception& e) {
            LOG_E(TAG, "Error processing token '%s': %s", token.c_str(), e.what());
            string err_msg = "Config recovery error: " + string(e.what());
            nd_service_obj->send_err_msg(SM_E_CONFIG_INIT_ERROR, EMPTY_REGION, err_msg);
            continue; // Skip this token and continue with next
        }
    }

    if (regions.empty()) {
        LOG_E(TAG, "No valid locale regions found in configuration");
        string err_msg = "invalid region in config, LOCALE : " + locale + ", DEFAULT: " + default_recovery_locale;
        nd_service_obj->send_err_msg(SM_E_CONFIG_INIT_ERROR, EMPTY_REGION, err_msg);
        return false;
    }

    // Generate file paths for each region
    int counter = 0, status = 1;
    bool send_msg = false;
    std::string status_msg = "";
    std::string file_err_msg = "Locale config not found: ";
    for (const auto& region : regions) {
        string file_name = ND_CONFIG_PREFIX + region + ND_CONFIG_FILE_EXTENSION;
        fileList.push_back(file_name);
        counter++;
        status = 1; //reset status for each file
        if (file_is_present(file_name) == true) {
            if( 0 == file_size(file_name) ) {
                LOG_E(TAG, "File size Zero: %s", file_name.c_str());
                status = 0;
            } else {
                Config_parser c(file_name);
                if (c.getParseStatus() != true) {
                    LOG_E (TAG,"Error in parsing: %s", file_name.c_str());
                    status = 0;
                }
            }
        } else {
            send_msg = true;
            LOG_E(TAG, "Config file is not present: %s", file_name.c_str());
            file_err_msg += region + ", ";
            status = 0;
        }

        status_msg += std::to_string(status) + ",";
        LOG_I(TAG, "Added locale config file: %s : %d", file_name.c_str(), status);
    }
    if(send_msg) {
        LOG_E(TAG, "err message for file not present : %s", file_err_msg.c_str());
        nd_service_obj->send_err_msg(SM_E_CONFIG_INIT_ERROR, CONFIG_FILE_NOT_AVAILABLE, file_err_msg);
    }
    string err_str = "LOCALE: " + locale + " ,DEFAULT: " + default_recovery_locale + " ,STATUS: " + status_msg;
    nd_service_obj->send_err_msg(SM_I_CONFIG_STATUS_INFO, fileList.size(), err_str);
    LOG_I(TAG, "Successfully processed %zu locale configuration files", fileList.size());
    return true;
}

//Initialize structures
bool recovery_init(int poll) {
    LOG_I(TAG, "Init recovery");
    poll_interval = poll;
    string err_msg = "";
    std::vector<std::string> locale_config_file_list;
    //Create recovery object for nddevice.ini
    LOG_I(TAG, "Creating object: %s", NDDEVICE_PATH.c_str());
    Recovery *r1 = new RecoveryIni1(NDDEVICE_PATH);
    //Push essential keys to structutre, these keys will be monitored
    for( int i=0; i< sizeof(keys_nddevice) / sizeof(pair<string,string>); i++ ) {
        r1->add_keys(keys_nddevice[i]);
        LOG_I(TAG, "section: %s key: %s", keys_nddevice[i].first.c_str(), keys_nddevice[i].second.c_str());
    }
    recovery_list.push_back(r1);

    //Create recovery object for deviceconfig.ini
    LOG_I(TAG, "Creating object: %s", DEVICECONFIG_PATH.c_str());
    Recovery *r2 = new RecoveryIni2(DEVICECONFIG_PATH);
    //Push essential keys to structure, these keys will be monitored
    for( int i=0; i< sizeof(keys_deviceconfig) / sizeof(pair<string,string>); i++ ) {
        r2->add_keys(keys_deviceconfig[i]);
        LOG_I(TAG, "section: %s key: %s", keys_deviceconfig[i].first.c_str(), keys_deviceconfig[i].second.c_str());
    }
    recovery_list.push_back(r2);

    //Commenting out this as bagheera_config.ini is now monitored using
    // md5-based recovery mechanism, which helps to keep the backup of the
    // latest file that comes with OTA update

    /*
    //Create recovery object for bagheeraconfig.ini
    LOG_I(TAG, "Creating object: %s", BAGHEERA_CONFIG_PATH.c_str());
    Recovery *r3 = new RecoveryIni3(BAGHEERA_CONFIG_PATH);
    recovery_list.push_back(r3);
    */

    // Create recovery object for cloudconfig.ini
    LOG_I(TAG, "Creating object: %s", CLOUDCONFIG_PATH.c_str());
    Recovery *r4 = new RecoveryIni3(CLOUDCONFIG_PATH);
    recovery_list.push_back(r4);

    // Keep the nd_config related recovery in a block to maintain the flow.
    // [1] Read the package_menifest file and get the region
    // [2] create the config file based on region
    // [3] check if nd_config_recovery is proper, if not recover it with default
    // config. If default config is not available, recover the file with
    // nd_config.ini from backup folder

    {
        // Read locale configuration files with error checking
        if (read_nd_config_locale_ini(locale_config_file_list) == false) {
            LOG_E(TAG, "Failed to read locale configuration files");
            LOG_I(TAG, "Continuing recovery initialization without locale-specific config files");
        } else {

            // Create recovery objects for locale-specific config files
            size_t created_objects = 0;
            for (const auto& locale_file : locale_config_file_list) {
                created_objects++;
                Recovery* locale_recovery = new RecoveryIni3(locale_file);
                recovery_list.push_back(locale_recovery);
                LOG_I(TAG, "Creating object: %s", locale_file.c_str());
            }

            LOG_I(TAG, "Created recovery objects for %zu out of %zu locale configuration files", created_objects, locale_config_file_list.size());
        }

        /*
         * check if nd_config_recovery.ini file is not present in latest
         * If read_nd_config_locale_ini returns false, it means nd_config_recovry
         * update has not happened
         * For the same reason calling this API from here before adding
         * nd_config_recovery file for recovery monitoring
         *
         * Create recovery object for nd_config_recovery after the
         * check_and_update_default_recovery function call, if the file is not
         * available, nd_config.ini from the back should be copied as
         * nd_config_recovery.ini
         *
         */
        if (check_and_update_default_recovery("")) {
            LOG_I(TAG, "Default recovery locale updated successfully");
        } else {
            LOG_E(TAG, "Failed to update default recovery locale");
        }
        LOG_I(TAG, "Creating object: %s", ND_CONFIG_RECOVERY_PATH.c_str());
        Recovery *r5 = new RecoveryIni3(ND_CONFIG_RECOVERY_PATH);
        recovery_list.push_back(r5);
    }

    // Register bagheera_config.spec using md5-based recovery.
    // The spec file is used to validate the bagheera_override.ini whenever a new config is pushed.
    // Refer DT-2397 for more details.
    const std::string SPEC_FILE = LINK_LATEST + "/bagheera_config.spec";
    LOG_I(TAG, "Creating object: %s", SPEC_FILE.c_str());
    Recovery *r_bagheera_config_spec = new RecoveryMd5sum(SPEC_FILE);
    recovery_list.push_back(r_bagheera_config_spec);

    // Register bagheera_config.ini using md5-based recovery.
    LOG_I(TAG, "Creating object: %s", BAGHEERA_CONFIG_PATH.c_str());
    Recovery *r_bagheera_config_ini = new RecoveryMd5sum(BAGHEERA_CONFIG_PATH);
    recovery_list.push_back(r_bagheera_config_ini);

    // Register bagheera_override.ini using md5-based recovery.
    const std::string BAGHEERA_OVERRIDE_FILE = nd_device_obj->get_bagheera_override_path();
    LOG_I(TAG, "Creating object: %s", BAGHEERA_OVERRIDE_FILE.c_str());
    Recovery *r_bagheera_override_ini = new RecoveryMd5sum(BAGHEERA_OVERRIDE_FILE);
    recovery_list.push_back(r_bagheera_override_ini);

    //Take backup of files that are not corrupted
    bool res = recovery_take_backup();
    LOG_I(TAG, "Backup result: %d", res);

    //Create a polling thread to keep periodically checking for corruption
    LOG_I(TAG, "Creating thread");
    pthread_t poll_thread;

    if(pthread_create(&poll_thread, NULL, fn_poll_thread, NULL)) {
        LOG_E(TAG, "Error creating thread\n");
        return false;
    }

    return true;
}

//Go through each file and verify sanity
bool recovery_check_sanity() {
    //Loop through recovery objects
    for( vector<Recovery *>::iterator iter = recovery_list.begin(), 
                end = recovery_list.end();
                iter != end; iter++ ) {

        if(*iter == NULL) {
            LOG_E(TAG, "recovery_check_sanity :: Null recovery object found in list, skipping");
            continue;
        }
        if( false == (*iter)->check() ) {
            LOG_E(TAG, "%s is corrupted", (*iter)->get_path().c_str()) ;
            //Return at first error, because this check will be done again
            return false;
        }
    }

    return true;
}

//Recover the files that are corrupted
bool recovery_recover() {
    bool final_res = true;

    //Loop through recovery objects
    for( vector<Recovery *>::iterator iter = recovery_list.begin(), end = recovery_list.end();
                iter != end; iter++ ) {
        if(*iter == NULL) {
            LOG_E(TAG, "recovery_recover :: Null recovery object found in list, skipping");
            continue;
        }
        //Check for corrupted file
        if( false == (*iter)->check() ) {
            LOG_I( TAG, "%s is corrupted, trying to recover", (*iter)->get_path().c_str() );
            const bool res = (*iter)->recover();
            final_res = final_res & res;
            LOG_I( TAG, "%s recovery status: %d", (*iter)->get_path().c_str(), res );
        }
    }

    return final_res;
}

//Funtion to take backup first time after bootup
bool recovery_take_backup() {
    bool final_res = true;

    // Check if any of the registered files need backup
    for( vector<Recovery *>::iterator iter = recovery_list.begin(), end = recovery_list.end();
                iter != end; iter++ ) {

        if(*iter == NULL) {
            LOG_E(TAG, "recovery_take_backup :: Null recovery object found in list, skipping");
            continue;
        }
    
        //If otacheck is running, do not take backup
        if( (*iter)->get_path() == NDDEVICE_PATH ) {
            string searchString = "RUN_STATE";
            string fileContent = readFileToString(nd_device_obj->get_otacheck_state_path());
            bool searchStringFound = (fileContent.find(searchString) != string::npos);
            if(searchStringFound){
                LOG_E(TAG, "Otacheck or Update Engine running, skipping backup");
                continue;
            }
        }

        //Check if the file was updated from last reboot
        if( false == (*iter)->needs_update() ) {
            //Not updated, continue to next file
            LOG_I(TAG, "%s skipping backup", (*iter)->get_path().c_str());
            continue;
        }

        //Backup needs to be taken
        LOG_I(TAG, "%s needs backup", (*iter)->get_path().c_str());

        //Backup
        const bool res = (*iter)->backup();
        final_res = final_res & res;
        LOG_I( TAG, "%s backup status: %d", (*iter)->get_path().c_str(), res );
    }

    return final_res;
}


