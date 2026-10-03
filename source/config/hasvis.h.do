#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
redo-ifchange config/tryvis.cpp compile link
if ./compile object/tryvis.o config/tryvis.cpp object/tryvis.d &&
   ./link command/tryvis object/tryvis.o
then
	echo '#define HAS_VIS 1' > "$3"
else
	echo '/* sysdep: -vis */' > "$3"
fi
