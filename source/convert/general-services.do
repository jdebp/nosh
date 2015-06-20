#!/bin/sh -e
#
# Convert the /etc/rc.conf{,.local} external configuration formats.
# This is invoked by all.do .
#

# This gets us *only* the configuration variables, safely.
dump() { clearenv read-conf -oknofile /etc/rc.conf read-conf -oknofile /etc/rc.conf.local `which printenv` ; }

for i in /etc/rc.conf{,.local}
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
while read n
do
	case "$n" in
	# Some rc.conf variables apply to differently named service bundles.
	avahi_daemon)		service=avahi-daemon ;;
	background_fsck)	service=background-fsck ;;
	hostid)			service=machine-id ;;
	cron)			service=vixiecron ;;
	cupsd)			service=org.cups.cupsd ;;
	lpd)			service=org.cups.cups-lpd ;;
	syslogs)		service=wettsteinsyslogd ;;
	cleanvar)		service=cleanuucp ;;
	nfs_server)		service=nfsd ;;
	local-unbound)		service=unbound ;;
	# Some rc.conf variables we just ignore here.
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
	# Other variables we haven't got around to having bundles for, yet.
	clear_tmp)		service=cleartmp ; continue ;;
	fusefs)			continue ;;
	gdm)			continue ;;
	hald)			continue ;;
	hpiod)			continue ;;
	hpssd)			continue ;;
	mixer)			continue ;;
	pefs)			continue ;;
	uhidd)			continue ;;
	uhidd)			continue ;;
	warden)			continue ;;
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
	while read var val
	do	
		test "$var" = "enable" && continue
		rcctl set "$service" "$var" "$val"
	done
done

redo-ifchange dnscache tinydns
