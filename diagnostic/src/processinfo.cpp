/* Copyright (C) 2024 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Debarpan Jana <debarpan.jana@netradyne.com>, September 2024
 */

#include <sstream>
#include <fstream>
#include <cstdio>
#include <cfloat>
#include <sys/sysinfo.h>
#include <cstdlib>
#include <proc/readproc.h>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <thread>
#include "service_utils.h"
#include <nd_factory.h>
#include "diag_resource_info_helper.h"
#include <unistd.h>
#include <sys/stat.h>
#include <linux/limits.h>
#include<system_utils.h>
#include<nd_file_utils.h>
#include <nd_utils.h>
#include "process_info.h"
#include "healthstatsdict.h"  

extern NDService *nd_service_obj;
extern ND_DeviceFactory *nd_device_obj;
static const std::string ND_DEVICE_REL_PATH = "/home/ubuntu/.nddevice";
static const std::string MEMORY_INFO_FILE_PATH = "/proc/meminfo";
#define TAG "PI"
#define SCALE_SIZE 1024.0
#define TOP_MEMORY_CONSUMING_PROCESSES 20
#define TOP_CPU_CONSUMING_PROCESSES 30
static const long CLOCK_TICKS = sysconf(_SC_CLK_TCK);
static const long PAGE_SIZE = sysconf(_SC_PAGESIZE);

int ProcessInfo::diagnosis(void *args)
{
    return 0;
}
void ProcessInfo::recover()
{
    return;
}

void ProcessInfo::notify()
{
    keep_alive = false;
    std::lock_guard<std::mutex> lk(pinfo_mutex);
    pinfo_cond.notify_one();
}

double ProcessInfo::calculate_cpu_percentage(int pid,const std::string &name, double utime, double stime)
{
    double val = 0;
    const std::string key = name + std::to_string(pid);
    auto now = std::chrono::steady_clock::now();
    if (last_pmap.find(key) != last_pmap.end())
    {
        double delta_time = (std::chrono::duration<double>(now - last_pmap[key].last_sys_cpu_times).count());
        double delta_proc = (utime - last_pmap[key].last_utime_s) + (stime - last_pmap[key].last_stime_s);
        cur_pmap[key].last_sys_cpu_times = now;
        cur_pmap[key].last_utime_s = utime;
        cur_pmap[key].last_stime_s = stime;
        if (delta_time == 0)
        {
            LOG_E(TAG, "Delta time is zero-----------------");
        }
        else
        {
            val = (delta_proc / delta_time) * 100;
            val = round_to_decimal(val, 1);
        }
    }
    else
    {
        cur_pmap[key].last_sys_cpu_times = now;
        cur_pmap[key].last_utime_s = utime;
        cur_pmap[key].last_stime_s = stime;
    }
    return val;
}

std::string ProcessInfo::get_exe(const std::string &pid)
{
    const std::string filepath = "/proc/" + pid + "/exe";
    char target_path[PATH_MAX];
    int len = readlink(filepath.c_str(), target_path, sizeof(target_path) - 1);
    std::string path = "";
    if (len != -1)
    {
        target_path[len] = '\0';
        path = std::string(target_path);
        auto pos = path.find('\0');
        if (pos != std::string::npos)
            path = path.substr(0, pos);
        const std::string suffix = " (deleted)";
        // Certain paths have ' (deleted)' appended. Usually this is 
        // bogus as the file actually exists. Even if it doesn't we don't care.
        if ((path.size() >= suffix.size()) &&
            (path.rfind(suffix) != std::string::npos))
        {
            // Check if the path does not exist
            if (!pathExists(path))
            {
                // Remove the suffix from the path
                path = path.substr(0, path.size() - suffix.size());
            }
        }
    }
    return path;
}

