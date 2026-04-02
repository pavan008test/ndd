/* Copyright (C) 2024 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Debarpan Jana <debarpan.jana@netradyne.com>, September 2024
 */

#ifndef CPU_GPU_INFO_H
#define CPU_GPU_INFO_H
#include "component_base.h"
#include <string>
#include <unordered_map>
#include <vector>
#include<mutex>
#include <condition_variable>
#include <cpu_scheduler_utils.h>
#include <sys/sysinfo.h>

class CpuGpuInfo : public ComponentBase
{
    std::unordered_map<int, std::unordered_map<std::string, std::vector<double>>> cpu_info_data;
    std::unordered_map<std::string, std::vector<double>> gpu_info_data;
    std::unordered_map<std::string, std::vector<double>> free_info_data;
    std::unordered_map<int, std::unordered_map<std::string, std::vector<double>>> master_cpu_info_data;
    std::unordered_map<std::string, std::vector<double>> master_gpu_info_data;
    std::unordered_map<std::string, std::vector<double>> master_free_info_data;
    std::unordered_map<int, std::pair<uint64_t, uint64_t>> prev_cpu_total_idle_time; // key of map is cpu core number and value is a pair of previous total time and idle time
    std::unordered_map<int, uint64_t> cpu_max_freq_khz; // core_number -> hardware max frequency in KHz (for load factor)
    double gpu_max_freq_mhz = 0.0; // GPU hardware max frequency in MHz (for load factor)
    int32_t cpugpuinfo_time;
    int32_t gpupoll_time;
    const int GPU_THREAD_SLEEP_SEC = 1;// GPU info polling thread minimum sleep time in seconds
    int32_t reset_master_data_counter_local = 0;
    bool keep_alive = true;
    std::condition_variable cpugpuinfo_cond;
    std::mutex cpugpuinfo_mutex;
    std::mutex gpu_mutex;
    void update_cpu_info(std::unordered_map<int, std::unordered_map<std::string, std::vector<double>>> &src, std::unordered_map<int, std::unordered_map<std::string, std::vector<double>>> &dest);
    void update_other_info(std::unordered_map<std::string, std::vector<double>> &src, std::unordered_map<std::string, std::vector<double>> &dest);
    void send_master_cpu_info(std::unordered_map<std::string, std::vector<std::unordered_map<std::string, double>>> &masterdata);
    void send_master_gpu_info(std::unordered_map<std::string, std::vector<std::unordered_map<std::string, double>>> &masterdata);
    void send_master_free_info(std::unordered_map<std::string, std::vector<std::unordered_map<std::string, double>>> &masterdata);
    void send_master_info();
    void send_cpu_info_to_healthstats();
    void send_gpu_info_to_healthstats();
    void send_free_info_to_healthstats();
    void send_cpu_gpu_free_info_to_healthstats();
    void get_swap_info();
    void get_free_info();
    void get_cpu_info();
    void get_gpu_info();
    void get_gpu_freq();
    void get_gpu_temp();
    void get_gpu_load();
    void gpu_thread();
    void system_process_info();
    void get_dma_buf_and_kgsl_info();
    PowerStateController ps_obj;
    public:
    CpuGpuInfo(std::string name, int interval_time, int start_time, int gpu_poll_interval) : ComponentBase(name, interval_time, start_time)
    {
        cpugpuinfo_time = interval_time;
        gpupoll_time = (gpu_poll_interval >= GPU_THREAD_SLEEP_SEC) ? gpu_poll_interval : GPU_THREAD_SLEEP_SEC; // range check to make sure interval is greater than 1 second
        ps_obj.loadPowerStateConfig(PowerStateService::eDiagnostic); // Initialisation needed for thermal throttling in case of high CPU temperature
        gpu_max_freq_mhz = nd_factory_utils::getGpuHardwareMaxFreqMHz(); // Cache GPU max freq for load factor computation
        int n_cpus = get_nprocs_conf(); // Get number of CPU cores to initialize cpu_max_freq_khz
        for (int i = 0; i < n_cpus; i++) {
            cpu_max_freq_khz[i] = nd_factory_utils::getCpuHardwareMaxFreqKHz(i); // Cache CPU max freq for load factor computation
        }
    }
    ~CpuGpuInfo() {};
    void notify();
    void execute_thread() override;
    int diagnosis(void *args) override;
    void recover() override;
};
#endif // CPU_GPU_INFO_H
