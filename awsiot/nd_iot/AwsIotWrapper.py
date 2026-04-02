#!/usr/bin/python3

import os
import os.path
import configparser
import sys
import traceback

import subprocess
import glob
import shutil
import traceback
import hashlib
import time
import logging
import logging.handlers
from aws_iot_utils import *

PNAME = "IOT"

def get_auth_token() :
    try:
        import py_service_mon
        py_to_cpp_alias = py_service_mon.PY_Class_PY_TO_CPP
        py_to_cpp_obj = py_to_cpp_alias("CTOR from python class PY_Class_PY_TO_CPP")
        py_to_cpp_alias.py_get_cpp_service_obj(py_to_cpp_obj, PNAME.encode())

        token = py_service_mon.PY_Class_PY_TO_CPP.py_get_jwt_from_cpp(py_to_cpp_obj)
        if sys.version_info.major == 3:
            token = token.decode()
        splited_tokens = token.split(';')
        return splited_tokens[0], splited_tokens[1]
    except Exception as error:
        print("Exception: AwsIotWrapper: %s" %traceback.format_exc())
        return "0", "X-Device-JWT: ERROR"

def send_critical_info(error_code, error_msg) :
    try:
        import py_service_mon
        py_to_cpp_alias = py_service_mon.PY_Class_PY_TO_CPP
        py_to_cpp_obj = py_to_cpp_alias("CTOR from python class PY_Class_PY_TO_CPP")
        py_to_cpp_alias.py_get_cpp_service_obj(py_to_cpp_obj, PNAME.encode())
        py_to_cpp_alias.py_send_err_msg_to_cpp(py_to_cpp_obj, error_code, -1, error_msg.encode())

    except Exception as error:
        print("Exception : AwsIotWrapper: %s" %traceback.format_exc())


def generate_checksum(logger, fileName):
    try:
        with open(fileName, 'rb') as f:
            data = f.read()
            md5sum = hashlib.md5(data).hexdigest()
    except Exception as e:
        md5sum = "error"
        logger.error(traceback.format_exc())

    return md5sum


def restore_backup(logger, filename) :
    logger.info("Restoring backup: %s"%filename)
    print ("Restoring backup: " + filename)

    backup_file_pattern = "/home/ubuntu/backup/" + filename + "*"
    backup_file_list = glob.glob(backup_file_pattern)

    if len(backup_file_list) != 1:
        logger.error("Can't restore backup. Backup count  %s"%len(backup_file_list))
        return False

    backup_file = backup_file_list[0]

    #validate backup file
    checksum = generate_checksum(logger, backup_file)
    if checksum not in backup_file:
        logger.error("Corrupted backup file %s"%backup_file)
        print ("Corrupted backup file " + backup_file)
        return False

    #restore backup
    cert_folder = "/home/ubuntu/.nddevice/certificate/"
    if (os.path.exists(cert_folder + filename)) :
        os.remove(cert_folder + filename)
    shutil.copy(backup_file, os.path.join(cert_folder, filename))

    #verify the checksum of the restored file
    if (generate_checksum(logger, (cert_folder + filename)) == checksum) :
        logger.info("Backup successfull")
        print ("Backup successfull")
        return True
    else :
        logger.error("Could not restore backup")
        print ("Could not restore backup")
        return False

