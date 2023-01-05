
setup:
	echo 'append these policy to `/etc/dbus-1/system.d/bluetooth.conf`'
	echo ' <policy group="bluetooth">'
	echo '  <allow send_interface="org.bluez.LEAdvertisement1"/>'
	echo '  <allow send_interface="org.bluez.Agent1"/>'
	echo '  <allow send_interface="org.bluez.MediaEndpoint1"/>'
	echo '  <allow send_interface="org.bluez.MediaPlayer1"/>'
	echo '  <allow send_interface="org.bluez.Profile1"/>'
	echo '  <allow send_interface="org.bluez.LEAdvertisingManager1"/>'
	echo ' </policy>'
	echo 'then restart dbus by `systemctl reload dbus`'


