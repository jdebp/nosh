#!/bin/sh -e
service_lists="../package/common-services ../package/bsd-services ../package/linux-services ../package/common-etc-services ../package/bsd-etc-services ../package/linux-etc-services ../package/bsd-ttys ../package/linux-ttys ../package/common-ttys ../package/common-sockets ../package/bsd-sockets ../package/linux-sockets"
target_lists="../package/standard-targets"
mount_lists="../package/common-mounts"
command1_lists="../package/commands1 ../package/extra-manpages1"
command8_lists="../package/commands8 ../package/extra-manpages8"
redo-ifchange version.h services/ttylogin@.service services/klogd.socket services/nfsd.service services/polkitd.service services/local-syslog.socket services/sysctl.service services/dbus.service services/console-fb-realizer@.service services/nscd.service services/static-networking.service convert/user-dbus.service
redo-ifchange ${service_lists} ${target_lists} ${mount_lists} ${command1_lists} ${command8_lists}
cat ../package/commands1 ../package/commands8 | xargs redo-ifchange
sort ${service_lists} |
while read i
do
	echo services/"$i" services/"cyclog@$i"
done | xargs redo-ifchange
redo-ifchange ${target_lists}
cat ${target_lists} |
while read i
do
	echo targets/"$i"
done | xargs redo-ifchange
# Unlike the other lists, the list of mounts could be empty.
cat /dev/null ${mount_lists} |
while read i
do
	echo services/fsck@"$i"
	echo services/mount@"$i"
done | xargs -r redo-ifchange
cat ${command1_lists} | 
while read i
do 
	echo "$i.1" "$i.html"
done | xargs redo-ifchange
cat ${command8_lists} | 
while read i
do 
	echo "$i.8" "$i.html"
done | xargs redo-ifchange
