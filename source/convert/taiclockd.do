#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Special setup for taiclockd.
# This is invoked by all.do .
#

set_if_unset() { if test -z "`system-control print-service-env \"$1\" \"$2\"`" ; then system-control set-service-env "$1" "$2" "$3" ; echo "$s: Defaulted $2 to $3." ; fi ; }

# These get us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf printenv "$1" ; }
list_network_addresses() { printf '%s\n' `read_rc taiclockd_network_addresses || echo ::0` ; }

redo-ifchange rc.conf general-services "taiclockd@.socket" "taiclockd.service"

# Old system: One pre-packaged taiclockd server for the entire machine
if s="`system-control find taiclockd`"
then
	set_if_unset taiclockd IP ::0

	if system-control is-enabled taiclockd
	then
		echo >> "$3" on taiclockd
	else
		echo >> "$3" off taiclockd
	fi
fi

# New system: Individual taiclockd servers for every IP adddress in taiclockd_netework_addresses in rc.conf

test -h /var/local/service-bundles/targets || { install -d -m 0755 /var/local/service-bundles && ln -s /etc/service-bundles/targets /var/local/service-bundles/ ; }
lr="/var/local/service-bundles/services/"
e="--no-systemd-quirks --escape-instance --local-bundle"

list_network_addresses |
while read -r i
do
	test -z "$i" && continue
	service="taiclockd@$i"
	s="$lr/${service}"

	system-control convert-systemd-units $e --bundle-root "$lr/" "./${service}.socket"
	rm -f -- "${s}/log"
	ln -s -f -- "../../../service-bundles/services/cyclog@taiclockd" "${s}/log"
	ln -s -f -- "../log" "${s}/wants"
	ln -s -f -- "../log" "${s}/after"

	install -d -m 0755 "${s}/service/env"

	system-control preset --rcconf-file rc.conf "${service}"
	if system-control is-enabled "${service}"
	then
		echo >> "$3" on "${service}"
	else
		echo >> "$3" off "${service}"
	fi
done
