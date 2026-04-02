#include <string>
#include <map>
#include "nd_msg_types.h"
#include <nd_time.h>
#include <mutex>
#include<thread>
#include "nd_device_storage_utils.h"

// Forward declaration
class StorageHealthMonitor;

static constexpr char* TAG_WAF = "WAF_U";

class eMMCWafMonitor{
public:
    eMMCWafMonitor(string name, int interval_time, int start_time) : name(name), waf_interval(interval_time)
    {
        LOG_I(TAG_WAF, "Creating thread for %s ", name.c_str());
        c_thread = std::thread(&eMMCWafMonitor::waf_monitor_thread, this);
    }
    bool is_thread_joinable() const;
    void join_thread();
    eMMCWafMonitor(const eMMCWafMonitor&) = delete;
    eMMCWafMonitor& operator=(const eMMCWafMonitor&) = delete;
    ~eMMCWafMonitor() {
    if (c_thread.joinable()) {
        c_thread.join();
    }
}


    void waf_monitor_thread();
    void send_waf_info_to_healthstats(const StorageHealthMonitor& stats , bool isInternal);

private:
    string name;
    int waf_interval = 0;
    std::thread c_thread;
};

