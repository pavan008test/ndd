/* Copyright (C) 2023 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Sunil KS <sunil.sampath@netradyne.com>, October 2023
 */

#include <getopt.h>

#include <atomic>
#include <iostream>

#include <log.h>
#include <nd_msg_types.h>
#include <nd_msg_utils.h>

static constexpr char kLogDir[]                      = "/home/ubuntu/.nddevice/log/nd_bt_cli";
static constexpr char kBT_Server_MQ_Name[]           = "BTFV";
static constexpr char kBT_Cli_MQ_Name[]              = "MQ_BT_Cli";

// #define ROUTE_LOGS

namespace nd {

namespace cli {

enum class BtCliOptions {
    kInvalid = -1,
    kCleanup,
};

struct BtCliOptArgs {
    // uint64_t interval_i_ = 0U;
    BtCliOptions option_ = BtCliOptions::kInvalid;
};

class BtCli {
 public:
    bool HandleCommand(const BtCliOptArgs &opt_args);

 private:
    bool SendMessage(msg_type_t type);
    std::atomic<int> msg_id_{0};
};

}  // namespace cli

}  // namespace nd

namespace nd {

namespace cli {

static const char * const kLogTag = "MAIN";

bool BtCli::SendMessage(msg_type_t type) {
    bool status = false;
#ifdef KRAIT
    generic_msg_64_t gen_msg{};
    if (send_msg((generic_msg_64_t *)&gen_msg, type, sizeof(gen_msg),
                 kBT_Cli_MQ_Name, kBT_Server_MQ_Name, msg_id_++)) {
        LOG_I(kLogTag, "%d message sent successfully", type);
        status = true;
    } else {
        LOG_E(kLogTag, "%d msg failed to be sent", type);
    }
#else
    generic_msg_t gen_msg{};
    if (send_msg((generic_msg_t *)&gen_msg, type, sizeof(gen_msg),
                 kBT_Cli_MQ_Name, kBT_Server_MQ_Name, msg_id_++)) {
        LOG_I(kLogTag, "%d message sent successfully", type);
        status = true;
    } else {
        LOG_E(kLogTag, "%d msg failed to be sent", type);
    }
#endif

    return status;
}

bool BtCli::HandleCommand(const BtCliOptArgs &opt_args) {
    bool status = false;

    switch(opt_args.option_) {
        case BtCliOptions::kCleanup: {
            LOG_I(kLogTag, "Sending BTFV_SHUTDOWN");
            status = SendMessage(BTFV_SHUTDOWN);
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

void Showhelp() {
    std::cout << "\n===================================================================================================\n";
    std::cout << "Usage: nd_bt_cli [options]\n";
    std::cout << "Adaptor for forwarding commands to nd_bt service.\n";
    std::cout << "Options:\n";
    std::cout << "  -c           - Send cleanup message to nd_bt)\n";
    std::cout << "  -h           - This help\n";
    std::cout << "===================================================================================================\n\n";
}

bool ParseCmdLineArgs(int argc, char **argv, nd::cli::BtCliOptArgs &cli_opts) {
    bool valid = false;
    int opt = -1;
    unsigned int multiple_opts = 0U;
    while ((opt = getopt(argc, argv, "ch")) != -1) {
        switch(opt) {
            case 'c': {
                cli_opts.option_ = nd::cli::BtCliOptions::kCleanup;
                ++multiple_opts;
                valid = true;
                break;
            }

            case 'h': {
                break;
            }

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

    static std::atomic<int> msg_id_{0};

    if (!nd_log_init(kLogDir)) {
        std::cout << "unable to init logger :: but continue" << std::endl;
    }

#ifdef ROUTE_LOGS
    route_logs(kLogDir);
#endif

    nd::cli::BtCliOptArgs cli_opts;
    if (nd::cli::ParseCmdLineArgs(argc, argv, cli_opts)) {
        nd::cli::BtCli cli_obj;
        cli_obj.HandleCommand(cli_opts);
    }

    return 0;
}
