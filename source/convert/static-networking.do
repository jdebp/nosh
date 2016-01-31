#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
#
# Special setup for static networking services
# This is invoked by general-services.do .
#

# These get us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf "`which printenv`" "$1" ; }
list_static_arp() { read_rc static_arp_pairs || true ; }
list_static_ndp() { read_rc static_ndp_pairs || true ; }

redo-ifchange rc.conf general-services

r="/var/local/sv"
e="--no-systemd-quirks --escape-instance --bundle-root"

redo-ifchange "static_arp@.service" "static_ndp@.service"

list_static_arp |
while read -r i
do
	test -z "$i" && continue
	echo static_arp_${i}
	eval j=\$static_arp_${i}
	arp_service="static_arp@$j"
	system-control convert-systemd-units $e "$r/" "./${arp_service}.service"
	mkdir -p -m 0755 "$r/${arp_service}/service/env"
	system-control disable "${arp_service}"
	rm -f -- "$r/${arp_service}/log"
	ln -s -- "../../cyclog@static-networking" "$r/${arp_service}/log"
	ln -s -- "../../static-networking" "$r/${arp_service}/wants/"
done

list_static_arp |
while read -r i
do
	test -z "$i" && continue
	eval j=\$static_arp_${i}
	arp_service="static_arp@$j"
	system-control preset "${arp_service}"
done

list_static_ndp |
while read -r i
do
	test -z "$i" && continue
	echo static_ndp_${i}
	eval j=\$static_ndp_${i}
	ndp_service="static_ndp@$j"
	system-control convert-systemd-units $e "$r/" "./${ndp_service}.service"
	mkdir -p -m 0755 "$r/${ndp_service}/service/env"
	system-control disable "${ndp_service}"
	rm -f -- "$r/${ndp_service}/log"
	ln -s -- "../../cyclog@static-networking" "$r/${ndp_service}/log"
	ln -s -- "../../static-networking" "$r/${ndp_service}/wants/"
done

list_static_ndp |
while read -r i
do
	test -z "$i" && continue
	eval j=\$static_ndp_${i}
	ndp_service="static_ndp@$j"
	system-control preset "${ndp_service}"
done
