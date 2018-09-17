#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Convert the various system installer configuration items.
# This is invoked by all.do .
#

redo-ifchange rc.conf

# This gets us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf printenv "$1" ; }

firstboot_sentinel() { read_rc "firstboot_sentinel" || echo "/firstboot" ; }

show_enable() {
	local i
	for i
	do
		if system-control is-enabled "$i"
		then
			echo on "$i"
		else
			echo off "$i"
		fi
	done
}

if t="`system-control find finish-update.target 2>/dev/null`"
then
	if test -e "/var/.freebsd-update-finish"
	then
		system-control preset "$t"
	else
		system-control disable "$t"
	fi
	show_enable "$t" >> "$3"
fi

f="`firstboot_sentinel`"

if t="`system-control find finish-install.target 2>/dev/null`"
then
	if	test -e "/var/.pcbsd-firstboot" || 
		test -e "${f}"
	then
		system-control preset "$t"
	else
		system-control disable "$t"
	fi
	show_enable "$t" >> "$3"
fi

if t="`system-control find reboot-after-install.target 2>/dev/null`"
then
	system-control set-service-env "$t" firstboot_sentinel "$f"
	system-control print-service-env "$t" firstboot_sentinel >> "$3"
fi

if t="`system-control find trueos-install-finish.service 2>/dev/null`"
then
	system-control set-service-env "$t" firstboot_sentinel "$f"
	system-control print-service-env "$t" firstboot_sentinel >> "$3"
fi
