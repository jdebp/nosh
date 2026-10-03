#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
redo-ifchange config/trywscons.cpp compile link
if ./compile object/trywscons.o config/trywscons.cpp object/trywscons.d &&
   ./link command/trywscons object/trywscons.o
then
	echo '#define HAS_WSCONS 1' > "$3"
else
	echo '/* sysdep: -wscons */' > "$3"
fi
