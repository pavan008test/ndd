/* Copyright (C) 2022 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, October 2022
 */

#include <getopt.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include <log.h>
#include <nd_msgq.h>
#include <nd_msg_types.h>
#include <nd_msg_utils.h>

static const char *const kLogDir = "/home/ubuntu/.nddevice/log/nd_sam_cli";
static const char *const kLogTag = "MAIN";

static const char *const kSAM_Ctl_Cli_MQ_Name = "MQ_SAM_Cli";
static const char *const kSAM_Server_MQ_Name  = "MQ_SAM";

// #define ROUTE_LOGS

namespace nd {

namespace cli {

enum class SamCliOptions {
    kInvalid = -1,
    kGetCurrentCounter,
    kIntervalChange,
    kPassChange,
    kSecretChange
};

struct SamCliOptArgs {
    // uint64_t interval_i_ = 0U;
    SamCliOptions option_ = SamCliOptions::kInvalid;
};

class SamCli {
 public:
    bool HandleCommand(const SamCliOptArgs &opt_args);

 private:
    void AwaitReply(std::promise<uint64_t> &promise_obj);
    bool GetReply();
    bool InitMQ();
    bool SendMessage(msg_type_t type);
    nd_msgq_t *server_mq_ = nullptr;
    std::atomic<int> msg_id_{0};
};

}  // namespace cli

}  // namespace nd

