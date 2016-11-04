#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
#
# Special setup for geli and gbde.
# This is invoked by all.do .
# 2016-01-24: This line forces a rebuild because of the new dependency tree.
#

# This gets us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf "`which printenv`" "$1" ; }
get_conf() { read_rc $1 || true ; }

redo-ifchange rc.conf volumes

r="/etc/service-bundles/services"

find "$r/" -type d -name 'mount@*' |
while read -r n
do
	where="${n#$r/mount@}"
	what="`tail -n 2 \"$n/service/start\"|head -n 1|sed -e 's:/dev/::'`"
	if >/dev/null 2>&1 system-control find "gdbe@$where"
	then
		system-control set-service-env "gdbe@$where" lock "`get_conf gbde_lock_\"$what\"`"
		system-control print-service-env "gbde@$where" >> "$3"
	fi
	if >/dev/null 2>&1 system-control find "geli@$where"
	then
		system-control set-service-env "geli@$where" flags "`get_conf eli_\"$what\"_flags`"
		system-control set-service-env "geli@$where" autodetach "`get_conf geli_\"$what\"_autodetach`"
		system-control print-service-env "geli@$where" >> "$3"
	fi
done
