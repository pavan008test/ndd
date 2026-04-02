from ctypes import *
import time
import os
import subprocess
import traceback

libsys=cdll.LoadLibrary('/usr/lib64/libsys.so')

def read_fan_speed():
    fan_tach_enable = libsys.fan_tach_enable
    fan_rpm_read = libys.fan_rpm_read
    tach_en=c_int(1)
    tach_dis=c_int(0)  
    fan_tach_enable(tach_en)
    sleep(2)
    fan_rpm = fan_rpm_read()
    fan_tach_enable(tach_dis)
    print (' FAN_RPM  :%d'fan_rpm)
    return fan_rpm 


def main():
    try:
        read_fan_speed()
    except:
        print ("Cannot read FAN RPM")
        #on any exception do pre_revD way of lumia gpio toggle
        print(traceback.format_exc())

if __name__=="__main__":
    main()
