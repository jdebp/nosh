#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
#
# Special setup for natd.
# This is invoked by general-services.do .
#

# These get us *only* the configuration variables, safely.
read_rc() { if type sysrc >/dev/null 2>&1 ; then sysrc -i -n "$1" ; else clearenv read-conf -oknofile /etc/defaults/rc.conf read-conf -oknofile /etc/rc.conf read-conf -oknofile /etc/rc.conf.local `which printenv` "$1" ; fi }
list_natd_interfaces() { read_rc natd_interface || true ; }
list_network_interfaces() { read_rc network_interfaces || ifconfig -l | while read -r i ; do echo "$i" ; done ; }
get_ifconfig() { read_rc ifconfig_"$1" || read_rc ifconfig_DEFAULT || true ; }

for i in /etc/defaults/rc.conf /etc/rc.conf.local /etc/rc.conf
do
	if test -e "$i"
	then
		redo-ifchange "$i"
	else
		redo-ifcreate "$i"
	fi
done

r="/var/local/sv"
e="--no-systemd-quirks --escape-instance --bundle-root"

redo-ifchange "natd@.service"

list_network_interfaces |
while read -r i
do
	test -z "$i" && continue
	natd_service="natd@$i"
	system-control convert-systemd-units $e "$r/" "./${natd_service}.service"
	mkdir -p -m 0755 "$r/${natd_service}/service/env"
	system-control disable "${natd_service}"
	if echo "$i" | grep -E -q '^[0-9]+(\.[0-9]+){0,3}$'
	then
		system-control set-service-env "${natd_service}" type "-interface"
	else
		system-control set-service-env "${natd_service}" type "-alias"
	fi
	if get_ifconfig "$i" | grep -q -E '\<(SYNC|NOSYNC)DHCP\>'
	then
		system-control set-service-env "${natd_service}" dynamic "-dynamic"
	else
		system-control set-service-env "${natd_service}" dynamic ""
	fi
	system-control print-service-env "${natd_service}" >> "$3"
	rm -f -- "$r/${natd_service}/log"
	ln -s -- "../../natd-log" "$r/${natd_service}/log"
done

list_natd_interfaces |
while read -r i
do
	test -z "$i" && continue
	system-control preset "natd@$i"
done
