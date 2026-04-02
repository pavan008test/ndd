echo "Start of internal_buffer folder clean_up"
ls -ltd $(find /home/iriscli/internal_buff/) | awk '{ if (!system("test -f " $9)) { size += $5; if (size > (2.8)*2^30) print $9} }' | xargs -i rm {}
echo "End of internal_buffer folder clean_up"
