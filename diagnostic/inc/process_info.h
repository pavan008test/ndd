/* Copyright (C) 2024 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Debarpan Jana <debarpan.jana@netradyne.com>, September 2024
 */

#ifndef PROCESS_INFO_H
#define PROCESS_INFO_H
#include "component_base.h"
#include <string>
#include <unordered_map>
#include <vector>
#include<mutex>
#include <condition_variable>

class ProcessInfo : public ComponentBase
{
    std::unordered_map<std::string, std::unordered_map<std::string, std::vector<double>>> process_data;
    std::unordered_map<std::string, std::unordered_set<uint8_t>> process_cpu_affinity;
    int ram_size = -1;
    int32_t processinfo_time;
    bool keep_alive = true;
    std::condition_variable pinfo_cond;
    std::mutex pinfo_mutex;
    // structure and map required to calculate cpu_percentage
    struct cpu_times
    {
        std::chrono::steady_clock::time_point last_sys_cpu_times;
        double last_utime_s = 0;
        double last_stime_s = 0;
    };
    std::unordered_map<std::string, cpu_times> last_pmap;
    std::unordered_map<std::string, cpu_times> cur_pmap;

    struct PInfo
    {
        std::string exe = "";
        double cpu_percent = 0.0;
        std::string name = "";
        double shared_memory = 0.0;
        double pss_memory = 0.0;
        double swap_pss_memory = 0.0;
        double mem_hw = -1; 
        double mem_sw = -1; 
        int pid = -2;
        double utime_s = 0;
        double stime_s = 0;
    };

    double calculate_cpu_percentage(int pid,const std::string &name, double utime, double stime);
    std::string get_exe(const std::string &pid);
    std::pair<uint64_t,uint64_t> get_pss(const std::string &pid); // first one is pss(total ram + swap_pss)
    //second one is swap_pss
    std::vector<PInfo> get_running_processes();
    void send_process_info_to_healthstats();
    void add_hw_memory(std::vector<PInfo> &list_of_process,std::vector<PInfo> &process_list);
    PInfo* get_process_by_name(const std::string& process_name, const std::vector<PInfo>& process_list);
    static bool compare_by_pss_memory(const PInfo &a, const PInfo &b);
    static bool compare_by_cpu_percent(const PInfo &a, const PInfo &b);
    std::vector<PInfo> get_desired_process_info();
    void get_process_info();
    int get_ram_Info();
    void set_ram_size();
    public:
    ProcessInfo(std::string name, int interval_time, int start_time) : ComponentBase(name, interval_time, start_time)
    {
        processinfo_time = interval_time;
    }
    ~ProcessInfo(){};
    void notify();
    void execute_thread() override;
    int diagnosis(void *args) override;
    void recover() override;
};
#endif // PROCESS_INFO_H
