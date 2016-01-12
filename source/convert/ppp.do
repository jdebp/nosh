#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
#
# Special setup for ppp.
# This is invoked by general-services.do .
#

# These get us *only* the configuration variables, safely.
read_rc() { if type sysrc >/dev/null 2>&1 ; then sysrc -i -n "$1" ; else clearenv read-conf -oknofile /etc/defaults/rc.conf read-conf -oknofile /etc/rc.conf read-conf -oknofile /etc/rc.conf.local `which printenv` "$1" ; fi }
list_interfaces() { read_rc ppp_profile || true ; }
get_var() { read_rc ppp_"$1"_"$2" || read_rc ppp_"$2" || true ; }

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

redo-ifchange "ppp@.service"

list_interfaces |
while read -r i
do
	test -z "$i" && continue
	service="ppp@$i"
	system-control convert-systemd-units $e "$r/" "./${service}.service"
	mkdir -p -m 0755 -- "$r/${service}/service/env"
	system-control preset "${service}"
	system-control set-service-env "${service}" mode "`get_var \"$i\" mode`"
	system-control set-service-env "${service}" nat "`get_var \"$i\" nat`"
	system-control set-service-env "${service}" unit "`get_var \"$i\" unit`"
	if system-control is-enabled "${service}"
	then
		echo >> "$3" on "${service}"
	else
		echo >> "$3" off "${service}"
	fi
	system-control print-service-env "${service}" >> "$3"
	rm -f -- "$r/${service}/log"
	ln -s -- "../../ppp-log" "$r/${service}/log"
done
