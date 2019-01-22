# vim: set filetype=sh:
# service list
service_with_dedicated_logger "VBoxService"
service_with_dedicated_logger "VBoxBalloonCtrl"
service_with_dedicated_logger "kmod@vboxguest"
service_with_dedicated_logger "kmod@vboxsf"
service_with_dedicated_logger "kmod@vboxvideo"
service_with_dedicated_logger "kmod@vboxadd"
target "virtualbox-guest"
