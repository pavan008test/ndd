#include <iostream>
#include <fstream>
#include <cstdio>
#include <string>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <curl/curl.h>
#include <log.h>
#include <ctime>
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include "sys/stat.h"
#include <service_utils.h>
#include <system_utils.h>

// #include "serial_port.h"

#define TAG         "AGNSS"
#define GPS         'G'
#define HEAD        0x2404
#define TAIL        0x44AA
#define START_ID    1200
#define DATA_ID     1201
#define END_ID      1202
#define ACK_TIMEOUT 5000
enum DownloadConstants{
    MAX_EPO_DOWNLOAD_RETRY = 3
};
using namespace std;

static std::atomic<int> try_rewrite{0};
string epo_erase_command="$PAIR472*3B";
string epo_erase_ack="$PAIR001,472,0*3A";
time_t epo_download_time;
tm* utc_time;
static bool critical_event = false;

extern NDService *nd_service_obj;
extern pthread_mutex_t gps_port_write_mutex;

bool check_internet_exist()
{
    int max_ping_count =4;

    string dns_array[max_ping_count] = {"8.8.8.8" /*ipv4 google dns*/, "2001:4860:4860::8888" /*ipv6 google dns*/, "1.1.1.1" /*ipv4 cloudflare dns*/, "2606:4700:4700::1111" /*ipv6 cloudflare dns*/};
    for (int i =0; i< max_ping_count; i++)
    {
        string cmd = "";
        cmd = "ping -c 1 -w 1 " + dns_array[i] + " > /dev/null; if [ $? -eq 0 ]; then echo 'Online'; else echo 'Offline'; fi";
        string status = "";

        if(system_execute_with_resp("INTERNET_CHECK", cmd, status) == false) {
            LOG_E(TAG, "failed to execute cmd: %s", cmd.c_str());
        }
        LOG_D(TAG, "func: %s: dns server: %s status = %s", __func__, dns_array[i].c_str(), status.c_str());

        if (status.find("Online") != status.npos) {
            return true;
        }
    }

    return false;
}

string get_imei(){
    string imei;
    string cmd = "lte_gps_sample_app 'AT+CGSN' | grep -oE '[0-9]{15}' ";
    if(system_execute_with_resp(TAG,cmd,imei)){
        LOG_I(TAG, "Detected IMEI: %s", imei.c_str());
        imei.erase(remove(imei.begin(), imei.end(), '\n'),imei.end());
        return imei;
    } else {
        LOG_E(TAG, "Failed to get IMEI");
        return "";
    }
}


size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    std::ofstream* ofs = static_cast<std::ofstream*>(userp);
    size_t totalSize = size * nmemb;
    ofs->write(static_cast<char*>(contents), totalSize);
    return totalSize;
}

