#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
redo-ifchange config/tryutmpxaddr.cpp compile link
if ./compile object/tryutmpxaddr.o config/tryutmpxaddr.cpp object/tryutmpxaddr.d &&
   ./link command/tryutmpxaddr object/tryutmpxaddr.o
then
	echo '#define HAS_UTMPX_ADDR 1' > "$3"
else
	echo '/* sysdep: -utmpxaddr */' > "$3"
fi
