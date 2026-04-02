#!/bin/sh

#Merge the dbs back incase of OTA failure
sqlite3 "" << EndOfCommands
ATTACH '/home/ubuntu/.nddevice/db/healthstats.db' AS db1;
ATTACH '/home/ubuntu/.nddevice/db/udid.db' AS db2;

CREATE TABLE IF NOT EXISTS db1.UDID_TABLE( \
ID INTEGER PRIMARY KEY  AUTOINCREMENT, \
UDID            INT    NOT NULL, \
UDID_JSON       TEXT    DEFAULT '{"drive_start": 0, "drive_end": 0, "rtc_jump_from": 0, "rtc_jump_to": 0}',\
STATUS          INT     DEFAULT 1,\
BOOT_TIME       INT     DEFAULT 0);

INSERT INTO db1.UDID_TABLE SELECT * FROM db2.UDID_TABLE;
DROP TABLE db2.UDID_TABLE;
EndOfCommands

#Delete the newly created DB by split.sh
rm /home/ubuntu/.nddevice/db/udid.db
