#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
redo-ifchange config/tryutmp.cpp compile link
if ./compile object/tryutmp.o config/tryutmp.cpp object/tryutmp.d &&
   ./link command/tryutmp object/tryutmp.o
then
	echo '#define HAS_UTMP 1' > "$3"
else
	echo '/* sysdep: -utmp */' > "$3"
fi
