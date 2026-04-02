#include <iostream>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <termios.h>
#include <sstream>
#include <cstring>      // for memset
#include <arpa/inet.h>  // for htonl
#include "mcs_obd.h"
#include "mcs_host.h"
#include "nd_task.h"
#include <string>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <pwd.h> // for getpwnam
#include <dirent.h> // for DIR, opendir, closedir
#include <time.h>
#include <sys/types.h>
#include "system_utils.h"

int uart_fd = -1;
constexpr int FD_OPEN_ERR = -1;
constexpr int SIZE_OF_START_FLAG = 4;

static const int adc_data_length = 5; //always 4 from firmware + 1 for null char
char adc_data[adc_data_length]; // To store ADC data read from UART

std::string volt = "";
std::string obd_valid = "";
constexpr uint MAX_READ_COUNT = 5;
constexpr uint TIMEOUT_SECONDS = 3;

bool mk_dir_if_not_present(std::string dname, mode_t mode=0666, bool change_owner=false);

bool is_file_present(std::string fname) {
    std::ifstream f(fname.c_str());
    return f.good();
}

bool change_own_folder(std::string dname)
{
    mode_t process_mask = umask(S_IRWXO);
    struct passwd *pwd = getpwnam ("root");
    if (pwd == NULL) {
        umask(process_mask);
        return false;
    } else {
        uid_t uid = pwd->pw_uid;
        gid_t gid = pwd->pw_gid;

        if ( chown (dname.c_str(), uid, gid) < 0 ) {
            umask(process_mask);
            return false;
        }
    }
    umask(process_mask);

    return true;
}

bool mk_dir_if_not_present(std::string dname, mode_t mode, bool change_owner) {

    DIR *dir = opendir(dname.c_str());
    //Check if directory exists, if yes return success
    if( NULL !=  dir ) {
        closedir(dir);

        if(change_owner == false) {
            return true;
        }
        return change_own_folder(dname);
    }

    //Create directory if not present
    const int res = mkdir(dname.c_str(), mode);
    if (0 != res) {
        return false;
    }

    if(change_owner == false) {
        return true;
    }
    return change_own_folder(dname);
}

static void resolve_path_fallback(std::string &path) {
    // create vehicle_data folder in /dev/shm if not present
    std::string dev_shm_vehicle_data_path = "/dev/shm/fallback";
    if(false == mk_dir_if_not_present(dev_shm_vehicle_data_path)) {
        path = "";
        return;
    }

    std::string filename = "";
    try {
        size_t last_slash_pos = path.find_last_of('/');
        if (last_slash_pos == std::string::npos) {
            path = "";
            return;
        }
        filename = path.substr(last_slash_pos + 1);
        path = dev_shm_vehicle_data_path + "/" + filename;  // Fallback path

        // create an empty file at the fallback path if it doesn't exist
        if(false == is_file_present(path)) {
            std::ofstream ofs(path, std::ios::out);
            if (!ofs.is_open()) {
                path = "";
                return;
            }
            int i = 0;
            ofs << i;
            ofs.close();
        }
    }
    catch (const std::out_of_range& e) {
        path = "";
        return;
    }
    catch (const std::exception& e) {
        path = "";  // Return early on unexpected errors
        return;
    }
    return;
}

bool read_from_sysfs(std::string sysfs_entry, int& value) {

    // open the sysfs entry for reading
    std::ifstream file(sysfs_entry);
    int read_value = 0;

    if (!file.is_open()) {
        return false;
    }

    // read the value from the sysfs entry
    if(!(file >> read_value)) {
        file.close();
        return false;
    }
    value = read_value;

    // close the file
    file.close();
    return true;
}

bool write_into_sysfs(std::string sysfs_entry, int value) {

    // read the value from the sysfs entry and check if it is already set
    int read_value = 0;
    if(true == read_from_sysfs(sysfs_entry, read_value)) {
        if(read_value == value) {
            return true;
        }
    }

    // open the sysfs entry for writing
    std::ofstream file(sysfs_entry);

    if (!file.is_open()) {
        return false;
    }

    // write the value to the sysfs entry
    if(!(file << value)) {
        file.close();
        return false;
    }
    // close the file
    file.close();
    return true;
}

