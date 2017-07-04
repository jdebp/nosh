#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
#
# Special setup for hostname.
# This is invoked by general-services.do .
#

# This gets us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf "`which printenv`" "$1" ; }
get_var1() { read_rc "$1" || true ; }

redo-ifchange general-services

if s="`system-control find hostname`"
then
	system-control set-service-env "${s}" hostname "`get_var1 hostname`"
fi
