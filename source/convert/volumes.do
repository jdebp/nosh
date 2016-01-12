#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
#
# Convert the /etc/fstab external configuration format.
# This is invoked by all.do .
#

redo-ifchange /etc/fstab

r="/etc/service-bundles/services/"
o="--etc-bundle --overwrite"

system-control convert-fstab-services --bundle-root "$r" $o

for i in mount fsck swap dump
do
	for j in "$r$i@"*
	do
		test -e "$j/" || continue
		n="${j#$r$i@}"
		rm -f -- "$j/log"
		ln -s -- "../sysinit-log" "$j/log"
		system-control disable "$i@$n"
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
	# ZFS non-swap volumes
	zfs list -H -o canmount,name,mountpoint -t filesystem |
	awk "{
		if (\$3 == \"none\" || \$1 == \"no\") next;
		print \"system-control write-volume-service-bundles --bundle-root \\\"$r\\\" $o zfs \\\"\"\$2\"\\\" \\\"\"\$3\"\\\"\";
	}" | 
	sh -e
	zfs list -H -o canmount,mountpoint -t filesystem |
	awk '{ if ($2 != "none" && $1 == "noauto") { gsub("-","\\x2d",$2); gsub("/","-",$2); print "system-control disable mount@\""$2"\""; } }' | 
	sh -e
	zfs list -H -o canmount,mountpoint -t filesystem |
	awk '{ if ($2 != "none" && $1 == "on") { gsub("-","\\x2d",$2); gsub("/","-",$2); print "system-control enable mount@\""$2"\""; } }' | 
	sh -e
	zfs list -H -o canmount,mountpoint -t filesystem |
	awk '{ if ($2 != "none") { gsub("-","\\x2d",$2); gsub("/","-",$2); print $1" mount@"$2; } }' >> "$3"
	zfs list -H -o canmount,mountpoint -t filesystem / |
	awk '{ if ($2 != "none" && $1 != "off") { gsub("-","\\x2d",$2); gsub("/","-",$2); print "system-control enable mount@\""$2"\""; } }' | 
	sh -e

	# ZFS volumes can have swap enabled via a property org.freebsd:swap=on .
	zfs list -H -o org.freebsd:swap,name -t volume |
	while read -r state n
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

redo-ifchange geom mdmfs
