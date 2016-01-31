#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
#
# Special setup for mdmfs.
# This is invoked by all.do .
#

# This gets us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf "`which printenv`" "$1" ; }
get_var() { read_rc "$1" || true ; }

redo-ifchange rc.conf

case "`uname`" in
*BSD)	;;
*)	echo > "$3" 'No mdmfs';exit 0;;
esac

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

for where in var include usr
do
	mfs="`get_var \"populate_${where}\"`"
	service="populate@${where}"
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
done
