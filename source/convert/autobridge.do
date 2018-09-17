#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Special setup for autobridge.
# This is invoked by all.do .
#

# These get us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf printenv "$1" ; }
list_interfaces() { read_rc autobridge_interfaces || true ; }
get_var1() { read_rc autobridge_"$2" || true ; }

redo-ifchange rc.conf general-services "autobridge@.service" autobridge.helper

r="/var/local/sv"
e="--no-systemd-quirks --escape-instance --bundle-root"

find "$r/" -maxdepth 1 -type d -name 'autobridge@*' -print0 |
xargs -0 system-control disable --
system-control disable autobridge-log

list_interfaces |
while read -r i
do
	test -z "$i" && continue
	service="autobridge@$i"
	echo "${service}:" >> "$3"
	system-control convert-systemd-units $e "$r/" "./${service}.service"
	mkdir -p -m 0755 -- "$r/${service}/service/env"
	system-control preset "${service}"
	system-control set-service-env "${service}" patterns "`get_var1 \"$i\"`"
	if system-control is-enabled "${service}"
	then
		echo >> "$3" on "${service}"
	else
		echo >> "$3" off "${service}"
	fi
	system-control print-service-env "${service}" >> "$3"
	install -m 0555 -- autobridge.helper "$r/${service}/service/helper"
	rm -f -- "$r/${service}/log"
	ln -s -- "../../sv/autobridge-log" "$r/${service}/log"
	system-control preset autobridge-log
done
