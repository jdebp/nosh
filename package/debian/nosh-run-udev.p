# vim: set filetype=sh:
fan_in_logger "udev"
socket_only "udev"
service_only "udev-finish"
service_only "udev-settle"
service_only "udev-trigger-add@subsystems"
service_only "udev-trigger-add@devices"
