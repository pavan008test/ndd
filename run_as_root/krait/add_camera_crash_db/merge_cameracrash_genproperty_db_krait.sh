#!/bin/sh

CRASH_DB="/data/nd_files/db/camera_crash.db"
GENPROP_DB="/data/nd_files/db/gen_property.db"

# Check if camera_crash.db exists
if [ ! -f "$CRASH_DB" ]; then
    echo "camera_crash.db not found. Exiting."
    exit 0
fi

# Check if CAMERA_CRASH_DB table exists inside camera_crash.db
TABLE_EXISTS=$(sqlite3 "$CRASH_DB" "SELECT name FROM sqlite_master WHERE type='table' AND name='CAMERA_CRASH_DB';")

if [ "$TABLE_EXISTS" != "CAMERA_CRASH_DB" ]; then
    echo "CAMERA_CRASH_DB table not found in camera_crash.db. Exiting."
    exit 0
fi

# Proceed with merging
sqlite3 "$GENPROP_DB" << EndOfCommands
ATTACH '$CRASH_DB' AS db2;

CREATE TABLE IF NOT EXISTS GENPROP (
    INDEXID INTEGER PRIMARY KEY AUTOINCREMENT,
    PROPERTY TEXT NOT NULL,
    DATA TEXT NOT NULL,
    TIME BIGINT DEFAULT 0,
    EPOCHTIME BIGINT DEFAULT 0
);

INSERT INTO GENPROP (PROPERTY, DATA, TIME, EPOCHTIME)
SELECT PROPERTY, DATA, TIME, EPOCHTIME FROM db2.CAMERA_CRASH_DB;

DROP TABLE db2.CAMERA_CRASH_DB;
DETACH db2;
EndOfCommands

# Remove the file only after successful merge
rm -f "$CRASH_DB"

