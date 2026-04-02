#ifndef BASE_COMPONENT_HPP
#define BASE_COMPONENT_HPP

#include <iostream>
#include <string>
#include <cstdlib>
#include <thread>
#include <sqlite3.h>
#include <cstring>
#include <sstream> 
#include <nd_time.h> 


using namespace std;



struct db_state_info_t{
    int64_t index;
    int64_t epoch_time;
    int64_t system_uptime;
    int64_t udid;
    int component_state;
    int component_substate;
    int recovery_method;
    int recovered;
    string component_name;
};
typedef sqlite3 db_handle_t;
static const string table_formatter = "CREATE TABLE COMPONENTSTATES(" \
                        "INDEXID INTEGER PRIMARY KEY  AUTOINCREMENT," \
                        "TIME         INT     NOT NULL," \
                        "SYSTEM_UPTIME        INT     NOT NULL," \
                        "UDID                     INT     NOT NULL," \
                        "COMPONENT_STATE           INT     NOT NULL," \
                        "COMPONENT_SUBSTATE           INT     NOT NULL," \
                        "RECOVERY_METHOD           INT     NOT NULL," \
                        "RECOVERED           INT     NOT NULL," \
                        "COMPONENT_NAME   TEXT     NOT NULL);" ;

static const string insert_str =
        "INSERT INTO COMPONENTSTATES (TIME,SYSTEM_UPTIME, UDID, COMPONENT_STATE, COMPONENT_SUBSTATE, RECOVERY_METHOD, RECOVERED, COMPONENT_NAME) VALUES ";

static const string PM_DB_PATH_NAME     = "/home/ubuntu/.nddevice/db/diagnostic.db";
static const string PM_GEN_PROP_DB_PATH_NAME     = "/home/ubuntu/.nddevice/db/gen_property.db";

/* 
    This is the base component class which is meant to be inherited 
    by the actual components of the pipeline.

    This class enforces the deriving classes to implement 
    diagnosis() , recovery(), and execute() methods

    Every object instantiation of this class creates a thread running 
    "execute_thread()" 
*/
class ComponentBase {

public:

    /* enum for component state */
    enum component_state_t{
        COMPONENT_NORMAL_STATE,  // Component is in working state
        COMPONENT_REDUCED_ACCESSIBILITY,  // Component's accessibility is not normal
        COMPONENT_REDUCED_PERFORMANCE,  // Component's performance is not normal
        COMPONENT_UNKNOWN_STATE,  // execution timeout
        COMPONENT_STATE_MAX
    };

    /* constructor expects name*/
    ComponentBase(std::string , int diag_int_time, int diag_start_time);

    /* Following three virtual methods are to be mandatorily implemented
       by deriving classes */
    virtual int diagnosis(void *args) = 0;
    virtual void recover() = 0;

    /* returns name of the component */
    std::string get_name();

    //delete copy constructor
    ComponentBase(const ComponentBase&) = delete;

    //delete copy assignment
    ComponentBase& operator=(const ComponentBase&) = delete;

    void join_child_thread() ;

    // destructor
    ~ComponentBase() ;

    const int diag_interval_time = 30; // interval between diagnosis
    const int diag_start_time = 60;   // first start diagnosis time after boot.
    bool get_db_node_event(string event, db_state_info_t* data_node, db_handle_t *db_handle);
    //// DB functions/variables
    static bool open_db(string bd_file, db_handle_t** db_handle);
    static pthread_mutex_t db_handle_mutex;
    static bool create_table_db(db_handle_t* db_handle);
    static bool exec_cmd_db(db_handle_t* db_handle, const string command,
                int (*callback)(void*,int,char**,char**), void* cb_data);
    static bool db_limit_rows(db_handle_t *db_handle);
    bool add_event_db(int substate) ;
    static db_handle_t* db_handle ;
    static int64_t udid;
    component_state_t state = COMPONENT_NORMAL_STATE;
    int recovery_method;
    int recovered;
    virtual void execute_thread();// actual thread logic, to be implemented by derived classes or use default implementation
    bool start_thread(); // after creating the object, call this method to start the thread

private:

    std::string name;
    std::thread c_thread;
    int64_t number_of_issue;

    /* method that runs in the component thread */
    
    bool add_event_db(db_handle_t *db_handle, int64_t time, int64_t sys_time, int64_t udid, int component_state, int component_substate, int recovery_method, int recovered,  string component_name);
};

#endif


