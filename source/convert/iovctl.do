#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Special setup for iovctl.
# This is invoked by all.do .
#

# These get us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf printenv "$1" ; }
get_var1() { read_rc iovctl_"$1" || true ; }
get_var2() { read_rc iovctl_"$1"_"$2" || read_rc iovctl_"$2" || true ; }

list_instances() { get_var1 "files" ; }

redo-ifchange rc.conf general-services "iovctl@.service"

r="/var/local/sv"
e="--no-systemd-quirks --bundle-root"

find "$r/" -maxdepth 1 -type d -name 'iovctl@*' -print0 |
xargs -0 system-control disable --

list_instances |
while read -r i
do
	service="iovctl@$i"

	system-control convert-systemd-units $e "$r/" "./${service}.service"
	install -d -m 0755 -- "$r/${service}/service/env"
	rm -f -- "$r/${service}/log"
	ln -s -- "../../../sv/iovctl-log" "$r/${service}/log"

	system-control preset iovctl-log "${service}"
	if system-control is-enabled "${service}"
	then
		echo >> "$3" on "${service}"
	else
		echo >> "$3" off "${service}"
	fi
	system-control print-service-env "${service}" >> "$3"
	echo >> "$3"
done
