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
			(/*)	i="$l" ;;
			(*)	i="`dirname \"$i\"`/$l" || break ;;
			esac
		done
	done
}

# ###
# Work out what files are going to be used and declare build dependencies from them.
# ###

case "${base}" in
(*@*)
	template="${name%%@*}"
	if test -e "${template}"@.socket
	then
		suffix=.socket
		unit="${name}${suffix}"
		service_unit="${template}".service
	elif test -e "${template}"@.timer
	then
		redo-ifcreate "${template}"@.socket
		suffix=.timer
		unit="${name}${suffix}"
		service_unit="${template}".service
	elif test -e "${template}"@.service
	then
		redo-ifcreate "${template}"@.socket
		redo-ifcreate "${template}"@.timer
		suffix=.service
		unit="${name}${suffix}"
	else
		echo 1>&2 "$0: ${name}: Don't know what to use to build this."
		exit 1
	fi
	# The activating template unit is always a dependency.
	ifchange_follow "${template}"@"${suffix}"
	# Check for drop-ins in the service unit.
	if test -n "${service_unit}"
	then
		if test -d "${service_unit}".d
		then
			redo-ifdelete "${service_unit}".d
			for d in "${service_unit}.d/"*".conf.do"
			do
				test ! "${d}" = "${service_unit}"'.d/*.conf.do' || continue
				redo-ifchange "${d%.do}"
			done
			for s in "${service_unit}.d/"*".conf"
			do
				test ! -e "${s}.do" || continue
				ifchange_follow "${s}"
			done
		else
			redo-ifcreate "${service_unit}".d
		fi
		ifchange_follow "${service_unit}"
	fi
	# Check for drop-ins in the activating template unit.
	if test -d "${template}"@"${suffix}".d
	then
		redo-ifdelete "${template}"@"${suffix}".d
		for d in "${template}"@"${suffix}.d/"*".conf.do"
		do
			test ! "${d}" = "${template}"@"${suffix}"'.d/*.conf.do' || continue
			redo-ifchange "${d%.do}"
		done
		for s in "${template}"@"${suffix}.d/"*".conf"
		do
			test ! -e "${s}.do" || continue
			ifchange_follow "${s}"
		done
	else
		redo-ifcreate "${template}"@"${suffix}".d
	fi
	# Check for drop-ins in the activating instance unit.
	if test -d "${unit}".d
	then
		redo-ifdelete "${unit}".d
		for d in "${unit}.d/"*".conf.do"
		do
			test ! "${d}" = "${unit}"'.d/*.conf.do' || continue
			redo-ifchange "${d%.do}"
		done
		for s in "${unit}.d/"*".conf"
		do
			test ! -e "${s}.do" || continue
			ifchange_follow "${s}"
		done
	else
		redo-ifcreate "${unit}".d
	fi
	;;
(*)
	if test -e "${name}".socket
	then
		unit="${name}".socket
		if test -e "${name}"@.service
		then
			service_unit="${name}"@.service
		else
			redo-ifcreate "${name}"@.service
			service_unit="${name}".service
		fi
	elif test -e "${name}".timer
	then
		redo-ifcreate "${name}".socket
		unit="${name}".timer
		service_unit="${name}".service
	elif test -e "${name}".service
	then
		redo-ifcreate "${name}".socket
		redo-ifcreate "${name}".timer
		unit="${name}".service
	else
		echo 1>&2 "$0: ${name}: Don't know what to use to build this."
		exit 1
	fi
	# The activating unit is always a dependency.
	ifchange_follow "${unit}"
	# Check for drop-ins in the service unit.
	if test -n "${service_unit}"
	then
		if test -d "${service_unit}".d
		then
			redo-ifdelete "${service_unit}".d
			for d in "${service_unit}.d/"*".conf.do"
			do
				test ! "${d}" = "${service_unit}"'.d/*.conf.do' || continue
				redo-ifchange "${d%.do}"
			done
			for s in "${service_unit}.d/"*".conf"
			do
				test ! -e "${s}.do" || continue
				ifchange_follow "${s}"
			done
		else
			redo-ifcreate "${service_unit}".d
		fi
		ifchange_follow "${service_unit}"
	fi
	# Check for drop-ins in the activating unit.
	if test -d "${unit}".d
	then
		redo-ifdelete "${unit}".d
		for d in "${unit}.d/"*".conf.do"
		do
			test ! "${d}" = "${unit}"'.d/*.conf.do' || continue
			redo-ifchange "${d%.do}"
		done
		for s in "${unit}.d/"*".conf"
		do
			test ! -e "${s}.do" || continue
			ifchange_follow "${s}"
		done
	else
		redo-ifcreate "${unit}".d
	fi
	;;
esac

# ###
# Decide the parameters for the conversion tool, and the location of the service and (if any) its logging service.
# ###

case "${base}" in
(cyclog@*)
	escape="--escape-format account --escape-instance"
	;;
(*-log)
	escape="--escape-format account --escape-prefix"
	;;
(*)
	escape=
	;;
esac

case "`uname`" in
(Linux)		etc_services="../package/common-etc-services ../package/linux-etc-services" ;;
(FreeBSD)	etc_services="../package/common-etc-services ../package/bsd-etc-services ../package/freebsd-etc-services" ;;
(NetBSD)	etc_services="../package/common-etc-services ../package/bsd-etc-services ../package/netbsd-etc-services" ;;
(*BSD)		etc_services="../package/common-etc-services ../package/bsd-etc-services" ;;
(*)		etc_services="../package/common-etc-services" ;;
esac
redo-ifchange -- ${etc_services}

case "${base}" in
(mount@*|fsck@*)
	# This is just the template-generated services that cannot be listed in package/*-etc-services .
	# Normally such services are handled by the default case, below.
	etc="--etc-bundle --supervise-in-run"
	;;
(*)
	# In the default case sysinit services log to a fan-in sysinit-log service and non-sysinit services log to a dedicated log service.
	if grep -q -- "^${base}\$" ${etc_services}
	then
		etc="--etc-bundle --supervise-in-run"
	else
		etc=
	fi
	;;
esac

case "${base}" in
(dbus)
	# This is an umbrella service that has no log service.
	log=
	after=
	;;
(cyclog@*|*-log)
	# Logging services themselves have no logs.
	log=
	after=
	;;
(emergency-login@console)
	# This is an emergency service that has no log service.
	log=
	after=
	;;
(mount@*|fsck@*)
	# These sysinit services log to a common fan-in log service.
	# This is just the template-generated services that cannot be listed in package/*-etc-services .
	# Normally such services are handled by the default case, below.
	log="../sysinit-log"
	after=	# sysinit-log only comes up after filesystems have been mounted.
	;;
(devd|devpubd)
	# These sysinit services log to their own dedicated log services.
	log="../${base}-log"
	after="log"
	;;
(systemd-udev|systemd-udev-trigger-add@*)
	# These sysinit services log to a common fan-in log service.
	log="../systemd-udev-log"
	after="log"
	;;
(udev|udev-trigger-add@*|udev-finish)
	# These sysinit services log to a common fan-in log service.
	log="../udev-log"
	after="log"
	;;
(busybox-mdev|busybox-mdev-rescan)
	# These sysinit services log to a common fan-in log service.
	log="../busybox-mdev-log"
	after="log"
	;;
(suckless-mdev|suckless-mdev-rescan)
	# These sysinit services log to a common fan-in log service.
	log="../suckless-mdev-log"
	after="log"
	;;
(mdevd|mdevd-rescan)
	# These sysinit services log to a common fan-in log service.
	log="../mdevd-log"
	after="log"
	;;
(ip6addrctl@*)
	# These non-sysinit services log to a common fan-in log service.
	log="../ip6addrctl-log"
	after="log"
	;;
(trueos-update-finish|trueos-install-finish|freebsd-update-finish|freebsd-install-finish)
	# These sysinit services log to a common fan-in log service.
	log="../system-installer-log"
	after="log"
	;;
(*)
	# In the default case sysinit services log to a common fan-in service and non-sysinit services log to a dedicated log service.
	if grep -q -- "^${base}\$" ${etc_services}
	then
		log="../sysinit-log"
		after=	# sysinit-log only comes up after filesystems have been mounted.
	else
		log="../cyclog@${base}"
		after="log"
	fi
	;;
esac

# ###
# Compile the source into a bundle.
# ###

install -d -m 0755 services.new

rm -r -f services.new/"${base}"

redo-ifchange command/build-helper

command/build-helper convert-systemd-units --no-systemd-quirks --no-generation-comment ${escape} ${etc} --bundle-root services.new/ "${unit}"

test -n "${log}" && ln -s -f "${log}" services.new/"${base}"/log
test -n "${after}" && ln -s -f "../${after}" services.new/"${base}"/after/

# ###
# Add in the environment directory infrastructure, if required.
# ###

if grep -q "envdir env" services.new/"${base}"/service/start services.new/"${base}"/service/run services.new/"${base}"/service/stop
then
	install -d -m 0755 services.new/"${base}"/service/env
fi

# ###
# Provide the "main/" courtesy link in logging services.
# ###

case "${base}" in
(cyclog@*)
	ln -f -s -- /var/log/sv/"${base#cyclog@}" services.new/"${base}"/main
	;;
(*-log)
	ln -f -s -- /var/log/sv/"${base%-log}" services.new/"${base}"/main
	;;
esac

# ###
# Populate the bundles with extra relationships, files, and directories peculiar to the services.
# ###

case "${base}" in
(kmod@vboxvideo)
	ln -f -s -- /etc/service-bundles/targets/virtualbox-guest services.new/"${base}"/wanted-by/
	ln -f -s -- ../../kmod@uvesafb services.new/"${base}"/after/
	;;
(kmod@vboxadd|kmod@vboxsf|kmod@vboxguest)
	ln -f -s -- /etc/service-bundles/targets/virtualbox-guest services.new/"${base}"/wanted-by/
	;;
(kmod@vboxdrv|kmod@vboxnetadp|kmod@vboxnetflt|kmod@vboxpci)
	ln -f -s -- /etc/service-bundles/targets/virtualbox-host services.new/"${base}"/wanted-by/
	;;
(kmod@uvesafb)
	ln -f -s -- /etc/service-bundles/targets/frame-buffer services.new/"${base}"/wanted-by/
	;;
(cyclog@ttylogin@*|cyclog@console-kvt-realizer@*|cyclog@ttycallout@*)
	rm -f -- services.new/"${base}"/wanted-by/workstation
	ln -s -f -- ../../"${base#cyclog@}" services.new/"${base}"/wanted-by/
	;;
(cyclog@VBoxService|cyclog@VBoxBalloonCtrl|cyclog@kmod@vboxadd|cyclog@kmod@vboxsf|cyclog@kmod@vboxguest|cyclog@kmod@vboxvideo)
	rm -f -- services.new/"${base}"/wanted-by/workstation
	ln -f -s -- /etc/service-bundles/targets/virtualbox-guest services.new/"${base}"/wanted-by/
	;;
(cyclog@kmod@vboxdrv|cyclog@kmod@vboxnetadp|cyclog@kmod@vboxnetflt|cyclog@kmod@vboxpci)
	rm -f -- services.new/"${base}"/wanted-by/workstation
	ln -f -s -- /etc/service-bundles/targets/virtualbox-host services.new/"${base}"/wanted-by/
	;;
(cyclog@vmtoolsd|cyclog@vgathd)
	rm -f -- services.new/"${base}"/wanted-by/workstation
	ln -f -s -- /etc/service-bundles/targets/vmware-guest services.new/"${base}"/wanted-by/
	;;
(dbus-daemon)
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

# ###
# Finalize
# ###

rm -r -f -- "$3"
mv -- services.new/"${base}" "$3"
