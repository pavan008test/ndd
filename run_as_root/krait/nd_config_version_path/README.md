Task
This task is created fix the issue migrating override versions from device_config to nddevice.

fix:
created a run_as_root task that will execute on OTA update and check if nd_config version is exist in the deviceconfig.ini if so we are setting nd_version and validity in nddevice.inii.