std::pair<uint64_t,uint64_t> ProcessInfo::get_pss(const std::string &pid)
{
    const std::string smapsrollupfilepath = "/proc/" + pid + "/smaps_rollup";
    const std::string pssPrefix = "Pss:";
    const std::string kbSuffix = "kB";
    const std::string swapPrefix = "SwapPss:";
    // sample output of /proc/<pid>/smaps_rollup
    // 5c75baa2d000-7ffd165f6000 ---p 00000000 00:00 0                          [rollup]
    // Rss:                6404 kB
    // Pss:                 726 kB
    // Pss_Dirty:           600 kB
    // Pss_Anon:            600 kB
    // Pss_File:            126 kB
    // Pss_Shmem:             0 kB
    // Shared_Clean:       5736 kB
    // SwapPss:              0 kB
    /*
    https://github.com/torvalds/linux/blob/master/fs/proc/task_mmu.c#L673-#L701
    https://github.com/torvalds/linux/blob/master/fs/proc/task_mmu.c#L805-#L850
    From the linux kernel code it is clear that SwapPss is not part of the normal Pss field
    */

    std::ifstream smapsrollupfile(smapsrollupfilepath);
    uint64_t total = 0;
    uint64_t swap_total = 0;
    if (!smapsrollupfile.is_open())
    {
        const std::string smapsfilepath = "/proc/" + pid + "/smaps";
        std::ifstream smapsfile(smapsfilepath);
        if (!smapsfile.is_open())
        {
            LOG_E(TAG, "Failed to open smaps file %s", smapsfilepath.c_str());
        }
        else
        {
            std::string line;
            while (std::getline(smapsfile, line))
            {
                if (nd::utils::StringUtils::StartsWith(line, pssPrefix) && nd::utils::StringUtils::EndsWith(line, kbSuffix))
                {
                    std::string numberStr = line.substr(pssPrefix.size(), line.size() - pssPrefix.size() - kbSuffix.size());
                    uint64_t pssValue = 0;
                    string_to_uint64(numberStr,pssValue);
                    total+=pssValue;
                }
                else if (nd::utils::StringUtils::StartsWith(line, swapPrefix) && nd::utils::StringUtils::EndsWith(line, kbSuffix))
                {
                    std::string numberStr = line.substr(swapPrefix.size(), line.size() - swapPrefix.size() - kbSuffix.size());
                    uint64_t swapValue = 0;
                    string_to_uint64(numberStr,swapValue);
                    swap_total+=swapValue;
                    total+=swapValue;
                }
            }
            smapsfile.close();
        }
    }
    else
    {
        std::string line;
        while (std::getline(smapsrollupfile, line))
        {
            if (nd::utils::StringUtils::StartsWith(line, pssPrefix) && nd::utils::StringUtils::EndsWith(line, kbSuffix))
            {

                std::string numberStr = line.substr(pssPrefix.size(), line.size() - pssPrefix.size() - kbSuffix.size());
                uint64_t pssValue = 0;
                string_to_uint64(numberStr,pssValue);
                total+=pssValue;
            }
            else if (nd::utils::StringUtils::StartsWith(line, swapPrefix) && nd::utils::StringUtils::EndsWith(line, kbSuffix))
            {
                std::string numberStr = line.substr(swapPrefix.size(), line.size() - swapPrefix.size() - kbSuffix.size());
                uint64_t swapValue = 0;
                string_to_uint64(numberStr,swapValue);
                swap_total+=swapValue;
                total+=swapValue;
                break;
            }
        }
        smapsrollupfile.close();
    }
    return std::make_pair(total,swap_total);
}

std::vector<ProcessInfo::PInfo> ProcessInfo::get_running_processes()
{
    std::vector<PInfo> desired_processes;
    PROCTAB *proc = openproc(PROC_FILLMEM | PROC_FILLCOM | PROC_FILLSTATUS | PROC_FILLSTAT | PROC_FILLARG);
    if (proc != nullptr)
    {
        proc_t proc_info;
        memset(&proc_info, 0, sizeof(proc_info));

        while (readproc(proc, &proc_info) != nullptr)
        {
            PInfo pinfo;
            pinfo.name = proc_info.cmd;
            pinfo.pid = proc_info.tid;
            pinfo.utime_s = proc_info.utime / (double)CLOCK_TICKS;
            pinfo.stime_s = proc_info.stime / (double)CLOCK_TICKS;
            pinfo.shared_memory = (double)(proc_info.share * PAGE_SIZE) / SCALE_SIZE; // in readproc it is in terms of # pages , so we have to multiply it by page size
            auto pss_value_pair = get_pss(std::to_string(pinfo.pid));
            pinfo.pss_memory = (double)pss_value_pair.first / SCALE_SIZE;
            pinfo.swap_pss_memory = (double)pss_value_pair.second / SCALE_SIZE;
            pinfo.cpu_percent = calculate_cpu_percentage(pinfo.pid, pinfo.name, pinfo.utime_s, pinfo.stime_s);
            pinfo.exe = get_exe(std::to_string(pinfo.pid));

            char **cmdline = proc_info.cmdline;
            if (cmdline != nullptr && cmdline[0] != nullptr)
            {
                std::string extended_path = get_file_name(cmdline[0]);
                if (nd::utils::StringUtils::StartsWith(extended_path, pinfo.name))
                {
                    pinfo.name = extended_path;
                }
                // On UNIX the name gets truncated to the first 15 characters.
                // If it matches the first part of the cmdline we return that
                // ne instead because it's usually more explicative.
                // Examples are "gnome-keyring-d" vs. "gnome-keyring-daemon".
            }
            else
            {
                continue; // may happen in case of zombie processes
            }

            desired_processes.push_back(pinfo);
        }
        last_pmap = std::move(cur_pmap);
        closeproc(proc);
    }
    else
    {
        LOG_E(TAG, "openproc returned nullptr");
    }
    return desired_processes;
}

