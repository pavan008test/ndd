#ifndef AGNSS_H
#define AGNSS_H

#include <stdio.h>
#include <string>

using namespace std;

bool check_internet_exist();
string get_imei();
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp);
bool agnss_init(int no_of_epofiles);
ssize_t SerialPortReadByte(char* byte, int fd);
ssize_t SerialPortRead(unsigned char* buffer, size_t size, int fd); 
ssize_t SerialPortWrite(const unsigned char* buffer, size_t size, int fd);
size_t ql_Read_EPO_File(const char* File_Path,char* Buffer,int Packet_Num);
unsigned char ql_Check_Sum(unsigned char* Buffer,int Length);
int ql_Encode_data(short Msg_ID,unsigned char* Buffer,int Packet_Num, char * File_Path,char Constellation_ID);      
bool epo_flash_wait_ack(unsigned char* ack, int ack_len, int timeout);
bool ack_log (unsigned char* ack, int count, int fd);
bool raise_ack_fail_ce();
void epo_try_rewrite();
bool Send_EPO(const char* File_Path, int fd, int file_num, int no_of_epofiles);


#endif