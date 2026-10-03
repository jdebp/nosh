#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
redo-ifchange config/tryupdwtmpx.cpp compile link
if ./compile object/tryupdwtmpx.o config/tryupdwtmpx.cpp object/tryupdwtmpx.d &&
   ./link command/tryupdwtmpx object/tryupdwtmpx.o
then
	echo '#define HAS_UDPWTMPX 1' > "$3"
else
	echo '/* sysdep: -updwtmpx */' > "$3"
fi
