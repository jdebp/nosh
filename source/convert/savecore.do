#!/bin/sh -e
#
# Special setup for savecore.
# This is invoked by general-services.do .
#

set_if_unset() { if test -z "`system-control print-service-env \"$1\" \"$2\"`" ; then system-control set-service-env "$1" "$2" "$3" ; echo "$s: Defaulted $2 to $3." ; fi ; }

case "`uname`" in
*BSD)
	set_if_unset savecore flags -z
	system-control print-service-env savecore >> "$3"
	;;
*)
	touch "$3"
	;;
esac