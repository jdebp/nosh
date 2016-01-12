#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
#
# Special setup for geli and gbde.
# This is invoked by volumes.do .
#

# This gets us *only* the configuration variables, safely.
read_rc() { if type sysrc >/dev/null 2>&1 ; then sysrc -i -n "$1" ; else clearenv read-conf -oknofile /etc/defaults/rc.conf read-conf -oknofile /etc/rc.conf read-conf -oknofile /etc/rc.conf.local `which printenv` "$1" ; fi }
get_conf() { read_rc $1 || true ; }

for i in /etc/defaults/rc.conf /etc/rc.conf.local /etc/rc.conf
do
	if test -e "$i"
	then
		redo-ifchange "$i"
	else
		redo-ifcreate "$i"
	fi
done

r="/etc/service-bundles/services"

find "$r/" -type d -name 'mount@*' |
while read -r n
do
	where="${n#$r/mount@}"
	what="`tail -n 2 \"$n/service/start\"|head -n 1|sed -e 's:/dev/::'`"
	if 2>/dev/null system-control find "gdbe@$where"
	then
		system-control set-service-env "gdbe@$where" lock "`get_conf gbde_lock_\"$what\"`"
		system-control print-service-env "gbde@$where" >> "$3"
	fi
	if 2>/dev/null system-control find "geli@$where"
	then
		system-control set-service-env "geli@$where" flags "`get_conf eli_\"$what\"_flags`"
		system-control set-service-env "geli@$where" autodetach "`get_conf geli_\"$what\"_autodetach`"
		system-control print-service-env "geli@$where" >> "$3"
	fi
done
