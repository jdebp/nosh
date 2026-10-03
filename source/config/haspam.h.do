#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
redo-ifchange config/trypam.cpp compile link
if ./compile object/trypam.o config/trypam.cpp object/trypam.d &&
   ./link command/trypam object/trypam.o
then
	echo '#define HAS_PAM 1' > "$3"
else
	echo '/* sysdep: -pam */' > "$3"
fi
