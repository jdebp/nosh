#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:

service_lists="../package/common-services ../package/bsd-services ../package/linux-services ../package/common-etc-services ../package/bsd-etc-services ../package/linux-etc-services ../package/common-sockets ../package/bsd-sockets ../package/linux-sockets ../package/common-timers ../package/linux-timers ../package/bsd-timers ../package/openbsd-ttys ../package/netbsd-ttys ../package/bsd-ttys ../package/linux-ttys ../package/common-ttys"
mount_lists="../package/common-mounts"

(

echo services/colord.service services/console-fb-realizer@.service services/cyclog@.service services/dbus-broker.service services/dbus-daemon.service services/exim4-smtp-relay@.service services/exim4-smtp-submission@.service services/geoclue.service services/gssd.service services/klogd.socket services/local-priv-syslog.socket services/local-syslog.socket services/mountd.service services/network-interfaces.service services/nfsd.service services/nscd.service services/phpsessionclean.service services/polkitd.service services/redis-sentinel.service services/redis-server.service services/sysctl.service services/system-wide.conf services/tcsd.service services/ttylogin@.service services/upower.service
echo ${service_lists} ${mount_lists}

awk '!x[$0]++' ${service_lists} |
sort |
while read -r i
do
	echo services/"$i" services/"cyclog@$i"
done

# Unlike the other lists, the list of mounts could be empty.
cat /dev/null ${mount_lists} |
while read -r i
do
	echo services/fsck@"$i"
	echo services/mount@"$i"
done

) | 
xargs -r redo-ifchange
