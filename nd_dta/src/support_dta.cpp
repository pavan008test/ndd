#include <support_dta.h>
string bagheera_config_path     = "/home/ubuntu/.nddevice/latest/bagheera_config.ini";
string device_config_path       = "/home/ubuntu/config/deviceconfig.ini";
string cloud_config_path        = "/home/ubuntu/.nddevice/latest/cloudconfig.ini";
#ifdef KRAIT
string DHUB_config_path         = "/data/nd_files/config/mdvr_config.ini";
string cam_override_config_path = "/data/nd_files/config/cam_override.ini";
#else
string DHUB_config_path         = "/home/ubuntu/.nddevice/mdvr_config.ini";
string cam_override_config_path = "/home/ubuntu/config/cam_override.ini";
#endif
string conn_mgr_config_path     = "/home/ubuntu/config/conn_mgr_config.txt";
string automation_config = "/home/ubuntu/config/automation_config.ini";
string set_record_config_file_path = "/dev/shm/set_record_config.ini";
string sam_config_path = "/home/ubuntu/.nddevice/latest/sam_config.ini";
#ifdef BAGHEERA2
static const string time_sync_token_file = "/dev/shm/nd_files_c/time_sync_token_file.bin";
#else
static const string time_sync_token_file = "/dev/shm/time_sync_token_file.bin";
#endif
const int TCP_SEND_MAX_RETRY = 5;
string udid = "";


string support_dta::get_config(const string& config, const string& section_name, const string& param_name, const string& default_val, bool get_override_config, bool is_value_overridden) {

    std::map<std::string, std::string> configMap = {
    {"DeviceConfig", device_config_path},
    {"BagheeraConfig", bagheera_config_path},
    {"CloudConfig", cloud_config_path},
    {"DHUBConfig", DHUB_config_path},
    {"ConnMgrConfig", conn_mgr_config_path},
    {"CamOverrideConfig", cam_override_config_path},
    {"DHUBSetRecordConfig", set_record_config_file_path},
    {"AutomationConfig", automation_config},
    {"SamConfig", sam_config_path}
    };

    auto it = configMap.find(config);

    if (it == configMap.end()) {
        return "Invalid config type";
    }

    Config_parser config_parser(it->second);

    if (!config_parser.getParseStatus()){
        LOG_E(TAG, "Can not parse config\n");
        return "Failed to parse config";
    }
    string value = config_parser.getConfig(section_name, param_name, default_val, get_override_config, is_value_overridden);
    return value;
}


string support_dta::getBroadcastAddress(string device_type) {
    string result;
    string tag = "getBroadcastAddress";
    string cmd;
    size_t found = string::npos;

    size_t dev_type_found = device_type.find("krait");
    if (dev_type_found == std::string::npos) {
        cmd = "ifconfig | grep -A1 wlan0 | grep broadcast | awk '{print $6}'";
    } else {
        cmd = "ifconfig | grep -A1 wlan0 | grep Bcast | awk -F'[ :]+' '{print $6}'";
    }
    if (!system_execute_with_resp(tag, cmd, result)) {
        return ""; // failed to execute command
    }
    found = result.find("\n");
    if (found != string::npos) {
        result = result.substr(0, found);  // trim the result to the first line
    }
    LOG_I(TAG,"Broadcast address is %s",result.c_str());
    return result;
}

/*track_reboot function will write uptime and current time into a file every 2sec and will continue to do so untill timeout
    If this function returns then device haven't rebooted*/
