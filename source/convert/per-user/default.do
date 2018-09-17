#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim:set filetype=sh:
#
# This prototype is copied into every (real) user's system-control/convert directory.
#
# This is run by the per-user external configuration import subsystem.
# It is used to make copies of source files for service bundles from system-wide configuration import.

name="$1""$2"

sd="/etc/system-control/convert/per-user"

redo-ifchange "${sd}"
if test -e "${sd}"/"${name}"
then
	# A hand-written source file is present; use it.
	redo-ifchange "${sd}"/"${name}"
	ln -f "${sd}"/"${name}" "$3" 2>/dev/null || ln -f -s "${sd}"/"${name}" "$3"
	exec true
else
	redo-ifcreate "${sd}"/"${name}"
fi
redo-ifchange "${sd}"/dbus
if test -e "${sd}"/dbus/"${name}"
then
	# A source file was generated from D-Bus a per-user service definition.
	redo-ifchange "${sd}"/dbus/"${name}"
	ln -f "${sd}"/dbus/"${name}" "$3" 2>/dev/null || ln -f -s "${sd}"/dbus/"${name}" "$3"
	exec true
else
	redo-ifcreate "${sd}"/dbus/"${name}"
fi

echo 1>&2 "${name}: Do not know how to redo this."
exec false