def main():
    #Logger Setup
    logger = logger_setup("awsiot", "AwsIotWrapper_")
    logger.info("::====================::AwsIotWrapper Starting::====================::")

    if len(sys.argv) <2:
        logger.error("Usage: AwsIotWrapper.py <certificate-folder-path>")
        print ("Usage: AwsIotWrapper.py <certificate-folder-path>")
        return 1

    root = sys.argv[1]

    certpath = root + "/certificate/"
    if not os.path.exists(certpath):
        logger.error("%s does not exist"%certpath)
        print (certpath + "does not exist")
        return 1

    configpath = "/home/ubuntu/config/deviceconfig.ini"
    if not os.path.exists(configpath) :
        logger.error("%s does not exist"%configpath)
        print (configpath + " does not exist")
        return 1

    nddevicepath = root+"/nddevice.ini"
    if not os.path.exists(nddevicepath) :
        logger.error("%s does not exist"%nddevicepath)
        print (nddevicepath + " does not exist")
        return 1

    exepath = root+"/latest/service/awsiot/"
    if not os.path.exists(exepath+"/AwsIot"):
        logger.error("AwsIot executable not found at: %s"%exepath)
        print ("AwsIot executable not found at: "+exepath)
        return 1

    clougconfigpath = root+"/latest/cloudconfig.ini"
    if not os.path.exists(clougconfigpath) :
        logger.error("%s does not exist"%clougconfigpath)
        print (clougconfigpath + " does not exist")
        return 1

    config = configparser.ConfigParser()
    config.read(configpath)

    deviceId = config.get('identity', 'deviceId')
    sessionId = config.get('identity', 'sessionId')
    deviceType = config.get ('identity','deviceType')
    if deviceType == "":
        logger.error("cannot get device type. Setting to bagheera")
        print ("cannot get device type. Setting to bagheera")
        deviceType = "bagheera"



    if deviceId == "" or sessionId == "":
        logger.error("Cannot ascertain deviceId, exiting...")
        print ("Cannot ascertain deviceId, exiting...")
        return 1

    cloud_config = configparser.ConfigParser()
    cloud_config.read(clougconfigpath)
    server = cloud_config.get('cloud', 'server')
    ser = cloud_config.get(server,'injestion')
    api_ver = cloud_config.get('cloud','injection-version')
    aws_server = cloud_config.get(server,'awsiot')

    if ser == "" or api_ver == "" or aws_server == "":
        logger.error("Cannot ascertain server id and api_version")
        print ("Cannot ascertain server id and api_version")
        return 1

    nddevice = configparser.ConfigParser()
    nddevice.read(nddevicepath)
    dev_ver = nddevice.get('version', 'nddevice')

    if dev_ver == "":
        logger.error("Cannot ascertain device version")
        print ("Cannot ascertain device version")
        return 1

    if not os.path.exists(certpath+"root-CA.crt") :
        logger.error("Root certificate does not exist")
        print ("Root certificate does not exist")

        restored = restore_backup(logger, "root-CA.crt")
        if not restored :
            SM_E_AWS_ROOT_CERT_NOT_FOUND = 80004
            send_critical_info(SM_E_AWS_ROOT_CERT_NOT_FOUND, "Root cert not found and restore failed")
            return 1

    logger.info("Found root certificate ...")
    print ("Found root certificate ...")

    retry_cnt = 0;
    while (not os.path.exists(certpath+"certificate.pem.crt") ) or (not os.path.exists(certpath+"private.pem.key") ):
        logger.info("Cannot find keys or certificate")
        print ("Cannot find keys or certificate")
        headerStatus, authHeader = get_auth_token()

        if(headerStatus == "0") :
            #DEADLOCK:: JWT keys are corrupted and AWS IoT service won't start
            #Need to RMA the device as whitelisting this API may lead to a security loophole
            SM_E_AWS_CORRUPT_JWT_AUTH_KEY = 80003
            send_critical_info(SM_E_AWS_CORRUPT_JWT_AUTH_KEY, "JWT auth key corrupted")
            logger.error("Corrupted jwt, Proceeding anyway for API call...")

        os.system("rm -f "+certpath+"temp.txt");
        curl_data = '\'{ "session_id": "%s", "device_id": "%s", "ver": "1.0", "deviceversion": \"' % (sessionId, deviceId) + dev_ver + '\", "deviceType": "bagheera", "imei": "",  "certArn": null }\''

        curl_cmd = "curl -X POST -H \"X-DeviceType: %s\" -H \"Content-Type: application/json\" -H \"X-DeviceId: %s\" -H \"%s\" --data " %(deviceType, deviceId, authHeader) + curl_data + " " + ser + "/" + api_ver+"/device/register > " + certpath + "temp.txt"
        print (curl_cmd)

        os.system( curl_cmd);
        os.system(exepath+"registerDevice "+certpath+"temp.txt "+certpath);

        if retry_cnt == 10:
            logger.error("Giving up...")
            print ("Giving up...")
            SM_E_AWS_INVALID_OR_CORROUPT_CERT = 80002
            send_critical_info(SM_E_AWS_INVALID_OR_CORROUPT_CERT, "AWS cert or key download failed")
            return

        retry_cnt = retry_cnt + 1

    logger.info("Found root keys and certificates ...")
    print ("Found root keys and certificates ...")
    exec_iot = exepath+"/AwsIot " + deviceId + " " +sessionId+ " " + server + " " + certpath + " " + aws_server
    print (exec_iot)

    os.system(exec_iot)

    return

if __name__ == '__main__':
    main();

