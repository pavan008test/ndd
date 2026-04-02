/* Copyright (C) 2024 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Debarpan Jana <debarpan.jana@netradyne.com>, September 2024
 */

#include <sstream>
#include <fstream>
#include <vector>
#include <utility>
#include <cstdio>
#include <cmath>
#include <cfloat>
#include <sys/sysinfo.h>
#include <chrono>
#include <jansson.h>
#include <sys/time.h>
#include "service_utils.h"
#include <nd_factory.h>
#include <log.h>
#include<system_utils.h>
#include<nd_file_utils.h>
#include<nd_utils.h>
#include "diag_resource_info_helper.h"
#include "cpugpu_info.h"
#include "healthstatsdict.h"   

extern NDService *nd_service_obj;

#define TAG "CGI"
#define SCALE_SIZE 1024.0
#define RESET_MASTER_DATA_TIME_LOCAL 15 //in minutes
static const std::string MEMORY_INFO_FILE_PATH = "/proc/meminfo";
static constexpr int CONVERT_TO_PERCENT = 100;
extern bool reset_master_data; // flag to reset the master data


void CpuGpuInfo::notify()
{
    keep_alive = false;
    std::lock_guard<std::mutex> lk(cpugpuinfo_mutex);
    cpugpuinfo_cond.notify_one();
}

int CpuGpuInfo::diagnosis(void *args)
{
    return 0;
} 

void CpuGpuInfo::recover()
{
    return;
}

void CpuGpuInfo::update_cpu_info(std::unordered_map<int, std::unordered_map<std::string, std::vector<double>>> &src, std::unordered_map<int, std::unordered_map<std::string, std::vector<double>>> &dest)
{
    for (auto &item : src)
    {
        int key = item.first;
        auto val = item.second;
        for (auto &v : val)
        {
            dest[key][v.first] = v.second;
        }
    }
}

void CpuGpuInfo::update_other_info(std::unordered_map<std::string, std::vector<double>> &src, std::unordered_map<std::string, std::vector<double>> &dest)
{
    for (auto &item : src)
    {
        dest[item.first] = item.second;
    }
}

void CpuGpuInfo::send_master_cpu_info(std::unordered_map<std::string, std::vector<std::unordered_map<std::string, double>>> &masterdata)
{
  std::vector<std::unordered_map<std::string, double>> final_cpu_info;
    for (auto &values : master_cpu_info_data)
    {
        int cpu_no = values.first;
        auto value = values.second;
        auto cpu_info = CreateMeanSdMinMaxValue(value);
        cpu_info["cpu_no"] = cpu_no;
        final_cpu_info.push_back(cpu_info);
    }
    masterdata["health_info:master_cpu_info"] = final_cpu_info;
     // Dump Json
    json_t *root = json_object();
    char *jsonString;

    json_object_set_new(root, "session", json_string("master_cpu_info"));
    json_object_set_new(root, string_to_const_char("health_info:master_cpu_info"), convert_vector_to_json(masterdata["health_info:master_cpu_info"]));
    if (root)
    {
        jsonString = json_dumps(root, JSON_REAL_PRECISION(3));
        if (jsonString)
        {
            nd_service_obj->send_msg_healthstats(jsonString, strlen(jsonString));
            free(jsonString);
        }
        json_decref(root);
    }
}

void CpuGpuInfo::send_master_gpu_info(std::unordered_map<std::string, std::vector<std::unordered_map<std::string, double>>> &masterdata)
{
    std::vector<std::unordered_map<std::string, double>> temp;
    auto final_gpu_info = CreateMeanSdMinMaxValue(master_gpu_info_data);
    temp.push_back(final_gpu_info);
    masterdata["health_info:master_gpu_info"] = temp;
    temp.pop_back();
    json_t* root = json_object();
    json_object_set_new(root, "session", json_string("master_gpu_info"));
    json_object_set_new(root, string_to_const_char("health_info:master_gpu_info"), convert_vector_to_json(masterdata["health_info:master_gpu_info"]));
    if (root)
    {
        char* jsonString = json_dumps(root, JSON_REAL_PRECISION(3));
        if (jsonString)
        {
            nd_service_obj->send_msg_healthstats(jsonString, strlen(jsonString));
            free(jsonString);
        }
        json_decref(root);
    }
}

