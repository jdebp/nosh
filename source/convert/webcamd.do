#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Special setup for webcamd.
# This is invoked by all.do .
#

# These get us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf printenv "$1" ; }
get_var() { read_rc "$1" || true ; }
get_var1() { read_rc webcamd_"$1" || true ; }
get_var2() { read_rc webcamd_"$1"_"$2" || read_rc webcamd_"$2" || true ; }

redo-ifchange rc.conf general-services "webcamd@.service"

r="/var/local/sv"
e="--no-systemd-quirks --escape-instance --bundle-root"

find "$r/" -maxdepth 1 -type d -name 'webcamd@*' -print0 |
xargs -0 system-control disable --
system-control disable webcamd-log

n=0
while true
do
	instance_flags="`get_var2 \"n\" flags`"
	test -z "$instance_flags" && break

	service="webcamd@$i"
	system-control convert-systemd-units $e "$r/" "./${service}.service"

	mkdir -p -m 0755 -- "$r/${service}/service/env"
	system-control set-service-env "${service}" "${n}_flags" "${instance_flags}"
	system-control set-service-env "${service}" flags "`get_var1 flags`"
	system-control set-service-env "${service}" user "`get_var2 \"$n\" user`"
	system-control set-service-env "${service}" group "`get_var2 \"$n\" group`"
	case "`get_var hald_enable`" in
	[Yy][Ee][Ss]|[Oo][Nn]|1)
		system-control set-service-env -- "${service}" hald_flags "-H"
		;;
	*)
		system-control set-service-env -- "${service}" hald_flags
		;;
	esac

	if echo -- "$instance_flags" | grep -E -q -- "(^|[[:space:]])-[dDNS]"
	then
		system-control preset "${service}"
	else
		system-control disable "${service}"
	fi

	if system-control is-enabled "${service}"
	then
		echo >> "$3" on "${service}"
	else
		echo >> "$3" off "${service}"
	fi
	system-control print-service-env "${service}" >> "$3"

	rm -f -- "$r/${service}/log"
	ln -s -- "../../webcamd-log" "$r/${service}/log"
	system-control preset webcamd-log

	n="`expr $n + 1`"
done
