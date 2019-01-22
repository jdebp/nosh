#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Convert the FreeBSD jails list external configuration formats.
# This is invoked by all.do .
#

redo-ifchange rc.conf general-services

conf=/etc/jail.conf
if ! test -e "${conf}"
then
	redo-ifcreate "${conf}"
	exit $?
fi

redo-ifchange "${conf}"

for i in jail
do
	target="$i".target

	system-control preset -- "$target"
	if system-control is-enabled "$target"
	then
		echo >> "$3" on "$target"
	else
		echo >> "$3" off "$target"
	fi
done
