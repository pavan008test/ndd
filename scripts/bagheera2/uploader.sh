#!/bin/sh

#script for invoking the uploader service

$(grep ND_INPUT_PATH /home/ubuntu/.bashrc)

sudo /home/ubuntu/.nddevice/latest/service/uploader/uploader

echo END OF SCRIPT