void CpuGpuInfo::send_master_free_info(std::unordered_map<std::string, std::vector<std::unordered_map<std::string, double>>> &masterdata)
{
    std::vector<std::unordered_map<std::string, double>> temp;
    auto final_free_info = CreateMeanSdValue(master_free_info_data);
    temp.push_back(final_free_info);
    masterdata["health_info:master_free_info"] = temp;
    temp.pop_back();
    json_t* root = json_object();
    json_object_set_new(root, "session", json_string("master_free_info"));
    json_object_set_new(root, string_to_const_char("health_info:master_free_info"), convert_vector_to_json(masterdata["health_info:master_free_info"]));
    if (root)
    {
        char* jsonString = json_dumps(root, JSON_REAL_PRECISION(3));
        if (jsonString)
        {
            nd_service_obj->send_msg_healthstats(jsonString, strlen(jsonString));
            free(jsonString);
        }
        json_decref(root);
    }
}
void CpuGpuInfo::send_master_info()
{
    LOG_I(TAG, "----entering SendCpuGpuMasterData----");
    if (reset_master_data)
    {
        // update
        update_cpu_info(cpu_info_data, master_cpu_info_data);
        update_other_info(gpu_info_data, master_gpu_info_data);
        update_other_info(free_info_data, master_free_info_data);
        reset_master_data = false;
        reset_master_data_counter_local = 0;
    }
    if(reset_master_data_counter_local >= RESET_MASTER_DATA_TIME_LOCAL)
    {
        update_cpu_info(cpu_info_data, master_cpu_info_data);
        update_other_info(gpu_info_data, master_gpu_info_data);
        update_other_info(free_info_data, master_free_info_data);
        reset_master_data_counter_local = 0;
        LOG_E(TAG, "Reset Master Data flag is not set, resetting master data in the 15th minute!!!! HS working???");
    }
    std::unordered_map<std::string, std::vector<std::unordered_map<std::string, double>>> masterdata;
    send_master_cpu_info(masterdata);
    send_master_gpu_info(masterdata);
    send_master_free_info(masterdata);
    reset_master_data_counter_local++;
    LOG_I(TAG, "----exiting SendCpuGpuMasterData----");
}

void CpuGpuInfo::send_cpu_info_to_healthstats()
{
     for (auto &values : cpu_info_data)
    {
        int cpu_no = values.first;
        auto value = values.second;
        auto cpu_info = CreateMeanSdMinMaxValue(value);
        cpu_info["cpu_no"] = cpu_no;
        std::unordered_map<std::string, std::unordered_map<std::string, double>> final_cpu_info;
        final_cpu_info["health_info:cpu_info"] = cpu_info;
        // Json dump, mq send

        // Dump Json
        json_t *root = json_object();

        // Iterate over the masterdata and add each element to JSON object
        for (const auto &entry : final_cpu_info)
        {
            json_object_set_new(root, string_to_const_char(entry.first), convert_map_to_json(entry.second));
        }

        json_object_set_new(root, string_to_const_char("isArray"), json_string(string_to_const_char("true")));
        // Convert JSON object to string
        if (root)
        {
            char *jsonString = json_dumps(root, JSON_REAL_PRECISION(3));
            if (jsonString)
            {
                nd_service_obj->send_msg_healthstats(jsonString, strlen(jsonString));
                free(jsonString);
            }
            json_decref(root);
        }
    }
}

void CpuGpuInfo::send_gpu_info_to_healthstats()
{
    auto gpu_info = CreateMeanSdMinMaxValue(gpu_info_data, true, GPU_USAGE_THRESHOLD);
    std::unordered_map<std::string, std::unordered_map<std::string, double>> final_gpu_info;
    final_gpu_info["health_info:gpu_info"] = gpu_info;
    // Json dump, mq send

    // Dump Json
    json_t *root = json_object();

    // Iterate over the masterdata and add each element to JSON object
    for (const auto &entry : final_gpu_info)
    {
        json_object_set_new(root, string_to_const_char(entry.first), convert_map_to_json(entry.second));
    }

    json_object_set_new(root, string_to_const_char("isArray"), json_string(string_to_const_char("true")));
    // Convert JSON object to string
    if(root)
    {
        char *jsonString = json_dumps(root, JSON_REAL_PRECISION(3));
        if(jsonString)
        {
            nd_service_obj->send_msg_healthstats(jsonString, strlen(jsonString));
            
            free(jsonString);
        }
        json_decref(root);
    }
}

