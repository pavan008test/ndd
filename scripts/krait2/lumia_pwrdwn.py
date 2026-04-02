from ctypes import *
import time
import os
import subprocess
import traceback

libsys=cdll.LoadLibrary('/usr/lib64/libsys.so')

def toggle_krait_pre_revD_lumia():
    device_lumia_gpio = libsys.gpio_set_value
    qcs_write=c_int(26)
    size_gp=c_int(8)
    set_off=c_char(1)

    device_lumia_gpio(byref(qcs_write),byref(set_off), size_gp)
    print('POWER DOWN Lumia via device GPIO')

def toggle_krait_revD_lumia():
    msp_lumia_gpio = libsys.msp_write_lumia_disable_gpio_state
    set_off=c_int(1)

    msp_lumia_gpio(set_off)
    print('POWER DOWN Lumia via MSP GPIO')

def get_krait_board_revision():
    device_board_gpio = libsys.gpio_get_value
    board_gpio=(c_int*3)()
    board_gpio[0]=c_int(143)
    board_gpio[1]=c_int(68)
    board_gpio[2]=c_int(145)
    size_gp=c_int(3)
    gpio_val=(c_ubyte*3)()

    device_board_gpio(byref(board_gpio),byref(gpio_val), size_gp)
    board_rev_gpio_val=((gpio_val[2] << 2) | (gpio_val[1] << 1) | (gpio_val[0]))
    board_revision = chr(ord('A')+board_rev_gpio_val)
    return board_revision

def is_krait_2_board():
    stream = os.popen('read_hwinfo_emmc')
    out = stream.read()
    result = out.find('Krait2.0_A')

    if result > 0:
     print ('HW Sub Revision: Krait 2.0')
    else:
     print ('HW Sub Revision: Krait 1.0')
    return result


def main():
    try:
        #Extract HW Revision
        hw_rev = get_krait_board_revision()
        print ("HW Revision: Rev_%s"%hw_rev)
        is_krait2 = is_krait_2_board()

        if is_krait2 > 0 or hw_rev >= 'D':
             toggle_krait_revD_lumia()
        else:
             toggle_krait_pre_revD_lumia()
    except:
        print ("Cannot determine board revision")
        #on any exception do pre_revD way of lumia gpio toggle
        print(traceback.format_exc())
        toggle_krait_revD_lumia() 

if __name__=="__main__":
    main()
