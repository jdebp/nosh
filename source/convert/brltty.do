#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Special setup for BrlTTY
# This is invoked by all.do .
#

# These get us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf printenv "$1" ; }

redo-ifchange rc.conf general-services "brltty@.service"

test -h /var/local/service-bundles/targets || { install -d -m 0755 /var/local/service-bundles && ln -s /etc/service-bundles/targets /var/local/service-bundles/ ; }
r="/var/local/service-bundles/services"
e="--no-systemd-quirks --local-bundle"

find "$r/" -maxdepth 1 -type d -name 'brltty@*' -a \( -type l -o -type f \) -print0 |
xargs -0 system-control disable --
system-control disable brltty-log

for etcdir in "/usr/local/etc" "/etc" "/usr/pkg/etc"
do
	if ! test -e "${etcdir}"
	then
		redo-ifcreate "${etcdir}"
		echo >>"$3" "${etcdir}" "does not exist."
		continue
	fi
	if ! test -x "${etcdir}"/
	then
		redo-ifchange "${etcdir}"
		echo >>"$3" "${etcdir}" "is not valid."
		continue
	fi
	find "${etcdir}"/ -maxdepth 1 -name 'brltty*.conf' -a \( -type l -o -type f \) 2>>"$3"
	redo-ifdelete "${etcdir}"
done |
while read -r i
do
	service="`system-control escape --prefix brltty@ \"$i\"`"

	system-control convert-systemd-units $e --bundle-root "$r/" "./${service}.service"
	install -d -m 0755 -- "$r/${service}/service/env"
	rm -f -- "$r/${service}/log"
	ln -s -- "../../service-bundles/services/brltty-log" "$r/${service}/log"

	system-control preset "${service}"
	if system-control is-enabled "${service}"
	then
		echo >> "$3" on "${service}"
	else
		echo >> "$3" off "${service}"
	fi
	system-control print-service-env "${service}" >> "$3"
	echo >> "$3"
	system-control preset brltty-log
done
