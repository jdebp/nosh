#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Convert the Desktop Bus system-wide services.
# This is invoked by all.do .
#

case "`uname`" in
(Linux)
	redo-ifchange rc.conf

	# This gets us *only* the configuration variables, safely.
	read_rc() { clearenv read-conf rc.conf printenv "$1" ; }

	case "`read_rc os_version`" in
	(arch:*) 	g=dbus ;;
	(void:*) 	g=dbus ;;
	(debian:*) 	g=messagebus ;;
	(gentoo:*) 	g=messagebus ;;
	(centos:*)	g=dbus ;;
	(rhel:*) 	g=dbus ;;
	(*)      	echo 1>&2 "$0: Do not know the message bus group for your system." ; exec false ;;
	esac
	;;
(*BSD)	g=messagebus ;;
(*)	echo 1>&2 "$0: Do not know the message bus group for your system." ; exec false ;;
esac

test -h /var/local/service-bundles/targets || { install -d -m 0755 /var/local/service-bundles && ln -s /etc/service-bundles/targets /var/local/service-bundles/ ; }
sr="/var/service-bundles/services/"
lr="/var/local/service-bundles/services/"
e="--no-systemd-quirks --escape-instance --local-bundle"

allow_dbus_bus_activation() {
	rm -f -- "$1/service/down"
	install -d -m 0750 -- "$1/supervise"
	test -p "$1/supervise/control" || mkfifo -m 0600 -- "$1/supervise/control"
	setfacl -m "g:${g}:rx" "$1/supervise/" || setfacl -m "group:${g}:rx::allow" "$1/supervise/" || :
	setfacl -m "g:${g}:w" "$1/supervise/control" || setfacl -m "group:${g}:wp::allow" "$1/supervise/control" || :
}

link_service_to_logger_service() {
	rm -f -- "${lr}/$1/log"
	ln -s -- "../$2" "${lr}/$1/log"
	rm -f -- "${lr}/$1/wants/log" "${lr}/$1/after/log"
	ln -s -- "../log" "${lr}/$1/wants/log"
	ln -s -- "../log" "${lr}/$1/after/log"
}

find_dbus_services() {
	for p in /usr/local/share /usr/share /share
	do
		printf 'Desktop Bus services from %s/%s\n' "${p}" "$1" >> "$3"
		if ! test -e "$p"
		then
			redo-ifcreate -- "$p"
			continue
		fi
		redo-ifchange -- "$p"
		test -d "$p" || continue
		d="$p/$1"
		if ! test -e "$d"
		then
			redo-ifcreate -- "$d"
			continue
		fi
		redo-ifchange -- "$d"
		test -d "$d" || continue

		find "$d/" -maxdepth 1 -name '*.service' -type f -print |
		while read -r i
		do
			printf 'Found %s\n' "${i}" >> "$3"
			basename "${i}" .service
		done
	done
}

allow_dbus_bus_activation "${sr}/cyclog@dbus"

# Make .service source files for all system-wide Desktop Bus service definitions.
# Then make service bundles from those.
find_dbus_services 'dbus-1/system-services' '' "$3" |
awk '!x[$0]++' |
while read -r i
do
	if test -e "${sr}/$i/"
	then
		redo-ifchange -- "${sr}/$i/"
		continue
	fi
	redo-ifcreate -- "${sr}/$i"
	test \! -d "${sr}/$i/" || continue
	case "${i}" in
	(org.freedesktop.systemd[0-9]*)	continue ;;	# deny-listed
	(org.freedesktop.network[0-9]*)	continue ;;	# deny-listed
	(org.freedesktop.resolve[0-9]*)	continue ;;	# deny-listed
	(org.freedesktop.import[0-9]*)	continue ;;	# deny-listed
	(org.freedesktop.machine[0-9]*)	continue ;;	# deny-listed
	(org.freedesktop.portable[0-9]*)	continue ;;	# deny-listed
	(org.freedesktop.timesync[0-9]*)	continue ;;	# deny-listed
	(org.freedesktop.login[0-9]*)	continue ;;	# deny-listed
	esac
	redo-ifchange -- "dbus/${i}.service"
	printf 'Made dbus/%s.service\n' "$i" >> "$3"
	if test -L "${lr}/$i"
	then
		redo-ifchange -- "${lr}/$i"
		continue
	fi
	system-control convert-systemd-units $e --bundle-root "${lr}/" "./dbus/$i.service"
	install -d -m 0755 -- "${lr}/$i/service/env"
	link_service_to_logger_service "$i" "../../service-bundles/services/cyclog@dbus"
	system-control preset "$i.service"
	allow_dbus_bus_activation "${lr}/$i"
	printf 'Configured %s/%s\n' "${lr}" "$i" >> "$3"
done

# Make .service source files for all per-user Desktop Bus service definitions.
# Per-user configuration import deals with making the actual service bundles.
find_dbus_services 'dbus-1/services' '' "$3" |
awk '!x[$0]++' |
while read -r i
do
	redo-ifchange -- "per-user/dbus/${i}.service"
	printf 'Made per-user/dbus/%s.service\n' "$i" >> "$3"
done
