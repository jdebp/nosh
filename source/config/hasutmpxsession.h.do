#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
redo-ifchange config/tryutmpxsession.cpp compile link
if ./compile object/tryutmpxsession.o config/tryutmpxsession.cpp object/tryutmpxsession.d &&
   ./link command/tryutmpxsession object/tryutmpxsession.o
then
	echo '#define HAS_UTMPX_SESSION 1' > "$3"
else
	echo '/* sysdep: -utmpxsession */' > "$3"
fi
