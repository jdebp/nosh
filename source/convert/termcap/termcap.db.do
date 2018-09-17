#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim:set filetype=sh:
#
# This is run by the system-wide external configuration import subsystem.

redo-ifchange /usr/share/misc/termcap termcap/termcap.linux termcap/termcap.interix termcap/termcap.teken
cap_mkdb -f "$3" /usr/share/misc/termcap termcap/termcap.linux termcap/termcap.interix termcap/termcap.teken
mv "$3.db" "$3"