bool close_uart() {
    if(uart_fd != FD_OPEN_ERR) {
        close(uart_fd);
    }
    return true;
}

// USE SET AND RESET API
bool reset_uart() {
    int ret = system("gpio-app -n 16 -o 1");
    if(ret < 0) {
        return false;
    }
    usleep(100*1000);
    ret = system("gpio-app -n 16 -o 0");
    if(ret < 0) {
        return false;
    }
    usleep(100*1000);
    return true;
}

void send_cmd_header() {

    tcflush(uart_fd, TCIOFLUSH);
    uint8_t start_pkt[SIZE_OF_START_FLAG] = {0x49, 0x43, 0x44, 0x43};
    for(int i = 0; i < SIZE_OF_START_FLAG; i++) {
        int write_ret = write(uart_fd, &start_pkt[i], sizeof(start_pkt[i]));
        if(write_ret < 0) {
            return;
        }
        usleep(10*1000);
    }

}

void send_cmd_pkt_info(cmd_pkt_t tx_pkt) {


    if(write(uart_fd, &tx_pkt.pkt_type, sizeof(tx_pkt.pkt_type)) < 0) {
        return;
    }
    usleep(10*1000);

    if(write(uart_fd, &tx_pkt.cmd_id, sizeof(tx_pkt.cmd_id)) < 0) {
        return;
    }

    usleep(10*1000);
    if(write(uart_fd, &tx_pkt.data_len, sizeof(tx_pkt.data_len)) < 0) {
        return;
    }

}


bool configure_uart() {
    uart_fd = open("/dev/ttyHS1", O_RDWR | O_SYNC | O_NOCTTY);
    if (FD_OPEN_ERR == uart_fd) {
        perror("Unable to open UART");
        return false;
    }

    struct termios tty;
    memset (&tty, 0, sizeof(tty)); // Clear struct for new port settings and allocate memory

    // Get current attributes of the Serial port
    if (tcgetattr (uart_fd, &tty) != 0) {
        return false;
    }

    // Set Baud Rate for the UART
    cfsetospeed (&tty, B38400); // Set output baud rate as 38400
    cfsetispeed (&tty, B38400); // Set input baud rate as 38400

    cfmakeraw(&tty);

    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 10;            // 0.5 seconds read timeout

    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;

    tty.c_cflag |= (CLOCAL | CREAD);// ignore modem controls,
                                    // enable reading
    tty.c_cflag &= ~(PARENB | PARODD);      // shut off parity

    tty.c_iflag |= (IGNPAR | ICRNL);

    // Set attributes to the UART
    if (tcsetattr (uart_fd, TCSANOW, &tty) != 0) {
        return false;
    }

    return true;
}

bool send_data_frame_to_uart() {

    // Flush any data received so far on UART
    tcflush(uart_fd, TCIFLUSH);

    // Header Block from 0 to 3 (4 bytes)
    // 0x11 - Packet Type
    // 0x00 - Data Length
    // 0xB2 - Command ID

    send_cmd_header();

    // Command Packet Block
    cmd_pkt_t tx_pkt;
    memset ((void *)&tx_pkt, 0, sizeof(cmd_pkt_t)); // Clear struct

    unsigned char cmd_len = 0;

    tx_pkt.start_flag = htonl (CMD_START_FLAG);
    tx_pkt.pkt_type = SYS_RD_PKT; // 0x11
    tx_pkt.cmd_id = GET_ADC_DATA;
    tx_pkt.data_len = cmd_len;
    send_cmd_pkt_info(tx_pkt);

    return true;
}

bool str_to_int (std::string s, int &num) {
    std::stringstream ss;

    ss.str("");
    ss.clear();

    ss << s;
    ss >> num;
    if (!ss.fail ()) {
        return true;
    }
    else {
        return false;
    }
}