support_dta::RebootStatus support_dta::track_reboot(int timeout) {
    LOG_I(TAG, "track_reboot received with timeout: %d", timeout);
    bool device_rebooted = false;
    string uptimeFileName = "/home/ubuntu/uptime.txt";
    ifstream uptimeFileRead(uptimeFileName);
    int64_t lastUptime;
    string lastTime;
    if (uptimeFileRead.is_open()) {
        string line;
        getline(uptimeFileRead, line);
        stringstream ss(line);
        ss >> lastUptime;
        getline(ss, lastTime);
        uptimeFileRead.close();
    } else {
        lastUptime = get_system_monotonic_time();
        ofstream uptimeFileWrite(uptimeFileName);
        uptimeFileWrite.close();
    }

    auto start = std::chrono::high_resolution_clock::now();
    while (true) {
        int64_t currentUptime = get_system_monotonic_time();

        auto now = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start);

        if(elapsed.count() >= timeout) {
            LOG_I(TAG, "Timeout reached without reboot. Returning current state.");
            return std::make_tuple(device_rebooted, lastUptime, currentUptime, lastTime);
        }

        time_t currentTime = chrono::system_clock::to_time_t(now);
        char currentTimeStr[100];
        strftime(currentTimeStr, sizeof(currentTimeStr), "%Y-%m-%d %H:%M:%S", std::localtime(&currentTime));

        ofstream uptimeFileWrite(uptimeFileName);
        uptimeFileWrite << currentUptime << " " << currentTimeStr << std::endl;
        uptimeFileWrite.flush();
        uptimeFileWrite.close();
        lastUptime = currentUptime;
        lastTime = currentTimeStr;
        LOG_I(TAG, "Uptime and time written to file");
        LOG_I(TAG, "Current uptime: %lld, Current time: %s", currentUptime, currentTimeStr);
        this_thread::sleep_for(std::chrono::seconds(3));
    }
}
/* get_reboot_status function will read the uptime and time from the file and compare it with the current uptime
    If current uptime is less than the last uptime then device have rebooted
*/
support_dta::RebootStatus support_dta::get_reboot_status() {
    LOG_I(TAG, "get_reboot_status received");
    bool device_rebooted = false;
    string uptimeFileName = "/home/ubuntu/uptime.txt";
    ifstream uptimeFileRead(uptimeFileName);
    int64_t lastUptime;
    string lastTime;
    if (uptimeFileRead.is_open()) {
        string line;
        getline(uptimeFileRead, line);
        stringstream ss(line);
        ss >> lastUptime;
        getline(ss, lastTime);
        uptimeFileRead.close();
    }

    int64_t currentUptime = get_system_monotonic_time();
    if (currentUptime < lastUptime) {
        device_rebooted = true;
    }

    return std::make_tuple(device_rebooted, lastUptime, currentUptime, lastTime);
}


void support_dta::handle_command_to_run(json json_data, int tcpSocket) {
    const std::string cmd = json_data.at("command_to_run").get<std::string>();
    LOG_I(TAG,"Command to run: %s", cmd.c_str());
    std::string result = "";
    bool exit_status;
    if(system_execute_with_resp(TAG, cmd, result)) {
        LOG_I(TAG,"Command executed successfully");
        exit_status = true;
    } else {
        LOG_E(TAG,"Failed to execute command");
        result = "None";
        exit_status = false;
    }
    json result_json;
    result_json["result"] = result;
    result_json["exit_status"] = exit_status;

    // Convert it to a string
    std::string result_str = result_json.dump();
    LOG_I(TAG,"Result: %s", result_str.c_str());

    // Send the result
    int sendResult;
    int retryCount = 0;
    do {
        sendResult = send(tcpSocket, result_str.c_str(), result_str.length(), 0);
        if (sendResult < 0) {
            LOG_E(TAG,"Failed to send msg to framework. Retrying...");
            retryCount++;
            sleep(5);
        }
    } while(sendResult < 0 && retryCount < TCP_SEND_MAX_RETRY);
}


void support_dta::handle_track_reboot_or_shutdown(json json_data, int tcpSocket) {
    LOG_I(TAG,"track_reboot_or_shutdown command received");

    int timeout = json_data.at("track_reboot_or_shutdown").at("timeout").get<int>();
    auto result = track_reboot(timeout);
    bool device_rebooted = std::get<0>(result);
    int64_t lastUptime = std::get<1>(result);
    int64_t currentUptime = std::get<2>(result);
    std::string lastTime = std::get<3>(result);

    json result_json;
    result_json["device_rebooted"] = device_rebooted;
    result_json["last_uptime"] = lastUptime;
    result_json["current_uptime"] = currentUptime;
    result_json["last_time"] = lastTime;

    std::string result_str = result_json.dump();
    LOG_I(TAG,"Reboot msg: %s", result_str.c_str());
    // Send the result
    int sendResult;
    int retryCount = 0;
    do {
        sendResult = send(tcpSocket, result_str.c_str(), result_str.length(), 0);
        if (sendResult < 0) {
            LOG_E(TAG,"Failed to send msg to framework. Retrying...");
            retryCount++;
            sleep(5);
        }
    } while(sendResult < 0 && retryCount < TCP_SEND_MAX_RETRY);
}

