#!/bin/sh -e

service_lists="../package/common-services ../package/bsd-services ../package/linux-services ../package/common-etc-services ../package/bsd-etc-services ../package/linux-etc-services ../package/openbsd-ttys ../package/netbsd-ttys ../package/bsd-ttys ../package/linux-ttys ../package/common-ttys ../package/common-sockets ../package/bsd-sockets ../package/linux-sockets"
target_lists="../package/standard-targets"
mount_lists="../package/common-mounts"
command1_lists="../package/commands1 ../package/extra-manpages1"
command8_lists="../package/commands8 ../package/extra-manpages8"

(

echo version.h systemd_names_escape_char.h
echo getty getty-noreset
echo services/colord.service services/console-fb-realizer@.service services/cyclog@.service services/dbus-broker.service services/dbus-daemon.service services/geoclue.service services/gssd.service services/klogd.socket services/local-priv-syslog.socket services/local-syslog.socket services/mountd.service services/network-interfaces.service services/nfsd.service services/nscd.service services/polkitd.service services/redis-sentinel.service services/redis-server.service services/sysctl.service services/system-wide.conf services/tcsd.service services/ttylogin@.service services/upower.service
echo systemd/service-manager.socket
echo convert/per-user/at-spi-dbus-bus.service convert/per-user/clock-applet.service convert/per-user/dbus-daemon.service convert/per-user/dconf-service.service convert/per-user/gam_server.service convert/per-user/gconfd.service convert/per-user/gnome-settings-daemon.service convert/per-user/gnome-terminal-server.service convert/per-user/goa-daemon.service convert/per-user/goa-identity-service.service convert/per-user/gvfs-afc-volume-monitor.service convert/per-user/gvfs-daemon.service convert/per-user/gvfs-goa-volume-monitor.service convert/per-user/gvfs-gphoto2-volume-monitor.service convert/per-user/gvfs-hal-volume-monitor.service convert/per-user/gvfs-metadata.service convert/per-user/gvfs-mtp-volume-monitor.service convert/per-user/gvfs-udisks2-volume-monitor.service convert/per-user/mate-notification-daemon.service convert/per-user/mpd.service convert/per-user/notification-area-applet.service convert/per-user/obex.service convert/per-user/per-user.conf convert/per-user/wnck-applet.service convert/per-user/xfconfd.service convert/per-user/zeitgeist-fts.service
echo convert/dhclient@.service convert/ifconfig@.service convert/run-user-directory@.service convert/user-dbus-daemon.service
echo ${service_lists} ${target_lists} ${mount_lists} ${command1_lists} ${command8_lists}

cat ../package/commands1 ../package/commands8

cat ${command1_lists} | 
while read -r i
do 
	echo "$i.1" "$i.html"
done
cat ${command8_lists} | 
while read -r i
do 
	echo "$i.8" "$i.html"
done

awk '!x[$0]++' ${service_lists} |
sort |
while read -r i
do
	echo services/"$i" services/"cyclog@$i"
done

cat ${target_lists} |
while read -r i
do
	echo targets/"$i"
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
