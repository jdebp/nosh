#!/bin/sh -e
#
# Convert the /etc/passwd external configuration format.
# This is invoked by all.do .
#

redo-ifchange /etc/passwd user-dbus@.socket user-dbus.service

r="/etc/service-bundles/services/"
e="--etc-bundle --no-systemd-quirks --escape-instance --bundle-root"

getent passwd |
awk -F : '{ if (!match($7,"/nologin$") && !match($7,"/false$")) print $1; }' |
while read i
do
	# On systems that dont set nologin/false as the shell, there is nothing distinguishing "system" accounts from accounts that temporarily have their password disabled.
	# As the manual page says, an account with a temporarily disabled password could still be used via SSH, and obviously one might want user Desktop Bus service for such a login.
	case "$i" in
	bin|daemon|games|irc|mail|news|nobody|root|sync|sys|uucp|www-data|libuuid|backup|lp|saned) continue ;;
	alias|qmail[dlpqrs]) continue ;;
	toor) continue ;;
	esac

	system-control convert-systemd-units $e "$r/" "./user-dbus-log@$i.service"
	echo "user-dbus-log@$i" >> "$3"
	system-control convert-systemd-units $e "$r/" "./user-dbus@$i.socket"
	echo "user-dbus@$i" >> "$3"

	mkdir -p -m 1755 "/var/log/user"
	mkdir -p -m 0750 "/var/log/user/$i"
	mkdir -p -m 0750 "/var/log/user/$i/dbus"
	setfacl -m "u:$i:rwx" "/var/log/user/$i"
	setfacl -m "u:$i:rwx" "/var/log/user/$i/dbus"
	test -d "$r/user-dbus@$i/log/" || ln -f -s "../user-dbus-log@$i" "$r/user-dbus@$i/log"

	system-control convert-systemd-units $e "$r/" "./user-services@$i.service"
	echo "user-services@$i" >> "$3"
	system-control convert-systemd-units $e "$r/" "./user@$i.service"
	echo "user@$i" >> "$3"
done
