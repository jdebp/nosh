#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
#
# Special setup for axfrdns.
# This is invoked by general-services.do .
#

s="`system-control find axfrdns`"

set_if_unset() { if test -z "`system-control print-service-env \"$1\" \"$2\"`" ; then system-control set-service-env "$1" "$2" "$3" ; echo "$s: Defaulted $2 to $3." ; fi ; }

set_if_unset axfrdns ROOT "$s/service/root"

system-control print-service-env axfrdns >> "$3"