void support_dta::handle_get_reboot_or_shutdown_status(json json_data, int tcpSocket) {
    LOG_I(TAG,"get_reboot_or_shutdown_status command received");
    auto result = get_reboot_status();
    bool device_rebooted = std::get<0>(result);
    int64_t lastUptime = std::get<1>(result);
    int64_t currentUptime = std::get<2>(result);
    std::string lastTime = std::get<3>(result);

    json result_json;
    result_json["device_rebooted"] = device_rebooted;
    result_json["last_uptime"] = lastUptime;
    result_json["current_uptime"] = currentUptime;
    result_json["last_time"] = lastTime;

    std::string result_str = result_json.dump();
    LOG_I(TAG,"Reboot msg: %s", result_str.c_str());
    // Send the result
    int sendResult;
    int retryCount = 0;
    do {
        sendResult = send(tcpSocket, result_str.c_str(), result_str.length(), 0);
        if (sendResult < 0) {
            LOG_E(TAG,"Failed to send msg to framework. Retrying...");
            retryCount++;
            sleep(5);
        }
    } while(sendResult < 0 && retryCount < TCP_SEND_MAX_RETRY);
}

void support_dta::handle_get_system_boot_uptime(json json_data, int tcpSocket) {
    LOG_I(TAG,"get_system_boot_uptime command received");
    int64_t boot_time = 0;
    bool status = read_boot_time(boot_time);
    if(status == false) {
        LOG_E(TAG, "Failed to get boot time");
    }
    LOG_I(TAG, "Boot time is %lld", boot_time);
    json result_json;
    result_json["boot_uptime"] = boot_time;

    std::string result_str = result_json.dump();
    LOG_I(TAG,"Result: %s", result_str.c_str());
    // Send the result
    int sendResult;
    int retryCount = 0;
    do {
        sendResult = send(tcpSocket, result_str.c_str(), result_str.length(), 0);
        if (sendResult < 0) {
            LOG_E(TAG,"Failed to send msg to framework. Retrying...");
            retryCount++;
            sleep(5);
        }
    } while(sendResult < 0 && retryCount < TCP_SEND_MAX_RETRY);
}

void support_dta::handle_LURejectCause(json json_data) {
    std::string error_number = json_data.at("LURejectCause").get<string>();
    int lu_reject_cause_int = atoi(error_number.c_str());
    LOG_I(TAG,"Received LURejectCause error code: %d", lu_reject_cause_int);
    conn_mgr_t msg;
    msg.sdk_error = lu_reject_cause_int;
    if(send_msg((generic_msg_t *)&msg, LU_REJECT_CAUSE_AUTOMATION, sizeof(msg), "CONN_MGR", "CONN_MGR", 0)) {
        LOG_I(TAG,"Message sent successfully");
    }
}

void support_dta::handle_sdk_error(json json_data) {
    std::string error_number = json_data.at("sdk_error").get<string>();
    int error_number_int = atoi(error_number.c_str());
    LOG_I(TAG,"Received SDK Error: %d", error_number_int);
    conn_mgr_t msg;
    msg.sdk_error = error_number_int;
    if(send_msg((generic_msg_t *)&msg, SDK_ERROR_AUTOMATION, sizeof(msg), "CONN_MGR", "CONN_MGR", 0)) {
        LOG_I(TAG,"Message sent successfully");
    }
}

