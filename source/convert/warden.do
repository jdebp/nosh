#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Convert the PC-BSD/TrueOS Warden external configuration formats.
# This is invoked by all.do .
#

conf=/usr/local/etc/warden.conf
if ! test -e "${conf}"
then
	redo-ifcreate "${conf}"
	exit $?
fi

redo-ifchange "${conf}" general-services

eval `sed -n -e '/^[[:space:]]*#.*$/d;s/: /=/p' "${conf}"`

if ! test -d "${JDIR}" 
then
	redo-ifcreate "${JDIR}"
	exit $?
fi

redo-ifchange "warden-jail@.service" "warden-jailed@.service"

r="/var/local/sv"
e="--no-systemd-quirks --escape-instance --bundle-root"

convert_setting() {
	test -r "$1" || return
	system-control set-service-env -- "${jail_service}" "$2" "`sed -e 's: :,:g' \"$1\"`"
}

for target in warden
do
	system-control preset -- "${target}".target
done

for target in warden
do
	if system-control is-enabled "${target}.target"
	then
		echo >> "$3" on "${target}"
	else
		echo >> "$3" off "${target}"
	fi
#	system-control print-service-env "${target}.target" >> "$3"
done

ls -d -1 "${JDIR}/".*.meta 2>/dev/null |
while read -r JMETADIR
do
	redo-ifchange "${JMETADIR}"
	JAILNAME="`basename \"${JMETADIR}\" .meta`"
	JAILNAME="${JAILNAME#.}"
	JAILDIR="${JDIR}/${JAILNAME}"

	jail_service="warden-jail@${JAILNAME}"
	jailed_service="warden-jailed@${JAILNAME}"

	system-control convert-systemd-units $e "$r/" ./"${jail_service}.service"
	system-control convert-systemd-units $e "$r/" ./"${jailed_service}.service"

	mkdir -p -m 0755 "$r/${jail_service}/service/env"
	mkdir -p -m 0755 "$r/${jailed_service}/service/env"

	if test -e "${JMETADIR}/autostart"
	then
		system-control preset -- "${jail_service}"
		system-control preset -- "${jailed_service}"
	else
		redo-ifcreate "${JMETADIR}/autostart"
		system-control disable -- "${jail_service}"
		system-control disable -- "${jailed_service}"
	fi

	if test -r "${JMETADIR}/iface"
	then
		system-control set-service-env -- "${jail_service}" "interface" "`cat \"${JMETADIR}/iface\"`"
	else
		system-control set-service-env -- "${jail_service}" "interface" "${NIC}"
	fi
	if test -r "${JAILDIR}/"
	then
		system-control set-service-env -- "${jail_service}" "root" "${JAILDIR}/"
	fi

	# This is fairly basic and needs improvement.
	convert_setting "${JMETADIR}/jail-flags" flags
	convert_setting "${JMETADIR}/host" hostname
	convert_setting "${JMETADIR}/ipv4" ipv4
	convert_setting "${JMETADIR}/alias-ipv4" "alias_ipv4"
	convert_setting "${JMETADIR}/ipv6" ipv6
	convert_setting "${JMETADIR}/alias-ipv6" "alias_ipv6"
	convert_setting "${JMETADIR}/devfs-ruleset" "devfs_ruleset"

	if test -r "${JMETADIR}/jail-start"
	then
		system-control set-service-env -- "${jailed_service}" "start" "`cat \"${JMETADIR}/jail-start\"`"
	elif test -e "${JAILDIR}/sbin/system-manager"
	then
		system-control set-service-env -- "${jailed_service}" "start" "/sbin/system-manager"
	elif test -e "${JAILDIR}/sbin/launchd"
	then
		system-control set-service-env -- "${jailed_service}" "start" "/sbin/launchd"
	elif test -e "${JAILDIR}/lib/systemd/systemd"
	then
		system-control set-service-env -- "${jailed_service}" "start" "/lib/systemd/system-manager"
	elif test -e "${JAILDIR}/etc/init.d/rc"
	then
		if test -e "${JMETADIR}/jail-linux" 
		then
			system-control set-service-env -- "${jailed_service}" "start" "/bin/sh /etc/init.d/rc 3"
		else
			system-control set-service-env -- "${jailed_service}" "start" "/bin/sh /etc/init.d/rc"
		fi
	elif test -e "${JAILDIR}/etc/rc"
	then
		if test -e "${JMETADIR}/jail-linux" 
		then
			system-control set-service-env -- "${jailed_service}" "start" "/bin/sh /etc/rc 3"
		else
			system-control set-service-env -- "${jailed_service}" "start" "/bin/sh /etc/rc"
		fi
	fi
	if test -r "${JMETADIR}/jail-stop"
	then
		system-control set-service-env -- "${jailed_service}" "stop" "`cat \"${JMETADIR}/jail-stop\"`"
	elif test -e "${JAILDIR}/bin/system-control"
	then
		system-control set-service-env -- "${jailed_service}" "stop" "/bin/system-control halt"
	elif test -e "${JAILDIR}/bin/launchctld"
	then
		system-control set-service-env -- "${jailed_service}" "stop" "/bin/launchctl halt"
	elif test -e "${JAILDIR}/etc/init.d/rc"
	then
		if test -e "${JMETADIR}/jail-linux" 
		then
			system-control set-service-env -- "${jailed_service}" "start" "/bin/sh /etc/init.d/rc 3"
		else
			system-control set-service-env -- "${jailed_service}" "start" "/bin/sh /etc/init.d/rc"
		fi
	elif test -e "${JAILDIR}/etc/rc"
	then
		if test -e "${JMETADIR}/jail-linux" 
		then
			system-control set-service-env -- "${jailed_service}" "start" "/bin/sh /etc/rc 3"
		else
			system-control set-service-env -- "${jailed_service}" "start" "/bin/sh /etc/rc"
		fi
	elif test -e "${JAILDIR}/etc/rc.shutdown"
	then
		system-control set-service-env -- "${jailed_service}" "stop" "/bin/sh /etc/rc.shutdown"
	fi

	if system-control is-enabled "${jail_service}"
	then
		echo >> "$3" on "${jail_service}"
	else
		echo >> "$3" off "${jail_service}"
	fi
	system-control print-service-env "${jail_service}" >> "$3"
	if system-control is-enabled "${jailed_service}"
	then
		echo >> "$3" on "${jailed_service}"
	else
		echo >> "$3" off "${jailed_service}"
	fi
	system-control print-service-env "${jailed_service}" >> "$3"
done