// receive ADC data frame from UART
bool receive_data_frame_from_uart() {

    ack_pkt_t rx_pkt;
    unsigned char first_byte, second_byte, third_byte, ack_byte, ack_data;

    // Read data from UART
    memset ((void *)&rx_pkt, 0, sizeof(ack_pkt_t)); // Clear struct for new packet

    if(read(uart_fd, &first_byte, sizeof(first_byte)) < 0) {
        return false;
    }
    rx_pkt.pkt_type = first_byte;
    usleep(10*1000);

    // second and third byte are data length bytes
    if(read(uart_fd, &second_byte, sizeof(second_byte)) < 0) {
        return false;
    }

    usleep(10*1000);
    if(read(uart_fd, &third_byte, sizeof(third_byte)) < 0) {
        return false;
    }
    usleep(10*1000);

    rx_pkt.data_len = 0;
    rx_pkt.data_len = (((short int)second_byte << 8)| (short int)third_byte);

    if((rx_pkt.data_len == 0) || (rx_pkt.data_len > 4)) {
        return false;
    }

    usleep(10*1000);
    if(read(uart_fd, &ack_byte, sizeof(ack_byte)) < 0) {
        return false;
    }
    rx_pkt.ack_id = ack_byte;

    usleep(10*1000);

    int data_length = rx_pkt.data_len;
    for(int i = 0; i < data_length; i++) {
        if(read(uart_fd, &ack_data, sizeof(ack_data)) < 0) {
            return false;
        }
        adc_data[i] = ack_data;
        usleep(10*1000);
    }
    adc_data[data_length] = '\0'; // Null terminate the string

    int adc_value = -1;
    int prev_value = -1;
    str_to_int(adc_data, adc_value);
    read_from_sysfs(volt, prev_value);

    static int read_count = 0;

    if(adc_value > prev_value) {
        write_into_sysfs(volt, adc_value);
        write_into_sysfs(obd_valid, ++read_count);
    }

    float voltage = adc_value * 0.01;
    printf("Voltage: %.2f V\n", voltage);
    return true;
}

bool uart_parity_configuration(){

    close(uart_fd);
    usleep(100*1000);

    uart_fd = open("/dev/ttyHS1", O_RDWR| O_SYNC | O_NOCTTY);
    if (uart_fd < 0){
        return false;
    }

    struct termios tty;
    memset (&tty, 0, sizeof(tty));
    if (tcgetattr (uart_fd, &tty) != 0) {
        return false;
    }

    cfsetospeed (&tty, B115200);
    cfsetispeed (&tty, B115200);

    cfmakeraw(&tty);

    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 10;            // 0.5 seconds read timeout

    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;

    tty.c_cflag |= (CLOCAL | CREAD);// ignore modem controls,
                                    // enable reading
    tty.c_cflag |= PARENB;
    tty.c_cflag &= ~PARODD;

    tty.c_iflag |= (IGNPAR | ICRNL);

    if (tcsetattr (uart_fd, TCSANOW, &tty) != 0){
        return false;
    }

    return true;
}

int finish_mcs_boot_partition(){

    unsigned char cmd_len;
    unsigned char tx_data=0;
    int partid = 36;

    cmd_pkt_t tx_pkt;

    if (uart_fd != -1){

        memset ((void *)&tx_pkt, 0, sizeof(cmd_pkt_t));
        cmd_len = 0x01;
        tx_pkt.start_flag = htonl (CMD_START_FLAG);
        tx_pkt.pkt_type = SYS_WR_PKT;
        tx_pkt.data_len = cmd_len;
        tx_pkt.cmd_id = CONTROL_BOOT_PARTITION;

        if (partid == 35){
           tx_data = 0x23;
        }
        else if (partid == 36){
           tx_data = 0x24;
        }
        else {
           return -1;
        }

        //write tx pkt to UART

        send_cmd_header();
        send_cmd_pkt_info(tx_pkt);
        int ret = write(uart_fd, &tx_data, sizeof(tx_data));
        if (ret == -1){
           return -1;
        }

    }
    uart_parity_configuration();
    return MCS_SUCCESS;
}

