#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Generate the Message of The Day from Debian/Ubuntu configuration files.
# This is invoked by all.do .

redo-ifchange rc.conf

# This gets us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf printenv "$1" ; }

case "`read_rc os_version`" in
arch:*) 	uname -srm > "$3" ;;
void:*) 	uname -srm > "$3" ;;
debian:*)
	if test -d "/etc/update-motd.d/"
	then
		redo-ifchange "/etc/update-motd.d"
		run-parts --lsbsysinit --test "/etc/update-motd.d/" | xargs redo-ifchange --
		run-parts --lsbsysinit "/etc/update-motd.d/" > "$3"
	else
		redo-ifcreate "/etc/update-motd.d"
		uname -srm > "$3"
	fi
	;;
gentoo:*) 	uname -srm > "$3" ;;
centos:*)	uname -srm > "$3" ;;
rhel:*) 	uname -srm > "$3" ;;
*)      	uname -srm > "$3" ;;
esac
