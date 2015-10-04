#!/bin/sh -e
#
# Special setup for sppp.
# This is invoked by general-services.do .
#

# These get us *only* the configuration variables, safely.
read_rc() { if type sysrc >/dev/null 2>&1 ; then sysrc -i -n "$1" ; else clearenv read-conf -oknofile /etc/rc.conf read-conf -oknofile /etc/rc.conf.local `which printenv` "$1" ; fi }
list_interfaces() { read_rc sppp_interfaces || true ; }
get_args() { read_rc spppconfig_$1 || true ; }

for i in /etc/rc.conf.local /etc/rc.conf
do
	if test -e "$i"
	then
		redo-ifchange "$i"
	else
		redo-ifcreate "$i"
	fi
done

r="/etc/service-bundles/services"
e="--etc-bundle --no-systemd-quirks --escape-instance --bundle-root"

redo-ifchange "spppcontrol@.service"

list_interfaces |
while read i
do
	test -z "$i" && continue
	sppp_service="spppcontrol@$i"
	system-control convert-systemd-units $e "$r/" "./${sppp_service}.service"
	system-control preset "${sppp_service}"
	system-control set-service-env "${sppp_service}" args "`get_args \"$i\"`"
	system-control print-service-env "${sppp_service}" >> "$3"
	rm -f -- "$r/${sppp_service}/log"
	ln -s -- "../sppp-log" "$r/${sppp_service}/log"
done
