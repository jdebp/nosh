#!/bin/sh -e
#
# Special setup for sppp.
# This is invoked by general-services.do .
#

set_if_unset() { if test -z "`system-control print-service-env \"$1\" \"$2\"`" ; then system-control set-service-env "$1" "$2" "$3" ; echo "$s: Defaulted $2 to $3." ; fi ; }

# These get us *only* the configuration variables, safely.
list_interfaces() { clearenv read-conf -oknofile /etc/rc.conf read-conf -oknofile /etc/rc.conf.local `which printenv` sppp_interfaces || true ; }
get_args() { clearenv read-conf -oknofile /etc/rc.conf read-conf -oknofile /etc/rc.conf.local `which printenv` spppconfig_$1 || true ; }

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

list_interfaces |
while read i
do
	test -z "$i" && continue
	system-control convert-systemd-units $e "$r/" "./spppcontrol@$i.service"
	system-control preset "spppcontrol@$i"
	set_if_unset spppcontrol@"$i" args "`get_args \"$i\"`"
	system-control print-service-env "spppcontrol@$i" >> "$3"
	rm -f -- "$r/spppcontrol@$i/log"
	ln -s -- "../sysinit-log" "$r/spppcontrol@$i/log"
done
