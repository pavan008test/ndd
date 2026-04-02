/* Copyright (C) 2024 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Debarpan Jana <debarpan.jana@netradyne.com>, September 2024
 */

#ifndef DIAG_RESOURCE_INFO_HELPER_H
#define DIAG_RESOURCE_INFO_HELPER_H
#include <string>
#include <utility>
#include <jansson.h>
#include <chrono>
#include <log.h>
#include "service_utils.h"
#include <vector>
#include <unordered_map>
#include <cmath>
#include <sys/stat.h>
#include <nd_factory.h>
static const std::string USAGE_V2 = "usage_v2";
static const std::string USAGE_V1 = "usage";
static const std::string FREQ = "freq";
static const std::string MEDIAN_SUFFIX = "_median";
static const std::string MEAN_V2_SUFFIX = "_mean_v2";
static const std::string VALID_COUNT_SUFFIX = "_valid_count";
static const std::string LOAD_FACTOR = "load_factor";
static constexpr int DECIMAL_PRECISION = 3;
static constexpr int MEDIAN_POS = 4;
static constexpr int MEAN_V2_POS = 5;
static constexpr int VALID_COUNT_POS = 6;
constexpr int EVEN_MIDDLE_DIVISOR = 2;
constexpr int EVEN_MIDDLE_OFFSET = 1;
constexpr double AVERAGE_DIVISOR = 2.0;
constexpr double GPU_USAGE_THRESHOLD = 2.0;
constexpr double DEFAULT_USAGE_THRESHOLD = 0.1;
constexpr double MHZ_TO_KHZ = 1000.0;

std::string get_device_type(ND_DeviceFactory *nd_device_obj);
bool pathExists(std::string path);
std::string get_file_name(std::string path);
long long get_epoch();
bool starts_with(const std::string &str, const std::string &prefix);
const char *string_to_const_char(const std::string &str);
std::pair<double, double> CalculateMeanSD(std::vector<double> &values);
std::unordered_map<std::string, double> CreateMeanSdValue(std::unordered_map<std::string, std::vector<double>> &values);
std::vector<double> CalculateMeanSDMinMax(std::vector<double> &values, bool filter_valid = false, double usage_threshold = DEFAULT_USAGE_THRESHOLD);
std::unordered_map<std::string, double> CreateMeanSdMinMaxValue(std::unordered_map<std::string, std::vector<double>> &values, bool filter_valid = false, double usage_threshold = DEFAULT_USAGE_THRESHOLD);
double round_to_decimal(double number, int decimals);
// Function to convert std::unordered_map<std::string, double> to json_t*
json_t *convert_map_to_json(const std::unordered_map<std::string, double> &mp);
// Function to convert std::vector<std::unordered_map<std::string, double>> to json_t*
json_t *convert_vector_to_json(const std::vector<std::unordered_map<std::string, double>> &vec);
// Function to convert std::unordered_set<uint8_t> to json_t*
json_t* convert_unordered_set_to_json_array(const std::unordered_set<uint8_t>& cpu_affinity_set);
#endif // DIAG_RESOURCE_INFO_HELPER_H
