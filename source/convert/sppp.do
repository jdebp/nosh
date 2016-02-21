#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
#
# Special setup for sppp.
# This is invoked by general-services.do .
#

# These get us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf "`which printenv`" "$1" ; }
list_interfaces() { read_rc sppp_interfaces || true ; }
get_args() { read_rc spppconfig_$1 || true ; }

redo-ifchange rc.conf general-services "spppcontrol@.service"

r="/var/local/sv"
e="--no-systemd-quirks --escape-instance --bundle-root"

list_interfaces |
while read -r i
do
	test -z "$i" && continue
	service="spppcontrol@$i"
	system-control convert-systemd-units $e "$r/" "./${service}.service"
	mkdir -p -m 0755 -- "$r/${service}/service/env"
	system-control preset "${service}"
	system-control set-service-env "${service}" args "`get_args \"$i\"`"
	if system-control is-enabled "${service}"
	then
		echo >> "$3" on "${service}"
	else
		echo >> "$3" off "${service}"
	fi
	system-control print-service-env "${service}" >> "$3"
	rm -f -- "$r/${service}/log"
	ln -s -- "../../sppp-log" "$r/${service}/log"
done