void CpuGpuInfo::send_free_info_to_healthstats()
{
    auto free_info = CreateMeanSdValue(free_info_data);
    std::unordered_map<std::string, std::unordered_map<std::string, double>> final_free_info;
    final_free_info["health_info:free_info"] = free_info;
    //  Json dump, mq send

    json_t* root = json_object();

    // Iterate over the masterdata and add each element to JSON object
    for (const auto &entry : final_free_info)
    {
        json_object_set_new(root, string_to_const_char(entry.first), convert_map_to_json(entry.second));
    }

    json_object_set_new(root, string_to_const_char("isArray"), json_string(string_to_const_char("true")));
    // Convert JSON object to string
    if (root)
    {
        char *jsonString = json_dumps(root, JSON_REAL_PRECISION(3));
        if(jsonString)
        {
            nd_service_obj->send_msg_healthstats(jsonString, strlen(jsonString));
            
            free(jsonString);
        }
        json_decref(root);
    }
}

//sample output of send_cpu_info_to_healthstats
//{'freq_mean': 2040.0, 'usage_mean': 47.4, 'temp_mean': 58.8, 'cpu_no': 1, 'timestamp': 1728024142266, 'temp_sd': 0.373, 'freq_sd': 0.0, 'usage_sd': 0.002}
//{'free_sd': 29000.0, 'free_mean': 122000.0, 'available_sd': 24700.0, 'shared_sd': 1.11, 'swap_used_mean': 595000.0, 'buff_cache_sd': 14400.0, 'timestamp': 1}
//{'freq_sd': 0.0, 'temp_sd': 0.361, 'timestamp': 1728024142283, 'freq_mean': 1120.0, 'temp_mean': 65.1}

void CpuGpuInfo::send_cpu_gpu_free_info_to_healthstats()
{
    LOG_I(TAG, "-----entering send_cpu_gpu_free_info_to_healthstats----");
    send_cpu_info_to_healthstats();
    send_gpu_info_to_healthstats();
    send_free_info_to_healthstats();
    send_master_info();
    LOG_I(TAG, "----exiting send_cpu_gpu_free_info_to_healthstats----");
    cpu_info_data.clear();
    gpu_info_data.clear();
    free_info_data.clear();
}

void CpuGpuInfo::get_swap_info()
{
    struct sysinfo si;
    if (sysinfo(&si) != 0)
    {
        LOG_E(TAG, "=========Error while getting swap memory=========");
        return;
    }

    unsigned long total = si.totalswap * si.mem_unit;
    unsigned long free = si.freeswap * si.mem_unit;
    double used = (double)(total - free) / SCALE_SIZE;
    free_info_data["swap_used"].push_back(used);
    master_free_info_data["swap_used"].push_back(used);
    free_info_data["swap_total"].push_back(((double)total) / SCALE_SIZE);
    master_free_info_data["swap_total"].push_back(((double)total) / SCALE_SIZE);
}

