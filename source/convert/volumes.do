#!/bin/sh -e
#
# Convert the /etc/fstab external configuration format.
# This is invoked by all.do .
#

redo-ifchange /etc/fstab

system-control convert-fstab-services --bundle-root /etc/service-bundles/services/ --etc-bundle --overwrite

for i in mount fsck swap dump
do
	for j in /etc/service-bundles/services/$i@*
	do
		rm -f -- "$j/log"
		ln -s -- "../sysinit-log" "$j/log"
		n="${j#/etc/service-bundles/services/$i@}"
		system-control preset --fstab --prefix "$i@" -- "$n"
		if system-control is-enabled "$i@$n"
		then
			echo >> "$3" on "$i@$n"
		else
			echo >> "$3" off "$i@$n"
		fi
	done
done

case "`uname`" in
*BSD)
	# ZFS volumes can have swap enabled via a property org.freebsd:swap=on .
	zfs list -H -o org.freebsd:swap,name -t volume |
	while read state n
	do
		case "${state}" in
		[Oo][Nn])
			system-control preset --fstab --prefix "swap@" -- "-dev-zvol-$n"
			if system-control is-enabled "swap@-dev-zvol-$n"
			then
				echo >> "$3" on "/dev/zvol/$n"
			else
				echo >> "$3" off "/dev/zvol/$n"
			fi
			;;
		*)
			system-control disable "swap@-dev-zvol-$n"
			echo >> "$3" off "/dev/zvol/$n"
			;;
		esac
	done
	;;
esac
