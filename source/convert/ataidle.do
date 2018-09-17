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
get_var2() { read_rc ataidle_"$1" || read_rc ataidle_"$2" || true ; }

r="/var/local/sv"
s="/etc/service-bundles/services"
e="--no-systemd-quirks --local-bundle --bundle-root"

redo-ifchange rc.conf "ataidle@.service"

find "$r/" -maxdepth 1 -type d -name 'ataidle@*' -print0 |
xargs -0 system-control disable --
system-control disable ataidle-log

for dev in `get_var2 devices device`
do
	ataidle_service="ataidle@${dev}"
	system-control convert-systemd-units $e "$r/" "./${ataidle_service}.service"
	mkdir -p -m 0755 "$r/${ataidle_service}/service/env"
	rm -f -- "$r/${ataidle_service}/log"
	ln -s -- "../../../sv/ataidle-log" "$r/${ataidle_service}/log"
	system-control preset ataidle-log
	flags="`get_var2 \"${dev}\" default`"
	system-control set-service-env "${ataidle_service}" flags "${flags}"
	system-control preset "${ataidle_service}"
	if system-control is-enabled "${ataidle_service}"
	then
		echo >> "$3" on "${ataidle_service}"
	else
		echo >> "$3" off "${ataidle_service}"
	fi
	system-control print-service-env "${ataidle_service}" >> "$3"
done
