#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Special setup for geli and gbde.
# This is invoked by all.do .
# 2016-01-24: This line forces a rebuild because of the new dependency tree.
#

# This gets us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf printenv "$1" ; }
get_conf() { read_rc $1 || true ; }

redo-ifchange rc.conf volumes

r="/etc/service-bundles/services"

find "$r/" -type d -name 'mount@*' |
while read -r n
do
	where="${n#$r/mount@}"
	what="`tail -n 2 \"$n/service/start\"|head -n 1|sed -e 's:/dev/::'`"
	if s="`system-control find \"gdbe@$where\" 2>/dev/null`"
	then
		system-control set-service-env "$s" lock "`get_conf gbde_lock_\"$what\"`"
		system-control print-service-env "$s" >> "$3"
	fi
	if s="`system-control find \"geli@$where\" 2>/dev/null`"
	then
		system-control set-service-env "$s" flags "`get_conf eli_\"$what\"_flags`"
		system-control set-service-env "$s" autodetach "`get_conf geli_\"$what\"_autodetach`"
		system-control print-service-env "$s" >> "$3"
	fi
done