void ProcessInfo::send_process_info_to_healthstats()
{
    LOG_I(TAG, "-----entering send_process_info_to_healthstats----");

    json_t *root = json_object();

    json_t *array = json_array();
    for (auto &values : process_data)
    {
        std::string key = values.first;
        auto value = values.second;
        auto final_process_info = CreateMeanSdValue(value);

        json_t *temp = convert_map_to_json(final_process_info);
        json_t *cpu_affinity_array = convert_unordered_set_to_json_array(process_cpu_affinity[key]);

        if (nullptr != cpu_affinity_array) {
            json_object_set_new(temp, "cpu_affinity", cpu_affinity_array);
        } else {
            LOG_E(TAG, "json_array() has returned a NULL pointer for process : %s", key.c_str());
        }
        json_object_set_new(temp, "process_name", json_string(key.c_str()));
        json_array_append_new(array, temp);
    }
    json_object_set_new(root, "health_info:process_info", array);
    json_object_set_new(root, std::string("isArray").c_str(), json_string(std::string("true").c_str()));
    if(root)
    {
        char *jsonString = json_dumps(root,JSON_REAL_PRECISION(3));
        if(jsonString)
        {
            LOG_I(TAG, "Message Size %d", strlen(jsonString));
            nd_service_obj->send_msg_healthstats_long(jsonString, strlen(jsonString));
            free(jsonString);
        }
        json_decref(root);
    }
    LOG_I(TAG, "----exiting send_process_info_to_healthstats----");
    process_data.clear();
    process_cpu_affinity = std::unordered_map<std::string, std::unordered_set<uint8_t>>();
}

std::string getProcessNameByPID(const std::string& pid) {
    std::string comm_file_path = "/proc/" + pid + "/comm";
    std::ifstream comm_file(comm_file_path);
    std::string process_name;

    if (comm_file.is_open()) {
        if(!std::getline(comm_file, process_name)){ // Read the process name from /proc/[pid]/comm
            LOG_E("Failed to read from comm file for PID: %s", pid.c_str());
            process_name = "unknown";
        }
        comm_file.close();
    } else {
        LOG_E("Failed to open comm file for PID: %s", pid.c_str());
        process_name = "unknown";  // Fallback if process name can't be determined
    }

    return process_name;
}

ProcessInfo::PInfo* ProcessInfo::get_process_by_name(const std::string& process_name, const std::vector<ProcessInfo::PInfo>& process_list) {
    auto it = std::find_if(process_list.begin(), process_list.end(),
                           [&process_name](const PInfo& proc) { return proc.name == process_name; });
    if (it != process_list.end()) {
        return new PInfo(*it);  // Return a copy of the found process
    }
    return nullptr;  // Return null if process not found
}

