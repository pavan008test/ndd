#!/usr/bin/python3

import os
import re
from configparser import ConfigParser


"""
Function to update configuration in a file
"""
def set_config_to_path(configPath, section, option, value, addSection):

    read = False

    #check if config file exists
    if ( os.path.isfile(configPath)):
        configPath = os.path.realpath(configPath);
        config = ConfigParser();
        #try to read the value from ini file
        try:
            config.read(configPath)
            if config.has_section(section) :
                    #Update the new values
                config.set(section, option, value)
                read = True
            elif(addSection):
                config.add_section(section)
                    #Update the new values
                config.set(section, option, value)
                read = True
        except: #read failed due currupted ini file that can happen due to suddent power of during update
            print("Write Error if config %s"%configPath)

    if( read == False): #file not exist and needs to be creatred
        config = ConfigParser();
        config.add_section(section)
        config.set(section, option, value)
        read = True

    fo=open(configPath, "w+")
    config.write(fo) # Write update config

    print ("File: %s is modified to  state:%s " %(configPath, os.getpid()))
    print("Updated configuration in path %s,  %s-%s to %s"%(configPath, section, option, value))

    return read


"""
Function to read configuration from file
"""

def get_config_from_path(configPath, section, option):
    read = False
    result = "none"

    #check if config file exists
    if ( os.path.isfile(configPath)):
        configPath = os.path.realpath(configPath);
        config = ConfigParser();
        #try to read the value from ini file
        try:
            config.read(configPath)
            if config.has_section(section) and config.has_option(section, option):
                result = config.get(section, option)
                read = True
        except: #read failed due currupted ini file that can happen due to suddent power of during update
            print("Error in %s file, have to read it from original ini"%configPath)

    if( read == False ): #not able to read from config File then read from origial ini
        origConfigPath = re.sub("nddevice.ini", "/release/nddevice.ini", configPath)
        if(os.path.isfile(origConfigPath)):#First time case no config available means no software

            configPath = os.path.realpath(origConfigPath);
            config = ConfigParser();
            #try to read the value from original ini file
            try:
                config.read(configPath)
                if config.has_section(section) and config.has_option(section, option):
                    result = config.get(section, option)
                    read = True
            except: #read failure will not happen if happen we need to get the version from cloud or timebeing we can just mark it as 0.0.0
                print("Error in original %s file"%configPath)
        else: #First time case no config available means no software
            print("First time case no software found")
    return result



"""
        Main function
"""
def main():

    #folder for device related software
    ND_DEVICE_REL_PATH = os.environ.get('ND_DEVICE_REL_PATH')
    validity = get_config_from_path('/home/ubuntu/config/deviceconfig.ini', 'version', 'validity' )
    ndconfig_version = get_config_from_path('/home/ubuntu/config/deviceconfig.ini', 'version', 'nd_config')
    nddevice_config_path = "%s/nddevice.ini"%ND_DEVICE_REL_PATH
    if(ndconfig_version != "none" and validity != "none"):
        set_config_to_path(nddevice_config_path, 'version', 'nd_config',ndconfig_version, True)
        set_config_to_path(nddevice_config_path, 'version', 'validity', validity, True)
        
    
if __name__ == '__main__':
    main();

