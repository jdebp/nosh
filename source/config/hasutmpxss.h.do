#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
redo-ifchange config/tryutmpxss.cpp compile link
if ./compile object/tryutmpxss.o config/tryutmpxss.cpp object/tryutmpxss.d &&
   ./link command/tryutmpxss object/tryutmpxss.o
then
	echo '#define HAS_UTMPX_SS 1' > "$3"
else
	echo '/* sysdep: -utmpxss */' > "$3"
fi
