#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Special setup for tinydns.
# This is invoked by all.do .
#

set_if_unset() { if test -z "`system-control print-service-env \"$1\" \"$2\"`" ; then system-control set-service-env "$1" "$2" "$3" ; echo "$s: Defaulted $2 to $3." ; fi ; }

# These get us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf printenv "$1" ; }
list_network_addresses() { printf '%s\n' `read_rc tinydns_network_addresses || echo 127.53.0.1` ; }
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

redo-ifchange rc.conf general-services "tinydns@.socket" "tinydns.service"

# Old system: One pre-packaged tinydns server for the entire machine
if s="`system-control find tinydns`"
then
	set_if_unset tinydns IP 127.53.0.1
	set_if_unset tinydns ROOT "root"

	if ! test -r "${s}/service/root/main" 
	then
		if test -e "${s}/service/root/public"
		then
			mv -- "${s}/service/root/public" "${s}/service/root/main"
		elif test -e "${s}/service/root/data"
		then
			mv -- "${s}/service/root/data" "${s}/service/root/main"
		else
			cp -p /usr/local/share/examples/tinydns/main "${s}/service/root/"
		fi
	fi
	if ! test -r "${s}/service/root/split-horizon" 
	then
		if test -e "${s}/service/root/private"
		then
			mv -- "${s}/service/root/private" "${s}/service/root/split-horizon"
		fi
	fi
	if test -e "${s}/service/root/data" && ! test -e "${s}/service/root/data.original" 
	then
		mv -- "${s}/service/root/data" "${s}/service/root/data.original"
	fi
	test -e "${s}/service/root/Makefile.original" || test \! -e "${s}/service/root/Makefile" || mv -- "${s}/service/root/Makefile" "${s}/service/root/Makefile.original"
	for i in add-alias add-childns add-ns add-mx add-host Makefile split-horizon convert.awk
	do
		if ! test -e "${s}/service/root/${i}"
		then
			ln -s /usr/local/share/examples/tinydns/"${i}" "${s}/service/root/"
			redo-ifchange "${s}/service/root/${i}"
		fi
	done

	show "${s}" >> "$3"
fi

# New system: Individual tinydns servers for every IP adddress in tinydns_netework_addresses in rc.conf

test -h /var/local/service-bundles/targets || { install -d -m 0755 /var/local/service-bundles && ln -s /etc/service-bundles/targets /var/local/service-bundles/ ; }
lr="/var/local/service-bundles/services/"
e="--no-systemd-quirks --escape-instance --local-bundle"

list_network_addresses |
while read -r i
do
	test -z "$i" && continue
	service="tinydns@$i"
	s="$lr/${service}"

	system-control convert-systemd-units $e --bundle-root "$lr/" "./${service}.socket"
	rm -f -- "${s}/log"
	ln -s -f -- "../../../../service-bundles/services/cyclog@tinydns" "${s}/log"
	ln -s -f -- "../log" "${s}/wants"
	ln -s -f -- "../log" "${s}/after"

	install -d -m 0755 "${s}/service/env"
	install -d -m 0755 "${s}/service/root"
	if ! test -r "${s}/service/root/main" 
	then
		if test -e "${s}/service/root/public"
		then
			mv -- "${s}/service/root/public" "${s}/service/root/main"
		elif test -e "${s}/service/root/data"
		then
			mv -- "${s}/service/root/data" "${s}/service/root/main"
		else
			cp -p /usr/local/share/examples/tinydns/main "${s}/service/root/"
		fi
	fi
	if ! test -r "${s}/service/root/split-horizon" 
	then
		if test -e "${s}/service/root/private"
		then
			mv -- "${s}/service/root/private" "${s}/service/root/split-horizon"
		fi
	fi
	if test -e "${s}/service/root/data" && ! test -e "${s}/service/root/data.original" 
	then
		mv -- "${s}/service/root/data" "${s}/service/root/data.original"
	fi
	for i in add-alias add-childns add-ns add-mx add-host Makefile split-horizon convert.awk
	do
		if ! test -e "${s}/service/root/${i}"
		then
			ln -s /usr/local/share/examples/tinydns/"${i}" "${s}/service/root/"
			redo-ifchange "${s}/service/root/${i}"
		fi
	done
	test -e "${s}/service/root/main" || cp -p /usr/local/share/examples/tinydns/main "${s}/service/root/"
	set_if_unset "${s}/" ROOT "root"

	system-control preset --rcconf-file rc.conf "${service}"
	show "${s}" >> "$3"
done
