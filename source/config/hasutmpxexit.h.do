#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
redo-ifchange config/tryutmpxexit.cpp compile link
if ./compile object/tryutmpxexit.o config/tryutmpxexit.cpp object/tryutmpxexit.d &&
   ./link command/tryutmpxexit object/tryutmpxexit.o
then
	echo '#define HAS_UTMPX_EXIT 1' > "$3"
else
	echo '/* sysdep: -utmpxexit */' > "$3"
fi
