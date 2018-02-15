#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Convert the /etc/passwd external configuration format.
# This is invoked by all.do .
#

redo-ifchange /etc/passwd "user-dbus-daemon@.socket" "user-dbus-daemon.service" "user-dbus-broker@.socket" "user-dbus-broker.service" "user-dbus-log@.service" "user-services.service" "user-services@.socket" "user-runtime@.service" "run-user-directory@.service" "user@.target" "per-user/exit.target" "per-user/per-user.conf"

lr="/var/local/sv/"
sr="/etc/service-bundles/services/"
tr="/etc/service-bundles/targets/"
eu="--etc-bundle --user"
e="--no-systemd-quirks --escape-instance --bundle-root"

redo-ifchange /etc/passwd
getent passwd |
awk -F : '{ if (!match($7,"/nologin$")) print $1; }' |
while read -r i
do
	# On systems that don't set nologin/false as the shell, there is nothing distinguishing "system" accounts from accounts that temporarily have their password disabled.
	# As the manual page says, an account with a temporarily disabled password could still be used via SSH, and obviously one might want user Desktop Bus service for such a login.
	case "$i" in
	bin|daemon|games|irc|mail|news|nobody|root|sync|sys|uucp|www-data|libuuid|backup|lp|saned|man|proxy|backup|list|gnats) continue ;;
	alias|qmail[dlpqrs]|publicfile) continue ;;
	avahi|colord|dnsmasq|geoclue|lightdm|messagebus|sddm|systemd-*|uuidd) continue ;;
	rtkit|*\\*) continue ;;
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

	test -n "$h" || continue
	test -d "$h/" || continue

	echo "per-user@$i: $h" >> "$3"

	install -d -m 1755 "/var/log/user"
	install -d -m 0750 -o "$i" "/var/log/user/$i"
	setfacl -m "u:$i:rwx" "/var/log/user/$i" || setfacl -m "user:$i:rwxpD::allow" "/var/log/user/$i" || :

	system-control convert-systemd-units --etc-bundle $e "$tr/" "./user@$i.target"
	echo "user@$i" >> "$3"

	# This is a system-level service that is disabled in favour of the user-level cyclog@dbus service.
	system-control convert-systemd-units $e "$lr/" "./user-dbus-log@$i.service"
	system-control disable "$lr/user-dbus-log@$i"
	echo "user-dbus-log@$i" >> "$3"
	rm -f -- "$lr/user-dbus-log@$i/main"
	ln -s -- "/var/log/user/$i/dbus" "$lr/user-dbus-log@$i/main"

	# This is a system-level service that is disabled in favour of the user-level dbus service.
	system-control convert-systemd-units $e "$lr/" "./user-dbus-daemon@$i.socket"
	system-control disable "$lr/user-dbus-daemon@$i"
	echo "user-dbus-daemon@$i" >> "$3"
	rm -f -- "$lr/user-dbus-daemon@$i/log"
	ln -s -- "../user-dbus-log@$i" "$lr/user-dbus-daemon@$i/log"
	install -m 0644 -- "per-user/per-user.conf" "$lr/user-dbus-daemon@$i/service/per-user.conf"
	redo-ifchange "user-dbus-broker.service" "user-dbus-broker@.socket"
	system-control convert-systemd-units $e "$lr/" "./user-dbus-broker@$i.socket"
	system-control disable "$lr/user-dbus-broker@$i"
	echo "user-dbus-broker@$i" >> "$3"
	rm -f -- "$lr/user-dbus-broker@$i/log"
	ln -s -- "../user-dbus-log@$i" "$lr/user-dbus-broker@$i/log"
	install -m 0644 -- "per-user/per-user.conf" "$lr/user-dbus-broker@$i/service/per-user.conf"

	system-control convert-systemd-units $e "$lr/" "./user-services@$i.socket"
	system-control enable "$lr/user-services@$i"
	echo "user-services@$i" >> "$3"
#	rm -f -- "$lr/user-services@$i/log"
#	ln -s -- "../user-log@$i" "$lr/user-services@$i/log"

	system-control convert-systemd-units $e "$lr/" "./user-runtime@$i.service"
	system-control enable "$lr/user-runtime@$i"
	echo "user-runtime@$i" >> "$3"
	rm -f -- "$lr/user-runtime@$i/after/mount@-run-user-$i"
	ln -s -- "/etc/service-bundles/services/mount@-run-user-$i" "$lr/user-runtime@$i/after/"
#	rm -f -- "$lr/user-runtime@$i/log"
#	ln -s -- "../user-log@$i" "$lr/user-runtime@$i/log"

	system-control convert-systemd-units $e "$lr/" "./run-user-directory@$i.service"
	system-control enable "$lr/run-user-directory@$i"
	echo "run-user-directory@$i" >> "$3"
	rm -f -- "$lr/run-user-directory@$i/after/mount@-run-user"
	rm -f -- "$lr/run-user-directory@$i/wants/mount@-run-user"
	ln -s -- "/etc/service-bundles/services/mount@-run-user" "$lr/run-user-directory@$i/after/"
	ln -s -- "/etc/service-bundles/services/mount@-run-user" "$lr/run-user-directory@$i/wants/"
	rm -f -- "$lr/run-user-directory@$i/before/mount@-run-user-$i"
	rm -f -- "$lr/run-user-directory@$i/wants/mount@-run-user-$i"
	ln -s -- "/etc/service-bundles/services/mount@-run-user-$i" "$lr/run-user-directory@$i/before/"
	ln -s -- "/etc/service-bundles/services/mount@-run-user-$i" "$lr/run-user-directory@$i/wants/"
#	rm -f -- "$lr/run-user-directory@$i/log"
#	ln -s -- "../user-log@$i" "$lr/run-user-directory@$i/log"

	ht="$h/.config/service-bundles/targets"
	install -d -o "$i" -m 0755 -- "$ht"
	hv="$h/.config/service-bundles/convert"
	install -d -o "$i" -m 0755 -- "$hv"

	# Yes, this service bundle is root-owned; we don't want users messing it up.
	system-control convert-systemd-units $eu $e "$ht/" "./per-user/exit.target"
	install -d -o "$i" -m 0755 "$ht/exit/supervise"
	echo "per-user@$i/exit" >> "$3"

	for s in all default home config-path user services
	do
		test -e "${hv}/${s}.do" ||
		setuidgid "$i" ln -f "per-user/${s}.do" "${hv}/" 2>/dev/null ||
		install -o "$i" -m 0755 "per-user/${s}.do" "${hv}/"
	done
	redo-ifchange "${hv}"
	echo "per-user@$i" >> "$3"
done

for p in /usr/local/share /usr/share /share
do
	if ! test -e "$p"
	then
		redo-ifcreate "$p"
		continue
	fi
	redo-ifchange "$p"
	test -d "$p" || continue
	d="$p/dbus-1/services"
	if ! test -e "$d"
	then
		redo-ifcreate "$d"
		continue
	fi
	redo-ifchange "$d"
	test -d "$d" || continue

	find "$d/" -maxdepth 1 -name '*.service' -type f -print |
	while read -r i
	do
		basename "${i}"
	done
done |
awk '!x[$0]++' |
while read -r i
do
	printf "per-user/dbus/%s\n" "$i"
	printf "per-user/dbus/%s\n" "$i" >> "$3"
done |
xargs -- redo-ifchange --
