/* Copyright (C) 2022 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, October 2022
 */

#include <unistd.h>

#include <iostream>
#include <memory>

#include <nd_timer.h>

void CB(uint64_t tick) {
    std::cout << tick << "\n";
}

int main () {
    std::unique_ptr<nd::utils::TimerTick> tick =  std::unique_ptr<nd::utils::TimerTick>(new (std::nothrow) nd::utils::TimerTick());

    if (tick) {
        tick->RegisterCB(CB);
        tick->SetInterval(3);
        tick->Start();
        sleep(15);
    }

    return 0;
}
