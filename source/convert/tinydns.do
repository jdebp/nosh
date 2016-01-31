#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
#
# Special setup for tinydns.
# This is invoked by general-services.do .
#

s="`system-control find tinydns`"

set_if_unset() { if test -z "`system-control print-service-env \"$1\" \"$2\"`" ; then system-control set-service-env "$1" "$2" "$3" ; echo "$s: Defaulted $2 to $3." ; fi ; }

redo-ifchange general-services

set_if_unset tinydns IP 127.53.0.1
set_if_unset tinydns ROOT "$s/service/root"

system-control print-service-env tinydns >> "$3"

test -r "$s/service/root/data" || echo '.' > "$s/service/root/data"
test -r "$s/service/root/Makefile" || echo 'data.cdb : data ; tinydns-data' > "$s/service/root/Makefile"
