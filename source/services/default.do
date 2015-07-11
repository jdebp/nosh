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
cyclog@*) 
	log=
	etc=
	;;
sysinit-log) 
	log=
	etc=--etc-bundle
	;;
mount@*|fsck@*)
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
	log="../mdev-log"
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

./system-control convert-systemd-units ${escape} ${etc} --bundle-root services.new/ "${unit}"

test -n "${log}" && ln -s -f "${log}" services.new/"${base}"/log

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
cyclog@VBoxService)
	rm -f -- services.new/"${base}"/wanted-by/workstation
	ln -f -s -- /etc/service-bundles/targets/virtualbox-guest services.new/"${base}"/wanted-by/
	;;
console-multiplexor@head0)
	for i in 1 2 3
	do 
		ln -s -f /run/dev/vc"$i" services.new/"${base}"/service/
	done
	;;
console-fb-realizer@head0)
	redo-ifchange console-terminal-emulator
	ln -s -f console-terminal-emulator console-convert-kbdmap

	mkdir -m 0755 services.new/"${base}"/service/kbdmaps
	mkdir -m 0755 services.new/"${base}"/service/fonts
	ln -s -f kbdmaps/us.kbdmap services.new/"${base}"/service/kbdmap

	case "`uname`" in
	Linux)
		./console-convert-kbdmap /dev/null > services.new/"${base}"/service/kbdmaps/us.kbdmap
		ln -s -f /dev/fb0 services.new/"${base}"/service/fb
		ln -s -f /dev/input/event0 services.new/"${base}"/service/event
		;;
	*BSD)
		for k in uk us ru de fr 
		do
			./console-convert-kbdmap /usr/share/vt/keymaps/"$k".kbd > services.new/"${base}"/service/kbdmaps/"$k".kbdmap
		done
		ln -s -f /dev/ttyv0 services.new/"${base}"/service/fb
		ln -s -f /dev/ttyv0 services.new/"${base}"/service/event
		;;
	esac
	;;
esac

if grep -q "envdir env" services.new/"${base}"/service/start services.new/"${base}"/service/run services.new/"${base}"/service/stop
then
	mkdir -m 0755 services.new/"${base}"/service/env
fi

test -e "${name}".tmpfiles && cp -a "${name}".tmpfiles services.new/"${base}"/service/
test -e "${name}".helper && cp -a "${name}".helper services.new/"${base}"/service/

rm -r -f -- "$3"
mv -- services.new/"${base}" "$3"
