#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Special setup for mdmfs.
# This is invoked by all.do .
#

# This gets us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf printenv "$1" ; }
get_var() { read_rc "$1" || true ; }
if_yes() { case "$1" in [Yy][Ee][Ss]|[Tt][Rr][Uu][Ee]|[Oo][Nn]|1) echo "$2" ;; esac ; }

redo-ifchange rc.conf

for where in tmp var
do
	mfs="`get_var \"${where}mfs\"`"
	service="mdmfs@-${where}"
	system-control set-service-env "${service}" flags "`get_var \"${where}mfs_flags\"`"
	system-control set-service-env "${service}" size "`get_var \"${where}size\"`"
	if test _"`if_yes \"${mfs}\" YES`" = _"YES"
	then
		system-control enable "${service}"
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
done

for where in var include usr
do
	mfs="`get_var \"populate_${where}\"`"
	service="populate@${where}"
	if test _"`if_yes \"${mfs}\" YES`" = _"YES"
	then
		system-control enable "${service}"
	else
		system-control disable "${service}"
	fi
	if system-control is-enabled "${service}"
	then
		echo >> "$3" on "${service}"
	else
		echo >> "$3" off "${service}"
	fi
done