int get_mcs_boot_state () {
    CMD_ACK_BOOT_DATA_T mode;
    int status;
    ack_pkt_t rx_pkt;
    unsigned char c, c1, c2, ack_byte;
    int return_val;
    unsigned char ackdata=0;

    short int cmd_len;
    cmd_pkt_t tx_pkt;
    memset ((void *)&tx_pkt, 0, sizeof(cmd_pkt_t));
    cmd_len = 0x0;
    tx_pkt.start_flag = htonl (CMD_START_FLAG);
    tx_pkt.pkt_type = SYS_RD_PKT;
    tx_pkt.data_len = htons (cmd_len);
    tx_pkt.cmd_id = CONTROL_BOOT_GET_MODE;

    send_cmd_header();
    send_cmd_pkt_info(tx_pkt);

    return_val = read(uart_fd, &c, sizeof(c));
    if(return_val < 0)
        return MCS_FAILURE;

    c =0;

    return_val = read(uart_fd, &c1, sizeof(c1));
    if(return_val < 0)
        return MCS_FAILURE;

    return_val = read(uart_fd, &c2, sizeof(c2));
    if(return_val < 0)
        return MCS_FAILURE;

    rx_pkt.data_len = ((c1 << 8)| c2);

    if (rx_pkt.data_len == 1) {
        return_val = read(uart_fd, &ack_byte, sizeof(ack_byte));
        if(return_val < 0)
            return MCS_FAILURE;

        return_val = read(uart_fd, &ackdata, sizeof(ackdata));
        if(return_val < 0)
            return MCS_FAILURE;

        mode = (CMD_ACK_BOOT_DATA_T)ackdata;

        switch (ackdata) {
            case CONTROL_BOOT_STATE_BL:
            case CONTROL_BOOT_STATE_AP_1:
            case CONTROL_BOOT_STATE_AP_2:
                status = MCS_SUCCESS;
                break;
            default:
                status = MCS_FAILURE;
                break;
        }
    }
    else {
        /* Seems like packet corrupted, length shouldn't be greater than 1 */
        status = MCS_FAILURE;
    }
    return status;
}

bool voltage_monitor_tt(void *args) {

    // Reset UART
    if(false == reset_uart()) {
        return false;
    }

    // Configure UART
    if(false == configure_uart()) {
        return false;
    }
    usleep(500*1000); // wait for 500 ms


    // get_mcs_boot_state();
    if(-1 == finish_mcs_boot_partition()) {
        return false;
    }
    usleep(100*1000); // wait for 100 ms

    uint read_count = 0;
    do {
        if(true == send_data_frame_to_uart()) {
            receive_data_frame_from_uart();
        }
        sleep(0.5);

    } while (++read_count < MAX_READ_COUNT);

    close_uart();

    return true;
}

int64_t get_system_mono_time() {
    struct timespec timeval;
    if(clock_gettime(CLOCK_MONOTONIC_RAW, &timeval)) {
        return 0;
    }
    int64_t cur_uptime =  (int64_t)timeval.tv_sec*1000 + (int64_t)timeval.tv_nsec/1000000;
    return cur_uptime;
}

int main() {

    int64_t start_time = get_system_mono_time();


    volt = get_sysfs_path_from_enum(PowermonParam::eOBD_VOLT);
    obd_valid = get_sysfs_path_from_enum(PowermonParam::eOBD_VALID);

    write_into_sysfs(volt, -1);

    printf("Starting voltage read from UART...\n");

    if(TASK_SUCCESS != nd_timed_task(voltage_monitor_tt, TIMEOUT_SECONDS, NULL, "VOLT_MONITOR")) {
        close_uart();
    }

    int64_t end_time = get_system_mono_time();
    printf("Time taken to read voltage: %lld ms\n", (end_time - start_time));
    return 0;
}
