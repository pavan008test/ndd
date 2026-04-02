#!/usr/bin/python3

import os
import os.path
import time
import logging
import logging.handlers

LOG_PATH = '/home/ubuntu/.nddevice/log'



class OnMarkRotatingFileHandler(logging.handlers.TimedRotatingFileHandler):

    def __init__(self, filename, when='h', interval=1, backupCount=0, encoding=None, delay=False, utc=False):
        super(OnMarkRotatingFileHandler, self).__init__(filename, when, interval, backupCount, encoding, delay, utc)


    def floor_to(self, num, scale):
        return int(num/scale) * scale


    def computeRollover(self, currentTime):
        temp_result = super(OnMarkRotatingFileHandler, self).computeRollover(currentTime)
        if not self.when.startswith('W'):
            result = self.floor_to(temp_result, self.interval)
        else:
            result = temp_result    # need to find out the first date of time (is it 1970/1/1?), what weekday that is.

        return result



"""
Logger info Setup
"""
def logger_setup(log_dir, log_file_prifix):
    path = os.path.join(LOG_PATH, log_dir)
    if path is None or not os.path.exists(path):
        print("log path doesn't exist, creating it...")
        if(not path is None):
            os.makedirs(path)
            if os.path.exists(path):
                print("Created path for logs: %s"%path)
            else:
                print("Cannot create path for logs. Please check permission for: %s. Exiting!!"%path)
        else:
            print("No path specified for logs. Please correct the code. Exiting!!")

    curr_time = int(round(time.time() * 1000))
    LOG_FILENAME = os.path.join(path, log_file_prifix + str(curr_time) + ".log")
    LOG_LEVEL = logging.INFO  # Could be DEBUG, INFO, WARNING, ERROR, CRITICAL

    logger = logging.getLogger(__name__)
    formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')

    # create console handler and set level to debug
    ch = logging.StreamHandler()
    ch.setLevel(LOG_LEVEL)
    # add ch to logger
    logger.addHandler(ch)

    # add formatter to ch
    ch.setFormatter(formatter)

    th = OnMarkRotatingFileHandler(LOG_FILENAME, when='M', interval=10, backupCount=288, utc=True)
    th.setFormatter(formatter)
    logger.addHandler(th)
    logger.setLevel(LOG_LEVEL)
    return logger
