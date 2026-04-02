#include "component_base.h"
#include <log.h>
#include <unistd.h>
#include <mutex>
#include <condition_variable>
#include "service_utils.h"

static constexpr char* TAG = "CBASE";

extern std::condition_variable sd_cond;
extern std::mutex sd_mutex;
static const int SQLITE3_BUSY_TIMEOUT = 5000; //sqlite3 timeout in ms
db_handle_t* ComponentBase::db_handle = NULL;
pthread_mutex_t ComponentBase::db_handle_mutex ;
int64_t ComponentBase::udid = 0 ;
extern NDService *nd_service_obj;

ComponentBase::ComponentBase(string name, int interval_time, int start_time) : name(name), diag_interval_time(interval_time), diag_start_time(start_time) {
    LOG_I(TAG,"CTOR called for %s", this->name.c_str());
}

void ComponentBase::join_child_thread()
{
    LOG_I(TAG,"join_child_thread() called. joining child thread: %s", this->name.c_str());
    c_thread.join();        
    LOG_I(TAG,"inside join_child_thread() ......... It should not come here" );
}

ComponentBase::~ComponentBase()
{
    LOG_I(TAG,"DTOR called for %s", this->name.c_str());
}
std::string ComponentBase::get_name(){
    return name;
}

void ComponentBase :: execute_thread(){
    LOG_I(TAG,"execute_thread(), component: %s", name.c_str() );
    LOG_I(TAG,"sleeping... diagnosis will start after %d seconds", diag_start_time);
    sleep(diag_start_time);  
    while(1){
        static component_state_t prev_state = COMPONENT_STATE_MAX;
        static int prev_substate = -1;
        int substate = this->diagnosis(NULL);

        if(prev_state != state || prev_substate != substate){
            LOG_I(TAG,"state changed, prev_state: %d, state: %d, prev_substate: %d, substate: %d ", prev_state, state, prev_substate, substate );
            add_event_db(substate);
        }
        prev_state = state;
        prev_substate = substate ;

        if(state == COMPONENT_REDUCED_ACCESSIBILITY || state == COMPONENT_REDUCED_PERFORMANCE){
            LOG_E(TAG,"Components State: %d", state);
            recover();
        }
        std::unique_lock<std::mutex> lk(sd_mutex);
        LOG_I(TAG,"sleeping on sd_cond ");
        if(sd_cond.wait_for(lk, std::chrono::seconds(diag_interval_time) ) == std::cv_status::timeout  ) {
            LOG_I(TAG," sd_cond.wait_for is timed out");
        }
        else {
            LOG_I(TAG," sd_cond.wait_for is notified");
        }
    }
}

bool ComponentBase::start_thread()
{
    bool status = false;
    if (c_thread.get_id() == std::thread::id())
    {
        try
        {
            c_thread = std::thread(&ComponentBase::execute_thread, this);
            status = true;
        }
        catch (const std::system_error &e)
        {
            LOG_E(TAG, "Exception caught while starting thread for %s with exception: %s", name.c_str(), e.what());
        }
    }
    else
    {
        LOG_E(TAG, "Thread already running for %s", name.c_str());
        status = true;
    }
    return status;
}

bool ComponentBase :: open_db(string db_file, db_handle_t** db_handle)
{
   char *zErrMsg = 0;
   int rc;

   rc = sqlite3_open(db_file.c_str(), db_handle);
   if( rc ){
      LOG_E(TAG, "Can't open database: %s", sqlite3_errmsg(*db_handle));
      *db_handle = NULL;
      return false;
   }

   rc = sqlite3_busy_timeout((*db_handle), SQLITE3_BUSY_TIMEOUT); //setting timeout
   if (rc){
       LOG_E(TAG, "Unable to set timeout for db. Timeout is set to default value = 0");
   }else{
       LOG_I(TAG, "Set db timeout to %dms", SQLITE3_BUSY_TIMEOUT);
   }
   return true;
}

