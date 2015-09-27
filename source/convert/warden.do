#!/bin/sh -e
#
# Convert the PC-BSD Warden external configuration formats.
# This is invoked by general-services.do .
#

conf=/usr/local/etc/warden.conf
if ! test -e "${conf}"
then
	redo-ifcreate "${conf}"
	exit $?
fi

redo-ifchange "${conf}"
eval `sed -n -e '/^[[:space:]]*#.*$/d;s/: /=/p' "${conf}"`

if ! test -d "${JDIR}" 
then
	redo-ifcreate "${JDIR}"
	exit $?
fi

redo-ifchange "warden-jail@.service" "warden-jailed@.service"

ls -d -1 "${JDIR}/".*.meta 2>/dev/null |
while read JMETADIR
do
	redo-ifchange "${JMETADIR}"
	JAILNAME="`basename \"${JMETADIR}\" .meta`"
	JAILNAME="${JAILNAME#.}"

	jail_service="warden-jail@${JAILNAME}"
	jailed_service="warden-jailed@${JAILNAME}"

	system-control convert-systemd-services --bundle-root /var/local/sv/ "${jail_service}.service"
	system-control convert-systemd-services --bundle-root /var/local/sv/ "${jailed_service}.service"

	if test -e "${JMETADIR}/autostart"
	then
		system-control preset -- "${jail_service}"
		system-control preset -- "${jailed_service}"
	else
		redo-ifcreate "${JMETADIR}/autostart"
		system-control disable -- "${jail_service}"
		system-control disable -- "${jailed_service}"
	fi

	if system-control is-enabled "${jail_service}"
	then
		echo >> "$3" on "${jail_service}"
	else
		echo >> "$3" off "${jail_service}"
	fi
	if system-control is-enabled "${jailed_service}"
	then
		echo >> "$3" on "${jailed_service}"
	else
		echo >> "$3" off "${jailed_service}"
	fi
done
