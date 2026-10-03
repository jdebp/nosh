#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
redo-ifchange config/tryshadow.cpp compile link
if ./compile object/tryshadow.o config/tryshadow.cpp object/tryshadow.d &&
   ./link command/tryshadow object/tryshadow.o
then
	echo '#define HAS_SHADOW 1' > "$3"
else
	echo '/* sysdep: -shadow */' > "$3"
fi