void support_dta::handle_get_config(json json_data, int tcpSocket) {
    LOG_I(TAG,"get_config command received");
    json json_get_config = json_data.at("get_config");
    string value = "";
    if (json_get_config.is_null()) {
        LOG_E(TAG, "get_config is null");
        value = "Invalid JSON";
    }
    else{
        std::string config = json_get_config.at("config").get<std::string>();
        std::string section_name = json_get_config.at("section_name").get<std::string>();
        std::string param_name = json_get_config.at("param_name").get<std::string>();
        // std::string value_type = get_config.at("value_type").get<std::string>();
        std::string default_val = json_get_config.at("default_val").get<std::string>();

        bool get_override_config = json_get_config.value("get_override_config", true);
        bool is_value_overridden = !json_get_config.value("is_value_overridden", false);
        value = get_config(config, section_name, param_name, default_val, get_override_config, is_value_overridden);
    }

    // Create a new JSON object and add result_json to it with key "config_value"
    json send_json;
    send_json["config_value"] = value;

    std::string result_str = send_json.dump();
    LOG_I(TAG,"Result: %s", result_str.c_str());
    // Send the result
    int sendResult;
    int retryCount = 0;
    do {
        sendResult = send(tcpSocket, result_str.c_str(), result_str.length(), 0);
        if (sendResult < 0) {
            LOG_E(TAG,"Failed to send msg to framework. Retrying...");
            retryCount++;
            sleep(5);
        }
    } while(sendResult < 0 && retryCount < TCP_SEND_MAX_RETRY);
}

void support_dta::handle_dhub_recovery_flag(json json_data) {
    string dhub_recovery_flag_str = json_data.at("dhub_recovery_flag").get<string>();
    bool dhub_recovery_flag = (dhub_recovery_flag_str == "true") ? true : false;
    dhub_recovery_msg_t msg;
    msg.config_flag = dhub_recovery_flag;

    if (send_msg((generic_msg_t *)&msg, DHUB_RECOVERY, sizeof(msg), Q_NAME, "EXT_CAM", 0)) {
        LOG_I(TAG, "DHUB_RECOVERY message sent successfully");
    }
}

void support_dta::handle_button(json json_data){
    test_automation_msg_t msg;
    msg.button_no = 0;

    if(json_data.contains("alert")) {
        msg.internal_msg_type = TEST_USER_ALERT;}
    else if(json_data.contains("long_press")) {
        msg.internal_msg_type = TEST_LONG_PRESS;}
    else if(json_data.contains("installer_app")) {
        msg.internal_msg_type = TEST_LONG_PRESS_INST;}

    if(send_msg((generic_msg_t *)&msg, TEST_AUTOMATION, sizeof(msg), Q_NAME, "Q_SVC", 0)){
        LOG_I(TAG,"Alert message sent successfully");
    }
}

void support_dta::handle_system_reboot(int tcpSocket) {
    LOG_I(TAG,"Reboot command received");
    bool status = system_reboot();
    //If this function returns then device haven't rebooted
    std::string result_str = status ? "true" : "false";
    LOG_E(TAG,"Reboot status: %s", result_str.c_str());
    json result_json;
    result_json["reboot_status"] = result_str;

    std::string result = result_json.dump();
    LOG_I(TAG,"Result: %s", result.c_str());
    // Send the result
    int sendResult;
    int retryCount = 0;
    do {
        sendResult = send(tcpSocket, result.c_str(), result.length(), 0);
        if (sendResult < 0) {
            LOG_E(TAG,"Failed to send msg to framework. Retrying...");
            retryCount++;
            sleep(5);
        }
    } while(sendResult < 0 && retryCount < TCP_SEND_MAX_RETRY);   
}

void support_dta::get_udid() {
    for(int attempt = 0; attempt < 5; ++attempt) {
        if(file_is_present(time_sync_token_file)) {
            std::ifstream file(time_sync_token_file);
            if(file) {
                std::getline(file, udid);
                file.close();
            }
            if (!udid.empty()) {
                break;
            }
        }

        // Delay for 2^attempt seconds
        sleep(1 << attempt);
    }
    LOG_I(TAG,"UDID: %s", udid.c_str());

}
void support_dta::handle_get_udid(int tcpSocket) {
    if (udid.empty()) {
        get_udid();
    }
    json send_json;
    send_json["udid"] = udid;
    std::string result_str = send_json.dump();
    // Send the result
    int sendResult;
    int retryCount = 0;
    do {
        sendResult = send(tcpSocket, result_str.c_str(), result_str.length(), 0);
        if (sendResult < 0) {
            LOG_E(TAG,"Failed to send msg to framework. Retrying...");
            retryCount++;
            sleep(5);
        }
    } while(sendResult < 0 && retryCount < TCP_SEND_MAX_RETRY);
}