namespace nd {

namespace cli {

static const char * const kLogTag = "ND_SAM_CLI";

void SamCli::AwaitReply(std::promise<uint64_t> &promise_obj) {

    do {
        if (nullptr == server_mq_) {
            LOG_E(kLogTag, "server_mq_ is null" );
            break;
        }

        nd_msgq_t::nd_msg_t *msg = nullptr;
        bool resp_received = false;

        // wait for message
        while (true) {
            if ((msg = server_mq_->receive()) == nullptr) {
                LOG_E(kLogTag, "Receive message failed" );
                continue;
            }

            generic_msg_t *g_msg = reinterpret_cast<generic_msg_t *>(msg->get_buffer());
            if (nullptr == g_msg) {
                LOG_E(kLogTag, "msg->get_buffer() returned NULL");
                continue;
            }

            switch(g_msg->msg_type) {

                case NOTIFY_CURRENT_COUNTER : {
                    LOG_I(kLogTag, "Received NOTIFY_CURRENT_COUNTER msg from: %s", g_msg->client_id);
                    sam_pass_counter_notify_msg_t *resp_msg = reinterpret_cast<sam_pass_counter_notify_msg_t *>(g_msg);
                    promise_obj.set_value(resp_msg->counter_);
                    resp_received = true;
                    break;
                }

                default : {
                    LOG_I(kLogTag, "Entered default, received %d msg from: %s", g_msg->msg_type, g_msg->client_id);
                    break;
                }
            }

            if (resp_received) {
                break;
            }
        }

    } while (false);
}

// packaged timed task

bool SamCli::GetReply() {
    bool status = false;

    std::promise<uint64_t> promise_obj;
    std::future<uint64_t> fut_counter = promise_obj.get_future();
    std::thread reply_th = std::thread(&SamCli::AwaitReply, this, std::ref(promise_obj));

    constexpr int kMsgPollTimeoutSec = 35;

    if (fut_counter.valid()) {
        if (std::future_status::timeout == fut_counter.wait_for(std::chrono::seconds(kMsgPollTimeoutSec))) {
            LOG_E(kLogTag, "Timeout waiting on fut_counter");
            if (0 == pthread_cancel(reply_th.native_handle())) {
                LOG_I(kLogTag, "pthread_cancel success in %s", __func__);
            } else {
                LOG_E(kLogTag, "pthread_cancel error in %s, %d - %s", __func__, errno, strerror(errno));
            }
        } else {
            LOG_I(kLogTag, "Counter received: %llu", fut_counter.get());
            status = true;
        }
    } else {
        LOG_E(kLogTag, "fut_counter is invalid");
    }

    if (reply_th.joinable()) {
        reply_th.join();
    }

    return status;
}

bool SamCli::SendMessage(msg_type_t type) {
    bool status = false;
    generic_msg_t gen_msg{};
    if (send_msg((generic_msg_t *)&gen_msg, type, sizeof(gen_msg),
                 kSAM_Ctl_Cli_MQ_Name, kSAM_Server_MQ_Name, msg_id_++)) {
        LOG_I(kLogTag, "%d message sent successfully", type);
        status = true;
    } else {
        LOG_E(kLogTag, "%d msg failed to be sent", type);
    }
    return status;
}

bool SamCli::InitMQ() {
    bool status = false;
    server_mq_ = nd_msgq_t::get_msgq(kSAM_Ctl_Cli_MQ_Name, nd_msgq_t::ND_MSGQ_SERVER);
    if (nullptr != server_mq_) {
        status = true;
        LOG_I(kLogTag, "Message queue created");
    } else {
        LOG_E(kLogTag, "Cannot create cli mq server");
    }
    return status;
}

bool SamCli::HandleCommand(const SamCliOptArgs &opt_args) {
    bool status = false;

    switch(opt_args.option_) {
        case SamCliOptions::kGetCurrentCounter: {
            LOG_I(kLogTag, "Sending GET_CURRENT_COUNTER");
            if (InitMQ()) {
                if (SendMessage(GET_CURRENT_COUNTER)) {
                    GetReply();
                }
            }
            break;
        }

        // case SamCliOptions::kIntervalChange: {
        //     SendMessage();
        //     break;
        // }

        case SamCliOptions::kPassChange: {
            LOG_I(kLogTag, "Sending ON_DEMAND_PASS_CHANGE");
            SendMessage(ON_DEMAND_PASS_CHANGE);
            break;
        }

        case SamCliOptions::kSecretChange: {
            LOG_I(kLogTag, "Sending ON_DEMAND_SECRET_CHANGE");
            SendMessage(ON_DEMAND_SECRET_CHANGE);
            break;
        }

        default : {
            // unknown
            LOG_I(kLogTag, "Entered default, unknown default: %d", static_cast<int>(opt_args.option_));
            break;
        }
    }

    return status;
}

bool CheckUID() {
    const bool status = ((0 == geteuid()) || (0 == getuid()));
    if (!status) {
        LOG_E(kLogTag, "Are you \"root\"?");
    }
    return status;
}

void Showhelp() {
    std::cout << "\n===================================================================================================\n";
    std::cout << "Usage: nd_sam_cli [options]\n";
    std::cout << "Adaptor for forwarding commands to SAM service.\n";
    std::cout << "Options:\n";
    std::cout << "  -p           - Force change password (Increments counter and generats new password)\n";
    std::cout << "  -s           - Force change secret (Generates new key, registers with cloud and changes password)\n";
    std::cout << "  -c           - Get current counter (Displays the current counter value on console)\n";
    // std::cout << "  -i interval  - Rotation interval change\n";
    std::cout << "  -h           - This help\n";
    std::cout << "===================================================================================================\n\n";
}

bool ParseCmdLineArgs(int argc, char **argv, nd::cli::SamCliOptArgs &cli_opts) {
    bool valid = false;
    int opt = -1;
    unsigned int multiple_opts = 0U;
    while ((opt = getopt(argc, argv, "chps")) != -1) {
        switch(opt) {
            case 'c': {
                cli_opts.option_ = nd::cli::SamCliOptions::kGetCurrentCounter;
                ++multiple_opts;
                valid = true;
                break;
            }

            case 'h': {
                break;
            }

            // case 'i': {
            //     cli_opts.option_ = nd::cli::SamCliOptions::kIntervalChange;
            //     cli_opts.interval_i_ = stoull(optarg, nullptr, 10);
            //     ++multiple_opts;
            //     valid = true;
            //     break;
            // }

            case 'p': {
                cli_opts.option_ = nd::cli::SamCliOptions::kPassChange;
                ++multiple_opts;
                valid = true;
                break;
            }

            case 's': {
                cli_opts.option_ = nd::cli::SamCliOptions::kSecretChange;
                ++multiple_opts;
                valid = true;
                break;
            }

            // case ':' : {
            //     if(optopt == 'i') {
            //         // ("Option -%i requires an argument.", optopt);
            //     }
            //     break;
            // }

            default: {
                break;
            }
        }

        if (!valid) {
            break;
        }
    }

        // cannot use multiple options
    if ((1 < multiple_opts) || (!valid)) {
        valid = false;
        Showhelp();
    }

    return valid;
}

}  // namespace cli

}  // namespace nd

int main(int argc, char **argv) {
    if (!nd_log_init(kLogDir)) {
        std::cout << "unable to init logger :: but continue" << std::endl;
    }

#ifdef ROUTE_LOGS
    route_logs(kLogDir);
#endif

    nd::cli::SamCliOptArgs cli_opts;
    if (nd::cli::ParseCmdLineArgs(argc, argv, cli_opts) && nd::cli::CheckUID()) {
        std::unique_ptr<nd::cli::SamCli> cli_ptr = std::make_unique<nd::cli::SamCli>();
        cli_ptr->HandleCommand(cli_opts);
    }
    return 0;
}
