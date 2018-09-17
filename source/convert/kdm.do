#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Convert the kdm external configuration formats.
# This is invoked by all.do .
#

# These get us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf printenv "$1" ; }
get_var1() { read_rc "$1" || true ; }
get_var2() { read_rc "$1" || read_rc "$2" || true ; }

redo-ifchange rc.conf general-services

#for tty in tty7 ttyv6
#do
#	kdm_service="kdm@${tty}"
#
#	lang="`get_var2 \"kdm4_${tty}_lang\" kmd4_lang`"
#	test -n "${lang}" || lang="en_US"
#	system-control set-service-env "${kdm_service}" LANG "${lang}".UTF-8
#
#	flags="`get_var2 \"kdm4_${tty}_flags\" kmd4_flags`"
#	if test -n "${flags}"
#	then
#		system-control set-service-env "${kdm_service}" flags "${flags}"
#	else
#		system-control set-service-env "${kdm_service}" flags
#	fi
#
#	system-control print-service-env "${kdm_service}" >> "$3"
#done
