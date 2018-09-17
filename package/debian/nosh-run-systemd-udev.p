# vim: set filetype=sh:
fan_in_logger "systemd-udev"
socket_only "systemd-udev"
service_only "udev-settle"
service_only "systemd-udev-trigger-add@subsystems"
service_only "systemd-udev-trigger-add@devices"
