#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
#
# Convert the FreeBSD jails list external configuration formats.
# This is invoked by general-services.do .
#

for i in /etc/defaults/rc.conf /etc/rc.conf.local /etc/rc.conf
do
	if test -e "$i"
	then
		redo-ifchange "$i"
	else
		redo-ifcreate "$i"
	fi
done

conf=/etc/jail.conf
if ! test -e "${conf}"
then
	redo-ifcreate "${conf}"
	exit $?
fi

redo-ifchange "${conf}"

for i in jail warden
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
