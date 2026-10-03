#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
redo-ifchange config/trykenv.cpp compile link
if ./compile object/trykenv.o config/trykenv.cpp object/trykenv.d &&
   ./link command/trykenv object/trykenv.o
then
	echo '#define HAS_KENV 1' > "$3"
else
	echo '/* sysdep: -kenv */' > "$3"
fi
