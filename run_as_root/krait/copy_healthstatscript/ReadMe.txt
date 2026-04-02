HealthStats DB (/home/ubuntu/.nddevice/db/healthstats.db) contains both UDID and AH data.

Split.sh script creates a new udid.db and move the udid table from healthstats.db

                  healthstats.db
                (UDID and AH data)
                        /\
                       /  \
                      /    \
                     /      \
                    /        \
                   /          \
                  /            \
                 /              \
         healthstats.db         udid.db
        (AH Data)               (UDID data)

Must be triggered only once on this OTA update.

If case of any failure revert the db changes (merge) using merge_db.sh script.

