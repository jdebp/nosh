#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Special setup for NFS services.
# This is invoked by all.do .
#

# These get us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf printenv "$1" ; }
get_var() { read_rc "$1" || true ; }
if_yes() { case "$1" in [Yy][Ee][Ss]|[Tt][Rr][Uu][Ee]|[Oo][Nn]|1) echo "$2" ;; esac ; }

redo-ifchange rc.conf general-services

old="`if_yes \"\`get_var oldnfs_server_enable\`\" YES`"

if s="`system-control find mountd 2>/dev/null`"
then
	weak_authentication="`get_var weak_mountd_authentication`"
	zfs="`if_yes \"\`get_var zfs_enable\`\" YES`"

	system-control set-service-env "${s}" weak_authentication "${weak_authentication:+-n}"
	system-control set-service-env "${s}" old "${old:+-o}"
	system-control set-service-env "${s}" exports "${zfs:+/etc/zfs/exports}"

	printf >> "$3" "%s:\n" "${s}"
	system-control print-service-env "${s}" >> "$3"
fi

if s="`system-control find nfsd 2>/dev/null`"
then
	system-control set-service-env "${s}" old "${old:+-o}"

	printf >> "$3" "%s:\n" "${s}"
	system-control print-service-env "${s}" >> "$3"
fi

if s="`system-control find nfsuserd 2>/dev/null`"
then
	manage_gids="`if_yes \"\`get_var nfs_server_managegids\`\" YES`"

	system-control set-service-env "${s}" manage_gids "${manage_gids:+--manage-gids}"

	printf >> "$3" "%s:\n" "${s}"
	system-control print-service-env "${s}" >> "$3"
fi
