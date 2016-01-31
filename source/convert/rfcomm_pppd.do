#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
#
# Special setup for rfcomm_pppd.
# This is invoked by general-services.do .
#

# These get us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf "`which printenv`" "$1" ; }
list_interfaces() { read_rc rfcomm_pppd_server_profile || true ; }
get_var() { read_rc rfcomm_pppd_server_"$1"_"$2" || read_rc rfcomm_pppd_server_"$2" || true ; }

redo-ifchange rc.conf general-services

r="/var/local/sv"
e="--no-systemd-quirks --escape-instance --bundle-root"

redo-ifchange "rfcomm_pppd@.service"

list_interfaces |
while read -r i
do
	test -z "$i" && continue
	service="rfcomm_pppd@$i"
	system-control convert-systemd-units $e "$r/" "./${service}.service"
	mkdir -p -m 0755 -- "$r/${service}/service/env"
	system-control preset "${service}"
	system-control set-service-env "${service}" dun "`get_var \"$i\" register_dun`"
	system-control set-service-env "${service}" sp "`get_var \"$i\" register_sp`"
	system-control set-service-env "${service}" channel "`get_var \"$i\" channel`"
	system-control set-service-env "${service}" addr "`get_var \"$i\" bdaddr`"
	if system-control is-enabled "${service}"
	then
		echo >> "$3" on "${service}"
	else
		echo >> "$3" off "${service}"
	fi
	system-control print-service-env "${service}" >> "$3"
	rm -f -- "$r/${service}/log"
	ln -s -- "../../rfcomm_pppd-log" "$r/${service}/log"
done
