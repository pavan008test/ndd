/* Copyright (C) 2023 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, January 2023
 */

#ifndef INC_ND_INTERFACE_STATE_H_
#define INC_ND_INTERFACE_STATE_H_

namespace nd {
namespace interface {

class IState {
 public:
    virtual ~IState() = default;
    virtual void OnMessage(void *msg) = 0;
    virtual void ActionOnEntry() = 0;
	virtual void ActionOnExit() = 0;
    virtual bool MeetsExitCriteria() = 0;
    virtual bool MeetsEntryCriteria() = 0;
};

} // namespace interface
} // namespace nd

#endif  // INC_ND_INTERFACE_STATE_H_
