#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
#
# Special setup for dnscache.
# This is invoked by general-services.do .
#

set_if_unset() { if test -z "`system-control print-service-env \"$1\" \"$2\"`" ; then system-control set-service-env "$1" "$2" "$3" ; echo "$s: Defaulted $2 to $3." ; fi ; }
dir_not_empty() { test -n "`/bin/ls -A \"$1\"`" ; }

# These get us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf "`which printenv`" "$1" ; }
list_network_addresses() { for i in `read_rc network_addresses || echo 127.0.0.1` ; do echo "$i" ; done ; }

redo-ifchange rc.conf general-services "dnscache@.socket" "dnscache.service"

if s="`system-control find dnscache`"
then
	set_if_unset dnscache IPSEND 0.0.0.0
	set_if_unset dnscache IP 127.0.0.1
#	set_if_unset dnscache DATALIMIT 3000000
	set_if_unset dnscache CACHESIZE 1000000
	set_if_unset dnscache ROOT "root"

	system-control print-service-env dnscache >> "$3"

	test \! -e "${s}/service/seed" || chmod 0 "${s}/service/seed" 
#	if ! test -r "${s}/service/seed"
#	then
#		install -m 0600 /dev/null "${s}/service/seed" 
#		dd if=/dev/urandom of="${s}/service/seed" bs=128 count=1
#	elif setuidgid nobody test -r "${s}/service/seed"
#		chmod 0600 "${s}/service/seed" 
#	fi
	test -r "${s}/service/root/servers/@" || echo '127.53.0.1' > "${s}/service/root/servers/@"
	dir_not_empty "${s}/service/root/ip" || touch "${s}/service/root/ip/127.0.0.1"
fi

lr="/var/local/sv/"
e="--no-systemd-quirks --escape-instance --bundle-root"

list_network_addresses |
while read -r i
do
	test -z "$i" && continue
	service="dnscache@$i"
	s="$lr/${service}"

	system-control convert-systemd-units $e "$lr/" "./${service}.socket"
	system-control preset "${service}"
	rm -f -- "${s}/log"
	ln -s -- "../../../sv/cyclog@dnscache" "${s}/log"

	install -d -m 0755 "${s}/service/env"
	install -d -m 0755 "${s}/service/root"
	install -d -m 0755 "${s}/service/root/ip"
	install -d -m 0755 "${s}/service/root/servers"
	test \! -e "${s}/service/seed" || chmod 0 "${s}/service/seed" 
#	if ! test -r "${s}/service/seed"
#	then
#		install -m 0600 /dev/null "${s}/service/seed" 
#		dd if=/dev/urandom of="${s}/service/seed" bs=128 count=1
#	elif setuidgid nobody test -r "${s}/service/seed"
#		chmod 0600 "${s}/service/seed" 
#	fi
	test -r "${s}/service/root/servers/@" || echo '127.53.0.1' > "${s}/service/root/servers/@"
	dir_not_empty "${s}/service/root/ip" || touch "${s}/service/root/ip/127.0.0.1"
	set_if_unset "${s}/" IPSEND 0.0.0.0
	set_if_unset "${s}/" CACHESIZE 1000000
	set_if_unset "${s}/" ROOT "root"

	if system-control is-enabled "${s}"
	then
		echo >> "$3" on "${service}"
	else
		echo >> "$3" off "${service}"
	fi
done
