#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
redo-ifchange config/tryevdev.cpp compile link
if ./compile object/tryevdev.o config/tryevdev.cpp object/tryevdev.d &&
   ./link command/tryevdev object/tryevdev.o
then
	echo '#define HAS_EVDEV 1' > "$3"
else
	echo '/* sysdep: -evdev */' > "$3"
fi
