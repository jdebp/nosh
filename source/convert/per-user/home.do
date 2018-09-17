#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim:set filetype=sh:
#
# This prototype is copied into every (real) user's system-control/convert directory.
#
# This is run by the per-user external configuration import subsystem.
# It is used to determine the home directory as set in the user accounts database (not the transient current value of the HOME environment variable).

redo-ifchange user
read -r i < user

case "`uname`" in
FreeBSD)	
	if test -r /etc/spwd.db
	then
		redo-ifchange /etc/spwd.db
	else
		redo-ifchange /etc/pwd.db
	fi
	;;
Linux|*)	redo-ifchange /etc/passwd ;;
esac

getent passwd "$i" | awk -F : '{print $6;}' >> "$3"
