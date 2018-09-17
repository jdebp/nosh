#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim:set filetype=sh:
#
# This prototype is copied into every (real) user's system-control/convert directory.
#
# This is run by the per-user external configuration import subsystem.
# It is used to determine the user name.

case "`uname`" in
FreeBSD)	redo-ifchange /etc/pwd.db ;;
Linux|*)	redo-ifchange /etc/passwd ;;
esac

exec id -nu >> "$3"
