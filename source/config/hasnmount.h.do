#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
redo-ifchange config/trynmount.cpp compile link
if ./compile object/trynmount.o config/trynmount.cpp object/trynmount.d &&
   ./link command/trynmount object/trynmount.o
then
	echo '#define HAS_NMOUNT 1' > "$3"
else
	echo '/* sysdep: -nmount */' > "$3"
fi
