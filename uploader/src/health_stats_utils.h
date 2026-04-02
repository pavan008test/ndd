#include <string>

static const char *TAG_THREAD_UPL_HS_TH ="TH_UPL_HS";

bool init_conn_mgr_msgq();
int create_hs_thread();
void collect_signal_info(std::string filename, int rc, bool poll);
void stop_signal_info_polling(std::string filename);
void request_hs_thread_shutdown();