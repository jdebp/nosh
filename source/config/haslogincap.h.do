#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
redo-ifchange config/trylogincap.cpp compile link
if ./compile object/trylogincap.o config/trylogincap.cpp object/trylogincap.d &&
   ./link command/trylogincap object/trylogincap.o
then
	echo '#define HAS_LOGINCAP 1' > "$3"
else
	echo '/* sysdep: -logincap */' > "$3"
fi
