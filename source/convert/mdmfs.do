#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
#
# Special setup for mdmfs.
# This is invoked by volumes.do .
#

# This gets us *only* the configuration variables, safely.
read_rc() { if type sysrc >/dev/null 2>&1 ; then sysrc -i -n "$1" ; else clearenv read-conf -oknofile /etc/defaults/rc.conf read-conf -oknofile /etc/rc.conf read-conf -oknofile /etc/rc.conf.local `which printenv` "$1" ; fi }
get_var() { read_rc "$1" || true ; }

for i in /etc/defaults/rc.conf /etc/rc.conf.local /etc/rc.conf
do
	if test -e "$i"
	then
		redo-ifchange "$i"
	else
		redo-ifcreate "$i"
	fi
done

for where in tmp var
do
	mfs="`get_var \"${where}mfs\"`"
	service="mdmfs@-${where}"
	system-control set-service-env "${service}" flags "`get_var \"${where}mfs_flags\"`"
	system-control set-service-env "${service}" size "`get_var \"${where}size\"`"
	case "${mfs}" in
	[Yy][Ee][Ss]|[Oo][Nn])
		system-control enable "${service}"
		;;
	*)
		system-control disable "${service}"
		;;
	esac
	if system-control is-enabled "${service}"
	then
		echo >> "$3" on "${service}"
	else
		echo >> "$3" off "${service}"
	fi
	system-control print-service-env "${service}" >> "$3"
done
