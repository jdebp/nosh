#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Special setup for syslog services.
# This is invoked by all.do .
#

# These get us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf printenv "$1" ; }
get_var1() { read_rc "$1" || true ; }

redo-ifchange rc.conf general-services

flags="`get_var1 syslogd_flags`"

case "`uname`" in
Linux)	extra="systemd-syslogd" ;;
*BSD)	extra="syslogd" ;;
esac

for i in local-syslogd udp-syslogd ${extra}
do
	system-control set-service-env "$i" flags "${flags}"
	system-control print-service-env "$i" flags >> "$3"
done
