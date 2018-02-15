#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:

name="$1"
base="`basename \"${name}\"`"

ifchange_follow() {
	local i l
	for i
	do	
		while test -n "$i"
		do
			redo-ifchange "$i"
			l="`readlink \"$i\"`" || break
			case "$l" in
			/*)	i="$l" ;;
			*)	i="`dirname \"$i\"`/$l" || break ;;
			esac
		done
	done
}

redo-ifchange system-control 

# ###
# Work out what files are going to be used and declare build dependencies from them.
# ###

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

# ###
# Decide the parameters for the conversion tool, and the location of the service and (if any) its logging service.
# ###

case "${base}" in
cyclog@*)
	escape="--alt-escape --escape-instance"
	;;
ppp-log|sppp-log|rfcomm_pppd-log|snort-log|natd-log|ataidle-log|autobridge-log|brltty-log|dhclient-log|dhcpcd-log|udhcpc-log|mixer-log|ifconfig-log|iovctl-log|uhidd-log|webcamd-log|system-installer-log|wlandebug-log) 
	escape="--alt-escape --escape-prefix"
	;;
*)
	escape=
	;;
esac

case "`uname`" in
Linux)	etc_services="../package/common-etc-services ../package/linux-etc-services" ;;
*BSD)	etc_services="../package/common-etc-services ../package/bsd-etc-services" ;;
*)	etc_services="../package/common-etc-services" ;;
esac

case "${base}" in
cyclog@*|dbus) 
	log=
	etc=
	after=
	;;
ppp-log|sppp-log|rfcomm_pppd-log|snort-log|natd-log|ataidle-log|autobridge-log|brltty-log|dhclient-log|dhcpcd-log|udhcpc-log|mixer-log|ifconfig-log|iovctl-log|uhidd-log|webcamd-log|system-installer-log|wlandebug-log)
	log=
	etc=
	after=
	;;
emergency-login@console) 
	log=
	etc=--etc-bundle
	after=
	;;
sysinit-log|suckless-mdev-log|busybox-mdev-log|vdev-log|devd-log) 
	log=
	etc=--etc-bundle
	after=
	;;
mount@*|fsck@*|monitor-fsck-progress)
	log="../sysinit-log"
	etc=--etc-bundle
	after=
	;;
devd)
	log="../devd-log"
	etc=--etc-bundle
	after="log"
	;;
systemd-udev|systemd-udev-trigger-add@*)
	log="../systemd-udev-log"
	etc=--etc-bundle
	after="log"
	;;
udev|udev-trigger-add@*|udev-finish)
	log="../udev-log"
	etc=--etc-bundle
	after="log"
	;;
busybox-mdev|busybox-mdev-rescan)
	log="../busybox-mdev-log"
	etc=--etc-bundle
	after="log"
	;;
suckless-mdev|suckless-mdev-rescan)
	log="../suckless-mdev-log"
	etc=--etc-bundle
	after="log"
	;;
trueos-update-finish|trueos-install-finish|freebsd-update-finish|freebsd-install-finish)
	log="../system-installer-log"
	etc=
	after="log"
	;;
*) 
	redo-ifchange -- ${etc_services}
	if grep -q -- "^${base}\$" ${etc_services}
	then
		log="../sysinit-log"
		etc=--etc-bundle
		after=
	else
		log="../cyclog@${base}"
		etc=
		after="log"
	fi
	;;
esac

# ###
# Compile the source into a bundle.
# ###

install -d services.new

rm -r -f services.new/"${base}"

./system-control convert-systemd-units --no-systemd-quirks --no-generation-comment ${escape} ${etc} --bundle-root services.new/ "${unit}"

test -n "${log}" && ln -s -f "${log}" services.new/"${base}"/log
test -n "${after}" && ln -s -f "../${after}" services.new/"${base}"/after/

# ###
# Add in the environment directory and UCSPI ruleset infrastructure, if required.
# ###
if grep -q "envdir env" services.new/"${base}"/service/start services.new/"${base}"/service/run services.new/"${base}"/service/stop
then
	install -d -m 0755 services.new/"${base}"/service/env
fi

if grep -q "ucspi-socket-rules-check" services.new/"${base}"/service/start services.new/"${base}"/service/run services.new/"${base}"/service/stop
then
	install -d -m 0755 services.new/"${base}"/service/ip4 services.new/"${base}"/service/ip4/10.0.0.0_8 services.new/"${base}"/service/ip4/127.0.0.0_8 services.new/"${base}"/service/ip4/192.168.0.0_16
	install -d -m 0755 services.new/"${base}"/service/ip6 services.new/"${base}"/service/ip6/::1_128 services.new/"${base}"/service/ip6/::ffff:10.0.0.0_104 services.new/"${base}"/service/ip6/::ffff:127.0.0.0_104 services.new/"${base}"/service/ip6/::ffff:192.168.0.0_112
fi

# ###
# Provide the "main/" courtesy link in logging services.
# ###
case "${base}" in
cyclog@*) 
	ln -f -s -- /var/log/sv/"${base#cyclog@}" services.new/"${base}"/main
	;;
devd-log|sysinit-log)
	ln -f -s -- /var/log/sv/"${base%-log}" services.new/"${base}"/main
	;;
ppp-log|sppp-log|rfcomm_pppd-log|snort-log|natd-log|ataidle-log|autobridge-log|brltty-log|dhclient-log|dhcpcd-log|udhcpc-log|mixer-log|ifconfig-log|iovctl-log|uhidd-log|webcamd-log|system-installer-log|wlandebug-log)
	ln -f -s -- /var/log/sv/"${base%-log}" services.new/"${base}"/main
	;;
esac

# ###
# Populate the bundles with extra relationships, files, and directories peculiar to the services.
# ###
case "${base}" in
kmod@vboxvideo)
	ln -f -s -- /etc/service-bundles/targets/virtualbox-guest services.new/"${base}"/wanted-by/
	ln -f -s -- ../../kmod@uvesafb services.new/"${base}"/after/
	;;
kmod@vboxadd|kmod@vboxsf|kmod@vboxguest)
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
http[46]d|ftp[46]d)
	install -m 0644 /dev/null services.new/"${base}"/service/ip4/10.0.0.0_8/allow
	install -m 0644 /dev/null services.new/"${base}"/service/ip4/127.0.0.0_8/allow
	install -m 0644 /dev/null services.new/"${base}"/service/ip4/192.168.0.0_16/allow
	install -m 0644 /dev/null services.new/"${base}"/service/ip6/::1_128/allow
	install -m 0644 /dev/null services.new/"${base}"/service/ip6/::ffff:10.0.0.0_104/allow
	install -m 0644 /dev/null services.new/"${base}"/service/ip6/::ffff:127.0.0.0_104/allow
	install -m 0644 /dev/null services.new/"${base}"/service/ip6/::ffff:192.168.0.0_112/allow
	;;
console-fb-realizer@head0)
	install -d -m 0755 services.new/"${base}"/service/kbdmaps
	install -d -m 0755 services.new/"${base}"/service/fonts
	;;
dnscache)
	install -d -m 0755 services.new/"${base}"/service/root
	install -d -m 0755 services.new/"${base}"/service/root/ip
	install -d -m 0755 services.new/"${base}"/service/root/servers
	;;
tinydns)
	install -d -m 0755 services.new/"${base}"/service/root

	for i in alias childns ns mx host
	do
		echo '#!/command/execlineb -S0' > services.new/"${base}"/service/root/add-"$i"
		echo "tinydns-edit data data.new" >> services.new/"${base}"/service/root/add-"$i"
		echo "add $i \$@" >> services.new/"${base}"/service/root/add-"$i"
		chmod 0755 services.new/"${base}"/service/root/add-"$i"
	done
	;;
axfrdns)
	ln -s ../../tinydns/service/root services.new/"${base}"/service/
	;;
dbus-daemon)
	redo-ifchange services/system-wide.conf
	install -m 0644 -- services/system-wide.conf services.new/"${base}"/service/system-wide.conf
	;;
esac

# ###
# Add in any helpers and snippets.
# ###
if test -e "${name}".tmpfiles 
then
	redo-ifchange "${name}".tmpfiles 
	install -m 0644 -- "${name}".tmpfiles services.new/"${base}"/service/tmpfiles
fi
if test -e "${name}".helper
then
	redo-ifchange "${name}".helper
	cp -p "${name}".helper services.new/"${base}"/service/helper
fi

rm -r -f -- "$3"
mv -- services.new/"${base}" "$3"
