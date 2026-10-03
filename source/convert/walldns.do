#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Special setup for walldns.
# This is invoked by all.do .
#

set_if_unset() { if test -z "`system-control print-service-env \"$1\" \"$2\"`" ; then system-control set-service-env "$1" "$2" "$3" ; echo "$s: Defaulted $2 to $3." ; fi ; }

# These get us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf printenv "$1" ; }
list_network_addresses() { printf '%s\n' `read_rc walldns_network_addresses || echo 127.53.1.1` ; }
show() {
	local service
	for service
	do
		if system-control is-enabled "${service}"
		then
			echo on "${service}"
		else
			echo off "${service}"
		fi
		system-control print-service-env "${service}" | sed -e 's/^/	/'
	done
}

redo-ifchange rc.conf general-services "walldns@.socket" "walldns.service"

# Old system: One pre-packaged walldns server for the entire machine
if s="`system-control find walldns`"
then
	install -d -m 0755 "${s}/service/env"
	install -d -m 0755 "${s}/service/root"

	set_if_unset walldns IP 127.53.1.1
	set_if_unset walldns ROOT "root"

	show "${s}" >> "$3"
fi

# New system: Individual walldns servers for every IP adddress in walldns_netework_addresses in rc.conf

test -h /var/local/service-bundles/targets || { install -d -m 0755 /var/local/service-bundles && ln -s /etc/service-bundles/targets /var/local/service-bundles/ ; }
lr="/var/local/service-bundles/services/"
e="--no-systemd-quirks --escape-instance --local-bundle"

list_network_addresses |
while read -r i
do
	test -z "$i" && continue
	service="walldns@$i"
	s="$lr/${service}"

	system-control convert-systemd-units $e --bundle-root "$lr/" "./${service}.socket"
	rm -f -- "${s}/log"
	ln -s -f -- "../../../../service-bundles/services/cyclog@walldns" "${s}/log"
	ln -s -f -- "../log" "${s}/wants"
	ln -s -f -- "../log" "${s}/after"

	install -d -m 0755 "${s}/service/env"
	install -d -m 0755 "${s}/service/root"
	set_if_unset "${s}/" ROOT "root"

	system-control preset --rcconf-file rc.conf "${service}"
	show "${s}" >> "$3"
done