void ProcessInfo::add_hw_memory(std::vector<PInfo> &list_of_process,std::vector<PInfo> &process_list)
{
    std::unordered_map<std::string, double> hw_memory_list;
    LOG_I(TAG,"Entered add hardware memory .... ");
    #ifdef BAGHEERA2
        std::ifstream file(HEALTH_STATS_DICT.at("process_memory_hw"));
        if (!file.is_open())
        {
            LOG_E(TAG, "File not found %s", HEALTH_STATS_DICT.at("process_memory_hw").c_str());
            return;
        }
        std::string line;
            //sample file for process_memory_hw
            // CLIENT                        PROCESS      PID         PSS        SIZE
            // user                   nvargus-daemon     4678          0K         76K
            // total                                                  64K         76K
        while (getline(file, line))
        {
            if (line.find("PROCESS") != std::string::npos)
            {
                continue;
            }
            std::vector<std::string> temp;
            std::string word;
            for (char ch : line) {
                if (isspace(ch)) {
                    if (!word.empty()) {   //skip multiple spaces
                        temp.push_back(word);
                        word.clear();
                    }
                } else {
                    word += ch;
                }
            }
            // Add the last word if there's any
            if (!word.empty()) {
                temp.push_back(word);
            }
            if (temp.size() > 4)
            {
                if (temp[1] == "analyticsServic")
                {
                    temp[1] = "analyticsService";
                }
                int64_t temporary_val =0;
                string_to_int64(temp[4].substr(0, temp[4].length() - 1),temporary_val);
                hw_memory_list[temp[1]] = temporary_val;
            }
        }
        file.close();
    #endif
    #ifdef KRAIT
        std:ifstream ion_heap_file(HEALTH_STATS_DICT.at("ion_heap_file"));
        if (!ion_heap_file.is_open())
        {
            LOG_E(TAG, "File not found %s", HEALTH_STATS_DICT.at("ion_heap_file").c_str());
            return;
        }
        try{
            std::string line;
            int count = 0;
            std::map<std::string, int> heap_allocations;
            std::map<std::string, std::string> service_pid;
            try {
                while (std::getline(ion_heap_file, line)) {
                    if (line.find("----") == 0) {
                        count++;
                        continue;
                    }
                    if (line.find("client") != std::string::npos || line.find("orphaned allocations") != std::string::npos || count == 2) {
                        continue;
                    }

                    std::stringstream ss(line);
                    std::string client, pid, size;
                    ss >> client >> pid >> size;
                    if (line.find("total orphaned") != std::string::npos) {
                        heap_allocations["total orphaned"] = std::stoi(size);
                        break;
                    } else {
                        if (heap_allocations.find(client) == heap_allocations.end()) {
                            heap_allocations[client] = 0;
                        }
                        heap_allocations[client] += std::stoi(size);
                    }

                    if (service_pid.find(pid) == service_pid.end()) {
                        service_pid[pid] = client;
                    }
                }
            } catch (const std::exception& e) {
                LOG_E(TAG,"Exception while reading ion heap file:  %s", e.what());
            }
            // Add heap allocations to hw_memory_list
            for (const auto& allocation : heap_allocations) {
                hw_memory_list[allocation.first] = static_cast<float>(allocation.second) / 1024;
            }
            std::string kgsl_path = (HEALTH_STATS_DICT.at("kgsl_proc_path"));
            // Process kgsl services
            vector<string> directories;
            get_directories(kgsl_path,directories);
            for (auto pid : directories) {
                std::string kgsl_file_path = kgsl_path+ "/" + pid + "/kernel";
                std::ifstream kgsl_file(kgsl_file_path);
                if (!kgsl_file.is_open())
                {
                    LOG_E(TAG, "File not found %s", kgsl_file_path.c_str());
                    return;
                }
                std::string size_str;
                std::getline(kgsl_file, size_str);
                int size;
                bool status = string_to_integer(size_str,size);
                if(!status)
                    size = 0;
                else{
                    try {
                        if (service_pid.find(pid) != service_pid.end()) {
                            hw_memory_list[service_pid[pid]] += static_cast<float>(size) / 1024;
                        }else {
                            // Get the process name by reading from /proc/[pid]/comm
                            std::string process_name = getProcessNameByPID(pid);
                            if (hw_memory_list.find(process_name) == hw_memory_list.end()) {
                                hw_memory_list[process_name] = 0;
                            }
                            hw_memory_list[process_name] += static_cast<float>(size) / 1024;
                        }
                    } catch (const std::exception& e) {
                        LOG_E(TAG,"An error occurred while getting PID for kgsl: %s", e.what());
                    }
                }
            }

            // Keep only the top 20 hardware memory consuming processes
            if (hw_memory_list.size() > 20) {
                std::vector<std::pair<std::string, float>> sorted_memory_list(hw_memory_list.begin(), hw_memory_list.end());
                std::sort(sorted_memory_list.begin(), sorted_memory_list.end(),
                    [](const std::pair<std::string, float>& a, const std::pair<std::string, float>& b) {
                        return a.second > b.second;
                    });

                hw_memory_list.clear();
                for (size_t i = 0; i < 20 && i < sorted_memory_list.size(); i++) {
                    hw_memory_list[sorted_memory_list[i].first] = sorted_memory_list[i].second;
                }
            }
        }catch(const std::exception& e){
            LOG_E(TAG,"An error occurred while getting hardware memory for krait :%s", e.what());
        }
    #endif
    for (auto &p : list_of_process)
    {
        std::string process_name = p.name;

        if (hw_memory_list.find(process_name) != hw_memory_list.end())
        {
            p.mem_hw = hw_memory_list[process_name] / 1024;
            p.mem_sw = p.pss_memory;
            p.pss_memory += (hw_memory_list[process_name] / 1024);

            hw_memory_list.erase(process_name);

            if (hw_memory_list.size() == 0)
            {
                break;
            }
                
        }
    }
    std::vector<PInfo> hw_process_list;

    if (!hw_memory_list.empty()) {
        for (auto it = hw_memory_list.begin(); it != hw_memory_list.end(); ++it) {
            const std::string& key = it->first;
            float mem_hw = it->second;

            PInfo* hw_process = get_process_by_name(key, process_list);
            if (hw_process == nullptr) {
                // If process not found, create a new entry
                PInfo new_process;
                new_process.cpu_percent = 0.0;
                new_process.name = key;
                new_process.exe = key;
                new_process.mem_hw = mem_hw / 1024.0;  // Convert to KB
                new_process.mem_sw = 0.0;
                new_process.pss_memory = new_process.mem_hw + new_process.mem_sw;
                hw_process_list.push_back(new_process);
            } else {
                // If process found, update its information
                hw_process->mem_hw = mem_hw / 1024.0;  // Update mem_hw
                hw_process->mem_sw = hw_process->pss_memory;  // mem_sw is stored in pss_memory
                hw_process->pss_memory = hw_process->pss_memory + hw_process->mem_hw;  // Add mem_hw to pss_memory
                hw_process_list.push_back(*hw_process);
            }

            if (hw_process != nullptr) {
                delete hw_process;  // Cleanup after use
            }
        }
    }

    // Append hw_process_list to list_of_process
    list_of_process.insert(list_of_process.end(), hw_process_list.begin(), hw_process_list.end());
}

