/* Copyright (C) 2024 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Debarpan Jana <debarpan.jana@netradyne.com>, September 2024
 */

#ifndef HEALTH_STATS_DICT_BAGHEERA_H
#define HEALTH_STATS_DICT_BAGHEERA_H
#include <unordered_map>
#include <string>

const std::unordered_map<std::string, std::string> HEALTH_STATS_DICT{
    {{"cpuTemp", "/sys/devices/virtual/thermal/thermal_zone1/temp"},
     {"cpu_frequency_path_prefix", "/sys/devices/system/cpu/"},
     {"cpu_frequency_path_suffix", "/cpufreq/scaling_cur_freq"},
     {"cpu_usage", "/proc/stat"},
     {"process_memory_hw", "/sys/kernel/debug/nvmap/iovmm/procrank"},
     {"gpu_freq", "/sys/devices/17000000.gp10b/devfreq/17000000.gp10b/cur_freq"},
     {"gpu_load", "/sys/devices/17000000.gp10b/load"},
     {"gpu_temp", "/sys/kernel/debug/bpmp/debug/soctherm/group_GPU/temp"},
     {"gpu_freq_divider", "1000000"},
     {"gpu_load_divider", "10"},
     {"ram_size","4096"},
     {"dma_buf_size", "/sys/kernel/debug/dma_buf/bufinfo"}}};

#endif // HEALTH_STATS_DICT_BAGHEERA_H