void CpuGpuInfo::get_free_info()
{
    std::unordered_map<std::string, uint64_t> memory_info_map;
    std::ifstream meminfo(MEMORY_INFO_FILE_PATH);
    if (!meminfo.is_open())
    {
        LOG_E(TAG, "=========Error while getting free memory=========");
        return;
    }
    //sample output of /proc/meminfo
    // MemTotal:       16021852 kB
    // MemFree:          826892 kB
    // MemAvailable:    9218204 kB
    // Buffers:          134676 kB
    // Cached:          9108256 kB
    // SwapCached:           60 kB
    std::string line;
    while (std::getline(meminfo, line))
    {
        // Find the position of the first space (to separate key from value)
        size_t pos = line.find(' ');
        std::string key = line.substr(0, pos); // Extract key
        if(key =="MemFree:" || key == "Shmem:" || key == "MemAvailable:" || key == "Buffers:" || key == "Cached:" || key == "KernelStack:" || key == "PageTables:" || key == "Slab:")
        {
            // Find the position of the second space (to extract the integer value)
            size_t valueStart = line.find_first_not_of(" ", pos);
            size_t valueEnd = line.find(' ', valueStart);
            uint64_t value = 0;
            string_to_uint64(line.substr(valueStart, valueEnd - valueStart),value);
            memory_info_map[key] = value; // Store in the map
        }
        
    }
    meminfo.close();
    uint64_t free = memory_info_map["MemFree:"];
    free_info_data["free"].push_back((double)free);
    master_free_info_data["free"].push_back((double)free);

    uint64_t shared = memory_info_map["Shmem:"];
    free_info_data["shared"].push_back((double)shared);
    master_free_info_data["shared"].push_back((double)shared);

    uint64_t available = memory_info_map["MemAvailable:"];

    free_info_data["available"].push_back((double)available);
    master_free_info_data["available"].push_back((double)available);

    uint64_t buffers = memory_info_map["Buffers:"];
    uint64_t cached = memory_info_map["Cached:"];
    uint64_t buff_cache = buffers + cached;
    free_info_data["buff_cache"].push_back((double)buff_cache);
    master_free_info_data["buff_cache"].push_back((double)buff_cache);
    uint64_t kernel_stack = memory_info_map["KernelStack:"];
    uint64_t page_tables = memory_info_map["PageTables:"];
    uint64_t slab = memory_info_map["Slab:"];
    uint64_t kernel_memory = kernel_stack + page_tables + slab;
    free_info_data["kernel_memory"].push_back(kernel_memory);
    master_free_info_data["kernel_memory"].push_back(kernel_memory);
}

