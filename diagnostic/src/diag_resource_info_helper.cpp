/* Copyright (C) 2024 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Debarpan Jana <debarpan.jana@netradyne.com>, September 2024
 */

#include "diag_resource_info_helper.h"
#define nd_device_obj (ND_DeviceFactory::Create_NDDevice())

//file Utils
bool pathExists(std::string path)
{
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0);
}
std::string get_file_name(std::string path)
{
    std::string filename = "";
    size_t pos = path.find_last_of("/");
    // If found, return the substring after the last separator; otherwise, return the whole path
    if (pos != std::string::npos)
    {
        filename = path.substr(pos + 1);
    }
    return filename; // Return the path itself if no separator is found
}
long long get_epoch()
{
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

//string Utils
const char *string_to_const_char(const std::string &str)
{
    return str.c_str();
}

//Math Utils
std::pair<double, double> CalculateMeanSD(std::vector<double> &values)
{
    int size = values.size();
    double sum = 0.0, mean = 0.0, sd = 0.0;
    if(size == 0){
        return std::make_pair(mean, sd);
    }
    for (size_t list_itr = 0; list_itr < size; list_itr++)
    {
        sum += values[list_itr];
    }
    mean = sum / (double)size;
    sum = 0.0;
    for (size_t list_itr = 0; list_itr < size; list_itr++)
    {
       double diff = values[list_itr] - mean;
       sum += (diff * diff);
    }
    sd = sqrt(sum / (double)size);
    return std::make_pair(mean, sd);
}

std::unordered_map<std::string, double> CreateMeanSdValue(std::unordered_map<std::string, std::vector<double>> &values)
{
    std::unordered_map<std::string, double> final_data;

    final_data["timestamp"] = get_epoch();

    for (auto &data : values)
    {
        if (data.second.size() == 0)
        {
            continue;
        }

        std::pair<double, double> msd = CalculateMeanSD(data.second);
        double mean = msd.first;
        double sd = msd.second;

        if (std::isnan(mean) || std::isnan(sd))
        {
            continue;
        }

        final_data[data.first + "_mean"] = round_to_decimal(mean, 3);
        final_data[data.first + "_sd"] = round_to_decimal(sd, 3);
    }
    return final_data;
}

std::vector<double> CalculateMeanSDMinMax(std::vector<double> &values, bool filter_valid, double usage_threshold)
{
    int size = values.size();
    int valid_value_count = 0;
    vector<double> filtered_values;
    double sum = 0.0, mean = 0.0, sd = 0.0, min_value = 0.0, max_value = 0.0, filtered_sum = 0.0;
    if(size == 0){
        return {mean,sd,min_value,max_value};
    }
    min_value = values[0];
    max_value = values[0];
    for (size_t list_itr = 0; list_itr < size; list_itr++)
    {
        sum += values[list_itr];
        min_value = std::min(min_value,values[list_itr]);
        max_value = std::max(max_value,values[list_itr]);
        if(filter_valid && (values[list_itr] >= usage_threshold)){
            // If filter_valid is true, we only consider values >= usage_threshold for median and mean_v2 calculation
            filtered_values.push_back(values[list_itr]);
            filtered_sum += values[list_itr];
            ++valid_value_count;
        }
    }
    mean = sum / (double)size;
    sum = 0.0;
    for (size_t list_itr = 0; list_itr < size; list_itr++)
    {
       double diff = values[list_itr] - mean;
       sum += (diff * diff);
    }
    sd = sqrt(sum / (double)size);
    if(filter_valid){
        // Standard logic to calculate median
        double median = 0.0, mean_v2 = 0.0;
        if(valid_value_count > 0){
            mean_v2 = filtered_sum / ((double)valid_value_count);
            const int mid = valid_value_count / EVEN_MIDDLE_DIVISOR;
            std::sort(filtered_values.begin(), filtered_values.end());
            if (((valid_value_count % EVEN_MIDDLE_DIVISOR) == 0) && (valid_value_count > EVEN_MIDDLE_OFFSET)) {
                median = (filtered_values[mid - EVEN_MIDDLE_OFFSET] + filtered_values[mid]) / AVERAGE_DIVISOR;
            } else {
                median = filtered_values[mid];
            }
        }
        return {mean,sd,min_value,max_value, median, mean_v2, (double)valid_value_count};
    }
    return {mean,sd,min_value,max_value};
}

std::unordered_map<std::string, double> CreateMeanSdMinMaxValue(std::unordered_map<std::string, std::vector<double>> &values, bool filter_valid, double usage_threshold) // usage_threshold will be used when filter_valid flag is true
{
    std::unordered_map<std::string, double> final_data;

    final_data["timestamp"] = get_epoch();

    for (auto &data : values)
    {
        if (data.second.size() == 0)
        {
            continue;
        } else if ((data.first == USAGE_V2) || (data.first == LOAD_FACTOR)) { // For cpu_usage_v2 and load_factor only mean is required
            auto msd_pair = CalculateMeanSD(data.second);
            final_data[data.first] = round_to_decimal(msd_pair.first, DECIMAL_PRECISION);
            continue;
        }
        // Below conditions check whether key is GPU usage. If it is, then we calculate usage_mean_v2, usage_median and valid values (values >= usage_threshold% usage)
        bool filter_valid_usage = (filter_valid) && (data.first == USAGE_V1);
        std::vector<double> msd = CalculateMeanSDMinMax(data.second, filter_valid_usage, usage_threshold);
        double mean = 0.0, sd = 0.0, min_value = 0.0, max_value = 0.0;
        if(msd.size() >= 4){
            mean = msd[0];
            sd = msd[1];
            min_value = msd[2];
            max_value = msd[3];
        }

        if (std::isnan(mean) || std::isnan(sd))
        {
            continue;
        }

        final_data[data.first + "_mean"] = round_to_decimal(mean, 3);
        final_data[data.first + "_sd"] = round_to_decimal(sd, 3);
        final_data[data.first + "_min"] = round_to_decimal(min_value, 3);
        final_data[data.first + "_max"] = round_to_decimal(max_value, 3);
        if(filter_valid && (msd.size() > VALID_COUNT_POS)){
            final_data[data.first + MEDIAN_SUFFIX] = round_to_decimal(msd[MEDIAN_POS], DECIMAL_PRECISION);
            final_data[data.first + MEAN_V2_SUFFIX] = round_to_decimal(msd[MEAN_V2_POS], DECIMAL_PRECISION);
            final_data[data.first + VALID_COUNT_SUFFIX] = msd[VALID_COUNT_POS];
        }
    }
    return final_data;
}

double round_to_decimal(double number, int decimals)
{
    double factor = std::pow(10, decimals);
    return std::round(number * factor) / factor;
}

//json Utils
// Function to convert std::unordered_map<std::string, double> to json_t*
json_t *convert_map_to_json(const std::unordered_map<std::string, double> &mp)
{

    json_t *obj = json_object();
    for (const auto &pair : mp)
    {

        if (pair.first == "cpu_no" || pair.first == "timestamp")
        {
            json_object_set_new(obj, string_to_const_char(pair.first), json_integer(pair.second));
        }
        else
        {
            json_object_set_new(obj, string_to_const_char(pair.first), json_real(pair.second));
        }
    }
    return obj;
}

// Function to convert std::vector<std::unordered_map<std::string, double>> to json_t*
json_t *convert_vector_to_json(const std::vector<std::unordered_map<std::string, double>> &vec)
{

    if (vec.size() == 1)
    {
        json_t *obj = json_object();
        for (const auto &pair : vec[0])
        {

            if (pair.first == "cpu_no" || pair.first == "timestamp")
            {
                json_object_set_new(obj, string_to_const_char(pair.first), json_integer(pair.second));
            }
            else
            {
                json_object_set_new(obj, string_to_const_char(pair.first), json_real(pair.second));
            }
        }
        return obj;
    }
    else
    {
        json_t *array = json_array();
        for (const auto &map : vec)
        {
            json_t *obj = json_object();
            for (const auto &pair : map)
            {
                if (pair.first == "cpu_no" || pair.first == "timestamp")
                {
                    json_object_set_new(obj, string_to_const_char(pair.first), json_integer(pair.second));
                }
                else
                {
                    json_object_set_new(obj, string_to_const_char(pair.first), json_real(pair.second));
                }
            }
            json_array_append_new(array, obj);
        }
        return array;
    }
}

// Function to convert std::unordered_set<uint8_t> to json_t* after sorting
json_t* convert_unordered_set_to_json_array(const std::unordered_set<uint8_t>& input_set) {
    std::vector<uint8_t> sorted_vector(input_set.begin(), input_set.end());
    std::sort(sorted_vector.begin(), sorted_vector.end());
    json_t *json_array_obj = json_array();
    if (nullptr == json_array_obj) {
        return json_array_obj;
    }
    for (const auto &value : sorted_vector) {
        json_array_append_new(json_array_obj, json_integer(value));
    }
    return json_array_obj;
}
