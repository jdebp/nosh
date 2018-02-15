#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim:set filetype=sh:
#

case "`uname`" in
FreeBSD)	redo-ifchange /etc/pwd.db ;;
Linux|*)	redo-ifchange /etc/passwd ;;
esac

exec id -nu >> "$3"
