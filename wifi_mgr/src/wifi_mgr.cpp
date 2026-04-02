#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <pthread.h>
#include <log.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <climits>
#include <string>
#include <config_parser.h>
#include <nd_task.h>
#include <system_utils.h>
#include "service_utils.h"
#include <nd_ext_cam_utils.h>
#include "nd_msgq.h"
#include "nd_msg_types.h"
#include "nd_msg_utils.h"
#include "nd_time.h"
#include "nd_vbus_info.h"
#include <atomic>
#include "ndmb/nd_msg_interface.h"
#include "ndmb/nd_mbserver.h"
#include <nd_factory.h>
#include <nd_net_utils.h>
#include <condition_variable>

using namespace std;

#define ROUTE_LOGS
#define TAG "WIFI_MGR"

static int PING_RESPONSE_THRESHOLD = 5000;
static string INI_KEY_PING_RESPONSE_THRESHOLD = "ping_response_threshold_msecs";

static int WIFI_SEARCH_SSID_INTERVAL_SECS = 30; //Interval for checking availability of SSIDs
static string INI_KEY_WIFI_SEARCH_SSID_INTERVAL_SECS = "ssid_availability_check_interval_secs";

static int WIFI_CHECK_CONN_INTERVAL_SECS = 60; //Once connected, interval for monitoring connection status
static string INI_KEY_WIFI_CHECK_CONN_INTERVAL_SECS = "wifi_connection_test_interval_secs";

static int WIFI_SECURITY_CHECK_TIMEOUT = 5; // Timed task timeout

static int WIFI_CHECK_TIMEOUT = 60; // Timed task timeout
static string INI_KEY_WIFI_CHECK_TIMEOUT = "wifi_connect_task_timeout_secs";

static int INTERVAL_RETRY_CONNECT = 180000;// Gap between a wifi disconnect (due to slow internet) and retrying connection
static string INI_KEY_INTERVAL_RETRY_CONNECT = "wifi_reconnect_interval_msecs";

static const int SLEEP_BETWEEN_CHECKS = 2;

NDService *nd_service_obj; //nd service object, to detect crashes
static const int WAIT_TIME_HOTSPOT_2 = 5;
static const int WAIT_TIME_HOTSPOT   = 30;
#ifdef KRAIT
static const int WAIT_TIME_BEFORE_BT_CONN = 1;
#else
static const int WAIT_TIME_BEFORE_BT_CONN = 10;
#endif
static const int UNIT_TIME=1;
static const int DEF_SHORT_SLEEP_DUR_IN_MICROS = 100000;
static const int DEFAULT_SLEEP_DURATION = 5;
static const int MAX_RETRY_COUNT = 4;
static const int MAX_FAIL_RETRIES = 4;
static const int MAX_RETRY_DHUB_CONN = 5;
static const string BAGHEERACONFIG_INI = "/home/ubuntu/.nddevice/latest/bagheera_config.ini";
static const string log_dir = "/home/ubuntu/.nddevice/log/wifi_mgr";
static const string default_ssid = "Netradyne-SSID";
static const string default_password = "12345678";
static const string default_iface = "wlan0";
static const string secondary_iface = "secondary";
static string hotspot_interface = "";
static const string hotspot_ip_address = "10.42.0.";
static const string BAGH_CONF_WIFI_SECTION = "wifi";
static const string BAGH_CONF_EXT_CAM_SECTION = "ext_cam";
static const string BAGH_CONF_EXT_CAM_SETTINGS_SECTION = "ext_cam_settings";
static const string BAGH_CONF_WIFI_FALLBACK = "wifi_fallback";
static const string BAGH_CONF_DHCPCD_STATUS = "dhcpcd_enabled";
static const string BAGH_CONF_WIFI_STATIC_IP = "static_ip";
static const string BAGH_CONF_DEF_ROUTE = "route";
static const string BAGH_CONF_WIFI_SECTION_COUNT = "count";
static const string BAGH_CONF_WIFI_SECTION_SSID_NAME_PREFIX = "wifiname";
static const string BAGH_CONF_WIFI_SECTION_SSID_PASS_PREFIX = "wifipassword";
static const string BAGH_CONF_WIFI_SECTION_HOTSPOT = "hotspot_interface";
static const string BAGH_CONF_WIFI_SECTION_HOTSPOT_CHANNEL = "hotspot_channel";
static const string BAGH_CONF_WIFI_SECTION_MAX_DISCONNECTIONS = "max_disconnections";
static const string BAGH_CONF_WIFI_SECTION_OPERATION_MODE = "operation_mode";
static const string BAGH_CONF_DRIVER_LOGIN_SECTION = "driverlogin";
static const string BAGH_CONF_DRIVER_LOGIN_SECTION_LOGIN_SPEED = "login_speed";

#ifdef KRAIT
static const string IOSIXCONFIG = "/data/nd_files/config/iosix_config.ini";
#else
static const string IOSIXCONFIG = "/home/ubuntu/config/iosix_config.ini";
#endif
static const string IOSIX_CONFIG_SECTION = "iosixwifi";
static const string IOSIX_SSID_NAME = "iosixssid";
static const string IOSIX_SSID_PASS = "iosixpassword";
static const string HOSTAPD_CONF_FILE = "/dev/shm/hostapd.conf";

static const int NUM_SSIDS_LIMIT = 16;
static bool wifi_fallback_enabled = true;
static bool ext_cam_enabled = false;
static bool iosix_enabled = false;
static bool wifi_fallback_manage = false;
static bool ext_cam_manage = true;
static bool installer_app_manage = false;
static bool installer_app_active = false;
static bool report_wlan_err_to_cloud = true;
static bool report_dhub_config_corruption = false;
static string installer_app_mac_addr = "";
static string mdvr_ssid = "";
static string mdvr_password = "";

static string iosix_ssid = "";
static string iosix_password = "";

static string wifi_fallback_iface = default_iface;
static const string BT_CHAR_READ_UUID = "6BAF94A2-885E-49B6-AA4C-FC937345555F";
static const string Q_NAME = "WIFI_MGR";
static const string Q_EXT_CAM_NAME = "EXT_CAM";
static const string Q_UPL = "UniUpload";
static const string Q_POWER = "q_power_monitor";
static const string Q_SPEED = "SPEED";
ND_DeviceFactory *nd_device_obj = nullptr;

#ifdef KRAIT
static string g_device_id = "";
#endif
//Server Queue pointer
static nd_msgq_t *server_q;
pthread_mutex_t scan_connect_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t scan_file_mutex = PTHREAD_MUTEX_INITIALIZER;
static const string installerAppActive = "/dev/shm/installerAppActive";
static const int wifi_read_char_delay =2;
static int max_disconnections = 3;
static const int DEFAULT_CHANNEL = 6;
static int hotspot_channel;
static int connected_device_count = 0;
static int dhub_disconnections_mins = 0;
static const int max_dhub_disconnections = 2;
static string hotspot_ssid = "";
static string hotspot_password = "";
#ifdef BAGHEERA2
static const string inet4_check_str = "inet ";
#else
static const string inet4_check_str = "inet addr:";
#endif
static bool sta_setup_done = false;
static bool monitor_hotspot_done = false;
bool persist_ap_mode = false;

static int gInstallerBandType = 'b';
const int UPDATE_WIFI_INFO_INTERVAL = 60;
wifi_info_msg_t wifi_msg = {};
secondary_wifi_info_msg_t sec_wifi_msg = {};
vector<string> wifi_info_cmds;
vector<string> sec_wifi_info_cmds;
vector<string> wifi_conn_info_clients;
static string vbus_mac_id = "default";
static string connected_devices;

static vector <pair<string, string>> ssids_from_ini;
static wifi_mode_t dhub_wifi_mode = eAPMode;
static wifi_mode_t device_wifi_mode = eSTAMode;
static wifi_mode_t current_wifi_mode = eSTAMode;
static const string DHUB_WIFI_MODE_CLIENTS[] = {Q_EXT_CAM_NAME, Q_UPL, Q_POWER};
static const int NUM_DHUB_WIFI_MODE_CLIENTS = sizeof (DHUB_WIFI_MODE_CLIENTS)/sizeof (string);
static wifi_mode_t prev_device_wifi_mode = eSTAMode;

atomic <bool> hotspot_created(false);
pthread_t ext_cam_wifi_th, wifi_scan_th, wifi_fallback_th, monitor_hotspot_th, update_wifi_info_th;
/*NDMBServer server(Q_NAME);
std::shared_ptr<ndmbmsg_vbus_wifi_updates_t>  wifi_update(new ndmbmsg_vbus_wifi_updates_t);*/

static int msg_idx = 0;
bool ignition_status = false;


bool is_dhcpcd_enabled = true;
bool station_up = false; // Flag to indicate if station is up
string static_ip_address = "10.10.10.20"; // Def for DHUB
string def_route = "10.10.10.254"; // Def for DHUB
#ifdef AUTOMATION
constexpr char * AUTOMATION_CONFIG = "/home/ubuntu/config/automation_config.ini";
#endif
bool dta_enabled = false;
int drv_login_speed = 10;
int drv_login_time = 10;
int current_speed = 0;

bool start_wifi_scan = false;
mutex start_wifi_scan_mutex;
condition_variable start_wifi_scan_cv;

enum ScanType
{ ACTIVE_SCAN,
  PASSIVE_SCAN
};

bool isProcessRunning(string &processName)
{
    string cmd = "ps -ef | grep -v grep | grep \"" + processName + "\"";

    string resp;
    bool ret = system_execute_with_resp("PGREP", cmd, resp);

    if(resp == "")
    {
        return false;
    }
    else
    {
        LOG_I(TAG, "isProcessRunning Resp %s", resp.c_str());
    }
    return true;
}
struct wifi_info_s {
    char ssid[32];
    char password[64];
    char wlan_iface[32];
};

static string get_msgq_name() {
    return Q_NAME;
}

static bool check_for_service(string Q_NAME)
{
    int serviceAvail = true;
    int timeout = 0;
    LOG_I(TAG, "Checking For %s Service",Q_NAME.c_str());
    while( ! is_msg_q_created(Q_NAME) )
    {
        sleep(1);
        if(timeout == 5)
        {
            serviceAvail = false;
            break;
        }
        timeout++;
    }

    if(serviceAvail)
        LOG_I(TAG, "%s Client Message Q Created",Q_NAME.c_str());
    else
        LOG_E(TAG, "%s Client Message Q Not Created",Q_NAME.c_str());

    return serviceAvail;
}

bool req_ignition_status()
{
    generic_msg_t req_igni_status;
    if (!send_msg (&req_igni_status, GET_POWERMON_IGNITION_STATUS, sizeof (req_igni_status), get_msgq_name(), Q_POWER, msg_idx++)) {
        LOG_E (TAG,"Failed to Send Ignition Status Request Message");
        return false;
    }
    return true;
}

void check_and_request_ignition_status()
{
    bool isPowerServiceAvail = check_for_service(Q_POWER);
    if(isPowerServiceAvail)
    {
        if( req_ignition_status())
        {
            LOG_E (TAG,"Request For Ignition Status Sent");
        }
        else
        {
            LOG_I (TAG,"Request For Ignition Status Falied");
        }
    }
    else
    {
        LOG_E(TAG,"Power Monitor Service Is Not Available");
    }
}

bool set_ignition_status(bool ign_status)
{
    ignition_status = ign_status;
}

bool get_ignition_status()
{
    return ignition_status;
}

/*#ifdef KRAIT
void kill_process(const std::string &process) {

    string cmd = "pkill -f \"" + process + "\"";
    const std::string cmd_tag = "KILL " + process;
    int retval = system_execute(cmd_tag, cmd);
    if(retval != 0) {
        LOG_E(TAG, "failed to pkill %s, trying force kill", process.c_str());

        cmd = "pkill -9 -f \"" + process + "\"";
        retval = system_execute(cmd_tag, cmd);
        if(retval != 0) {
            LOG_E(TAG, "failed to pkill %s forcefully", process.c_str());
        }
    }
}
#endif*/

static bool check_if_wifi_iface_exists(string wlan_iface) {
    string cmd = "ifconfig | grep " + wlan_iface;
    string resp = "";

    if(system_execute_with_resp("WIFI_EXIST", cmd, resp) == false) {
        LOG_E (TAG, "CMD: %s exec failed", cmd.c_str());
        return false;
    }

    if(resp.find(wlan_iface) != string::npos) {
        LOG_I(TAG, "found %s interface", wlan_iface.c_str());
        return true;
    }

    return false;
}

static bool check_for_wlan_iface(string wlan_iface) {
    int retry_count = 0;
    string cmd = "ifconfig | grep " + wlan_iface;


    do {
        string resp = "";
        if(system_execute_with_resp("WIFI_EXIST", cmd, resp) == false) {
            LOG_E (TAG, "CMD: %s exec failed", cmd.c_str());
            retry_count++;
            continue;
        }

        if(resp.find(wlan_iface) != string::npos) {
            LOG_I(TAG, "found %s interface", wlan_iface.c_str());
            return true;
        }

        //return from here if check is for secondary interface
        if(wlan_iface != default_iface) {
            return false;
        }

        //wifi interface is not up
#ifdef BAGHEERA2
        cmd = "nmcli radio wifi on";

        LOG_I (TAG,"Turning wifi on");
        pthread_mutex_lock(&scan_connect_mutex);
        int ret = system_execute("WIFI_ON", cmd);
        pthread_mutex_unlock(&scan_connect_mutex);

        if(ret != 0) {
            LOG_E(TAG, "cmd to turn wifi on failed");
        }

        sleep (1); //Small delay will be a good to have after turning radio on

        //run up command
        cmd = "ifconfig " + wlan_iface + " up";
#else
        cmd = "ifconfig " + wlan_iface + " up";
#endif
        LOG_I(TAG, "bringing wifi interface up");
        if(system_execute("WIFI_ON", cmd) != 0) {
            LOG_E(TAG, "cmd to bring wifi interface up failed");
        }

        sleep(1);
        retry_count++;
    } while(retry_count < MAX_RETRY_COUNT);

#ifdef BAGHEERA2
    // If code reaches here, try unblocking rfkill wifi before return false
    cmd = "rfkill unblock wifi";

    LOG_I (TAG,"rfkill unblock wifi");
    if(system_execute("WIFI_UNBLOCK", cmd) != 0) {
        LOG_E(TAG, "cmd to unblock wifi failed");
    }

#endif
    cmd = "ifconfig " + wlan_iface + " up";
    if(system_execute("WIFI_ON", cmd) != 0) {
        LOG_E(TAG, "cmd to bring wifi interface up failed");
    }

    return false;
}

void check_and_report_wlan0_interface_availability()
{
    if(!check_if_wifi_iface_exists(default_iface))
    {
        LOG_E(TAG, "wlan0 interface not found");
        if(get_ignition_status() == true)
        {
            nd_service_obj->send_err_msg(SM_E_WMGR_WLAN_ERR, NDService::UNUSED_ERR_AUX_CODE, "wlan0 not found" );
        }
    }
}

static bool scan_for_ssid(string ssid, string wlan_iface, int max_retry_count) {
    bool home_ssid_found = false;

#ifdef BAGHEERA2
    string cmd = "iwlist " + wlan_iface + " scan | grep -w \""+ ssid + "\"";
#else
    string cmd = "iw " + wlan_iface + " scan | grep -w \""+ ssid + "\"";
#endif
    LOG_D(TAG, "scan cmd: %s", cmd.c_str());
    int scan_retry_count = 0;

    do {

        home_ssid_found = false;
        string ssid_str = "";
        pthread_mutex_lock(&scan_connect_mutex);
        bool ret = system_execute_with_resp("WIFI_SCAN", cmd, ssid_str);
        pthread_mutex_unlock(&scan_connect_mutex);

        if(ret == false) {
            LOG_I (TAG, "scan cmd exec failed");
            scan_retry_count++;
            sleep(1);
            continue;
        }

        if(ssid_str == "") {
            LOG_I (TAG,"home_ssid_found: %d",home_ssid_found);
            scan_retry_count++;
            sleep(1);
            continue;
        }

        // extracting ssid from line
       LOG_I(TAG, "%s", ssid_str.c_str());
       std:: string s = ssid_str;

       size_t pos = 0;
       size_t pos_1 =0;
       string sub_string = "";
       std::string separator = "\n";

       while ((pos = s.find(separator)) != std::string::npos) {
             sub_string = "";
             sub_string = s.substr(0, pos);
             pos_1 = sub_string.find(":");

             if (pos_1 != string::npos){
#ifdef BAGHEERA2
                 // ssid name will be like ESSID:"Netradyne"\n
                 // initial +2 is for ':' and '"' and +1 is for '"', \n is removed because based on \n
                 // parsing is happening
                 sub_string.assign (sub_string, pos_1+2, sub_string.length() - (pos_1+2+1));
#else
                 // Bagheera- ssid name will be like ESSID:"Netradyne"\n
                 // Krait - with this logic one extra char getting striped in Krait
                 // In Krait ssid are like SSID: ACTFIBERNET
                 // initial +2 is for ':' and '"' and +1 is for '"', \n is removed because based on \n
                 // parsing is happening
                 sub_string.assign (sub_string, pos_1+2, sub_string.length() - (pos_1+2));
#endif

                 LOG_I(TAG, "going to check : %s and %s", sub_string.c_str(), ssid.c_str());
                 if (sub_string == ssid){
                    LOG_I(TAG, "home ssid: %s found", sub_string.c_str());
                    return true;
                 }
             }
             s.erase(0, pos + separator.length());
        }

        LOG_I (TAG, "home_ssid_found: %d", home_ssid_found);
        scan_retry_count++;
        sleep(1);
    } while (scan_retry_count < max_retry_count);

    return home_ssid_found;
}

