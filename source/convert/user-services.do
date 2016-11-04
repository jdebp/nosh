#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
#
# Convert the /etc/passwd external configuration format.
# This is invoked by all.do .
#

redo-ifchange /etc/passwd "user-dbus@.socket" "user-dbus.service" "user-dbus-log@.service" "user-services@.service" "user-runtime@.service" "user@.target" "exit.target" "gnome-terminal-server.service" "dirmngr.socket" "dirmngr.service" "gpg-agent.socket" "gpg-agent.service"

lr="/var/local/sv/"
sr="/etc/service-bundles/services/"
tr="/etc/service-bundles/targets/"
eu="--etc-bundle --user"
e="--no-systemd-quirks --escape-instance --bundle-root"

getent passwd |
awk -F : '{ if (!match($7,"/nologin$") && !match($7,"/false$")) print $1; }' |
while read -r i
do
	# On systems that don't set nologin/false as the shell, there is nothing distinguishing "system" accounts from accounts that temporarily have their password disabled.
	# As the manual page says, an account with a temporarily disabled password could still be used via SSH, and obviously one might want user Desktop Bus service for such a login.
	case "$i" in
	bin|daemon|games|irc|mail|news|nobody|root|sync|sys|uucp|www-data|libuuid|backup|lp|saned|man|proxy|backup|list|gnats) continue ;;
	alias|qmail[dlpqrs]) continue ;;
	toor) continue ;;
	esac

	h="`getent passwd \"$i\" | awk -F : '{print $6;}'`"

	for j in '' -dbus -dbus-log -services -runtime
	do
		if test -d "$sr/user$j@$i/" 
		then
			## Disable old user service
			system-control disable "$sr/user$j@$i" || :
			if ! system-control is-loaded "$sr/user$j@$i" 2>/dev/null
			then
				rm -r -- "$sr/user$j@$i/"
				redo-ifcreate "$sr/user$j@$i/"
			fi
		fi
	done

	system-control convert-systemd-units --etc-bundle $e "$tr/" "./user@$i.target"
	echo "user@$i" >> "$3"

	system-control convert-systemd-units $e "$lr/" "./user-dbus-log@$i.service"
	system-control enable "$lr/user-dbus-log@$i"
	echo "user-dbus-log@$i" >> "$3"
	rm -f -- "$lr/user-dbus-log@$i/main"
	ln -s -- "/var/log/user/$i/dbus" "$lr/user-dbus-log@$i/main"

	mkdir -p -m 1755 "/var/log/user"
	mkdir -p -m 0750 "/var/log/user/$i"
	mkdir -p -m 0750 "/var/log/user/$i/dbus"
	setfacl -m "u:$i:rwx" "/var/log/user/$i" || setfacl -m "user:$i:rwxpD::allow" "/var/log/user/$i" || :
	setfacl -m "u:$i:rwx" "/var/log/user/$i/dbus" || setfacl -m "user:$i:rwxpD::allow" "/var/log/user/$i/dbus" || :

	system-control convert-systemd-units $e "$lr/" "./user-dbus@$i.socket"
	system-control enable "$lr/user-dbus@$i"
	echo "user-dbus@$i" >> "$3"
	rm -f -- "$lr/user-dbus@$i/log"
	ln -s -- "../user-dbus-log@$i" "$lr/user-dbus@$i/log"

	system-control convert-systemd-units $e "$lr/" "./user-services@$i.service"
	system-control enable "$lr/user-services@$i"
	echo "user-services@$i" >> "$3"
#	rm -f -- "$lr/user-services@$i/log"
#	ln -s -- "../user-log@$i" "$lr/user-services@$i/log"

	system-control convert-systemd-units $e "$lr/" "./user-runtime@$i.service"
	system-control enable "$lr/user-runtime@$i"
	echo "user-runtime@$i" >> "$3"
#	rm -f -- "$lr/user-runtime@$i/log"
#	ln -s -- "../user-log@$i" "$lr/user-runtime@$i/log"

	if test -n "$h" && test -d "$h/"
	then
		ht="$h/.config/service-bundles/targets"
		install -d -o "$i" -m 0755 "$ht"

		system-control convert-systemd-units $eu $e "$ht/" "./exit.target"
		install -d -o "$i" -m 0755 "$ht/exit/supervise"
		for t in halt reboot poweroff
		do
			rm -f -- "$ht/$t"
			ln -s -- "exit" "$ht/$t"
		done

		hs="$h/.config/service-bundles/services"
		install -d -o "$i" -m 0755 "$hs"

		setuidgid "$i" system-control convert-systemd-units $eu $e "$hs/" "./gnome-terminal-server.service"
		rm -f -- "$hs/org.gnome.Terminal"
		setuidgid "$i" ln -s -- "gnome-terminal-server" "$hs/org.gnome.Terminal"
		setuidgid "$i" system-control convert-systemd-units $eu $e "$hs/" "./dirmngr.socket"
		setuidgid "$i" system-control convert-systemd-units $eu $e "$hs/" "./gpg-agent.socket"
	fi
done
