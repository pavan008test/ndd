
Path added in wgetrc to validate ca certificates. This is same as curl cert path
In 0.4.9 we are enforcing ssl check on all endpoints*, wget seems to validate certificate from a different path. Below change is required so that version check & OTA update should not fail.
Command used:
#grep -qxF 'ca-certificate = /etc/ssl/certs/ca-certificates.crt' /etc/wgetrc || echo 'ca-certificate = /etc/ssl/certs/ca-certificates.crt' >> /etc/wgetrc
-q be quiet. Suppress grep output
-x match the whole line
-F pattern is a plain string

