#!/bin/bash

FILE="/dev/shm/shmfile/CAM0.txt"  # Path to the file containing shmids

# Check if the file exists
if [ -f "$FILE" ]; then
	# Read the file line by line
	while IFS= read -r shmid
	do
		# Remove each shmid using ipcrm
		ipcrm -m "$shmid"
	done < "$FILE"

	echo "Shared memory segments removed successfully."

	# Remove the file
	rm "$FILE"
	echo "File $FILE removed."
else
	echo "File $FILE does not exist."
fi
