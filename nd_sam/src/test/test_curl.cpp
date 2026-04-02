/* Copyright (C) 2022 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, October 2022
 */

#include <iostream>

#include <jansson/jansson.h>

#include <nd_curl_helper.h>

int main() {

    nd::utils::CurlHelper curl;
    const std::string data = "{\"secret\":\"" + std::string("848efeb691e4b6c3c67be64e8fa1929e190187b4246b3a24ed995754a8d930a") + "\",\"counter\":1}";
    auto status_ptr = curl.Call("device/secret-key", data);

    if (status_ptr) {
        json_error_t error;
        json_t *root = json_loads(status_ptr->response_string_.c_str(), 0, &error);

        bool response = false;

        if (NULL != root) {
            const json_t *call_json_resp = json_object_get(root, "response");
            if (NULL != call_json_resp) {
                response = json_boolean_value(call_json_resp);
                std::cout << "response: " << response <<"\n";
            }
        }
        json_decref(root);
        if ((CURLE_OK == status_ptr->code_) &&
            (200 == status_ptr->resp_code_) &&
            (response)) {
            std::cout << "success\n";
        }
    }

    return 0;
}
