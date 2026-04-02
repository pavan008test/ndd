import os
import sys
import json

def readData(filePath):
    data = None
    with open(filePath, 'r') as f:
        data = json.load(f)
    return data

def writeToFile(filepath, data):
    with open(filepath, 'w') as f:
        f.write(data + "\n")
        f.close()

def main():
    arguments = sys.argv
    registerDeviceResultsFile = arguments[1]

    jsonResponse = readData(registerDeviceResultsFile)

    if jsonResponse['response'] == True:
        clientId = jsonResponse['data']['clientId']
        certArn = jsonResponse['data']['certArn']
        certificatePemFileContent = jsonResponse['data']['certPem']
        writeToFile(arguments[2]+"/certificate.pem.crt", certificatePemFileContent)
        perKeyPemFileContent = jsonResponse['data']['prkey']
        writeToFile(arguments[2]+"/private.pem.key", perKeyPemFileContent)

    else:
        print ("Device registration failed")

if __name__ == "__main__":
  sys.exit(main())
