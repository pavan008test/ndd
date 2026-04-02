#include <iostream>
#include <vector>
#include <string>
#include <sys/types.h>
#include <sys/xattr.h>
#include <iomanip>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <sys/stat.h>
#include <unistd.h>

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return s;
}

static bool ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static bool is_target_filetype(const std::string& filename) {
    std::string f = to_lower(filename);
    return ends_with(f, ".mp4") || ends_with(f, ".zip") || ends_with(f, ".aac") || ends_with(f, ".json");
}

static std::string bytes_to_cstring(const std::vector<uint8_t>& buf) {
    if (buf.empty()) return "";
    // Find first NUL; if present, treat as C-string.
    auto it = std::find(buf.begin(), buf.end(), 0);
    size_t n = (it == buf.end()) ? buf.size() : static_cast<size_t>(it - buf.begin());
    return std::string(reinterpret_cast<const char*>(buf.data()), n);
}

static bool read_u32_le(const std::vector<uint8_t>& buf, uint32_t& out) {
    if (buf.size() != 4) return false;
    out = (uint32_t)buf[0]
        | ((uint32_t)buf[1] << 8)
        | ((uint32_t)buf[2] << 16)
        | ((uint32_t)buf[3] << 24);
    return true;
}

static bool read_u64_le(const std::vector<uint8_t>& buf, uint64_t& out) {
    if (buf.size() != 8) return false;
    out = (uint64_t)buf[0]
        | ((uint64_t)buf[1] << 8)
        | ((uint64_t)buf[2] << 16)
        | ((uint64_t)buf[3] << 24)
        | ((uint64_t)buf[4] << 32)
        | ((uint64_t)buf[5] << 40)
        | ((uint64_t)buf[6] << 48)
        | ((uint64_t)buf[7] << 56);
    return true;
}

static void print_hex_bytes(const std::vector<uint8_t>& buf) {
    std::ios old_state(nullptr);
    old_state.copyfmt(std::cout);

    for (uint8_t b : buf) {
        std::cout << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<int>(b);
    }

    std::cout.copyfmt(old_state);
}

// Your schema keys (xattr names are assumed to be user.<key>)
static bool is_schema_key(const std::string& xattr_name, std::string& key_out) {
    // Expect: user.time, user.name, ...
    const std::string prefix = "user.";
    if (xattr_name.rfind(prefix, 0) != 0) return false;
    key_out = xattr_name.substr(prefix.size());
    return true;
}

static bool is_int_key_32(const std::string& key) {
    // Everything int except NAME; but sizes may differ (time looks like u64 in your sample).
    // Use explicit handling below instead of trying to guess here.
    (void)key;
    return true;
}

static void print_schema_value(const std::string& key, const std::vector<uint8_t>& val_buf) {

    // NAME is string (often padded with zeros)
    if (key == "name") {
        std::string s = bytes_to_cstring(val_buf);
        std::cout << s;
        return;
    }

    // From your sample:
    // time: 8 bytes
    // sid: 8 bytes
    // udid: 8 bytes
    // others: 4 bytes
    if (key == "time" || key == "sid" || key == "udid") {
        uint64_t v = 0;
        if (!read_u64_le(val_buf, v)) {
            // If it doesn't match expected size, fall back to hex
            print_hex_bytes(val_buf);
            return;
        }

        if (key == "sid" || key == "udid") {
            // Print both int and hex (as requested)
            std::cout << v << " (hex=0x" << std::hex << v << std::dec << ")";
        } else {
            // time: print as integer
            std::cout << v;
        }
        return;
    }

    // Default: treat as 32-bit little-endian integer
    uint32_t v32 = 0;
    if (!read_u32_le(val_buf, v32)) {
        print_hex_bytes(val_buf);
        return;
    }
    std::cout << v32;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <filename>\n";
        return 1;
    }

    std::string filename = argv[1];
    bool use_schema = is_target_filetype(filename);

    uint64_t total_user_xattr_bytes = 0;

    // Determine filesystem "preferred block size" for I/O (often 4096, but not guaranteed).
    // We'll use it to provide a simple "block-rounded" estimate of disk usage.
    struct stat st {};
    if (stat(filename.c_str(), &st) != 0 || st.st_blksize <= 0) {
        st.st_blksize = 4096;
    }

    ssize_t list_size = listxattr(filename.c_str(), nullptr, 0);
    if (list_size == -1) {
        perror("listxattr");
        return 1;
    }
    if (list_size == 0) return 0;

    std::vector<char> list_buf((size_t)list_size);
    if (listxattr(filename.c_str(), list_buf.data(), list_size) == -1) {
        perror("listxattr");
        return 1;
    }

    size_t pos = 0;
    while (pos < list_buf.size()) {
        std::string name(&list_buf[pos]);
        pos += name.size() + 1;

        ssize_t val_size = getxattr(filename.c_str(), name.c_str(), nullptr, 0);
        if (val_size == -1) {
            perror(("getxattr " + name).c_str());
            continue;
        }

        std::vector<uint8_t> val_buf((size_t)val_size);
        if (val_size > 0) {
            if (getxattr(filename.c_str(), name.c_str(), val_buf.data(), val_size) == -1) {
                perror(("getxattr " + name).c_str());
                continue;
            }
        }

        bool is_user = (name.rfind("user.", 0) == 0);
        if (!is_user) {
            continue;
        }

        total_user_xattr_bytes += static_cast<uint64_t>(val_size);

        std::cout << name << " (size=" << val_size << ") = ";

        if (use_schema) {
            std::string key;
            if (is_schema_key(name, key)) {
                // Only apply schema decode to known keys; otherwise keep hex
                static const char* known_keys[] = {
                    "time","name","duration","size","file_type","status","cam_type","tc_status",
                    "compr_type","udid","sid","upl_vid","rec_vid","drp_status",
                    "ext_upl_vid","ext_rec_vid","ext_drp_status"
                };

                bool known = false;
                for (auto* k : known_keys) {
                    if (key == k) { known = true; break; }
                }

                if (known) {
                    print_schema_value(key, val_buf);
                } else {
                    print_hex_bytes(val_buf);
                }
            }
        } else {
            // Fallback: printable vs hex (still user.* only)
            bool printable = true;
            for (uint8_t b : val_buf) {
                unsigned char c = (unsigned char)b;
                if (!std::isprint(c) && !std::isspace(c)) { printable = false; break; }
            }
            if (printable) {
                std::cout.write((const char*)val_buf.data(), (std::streamsize)val_buf.size());
            } else {
                print_hex_bytes(val_buf);
            }
        }

        std::cout << "\n";
    }

    std::cout << "TOTAL user.* xattr size=" << total_user_xattr_bytes << " bytes\n";
    return 0;
}