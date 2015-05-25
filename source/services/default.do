#!/bin/sh -e
name="$1"
base="`basename \"${name}\"`"

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
			redo-ifchange "`readlink -f \"${name}"@.service`" "${name}"@.service
		else
			redo-ifcreate "${name}"@.service
			redo-ifchange "`readlink -f \"${name}".service`" "${name}".service
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

redo-ifchange "`readlink -f \"${unitfile}\"`" "${unitfile}"

mkdir -p services.new

rm -r -f services.new/"${base}"

case "${base}" in
cyclog@*)
	escape="--alt-escape"
	;;
mount@*|fsck@*|ttylogin@*|console-multiplexor@*|console-fb-realizer@*)
	escape="--unescape-instance"
	;;
*)
	escape=
	;;
esac

case "`uname`" in
Linux)	sysinit_services="../package/common-sysinit-services ../package/linux-sysinit-services" ;;
*BSD)	sysinit_services="../package/common-sysinit-services ../package/bsd-sysinit-services" ;;
esac

case "${base}" in
cyclog@*) 
	log=
	sysinit=
	;;
sysinit-log) 
	log=
	sysinit=--etc-service
	;;
mount@*|fsck@*|kmod@*|emergency-login@*)
	log="../sysinit-log"
	sysinit=--etc-service
	;;
devd)
	log="../${base}-log"
	sysinit=--etc-service
	;;
*) 
	redo-ifchange -- ${sysinit_services}
	if fgrep -q -- "${base}" ${sysinit_services}
	then
		log="../sysinit-log"
		sysinit=--etc-service
	else
		log="../cyclog@${base}"
		sysinit=
	fi
	;;
esac

./system-control convert-systemd-units ${escape} ${sysinit} --bundle-root services.new/ "${unit}"

test -n "${log}" && ln -s -f "${log}" services.new/"${base}"/log

case "${base}" in
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
	ln -s -f ../../ttylogin@tty1 ../../ttylogin@ttyv0 services.new/"${base}"/conflicts/
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

if grep -q "envdir env" services.new/"${base}"/service/run 
then
	mkdir -m 0755 services.new/"${base}"/service/env
fi

rm -r -f -- "$3"
mv -- services.new/"${base}" "$3"
