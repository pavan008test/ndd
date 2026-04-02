#include "component_base.h"

class msgq : public ComponentBase {
    public:
    msgq (std::string name, int interval_time, int start_time) : ComponentBase(name, interval_time, start_time)
    {
        ;
    }
    int diagnosis(void *args);
    void recover();

    bool openMsgQ(int storage_type);
    ~msgq(){};
    private:
    
};


