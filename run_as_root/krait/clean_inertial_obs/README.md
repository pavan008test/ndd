Taks
This task is created fix the issue found under OS release 10.4.3.

Issue found in above mentioned OS version:
--> Unnecessary recursive soft link file [name: observations] present under /data/nd_files/observations/
--> Unnecessary directory [name: inertial_obs_temp] present present under /data/nd_files/observations/

fix:
created a run_as_root task that will execute on OTA update and check if the above mentioned file and directory is found then unlink and delete.
