#!/bin/sh -e
name="$1"
base="`basename \"${name}\"`"

ifchange_follow() {
	local i l
	for i
	do	
		l="`readlink \"$i\"`" || true
		[ -n "$l" ] && redo-ifchange "`dirname \"$i\"`/$l"
		redo-ifchange "$i"
	done
}

redo-ifchange system-control 

case "${base}" in
*@*) 
	template="${name%%@*}"
	if test -e "${template}"@.service
	then
		unit="${name}".service
		unitfile="${template}"@.service
	else
		echo 1>&2 "$0: ${name}: Don't know what to use to build this."
		exit 1
	fi
	;;
*)
	if test -e "${name}".socket
	then
		unit="${name}".socket
		if test -e "${name}"@.service
		then
			ifchange_follow "${name}"@.service
		else
			redo-ifcreate "${name}"@.service
			ifchange_follow "${name}".service
		fi
	elif test -e "${name}".service
	then
		redo-ifcreate "${name}".socket
		unit="${name}".service
	else
		echo 1>&2 "$0: ${name}: Don't know what to use to build this."
		exit 1
	fi
	unitfile="${unit}"
	;;
esac

ifchange_follow "${unitfile}"

mkdir -p services.new

rm -r -f services.new/"${base}"

case "${base}" in
cyclog@*)
	escape="--alt-escape --escape-instance"
	;;
*)
	escape=
	;;
esac

case "`uname`" in
Linux)	etc_services="../package/common-etc-services ../package/linux-etc-services" ;;
*BSD)	etc_services="../package/common-etc-services ../package/bsd-etc-services" ;;
esac

case "${base}" in
ppp-log|sppp-log|rfcomm_pppd-log|natd-log|cyclog@*) 
	log=
	etc=
	;;
emergency-login@console) 
	log=
	etc=--etc-bundle
	;;
sysinit-log) 
	log=
	etc=--etc-bundle
	;;
mount@*|fsck@*|monitor-fsck-progress)
	log="../sysinit-log"
	etc=--etc-bundle
	;;
devd)
	log="../${base}-log"
	etc=--etc-bundle
	;;
udev|udev-trigger-add@*)
	log="../udev-log"
	etc=--etc-bundle
	;;
busybox-mdev|busybox-mdev-rescan)
	log="../busybox-mdev-log"
	etc=--etc-bundle
	;;
suckless-mdev|suckless-mdev-rescan)
	log="../suckless-mdev-log"
	etc=--etc-bundle
	;;
*) 
	redo-ifchange -- ${etc_services}
	if grep -q -- "^${base}\$" ${etc_services}
	then
		log="../sysinit-log"
		etc=--etc-bundle
	else
		log="../cyclog@${base}"
		etc=
	fi
	;;
esac

./system-control convert-systemd-units --no-systemd-quirks ${escape} ${etc} --bundle-root services.new/ "${unit}"

test -n "${log}" && ln -s -f "${log}" services.new/"${base}"/log

if grep -q "envdir env" services.new/"${base}"/service/start services.new/"${base}"/service/run services.new/"${base}"/service/stop
then
	mkdir -m 0755 services.new/"${base}"/service/env
fi

case "${base}" in
cyclog@*) 
	ln -f -s -- /var/log/sv/"${base#cyclog@}" services.new/"${base}"/main
	;;
devd-log) 
	ln -f -s -- /var/log/sv/devd services.new/"${base}"/main
	;;
sppp-log) 
	ln -f -s -- /var/log/sv/sppp services.new/"${base}"/main
	;;
natd-log) 
	ln -f -s -- /var/log/sv/natd services.new/"${base}"/main
	;;
sysinit-log) 
	ln -f -s -- /var/log/sv/sysinit services.new/"${base}"/main
	;;
esac

case "${base}" in
kmod@vboxadd|kmod@vboxsf|kmod@vboxguest|kmod@vboxvideo)
	ln -f -s -- /etc/service-bundles/targets/virtualbox-guest services.new/"${base}"/wanted-by/
	;;
kmod@vboxdrv|kmod@vboxnetadp|kmod@vboxnetflt|kmod@vboxpci)
	ln -f -s -- /etc/service-bundles/targets/virtualbox-host services.new/"${base}"/wanted-by/
	;;
kmod@uvesafb)
	ln -f -s -- /etc/service-bundles/targets/frame-buffer services.new/"${base}"/wanted-by/
	;;
cyclog@ttylogin@*)
	rm -f -- services.new/"${base}"/wanted-by/workstation
	ln -s -f -- ../../"${base#cyclog@}" services.new/"${base}"/wanted-by/
	;;
cyclog@VBoxService|cyclog@kmod@vboxadd|cyclog@kmod@vboxsf|cyclog@kmod@vboxguest|cyclog@kmod@vboxvideo)
	rm -f -- services.new/"${base}"/wanted-by/workstation
	ln -f -s -- /etc/service-bundles/targets/virtualbox-guest services.new/"${base}"/wanted-by/
	;;
cyclog@kmod@vboxdrv|cyclog@kmod@vboxnetadp|cyclog@kmod@vboxnetflt|cyclog@kmod@vboxpci)
	rm -f -- services.new/"${base}"/wanted-by/workstation
	ln -f -s -- /etc/service-bundles/targets/virtualbox-host services.new/"${base}"/wanted-by/
	;;
console-multiplexor@head0)
	for i in 1 2 3
	do 
		ln -s -f /run/dev/vc"$i" services.new/"${base}"/service/
	done
	;;
ttylogin@tty[0-9]*)
	case "`uname`" in
	Linux)
		# These services are started on demand by ttylogin-starter.
		rm -f -- services.new/"${base}"/wanted-by/multi-user
		;;
	*BSD)
		;;
	esac
	;;
console-fb-realizer@head0)
	mkdir -m 0755 services.new/"${base}"/service/kbdmaps
	mkdir -m 0755 services.new/"${base}"/service/fonts
	;;
dnscache)
	mkdir -m 0755 services.new/"${base}"/service/root
	mkdir -m 0755 services.new/"${base}"/service/root/ip
	mkdir -m 0755 services.new/"${base}"/service/root/servers
	;;
tinydns)
	mkdir -m 0755 services.new/"${base}"/service/root

	for i in alias childns ns mx host
	do
		echo '#!/command/execlineb -S0' > services.new/"${base}"/service/root/add-"$i"
		echo "tinydns-edit data data.new" >> services.new/"${base}"/service/root/add-"$i"
		echo "add $i \$@" >> services.new/"${base}"/service/root/add-"$i"
		chmod 0755 services.new/"${base}"/service/root/add-"$i"
	done

	echo 'data.cdb: data; tinydns-data' > services.new/"${base}"/service/root/Makefile
	chmod 0644 services.new/"${base}"/service/root/Makefile
	;;
axfrdns)
	ln -s ../../tinydns/service/root services.new/"${base}"/service/
	;;
esac

if test -e "${name}".tmpfiles 
then
	redo-ifchange "${name}".tmpfiles 
	cp -a "${name}".tmpfiles services.new/"${base}"/service/tmpfiles
fi
if test -e "${name}".helper
then
	redo-ifchange "${name}".helper
	cp -a "${name}".helper services.new/"${base}"/service/helper
fi

rm -r -f -- "$3"
mv -- services.new/"${base}" "$3"
