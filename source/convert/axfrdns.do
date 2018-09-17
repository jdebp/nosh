#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Special setup for axfrdns.
# This is invoked by all.do .
#

set_if_unset() { if test -z "`system-control print-service-env \"$1\" \"$2\"`" ; then system-control set-service-env "$1" "$2" "$3" ; echo "$s: Defaulted $2 to $3." ; fi ; }

# These get us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf printenv "$1" ; }
list_network_addresses() { ( read_rc network_addresses || echo 127.53.0.1 ) | fmt -w 1 ; }

redo-ifchange rc.conf general-services "axfrdns@.socket" "axfrdns@.service"

if s="`system-control find axfrdns`"
then
	set_if_unset axfrdns ROOT "../../tinydns/service/root"

	system-control print-service-env axfrdns >> "$3"
fi

lr="/var/local/sv/"
e="--no-systemd-quirks --escape-instance --bundle-root"

list_network_addresses |
while read -r i
do
	test -z "$i" && continue
	service="axfrdns@$i"
	s="$lr/${service}"

	system-control convert-systemd-units $e "$lr/" "./${service}.socket"
	system-control preset "${service}"
	rm -f -- "${s}/log"
	ln -s -- "../../../sv/cyclog@axfrdns" "${s}/log"

	install -d -m 0755 "${s}/service/env"
	set_if_unset "${s}/" ROOT "../../tinydns@$i/service/root"

	if system-control is-enabled "${service}"
	then
		echo >> "$3" on "${service}"
	else
		echo >> "$3" off "${service}"
	fi
done