// If internet exists, make sures to download epo files of non-zero byte size and stores them under a directory in RAM
bool agnss_init(int no_of_epofiles) {
    LOG_I(TAG, "Entered agnss_init()..");
    CURL* curl;
    CURLcode res;
    std::ofstream ofs;

    string resp;
    string cmd = "mkdir -p /dev/shm/agnss_epo_files";
    if(system_execute_with_resp(TAG, cmd, resp)==true){
        LOG_I(TAG, "Epo files Directory created successfully.");
    } else {
        LOG_E(TAG, "failed to create epo files directory. cmd : %s", cmd.c_str());
        return false;
    } 

    if(check_internet_exist()){
        LOG_I(TAG, "Internet is available.");
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl = curl_easy_init();
        if(curl) {
           LOG_I(TAG, "CURL initialized successfully.");
           string imei_no = get_imei();
           for (int i = 1; i <= no_of_epofiles; ++i) {
               std::string url = "http://wpepodownload.mediatek.com/EPO_GPS_3_" + std::to_string(i) + ".DAT?vendor=QUECTEL&project=LxSRJQpGKYThHDFOvGh0Kmz0w-6nSrW9U_o0CS-kmlA&device_id=" + imei_no;
               std::string file_path = "/dev/shm/agnss_epo_files/EPO_GPS_3_" + std::to_string(i) + ".DAT";
               int epo_download_retry = 0;
               bool download_successful = false;
               while (epo_download_retry < MAX_EPO_DOWNLOAD_RETRY && !download_successful) {
                   ofs.open(file_path);
                   curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
                   curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
                   curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ofs);

                   res = curl_easy_perform(curl);
                   if(res != CURLE_OK) {
                       LOG_E(TAG, "Failed to perform curl_easy_perform(). Error: %s", curl_easy_strerror(res));
                       ofs.close();
                       return false;
                   }

                   epo_download_time = std::time(nullptr); // Store the current time
                   utc_time = std::gmtime(&epo_download_time); // Convert to UTC time
                   LOG_I(TAG, "File %d , EPO_GPS_3_%d File downloaded at (UTC): %s", i, i, std::asctime(utc_time));
                   ofs.close();
                   // sometimes epo files of size 0 bytes are being downloaded. So, check the file size and retry if it is 0 bytes.
                   struct stat file_stat;
                   if(stat(file_path.c_str(), &file_stat) == 0) {
                       if(file_stat.st_size == 0) {
                           LOG_E(TAG, "Downloaded file %s is of 0 bytes. Retrying... (%d/3)", file_path.c_str(), epo_download_retry + 1);
                           ++epo_download_retry;
                       } else {
                           LOG_I(TAG, "Downloaded file %s size: %ld bytes", file_path.c_str(), file_stat.st_size);
                           download_successful = true;
                       }
                   } else {
                       LOG_E(TAG, "Failed to get file status for %s", file_path.c_str());
                       ++epo_download_retry;
                   }
               }
               if (!download_successful) {
                   LOG_E(TAG, "Failed to download file %s after 3 retries", file_path.c_str());
                   curl_easy_cleanup(curl);
                   curl_global_cleanup();
                   return false;
               }
           }
           curl_easy_cleanup(curl);
           curl_global_cleanup();
           return true;
       } else {
           LOG_E(TAG, "Failed to initialize CURL.");
           curl_global_cleanup();
           return false;
       }

    } else {
        LOG_E(TAG, "Internet is not available.");
        curl_global_cleanup();
        return false;
    }   
}

ssize_t SerialPortWrite(const unsigned char* buffer, size_t size, int fd) {
    if (fd < 0) {
        fprintf(stderr, "Serial port not initialized");
        return -1;
    }
    return write(fd, buffer, size);
}

ssize_t SerialPortRead(unsigned char* buffer, size_t size, int fd) {
    if (fd < 0) {
        fprintf(stderr, "Serial port not initialized");
        return -1;
    }
    return read(fd, buffer, size);
}

ssize_t SerialPortReadByte(char* byte, int fd) {
    if (fd < 0) {
        fprintf(stderr, "Serial port not initialized");
        return -1;
    }
    // Read exactly one byte
    return read(fd, byte, 1);
}

size_t ql_Read_EPO_File(const char* File_Path,char* Buffer,int Packet_Num)
{

    FILE *epo_fd = fopen(File_Path, "rb");
    size_t read_len = 0;
    int file_seek;

    if (epo_fd == NULL)
    {
        LOG_I(TAG,"open epo file fail\r");
        return 0;
    }
    file_seek = fseek(epo_fd, Packet_Num * 72, SEEK_SET);
    if (file_seek < 0)
    {
        LOG_I(TAG,"fseek epo file fail\r");
        return 0;
    }
    read_len = fread(Buffer, 1, 72, epo_fd);
    if (read_len == 0)
    {
        LOG_I(TAG,"read 0 byte\r");
        return 0;
    }

    fclose(epo_fd);
    return read_len;
}