static bool add_wifi_iface(string wlan_iface, string mode) {
    int ret2 = 0;

    string cmd_to_add_iface = "";

    if(mode == "ap") {
        cmd_to_add_iface = "iw phy phy0 interface add " + wlan_iface + " type __ap";
    }
    else
    {
        cmd_to_add_iface = "iw phy phy0 interface add " + wlan_iface + " type managed";
    }

    LOG_I(TAG, "%s\n", cmd_to_add_iface.c_str());

    int retry_count = 0;
    while (retry_count < MAX_RETRY_COUNT) {
#ifdef KRAIT
        // proceed to add iface only if default iface exist
        if(check_for_wlan_iface(default_iface) == false) {
            sleep(DEFAULT_SLEEP_DURATION);
            retry_count++;
            continue;
        }
#endif
        pthread_mutex_lock(&scan_connect_mutex);
        ret2= system_execute("INSTALLER_APP_WIFI_IFACE_ADD", cmd_to_add_iface);
        pthread_mutex_unlock(&scan_connect_mutex);

        if (ret2 != 0) {
            LOG_E(TAG, "command to add %s interface failed", wlan_iface.c_str());
            sleep(DEFAULT_SLEEP_DURATION);
            retry_count++;
            continue;
        }
#ifdef KRAIT
        // bring interface up after adding
        string cmd = "ifconfig " + wlan_iface + " up";
        int retval = system_execute("IFUP", cmd);
        if(retval != 0) {
            LOG_E(TAG, "failed to run ifconfig up");
        }

        sleep(UNIT_TIME);
#endif
        // check once again after running iface add command
        bool wifi_iface_exist = check_for_wlan_iface(wlan_iface);

        if(wifi_iface_exist == true) {
            LOG_I(TAG, "%s interface added successfully", wlan_iface.c_str());
            return true;
        }

        sleep(DEFAULT_SLEEP_DURATION);
        retry_count++;
    }

    return false;
}

#ifdef KRAIT

void generate_hostapd_conf_krait()
{
    ofstream conf_file(HOSTAPD_CONF_FILE);
    conf_file << "\ninterface=wlan0\nssid=";
    conf_file << hotspot_ssid;
    conf_file << "\nhw_mode=g\nchannel=" + to_string(hotspot_channel) + "\nauth_algs=3\nwpa_group_rekey=7200\nieee80211n=1\nwpa=3\nwpa_passphrase=";
    conf_file << hotspot_password;
    conf_file << "\nwpa_key_mgmt=WPA-PSK\nwpa_pairwise=CCMP\nrsn_pairwise=CCMP";
    conf_file.close();
}
#endif

bool restart_network_manager()
{
    LOG_I(TAG,"Dnsmasq is not running on wlan0,RESTARTING NETWORK MANAGER");
    string cmd1="systemctl status NetworkManager";
    string response1="";
    int ret = system_execute_with_resp("NetworkManager status", cmd1, response1);
    LOG_I(TAG,"NetworkManager status= %s",response1.c_str());
    string cmd3="systemctl restart NetworkManager";
    string resp3="";
    if(system_execute_with_resp("NO_WIFI", cmd3, resp3) == false)
    {
        LOG_E (TAG, "CMD: %s exec failed", cmd3.c_str());
        return false;
    }
    else
    {
         LOG_I(TAG,"Network manager restarted with cmd %s",cmd3.c_str());
        sleep(5);

    }

    return true;

}

bool check_interface_status(const string& interface)
{
    string check_status_cmd = "nmcli -t -f DEVICE,STATE device status | grep \"" + interface + ":connected\"";
    string result = "";

    if(system_execute_with_resp("CHECK_INTERFACE_CONN", check_status_cmd, result) == false)
    {
        LOG_E(TAG, "CMD: %s check_interface_status failed", check_status_cmd.c_str());
        return false;
    }

    if (!result.empty())
    {
        LOG_I(TAG, "%s is already connected", interface.c_str());
        return true;
    }

    return false;
}

bool bring_up_hotspot()
{
    LOG_I(TAG,"Dnsmasq is not running on wlan0, Bringing Up Hotspot Again");

    string cmd="sudo nmcli con down DriveriHostspot";
    string resp="";

    if(system_execute_with_resp("HOTSPOT_CONN_DOWN", cmd, resp) == false)
    {
        LOG_E (TAG, "CMD: %s con down DriveriHostspot failed", cmd.c_str());
    }
    else
    {
        LOG_I(TAG,"Hotspot Brought Down With Command %s",cmd.c_str());
    }

    sleep(SLEEP_BETWEEN_CHECKS);

    cmd="sudo nmcli con up DriveriHostspot";
    resp="";

    if(system_execute_with_resp("HOTSPOT_CONN_UP", cmd, resp) == false)
    {
        LOG_E (TAG, "CMD: %s con up DriveriHostspot failed", cmd.c_str());
        return false;
    }
    else
    {
        LOG_I(TAG,"Hotspot Brought Up With Command %s",cmd.c_str());
    }

    return true;
}

bool restart_dnsmasq()
{
#ifdef KRAIT
    LOG_I(TAG,"Dnsmasq not running starting it");
    string cmd = "pkill dnsmasq; dnsmasq -i wlan0 --conf-file=/etc/dnsmasq.conf --except-interface=lo --dhcp-range=10.42.0.10,10.42.0.100,5m --dhcp-leasefile=/data/dnsmasq_d.leases";
    LOG_I(TAG, "Hotspot Step 5.5 Cmd: %s", cmd.c_str());
    int retval = system_execute("RESTART_DNSMASQ", cmd);
    if(retval < 0)
    {
        LOG_I(TAG, "failed to start dnsmasq");
        return false;
    }
#else
    if(bring_up_hotspot())
    {
        return true;
    }
#endif
    return false;
}


#ifdef KRAIT
void delete_default_route(string wlan_iface) {
    string cmd = "route del default dev " + wlan_iface;
    LOG_I(TAG, "delete route cmd: %s", cmd.c_str());

    if(system_execute("WIFI_DISCONNECT", cmd) != 0) {
        LOG_E(TAG, "failed to run cmd to delete default route entry");
    }
}
#endif

bool wifi_check_availability (string ssid, string wlan_iface,  int max_scan_retry_count)
{

#ifdef BAGHEERA2
    if(wlan_iface != default_iface)
    {
        return false;
    }
#endif

    bool wlan_iface_exist = check_for_wlan_iface(wlan_iface);
    // If wlan interface is not "wlan0", we need to add additional interface
    if(wlan_iface != default_iface) {

        //check if secondary iface is already available
        if(wlan_iface_exist == false) {
            if(add_wifi_iface(wlan_iface, "managed") == false) {
                LOG_E(TAG, "Not able to add %s wifi interface", wlan_iface.c_str());
                return false;
            }
        }
    } else {
        if(wlan_iface_exist == false) {
            LOG_E(TAG, "wlan interface not found");
            return false;
        }
    }

    pthread_mutex_lock(&scan_file_mutex);
    bool checkForSSid =  check_for_ssid(ssid);
    pthread_mutex_unlock(&scan_file_mutex);

    return checkForSSid;
}

