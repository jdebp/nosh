#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Convert the /etc/rc.conf{,.local} external configuration formats.
# This is invoked by all.do .
#

# This gets us *only* the configuration variables, safely.
dump_rc() { clearenv read-conf rc.conf printenv ; }

redo-ifchange rc.conf

dump_rc |
awk -F = '/_enable/{print substr($1,1,length($1)-7);}' |
while read -r n
do
	case "$n" in

	# Some rc.conf variables we just ignore here.
	appcafe)		continue ;; # This is handled by a special conversion.
	atm)			continue ;; # This was obsolete in FreeBSD 6.
	compat5x)		continue ;; # This has disappeared as of FreeBSD 10.
	fsck_y)			continue ;;
	ftpproxy)		continue ;; # This is handled by a special conversion.
	gateway)		continue ;; # This is handled by a special conversion.
	ibcs2)			continue ;; # This has disappeared as of FreeBSD 10.
	ike)			continue ;; # isakmpd seems to have disappeared as of FreeBSD 10.
	inetd)			continue ;; # Service management of individual UCSPI services supersedes this.
	ip6addrctl)		continue ;; # This is handled by a special conversion.
	ipv6_gateway)		continue ;; # This is handled by a special conversion.
	ipxgateway)		continue ;; # This has disappeared as of FreeBSD 10.
	jail)			continue ;; # This is a target, not a service.
	jail_*_devfs)		continue ;; # This is a jail configuration item, not a service.
	jail_*_fdescfs)		continue ;; # This is a jail configuration item, not a service.
	jail_*_linprocfs)	continue ;; # This is a jail configuration item, not a service.
	jail_*_linsysfs)	continue ;; # This is a jail configuration item, not a service.
	jail_*_mount)		continue ;; # This is a jail configuration item, not a service.
	jail_*_procfs)		continue ;; # This is a jail configuration item, not a service.
	jail_devfs)		continue ;; # This is a jail configuration item, not a service.
	jail_mount)		continue ;; # This is a jail configuration item, not a service.
	kldxref)		continue ;; # This is not a service.
	ldconfig)		continue ;; # This is handled by a special conversion.
	linux)			continue ;; # This is handled by a special conversion.
	mariadb)		continue ;; # This is handled by a special conversion.
	mixer)			continue ;; # This is replaced by templatized services.
	moused_nondefault)	continue ;; # This is not used for services.
	mysql)			continue ;; # This is handled by a special conversion.
	opensm)			continue ;; # This seems to have disappeared as of FreeBSD 10.
	pcbsdinit)		continue ;; # Service management of individual services supersedes this.
	ppp)			continue ;; # This is handled by a special conversion.
	rfcomm_pppd_server)	continue ;; # This is handled by a special conversion.
	sendmail*)		continue ;; # In the process of being dropped by the BSDs.
	service_manager)	continue ;; # There should not be any variables for this.
	service_manager_svscan)	continue ;; # There should not be any variables for this.
	svr4)			continue ;; # This is handled by a special conversion.
	syslog)			continue ;; # This is handled by a special conversion.
	system_control_normal)	continue ;; # There should not be any variables for this.
	sysvipc)		continue ;; # This is handled by a special conversion.
	tmpmfs)			continue ;; # This is handled by a special conversion.
	uhidd)			continue ;; # This is replaced by templatized services.
	varmfs)			continue ;; # This is handled by a special conversion.
	vboxguest)		continue ;; # There's a nosh-run-virtualbox-guest package for this.
	vboxservice)		continue ;;
	warden)			continue ;; # This is a target, not a service.
	webcamd)		continue ;; # This is handled by a special conversion.

	# Other variables we haven't got around to having bundles for, yet.
	adjkerntz)		continue ;;
	firewall)		continue ;; # TODO: Needs to distribute to ipfs and ipfw0.
	nfsv4_server)		continue ;;
	netif)			continue ;;

	*)			service="$n" ;;
	esac

	# Some services are early services, logged together, that don't have their own individual log services.
	if ! test -d /etc/service-bundles/services/"$service"/
	then
		system-control preset --rcconf-file rc.conf --prefix "cyclog@" -- "$service"
	fi
	system-control preset --rcconf-file rc.conf -- "$service"
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
