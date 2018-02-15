#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim:set filetype=sh:
#

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
