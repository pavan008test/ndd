Whoopsie is a crash report service used by debian family
As a request of TMO we are disabling whoopsie crash reports
setting report_crashs=false in /etc/init.d/whoopsie will make sure the reports are not sent.


file_name=/etc/init.d/whoopsie
sed -i 's/.*report_crashes.*$//' $file_name ; echo  'report_crashes=False' >> $file_name
