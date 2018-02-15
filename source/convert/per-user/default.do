#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim:set filetype=sh:
#

name="$1""$2"

sd="/etc/system-control/convert/per-user"

redo-ifchange "${sd}"
if test -e "${sd}"/"${name}"
then
	redo-ifchange "${sd}"/"${name}"
	ln -f "${sd}"/"${name}" "$3" 2>/dev/null || ln -f -s "${sd}"/"${name}" "$3"
	exec true
else
	redo-ifcreate "${sd}"/"${name}"
fi
redo-ifchange "${sd}"/dbus
if test -e "${sd}"/dbus/"${name}"
then
	redo-ifchange "${sd}"/dbus/"${name}"
	ln -f "${sd}"/dbus/"${name}" "$3" 2>/dev/null || ln -f -s "${sd}"/dbus/"${name}" "$3"
	exec true
else
	redo-ifcreate "${sd}"/dbus/"${name}"
fi

echo 1>&2 "${name}: Do not know how to redo this."
exec false