bool ip_exist_wlan(string wlan_iface)
{
    string cmd = "ifconfig | grep -A1 " + wlan_iface;
    string resp = "";

    if(system_execute_with_resp("IP_EXIST", cmd, resp) == false) {
        LOG_E(TAG, "failed to run cmd check for ip on wlan iface");
        return false;
    }

    if(resp == "") {
        LOG_E(TAG, "failed to find ip on wlan iface");
        return false;
    }
#ifdef BAGHEERA2
    if (resp.find (inet4_check_str.c_str()) == string::npos)
    {
         LOG_D(TAG, "No ip on wlan interface, %s", inet4_check_str.c_str());
#else
    if (resp.find ("inet addr:") == string::npos)
    {
         LOG_D(TAG, "No ip on wlan interface");
#endif
         return false;
    }

    LOG_D(TAG,"ip on wlan interface exist");
    return true;
}

bool wifi_disconnect (string wlan_iface) {

    if(!ip_exist_wlan(wlan_iface))
    {
        LOG_I(TAG, "Wifi Is Already Disconnected");
        return true;
    }
#ifdef KRAIT
    string cmd = "iw dev " + wlan_iface + " disconnect";
#else
    string cmd = "sudo nmcli dev disconnect " + wlan_iface;
    string response = "Device '" + wlan_iface + "' successfully disconnected";
    string resp = "";
#endif

    pthread_mutex_lock(&scan_connect_mutex);
#ifdef KRAIT
    int ret = system_execute("WIFI_DISCONNECT", cmd);
#else
    bool ret = system_execute_with_resp("WIFI_DISCONNECT", cmd, resp);
#endif
    pthread_mutex_unlock(&scan_connect_mutex);

    string flush_ip_cmd = "sudo ip addr flush dev " + wlan_iface;

    LOG_I(TAG, "flush ip cmd: %s", flush_ip_cmd.c_str());

    if(system_execute("FLUSH_IP", flush_ip_cmd) != 0) {
        LOG_E(TAG, "failed to run cmd to flush ip on wlan iface");
    }

#ifdef KRAIT
    if(ret != 0) {
#else
    if(ret == false) {
#endif
        LOG_E(TAG, "failed to run cmd to disconnect wifi");
        return false;
    }

#ifdef BAGHEERA2
    if (resp.find (response) != string::npos) {
        LOG_I (TAG, "Wifi disconnected");
        return true;
    }

    return false;
#else
    return true;
#endif
}

#ifdef BAGHEERA2
// wifi is secured only when we have WPA2 encryption in our case
// nmcli device wifi list | grep -w "Netradyne" | grep WPA2
bool wifi_secure_check (void *args) {

    wifi_info_s *wifi_info = (wifi_info_s *)args;
    stringstream ss;
    ss << "nmcli device wifi list | grep -w \"" << wifi_info->ssid << "\"" << " | grep WPA2";
    LOG_I(TAG, "Command for security check: %s", ss.str().c_str());

    string cmd = ss.str();
    string resp = "";

    bool ret = system_execute_with_resp("WIFI_SECURITY_CHECK", cmd, resp);
    if(ret == false) {
        LOG_E(TAG, "failed to run cmd for wifi security check");
        return false;
    }

    if(resp.find ("WPA2") != string::npos) {
        LOG_I(TAG, "wifi security check success for %s", (wifi_info->ssid));
        return true;
    }

    LOG_E(TAG, "wifi security check failed for %s", (wifi_info->ssid));
    return false;
}
#endif

bool wifi_connect_int (void *args) {

    wifi_info_s *wifi_info = (wifi_info_s *)args;
    stringstream ss;
    string cmd;
#ifdef KRAIT
    //kill any existing hostapd
    kill_process("hostapd");

    string pname = "wpa_supplicant";
    bool ret_wpa = isProcessRunning(pname);

    if(is_dhcpcd_enabled)
    {
        pname = "dhcpcd " + string(wifi_info->wlan_iface);
        bool ret_dhcpcd = isProcessRunning(pname);
        LOG_I(TAG, "ret_wpa %d, ret_dhcpcd %d", ret_wpa, ret_dhcpcd);

        if ( (ret_wpa == true) && (ret_dhcpcd == true) )
        {
            LOG_I(TAG, "DHCPCD and WPA_SUPPLOCANT are RUNNING not Starting");
            return true;
        }
    }
    else {
        if(ret_wpa == true)
        {
            LOG_I(TAG, "WPA_SUPPLOCANT is RUNNING not Starting");
            return true;

        }
    }


    // kill any wpa_supplicant
    kill_process("wpa_supplicant");

    stringstream conf_ss;
    conf_ss << "/dev/shm/";
    conf_ss << wifi_info->wlan_iface;
    conf_ss << "_wpa_supplicant.conf";
    string supplicant_conf_file = conf_ss.str();

    ofstream conf_file(supplicant_conf_file);
    conf_file << "ctrl_interface=/var/run/wpa_supplicant\nupdate_config=1\nnetwork={\nssid=\"";
    conf_file << wifi_info->ssid;
    conf_file << "\"\npsk=\"";
    conf_file << wifi_info->password;
    conf_file << "\"\nkey_mgmt=WPA-PSK\npriority=15\n}";
    conf_file.close();
    ss << "wpa_supplicant -B -D nl80211 -i ";
    ss << wifi_info->wlan_iface;
    ss << " -c ";
    ss << supplicant_conf_file;
#else
    ss << "nmcli dev wifi connect ";
    ss << "\"" << wifi_info->ssid << "\"";
    ss << " password ";
    ss << "\"" << wifi_info->password << "\"";
    ss << " ifname ";
    ss << wifi_info->wlan_iface;
#endif
    cmd = ss.str();
    LOG_I(TAG, "connect cmd: %s", cmd.c_str());
    string resp = "";

    bool ret = system_execute_with_resp("WIFI_CONNECT", cmd, resp);
    if(ret == false) {
        LOG_E(TAG, "failed to run cmd for wifi connect");
        return false;
    }

#ifdef KRAIT
    if(resp == "") {
        LOG_E(TAG, "wifi connect wpa_supplicant resp empty");
        return false;
    }
    if( (ret == true) && (is_dhcpcd_enabled == false) )
    {
        LOG_I(TAG, "DHCPCD is disabled, so not starting");
        string cmd = "ifconfig " + string(wifi_info->wlan_iface) + " " + static_ip_address + " netmask 255.255.255.0";
        string route_cmd = "route add -net 10.10.10.0 netmask 255.255.255.0 gw " + def_route +" dev " + string(wifi_info->wlan_iface);
        bool ip_set = system_execute("IP_SET", cmd);
        bool route_set = system_execute("Route_SET", route_cmd);
        if(ip_set != 0)
        {
            LOG_E(TAG, "failed to set ip address %s", cmd.c_str());
            return false;
        }
        if(route_set != 0)
        {
            LOG_E(TAG, "failed to set route %s", route_cmd.c_str());
            return false;
        }
        return true;

    }

    LOG_I(TAG, "cmd resp: %s", resp.c_str());


    kill_process("dhcpcd " + string(wifi_info->wlan_iface));

    ss.str("");
    ss << "dhcpcd ";
    ss << wifi_info->wlan_iface;
    cmd = ss.str();
    LOG_I(TAG, "connect dhcpcd cmd: %s", cmd.c_str());
    resp = "";
    ret = system_execute_with_resp("WIFI_CONNECT", cmd, resp);
    if(ret == false) {
        LOG_E(TAG, "failed to run cmd for wifi connect");
        return false;
    }
#else
    if(resp == "") {
        LOG_E(TAG, "wifi connect resp empty");
        return false;
    }

#endif
    LOG_I(TAG, "cmd resp: %s", resp.c_str());

#ifdef KRAIT
return true;
#else
 if (resp.find ("successfully activated") != string::npos) {
        LOG_I (TAG, "Wifi Connected to %s on %s", wifi_info->ssid, wifi_info->wlan_iface);
        return true;
    }
    return false;
#endif
}

bool wifi_connect(string ssid, string password, string wlan_iface, bool is_mdvr)
{
    if(file_is_present(installerAppActive))
    {
        LOG_I(TAG, " Installer App Active - Ignoring the Wifi Connect Request");
        return false;
    }

    if (is_mdvr)
    {
        int64_t pair_time;
        if(installer_app_active || get_installer_pair_time(pair_time))
        {
            LOG_I(TAG,"Not Going To Connect and Recover DHUB Currently As Installer App Is Active");
            return false;
        }
    }

    wifi_info_s wifi_info;
    strncpy(wifi_info.ssid, ssid.c_str(), sizeof(wifi_info.ssid)-1);
    strncpy(wifi_info.password, password.c_str(), sizeof(wifi_info.password)-1);
    strncpy(wifi_info.wlan_iface, wlan_iface.c_str(), sizeof(wifi_info.wlan_iface)-1);
    wifi_info.ssid[sizeof(wifi_info.ssid)-1] = '\0';
    wifi_info.password[sizeof(wifi_info.password)-1] = '\0';
    wifi_info.wlan_iface[sizeof(wifi_info.wlan_iface)-1] = '\0';

    task_result_t timed_task_result;

    pthread_mutex_lock(&scan_connect_mutex);
#ifdef BAGHEERA2
    // Check if wifi SSID is secure
    timed_task_result =
          nd_timed_task(wifi_secure_check, WIFI_SECURITY_CHECK_TIMEOUT, (void *)&wifi_info, "Wifi security check task");
    if(timed_task_result != TASK_SUCCESS) {
        LOG_E (TAG, "Wifi security check timed task failed with result %d", timed_task_result);
        pthread_mutex_unlock(&scan_connect_mutex);
        return false;
    }

    sleep (2); // adding sleep to prevent 2 nmcli commands from running back to back and causing a time out

    // Actual wifi connection step
#endif
    timed_task_result =
          nd_timed_task(wifi_connect_int, WIFI_CHECK_TIMEOUT, (void *)&wifi_info, "Wifi connect task");
    pthread_mutex_unlock(&scan_connect_mutex);

    if(timed_task_result != TASK_SUCCESS) {
        LOG_E (TAG, "Wifi connect timed task failed with result %d", timed_task_result);
        return false;
    }

    return true;
}


bool read_wifi_fallback_config () {
    stringstream ss;
    int num_ssids = 0;
    Config_parser automation_conf(AUTOMATION_CONFIG);
    Config_parser bagh_conf (BAGHEERACONFIG_INI);

    bool is_val_overridden = false;

    bool val_overridden = true;
    string s = bagh_conf.getConfig(BAGH_CONF_WIFI_SECTION, BAGH_CONF_WIFI_FALLBACK, "",
            true, val_overridden);
    string dhcpcd_config = bagh_conf.getConfig(BAGH_CONF_EXT_CAM_SETTINGS_SECTION, BAGH_CONF_DHCPCD_STATUS, "",
            true, val_overridden);
    static_ip_address  = bagh_conf.getConfig(BAGH_CONF_EXT_CAM_SETTINGS_SECTION, BAGH_CONF_WIFI_STATIC_IP, "",
            true, val_overridden);
    def_route  = bagh_conf.getConfig(BAGH_CONF_EXT_CAM_SETTINGS_SECTION, BAGH_CONF_DEF_ROUTE, "",
            true, val_overridden);

    LOG_I(TAG, "DHCP Client is %s", dhcpcd_config.c_str());
    LOG_I(TAG, "Static IP Address is %s", static_ip_address.c_str());
    LOG_I(TAG, "Default Route is %s", def_route.c_str());

    if (dhcpcd_config == "enable") {
        is_dhcpcd_enabled = true;
    }
    else if(dhcpcd_config == "disable") {
        is_dhcpcd_enabled = false;
    }
    else{
        is_dhcpcd_enabled = true;
    }
    if (dta_enabled == false) {
        if (bagh_conf.getParseStatus() == false) {
            LOG_E(TAG, "failed to parse bagheera config");
            return false;
        }

        bool val_overridden;
        if (s != "enable") {
            LOG_I (TAG,"Wifi fall back is not enabled in %s",
                                                BAGHEERACONFIG_INI.c_str());
            return false;
        }

        string ssid_count = bagh_conf.getConfig(BAGH_CONF_WIFI_SECTION, BAGH_CONF_WIFI_SECTION_COUNT, "",
                                                                    true, val_overridden);
        if (ssid_count == "") {
            LOG_E (TAG,"wifi-count returned empty");
            num_ssids = 0;
        }

        ss.str("");
        ss << ssid_count;
        ss >> num_ssids;
        LOG_D (TAG,"Found %d ssids in config file", num_ssids);
        ss.clear();

        if (num_ssids > NUM_SSIDS_LIMIT) {
            num_ssids = NUM_SSIDS_LIMIT;
        }

        for (int i=0; i<num_ssids; i++) {
            ss.str("");
            ss << BAGH_CONF_WIFI_SECTION_SSID_NAME_PREFIX << i;
            string wifi_name = bagh_conf.getConfig(BAGH_CONF_WIFI_SECTION, ss.str(), "", true, val_overridden);
            if (wifi_name == "") {
                LOG_E (TAG,"wifi name ssid %d is empty. Skipping",i);
                continue;
            }

            LOG_I(TAG,"wifi name found in cfg file is %s", wifi_name.c_str());
            ss.str("");
            ss << BAGH_CONF_WIFI_SECTION_SSID_PASS_PREFIX << i;
            string wifi_password = bagh_conf.getConfig(BAGH_CONF_WIFI_SECTION, ss.str(), "", true, val_overridden);
            if (wifi_password == "") {
                LOG_E (TAG,"wifi_password for ssid %d is empty. Assuming no password");
            }

            //Just push back. Availabilty is checked later
            ssids_from_ini.push_back(make_pair(wifi_name, wifi_password));
        }

        LOG_D(TAG,"stored %d ssids from config file",ssids_from_ini.size());

        //Actual number of valid ssids
        num_ssids = ssids_from_ini.size();
        if (num_ssids == 0) {
            LOG_E(TAG,"No valid SSIDs from config file, setting defaults");
            ssids_from_ini.push_back(make_pair(default_ssid, default_password));
        }

        int temp = 0;
        // Assign global threshold values from config file
        string ping_resp_threshold = bagh_conf.getConfig (BAGH_CONF_WIFI_SECTION, INI_KEY_PING_RESPONSE_THRESHOLD, "", true, val_overridden);
        if (string_to_integer (ping_resp_threshold, temp)) {
            PING_RESPONSE_THRESHOLD = temp;
        }
        LOG_I (TAG, "Ping response threshold ms: %d", PING_RESPONSE_THRESHOLD);

        string ssid_search_interval = bagh_conf.getConfig (BAGH_CONF_WIFI_SECTION, INI_KEY_WIFI_SEARCH_SSID_INTERVAL_SECS, "", true, val_overridden);
        if (string_to_integer (ssid_search_interval, temp)) {
            WIFI_SEARCH_SSID_INTERVAL_SECS = temp;
        }
        LOG_I (TAG, "ssid_availability_check_interval_secs: %d", WIFI_SEARCH_SSID_INTERVAL_SECS);

        string wifi_conn_test_interval = bagh_conf.getConfig (BAGH_CONF_WIFI_SECTION, INI_KEY_WIFI_CHECK_CONN_INTERVAL_SECS, "", true, val_overridden);
        if (string_to_integer (wifi_conn_test_interval, temp)) {
            WIFI_CHECK_CONN_INTERVAL_SECS = temp;
        }
        LOG_I (TAG, "wifi_conn_test_interval_secs: %d", WIFI_CHECK_CONN_INTERVAL_SECS);

        string wifi_conn_task_timeout = bagh_conf.getConfig (BAGH_CONF_WIFI_SECTION, INI_KEY_WIFI_CHECK_TIMEOUT, "", true, val_overridden);
        if (string_to_integer (wifi_conn_task_timeout, temp)) {
            WIFI_CHECK_TIMEOUT = temp;
        }
        LOG_I (TAG, "wifi_connect_task_timeout_secs: %d", WIFI_CHECK_TIMEOUT);

        string wifi_reconnect_interval = bagh_conf.getConfig (BAGH_CONF_WIFI_SECTION, INI_KEY_INTERVAL_RETRY_CONNECT, "", true, val_overridden);
        if (string_to_integer (wifi_reconnect_interval, temp)) {
            INTERVAL_RETRY_CONNECT = temp;
        }
        LOG_I (TAG, "wifi_reconnect_interval_msecs: %d", INTERVAL_RETRY_CONNECT);

        return true;
    }
    #ifdef AUTOMATION
    else{
        ss.str("");
        ss << BAGH_CONF_WIFI_SECTION_SSID_NAME_PREFIX << 0;
        string wifi_name = automation_conf.getConfig(BAGH_CONF_WIFI_SECTION, ss.str(), "", false, is_val_overridden);

        ss.str("");
        ss << BAGH_CONF_WIFI_SECTION_SSID_PASS_PREFIX << 0;
        string wifi_password = automation_conf.getConfig(BAGH_CONF_WIFI_SECTION, ss.str(), "", false, is_val_overridden);

        if (wifi_name == "" || wifi_password == "") {
            LOG_E (TAG,"TEST wifi name found in cfg file is %d is empty. Connecting to Netradynelab");
            string automation_ssid = "Netradynelab";
            string automation_password = "Kh8Q=cQ2E8MV";
            ssids_from_ini.push_back(make_pair(automation_ssid, automation_password));
        } else {
            LOG_I(TAG,"TEST wifi name found in cfg file is %s", wifi_name.c_str());
            //Just push back. Availabilty is checked later
            ssids_from_ini.push_back(make_pair(wifi_name, wifi_password));
        }
        return true;
    }
    #endif

}

#ifdef BAGHEERA2
void delete_default_route(string wlan_iface) {
    string cmd = "route del default dev " + wlan_iface;

    if(system_execute("WIFI_DISCONNECT", cmd) != 0) {
        LOG_E(TAG, "failed to run cmd to delete default route entry");
    }
}

#else

void change_route_metric(string line, string iface, string metric) {
    string del_cmd = "busybox ip route del " + line + " 2>&1";

    LOG_I(TAG, "%s route del cmd: %s", iface.c_str(), del_cmd.c_str());
    string resp = "";
    if(!system_execute_with_resp("DEL_ROUTE", del_cmd, resp)) {
        LOG_E(TAG, "failed to del default %s route", iface.c_str());
    }

    LOG_I(TAG, "%s route del resp: %s", iface.c_str(), resp.c_str());

    sleep(UNIT_TIME);

    string add_cmd = "busybox ip route add " + line + " metric " + metric + " 2>&1";
    size_t pos = line.find("metric");
    if(pos != string::npos) {
        add_cmd = "busybox ip route add " + line.substr(0, pos) + " metric " + metric + " 2>&1";
    }

    resp = "";
    LOG_I(TAG, "%s route add resp: %s", iface.c_str(), add_cmd.c_str());

    // add new default route with altered metric
    if(!system_execute_with_resp("ADD_ROUTE", add_cmd, resp)) {
        LOG_E(TAG, "failed to add default route with altered metric");
        return;
    }

    LOG_I(TAG, "%s route add resp: %s", iface.c_str(), resp.c_str());
}
#endif

#ifdef BAGHEERA2
void alter_route_metric(string wifi_iface) {
    string cmd = "ip route show";
    string resp = "";

    if(system_execute_with_resp("ROUTE_SHOW", cmd, resp) == false) {
        LOG_E(TAG, "failed to run ip route show cmd");
        return;
    }

    if(resp == "") {
        LOG_E(TAG, "ip route resp empty");
        return;
    }

    stringstream ss(resp);
    string line = "";

    // make the route metric (99) of wlan interface lower than
    // that of eth0 (100) to make data flow through it
    while(std::getline(ss,line,'\n')) {
        if(line == "") {
            continue;
        }

        LOG_I(TAG, "line: %s", line.c_str());
        // check for default route with given wifi iface
        if(line.find(wifi_iface) == string::npos || line.find("default") == string::npos) {
            continue;
        }

        size_t pos = line.find("metric");
        if(pos == string::npos) {
            continue;
        }

        cmd = "ip route add " + line.substr(0, pos) + "metric 99";
        break;
    }

    // add new default route with altered metric
    if(system_execute("ADD_ROUTE", cmd) != 0) {
        LOG_E(TAG, "failed to add default route with altered metric");
        return;
    }

    // delete the existing default route with default metric
    cmd = "ip route del " + line;

    if(system_execute("DEL_ROUTE", cmd) != 0) {
        LOG_E(TAG, "failed to del default route with default metric");
    }
    // log the route after alteration
    cmd = "ip route show";
    resp = "";

    if(system_execute_with_resp("ROUTE_SHOW", cmd, resp) == false) {
        LOG_E(TAG, "failed to run ip route show cmd");
        return;
    }

    LOG_I(TAG, "%s", resp.c_str());
}

#else

void alter_route_metric(string wifi_iface) {
    string show_cmd = "ip route show";
    string show_resp = "";
    string line = "";
    string resp = "";

    int retry_cnt = 0;
    bool default_wifi_route_found = false;
    while(retry_cnt < MAX_RETRY_COUNT && !default_wifi_route_found) {
    show_resp = "";
        if(system_execute_with_resp("ROUTE_SHOW", show_cmd, show_resp) == false) {
            LOG_E(TAG, "failed to run ip route show cmd");
            return;
        }

        if(show_resp == "") {
            LOG_E(TAG, "ip route resp empty");
            return;
        }

        stringstream ss(show_resp);
        line = "";

    while(std::getline(ss,line,'\n')) {
            if(line == "") {
                continue;
            }

            // delete any eth0 default route without any metric to it and add woute with metric
        if(line.find("eth0") != string::npos && line.find("default") != string::npos) {
                if(line.find("metric 100") != string::npos) {
                    LOG_I(TAG, "eth0 route metric set as required");
                } else {
                    change_route_metric(line, "eth0", "100");
                }
                continue;
        }

            LOG_D(TAG, "line: %s", line.c_str());
            // check for default route with given wifi iface
            if(line.find(wifi_iface) == string::npos || line.find("default") == string::npos) {
                continue;
            }

        if(line.find("metric 99") != string::npos) {
                LOG_I(TAG, "%s route metric set as required", wifi_iface.c_str());
                default_wifi_route_found = true;
                continue;
        }

            // set metric 99 for given wifi iface
            change_route_metric(line, wifi_iface, "99");
        }

    sleep(3);
    retry_cnt++;
    }

    // do not log route if default route already found
    if(default_wifi_route_found) {
        return;
    }

    // log the route after alteration
    show_resp = "";
    if(system_execute_with_resp("ROUTE_SHOW", show_cmd, show_resp) == false) {
        LOG_E(TAG, "failed to run ip route show cmd");
        return;
    }

    LOG_I(TAG, "%s", show_resp.c_str());
}
#endif

bool send_msg_get_dhub_wifi_mode_db()
{
    generic_msg_t dhub_wifi_mode_msg;

    for(int i=0; i < NUM_DHUB_WIFI_MODE_CLIENTS; i++ )
    {
        if( false == send_msg(&dhub_wifi_mode_msg, REQ_DHUB_WIFI_MODE_FROM_DB, sizeof(dhub_wifi_mode_msg), get_msgq_name(), DHUB_WIFI_MODE_CLIENTS[i], 0) )
        {
            LOG_E(TAG, "Not Able To Send Get DHUB WIFI Mode From DB Message");
        }
        else
        {
            LOG_I(TAG, "Successfully Sent Get DHUB WIFI Mode From DB Message");
        }
    }
    return true;
}

bool getWifiConnectedDevices(int &count, map<string,AccessoryConnInfo> &accessories_conn_info, string& conn_devices)
{
    string cmd = "ip neigh show dev wlan0";
    string resp;
    conn_devices = "";

    if(system_execute_with_resp("CONN_DEV_INFO", cmd, resp) == false)
    {
        LOG_E(TAG, "CMD: %s Exec Failed", cmd.c_str());
        return false;
    }
    else
    {
        LOG_I(TAG,"DHCP Leased Clients: %s",resp.c_str());

        accessories_conn_info["DHUB"] = {false,  {'\0'}};
        accessories_conn_info["VBUS"] = {false,  {'\0'}};

        istringstream ip_ne_out(resp);
        string line;
        count = 0;

        json_t *root = json_object();

        json_t *vbus_obj = json_object();
        json_object_set_new(vbus_obj, "conn_status", json_false());

        json_t *dhub_obj = json_object();
        json_object_set_new(dhub_obj, "conn_status", json_false());

        while (getline(ip_ne_out, line))
        {
            if(line.find("REACHABLE") != string::npos)
            {
                LOG_D(TAG,"Device Is Connected To Our Hotspot %s", line.c_str());
                count++;
                if (line.find("e0:e2") != string::npos)
                {
                    string vbus_ip = line.substr(0, line.find(' '));
                    LOG_I(TAG,"VBUS Is Connected With Ip: %s", vbus_ip.c_str());

                    conn_devices += "VBUS, ";
                    accessories_conn_info["VBUS"].isConnected = true;
                    strcpy(accessories_conn_info["VBUS"].ip, vbus_ip.c_str());

                    json_object_set_new(vbus_obj, "conn_status", json_true());
                    json_object_set_new(vbus_obj, "ip_addr", json_string(vbus_ip.c_str()));
                }
                else if(line.find("30:eb") != string::npos)
                {
                    string dhub_ip = line.substr(0, line.find(' '));
                    LOG_I(TAG,"DHUB Is Connected With Ip: %s", dhub_ip.c_str());

                    conn_devices += "DHUB, ";
                    accessories_conn_info["DHUB"].isConnected = true;
                    strcpy(accessories_conn_info["DHUB"].ip, dhub_ip.c_str());

                    json_object_set_new(dhub_obj, "conn_status", json_true());
                    json_object_set_new(dhub_obj, "ip_addr", json_string(dhub_ip.c_str()));

                    if(getCurrDHUBWifiModeFromDB() == eAPMode)
                    {
                       if(updateDHUBWifiModeInDB(eSTAMode))
                        {
                            setCurrDHUBWifiModeFromDB();
                            send_msg_get_dhub_wifi_mode_db();
                        }
                    }

                }
            }
        }
        if(conn_devices!="")
        {
            conn_devices = conn_devices.substr(0, conn_devices.size()-2);
        }
        json_object_set_new(root, "VBUS", vbus_obj);
        json_object_set_new(root, "DHUB", dhub_obj);
        char * conn_info;
        if(root!=NULL)
        {
            conn_info = json_dumps(root, 0);
        }
        else
        {
            LOG_E(TAG,"Failed To Update Connected Device Info");
            return false;
        }
        string conn_info_str = conn_info;
        ofstream file(wifi_conn_devices_file_path);
        file << conn_info_str;
        file.close();
        free(conn_info);
        json_decref(root);

        LOG_D(TAG,"Found %d Accessories Connected",count);

    }
    return true;
}

bool check_mdvr_connectivity(string wifi_iface)
{
#ifdef BAGHEERA2
    char buffer[256];

    //Get avg response time for ping
    string cmd = "ping -I " + wifi_iface +" -c 1 -w 1 10.10.10.254 | tail -1 | "
                "awk '{print $4}' | cut -d '/' -f 2";

    LOG_I(TAG,"check mdvr connectivity cmd: %s", cmd.c_str());

    FILE *fp = popen (cmd.c_str(), "r");
    string response_time;

    if (fp == NULL)
    {
        LOG_I (TAG,"executiong of %s failed",cmd.c_str());
        return false;
    }

    if (fgets (buffer, sizeof(buffer), fp))
    {
        pclose(fp);

        response_time = string (buffer);
#else
    //Get avg response time for ping
    string cmd = "busybox ping -I " + wifi_iface +" -c 1 -w 1 10.10.10.254 | tail -1 | grep \"round-trip\" | awk \'{print $4}\' | cut -d \'/\' -f 2";

    string response_time = "";

    LOG_I(TAG,"check mdvr connectivity cmd: %s", cmd.c_str());

    if (system_execute_with_resp("PING_INT_CHECK", cmd, response_time))
    {
#endif
        response_time.erase(remove_if(response_time.begin(), response_time.end(), ::isspace), response_time.end());
        LOG_I (TAG," resp for check mdvr connectivity cmd: %s",response_time.c_str());
        if (response_time == "")
        {
            LOG_E(TAG,"cmd %s returned empty",cmd.c_str());
            return false;
        }
        else
        {
            //Get integer part.
            size_t pos = response_time.find ('.');
            if (pos != string::npos)
            {
                response_time.assign (response_time, 0, pos);
            }

            long int res_time = strtol (response_time.c_str(), NULL, 10);
            LOG_I (TAG,"ping: response time: %ld",res_time);
            if (res_time != LONG_MAX && res_time != LONG_MIN && res_time != 0)
            {
                if(getCurrDHUBWifiModeFromDB() == eSTAMode)
                {
                    if(updateDHUBWifiModeInDB(eAPMode))
                    {
                        setCurrDHUBWifiModeFromDB();
                        send_msg_get_dhub_wifi_mode_db();
                    }
                }
                return true;
            }
            else
            {
                LOG_E (TAG,"strtol failed errno:%d",errno);
                return false;
            }
        }
    }
    else
    {
#ifdef BAGHEERA2
        LOG_E (TAG,"fgets returned null");
#else
        LOG_E (TAG,"failed to execute ping command");
#endif
        return false;
    }

    return true;
}

bool check_internet_connectivity(string wifi_iface)
{
    std::vector<std::string> dns_array = {"8.8.8.8","2001:4860:4860::8888", "1.1.1.1", "2606:4700:4700::1111"};

    if (dta_enabled) {
        string def_route = "";
        if(system_execute_with_resp("get_default_gateway", "ip route | grep default | grep wlan0 | awk \'{print $3}\'", def_route)) {
            LOG_I(TAG, "Default Route: %s", def_route.c_str());
            if (def_route != "")  {
                dns_array.push_back(def_route);
            }
        }
    }

    for (int i =0; i< dns_array.size(); i++)
    {
#ifdef BAGHEERA2
        //Get avg response time for ping
        string cmd = "ping -I " + wifi_iface +" -c 1 -w 1 " + dns_array[i] + " | tail -1 | "
                    "awk '{print $4}' | cut -d '/' -f 2";
#else
        //Get avg response time for ping
        string cmd = "busybox ping -I " + wifi_iface +" -c 2 " + dns_array[i] + " | tail -1 | grep \"round-trip\" | awk \'{print $4}\' | cut -d \'/\' -f 2 2>&1";
#endif
	    LOG_I(TAG, "ping cmd: %s", cmd.c_str());
	    string response_time = "";
	    if(system_execute_with_resp("PING_INT_CHECK", cmd, response_time))
	    {
		    LOG_I (TAG,"%s", response_time.c_str());
		    if (response_time == "")
		    {
			    LOG_E(TAG,"cmd %s returned empty",cmd.c_str());
                            continue;
		    }
		    else
		    {
			    //Get integer part.
			    size_t pos = response_time.find ('.');
			    if (pos != string::npos)
			    {
				    response_time.assign (response_time, 0, pos);
			    }

			    long int res_time = strtol (response_time.c_str(), NULL, 10);
			    LOG_I (TAG,"ping: response time: %ld",res_time);
			    if (res_time != LONG_MAX && res_time != LONG_MIN && res_time != 0)
			    {
				    //Check if response time is greater than threshold.
				    //if so, it is as good as no internet connectivitu
				    if (res_time < PING_RESPONSE_THRESHOLD)
				    {
					    return true;
				    }
				    else
				    {
					    LOG_E(TAG,"No internet connectivity");
					    continue;
				    }
			    }
			    else
			    {
				    LOG_E (TAG,"strtol failed errno:%d",errno);
				    continue;
			    }
		    }
	    }
	    else
	    {
		    LOG_E (TAG,"failed to execute ping command");
		    continue;
	    }
    }

   return false;
}

void check_and_sleep(int duration, volatile bool &manage) {
    int sleep_secs_count = 0;
    while(sleep_secs_count < duration) {
        if(!manage) {
            break;
        }

        sleep (SLEEP_BETWEEN_CHECKS);

        sleep_secs_count += 2;
    }
}

bool send_msg_toggle_wifi_mode(wifi_mode_t wifi_mode)
{
    toggle_wifi_mode_t req_wifi_toggle;
    req_wifi_toggle.mode = wifi_mode;

    if( false == send_msg( (generic_msg_t *)&req_wifi_toggle, REQ_TOGGLE_DRIVERI_WIFI_MODE, sizeof(req_wifi_toggle), get_msgq_name(), get_msgq_name(), 0) )
    {
        LOG_E(TAG, "Not Able To Send Toggle Wifi Message");
        return false;
    }
    else
    {
        LOG_I(TAG, "Successfully Sent Toggle Wifi Message");
    }
    return true;
}

void manage_wifi_connection(string& ssid, string& password, string wifi_iface, volatile bool &manage, bool is_mdvr) {
    bool connected = false, internet_connectivity = false, mdvr_connectivity = false, try_connection = true;
    int64_t cur_time = 0, conn_attempt_failed_time = 0, internet_conn_failed_time = 0, mdvr_conn_failed_time = 0;
    int fail_retry_cnt = 0, dhub_fail_retry_cnt = 0;
    if(is_mdvr == false)
    {
        is_dhcpcd_enabled = true;
        LOG_I(TAG, "Enabling DHCPCD By Default For NON D-HUB Devices");
    }
    while (manage) {
        connected = false;
        internet_connectivity = false;
        mdvr_connectivity = false;
        try_connection = true;
        fail_retry_cnt = 0;
#ifdef KRAIT
        if(is_mdvr) {
            delete_default_route(wifi_iface);
        }

#endif


        if(hotspot_created) {
            sleep(WIFI_SEARCH_SSID_INTERVAL_SECS);
            continue;
        }

        while (wifi_check_availability (ssid, wifi_iface, MAX_RETRY_COUNT) && manage &&  (!hotspot_created)) {
            //Try wifi connect only if wifi is not already connected
            if(is_mdvr) {
                delete_default_route(wifi_iface);
            }
            if (connected || (is_mdvr && isMDVRConnected())) {
                // Check if ip exist on wlan interface
/*                if(ip_exist_wlan(wifi_iface) == false) {
                    LOG_I(TAG,"ip became unavailable. Disconnecting");
                    wifi_disconnect(wifi_iface);
                    connected = false;
                }*/
            }
            else if(try_connection) {
                if (wifi_connect (ssid, password, wifi_iface, is_mdvr)) {
                    connected = true;
                    internet_connectivity = true;
                    mdvr_connectivity = true;
                    // reset fail retry count once connection is successful
                    fail_retry_cnt = 0;
                    //if connection is for mdvr delete default wlan0 route
                    if(is_mdvr) {
                        delete_default_route(wifi_iface);
                    } else { // if connection is for wifi fallback, reduce metric to avoid flow over eth
#ifdef KRAIT
                        // small delay needed before route can be found
                        sleep(3);
#endif
                        alter_route_metric(wifi_iface);
                    }
                }
                else {
                    LOG_E (TAG,"Attempt to connect to home ssid failed");
                    connected = false;
                    // for wifi fallback do not try back to back connection on failure
                    if(!is_mdvr) {
                        if(fail_retry_cnt > MAX_FAIL_RETRIES) {
                            try_connection = false;
                        }
                        fail_retry_cnt++;
                        conn_attempt_failed_time = get_system_time();
                    }
                    else if(is_mdvr && iosix_enabled)
                    {
                        if( dhub_fail_retry_cnt > MAX_RETRY_DHUB_CONN )
                        {
                            LOG_I(TAG,"Reached Max Retries To Connect To DHUB, Going To Switch To AP Mode");
                            try_connection = false;

                            if( false == send_msg_toggle_wifi_mode(eAPMode))
                            {
                                LOG_E(TAG, "Not Able To Send Toggle Wifi Message");
                            }
                            else
                            {
                                LOG_I(TAG, "Successfully Sent Toggle Wifi To AP Message");
                                dhub_fail_retry_cnt = 0;
                            }
                        }
                        else
                        {
                            dhub_fail_retry_cnt++;
                            LOG_E(TAG,"Failed To Connect To DHUB, Retry Count : %d", dhub_fail_retry_cnt);
                        }
                    }
                }
            }
            else if(!try_connection) {
                //A previous attempt to connect to home ssid was failed.
                //Check current time and decide if it is time to give another try
                cur_time = get_system_time();
                if ((cur_time - conn_attempt_failed_time) > INTERVAL_RETRY_CONNECT) {
                    LOG_I(TAG,"let's try connecting to home ssid once");
                    //clear this flag so that wifi connect will be tried in next iteration
                    try_connection = true;
                }
            }

            // check internet for wifi fallback
            if (!connected) {
                LOG_D(TAG, "check and sleep on wifi connection");
                check_and_sleep(WIFI_CHECK_CONN_INTERVAL_SECS, manage);
                continue;
            }

            // check mdvr connectivity
            if(is_mdvr) {
                if(check_mdvr_connectivity(wifi_iface)) {
                    mdvr_connectivity = true;
                    dhub_fail_retry_cnt = 0;
                    LOG_D(TAG, "check and sleep on mdvr connectivity success");
                    check_and_sleep(WIFI_CHECK_CONN_INTERVAL_SECS, manage);
                    continue;
                }

                if(mdvr_connectivity) {
                    mdvr_connectivity = false;
                    mdvr_conn_failed_time = get_system_time();
                    LOG_D(TAG, "check and sleep on mdvr connectivty lost");
                    check_and_sleep(WIFI_CHECK_CONN_INTERVAL_SECS, manage);
                    continue;
                }

                cur_time = get_system_time();
                if ((cur_time - mdvr_conn_failed_time) > WIFI_CHECK_CONN_INTERVAL_SECS) {
                    LOG_E (TAG,"No mdvr connectivity. disconnecting");
                    if( iosix_enabled)
                    {
                        if( dhub_fail_retry_cnt > MAX_RETRY_DHUB_CONN )
                        {
                            LOG_I(TAG,"Reached Max Retries To Connect To DHUB, Going To Switch To AP Mode");
                            try_connection = false;

                            if( false == send_msg_toggle_wifi_mode(eAPMode))
                            {
                                LOG_E(TAG, "Not Able To Send Toggle Wifi Message");
                            }
                            else
                            {
                                LOG_I(TAG, "Successfully Sent Toggle Wifi To AP Message");
                                dhub_fail_retry_cnt = 0;
                            }
                        }
                        else
                        {
                            dhub_fail_retry_cnt++;
                            LOG_E(TAG,"Failed To Connect To DHUB, Retry Count : %d", dhub_fail_retry_cnt);
                        }
                    }
                    break;
                }
            }

            //Already connected to home ssid. Check internet connectivity.
            //ping 8.8.8.8 and get avg response time
            //string cmd = "ping -c 1 -w 1 8.8.8.8 | tail -1 | awk '{print $4}' | cut -d '/' -f 2";
            if (check_internet_connectivity(wifi_iface)) {
                internet_connectivity = true;
                LOG_D(TAG, "check and sleep for internet connectivity success");
                check_and_sleep(WIFI_CHECK_CONN_INTERVAL_SECS, manage);
                continue;
            }

            //This if has to be true for the first time after internet connectivity
            //is lost because this is where we capture start time when connectivity was
            //lost. This is made sure by setting 'internet_connectivity' to true
            //if 'wifi_connect' was successfull.

            if (internet_connectivity) {
                //First time
                internet_connectivity = false;
                internet_conn_failed_time = get_system_time();
#ifdef KRAIT
                // if eth0 comes up after wlan0 connection, route may have eth0 default
                // so before declaring actual connectivity failure, try altering route once
                // connectivity check may succeed in next attempt if alter route sets wlan0 as default route
                alter_route_metric(wifi_iface);
#endif
            } else {
                cur_time = get_system_time();
                if ((cur_time - internet_conn_failed_time) > WIFI_CHECK_CONN_INTERVAL_SECS) {
                    //If no connectivity, consider it as a failed connection attempt.
                    LOG_E (TAG,"No internet connectivity. disconnecting");
                    if (wifi_disconnect(wifi_iface)) {
                        LOG_I(TAG,"Wifi disconnected");
                        connected = false;
                        try_connection = false;
                        conn_attempt_failed_time = get_system_time();
#ifdef BAGHEERA2
                        delete_default_route(wifi_iface);
#endif
                    }
                }
            }
            // smaller sleeps to break faster in case of external triggers like installer app
            LOG_D(TAG, "check and sleep before inner while exit");
            check_and_sleep(WIFI_CHECK_CONN_INTERVAL_SECS, manage);
        }

        bool installer_app_active = file_is_present(installerAppActive);

        if (connected && !installer_app_active && !hotspot_created) {
            LOG_I(TAG,"%s became unavailable. Disconnecting", ssid.c_str());
            if (wifi_disconnect(wifi_iface)) {
                connected = false;
            }
        }
        else {
            LOG_E (TAG, "wifi availability check for ssid %s failed or stop managing", ssid.c_str());
            if(!is_mdvr) {
                return;
            }
            else if(is_mdvr && iosix_enabled)
            {
                if( dhub_fail_retry_cnt > MAX_RETRY_DHUB_CONN )
                {
                    LOG_I(TAG,"Reached Max Retries To Connect To DHUB, Going To Switch To AP Mode");
                    try_connection = false;

                    if( false == send_msg_toggle_wifi_mode(eAPMode))
                    {
                        LOG_E(TAG, "Not Able To Send Toggle Wifi Message");
                    }
                    else
                    {
                        LOG_I(TAG, "Successfully Sent Toggle Wifi To AP Message");
                        dhub_fail_retry_cnt = 0;
                    }
                }
                else
                {
                    dhub_fail_retry_cnt++;
                    LOG_E(TAG,"Failed To Connect To DHUB, Retry Count : %d", dhub_fail_retry_cnt);
                }
            }

        }

        if(!manage) {
            break;
        }

        check_and_sleep(WIFI_SEARCH_SSID_INTERVAL_SECS, manage);
    }
}

#ifdef BAGHEERA2
bool set_bit_to_enable_hotspot(){

    int ret =0;

    static const string enable_hotspot_call = "echo 2 > /sys/module/bcmdhd/parameters/op_mode";
    LOG_I(TAG, "going to enable hostspot mode\n");
    LOG_I(TAG, "setting AP mode in TX1\n");
    ret = system_execute("enable hotspot", enable_hotspot_call);
    sleep (UNIT_TIME);
    if (ret !=0){
       LOG_E(TAG, "setting up AP mode failed\n");
       return false;
    }
    return true;

}
#endif

bool reset_wifi_interface(string iface)
{
    int retval;
    string iface_down_cmd = "ifconfig " + iface + " down";
    string iface_up_cmd = "ifconfig " + iface + " up";

    retval = system_execute("IFACE_DOWN", iface_down_cmd);
    if(retval < 0)
    {
        LOG_E(TAG, "Failed To Run %s Down Cmd", iface.c_str());
        return false;
    }

    sleep(1);

    retval = system_execute("IFACE_UP", iface_up_cmd);
    if(retval < 0)
    {
        LOG_E(TAG, "Failed To Run %s Up Cmd", iface.c_str());
        return false;
    }
    return true;
}

bool remove_dnsmasq_leases()
{
    string cmd;
    cmd = "rm -rf /data/dnsmasq_d.leases";
    LOG_I(TAG, "Hotspot Step 4 Cmd: %s", cmd.c_str());
    int retval = system_execute("REMVOING CACHE", cmd);
    if(retval != 0) {
        LOG_E(TAG, "Failed To Run Remove Cache Cmd");
        return false;
    }
    return true;
}

bool enable_hotspot(void *args){

    int ret2=0;
    bool def_iface = *((bool *)args);

    string iface_name;
    if(def_iface) {
        iface_name = default_iface;
    } else {
        iface_name = secondary_iface;
    }

#ifdef BAGHEERA2
    // Reference: https://developer-old.gnome.org/NetworkManager/stable/settings-802-11-wireless.html#
    // https://www.programmersought.com/article/18587356843/

    std::string wireless_band = "bg";

    switch(gInstallerBandType) {
        case 'a': {
            wireless_band = "a";
            LOG_I(TAG, "Enabling 5Ghz");
            break;
        }

        case 'b': {
            wireless_band = "bg";
            LOG_I(TAG, "Enabling 2.4Ghz");
            break;
        }

        default: {
            wireless_band = "bg";
            LOG_I(TAG, "Default, Enabling 2.4Ghz");
            break;
        }
    }

    const std::vector<std::string> commands = {
                                                "nmcli con add type wifi ifname " + iface_name + " con-name DriveriHostspot autoconnect yes ssid \"" + hotspot_ssid + "\"",
                                                "nmcli con modify DriveriHostspot 802-11-wireless.mode ap 802-11-wireless.band " + wireless_band + " 802-11-wireless.channel " + to_string(hotspot_channel),
                                                "nmcli con modify DriveriHostspot wifi-sec.key-mgmt wpa-psk",
                                                "nmcli con modify DriveriHostspot 802-11-wireless-security.proto rsn",
                                                "nmcli con modify DriveriHostspot 802-11-wireless-security.group ccmp",
                                                "nmcli con modify DriveriHostspot 802-11-wireless-security.pairwise ccmp",
                                                "nmcli con modify DriveriHostspot 802-11-wireless-security.psk \"" + hotspot_password + "\"",
                                                "nmcli con modify DriveriHostspot ipv4.method shared",
                                                "nmcli con modify DriveriHostspot ipv6.method ignore",
                                                "nmcli con up DriveriHostspot"
                                              };

    for(const auto &cmd : commands) {
        LOG_I(TAG, "Hotspot commands: %s", cmd.c_str());
        const auto status = system(cmd.c_str());

        if (0 != status) {
            LOG_E(TAG, "command failed for AP mode: %d", status);
            return false;
        }
    }
#else
    string cmd = "interface=softap0";
    int retval = system_execute("SOFTAP_INTF", cmd);
    if(retval != 0) {
        LOG_E(TAG, "failed to run interface cmd");
    }

    //kill any existing hostapd
    kill_process("hostapd");

    // kill any wpa_supplicant
    kill_process("wpa_supplicant");

    // kill any dnsmasq
    kill_process("dnsmasq");

    // wpa_group_rekey : Change the broadcasted/multicasted keys after this many seconds. JiraID: KRT2-61

    cmd = "sync;sleep 1";
    LOG_I(TAG, "Hotspot Step 0 Cmd: %s", cmd.c_str());
    retval = system_execute("SYNC", cmd);
    if(retval != 0) {
        LOG_E(TAG, "failed to run sync cmd");
    }

    stringstream ss;
    cmd = "hostapd -B " + HOSTAPD_CONF_FILE;

    LOG_I(TAG, "Hotspot Step 2 Cmd: %s", cmd.c_str());
    string resp = "";

    //kill any existing hostapd
    kill_process("hostapd");

    bool ret = system_execute_with_resp("WIFI_HOTSPOT", cmd, resp);
    if(ret == false) {
        LOG_E(TAG, "failed to run cmd for wifi hotspot creation, Retrying ...");

        reset_wifi_interface(iface_name);
        //kill any existing hostapd
        kill_process("hostapd");

        bool ret = system_execute_with_resp("WIFI_HOTSPOT", cmd, resp);
        if(ret == false) {
            LOG_E(TAG, "failed to run cmd for wifi hotspot creation");
            return false;
        }
    }

    if(resp == "") {
        LOG_E(TAG, "wifi hotspot hostapd resp empty");
        return false;
    }
    printf("cmd resp: %s\n", resp.c_str());
    LOG_I(TAG, "cmd resp: %s", resp.c_str());

    string resp_check = iface_name + ": AP-ENABLED";
    if(resp.find(resp_check) == string::npos) {
        LOG_E(TAG, "failed to create hotspot");
           return false;
    }
    cmd = "ifconfig " + iface_name + " 10.42.0.1 netmask 255.255.255.0 up";
    LOG_I(TAG, "Hotspot Step 3 Cmd: %s", cmd.c_str());
    retval = system_execute("WIFI_HOTSPOT", cmd);
    if(retval != 0) {
        LOG_I(TAG, "failed to set IP to wlan0 interface");
	return false;
    }

    if(!remove_dnsmasq_leases())
    {
        return false;
    }

#ifdef KRAIT
    cmd = "pkill dnsmasq;dnsmasq -i " + iface_name + " --conf-file=/etc/dnsmasq.conf --except-interface=lo --dhcp-range=10.42.0.10,10.42.0.100,5m --dhcp-leasefile=/data/dnsmasq_d.leases";
#else
    cmd = "dnsmasq -i " + iface_name + " --dhcp-range=10.42.0.10,10.42.0.100,100h --dhcp-leasefile=/data/dnsmasq_d.leases";
#endif
     LOG_I(TAG, "Hotspot Step 5 Cmd: %s", cmd.c_str());
    retval = system_execute("START_DNSMASQ", cmd);
    if(retval != 0) {
        LOG_I(TAG, "failed to start dnsmasq");
	return false;
    }

#endif
    return true;
}

void delete_secondary_iface(string wifi_iface) {
#ifdef BAGHEERA2
    string cmd = "iw dev " + wifi_iface + " del";

#else
    string cmd = "iw dev " + wifi_iface + " del";
    LOG_I(TAG, "del cmd: %s", cmd.c_str());
#endif
    pthread_mutex_lock(&scan_connect_mutex);
    int ret = system_execute("DEL_SEC_IFACE", cmd);
    pthread_mutex_unlock(&scan_connect_mutex);

    if(ret != 0) {
        LOG_E(TAG, "failed to delete secondary interface");
        return;
    }

    LOG_I(TAG, "secondary interface deleted successfully");
}

bool prepare_hotspot(string wifi_iface) {

    task_result_t task_result;
    int ret = 0;
    bool res = false, def_iface = false;

    if(wifi_iface == default_iface) {
        wifi_disconnect(wifi_iface);
        sleep(UNIT_TIME*2);
/*#ifdef BAGHEERA2
        res = set_bit_to_enable_hotspot();
        if (!res){
            return false;
        }
#endif*/
        def_iface = true;
    } else {
        //check if secondary iface is already available
        if(check_for_wlan_iface(wifi_iface) == true) {
            wifi_disconnect(wifi_iface);
            delete_secondary_iface(wifi_iface);
        }

        //add secondary interface in ap mode
        if(add_wifi_iface(wifi_iface, "ap") == false) {
            LOG_E(TAG, "Not able to add %s wifi interface", wifi_iface.c_str());
            return false;
       }
    }

    pthread_mutex_lock(&scan_connect_mutex);
    task_result = nd_timed_task (enable_hotspot, WAIT_TIME_HOTSPOT, (void *)&def_iface, "enable hotspot task");
    pthread_mutex_unlock(&scan_connect_mutex);

    if (task_result == TASK_SUCCESS) {
        LOG_I (TAG, "hotspot enabled within time limit");
    } else {
#ifdef BAGHEERA2
        LOG_E(TAG, "hotspot creation failed  - retrying");
        sleep (UNIT_TIME);
        // need to try one more time for wlan0 iface
        if(wifi_iface == default_iface) {
            pthread_mutex_lock(&scan_connect_mutex);
            task_result = nd_timed_task (enable_hotspot, WAIT_TIME_HOTSPOT_2, (void *)&def_iface, "enable hotspot task");
            pthread_mutex_unlock(&scan_connect_mutex);
            LOG_I(TAG, "came out of second enable hotspot");
            if (task_result == TASK_SUCCESS) {
                LOG_I (TAG, "hotspot enabled in second retry");
            }
            else
            {
                LOG_E(TAG, "hotspot creation failed  - in retry");
                return false;
            }
        }
#else
        LOG_E(TAG, "Hotspot Creation Failed, Exiting ...");
        nd_service_obj->send_err_msg(SM_E_WMGR_FAILED_HOTSPOT_ENABLED, NDService::UNUSED_ERR_AUX_CODE, "Hotspot Creation Failed");
        exit(0);
        return false;
#endif
    }

    LOG_I(TAG, "exiting prepare hotspot function");
    return true;
}

void send_msg_to_installer_app() {
    // Send message to installer app service
    generic_msg_t inst_msg;

    if( false == send_msg(&inst_msg, (msg_type_t)CREATE_INSTALLER_SOCKET,
                           sizeof(inst_msg), get_msgq_name(), "installer_queue", 0 ) ) {
        LOG_E(TAG, "Sending msg to instalelr app failed");
        return;
    }

    LOG_I(TAG, "Sent message to installer app service");
}

void send_msg_for_installer_scan() {
    // Send message to btfv for installer app scan
    generic_msg_t scan_msg;

    if( false == send_msg(&scan_msg, (msg_type_t)START_INSTALLER_SCAN,
                           sizeof(scan_msg), get_msgq_name(), "BTFV", 0 ) ) {
        LOG_E(TAG, "Sending msg to btfv failed");
        return;
    }

    LOG_I(TAG, "Sent message to btfv service to start installer scan");
}

void send_bth_restart_msg() {
    // Send message to btfv service
    generic_msg_t inst_msg;

    if( false == send_msg(&inst_msg, (msg_type_t)RESTART_BLUETOOTH,
                           sizeof(inst_msg), get_msgq_name(), "BTFV", 0 ) ) {
        LOG_E(TAG, "Sending msg to btfv to restart bluetooth activities failed");
        return;
    }

    LOG_I(TAG, "Sent message to btfv service to restart bluetooth activities");
}

void send_msg_to_btfv() {
    // Send message to installer app service
    generic_msg_t btfv_msg;

    if( false == send_msg(&btfv_msg, (msg_type_t)INSTALLER_WRITE_CHAR,
                           sizeof(btfv_msg), get_msgq_name(), "BTFV", 0 ) ) {
        LOG_E(TAG, "Sending msg to BTFV failed");
        return;
    }

    LOG_I(TAG, "Sent message to btfv service");
}

bool trigger_bluetooth_connect(void *args) {
    string cmd = "gatttool -i hci0 -t random -b " + installer_app_mac_addr + " --char-read --uuid=" + BT_CHAR_READ_UUID;
    string resp = "";
    LOG_I(TAG, "bth connect cmd: %s", cmd.c_str());
    bool ret = system_execute_with_resp("BTH_CONN", cmd, resp);
    if(ret == false || resp == "") {
        LOG_E(TAG, "failed to execute bluetooth connect command");
        return false;
    }

    LOG_I(TAG, "%s", resp.c_str());
    LOG_I(TAG, "bluetooth connect command triggered successfully");
    return true;
}

void send_msg_to_btfv_for_antenna_time() {
    // Send message to btfv for antenna time
    generic_msg_t req_antenna_msg;

    if( false == send_msg(&req_antenna_msg, (msg_type_t)REQUEST_ANTENNA_TIME,
                           sizeof(req_antenna_msg), get_msgq_name(), "BTFV", 0 ) ) {
        LOG_E(TAG, "Sending msg to btfv failed");
        return;
    }

    LOG_I(TAG, "Sent message to btfv service to req antenna time");
}

void send_msg_to_btfv_to_reset_antenna_time() {
    // Send message to btfv for antenna time
    generic_msg_t req_antenna_msg;

    if( false == send_msg(&req_antenna_msg, (msg_type_t)RESET_ANTENNA_TIME,
                           sizeof(req_antenna_msg), get_msgq_name(), "BTFV", 0 ) ) {
        LOG_E(TAG, "Sending msg to btfv failed");
        return;
    }

    LOG_I(TAG, "Sent message to btfv service to reset antenna time");
}

void manage_hotspot_connection(string wifi_iface)
{
    // return if hotspot already created
    if(!hotspot_created) {
        LOG_I(TAG, "going to enable hotspot");
        bool result = prepare_hotspot(default_iface);
        if (!result) {
            LOG_E(TAG, "failed to create hotpsot. sending msg to restart scan");
            hotspot_created = false;
            wifi_fallback_manage = true;
            installer_app_manage = false;
            send_msg_for_installer_scan();
            return;
        }
        hotspot_created = true;
    }
    else
    {
        LOG_I(TAG,"Hotspot Is Already Created");
#ifdef KRAIT
        restart_dnsmasq();
#endif
    }

    LOG_I(TAG, "hotspot enabled successfully");
    string msg = "AP Mode - " + hotspot_ssid + " Hotspot Enabled";
    nd_service_obj->send_err_msg(SM_E_WMGR_HOTSPOT_ENABLED, NDService::UNUSED_ERR_AUX_CODE, msg);

    if(installer_app_manage)
    {
        send_msg_to_installer_app();

#ifdef BAGHEERA2
        LOG_I(TAG, "Bagheera2 flag enabled, sent msg to btfv for write char");
        send_msg_to_btfv();
#else
        /* Previously socket timeout was 300 sec because of this delay
         * it was approx 290 sec. But now socket timeout changed to 90 sec
         * this delay is significant as compare to 90 sec so changed it to
         * 2 sec. In Krait no such delay */

        // sleep(wifi_read_char_delay);
#endif
#ifdef KRAIT
        send_msg_to_btfv();
#else

        sleep(WAIT_TIME_BEFORE_BT_CONN);
#endif
       installer_app_manage = false;
    }
}

bool is_dnsmasq_running_on_wlan0()
{
    string cmd, response;
    cmd = "ps -eLf | grep dnsmasq";
    bool ret = system_execute_with_resp("check_dnsmasq", cmd, response);
    if(!ret)
    {
        LOG_E(TAG,"Failed To Execute DNS Check Command");
        return false;
    }
    else
    {
        LOG_I(TAG,"The Dnsmasq Processes are : %s",response.c_str());
#ifdef KRAIT
        return response.find("dnsmasq -i wlan0") != std::string::npos;
#else
        return response.find("/usr/sbin/dnsmasq") != std::string::npos;
#endif
    }
}

bool publish_wifi_updates(const int &connected_device_count, map<string,AccessoryConnInfo> accessories_conn_info)
{
    bool ret = true;
    for( auto iter= wifi_conn_info_clients.begin(), end = wifi_conn_info_clients.end(); iter != end; iter++ )
    {
        wifi_updates_t wifi_update_msg;
        if(*iter == Q_EXT_CAM_NAME)
        {
            wifi_update_msg.count = connected_device_count;
            wifi_update_msg.acc_conn_info = accessories_conn_info["DHUB"];
            LOG_D(TAG,"DHUB Connection status : %d",wifi_update_msg.acc_conn_info.isConnected);
        }
       ret &= send_msg( (generic_msg_t *)&wifi_update_msg, RES_WIFI_UPDATE, sizeof(wifi_update_msg), get_msgq_name(), iter->c_str(), 0);
    }
  /*  wifi_update->count = connected_device_count;
    wifi_update->isConnected = accessories_conn_info["VBUS"].isConnected;
    strcpy(wifi_update->ip, accessories_conn_info["VBUS"].ip);

    ret &= server.publish(TOPIC_VBUS_WIFI_UPDATES, wifi_update);
    if (!ret){
        LOG_E(TAG, "publish error");
    }*/

    return ret;
}

void* monitor_hotspot_thread(void* args)
{
    int no_of_disconnections = 0;
    int i = 0;
    while(1)
    {
        sleep (15);
        i++;
        string cmd = "";
        string resp ="";
        string response ="";
        int retval = -1;

        if(hotspot_created)
        {
            if(!is_dnsmasq_running_on_wlan0())
            {
                LOG_I(TAG,"Dnsmasq Is Not Up On Wlan0");
                if( restart_dnsmasq() )
                {
                    LOG_I(TAG,"Successufully Restarted Dnsmasq");
                }
                else
                {
                    LOG_E(TAG,"Failed To Restart Dnsmasq");
                }
            }

            if( i%2 == 0 )
            {
                map<string,AccessoryConnInfo > accessories_conn_info;
                if (getWifiConnectedDevices(connected_device_count, accessories_conn_info, connected_devices))
                {
                    if(!publish_wifi_updates(connected_device_count, accessories_conn_info))
                    {
                        LOG_E(TAG,"Failed To Publish Wifi Updates");
                    }
                    if(ext_cam_enabled && !isMDVRConnected())
                    {
                        dhub_disconnections_mins++;
                        LOG_I(TAG,"DHUB Has Been Disconnected For %d Mins", dhub_disconnections_mins);
                        if(wifi_check_availability (mdvr_ssid, default_iface, MAX_RETRY_COUNT))
                        {
                            LOG_I(TAG,"Found DHUB Hotspot Ssid In Scan ,DHUB Has Reset To AP Mode");
                            updateDHUBWifiModeInDB(eAPMode);
                            setCurrDHUBWifiModeFromDB();
                            send_msg_get_dhub_wifi_mode_db();
                            int64_t pair_time;
                            if(installer_app_active || get_installer_pair_time(pair_time))
                            {
                                LOG_I(TAG,"Not Going To Recover DHUB Currently As Installer App Is Active");
                            }
                            else
                            {
                                if(!persist_ap_mode)
                                {
                                    LOG_I(TAG,"Going To Recover DHUB");
                                    if( false == send_msg_toggle_wifi_mode(eSTAMode) )
                                    {
                                        LOG_E(TAG, "Not Able To Send Toggle Wifi Message");
                                    }
                                    else
                                    {
                                        LOG_I(TAG, "Successfully Sent Toggle Wifi Message");

                                        toggle_wifi_mode_t req_dhub_wifi_toggle;
                                        req_dhub_wifi_toggle.mode = eSTAMode;

                                        if( false == send_msg( (generic_msg_t *)&req_dhub_wifi_toggle, REQ_TOGGLE_DHUB_WIFI_MODE, sizeof(req_dhub_wifi_toggle), get_msgq_name(),  Q_EXT_CAM_NAME, 0 ) )
                                        {
                                            LOG_E(TAG, "Not Able To Send Toggle DHUB Wifi Message To EXT_CAM");
                                        }
                                        else
                                        {
                                            LOG_I(TAG,"Successfully Sent Toggle DHUB Wifi Message");
                                            string str_msg = "DHUB Disconnected, Enabling STA Mode";
                                            nd_service_obj->send_err_msg(SM_E_WMGR_STA_MODE_ENABLED, NDService::UNUSED_ERR_AUX_CODE, str_msg );
                                            dhub_disconnections_mins = 0;

                                        }
                                    }
                                }
                            }
                        }
                    }
                    else
                    {
                        dhub_disconnections_mins = 0;
                    }
                }

                LOG_I(TAG, "No Of Devices Connected To Hotspot : %d",connected_device_count);

                if(connected_device_count > 0 )
                {
                    LOG_D(TAG, "Atleast One Device Is Connected");
                    no_of_disconnections=0;
                }
                else
                {
                    no_of_disconnections++;
                    LOG_I(TAG,"Disconnections Count = %d minutes", no_of_disconnections);
                    if(no_of_disconnections >= max_disconnections)
                    {
                        LOG_I(TAG,"Max Disconnections(%d mins) Reached ", max_disconnections);

                        if(installer_app_active)
                        {
                            LOG_I(TAG,"Not Restarting Dnsmasq Even Though Max Disconnections are Reached As Installer App Is Active");
                        }
                        else
                        {
                            if(!is_dnsmasq_running_on_wlan0())
                            {
                                LOG_I(TAG,"Dnsmasq Is Not Up On Wlan0");

                                if( restart_dnsmasq() )
                                {
                                    LOG_I(TAG,"Successufully Restarted Dnsmasq");
                                }
                                else
                                {
                                    LOG_E(TAG,"Failed To Restart Dnsmasq");
                                }
                            }
                        }
                    }
                }
            }
        }
        else
        {
            LOG_I(TAG,"Not Monitoring As Hotspot Is Not Created");
        }
    }
}

void manage_wifi_fallback_connection(vector<pair<string, string>> ssids, const string&  wifi_iface) {

    LOG_I(TAG, "manage wifi fallback connection");
    while (1) {
        if(!wifi_fallback_manage) {
            // if installer app enabled, call manage hotpsot connection
            if(installer_app_manage) {
                sleep(WIFI_SEARCH_SSID_INTERVAL_SECS);
                continue;
            }

            // if hotspot created, no need to proceed to fallback checks
            if(hotspot_created) {
                sleep(WIFI_SEARCH_SSID_INTERVAL_SECS);
                continue;
            }
        }

        sleep(2);

        vector <pair<string,string> >::iterator iter = ssids.begin();
        while (iter != ssids.end()  && wifi_fallback_manage) {
           if (wifi_check_availability (iter->first, wifi_iface, 1)) {
                LOG_I (TAG,"ssid %s is available", (iter->first).c_str());
                break;
            }
            iter++;
        }
        if(iter != ssids.end() && wifi_fallback_manage) {
            LOG_I(TAG, "initiating wifi connection for ssid %s", (iter->first).c_str());
            manage_wifi_connection(iter->first, iter->second, wifi_iface, wifi_fallback_manage, false);
        }
        sleep(30);
    }
}

bool check_if_scan_required()
{
    bool scan_required = false;


    // Check if both ext_cam and iosix are disabled
    if((ext_cam_enabled == false) && (iosix_enabled == false) && (current_speed > drv_login_speed))
    {
        LOG_I(TAG, "Not Scanning as both ext_cam_enabled and iosix_enabled are disabled");
        return scan_required;
    }

    scan_required = (!ip_exist_wlan(default_iface) || (ext_cam_enabled && !isMDVRConnected())) ;

    if (scan_required)
    {
        LOG_I(TAG, "Scanning as IP does not exist");
    }
    else
    {
        LOG_I(TAG, "Not Scanning as IP Exists");
    }

    return scan_required;
}

void generate_and_execute_scan_command(ScanType scanType)
{
    string mode = (scanType == ACTIVE_SCAN) ? "" : " passive ";
    string cmd = "iw dev " + default_iface + " scan" + mode + "| grep -w \"SSID:\" | cut -d ' ' -f 2- > " + wifi_scan_result_file;
    string log_mode = (scanType == ACTIVE_SCAN) ? "WIFI_SCAN_ACTIVE" : "WIFI_SCAN_PASSIVE";

    // Only perform BT antenna time coordination for Azurewave WiFi modules
    WifiModuleVendorType wifi_module = getWifiModuleVendorType();
    bool is_azurewave = (wifi_module == WIFI_MODULE_AZUREWAVE);

    if (is_azurewave) {
        send_msg_to_btfv_for_antenna_time();

        unique_lock<std::mutex> lk(start_wifi_scan_mutex);
        int start_time_wait_for_bt = get_system_time();

        LOG_I(TAG,"%s Started Waiting For BT To Relax Scan", log_mode.c_str());
        start_wifi_scan_cv.wait(lk, []{return start_wifi_scan;});
        LOG_I(TAG,"%s Waiting For BT Relax Scan Done, It took %lld", log_mode.c_str(), (get_system_time() - start_time_wait_for_bt));
    } else {
        LOG_I(TAG, "%s BT antenna time coordination not required for this WiFi module", log_mode.c_str());
    }

    pthread_mutex_lock(&scan_file_mutex);
    pthread_mutex_lock(&scan_connect_mutex);
    LOG_I(TAG,"%s Scan cmd = %s", log_mode.c_str(), cmd.c_str());
    system_execute(log_mode, cmd);
    pthread_mutex_unlock(&scan_connect_mutex);
    pthread_mutex_unlock(&scan_file_mutex);

    if (is_azurewave) {
        start_wifi_scan = false;
        send_msg_to_btfv_to_reset_antenna_time();
    } else {
        LOG_I(TAG, "%s No BT antenna time reset required for this WiFi module", log_mode.c_str());
    }
}

void *wifi_scan_thread(void *args)
{
    LOG_I(TAG, "Starting wifi_scan_thread");

    sleep (DEFAULT_SLEEP_DURATION); // waiting 5 seconds after service start to scan
    
    int scan_iteration_count = 0;
    bool wlan0_check_done = false;
    const int WLAN0_CHECK_SCAN_THRESHOLD = 2;
#ifdef AUTOMATION
    Config_parser automation_conf(AUTOMATION_CONFIG);
    bool automation_is_val_overridden = false;
    int wifi_reinit_counter = 0;
    if (automation_conf.getParseStatus() == true) {
        if(string_to_integer(automation_conf.getConfig("test_automation", "wifi_reinit_counter", "", false, automation_is_val_overridden), wifi_reinit_counter) == false ) {
            LOG_E(TAG, "wifi_reinit_counter is not set in %s or not a valid integer", AUTOMATION_CONFIG);
            wifi_reinit_counter = 0;
        }
    }else {
        LOG_E(TAG, "Error parsing %s", AUTOMATION_CONFIG);
    }
    string touch_file_to_wifi_reinit = "/dev/shm/wifi_reinit";
    string msg_for_critical_event = "Wifi scan result file is empty for " + to_string(wifi_reinit_counter) + " times, reinitializing wifi";
    int empty_wifi_scan_result_file_counter = 0;
#endif
    string cmd = "iw dev " + default_iface + " scan | grep -w \"SSID:\" | cut -d ' ' -f 2- > " + wifi_scan_result_file;
    pthread_mutex_lock(&scan_file_mutex);
    pthread_mutex_lock(&scan_connect_mutex);
    LOG_I(TAG,"Active Scan cmd = %s", cmd.c_str());
    system_execute("WIFI_SCAN_ACTIVE", cmd);
    pthread_mutex_unlock(&scan_connect_mutex);
    pthread_mutex_unlock(&scan_file_mutex);

    generate_and_execute_scan_command(ACTIVE_SCAN);

    while(1)
    {
        sleep (60);
        scan_iteration_count++;
        LOG_D(TAG, "Scan iteration count: %d", scan_iteration_count);
        
        // Check for wlan0 interface after required scan iterations
        if(scan_iteration_count >= WLAN0_CHECK_SCAN_THRESHOLD && !wlan0_check_done)
        {
            check_and_report_wlan0_interface_availability();
            wlan0_check_done = true;
        }
        
        if(check_if_wifi_iface_exists(default_iface) && check_if_scan_required())
        {
            generate_and_execute_scan_command(PASSIVE_SCAN);
        }
#ifdef AUTOMATION
        if(dta_enabled == true && wifi_reinit_counter > 0)
        {
            if (file_size(wifi_scan_result_file) <= 0) {
                empty_wifi_scan_result_file_counter++;
                if (empty_wifi_scan_result_file_counter == wifi_reinit_counter) {
                        LOG_I(TAG, "Wifi scan result file is empty for %d times, going to touch a file for reinit wifi", wifi_reinit_counter);
                        if( file_touch(touch_file_to_wifi_reinit) == false ) {
                            LOG_E(TAG, "Cannot touch file %s", touch_file_to_wifi_reinit.c_str());
                        }
                        else{
                            nd_service_obj->send_err_msg(SM_E_WMGR_SCAN_RESULT_EMPTY_AUTOMATION, NDService::UNUSED_ERR_AUX_CODE, msg_for_critical_event);
                        }
                        empty_wifi_scan_result_file_counter = 0;
                }
            } else {
                empty_wifi_scan_result_file_counter = 0;
            }
        }
#endif
    }
}

void *ext_cam_wifi_thread(void *args) {
    LOG_I(TAG, "manage external camera wifi connection thread");
    LOG_I(TAG, "STA Mode Enabled Successfully");
    string msg = "STA Mode - Trying To Connect To " + mdvr_ssid;
    nd_service_obj->send_err_msg(SM_E_WMGR_STA_MODE_ENABLED, NDService::UNUSED_ERR_AUX_CODE, msg);
    manage_wifi_connection(mdvr_ssid, mdvr_password, default_iface, ext_cam_manage, true);
    LOG_E(TAG, "returned from manage wifi connection. not expected to be here");
    pthread_exit(NULL);
}

void *wifi_fallback_thread(void *args) {
    LOG_I(TAG, "manage wifi fallback thread");
    manage_wifi_fallback_connection(ssids_from_ini, wifi_fallback_iface);
    LOG_E(TAG, "returned from manage wifi fallback. not expected to be here");
    pthread_exit(NULL);
}

void fill_default_wifi_info()
{
    wifi_msg.time = get_system_time();
    wifi_msg.status = 0;
    nd_strncpy(wifi_msg.ipv4_address, "NA", IPV4_ADDRESS_LEN);
    nd_strncpy(wifi_msg.ipv6_address, "NA", IPV6_ADDRESS_LEN);
    nd_strncpy(wifi_msg.routes, "NA", ROUTES_LEN);
    nd_strncpy(wifi_msg.mac_addr, "NA", MAC_ADDR_LEN);
    nd_strncpy(wifi_msg.ssid, "NA", MAX_SSID_LENGTH);
    wifi_msg.signal_strength = 0;
    connected_devices = "NA";

    sec_wifi_msg.time = get_system_time();
    nd_strncpy(sec_wifi_msg.ipv4_address, "NA", IPV4_ADDRESS_LEN);
    nd_strncpy(sec_wifi_msg.ipv6_address, "NA", IPV6_ADDRESS_LEN);
    nd_strncpy(sec_wifi_msg.routes, "NA", ROUTES_LEN);
    nd_strncpy(sec_wifi_msg.mac_addr, "NA", MAC_ADDR_LEN);
    nd_strncpy(sec_wifi_msg.ssid, "NA", MAX_SSID_LENGTH);
    sec_wifi_msg.signal_strength = 0;

}

void *update_wifi_info_thread (void *arg)
{
    string resp;
    int pos;
    int wifi_info_cmds_len = wifi_info_cmds.size();
    int sec_wifi_info_cmds_len = sec_wifi_info_cmds.size();
    do
    {
        sleep(UPDATE_WIFI_INFO_INTERVAL);
        resp = "";
        wifi_msg.time = get_system_time();
        pthread_mutex_lock(&scan_connect_mutex);
        if(!ip_exist_wlan(default_iface)) {
            wifi_msg.status = 0;
        }
        else
        {
                wifi_msg.status = 1;
                for (int i = 1; i < wifi_info_cmds_len; i++)
                {
                    resp = "";
                    if(system_execute_with_resp("WIFI", wifi_info_cmds[i], resp) == false)
                    {
                        LOG_E (TAG, "CMD: %s exec failed", wifi_info_cmds[i].c_str());
                    }
                    else
                    {
                        switch (i)
                        {
                            case 1:
                                pos = resp.find("/");
                                strncpy (wifi_msg.ipv4_address ,resp.substr(0,pos).c_str(), IPV4_ADDRESS_LEN);
                                break;
                            case 2:
                                pos = resp.find("/");
                                strncpy (wifi_msg.ipv6_address ,resp.substr(0,pos).c_str(), IPV6_ADDRESS_LEN);
                                break;
                            case 3:
                                strncpy (wifi_msg.routes, resp.c_str(), ROUTES_LEN);
                                break;
                            case 4:
                                strncpy (wifi_msg.mac_addr, resp.c_str(), MAC_ADDR_LEN);
                                break;
                            case 5:
                                if(device_wifi_mode == eAPMode)
                                {
                                    strncpy (wifi_msg.ssid, hotspot_ssid.c_str(), MAX_SSID_LENGTH);
                                }
                                else
                                {
                                    strncpy (wifi_msg.ssid, resp.c_str(), MAX_SSID_LENGTH);
                                }
                                break;
                            case 6:
                                if(resp != "")
                                {
                                    string_to_integer(resp,wifi_msg.signal_strength);
                                }
                                else
                                {
                                    wifi_msg.signal_strength = 0;
                                }
                                break;
                            default:
                                break;
                        }
                    }
                }
        }
#ifndef BAGHEERA2
        resp = "";
        sec_wifi_msg.time = get_system_time();
        if(!ip_exist_wlan(secondary_iface))
        {
            LOG_E (TAG, "CMD: %s exec failed", sec_wifi_info_cmds[0].c_str());
        }
        else
        {
                for (int i = 1; i < sec_wifi_info_cmds_len; i++)
                {
                    resp = "";
                    if(system_execute_with_resp("WIFI", sec_wifi_info_cmds[i], resp) == false)
                    {
                        LOG_E (TAG, "CMD: %s exec failed", sec_wifi_info_cmds[i].c_str());
                    }
                    else
                    {
                        switch (i)
                        {
                            case 1:
                                pos = resp.find("/");
                                strncpy (sec_wifi_msg.ipv4_address ,resp.substr(0,pos).c_str(), IPV4_ADDRESS_LEN);
                                break;
                            case 2:
                                pos = resp.find("/");
                                strncpy (sec_wifi_msg.ipv6_address ,resp.substr(0,pos).c_str(), IPV6_ADDRESS_LEN);
                                break;
                            case 3:
                                strncpy (sec_wifi_msg.routes, resp.c_str(), ROUTES_LEN);
                                break;
                            case 4:
                                strncpy (sec_wifi_msg.mac_addr, resp.c_str(), MAC_ADDR_LEN);
                                break;
                            case 5:
                                if(device_wifi_mode == eAPMode)
                                {
                                    strncpy (wifi_msg.ssid, hotspot_ssid.c_str(), MAX_SSID_LENGTH);
                                }
                                else
                                {
                                    strncpy (wifi_msg.ssid, resp.c_str(), MAX_SSID_LENGTH);
                                }
                                break;
                            case 6:
                                sec_wifi_msg.signal_strength = (resp != "") ? stoi(resp) : 0;
                                break;
                            default:
                                break;
                        }
                    }
                }
        }
#endif
    pthread_mutex_unlock(&scan_connect_mutex);
    }while ( 1 );
}

bool configure_interface_AP_mode()
{
    file_delete(wifi_scan_result_file);
    sleep(5);
    manage_hotspot_connection(default_iface);
    prev_device_wifi_mode = device_wifi_mode;
    device_wifi_mode = eAPMode;
    station_up = false;
   // send_msg_for_installer_scan();
    if(!monitor_hotspot_done)
    {
        int res = pthread_create(&monitor_hotspot_th, NULL, monitor_hotspot_thread, NULL);
        if (res != 0){
            LOG_E(TAG, "Thread Creation Failed For Monitor Hotspot");
        }
        else
        {
            monitor_hotspot_done = true;
        }
    }
    else
    {
        LOG_I(TAG, "Not Creating Monitor Hotspot Thread As Its Already Created");
    }
}

bool disableHotspot()
{
     if( (nd_device_obj != nullptr) && (nd_device_obj->getDeviceType() != eKrait_1) && (nd_device_obj->getDeviceType() != eKrait_2))
     {
        if(station_up)
        {
            LOG_I(TAG,"Station is Already Up, No Need to disable hotspot");
            return true;
        }
     }
    LOG_I(TAG, "Disabling Hotspot");

#ifdef KRAIT
    kill_process("hostapd");
    kill_process("dnsmasq");
#else
    string cmd = "sudo nmcli con down DriveriHostspot";
    string cmd0 = "sudo nmcli con del DriveriHostspot";

    if (system_execute("SET_HOTSPOT_DOWN", cmd) != 0)
    {
        LOG_E(TAG, "Failed To Set  DriveriHostspot down");
    }

    if (system_execute("DEL_HOTSPOT", cmd0) != 0)
    {
        LOG_E(TAG, "Failed To Delete DriveriHostspot");
    }
#endif

    hotspot_created = false;

    string cmd1 = "ip link set wlan0 down";
    string cmd2 = "ip addr flush dev wlan0";
    string cmd3 = "iw dev wlan0 set type managed";
    string cmd4 = "ip link set wlan0 up";
    string resp = "";

    if (system_execute("SET_WLAN0_DOWN", cmd1) != 0)
    {
        LOG_E(TAG, "Failed To Set wlan0 down");
        return false;
    }

    if (system_execute("FLUSH_WLAN0", cmd2) != 0)
    {
        LOG_E(TAG, "Failed To Flush wlan0");
        return false;
    }

    if (system_execute("SET_WLAN0_MANAGED", cmd3) != 0)
    {
        LOG_E(TAG, "Failed To Set wlan0 to managed");
        return false;
    }

    if (system_execute("SET_WLAN0_UP", cmd4) != 0)
    {
        LOG_E(TAG, "Failed To Set wlan0 up");
        return false;
    }
    return true;
}

bool configure_interface_to_STA_mode()
{
    disableHotspot();
    if(!sta_setup_done)
    {
        if(ext_cam_enabled)
        {
            wifi_fallback_enabled = false; // Stopping creation of secondary interface when ext_cam is enabled.
            wifi_fallback_iface = secondary_iface;
            LOG_I(TAG, "Not Creating Secondary Interface As Ext Cam Is Enabled");
            LOG_I(TAG, "Creating Thread To Manage Mdvr Wifi Connection");
            int ret = pthread_create(&ext_cam_wifi_th, NULL, ext_cam_wifi_thread, NULL);
            if (ret != 0)
            {
                LOG_E(TAG, "Thread Creation Failed For Ext Cam Wifi Connection Management");
            }
        }

        prev_device_wifi_mode = device_wifi_mode;
        device_wifi_mode = eSTAMode;

        if(wifi_fallback_enabled) {
            if(!(installer_app_manage && ext_cam_enabled)) {
                wifi_fallback_manage = true;
            }
            int ret = pthread_create(&wifi_fallback_th, NULL, wifi_fallback_thread, NULL);
            if (ret != 0)
            {
                LOG_E(TAG, "Thread Creation Failed For Wifi Fallback Connection Management");
                return false;
            }

        }
        sta_setup_done = true;
    }
    else
    {
        LOG_E(TAG,"STA Mode Setup Already Done");
    }

    return true;
}

bool hotspot_up()
{
    if ( (nd_device_obj != nullptr) && (nd_device_obj->getDeviceType() != eKrait_1) && (nd_device_obj->getDeviceType() != eKrait_2))
    {
        const int MAX_TIME_FOR_HOTSPOT_CREATION = 15;
        int time_waited_for_hotspot_creation = 0;
        while (time_waited_for_hotspot_creation < MAX_TIME_FOR_HOTSPOT_CREATION)
        {
            string cmd = "sudo nmcli device show wlan0 | awk '/GENERAL.CONNECTION/ {print $2}'";
            string ip_cmd = "nmcli device show wlan0 | awk '/IP4.ADDRESS/ {print $2}' | cut -d'/' -f1 ";
            string resp = "";
            string ip_resp = "";

            if(system_execute_with_resp("WIFI", cmd, resp) == false)
            {
                LOG_E(TAG, "Failed to check connection status of wlan0");
                return false;
            }
            if(system_execute_with_resp("WIFI", ip_cmd, ip_resp) == false)
            {
                LOG_E(TAG, "Failed to check IP address of wlan0");
                return false;
            }
            if((resp.find("DriveriHostspot") != string::npos) && (ip_resp.find("10.42.0.1") != string::npos))
            {
                LOG_I(TAG, "Hotspot is up with IP: %s", ip_resp.c_str());
                return true;
            }
            sleep(UNIT_TIME);
            time_waited_for_hotspot_creation++;
        }
        LOG_I(TAG, "Hotspot is not up after waiting %d secs", MAX_TIME_FOR_HOTSPOT_CREATION);
        return false;
    }
    else
    {
        LOG_I(TAG,"Krait Device, Not Checking Hotspot Up");
        return false;
    }
}

void check_and_manage_wifi_mode()
{
    LOG_I(TAG,"current_wifi_mode = %d", current_wifi_mode);
    LOG_I(TAG,"device_wifi_mode = %d", device_wifi_mode);

    if ((nd_device_obj->getDeviceType() != eKrait_1) && (nd_device_obj->getDeviceType() != eKrait_2))
    {
        if(current_wifi_mode == device_wifi_mode)
        {
            if((device_wifi_mode == eSTAMode))
            {
                station_up = true;
                LOG_I(TAG,"Station Is Already Up");
            }
        }
    }
    if(device_wifi_mode == eSTAMode)
    {
        configure_interface_to_STA_mode();
    }
    else
    {
        if((device_wifi_mode == eAPMode) && (hotspot_up()))
        {
            LOG_I(TAG,"Hotspot Is Already Up");
            hotspot_created = true;
        }
        configure_interface_AP_mode();
    }
}



void send_wifi_info_healthstats(int64_t &time)
{
    json_t *ipv4_info = json_array();
    json_t *ipv6_info = json_array();
    json_t *routes_info = json_array();
    json_t *wifi_info = json_object();
    json_t *root = json_object();
    char* req_params = NULL;

    char *ptr;
    ptr = strtok(wifi_msg.routes, "\r\n");
    int i =0;

    while (ptr != NULL)
    {
        LOG_D(TAG,"route : %s",ptr);
        json_array_append_new(routes_info, json_string(ptr));
        ptr = strtok (NULL, "\r\n ");
        i++;
    }
    remove(std::begin(wifi_msg.mac_addr), std::end(wifi_msg.mac_addr), '\n');
    remove(std::begin(wifi_msg.mac_addr), std::end(wifi_msg.ssid), '\n');

    if(strcmp(wifi_msg.ipv4_address,"") != 0)
    {
       json_array_append_new(ipv4_info, json_string(wifi_msg.ipv4_address));
    }
    else
    {
       json_array_append_new(ipv4_info, json_string("NA"));
    }

    if(strcmp(wifi_msg.ipv6_address,"") != 0)
    {
        json_array_append_new(ipv6_info, json_string(wifi_msg.ipv6_address));
    }
    else
    {
        json_array_append_new(ipv6_info, json_string("NA"));
    }

    if(strcmp(wifi_msg.mac_addr,"") != 0)
    {

        json_object_set_new( wifi_info, "mac_id", json_string(wifi_msg.mac_addr));
        json_object_set_new( wifi_info, "ssid", json_string(wifi_msg.ssid));
    }
    else
    {

        json_object_set_new( wifi_info, "mac_id", json_null());
        json_object_set_new( wifi_info, "ssid", json_null());
    }

    json_object_set_new( wifi_info, "ts", json_integer(time));
    json_object_set_new( wifi_info, "routes" , routes_info);
    json_object_set_new( wifi_info, "status" , json_boolean(wifi_msg.status));
    json_object_set_new( wifi_info, "ipv4s" , ipv4_info);
    json_object_set_new( wifi_info, "ipv6s" , ipv6_info);
    json_object_set_new( wifi_info, "signal_strength", json_integer(wifi_msg.signal_strength));
    json_object_set_new( wifi_info, "clients", json_string(connected_devices.c_str()));
    json_object_set_new( root, "health_info:network_info:wifi" , wifi_info);
    json_object_set_new( root, "isArray" , json_string("true"));
    req_params = json_dumps(root, 0);
    int length = strlen(req_params);

    LOG_D(TAG,"health msg = %s",req_params);
    if(strcmp(wifi_msg.ipv4_address,"") != 0 || strcmp(wifi_msg.ipv6_address,"") != 0)
    {
        nd_service_obj->send_msg_healthstats(req_params, length);
    }
    json_decref(root);
    free(req_params);

    json_t *sec_ipv4_info = json_array();
    json_t *sec_ipv6_info = json_array();
    json_t *sec_routes_info = json_array();
    json_t *sec_wifi_info = json_object();
    json_t *sec_root = json_object();
    char* sec_req_params = NULL;

    char *sec_ptr;
    sec_ptr = strtok(sec_wifi_msg.routes, "\r\n");
    i =0;

    while (sec_ptr != NULL)
    {
        LOG_D(TAG,"route : %s",sec_ptr);
        json_array_append_new(sec_routes_info, json_string(sec_ptr));
        sec_ptr = strtok (NULL, "\r\n ");
        i++;
    }
    remove(std::begin(sec_wifi_msg.mac_addr), std::end(sec_wifi_msg.mac_addr), '\n');
    remove(std::begin(sec_wifi_msg.mac_addr), std::end(sec_wifi_msg.ssid), '\n');

    if(strcmp(sec_wifi_msg.ipv4_address,"") != 0)
    {
        json_array_append_new(sec_ipv4_info, json_string(sec_wifi_msg.ipv4_address));
    }

    if(strcmp(sec_wifi_msg.ipv6_address,"") != 0)
    {
        json_array_append_new(sec_ipv6_info, json_string(sec_wifi_msg.ipv6_address));
    }

    if(strcmp(sec_wifi_msg.mac_addr,"") != 0)
    {

        json_object_set_new( sec_wifi_info, "mac_id", json_string(sec_wifi_msg.mac_addr));
        json_object_set_new( sec_wifi_info, "ssid", json_string(sec_wifi_msg.ssid));
    }
    else
    {

        json_object_set_new( sec_wifi_info, "mac_id", json_null());
        json_object_set_new( sec_wifi_info, "ssid", json_null());
    }

    json_object_set_new( sec_wifi_info, "ts", json_integer(sec_wifi_msg.time));
    json_object_set_new( sec_wifi_info, "routes" , sec_routes_info);
    json_object_set_new( sec_wifi_info, "ipv4s" , sec_ipv4_info);
    json_object_set_new( sec_wifi_info, "ipv6s" , sec_ipv6_info);
    json_object_set_new( sec_wifi_info, "signal_strength", json_integer(sec_wifi_msg.signal_strength));
    json_object_set_new( sec_root, "health_info:network_info:secondary_wifi" , sec_wifi_info);
    json_object_set_new( sec_root, "isArray" , json_string("true"));
    sec_req_params = json_dumps(sec_root, 0);
    int sec_length = strlen(sec_req_params);

    LOG_D(TAG,"sec health msg = %s",sec_req_params);
    if(strcmp(sec_wifi_msg.ipv4_address,"") != 0 || strcmp(sec_wifi_msg.ipv6_address,"") != 0)
    {
        nd_service_obj->send_msg_healthstats(sec_req_params, sec_length);
    }
    json_decref(sec_root);
    free(sec_req_params);
    fill_default_wifi_info();

 }

static bool init_msgq() {

    //Create message queue
    server_q = nd_msgq_t::get_msgq( get_msgq_name(), nd_msgq_t::ND_MSGQ_SERVER );

    if( server_q == NULL ) {
        LOG_E(TAG, "Cannot create message queue");
        return false;
    }

    LOG_I(TAG, "Message queue created");
    return true;
}

void *msg_loop_thread(void *args) {

    LOG_I(TAG, "Inside Msg Loop thread");
    while (1) {
        nd_msgq_t::nd_msg_t *msg = NULL;
        //Block until a new message is received
        if( (msg = server_q->receive( )) == NULL ) {
            LOG_E(TAG, "Receive message failed" );
            continue;
        }

        msg_type_t type = get_msg_type(msg->get_buffer());
        string msg_client = get_msgq_id(msg->get_buffer());
        generic_msg_t *m = (generic_msg_t *)msg->get_buffer();
        if( m == NULL ) {
            LOG_E(TAG, "Received NULL message");
            continue;
        }

        LOG_I(TAG, "%d received", m->msg_type);

        switch(m->msg_type)
        {
            case CREATE_INSTALLER_HOTSPOT:
            {
                create_installer_hotspot_t *bluetooth_msg = (create_installer_hotspot_t *)m;

                if( NULL == bluetooth_msg ) {
                    LOG_E(TAG, "msg->get_buffer() returned NULL");
                    break;
                }

#ifdef BAGHEERA2
                if (!bluetooth_msg -> enable){
                    LOG_E(TAG, "exiting since bluetooth_msg->enable is false");
                    break;
                }

                gInstallerBandType = bluetooth_msg->band_type;

                LOG_I(TAG, "got the instruction to enable hotspot for bandtype: %c", gInstallerBandType);
#else
                LOG_I(TAG, "got the instruction to enable hotspot");
#endif
                char mac_addr[MAX_MAC_ADDR_LEN];
                strncpy(mac_addr, bluetooth_msg->mac_addr, MAX_MAC_ADDR_LEN-1);
                mac_addr[MAX_MAC_ADDR_LEN-1] = '\0';
                installer_app_mac_addr = mac_addr;
                installer_app_manage = true;

                if((file_touch(installerAppActive) == false) )
                {
                    LOG_E(TAG, "Failed to Create Installer App Active File");
                }
                else
                {
                    LOG_I(TAG, "Installer App Active File Created");
                    installer_app_active = true;
                }

                configure_interface_AP_mode();
            }
            break;
            case STOP_INSTALLER_HOTSPOT:
            {
                LOG_I(TAG, "received message to stop installer hotspot");
                if(prev_device_wifi_mode == eAPMode)
                {
                    LOG_I(TAG,"Ignoring Message To Stop Hotspot As Driveri Was Alerady In Hotspot Mode Before Installer App");
                    LOG_I(TAG, "sending message to restart bluetooth activities");
                    send_bth_restart_msg();
                }
                else
                {
                    //check if secondary iface is already available
#ifdef KRAIT
                    // kill any dnsmasq
                    kill_process("dnsmasq");

                    // kill any wpa_supplicant
                    kill_process("wpa_supplicant");

                    //kill any existing hostapd
                    kill_process("hostapd");

#endif

                    if(check_for_wlan_iface(default_iface) == true) {
                        wifi_disconnect(default_iface);
                        //delete_secondary_iface(secondary_iface);
                    }

                    hotspot_created = false;
                    wifi_fallback_manage = true;

                    if (!file_delete(installerAppActive)) {
                        LOG_E(TAG, "Failed to delete Installer App Active File");
                    }

                    stop_installer_hotspot_t *stop_msg = (stop_installer_hotspot_t *)m;
                    if(stop_msg->rescan) {
                        LOG_I(TAG, "sending message for bt rescan to btfv");
                        send_msg_for_installer_scan();
                    } else {
                        LOG_I(TAG, "sending message to restart bluetooth activities");
                        send_bth_restart_msg();
                    }
                }
            }
            break;
            case AUTO_CONFIG_MDVR:
            {
                mdvr_auto_conf_t* conf = (mdvr_auto_conf_t*)m;
                mdvr_ssid = conf->ssid;
                mdvr_password = conf->pwd;
                LOG_I(TAG,"Auto Config SSID Is %s",mdvr_ssid.c_str());
                string msg = "Auto Configuring To " + mdvr_ssid;
                nd_service_obj->send_err_msg(SM_E_WMGR_AUTO_CONFIG_MDVR_DONE, conf->speed, msg );
            }
       	    break;
            case REQ_HEALTH_INFO:
            {
                hs_gen_time *hs_time = (hs_gen_time*)m;
                send_wifi_info_healthstats(hs_time->time);
                LOG_I(TAG,"RECIEVED REQ_WIFI_NETWORK_INFO");
            }
            break;
            case POWERMON_IGNITION:
            {
                powermon_ignition_msg_t *msg = (powermon_ignition_msg_t*)m;

                LOG_I(TAG, "Ign status = %lld, crank_change_time = %lld, lpw_status = %lld", msg->status, msg->crank_change_time, msg->lpw_status);
                if (msg->status == static_cast<int64_t>(IGNITION_ON))
                {
                    set_ignition_status(true);
                }
                else if (msg->status == static_cast<int64_t>(IGNITION_OFF))
                {
                    set_ignition_status(false);
                }
                else
                {
                    LOG_E(TAG,"Error in reading ignition status");
                }
            }
            break;
            case REQ_TOGGLE_DRIVERI_WIFI_MODE:
            {
                LOG_I(TAG,"RECIEVED REQ_TOGGLE_DRIVERI_WIFI_MODE");
                toggle_wifi_mode_t* wifi_toggle =  (toggle_wifi_mode_t*)m;
                setCurrDHUBWifiModeFromDB();
                if(wifi_toggle->mode == eAPMode)
                {
                    LOG_I(TAG,"Going To Toggle Wifi Mode To AP");
                    if(wifi_toggle->persist_mode)
                    {
                       LOG_I(TAG,"Persisting AP Mode As GEN 2 DHUB with VBUS Paired With Device");
                       persist_ap_mode = true;
                    }
                    configure_interface_AP_mode();
                }
                else if(wifi_toggle->mode == eSTAMode)
                {
                    LOG_I(TAG,"Going To Toggle Wifi Mode To STA");
                    configure_interface_to_STA_mode();

                }
                else
                {
                    LOG_E(TAG,"Invalid Wifi Mode");
                }

            }
            break;
            case REQ_WIFI_REG:
            {
                LOG_I(TAG,"RECIEVED REQ_WIFI_REG");
                wifi_conn_info_clients.push_back(msg_client);
            }
            break;
            case REQ_DHUB_WIFI_MODE_FROM_DB:
            {
                LOG_I(TAG,"RECIEVED GET_DHUB_WIFI_MODE_DB");
                setCurrDHUBWifiModeFromDB();
            }
            break;
            case DHUB_AP_CONFIG_CORRUPTED:
            {
                mdvr_auto_conf_t* conf = (mdvr_auto_conf_t*)m;
                LOG_I(TAG,"RECIEVED DHUB_AP_CONFIG_CORRUPTED");

                mdvr_ssid = conf->ssid;
                mdvr_password = conf->pwd;

                if(ext_cam_feature_enabled && !report_dhub_config_corruption)
                {
                    LOG_E(TAG,"DHUB AP Config Corrupted");
                    nd_service_obj->send_err_msg(SM_E_WMGR_DHUB_CONF_CORRUPT, NDService::UNUSED_ERR_AUX_CODE, "DHUB AP Config Corrupted" );
                    report_dhub_config_corruption = true;
                }
            }
            break;
            case RES_SPEED_REG:
            {
                LOG_I(TAG, "Speed Service Registration Successfull");
            }
            break;
            case RES_SPEED_UNREG:
            {
                LOG_I(TAG, "Speed Service Un-Registration Successfull");
            }
            break;
            case RES_SPEED_UPDATE:
            {
                res_speed_update_msg_t *msg = (res_speed_update_msg_t *)m;
                current_speed = msg->speed;
                LOG_D(TAG,"speed is %d",current_speed);
            }
            break;
            case REQUEST_ANTENNA_TIME_RESPONSE:
            {
                LOG_I(TAG, "Received REQUEST_ANTENNA_TIME_RESPONSE");
                lock_guard<std::mutex> lk(start_wifi_scan_mutex);
                start_wifi_scan = true;
                start_wifi_scan_cv.notify_one();
                LOG_I(TAG, "Start WiFi scan Signal sent");

            }
            break;
            default:
            {
                LOG_E(TAG, "unknown message type");
            }
            break;
        }
        if(msg != NULL)
        {
            delete msg;
            msg = NULL;
        }
    }
}

void init_wifi_info_cmd_arr()
{
    wifi_info_cmds = { "ip route | grep wlan0",
        "ip -4 addr show dev wlan0  | grep inet  | grep global | awk '{print $2;}'",
        "ip -6 addr show dev wlan0  | grep inet6  | grep global | awk '{print $2;}'",
        "route -n | grep wlan0 | grep  U[^G] | awk '{print $1;}'",
        "ip link show dev wlan0 | grep link | awk '{print($2)}'",
#ifdef BAGHEERA2
        "iwconfig wlan0 | grep ESSID | awk '{print($4)}' | cut -d '\"' -f 2",
        "iwconfig wlan0 | awk '/Signal level/ {print $4}' | sed 's/[^0-9\\-]//g'"};
#else
        "iwconfig wlan0 | grep ESSID | awk '{print($3)}' | cut -d '\"' -f 2",
        "iw dev wlan0 link | grep signal | cut -d ' ' -f 2"};
#endif
}

void init_sec_wifi_info_cmd_arr()
{
    sec_wifi_info_cmds = { "ip route | grep secondary",
        "ip -4 addr show dev secondary  | grep inet  | grep global | awk '{print $2;}'",
        "ip -6 addr show dev secondary  | grep inet6  | grep global | awk '{print $2;}'",
        "route -n | grep secondary | grep  U[^G] | awk '{print $1;}'",
        "ip link show dev secondary | grep link | awk '{print($2)}'",
#ifdef BAGHEERA2
        "iwconfig secondary | grep ESSID | awk '{print($4)}' | cut -d '\"' -f 2",
#else
        "iwconfig secondary | grep ESSID | awk '{print($3)}' | cut -d '\"' -f 2",
#endif
        "iwconfig secondary | grep Signal | awk '{print($4)}' | cut -d '=' -f 2"};
}

void read_iosix_ssid_config (string &ssid, string &password) {
    bool get_override_val = true;
    bool is_val_overridden = false;

    ssid     = "";
    password = "";

    if(file_is_present(IOSIXCONFIG))
    {
        Config_parser c(IOSIXCONFIG);
        if (c.getParseStatus() != true) {
            LOG_E (TAG, "Error parsing %s", IOSIXCONFIG.c_str());
            return;
        }

        ssid     = c.getConfig(IOSIX_CONFIG_SECTION, IOSIX_SSID_NAME, "", get_override_val, is_val_overridden);
        password = c.getConfig(IOSIX_CONFIG_SECTION, IOSIX_SSID_PASS, "", get_override_val, is_val_overridden);
    }
}

void read_hotspot_interface_config ()
{
    hotspot_interface = default_iface;

    Config_parser bagh_config (bagheera_config_path);
    if (bagh_config.getParseStatus() != true) {
        LOG_E (TAG, "Error parsing %s", bagheera_config_path.c_str());
        return;
    }

    bool get_override_val = true;
    bool is_val_overridden = false;

    string interface_id = bagh_config.getConfig(BAGH_CONF_WIFI_SECTION, BAGH_CONF_WIFI_SECTION_HOTSPOT, "1",
             get_override_val, is_val_overridden);

    if(interface_id != "1")
    {
        hotspot_interface = secondary_iface;
    }

    LOG_I(TAG, "WIFI Hotspot Will Be Configured on %s", hotspot_interface.c_str());
}

void read_hotpot_channel_number()
{
    hotspot_channel = DEFAULT_CHANNEL;
    LOG_I(TAG,"before Hotspot Channel is %d",hotspot_channel);

    read_config config;
    bool result = config.read_integer_config(eBagheeraConfig, BAGH_CONF_WIFI_SECTION, BAGH_CONF_WIFI_SECTION_HOTSPOT_CHANNEL, hotspot_channel, "6");
    if(!result)
    {
        LOG_I(TAG,"after Hotspot Channel is %d",hotspot_channel);
    }
    return;
}

void read_max_disconnections_count()
{

    max_disconnections = 3;
    read_config config;
    bool result = config.read_integer_config(eBagheeraConfig, BAGH_CONF_WIFI_SECTION, BAGH_CONF_WIFI_SECTION_MAX_DISCONNECTIONS, max_disconnections, "3");
    return;

}

void read_wifi_operation_mode()
{
    string operation_mode = "AP";

    read_config config;
    bool result = config.read_string_config(eBagheeraConfig, BAGH_CONF_WIFI_SECTION, BAGH_CONF_WIFI_SECTION_OPERATION_MODE, operation_mode, "AP");

    if(operation_mode == "AP")
    {
        device_wifi_mode = eAPMode;
    }
    else
    {
        device_wifi_mode = eSTAMode;
    }

}

void read_current_wifi_mode()
{
    string current_mode = "";
    current_wifi_mode = eSTAMode;

    sleep(1);
    string cmd_current_wifi_mode = "iw dev wlan0 info | grep type | awk '{print $2}'";

    if (system_execute_with_resp("WIFI", cmd_current_wifi_mode, current_mode))
    {
        LOG_I(TAG, "Current Wifi Mode Is %s", current_mode.c_str());
        if (current_mode.find("AP") != string::npos)
        {
            current_wifi_mode = eAPMode;
        }
    }
    else
    {
        LOG_E(TAG, "Failed to get current wifi mode, defaulting to STA mode");
    }
}

void setup_driveri_wifi_mode()
{
    setCurrDHUBWifiModeFromDB();
    dhub_wifi_mode = getCurrDHUBWifiModeFromDB();
    if(iosix_enabled == false)
    {
        LOG_I(TAG, "IOSIX feature disabled or ssid invalid");
        if(ext_cam_enabled)
        {
            if(dhub_wifi_mode == eAPMode)
            {
                device_wifi_mode = eSTAMode;
            }
            else
            {
                device_wifi_mode = eAPMode;
            }
        }
        else
        {
                device_wifi_mode = eSTAMode;
        }
    }
    else
    {
        device_wifi_mode = eAPMode;
    }
    LOG_I(TAG, "Device Wifi Mode Is Set To %d", device_wifi_mode);
}

void read_driver_login_speed_config(int &drv_login_speed)
{
    drv_login_speed = 10; // Default value

    read_config config;

    bool result = config.read_integer_config(eBagheeraConfig, BAGH_CONF_DRIVER_LOGIN_SECTION, BAGH_CONF_DRIVER_LOGIN_SECTION_LOGIN_SPEED, drv_login_speed, "10");

    LOG_I(TAG, "Driver Login Speed Is : %d", drv_login_speed);
}

static bool register_for_speed_with_speed(const bool regSpeed)
{

    if(regSpeed)
    {
        req_speed_reg_msg_t req;
        req.reg_type = SPEED_REG_DRV_LOGIN;
        req.speed = 0;
        req.contig_secs = drv_login_time;

        if( false == send_msg( (generic_msg_t *)&req, REQ_SPEED_REG, sizeof(req), get_msgq_name(), Q_SPEED, msg_idx++ ) ) {
            LOG_E(TAG, "Not Able To Register With Speed Service");
            return false;
        }
    }
    else
    {
        req_speed_unreg_msg_t req;

        if( false == send_msg( (generic_msg_t *)&req, REQ_SPEED_UNREG, sizeof(req), get_msgq_name(), Q_SPEED, msg_idx++ ) ) {
            LOG_E(TAG, "Not Able To Un-Register With Speed Service");
            return false;
        }
    }
    return true;
}

void get_wifi_chipset_vendor_name()
{
    WifiModuleVendorType wifi_module = getWifiModuleVendorType();
    string message = "";
    if(wifi_module==WIFI_MODULE_REALTEK)
    {
        message = "Realtek Wi-Fi Module Detected";
    }
    else if(wifi_module==WIFI_MODULE_AZUREWAVE)
    {
        message = "Azurewave Wi-Fi Module Detected";
    }
    else if(wifi_module==WIFI_MODULE_QUALCOMM)
    {
        message = "Qualcomm Wi-Fi Module Detected";
    }
    else{
        message = "Unknown Wi-Fi Module Detected";
    }
    LOG_I(TAG, "%s", message.c_str());
    nd_service_obj->send_err_msg(SM_E_WMGR_WIFI_CHIP_NAME, NDService::UNUSED_ERR_AUX_CODE, message.c_str());
}

int main() {

    nd_service_obj = NDService::get_service_obj(TAG);
    pthread_t msg_loop_th;

    bool connected = false;
    nd_log_init (log_dir.c_str());
#ifdef ROUTE_LOGS
    route_logs( log_dir.c_str() );
#endif

    LOG_I(TAG,"**********Starting WIFI MGR Service**********");

    get_wifi_chipset_vendor_name();
    // Read config to confirm if ext cam feature is enabled
    ext_cam_enabled = is_ext_cam_feature_enabled();
    nd_device_obj_init(); // Initialize the device object
    read_ext_camera_common_config(ext_cam_feature_enabled);
    read_hotspot_interface_config();
    read_hotpot_channel_number();
    read_max_disconnections_count();
    read_driver_login_speed_config(drv_login_speed);
    get_driveri_hotspot_credentials(hotspot_ssid, hotspot_password);
#ifdef KRAIT
    generate_hostapd_conf_krait();
#endif
    get_info_for_vbus(nd_service_obj);
    string vbus_sn = get_vbus_sn_();
    vbus_mac_id = get_mac_id(vbus_sn);
    //Initialize Message Queue
    bool msgq_init_status = init_msgq();
    if( false == msgq_init_status ) {
        string str_msg = "MSG queue init failed";
        LOG_E(TAG, str_msg.c_str());
        nd_service_obj->send_err_msg(SM_E_WMGR_MSGQ_INIT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg );
    } else {
        //msg loop thread
        LOG_I(TAG, "creating msg loop thread");
        int ret = pthread_create(&msg_loop_th, NULL, msg_loop_thread, NULL);
        if (ret != 0){
            LOG_E(TAG, "thread creation failed for wifi mgr msg loop failed");
        }
    }
    #ifdef AUTOMATION
    dta_enabled = isAutomationEnabled();
    #endif
    check_and_request_ignition_status();

    bool isSpeedServiceAvail = check_for_service(Q_SPEED);
    if(isSpeedServiceAvail)
    {
        if(register_for_speed_with_speed(true) == false)
        {
            LOG_E (TAG,"Registration For Speed Service Failed");
        }
    }
    else
    {
        LOG_E (TAG,"Speed Service Not Available");
    }

    if(ext_cam_feature_enabled == false) {
        LOG_I(TAG, "Ext cam feature disabled or ssid invalid");
    } else {
        read_ext_cam_ssid_config(mdvr_ssid, mdvr_password);
        if (mdvr_ssid == "") {
            LOG_E (TAG,"wifi ssid name is empty");
            ext_cam_feature_enabled = false;
        } else {
            ext_cam_feature_enabled = true;
        }
    }

    if(!ext_cam_feature_enabled)
    {
        mdvr_ssid = "default";
        mdvr_password = "default";
    }

    init_wifi_info_cmd_arr();
    init_sec_wifi_info_cmd_arr();
    read_iosix_config(iosix_enabled);
    read_current_wifi_mode();
    setup_driveri_wifi_mode();
    fill_default_wifi_info();

    if ( (pthread_create (&update_wifi_info_th, NULL, update_wifi_info_thread, NULL)) !=0 )
    {
        LOG_E (TAG,"Can't create siginfo thread, exiting");
    }

    // Read config to confirm if wifi fallback feature is enabled
    wifi_fallback_enabled = read_wifi_fallback_config ();

    int res = pthread_create(&wifi_scan_th, NULL, wifi_scan_thread, NULL);
    if (res != 0)
    {
        LOG_E(TAG, "Thread Creation Failed For Wifi Scan");
    }
    check_and_manage_wifi_mode();
    pthread_join(msg_loop_th, NULL);
    pthread_join(update_wifi_info_th, NULL);
    if(ext_cam_enabled) {
        pthread_join(ext_cam_wifi_th, NULL);
    }
    if(monitor_hotspot_done)
    {
        pthread_join(monitor_hotspot_th, NULL);
    }
    pthread_join(wifi_scan_th, NULL);

    LOG_E (TAG,"exiting from main");
    nd_service_obj->release_service_obj();
    return 0;
}
