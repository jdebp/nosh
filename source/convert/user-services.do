#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
#
# Convert the /etc/passwd external configuration format.
# This is invoked by all.do .
#

redo-ifchange /etc/passwd "user-dbus@.socket" "user-dbus.service" "user-dbus-log@.service" "user-services@.service" "user-runtime@.service" "user@.target"

lr="/var/local/sv/"
sr="/etc/service-bundles/services/"
tr="/etc/service-bundles/targets/"
e="--no-systemd-quirks --escape-instance --bundle-root"

getent passwd |
awk -F : '{ if (!match($7,"/nologin$") && !match($7,"/false$")) print $1; }' |
while read -r i
do
	# On systems that dont set nologin/false as the shell, there is nothing distinguishing "system" accounts from accounts that temporarily have their password disabled.
	# As the manual page says, an account with a temporarily disabled password could still be used via SSH, and obviously one might want user Desktop Bus service for such a login.
	case "$i" in
	bin|daemon|games|irc|mail|news|nobody|root|sync|sys|uucp|www-data|libuuid|backup|lp|saned) continue ;;
	alias|qmail[dlpqrs]) continue ;;
	toor) continue ;;
	esac

	if test -d "$sr/user@$i" 
	then
		system-control disable "$sr/user@$i" || :		## Disable old user service
	fi
	if test -d "$sr/user-dbus@$i" 
	then
		system-control disable "$sr/user-dbus@$i" || :		## Disable old user service
	fi
	if test -d "$sr/user-dbus-log@$i" 
	then
		system-control disable "$sr/user-dbus-log@$i" || :	## Disable old user service
	fi
	if test -d "$sr/user-services@$i" 
	then
		system-control disable "$sr/user-services@$i" || :	## Disable old user service
	fi
	if test -d "$sr/user-runtime@$i" 
	then
		system-control disable "$sr/user-runtime@$i" || :	## Disable old user service
	fi

	system-control convert-systemd-units --etc-bundle $e "$tr/" "./user@$i.target"
	echo "user@$i" >> "$3"

	system-control convert-systemd-units $e "$lr/" "./user-dbus-log@$i.service"
	system-control enable "$lr/user-dbus-log@$i"
	echo "user-dbus-log@$i" >> "$3"

	mkdir -p -m 1755 "/var/log/user"
	mkdir -p -m 0750 "/var/log/user/$i"
	mkdir -p -m 0750 "/var/log/user/$i/dbus"
	setfacl -m "u:$i:rwx" "/var/log/user/$i" || setfacl -m "user:$i:rwxpD::allow" "/var/log/user/$i" || :
	setfacl -m "u:$i:rwx" "/var/log/user/$i/dbus" || setfacl -m "user:$i:rwxpD::allow" "/var/log/user/$i/dbus" || :

	system-control convert-systemd-units $e "$lr/" "./user-dbus@$i.socket"
	system-control enable "$lr/user-dbus@$i"
	echo "user-dbus@$i" >> "$3"
	rm -f -- "$lr/user-dbus@$i/log"
	ln -s -- "../../user-dbus-log@$i" "$lr/user-dbus@$i/log"

	system-control convert-systemd-units $e "$lr/" "./user-services@$i.service"
	system-control enable "$lr/user-services@$i"
	echo "user-services@$i" >> "$3"
#	rm -f -- "$lr/user-services@$i/log"
#	ln -s -- "../../user-log@$i" "$lr/user-services@$i/log"

	system-control convert-systemd-units $e "$lr/" "./user-runtime@$i.service"
	system-control enable "$lr/user-runtime@$i"
	echo "user-runtime@$i" >> "$3"
#	rm -f -- "$lr/user-runtime@$i/log"
#	ln -s -- "../../user-log@$i" "$lr/user-runtime@$i/log"
done
