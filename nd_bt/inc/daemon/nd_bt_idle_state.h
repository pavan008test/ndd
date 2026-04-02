/* Copyright (C) 2023 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, January 2023
 */

#ifndef INC_ND_BT_IDLE_STATE_H_
#define INC_ND_BT_IDLE_STATE_H_

#include <nd_bt_state.h>

namespace nd {
namespace device {

class BTManager;

class IdleState : public interface::IState {
 public:
    explicit IdleState(BTManager *bt_man_ptr);
    void OnMessage(void *msg) override;
    void ActionOnEntry() override;
	void ActionOnExit() override;
    bool MeetsExitCriteria() override;
    bool MeetsEntryCriteria() override;
 private:
    nd::device::BTManager *bt_man_ptr_ = nullptr;
};

} // namespace device
} // namespace nd

#endif  // INC_ND_BT_IDLE_STATE_H_
