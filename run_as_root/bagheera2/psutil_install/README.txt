Task_name: psutil_install
Description: To install the psutil for the root user.
To install the psutils offline psutil whl file should be placed in task and that can be installed using pip install -I <psutil whl filename>
Steps: 
1. Created the psutil_install task in run_as_root
2. Copied psutil whl into that folder.
3. Created run.sh file to run the install command