void CpuGpuInfo::get_cpu_info()
{
    try {
        int cpu_core = -1;
        int cpu_temp_val = -1;
        std::string cpu_temp = "";
        auto cpu_temp_vector = read_lines_from_file(HEALTH_STATS_DICT.at("cpuTemp"));
        if(!cpu_temp_vector.empty())
        {
            cpu_temp = cpu_temp_vector[0];
            if (!cpu_temp.empty())
            {
                string_to_integer(cpu_temp,cpu_temp_val);
                ps_obj.setTemperatureThrottlingState(cpu_temp_val);
            }
        }
        else
        {
            cpu_temp_val = -99;
        }
        std::vector<std::string> cpu_usage_list = read_lines_from_file(HEALTH_STATS_DICT.at("cpu_usage"));
        // value got from above file
        // cpu  403250 0 312975 936531 10005 15043 6187 0 0 0
        // cpu0 99562 0 80078 224855 3285 10019 2201 0 0 0
        // cpu3 99771 0 78124 237301 2264 1668 1320 0 0 0
        // cpu4 99727 0 77004 238262 2189 1672 1317 0 0 0
        // cpu5 104189 0 77768 232848 2265 1683 1348 0 0 0

        // user: normal processes executing in user mode
        // nice: niced processes executing in user mode
        // system: processes executing in kernel mode
        // idle: twiddling thumbs
        // iowait: waiting for I/O to complete
        // irq: servicing interrupts
        // softirq: servicing softirqs

        for (size_t list_itr = 1; list_itr < cpu_usage_list.size(); list_itr++)
        {
            if (nd::utils::StringUtils::StartsWith(cpu_usage_list[list_itr], "cpu"))
            {
                cpu_core++;
                std::vector<string> values = split_by_delim(cpu_usage_list[list_itr]," ");
                std::string cpu_name;
                if(values.empty())
                {
                    LOG_E(TAG,"Cpu usage file line is empty!!!!");
                    continue;
                }
                cpu_name = values[0];
                uint64_t total = 0;
                for(size_t nested_list_itr =1; nested_list_itr <values.size(); nested_list_itr++)
                {
                    int64_t temporary_value = 0;
                    string_to_int64(values[nested_list_itr],temporary_value);
                    if(temporary_value > 0)
                    {
                        total += temporary_value;
                    }
                }
                if (values.size() >= 9)
                {
                    uint64_t idle = 0;
                    string_to_uint64(values[4],idle);
                    uint64_t usage = total - idle;
                    double cpu_usage = 0.0;
                    if(total != 0)
                    {
                        cpu_usage = ((double)usage / (double)total) * 100;
                    }
                    else
                    {
                        LOG_E(TAG,"Total cpu usage is 0!!!!");
                    }
                    cpu_info_data[cpu_core]["usage"].push_back(cpu_usage);
                    master_cpu_info_data[cpu_core]["usage"].push_back(cpu_usage);

                    // Calculating usage within the previous interval (cpu_usage_v2)
                    uint64_t total_diff = 0;
                    uint64_t idle_diff = 0;
                    auto prev_time_it = prev_cpu_total_idle_time.find(cpu_core);
                    if(prev_time_it == prev_cpu_total_idle_time.end()) {
                        total_diff = total;
                        idle_diff = idle;
                    } else {
                        const auto &prev_cpu_total_idle_time_pair = prev_time_it->second;
                        total_diff = total - prev_cpu_total_idle_time_pair.first;
                        idle_diff = idle - prev_cpu_total_idle_time_pair.second;
                    }
                    double cpu_usage_v2 = 0.0;
                    if (total_diff != 0) {
                        cpu_usage_v2 = ((double)(total_diff - idle_diff) / (double)total_diff) * CONVERT_TO_PERCENT;
                    } else {
                        LOG_E(TAG, "Total diff is 0! Not calculating cpu_usage_v2");
                    }
                    cpu_info_data[cpu_core][USAGE_V2].push_back(cpu_usage_v2);
                    master_cpu_info_data[cpu_core][USAGE_V2].push_back(cpu_usage_v2);
                    // Storing the total time and idle time for this interval so that it can be used in the next interval
                    prev_cpu_total_idle_time[cpu_core] = std::make_pair(total, idle);
                }
                std::string final_path = HEALTH_STATS_DICT.at("cpu_frequency_path_prefix") + cpu_name + HEALTH_STATS_DICT.at("cpu_frequency_path_suffix");
                std::string cpu_freq = "";
                int64_t cpu_freq_val = 0;
                auto cpu_freq_vector = read_lines_from_file(final_path);
                if(!cpu_freq_vector.empty())
                {
                    cpu_freq = cpu_freq_vector[0];
                    string_to_int64(cpu_freq,cpu_freq_val);
                    if (!cpu_freq.empty())
                    {
                        cpu_info_data[cpu_core]["freq"].push_back((double)cpu_freq_val / 1000);
                        master_cpu_info_data[cpu_core]["freq"].push_back((double)cpu_freq_val / 1000);
                    }
                }
                // Compute CPU load factor = (usage_v2 * current_freq) / max_freq
                if ((!cpu_info_data[cpu_core][USAGE_V2].empty()) && (!cpu_info_data[cpu_core][FREQ].empty())) {
//                    if (cpu_max_freq_khz.find(cpu_core) == cpu_max_freq_khz.end()) {
//                        cpu_max_freq_khz[cpu_core] = nd_factory_utils::getCpuHardwareMaxFreqKHz(cpu_core);
//                    }
                    uint64_t max_freq = cpu_max_freq_khz[cpu_core];
                    if (max_freq > 0) {
                        double latest_usage_v2 = cpu_info_data[cpu_core][USAGE_V2].back();
                        double latest_freq_val = cpu_info_data[cpu_core][FREQ].back() * MHZ_TO_KHZ; // converting back to kHz for load factor calculation
                        double load_factor = (latest_usage_v2 * (double)latest_freq_val) / (double)max_freq;
                        cpu_info_data[cpu_core][LOAD_FACTOR].push_back(load_factor);
                        master_cpu_info_data[cpu_core][LOAD_FACTOR].push_back(load_factor);
                    }
                }
                if (cpu_temp_val > 0)
                {
                    cpu_info_data[cpu_core]["temp"].push_back((double)cpu_temp_val/1000);
                    master_cpu_info_data[cpu_core]["temp"].push_back((double)cpu_temp_val/1000);
                }
            }
        }
    } catch (const std::exception& e) {
        LOG_E(TAG, "Exception caught in get_cpu_info(): %s", e.what());
    }
}

void CpuGpuInfo::get_gpu_info()
{
    get_gpu_freq();
    get_gpu_temp();
    get_gpu_load();

}

