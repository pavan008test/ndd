/* Copyright (C) 2024 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Debarpan Jana <debarpan.jana@netradyne.com>, September 2024
 */

#ifndef HEALTH_STATS_DICT_KRAIT_H
#define HEALTH_STATS_DICT_KRAIT_H
#include <unordered_map>
#include <string>

const std::unordered_map<std::string, std::string> HEALTH_STATS_DICT{
    {{"cpuTemp", "/sys/class/thermal/thermal_zone8/temp"},
     {"cpu_frequency_path_prefix", "/sys/devices/system/cpu/"},
     {"cpu_frequency_path_suffix", "/cpufreq/scaling_cur_freq"},
     {"cpu_usage", "/proc/stat"},
     {"gpu_freq", "/sys/kernel/gpu/gpu_clock"},
     {"gpu_load", "/sys/kernel/gpu/gpu_busy"},
     {"gpu_temp", "/sys/class/thermal/thermal_zone18/temp"},
     {"gpu_freq_divider", "1"},
     {"gpu_load_divider", "1"},
     {"ram_size","2048"},
     {"ion_heap_file", "/sys/kernel/debug/ion/heaps/system"},
     {"dma_buf_size", "/sys/kernel/debug/dma_buf/bufinfo"},
     {"kgsl_proc_path", "/sys/class/kgsl/kgsl/proc"},
     {"kgsl_system_path", "/sys/devices/virtual/kgsl/kgsl/page_alloc"}}};

#endif // HEALTH_STATS_DICT_KRAIT_H
