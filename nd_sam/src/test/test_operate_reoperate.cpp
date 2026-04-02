/* Copyright (C) 2022 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, October 2022
 */

#include <iostream>

#include <nd_auth_openssl.h>

static const char * const path = "/home/ubuntu/.nddevice/sam_db/sam_cfd.db";
static const char * const path_other = "/home/ubuntu/.nddevice/sam_db/sam_cfd_dec.db";

int main() {

    const auto result = nd_file_reoperate_to_file(path, path_other, true);

    if (ND_AUTH_SUCCESS == result) {
        std::cout << "nd_file_reoperate_to_file success, src:" << path << " dest: " << path_other << " res: " << result;
    } else {
        std::cout << "nd_file_reoperate_to_file failed, src:" << path << " dest: " << path_other << " res: " << result;
    }

    return 0;
}
