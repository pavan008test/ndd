import os
import sys
import json
import time
import logging
import logging.handlers
import hashlib
from aws_iot_utils import *
import py_nd_auth
import traceback

def readData(logger, filePath):
    data = None
    with open(filePath, 'r') as f:
        # Try block to handle crash in case f is not a valid JSON
        try:
            data = json.load(f)
        except Exception as e:
            error_msg = "JSON processing failed: {}".format(str(e))
            logger.error(error_msg)
    return data

def writeToFile(filepath, data):
    with open(filepath, 'w') as f:
        f.write(data + "\n")
        f.close()

def calculate_md5sum(file_path):
    """Calculate MD5 checksum of a file"""
    try:
        with open(file_path, 'rb') as f:
            data = f.read()
            return hashlib.md5(data).hexdigest()
    except Exception:
        return ""

def verify_and_operate_private_key(logger, plain_text_data, dest_path):
    """
    Verify encryption/decryption integrity before committing the encrypted file.
    
    Args:
        logger: Logger instance
        plain_text_data: The plain text private key data
        dest_path: Destination path for the encrypted file
        
    Returns:
        bool: True if verification succeeded and encryption is safe, False otherwise
    """
    temp_plain_file = "/dev/shm/temp_plain.txt"
    temp_encrypted_file = "/dev/shm/temp_operated.txt"
    temp_decrypted_file = "/dev/shm/temp_reoperated.txt"

    try:
        # Step 1: Write plain text data to temp file and calculate MD5
        with open(temp_plain_file, 'w') as f:
            f.write(plain_text_data)
        
        original_md5sum = calculate_md5sum(temp_plain_file)
        if not original_md5sum:
            logger.error("Failed to calculate MD5 sum of original data")
            return False
                
        # Step 2: Encrypt the temp plain file
        encrypt_status = py_nd_auth.nd_file_operate_to_file(temp_plain_file.encode(), temp_encrypted_file.encode())
        if encrypt_status != 0:
            logger.error("operate failed while verifying")
            return False
        
        # Step 3: Decrypt the encrypted file back to verify integrity. nd_file_reoperate_to_buff api is not exposed to python
        decrypt_status = py_nd_auth.nd_file_reoperate_to_file(temp_encrypted_file.encode(), temp_decrypted_file.encode())
        if decrypt_status != 0:
            logger.error("reoperate failed while verifying")
            return False
        
        # Step 4: Calculate MD5 of decrypted content and compare
        decrypted_md5sum = calculate_md5sum(temp_decrypted_file)
        if not decrypted_md5sum:
            logger.error("Failed to calculate MD5 sum of reoperated content")
            return False
        
        if original_md5sum != decrypted_md5sum:
            logger.error("MD5 mismatch!  Original: %s, reoperated: %s", original_md5sum, decrypted_md5sum)
            return False
        
        # Step 5: Verification passed - move the verified encrypted file to destination
        logger.info("MD5 verification passed - moving verified operated file to destination")
        try:
            import shutil
            shutil.move(temp_encrypted_file, dest_path)
            logger.info("Successfully moved verified operated file to: %s", dest_path)
        except Exception as e:
            logger.error("Failed to move operated file to destination: %s", str(e))
            return False
        
        return True
        
    except Exception as e:
        logger.error("Exception in verify_and_operate_private_key: %s", str(e))
        logger.error(traceback.format_exc())
        return False
    finally:
        # Cleanup all temporary files
        for temp_file in [temp_plain_file, temp_encrypted_file, temp_decrypted_file]:
            if os.path.exists(temp_file):
                try:
                    os.remove(temp_file)
                except:
                    pass

def encryptAndWriteToFileWithVerification(logger, destPath, data):
    """
    Does verification of encrytion/decryption process before final encryption.
    If verification fails, the file is left in plain text to avoid corruption.
    """
    # Try verified encryption first
    if verify_and_operate_private_key(logger, data, destPath):
        return True
    else:
        # Verification failed - write as plain text to avoid corruption
        logger.error("operate & verify failed")
        try:
            with open(destPath, 'w') as f:
                f.write(data + "\n")
            return True
        except Exception as e:
            logger.error("Failed to write in plain post operate failure: %s", str(e))
            return False

def main():
    #Logger Setup
    logger = logger_setup("awsiot", "registerDevice_")
    logger.info("::====================::registerDevice Starting::====================::")
    arguments = sys.argv
    registerDeviceResultsFile = arguments[1]

    jsonResponse = readData(logger, registerDeviceResultsFile)

    if jsonResponse['response'] == True:
        certificatePemFileContent = jsonResponse['data']['certPem']
        writeToFile(arguments[2]+"/certificate.pem.crt", certificatePemFileContent)
        perKeyPemFileContent = jsonResponse['data']['prkey']
        encryptAndWriteToFileWithVerification(logger, arguments[2]+"/private.pem.key", perKeyPemFileContent)
        logger.info("Device registration done")
        os.system("rm -f "+registerDeviceResultsFile) # Delete temp file on success
    else:
        logger.error("Device registration failed")
        print ("Device registration failed")

if __name__ == "__main__":
    sys.exit(main())