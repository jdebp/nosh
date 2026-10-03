#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
redo-ifchange config/tryutmpx.cpp compile link
if ./compile object/tryutmpx.o config/tryutmpx.cpp object/tryutmpx.d &&
   ./link command/tryutmpx object/tryutmpx.o
then
	echo '#define HAS_UTMPX 1' > "$3"
else
	echo '/* sysdep: -utmpx */' > "$3"
fi