bool ProcessInfo::compare_by_pss_memory(const PInfo &a, const PInfo &b)
{
    return a.pss_memory > b.pss_memory;
}

bool ProcessInfo::compare_by_cpu_percent(const PInfo &a, const PInfo &b)
{
    return a.cpu_percent > b.cpu_percent;
}

std::vector<ProcessInfo::PInfo> ProcessInfo::get_desired_process_info()
{
    std::vector<PInfo> desired_process;
    int count = 0;
    auto temp = get_running_processes();
    auto mem_sort_count = std::min<size_t>(TOP_MEMORY_CONSUMING_PROCESSES, temp.size());
    std::partial_sort(temp.begin(), temp.begin() + mem_sort_count, temp.end(), compare_by_pss_memory);

    for (auto &p : temp)
    {
        if (p.exe.length() == 0)
        {
            p.exe = p.name;
        }
        if (count < mem_sort_count || p.exe.find(ND_DEVICE_REL_PATH) != std::string::npos)
        {
            desired_process.push_back(p);
            p.pid = -1; // to avoid sending the same data again
        }
        else if (p.exe.find("nvargus-daemon") != std::string::npos || p.exe.find("nvcamera-daemon") != std::string::npos)
        {
            desired_process.push_back(p);
            p.pid = -1; // to avoid sending the same data again
        }
        count++;
    }
    // bagheera hardware memory
    // #ifdef BAGHEERA2
    add_hw_memory(desired_process,temp);
    // #endif
    count = 0;
    auto cpu_sort_count = std::min<size_t>(TOP_CPU_CONSUMING_PROCESSES, temp.size());
    std::partial_sort(temp.begin(), temp.begin() + cpu_sort_count, temp.end(), compare_by_cpu_percent);
    for (auto &p : temp)
    {
        if (p.pid != -1 && count < cpu_sort_count)
        {
            desired_process.push_back(p);
        }  
        count++;
        if (count >= cpu_sort_count)
        {
            break;
        }
    }
    return desired_process;
}

