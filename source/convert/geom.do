#!/bin/sh -e
#
# Special setup for geli and gbde.
# This is invoked by volumes.do .
#

set_if_unset() { if test -z "`system-control print-service-env \"$1\" \"$2\"`" ; then system-control set-service-env "$1" "$2" "$3" ; echo "$s: Defaulted $2 to $3." ; fi ; }

# This gets us *only* the configuration variables, safely.
get_conf() { clearenv read-conf -oknofile /etc/rc.conf read-conf -oknofile /etc/rc.conf.local `which printenv $1` || true ; }

for i in /etc/rc.conf.local /etc/rc.conf
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
while read n
do
	where="${n#$r/mount@}"
	what="`tail -n 2 \"$n/service/start\"|head -n 1|sed -e 's:/dev/::'`"
	if 2>/dev/null system-control find "gdbe@$where"
	then
		set_if_unset "gdbe@$where" lock "`get_conf gbde_lock_\"$what\"`"
		system-control print-service-env "gbde@$where" >> "$3"
	fi
	if 2>/dev/null system-control find "geli@$where"
	then
		set_if_unset "geli@$where" flags "`get_conf eli_\"$what\"_flags`"
		set_if_unset "geli@$where" autodetach "`get_conf geli_\"$what\"_autodetach`"
		system-control print-service-env "geli@$where" >> "$3"
	fi
done