unsigned char ql_Check_Sum(unsigned char* Buffer,int Length)
{
    unsigned char check_sum = 0;
    for (int i = 0; i < Length; i++)
    {
        check_sum ^= Buffer[i];
    }
    return check_sum;
}

int ql_Encode_data(short Msg_ID,unsigned char* Buffer,int Packet_Num, const char * File_Path,char Constellation_ID)
{
    int i = 0;
    size_t read_length;
    char temp_buf[72] = {0};
    if (Buffer == NULL)
    {
        printf("para error\r\n");
        return 0;
    }
    //Head
    Buffer[i++] = HEAD & 0x00FF;
    Buffer[i++] = HEAD >> 8 & 0xFF;
    //Msg_ID
    Buffer[i++] = Msg_ID & 0x00FF;
    Buffer[i++] = Msg_ID >> 8 & 0xFF;
    switch (Msg_ID)
    {
        case START_ID:
        case END_ID:
        {
            //length
            Buffer[i++] = 0x01;
            Buffer[i++] = 0x00;
            //Data
            Buffer[i++] = Constellation_ID;
        }break;

        case DATA_ID:
        {
            //length 
            read_length = ql_Read_EPO_File(File_Path, temp_buf, Packet_Num);
            Buffer[i++] = read_length & 0x00FF;
            Buffer[i++] = read_length >> 8 & 0xFF;
            //Data
            memcpy(&Buffer[i], temp_buf, read_length);
            i += read_length;
        }break;
        default:
            break;
    }
    //Check_sum
    Buffer[i++] = ql_Check_Sum(&Buffer[2], i - 2);
    //End
    Buffer[i++] = TAIL & 0x00FF;
    Buffer[i++] = TAIL >> 8 & 0xFF;
    return i;
}

bool epo_flash_wait_ack(unsigned char* ack, int count, int timeout, int fd)
{
    int length = 0;
    unsigned char buff[512] = { 0 };
    clock_t start_timer = clock();
    do
    {
        char chr = 0;
        length = 0;
        bool is_find_head = false;
        memset(buff, 0, length);

        do
        {
            SerialPortReadByte(&chr,fd);
            buff[length++] = chr;
            if (length >= 512)
            {
                break;
            }
            if ((is_find_head != true) && (length >= 2) && (buff[length - 1] == 0x24) && (buff[length - 2] == 0x04))
            {
                is_find_head = true;
                length = 2;
                buff[0] = 0x04;
                buff[1] = 0x24;
            }
            else if ((length >= 2)  && (buff[length - 1] == 0x44) && (buff[length - 2] == 0xAA))
            {
                break;
            }

        } while (true);

        for (int i = 0; i < count; i++)
        {
            if ((unsigned char)ack[i] != (unsigned char)buff[i])
            {
                break;
            }
            if (i >= count - 1)
            {
                return true;
            }
        }

    } while ((clock() - start_timer) < timeout);
    return false;
}

bool encode_and_send(short MSG_ID, int packet_num, const char *File_Path, unsigned char* ack, int size_of_start_ack, int fd, const string& ack_tag) {
    unsigned char epo_data[128];
    int packet_len; 
    char con_id = GPS;
    int ack_size =size_of_start_ack;
    LOG_I(TAG, "Ack size = %d", ack_size);
    packet_len = ql_Encode_data(MSG_ID, epo_data, packet_num, File_Path, con_id);
    pthread_mutex_lock(&gps_port_write_mutex);
    SerialPortWrite(epo_data, packet_len, fd);
    pthread_mutex_unlock(&gps_port_write_mutex);
    if (epo_flash_wait_ack(ack, ack_size, ACK_TIMEOUT, fd)) {
        LOG_I(TAG, "epo %s ack received", ack_tag.c_str());
        return true;
    } else {
        LOG_I(TAG, "send epo %s ack time out", ack_tag.c_str());
        return false;
    }
}