bool ComponentBase :: create_table_db(db_handle_t* db_handle)
{
    if(db_handle == NULL){
        LOG_E(TAG, "create_table_db: db_handle == NULL; returning");
        return false;
    }
    char *zErrMsg = 0;
    int  rc;

    pthread_mutex_lock(&db_handle_mutex);
    rc = sqlite3_exec(db_handle, table_formatter.c_str(), NULL, 0, &zErrMsg);
    pthread_mutex_unlock(&db_handle_mutex);
    //// parse the error message
    if( rc != SQLITE_OK ){
        if(strstr(zErrMsg, "already exists") != NULL){
            LOG_I(TAG, "SQL error@ %s", zErrMsg);
            LOG_I(TAG, "Ignoring error since table already exists");
            sqlite3_free(zErrMsg);
            return true;
        }
        LOG_E(TAG, "SQL error@ %s", zErrMsg);
        sqlite3_free(zErrMsg);
        return false;
    }else{
      LOG_I(TAG, "Success in create_table_db");
      return true;
    }
}

bool ComponentBase :: db_limit_rows(db_handle_t *db_handle) {
    int rc;
    std::stringstream val_stream;
    val_stream << "DELETE FROM COMPONENTSTATES WHERE INDEXID IN (SELECT INDEXID FROM " \
        " COMPONENTSTATES ORDER BY INDEXID DESC LIMIT -1 OFFSET 1000)";
    rc = ComponentBase::exec_cmd_db(db_handle, val_stream.str(), NULL, NULL);
    if( rc == false ){
      LOG_E(TAG, "SQL error");
      return false;
    }
    return true;
}

bool ComponentBase :: exec_cmd_db(db_handle_t* db_handle, const string command,
        int (*callback)(void*,int,char**,char**), void* cb_data)
{
    if(db_handle == NULL){
        LOG_E(TAG, "exec_cmd_db: db_handle == NULL; returning");
        return false;
    }
    int rc;
    LOG_I(TAG, "command ::%s::", command.c_str());
    pthread_mutex_lock(&db_handle_mutex);
    char *errmsgs = 0;
    rc = sqlite3_exec(db_handle, command.c_str(), callback, cb_data, &errmsgs);
    pthread_mutex_unlock(&db_handle_mutex);
    if( rc != SQLITE_OK ) {
      LOG_E(TAG, "SQL error: %s", errmsgs);
      sqlite3_free(errmsgs);
      return false;
    } else {
      LOG_I(TAG, "Success in exec_cmd_db");
    }
    return true;
}

bool ComponentBase::add_event_db(db_handle_t *db_handle, int64_t time, int64_t sys_uptime, int64_t udid, int component_state, 
                                                                int component_substate, int recovery_method, int recovered, string component_name)
{
    LOG_C(TAG, "inside add_event_db for %s ", component_name.c_str());
    int rc;

    std::stringstream val_stream;
    val_stream  << insert_str
                << "(" << time << ", "
                << sys_uptime << ", "
                << udid << ", "
                << "'" << component_state << "'" << ", "
                << "'" << component_substate << "'" << ", "
                << "'" << recovery_method << "'" << ", "
                << "'" << recovered << "'" << ", "
                << "'" << component_name << "'" << " );" ;

    rc = ComponentBase::exec_cmd_db(db_handle, val_stream.str(), NULL, NULL);
    if(rc == false){
        // Notify health mon
        std::stringstream str_msg;
        str_msg << "failed to execute exec_cmd_db ";
        LOG_E(TAG, str_msg.str().c_str() );
        nd_service_obj->send_err_msg(SM_E_PM_DB_ADD_EVENT_FAIL, NDService::UNUSED_ERR_AUX_CODE, str_msg.str() );
        return false;
    }
    return true;
}

bool ComponentBase::add_event_db(int substate) {
    bool ret = add_event_db(db_handle, get_system_time(), get_system_monotonic_time(), udid, state, substate, recovery_method, recovered, name);
    LOG_I(TAG, "add_event_db() exiting, ret:  %d",ret);
    return ret;
}


