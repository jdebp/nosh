#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:

service_lists="../package/common-services ../package/bsd-services ../package/linux-services ../package/common-etc-services ../package/bsd-etc-services ../package/linux-etc-services ../package/common-sockets ../package/bsd-sockets ../package/linux-sockets ../package/common-timers ../package/linux-timers ../package/bsd-timers ../package/openbsd-ttys ../package/netbsd-ttys ../package/bsd-ttys ../package/linux-ttys ../package/common-ttys"
mount_lists="../package/common-mounts"

(
echo services/system-wide.conf 
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
