#!/usr/bin/python3

import os
import os.path
import configparser
import sys
import traceback

PNAME = "AwsIotWrapper"
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

def main():
    if len(sys.argv) <2:
        print ("Usage: AwsIotWrapper.py <certificate-folder-path>")
        return 1

    root = sys.argv[1]

    certpath = root + "/certificate/"
    if not os.path.exists(certpath):
        print (certpath + "does not exist")
        return 1

    configpath = "/home/ubuntu/config/deviceconfig.ini"
    if not os.path.exists(configpath) :
        print (configpath + " does not exist")
        return 1

    nddevicepath = root+"/nddevice.ini"
    if not os.path.exists(nddevicepath) :
        print (nddevicepath + " does not exist")
        return 1
    
    exepath = root+"/latest/service/awsiot/"
    if not os.path.exists(exepath+"/AwsIot"):
        print ("AwsIot executable not found at: "+exepath)
        return 1

    clougconfigpath = root+"/latest/cloudconfig.ini"
    if not os.path.exists(clougconfigpath) :
        print (clougconfigpath + " does not exist")
        return 1

    config = configparser.ConfigParser()
    config.read(configpath)

    deviceId = config.get('identity', 'deviceId')
    sessionId = config.get('identity', 'sessionId')
    deviceType = config.get ('identity','deviceType')
    if deviceType == "":
        print ("cannot get device type. Setting to bagheera")
        deviceType = "bagheera"



    if deviceId == "" or sessionId == "":
        print ("Cannot ascertain deviceId, exiting...")
        return

    cloud_config = configparser.ConfigParser()
    cloud_config.read(clougconfigpath)
    server = cloud_config.get('cloud', 'server')
    ser = cloud_config.get(server,'injestion') 
    api_ver = cloud_config.get('cloud','injection-version')    
    aws_server = cloud_config.get(server,'awsiot')

    if ser == "" or api_ver == "" or aws_server == "":
        print ("Cannot ascertain server id and api_version")
        return 1

    nddevice = configparser.ConfigParser()
    nddevice.read(nddevicepath)
    dev_ver = nddevice.get('version', 'nddevice')
    
    if dev_ver == "":
        print ("Cannot ascertain device version")
        return 1

    if not os.path.exists(certpath+"root-CA.crt") :
        print ("Root certificate does not exist")
        return

    print ("Found root certificate ...")
    
    retry_cnt = 0;
    while (not os.path.exists(certpath+"certificate.pem.crt") ) or (not os.path.exists(certpath+"private.pem.key") ):
        print ("Cannot find keys or certificate")

        headerStatus, authHeader = get_auth_token()

        if(headerStatus == "0") :
            #DEADLOCK:: The key will never recover as AWS IoT will not be up unless the below cloud call is successful
            #As discussed with cloud team, device RMA will be done as whitelisting this API may be a loophole
            print ("Corrupted jwt, Proceeding anyway for API call...")

        os.system("rm -f "+certpath+"temp.txt");
        curl_data = '\'{ "session_id": "%s", "device_id": "%s", "ver": "1.0", "deviceversion": \"' % (sessionId, deviceId) + dev_ver + '\", "deviceType": "bagheera", "imei": "",  "certArn": null }\''

        curl_cmd = "curl -X POST -H \"X-DeviceType: %s\" -H \"Content-Type: application/json\" -H \"X-DeviceId: %s\" -H \"%s\" --data " %(deviceType, deviceId, authHeader) + curl_data + " " + ser + "/" + api_ver+"/device/register > " + certpath + "temp.txt"
        print (curl_cmd)

        os.system( curl_cmd);
        os.system(exepath+"registerDevice "+certpath+"temp.txt "+certpath);

        if retry_cnt == 10:
            print ("Giving up...")
            return

        retry_cnt = retry_cnt + 1

    print ("Found root keys and certificates ...")
    exec_iot = exepath+"/AwsIot " + deviceId + " " +sessionId+ " " + server + " " + certpath + " " + aws_server
    print (exec_iot)

    os.system(exec_iot)

    return

if __name__ == '__main__':
    main();

