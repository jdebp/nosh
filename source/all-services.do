#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:

common_service_lists="../package/common-services ../package/common-sockets ../package/common-timers ../package/common-ttys"
common_etc_service_lists="../package/common-etc-services"
mount_lists="../package/common-mounts"

case "`uname`" in
	(Linux)
		platform_service_lists="../package/linux-services ../package/linux-sockets ../package/linux-timers ../package/linux-ttys"
		platform_etc_service_lists="../package/linux-etc-services"
		;;
	(OpenBSD)
		platform_service_lists="../package/bsd-services ../package/bsd-sockets ../package/openbsd-services ../package/openbsd-timers ../package/openbsd-ttys"
		platform_etc_service_lists="../package/bsd-etc-services"
		;;
	(FreeBSD)
		platform_service_lists="../package/bsd-services ../package/bsd-sockets ../package/freebsd-services ../package/freebsd-timers ../package/freebsd-ttys"
		platform_etc_service_lists="../package/bsd-etc-services ../package/freebsd-etc-services"
		;;
	(NetBSD)
		platform_service_lists="../package/bsd-services ../package/bsd-sockets ../package/netbsd-services ../package/netbsd-timers ../package/netbsd-ttys"
		platform_etc_service_lists="../package/bsd-etc-services ../package/netbsd-etc-services"
		;;
	(*BSD)
		platform_service_lists="../package/bsd-services ../package/bsd-sockets"
		platform_etc_service_lists="../package/bsd-etc-services"
		;;
	(*)
		platform_service_lists=''
		platform_etc_service_lists=''
		;;
esac

(

echo services/system-wide.conf
echo ${common_service_lists} ${plaform_service_lists} ${common_etc_service_lists} ${platform_etc_service_lists} ${mount_lists}

awk '!x[$0]++' ${common_service_lists} ${platform_service_lists} ${common_etc_service_lists} ${platform_etc_service_lists} |
sort |
while read -r i
do
	printf 'services/%s\n' "$i"
	printf >> "$3" 'services/%s\n' "$i"
	case "$i" in
		(*-log)	;;
		(*)
			if ! grep -q -- "^$i\$" ${common_etc_service_lists} ${platform_etc_service_lists}
			then
				printf 'services/cyclog@%s\n' "$i"
			fi
			;;
	esac
done

# Unlike the other lists, the list of mounts could be empty.
cat /dev/null ${mount_lists} |
while read -r i
do
	echo services/fsck@"$i"
	echo services/mount@"$i"
	printf >> "$3" 'services/fsck@%s\n' "$i"
	printf >> "$3" 'services/mount@%s\n' "$i"
done

) |
xargs -r redo-ifchange
