# Jira ID: DT-

## RUN.SH
1. Check if `md5sum` of `nvpmodel_t186.conf` and `nvpmodel_org.conf` in the RAR folder is the same and log it.
2. Take a backup of `/etc/nvpmodel/nvpmodel_t186.conf` by renaming it as `nvpmodel_t186_backup.conf`.
3. Log the output of `nvpmodel -q --verbose`.
4. Place the `nvpmodel_t186.conf` from the RAR folder into `/etc/nvpmodel`.
5. Run the command `nvpmodel -m 3`.
6. Log the output of `nvpmodel -q --verbose`.
7. Verify the following outputs:
   - `cat /sys/devices/system/cpu/cpu1/online` should be `1`.
   - `cat /sys/devices/system/cpu/cpu2/online` should be `1`.
   - `cat /sys/devices/17000000.gp10b/devfreq/17000000.gp10b/max_freq` should be `1122000000`.
8. Run the `systemd_override_b2.sh` script.
9. Reboot the system.

## REVERT.SH
1. Remove the current `/etc/nvpmodel/nvpmodel_t186.conf` and rename the backup back to its original name.
2. Run the command `nvpmodel -m 3`.
3. Log the output of `nvpmodel -q --verbose`.
4. Verify the following outputs:
   - `cat /sys/devices/system/cpu/cpu1/online` should be `0`.
   - `cat /sys/devices/system/cpu/cpu2/online` should be `0`.
   - `cat /sys/devices/17000000.gp10b/devfreq/17000000.gp10b/max_freq` should be `1122000000`.
5. Run the command `find /etc/systemd/system/ -type d -name "*.service.d" | xargs rm -rf`.
6. Reboot the system.

## Notes
Steps followed as above for the `run.sh` and `revert.sh`.
