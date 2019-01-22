#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Special setup for pefs mount services
# This is invoked by all.do .
#

redo-ifchange general-services

r="/var/local/sv"
e="--no-systemd-quirks --escape-instance --bundle-root"

redo-ifchange "pefs-mount@.service"

if test -d /var/db/pefs/
then
	while read -r i
	do
		test -z "$i" && continue
		pefs_service="pefs-mount@$i"
		system-control convert-systemd-units $e "$r/" "./${pefs_service}.service"
		mkdir -p -m 0755 "$r/${pefs_service}/service/env"
		system-control disable "${pefs_service}"
		rm -f -- "$r/${pefs_service}/log"
		ln -s -- "../../cyclog@pefs" "$r/${pefs_service}/log"
	done < /var/db/pefs/auto_mounts

	while read -r i
	do
		test -z "$i" && continue
		arp_service="static_arp@$i"
		system-control preset "${arp_service}"
	done < /var/db/pefs/auto_mounts
fi
