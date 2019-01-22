#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Special setup for axfrdns.
# This is invoked by all.do .
#

# This gets us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf printenv "$1" ; }
get_conf() { read_rc $1 || true ; }

redo-ifchange rc.conf general-services

service="securelevel"
if ! s="`system-control find \"${service}\" 2>/dev/null`"
then
	exit 0
fi

if test -z "`system-control print-service-env \"${service}\" level`"
then
	l="`get_conf kern_\"${service}\"`"
	test -n "$l" || l=-1
	system-control set-service-env "${service}" level "$l"
	echo "$s: Defaulted "${service}" to ${l}."
fi

system-control print-service-env "${service}" >> "$3"