void support_dta::handle_reboot_request_to_PM() {

    if(send_powermon_to_reboot(Q_NAME, REQ_POWERMON_ANALYTICS_TO_REBOOT) == false) {
        LOG_I(TAG,"inside false of svc reboot");

    }
}

void support_dta::handle_to_start_dts(json json_data)
{
    int msg_idx = 0;
    LOG_I(TAG, "Received start_dts: %s", json_data.at("start_dts").get<string>().c_str());
    string start_dts_str = json_data.at("start_dts").get<string>();
    generic_msg_t msg;

    int64_t start_dts = std::stoll(start_dts_str);

    // Get current time in milliseconds from epoch
    int64_t current_time_ms = get_system_time();
    LOG_I(TAG, "current_time_ms: %lld", current_time_ms);

    // Calculate the delay required
    int64_t delay_ms = start_dts - current_time_ms;
    if (delay_ms > 0)
    {
        LOG_I(TAG, "Sleeping for %lld ms", delay_ms);
        // Sleep for the desired amount of time
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        if (false == send_msg(&msg, START_DTS_MSG, sizeof(msg), Q_NAME, "Q_DTS_IMU_MSG", msg_idx++) ||
            false == send_msg(&msg, START_DTS_MSG, sizeof(msg), Q_NAME, "Q_DTS_GPS_MSG", msg_idx++))
        {
            LOG_E(TAG, "Failed to send START_DTS_MSG to Q_DTS_IMU_MSG or Q_DTS_GPS_MSG");
        }
        else
        {
            LOG_I(TAG, "Successfully sent START_DTS_MSG to Q_DTS_IMU_MSG and Q_DTS_GPS_MSG");
        }
    }
}

void support_dta::receiveData(int tcpSocket){
    nd_server sock;
    char buffer[1024];
    int recvBytes;
    string receivedData;
    while (true) {
        recvBytes = sock.tcp_receive(tcpSocket, buffer, sizeof(buffer) - 1, 0);
        if (recvBytes > 0) {
            // Null-terminate the received data since we're treating it as text
            buffer[recvBytes] = '\0';
            receivedData.append(buffer, recvBytes);
            LOG_I(TAG,"Received data: %s", receivedData.c_str());

            try {
                json json_data = json::parse(receivedData);

                if(json_data.contains("LURejectCause")) {
                    handle_LURejectCause(json_data);
                }
                if(json_data.contains("sdk_error")) {
                    handle_sdk_error(json_data);
                }
                if(json_data.contains("alert") || json_data.contains("long_press") ||
                   json_data.contains("installer_app")) {
                    handle_button(json_data);
                }
                if(json_data.contains("dhub_recovery_flag")) {
                    handle_dhub_recovery_flag(json_data);
                }
                if(json_data.contains("command_to_run")) {
                    handle_command_to_run(json_data, tcpSocket);
                }
                if(json_data.contains("track_reboot_or_shutdown")) {
                    handle_track_reboot_or_shutdown(json_data, tcpSocket);
                }
                if(json_data.contains("get_reboot_or_shutdown_status")) {
                    handle_get_reboot_or_shutdown_status(json_data, tcpSocket);
                }
                if(json_data.contains("get_system_boot_uptime")) {
                    handle_get_system_boot_uptime(json_data, tcpSocket);
                }
                if(json_data.contains("get_config")) {
                    handle_get_config(json_data, tcpSocket);
                }
                if(json_data.contains("reboot")) {
                    handle_system_reboot(tcpSocket);
                    break;
                }
                if(json_data.contains("get_current_udid")) {
                    handle_get_udid(tcpSocket);
                }
                if(json_data.contains("send_reboot_request_to_PM")) {
                    handle_reboot_request_to_PM();
                }
                if (json_data.contains("start_dts")) {
                    handle_to_start_dts(json_data);
                }

            } catch(json::exception& e) {
                LOG_E(TAG, "Error parsing JSON: %s", e.what());
                continue; // skip to next iteration
            }

        }
        else if (recvBytes == 0) {
            // Client closed connection
            LOG_I(TAG,"Client closed connection");
            break;
        } else {
            // recv failed
            perror("recv");
            break;
        }
    }
    // Close the client socket
    close(tcpSocket);
}

