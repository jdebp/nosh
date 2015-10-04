#!/bin/sh -e
#
# Convert the loaded kernel modules list external configuration formats.
# This is invoked by all.do .
#

# This is the systemd system, with several .d files full of module list files.
read_dir() { for d ; do test \! -d "$d" || ( cd "$d" && find -maxdepth 1 -name '*.conf' -a \( -type l -o -type f \) ) | while read i ; do echo "$d" "$i" ; done ; done }
list_modules_linux() { read_dir /etc/modules-load.d/ /lib/modules-load.d/ /usr/lib/modules-load.d/ /usr/local/lib/modules-load.d/ | awk '{ if (!x[$2]++) print $1$2"\n"; }' | xargs grep -- '^[^;#]' ; true ; echo autofs ; echo ipv6 ; echo unix ; }

# This is the BSD system, with settings in /etc/rc.conf{,.local}
read_rc() { if type sysrc >/dev/null 2>&1 ; then sysrc -i -n "$1" || true ; else clearenv read-conf -oknofile /etc/rc.conf read-conf -oknofile /etc/rc.conf.local `which printenv` "$1" || true ; fi }
list_modules_bsd() { 
	( 
		read_rc kld_list || true
# ibcs2 is in the FreeBSD 10 rc.d scripts, but isn't actually present as kernel modules any more.
#		read_rc ibcs2_loaders | sed -e 's:^:ibcs2_:' || true 
	) | fmt -w 1
	true
}

list_modules() { case "`uname`" in Linux) list_modules_linux ;; *BSD) list_modules_bsd ;; esac ; }

case "`uname`" in
*BSD)
	# This is the list of "known" BSD kernel modules for which service bundles are pre-supplied.
	# Note that we cannot just disable kmod@* and cyclog@kmod@* because of VirtualBox and others.
	for n in \
		acpi_video \
		bwi_v3_ucode \
		bwn_v4_ucode \
		cuse4bsd \
		ext2fs \
		fdescfs \
		geom_uzip \
		if_bwi \
		if_bwn \
		iwn1000fw \
		iwn4965fw \
		iwn5000fw \
		iwn5150fw \
		iwn6000fw \
		iwn6000g2afw \
		iwn6000g2bfw \
		iwn6050fw \
		libiconv \
		libmchain \
		linsysfs \
		mmc \
		mmcsd \
		msdosfs_iconv \
		ng_ubt \
		ntfs \
		ntfs_iconv \
		pefs \
		reiserfs \
		runfw \
		scd \
		sem \
		smbfs \
		udf \
		udf_iconv \
		xfs \
		;
	do
		system-control disable "cyclog@kmod@$n"
		system-control disable "kmod@$n"
	done
	;;
esac

list_modules |
while read n
do
	# Note that the way that we are setting up prefixes allows variables such as ntfs_enable in /etc/rc.conf{,.local} .
	system-control preset --prefix "cyclog@kmod@" -- "$n"
	system-control preset --prefix "kmod@" -- "$n"
	if system-control is-enabled "kmod@$n"
	then
		echo >> "$3" on "$n"
	else
		echo >> "$3" off "$n"
	fi
done

true
