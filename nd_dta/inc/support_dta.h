#include "config_parser.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "nd_msg_utils.h"
#include "nd_msg_types.h"
#include <thread>
#include <system_utils.h>
#include <log.h>
#include <nlohmann/json.hpp> //nlohmann/json library for JSON parsing
#include <signal.h>
#include <iostream>
#include <nd_server.h>
#include <nd_read_config.h>
#include <nd_time.h>
#include <nd_file_utils.h>
#include <ctime>
#define TAG "DTA"
#define Q_NAME "DTA"
using namespace std;
using json = nlohmann::json;

class support_dta{
    public:
        typedef std::tuple<bool,int64_t, int64_t, std::string> RebootStatus;
        struct conn_mgr_t{
        GENERIC_MSG
        uint sdk_error;
        };
        string getBroadcastAddress(string device_type);
        RebootStatus track_reboot(int timeout);
        RebootStatus get_reboot_status();
        void receiveData(int tcpSocket);
        string get_config(const string& config, const string& section_name, const string& param_name, const string& default_val, bool get_override_config, bool is_value_overridden);
        void handle_command_to_run(json json_data, int tcpSocket);
        void handle_track_reboot_or_shutdown(json json_data, int tcpSocket);
        void handle_LURejectCause(json json_data);
        void handle_sdk_error(json json_data);
        void handle_get_reboot_or_shutdown_status(json json_data, int tcpSocket);
        void handle_get_system_boot_uptime(json json_data, int tcpSocket);
        void handle_get_config(json json_data, int tcpSocket);
        void handle_dhub_recovery_flag(json json_data);
        void handle_button(json json_data);
        void handle_system_reboot(int tcpSocket);
        void handle_reboot_request_to_PM();
        void get_udid();
        void handle_get_udid(int tcpSocket);
        void handle_to_start_dts(json json_data);
};
