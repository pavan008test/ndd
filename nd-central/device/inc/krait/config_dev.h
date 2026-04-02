/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Suresh Kumar <suresh.kumar@netradyne.com>, October 2016
 */

#ifndef CONFIG_DEV_H
#define CONFIG_DEV_H

#include <config.h>

struct Config_dev_ctx{
    std::string devconfig_file_path = "/home/ubuntu/config/deviceconfig.ini";
    std::string devconfig_file_content;

    std::string nddevice_file_path = "/home/ubuntu/.nddevice/nddevice.ini";
    std::string nddevice_file_content;

    std::string multi_config_file_content = "";

    bool read_buffer(string file_path1, string file_path2);
 };

string get_config_dev(string key);

#endif

