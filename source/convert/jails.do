#!/bin/sh -e
#
# Convert the warden external configuration formats.
# This is invoked by all.do .
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

ls -d "${JDIR}/".*.meta 2>/dev/null |
while read JMETADIR
do
	JAILNAME="`basename \"${JMETADIR}\" .meta`"
	JAILNAME="${JAILNAME#.}"
	service="jail@${JAILNAME}"
	system-control convert-systemd-services --bundle-root /var/local/sv/ --overwrite "${service}.service"
	if test -e "${JMETADIR}/autostart"
	then
		system-control preset -- "${service}"
	else
		system-control disable -- "${service}"
	fi
	if system-control is-enabled "${service}"
	then
		echo >> "$3" on "${service}"
	else
		echo >> "$3" off "${service}"
	fi
done