bool raise_ack_fail_ce(){
    if(critical_event == false) return false;
    else{
        string ErrMsg = "EPO files failed to load to module";
        LOG_I(TAG, ErrMsg.c_str());
        nd_service_obj->send_err_msg(SM_E_GPS_EPO_MODULE_LOADING_FAIL, 0, ErrMsg);
        critical_event = false;
        return true;
    }
}

void epo_try_rewrite(int fd){
    unsigned char erase_ack_buffer[512];
    if(try_rewrite < 6){
        LOG_I(TAG, "So Erasing and rewriting data");
        pthread_mutex_lock(&gps_port_write_mutex);
        SerialPortWrite((unsigned char*)epo_erase_command.c_str(), epo_erase_command.length(), fd);
        pthread_mutex_unlock(&gps_port_write_mutex);
        SerialPortRead(erase_ack_buffer, sizeof(erase_ack_buffer), fd);
        if(memcmp(erase_ack_buffer, epo_erase_ack.c_str(), epo_erase_ack.length()) == 0){
            LOG_I(TAG, "Erase data success");
        }
        try_rewrite++;       
    }
    else{
        LOG_I(TAG, "Raise a critical event, Max retry reached for erasing and rewriting data");
        critical_event = true;       
    }
    return;
 }

bool Send_EPO(const char* File_Path, int fd, int file_num, int no_of_epofiles)
{
    // LOG_I(TAG, "Entered Send_EPO()..");
    short msg_id;
    int total_packet_num;
    int packet_num = 0;
    struct stat file_property;
    int state;
    int progress = 0;
    intmax_t file_size;


    state = stat(File_Path, &file_property);
    if (state != 0)
    {
        LOG_I(TAG,"file property get fail");
    }
    file_size = file_property.st_size;
    LOG_I(TAG, "file size = %d", file_size);
    total_packet_num = file_property.st_size % 72 ? file_property.st_size / 72 + 1 : file_property.st_size / 72;
    LOG_I(TAG, "total packet num = %d", total_packet_num);

    unsigned char start_ack[] ={0x04, 0x24, 0xE8, 0x03, 0x04, 0x00, 0xB0, 0x04, 0x00, 0x00, 0x5B, 0xAA, 0x44};
    unsigned char data_ack[] = {0x04, 0x24, 0xE8, 0x03, 0x04, 0x00, 0xB1, 0x04, 0x00, 0x00, 0x5A, 0xAA, 0x44 };
    unsigned char end_ack[] = {0x04, 0x24, 0xE8, 0x03, 0x04, 0x00, 0xB2, 0x04, 0x00, 0x00, 0x59, 0xAA, 0x44 };

    int size_of_start_ack = sizeof(start_ack);
    int num_of_ele_start_ack = sizeof(start_ack) / sizeof(start_ack[0]);

    //send start
    if(encode_and_send(START_ID, 0, NULL, start_ack, size_of_start_ack, fd, "START") == false){
        epo_try_rewrite(fd);
        return false;
    }

    //send data
    for (packet_num = 0; packet_num < total_packet_num; packet_num++)
    {
        if(encode_and_send(DATA_ID, packet_num, File_Path, data_ack, size_of_start_ack, fd, "DATA") == false){
            epo_try_rewrite(fd);
            return false;
        }
    }

    //send end
    if(encode_and_send(END_ID, 0, NULL, end_ack, size_of_start_ack, fd, "END") == false){
        epo_try_rewrite(fd);
        return false;
    }


    LOG_I(TAG,"send complete for %d", file_num);
    if(file_num == no_of_epofiles){
        std::ofstream download_time_ofs("/home/ubuntu/.nddevice/.epo_file_download_time", std::ios::out | std::ios::trunc);
        download_time_ofs << epo_download_time << std::endl;
        LOG_I(TAG, "Download time written to file: /home/ubuntu/.nddevice/.epo_file_download_time, and the time is: %ld in secs", epo_download_time);
        download_time_ofs.close();
    }
    return true;
}