/* Copyright (C) 2022 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, October 2022
 */

#include <cstring>
#include <iostream>
#include <vector>

#include <jwt-cpp/base.h>

#include <nd_security_crypt.h>
#include <nd_security_shadow.h>

static uint8_t HexToNibble(char hex) {
    hex = ::toupper(hex);

    if (hex >= 'A' && hex <= 'F') { // A-F case
        return 10 + (hex - 'A');
    } else { // 0-9 case
        return hex - '0';
    }
}

static std::vector<unsigned char> ConvertStringToBytes(const std::string &in_hex_str) {
    std::vector<unsigned char> binary_data;
    if (in_hex_str.size() % 2 == 0) {
        binary_data.resize(in_hex_str.size() / 2 , 0);

        size_t length = in_hex_str.length();
        size_t pos = 0;
        size_t opos = 0;

        while (pos < length && opos < binary_data.size()) {
            uint8_t c1 = HexToNibble(in_hex_str.at(pos++));
            uint8_t c2 = HexToNibble(in_hex_str.at(pos++));
            binary_data[opos++] = (c1 << 4) | c2;
        }
    }

    return binary_data;
}

int main() {
    std::vector<unsigned char> rbytes(32);
    unsigned long err_code = 0;
    if (nd::security::RandomBytes::Get(rbytes, err_code)) {

        char res_hexstring[64];
        std::cout << rbytes.size() << "success rbytes:\n" ;
        for(int x = 0; x < 32; ++x) {
            sprintf(&(res_hexstring[x * 2]), "%02x", rbytes[x]);
        }
        std::cout << res_hexstring << std::endl;

        // std::string rhstr(res_hexstring, 64);
        std::string rhstr = "A810BE0FB51B56524218543F1BC4A6CB758266D841FCD1AA8690CD6BE86CDFA7";

        const std::string dev_id("3633000350");
        const int counter(1);
        const std::string data = dev_id + "-" + std::to_string(counter);
        std::cout << "data: " << data << ":" << data.size() << "\n";

        auto bytes = ConvertStringToBytes(rhstr);

        auto ret = nd::security::HMAC256Evp::Compute(bytes.data(), bytes.size(), (unsigned char *)data.c_str(), data.size());

        constexpr size_t pass_length = 16;

        const std::string hash_str(ret.begin(), ret.end());
        const std::string pass_url = jwt::base::trim<jwt::alphabet::base64url>(jwt::base::encode<jwt::alphabet::base64url>(hash_str));
        std::string pass_out = pass_url.substr(0, pass_length);

        std::cout << "pass_out" << pass_out << "\n";

        memset(&res_hexstring, 0x00, sizeof(res_hexstring));

        for (int i = 0; i < ret.size(); i++) {
            sprintf(&(res_hexstring[i * 2]), "%02x", ret[i]);
            std::cout << "ret:" << ret[i] << "\n";
        }

        std::cout << "HMAC: " << res_hexstring << std::endl;
        std::string hmac_str(res_hexstring);

        std::string encoded;
        if (nd::security::Base64::Encode(ret.data(), ret.size(), encoded)) {
            std::cout << "encoding success: " << encoded << "\n";
        }

        std::vector<unsigned char> dec;

        if (nd::security::Base64::Decode(encoded, dec)) {
            std::cout << "decoding success: " << "\n";
            memset(&res_hexstring, 0x00, sizeof(res_hexstring));
            for (int i = 0; i < dec.size(); i++) {
                sprintf(&(res_hexstring[i * 2]), "%02x", dec[i]);
            }
            std::cout << "decoded: " << res_hexstring << std::endl;
        }

    } else {
        std::cout << "failed in random bytes" << err_code;
    }
    return 0;
}
