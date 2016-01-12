#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
#
# Convert the /etc/rc.conf{,.local} external configuration formats.
# This is invoked by all.do .
#

# This gets us *only* the configuration variables, safely.
dump() { clearenv read-conf -oknofile /etc/defaults/rc.conf read-conf -oknofile /etc/rc.conf read-conf -oknofile /etc/rc.conf.local `which printenv` ; }

for i in /etc/defaults/rc.conf /etc/rc.conf.local /etc/rc.conf
do
	if test -e "$i"
	then
		redo-ifchange "$i"
	else
		redo-ifcreate "$i"
	fi
done

dump |
awk -F = '/_enable/{print substr($1,1,length($1)-7);}' |
while read -r n
do
	case "$n" in

	# Some rc.conf variables apply to differently named service bundles.
	avahi_daemon)		service=avahi-daemon ;;
	background_fsck)	service=background-fsck ;;
	bsdextended)		service=ugidfw ;;
	cleanvar)		service=cleanuucp ;;
	clear_tmp)		service=cleantmp ;;
	cron)			service=vixiecron ;;
	cupsd)			service=org.cups.cupsd ;;
	hostid)			service=machine-id ;;
	ipfilter)		service=ipf ;;
	local-unbound)		service=unbound ;;
	local_unbound)		service=unbound ;;
	lpd)			service=org.cups.cups-lpd ;;
	nfs_server)		service=nfsd ;;
	pc-sounddetect)		service=snddetect ;;
	syslogs)		service=wettsteinsyslogd ;;
	natd)			service=kmod@ipdivert ;;
	dummynet)		service=kmod@dummynet ;;
	firewall_nat)		service=kmod@ipfw_nat ;;
	fusefs)			service=kmod@fuse ;;
	random)			service=entropy ;;

	# Some rc.conf variables we just ignore here.
	service_manager_svscan)	continue ;;
	service_manager)	continue ;;
	system_control_normal)	continue ;;
	adjkerntz)		continue ;;
	compat5x)		continue ;;
	fastboot)		continue ;;
	fsck_y)			continue ;;
	linux)			continue ;;
	pcbsdinit)		continue ;;
	sendmail*)		continue ;; # In the process of being dropped by the BSDs.
	syslog)			continue ;;
	vboxguest)		continue ;; # There's a nosh-run-virtualbox-guest package for this.
	vboxservice)		continue ;;
	svr4)			continue ;;
	sysvipc)		continue ;;
	ibcs2)			continue ;;
	warden)			continue ;; # This is a target, not a service.
	jail)			continue ;; # This is a target, not a service.
	jail_mount)		continue ;; # This is a jail configuration item, not a service.
	jail_devfs)		continue ;; # This is a jail configuration item, not a service.
	jail_*_mount)		continue ;; # This is a jail configuration item, not a service.
	jail_*_devfs)		continue ;; # This is a jail configuration item, not a service.
	jail_*_fdescfs)		continue ;; # This is a jail configuration item, not a service.
	jail_*_procfs)		continue ;; # This is a jail configuration item, not a service.
	jail_*_linsysfs)	continue ;; # This is a jail configuration item, not a service.
	jail_*_linprocfs)	continue ;; # This is a jail configuration item, not a service.

	# Other variables we haven't got around to having bundles for, yet.
	atm)			continue ;;
	autofs)			continue ;;
	bootparamd)		continue ;;
	chkprintcap)		continue ;;
	firewall)		continue ;;
	ftpd)			continue ;;
	ftpproxy)		continue ;;
	gateway)		continue ;;
	gdm)			continue ;;
	gptboot)		continue ;;
	hald)			continue ;;
	hostapd)		continue ;;
	hpiod)			continue ;;
	hpssd)			continue ;;
	ike)			continue ;;
	inetd)			continue ;;
	ip6addrctl)		continue ;;
	ipropd_master)		continue ;;
	ipropd_slave)		continue ;;
	ipsec)			continue ;;
	ipv6_gateway)		continue ;;
	ipxgateway)		continue ;;
	kdc)			continue ;;
	kern_securelevel)	continue ;;
	kldxref)		continue ;;
	mixer)			continue ;;
	moused_nondefault)	continue ;;
	netwait)		continue ;;
	nfs_client)		continue ;;
	nfsv4_server)		continue ;;
	nis_client)		continue ;;
	nis_server)		continue ;;
	nis_yppasswdd)		continue ;;
	nis_ypset)		continue ;;
	nis_ypxfrd)		continue ;;
	oldnfs_server)		continue ;;
	opensm)			continue ;;
	pcsysconfig)		continue ;;
	ppp)			continue ;;
	pppoed)			continue ;;
	rctl)			continue ;;
	rfcomm_pppd_server)	continue ;;
	rpc_lockd)		continue ;;
	rpc_statd)		continue ;;
	rpc_ypupdated)		continue ;;
	rtadvd)			continue ;;
	ubthidhci)		continue ;;
	uhidd)			continue ;;
	virecover)		continue ;;
	webcamd)		continue ;;

	*)			service="$n" ;;
	esac

	# Some services are early services, logged together, that don't have their own individual log services.
	if ! test -d /etc/service-bundles/services/"$service"/
	then
		system-control preset --prefix "cyclog@" -- "$service"
	fi
	system-control preset -- "$service"
	if system-control is-enabled "$service"
	then
		echo >> "$3" on "$service"
	else
		echo >> "$3" off "$service"
	fi

	# Now convert any variables.
	dump |
	awk -F = "/^${n}_/{print substr(\$1,length(\"${n}\")+2),\$2;}" |
	while read -r var val
	do	
		test "$var" = "enable" && continue
		system-control set-service-env "$service" "$var" "$val"
	done
done

case "`uname`" in
*BSD)
	redo-ifchange jails v9-jails warden
	;;
esac

redo-ifchange dnscache tinydns axfrdns savecore sppp ppp rfcomm_pppd natd openldap static-networking pefs kernel-vt