void CpuGpuInfo::get_gpu_freq()
{
    std::string gpu_freq = "";
    auto gpu_freq_vector = read_lines_from_file(HEALTH_STATS_DICT.at("gpu_freq"));
    if(!gpu_freq_vector.empty())
    {
        gpu_freq = gpu_freq_vector[0];
        if (!gpu_freq.empty())
        {
            std::string gfd = HEALTH_STATS_DICT.at("gpu_freq_divider");
            int64_t numerator=1, denominator=1;
            string_to_int64(gpu_freq, numerator);
            string_to_int64(gfd, denominator);
            if (denominator == 0)
            {
                LOG_E(TAG, "Denominator is 0!!!!");
            }
            else
            {
                double gpu_freq_val = (double)numerator / (double)denominator;
                const std::lock_guard<std::mutex> lock(gpu_mutex);
                gpu_info_data["freq"].push_back(gpu_freq_val);
                master_gpu_info_data["freq"].push_back(gpu_freq_val);
            }
        }
    }
    

}

void CpuGpuInfo::get_gpu_temp()
{
    std::string gpu_temp = "";
    auto gpu_temp_vector = read_lines_from_file(HEALTH_STATS_DICT.at("gpu_temp"));
    if(!gpu_temp_vector.empty())
    {
        gpu_temp = gpu_temp_vector[0];
        if (!gpu_temp.empty())
        {
            int64_t gpu_temp_val;
            string_to_int64(gpu_temp, gpu_temp_val);
            const std::lock_guard<std::mutex> lock(gpu_mutex);
            gpu_info_data["temp"].push_back((double)gpu_temp_val / 1000);
            master_gpu_info_data["temp"].push_back((double)gpu_temp_val / 1000);
        }
    }
}
void CpuGpuInfo::get_gpu_load()
{
    std::string gpu_load = "";
    auto gpu_load_vector = read_lines_from_file(HEALTH_STATS_DICT.at("gpu_load"));
    if(!gpu_load_vector.empty())
    {
        gpu_load = gpu_load_vector[0];
        size_t pos = gpu_load.find(" ");
        if (pos != std::string::npos)
        {
            gpu_load = gpu_load.substr(0, pos); //0 %   krait file gives this as single line output so taking only the value upto the first space
        }
        if (!gpu_load.empty())
        {
            std::string gld = HEALTH_STATS_DICT.at("gpu_load_divider");
            int64_t numerator =1, denominator=1;
            string_to_int64(gpu_load, numerator);
            string_to_int64(gld, denominator);
            if (denominator == 0)
            {
                LOG_E(TAG, "Denominator is 0!!!!");
            }
            else
            {
                double gpu_load_val = (double)numerator / (double)denominator;
                const std::lock_guard<std::mutex> lock(gpu_mutex);
                gpu_info_data["usage"].push_back(gpu_load_val);
                master_gpu_info_data["usage"].push_back(gpu_load_val);
            }
        }
    }
    // Compute GPU load factor = (usage * current_freq) / max_freq
    const std::lock_guard<std::mutex> lock(gpu_mutex);
    if ((!gpu_info_data[USAGE_V1].empty()) && (!gpu_info_data[FREQ].empty()) && (gpu_max_freq_mhz > 0)) {
        double gpu_usage = gpu_info_data[USAGE_V1].back();
        double gpu_cur_freq = gpu_info_data[FREQ].back();
        double load_factor = (gpu_usage * gpu_cur_freq) / gpu_max_freq_mhz;
        gpu_info_data[LOAD_FACTOR].push_back(load_factor);
        master_gpu_info_data[LOAD_FACTOR].push_back(load_factor);
    }
    
}

