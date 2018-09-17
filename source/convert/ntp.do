#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Special setup for NTP services.
# This is invoked by all.do .
#

# These get us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf printenv "$1" ; }
get_var() { read_rc "$1" || true ; }
if_yes() { case "$1" in [Yy][Ee][Ss]|[Tt][Rr][Uu][Ee]|[Oo][Nn]|1) echo "$2" ;; esac ; }

redo-ifchange rc.conf general-services

if s="`system-control find ntpd`"
then
	g="`if_yes \"\`get_var ntpd_sync_on_start\`\" YES`"

	system-control set-service-env "${s}" panicgate "${g:+--panicgate}"

	printf >> "$3" "%s:\n" "${s}"
	system-control print-service-env "${s}" >> "$3"
fi
