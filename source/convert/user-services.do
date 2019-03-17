#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Convert the /etc/passwd external configuration format.
# This is invoked by all.do .
# 2018-05-29: This line forces a rebuild because of the new setlogin functionality.
#

lr="/var/local/sv/"
sr="/etc/service-bundles/services/"
tr="/etc/service-bundles/targets/"
eu="--etc-bundle --user"
e="--no-systemd-quirks --escape-instance --bundle-root"

case "`uname`" in
FreeBSD)	
	if test -r /etc/spwd.db
	then
		redo-ifchange /etc/spwd.db
	else
		redo-ifchange /etc/pwd.db
	fi
	;;
Linux|*)	redo-ifchange /etc/passwd ;;
esac

redo-ifchange "user-dbus-daemon@.socket" "user-dbus-daemon.service" "user-dbus-broker@.socket" "user-dbus-broker.service" "user-dbus-log@.service" "user-services.service" "user-services@.socket" "user-runtime@.service" "run-user-directory@.service" "user@.target" "per-user/exit.target" "per-user/per-user.conf" "per-user/all.do" "per-user/default.do" "per-user/home.do" "per-user/user.do" "per-user/config-path.do" "per-user/services.do"

getent passwd |
awk -F : '{ if (!match($7,"/nologin$") && !match($7,"/false$")) print $1; }' |
while read -r i
do
	# On systems that don't set nologin/false as the shell, there is nothing distinguishing "system" accounts from accounts that temporarily have their password disabled.
	# As the manual page says, an account with a temporarily disabled password could still be used via SSH, and obviously one might want user Desktop Bus service for such a login.
	case "$i" in
	bin|daemon|nobody|root|toor|sync|sys|games|irc|mail|news|uucp|www-data|libuuid|backup|lp|saned|man|proxy|backup|list|gnats) continue ;;
	alias|qmail[dlpqrs]|publicfile) continue ;;
	avahi|colord|dnsmasq|geoclue|lightdm|messagebus|sddm|systemd-*|uuidd|cron) continue ;;
	rtkit|*\\*) continue ;;
	*-log) continue ;;
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

	# We don't want to be setting up things in certain directories, regardless of the account name.
	case "$h" in
	# Apparently some people have not received the wisdom of the 1990s, which moved the superuser's home to /root, and still make / a home directory for some users.
	/)
		printf "WARNING: Dangerous home directory %s for account %s\n" "$h" "$i" 1>&2
		printf "WARNING: Dangerous home directory %s for account %s\n" "$h" "$i" >> "$3"
		continue
		;;
	# Most of these will be caught by the preceding well-known account names test; but better safe than sorry.
	# Allow subdirectories of /run, but not /run itself.
	/run|/dev|/dev/*|/proc|/proc/*|/sys|/sys/*|/bin|/sbin|/usr/bin|/usr/sbin|/usr/local/bin|/usr/local/sbin|/rescue|/rescue/*|/boot|/boot/*|/root|/root/*)
		printf "WARNING: Dangerous home directory %s for account %s\n" "$h" "$i" 1>&2
		printf "WARNING: Dangerous home directory %s for account %s\n" "$h" "$i" >> "$3"
		continue
		;;
	esac

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
#	setfacl -m "u:$i:rwx" "/var/log/user/$i/dbus" || setfacl -m "user:$i:rwxpD::allow" "/var/log/user/$i/dbus" || :

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
	setuidgid "$i" install -d -o "$i" -m 0755 -- "$ht"

	# Yes, this service bundle is root-owned; we don't want users messing it up.
	system-control convert-systemd-units $eu $e "$ht/" "./per-user/exit.target"
	install -d -o "$i" -m 0755 "$ht/exit/supervise"
	echo "per-user@$i/exit" >> "$3"

	setuidgid "$i" install -d -o "$i" -m 0755 -- "$h/.config/system-control"

	hv="$h/.config/system-control/convert"
	if test ! -d "${hv}" && test -d "$h/.config/service-bundles/convert"
	then
		mv -- "$h/.config/service-bundles/convert" "${hv}"
		ln -s -- "../system-control/convert" "$h/.config/service-bundles/"
	fi
	setuidgid "$i" install -d -o "$i" -m 0755 -- "$hv"
	for s in all default home config-path user services
	do
		test -e "${hv}/${s}.do" ||
		setuidgid "$i" ln -f "per-user/${s}.do" "${hv}/" 2>/dev/null ||
		setuidgid "$i" install -o "$i" -m 0755 "per-user/${s}.do" "${hv}/"
	done
	echo "per-user@$i" >> "$3"
done