void CpuGpuInfo::get_dma_buf_and_kgsl_info(){
    LOG_I(TAG,"Get DMA BUF ");
    std::ifstream dma_buf_file(HEALTH_STATS_DICT.at("dma_buf_size"));
    if (!dma_buf_file.is_open()){
        LOG_E(TAG,"Error in opening dma buffer file");
    }
    else{
        LOG_I(TAG,"DMA Buf file opened");
        std::string line;
        std::vector<std::string> lines;
        while (std::getline(dma_buf_file, line)) {
            lines.push_back(line);
        }
        float dma_buf_size = 0.0;
        long int dma_buf_objects = 0;
        float kgsl = 0.0;
        int start_idx = std::max(static_cast<int>(lines.size()) - 10, 0);
        for (int i = lines.size() - 1; i >= start_idx; i--) {
            if (lines[i].find("Total") != std::string::npos && lines[i].find("objects") != std::string::npos && lines[i].find("bytes") != std::string::npos) {
                std::stringstream ss(lines[i]);
                std::string temp;
                ss >> temp >> dma_buf_objects >> temp >> dma_buf_size;
                dma_buf_size = dma_buf_size / 1024;
                break;
            }
        }
        free_info_data["dma_buf_size"].push_back(dma_buf_size);
        master_free_info_data["dma_buf_size"].push_back(dma_buf_size);
        free_info_data["dma_buf_objects"].push_back(dma_buf_objects);
        master_free_info_data["dma_buf_objects"].push_back(dma_buf_objects);
        dma_buf_file.close();
    }
    #ifdef KRAIT
        LOG_I(TAG,"KGSL File");
        std::ifstream kgsl_file(HEALTH_STATS_DICT.at("kgsl_system_path"));
        if (!kgsl_file.is_open()){
            LOG_E(TAG,"Error in opening kgsl file");
            return ;
        }
        LOG_I(TAG,"KGSL File opened");
        std::string kgsl_string;
        kgsl_file >> kgsl_string;
        // Check if the read operation was successful
        if (kgsl_file.fail()) {
            LOG_E(TAG, "Failed to read value from KGSL file");
            return;
        }

        // Check if the value is numeric (all characters are digits)
        if (!std::all_of(kgsl_string.begin(), kgsl_string.end(), ::isdigit)) {
            LOG_E(TAG, "Non-numeric value found in KGSL file");
            return;
        }
        try {
            // Convert the numeric string to unsigned long long
            unsigned long long kgsl_bytes = std::stoull(kgsl_string);

            // Convert bytes to KB (1 KB = 1024 bytes)
            double kgsl_kb = static_cast<double>(kgsl_bytes) / 1024;
            free_info_data["kgsl_total"].push_back(kgsl_kb);
            master_free_info_data["kgsl_total"].push_back(kgsl_kb);
        } catch (const std::invalid_argument& e) {
            LOG_E(TAG, "Invalid argument: Failed to convert value to unsigned long long");
            return;
        }catch (const std::out_of_range& e) {
            LOG_E(TAG, "Out of range: Value exceeds range for unsigned long long");
            return;
        }
        kgsl_file.close();
    #endif
}
void CpuGpuInfo::gpu_thread()
{
    LOG_I(TAG, "Starting GPU info thread with poll time: %d seconds", gpupoll_time);
    while (keep_alive)
    {
        get_gpu_info();
        std::this_thread::sleep_for(std::chrono::seconds(gpupoll_time));
    }
}

void CpuGpuInfo::system_process_info()
{
    get_cpu_info();
    get_free_info();
    get_swap_info();   
    get_dma_buf_and_kgsl_info();
}

void CpuGpuInfo::execute_thread()
{
    LOG_I(TAG, "=============starting cpu gpu free info thread==============");      
    int32_t freq = cpugpuinfo_time;
    std::chrono::steady_clock::time_point time_before_calling, time_after_calling;
    int32_t num_of_sec = 0;
    std::thread gpu_thread_obj(&CpuGpuInfo::gpu_thread, this);

    while (keep_alive)
    {
        time_before_calling = std::chrono::steady_clock::now();
        system_process_info();
        num_of_sec += freq;
        if (num_of_sec >= 60)
        {
            const std::lock_guard<std::mutex> lock(gpu_mutex);
            send_cpu_gpu_free_info_to_healthstats();
            num_of_sec = 0;
        }
        time_after_calling = std::chrono::steady_clock::now();
        auto difference = std::chrono::duration<double>(time_after_calling - time_before_calling);
        double diff = difference.count();
        double remaining_time = ((double)freq - diff);
        if (remaining_time > 0.0)
        {
           std::unique_lock<std::mutex> lk(cpugpuinfo_mutex);
            LOG_I(TAG, "sleeping on cpugpuinfo_cond ");
            if (cpugpuinfo_cond.wait_for(lk,std::chrono::duration<double>(remaining_time)) == std::cv_status::timeout)
            {
                LOG_I(TAG, " cpugpuinfo_cond.wait_for is timed out");
            }
            else
            {
                LOG_I(TAG, " cpugpuinfo_cond.wait_for is notified");
            }
        }
    }
    if (gpu_thread_obj.joinable()) {
        gpu_thread_obj.join();
    }
}