void ProcessInfo::get_process_info()
{
    auto list_of_process = get_desired_process_info();
    for (auto &value : list_of_process)
    {
        process_data[value.name]["cpu_usage"].push_back(value.cpu_percent);
        process_data[value.name]["mem_usage"].push_back((value.pss_memory / ram_size) * 100);
        process_data[value.name]["swap_mem_usage"].push_back(value.swap_pss_memory); //raw values in MB
        process_data[value.name]["shared_mem"].push_back(value.shared_memory);
        std::vector<uint8_t> temp = get_cpu_affinity(value.pid, nd_device_obj->expected_cores_online());
        process_cpu_affinity[value.name].insert(temp.begin(), temp.end());

        if (value.mem_hw >= 0 && value.mem_sw >= 0)
        {
            process_data[value.name]["mem_hw"].push_back(value.mem_hw / ram_size * 100);
            process_data[value.name]["mem_sw"].push_back(value.mem_sw / ram_size * 100);
        }
    }
}

int ProcessInfo::get_ram_Info()
{
    int value = -1;
    //sample output of /proc/meminfo
    // MemTotal:       16021852 kB
    // MemFree:          826892 kB
    // MemAvailable:    9218204 kB
    // Buffers:          134676 kB
    // Cached:          9108256 kB
    // SwapCached:           60 kB
    std::ifstream meminfo(MEMORY_INFO_FILE_PATH);
    if (!meminfo.is_open())
    {
        LOG_E(TAG, "Failed to open meminfo file");
    }
    else
    {
        std::string line;
        while (std::getline(meminfo, line))
        {
            if (line.find("MemTotal:") != std::string::npos)
            {
                size_t pos = line.find(' ');
                std::string key = line.substr(0, pos); // Extract key
                // Find the position of the second space (to extract the integer value)
                size_t valueStart = line.find_first_not_of(" ", pos);
                size_t valueEnd = line.find(' ', valueStart);
                string_to_integer(line.substr(valueStart, valueEnd - valueStart), value);
                break;
            }
        }
        meminfo.close();
    }
    return value;
}

void ProcessInfo::set_ram_size()
{
    ram_size = get_ram_Info();
    if (ram_size == -1)
    {
       std::string RAM_SIZE = HEALTH_STATS_DICT.at("ram_size");
       string_to_integer(RAM_SIZE,ram_size);

    }
    else
    {
        ram_size /= SCALE_SIZE;
    }
    LOG_I(TAG, "ram size is set to %d", ram_size);
}
void ProcessInfo::execute_thread()
{
    LOG_I(TAG, "===========starting processinfo thread=================");
    set_ram_size();     
    int32_t freq = processinfo_time;
    std::chrono::steady_clock::time_point time_before_calling, time_after_calling;
    int32_t num_of_sec = 0;
    while (keep_alive)
    {
        time_before_calling = std::chrono::steady_clock::now();
        get_process_info();

        num_of_sec += freq;
        if (num_of_sec >= 60)
        {
            send_process_info_to_healthstats();
            num_of_sec = 0;
        }

        time_after_calling = std::chrono::steady_clock::now();
        auto difference = std::chrono::duration<double>(time_after_calling - time_before_calling);
        double diff = difference.count();
        double remaining_time = ((double)freq - diff);
        if (remaining_time > 0)
        {
            std::unique_lock<std::mutex> lk(pinfo_mutex);
            LOG_I(TAG, "sleeping on pinfo_cond ");
            if (pinfo_cond.wait_for(lk,std::chrono::duration<double>(remaining_time)) == std::cv_status::timeout)
            {
                LOG_I(TAG, " pinfo_cond.wait_for is timed out");
            }
            else
            {
                LOG_I(TAG, " pinfo_cond.wait_for is notified");
            }
        }
    }
}
