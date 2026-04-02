#include <string>
#include <unistd.h>
#include <stdlib.h>

#include "log.h"

#include "service_utils.h"

using namespace std;

static const string sname="TEST_SERVICE";

int main() {
    NDService *obj = NDService::get_service_obj(sname);
    obj->send_err_msg(SM_E_CAM_CRASH, 0, "Camera crash");

    while(1) { sleep(10); }

    NDService::release_service_obj();

    return 0;
}

