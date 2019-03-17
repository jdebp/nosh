#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Convert the /etc/fstab external configuration format.
# This is invoked by all.do .
# 2016-01-24: This line forces a rebuild because of the new dependency tree.
#

redo-ifchange /etc/fstab

r="/etc/service-bundles/services/"
o="--etc-bundle --overwrite"

case "`uname`" in
Linux)	esc='\' ;;
*BSD)	esc='_' ;;
esac

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
	awk '{ if ($3 != "none" && $1 != "off") print "system-control write-volume-service-bundles --bundle-root \"'"$r"'\" '"$o"' zfs \""$2"\" \""$3"\""; }' | 
	sh -e
	zfs list -H -o canmount,mountpoint -t filesystem |
	awk '{ if ($2 != "none" && $1 == "noauto") { gsub("-","'${esc}'x2d",$2); gsub("/","-",$2); print "system-control disable mount@\""$2"\""; } }' | 
	sh -e
	zfs list -H -o canmount,mountpoint -t filesystem |
	awk '{ if ($2 != "none" && $1 == "on") { gsub("-","'${esc}'x2d",$2); gsub("/","-",$2); print "system-control enable mount@\""$2"\""; } }' | 
	sh -e
	zfs list -H -o canmount,mountpoint -t filesystem |
	awk '{ if ($2 != "none") { gsub("-","'${esc}'x2d",$2); gsub("/","-",$2); print $1" mount@"$2; } }' >> "$3"
	zfs list -H -o canmount,mountpoint -t filesystem / |
	awk '{ if ($2 != "none" && $1 != "off") { gsub("-","'${esc}'x2d",$2); gsub("/","-",$2); print "system-control enable mount@\""$2"\""; } }' | 
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

for i in mount fsck
do
	if ! system-control find "$i@-" >/dev/null 2>&1
	then
		echo 1>&2 WARNING: / volume $i@ service bundle is absent.
	elif ! system-control is-enabled "$i@-"
	then
		# This is a semi-legitimate thing to do, since this is always a remount/update, but one rarely wants to do it.
		echo 1>&2 WARNING: / volume $i@ service bundle is disabled.
	fi
done
