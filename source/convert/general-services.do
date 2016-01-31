#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
#
# Convert the /etc/rc.conf{,.local} external configuration formats.
# This is invoked by all.do .
#

# This gets us *only* the configuration variables, safely.
dump_rc() { clearenv read-conf rc.conf "`which printenv`" ; }

redo-ifchange rc.conf

dump_rc |
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
	oldnfs_server)		service=nfsserver ;;
	pc-sounddetect)		service=snddetect ;;
	syslogs)		service=wettsteinsyslogd ;;
	natd)			service=kmod@ipdivert ;;
	dummynet)		service=kmod@dummynet ;;
	firewall_nat)		service=kmod@ipfw_nat ;;
	fusefs)			service=kmod@fuse ;;
	random)			service=entropy ;;
	serial)			service=bsd-serial-init ;;

	# Some rc.conf variables we just ignore here.
	service_manager_svscan)	continue ;;
	service_manager)	continue ;;
	system_control_normal)	continue ;;
	adjkerntz)		continue ;;
	compat5x)		continue ;;
	fastboot)		continue ;;
	fsck_y)			continue ;;
	ip6addrctl)		continue ;; # This is handled by a more specific converter.
	tmpmfs)			continue ;; # This is handled by a more specific converter.
	varmfs)			continue ;; # This is handled by a more specific converter.
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
	appcafe)		continue ;;
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
	hostapd)		continue ;;
	iod)			continue ;;
	hpssd)			continue ;;
	ike)			continue ;;
	inetd)			continue ;;
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
	nfs_client)		continue ;;
	nfsv4_server)		continue ;;
	nis_client)		continue ;;
	nis_server)		continue ;;
	nis_yppasswdd)		continue ;;
	nis_ypset)		continue ;;
	nis_ypxfrd)		continue ;;
	opensm)			continue ;;
	pcdm)			continue ;;
	pcsysconfig)		continue ;;
	ppp)			continue ;;
	pppoed)			continue ;;
	rctl)			continue ;;
	rfcomm_pppd_server)	continue ;;
	rpc_lockd)		continue ;;
	rpc_statd)		continue ;;
	rpc_ypupdated)		continue ;;
	rtadvd)			continue ;;
	syscache)		continue ;;
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
	dump_rc |
	awk -F = "/^${n}_/{print substr(\$1,length(\"${n}\")+2),\$2;}" |
	while read -r var val
	do	
		test "$var" = "enable" && continue
		system-control set-service-env "$service" "$var" "$val"
	done
done
