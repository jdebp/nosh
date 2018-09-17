#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Special setup for mdconfig.
# This is invoked by all.do .
#

# This gets us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf printenv "$1" ; }
get_var1() { read_rc "$1" || true ; }
get_var2() { read_rc ftpproxy_"$1" || read_rc ftpproxy_"$2" || true ; }

r="/var/local/sv"
s="/etc/service-bundles/services"
e="--no-systemd-quirks --bundle-root"

redo-ifchange rc.conf "ftp-proxy@.service"

find "$r/" -maxdepth 1 -type d -name 'ftp-proxy@*' |
while read -r n
do
	system-control disable "$n"
done

for dev in `get_var2 instances instance`
do
	service="ftp-proxy@${dev}"

	system-control convert-systemd-units $e "$r/" "./${service}.service"
	mkdir -p -m 0755 "$r/${service}/service/env"
	rm -f -- "$r/${service}/log"
	ln -s -- "../../../sv/cyclog@ftp-proxy" "$r/${service}/log"

	flags="`get_var2 \"${dev}\" flags`"

	system-control set-service-env "${service}" flags "${flags}"

	system-control preset "${service}"

	if system-control is-enabled "${service}"
	then
		echo >> "$3" on "${service}"
	else
		echo >> "$3" off "${service}"
	fi
	system-control print-service-env "${service}" >> "$3"
done
