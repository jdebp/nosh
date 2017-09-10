#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
#
# Convert the /etc/passwd external configuration format.
# This is invoked by all.do .
#

redo-ifchange /etc/passwd "user-dbus-daemon@.socket" "user-dbus-daemon.service" "user-dbus-broker@.socket" "user-dbus-broker.service" "user-dbus-log@.service" "user-services@.service" "user-runtime@.service" "run-user-directory@.service" "user@.target" "exit.target" "per-user.conf" "mpd.conf"

lr="/var/local/sv/"
sr="/etc/service-bundles/services/"
tr="/etc/service-bundles/targets/"
eu="--etc-bundle --user"
e="--no-systemd-quirks --escape-instance --bundle-root"

user_simple_alias() {
	local t="$1"
	shift
	for s
	do
		rm -f -- "$hs/$s"
		setuidgid "$i" ln -s -- "$t" "$hs/$s"
	done
}

user_dbus_definition() {
	for s
	do
		if ! test -e "$hd/dbus-1/services/$s.service"
		then
			printf > "$hd/dbus-1/services/$s.service" '# Avoid Desktop Bus activation.\n[D-BUS Service]\nName=%s\nUser=%s\nExec=/bin/exec system-control start --user %s' "$s" "$i" "$s"
			chown "$i" "$hd/dbus-1/services/$s.service"
		fi
	done
}

user_dbus_alias() {
	local t="$1"
	shift
	for s
	do
		rm -f -- "$hs/$s"
		setuidgid "$i" ln -s -- "$t" "$hs/$s"
	done
	user_dbus_definition "$@"
}

user_display() {
	if ! test -e "$hs/$1/service/env/DISPLAY"
	then
		setuidgid "$i" system-control set-service-env "$hs/$1" DISPLAY ":0"
	fi
}

user_dbus() {
	if ! test -e "$hs/$1/service/env/DBUS_SESSION_BUS_ADDRESS"
	then
#		setuidgid "$i" system-control set-service-env "$hs/$1" DBUS_SESSION_BUS_ADDRESS "unix:runtime=yes"
		setuidgid "$i" system-control set-service-env "$hs/$1" DBUS_SESSION_BUS_ADDRESS "unix:path=/run/user/$i/bus"
	fi
}

user_link_to_log() {
	setuidgid "$i" rm -f -- "$hs/$1/log"
	setuidgid "$i" ln -s -- "../$2" "$hs/$1/log"
	setuidgid "$i" rm -f -- "$hs/$1/wants/log" "$hs/$1/after/log"
	setuidgid "$i" ln -s -- "../log" "$hs/$1/wants/log"
	setuidgid "$i" ln -s -- "../log" "$hs/$1/after/log"
}

user_socket_dbus_service() {
	redo-ifchange "$1.socket" "$1.service"
	setuidgid "$i" system-control convert-systemd-units $eu $e "$hs/" "./$1.socket"
	setuidgid "$i" install -d -m 0755 -- "$hs/$1/service/env"
	user_link_to_log "$1" "dbus-servers-log"
}

user_simple_dbus_service() {
	redo-ifchange "$1.service"
	setuidgid "$i" system-control convert-systemd-units $eu $e "$hs/" "./$1.service"
	setuidgid "$i" install -d -m 0755 -- "$hs/$1/service/env"
	user_link_to_log "$1" "dbus-servers-log"
	user_dbus "$1"
	user_dbus_alias "$@"
}

user_dbus_service_with_dedicated_logger() {
	redo-ifchange "$1.service" "cyclog@.service"
	setuidgid "$i" system-control convert-systemd-units $eu $e "$hs/" "./$1.service"
	setuidgid "$i" install -d -m 0755 -- "$hs/$1/service/env"
	setuidgid "$i" system-control convert-systemd-units $eu $e "$hs/" "./cyclog@$1.service"
	setuidgid "$i" install -d -m 0755 -- "$hs/cyclog@$1/service/env"
	setuidgid "$i" rm -f -- "$hs/cyclog@$1/main"
	setuidgid "$i" ln -s -- "/var/log/user/$i/$1" "$hs/cyclog@$1/main"
	user_link_to_log "$1" "cyclog@$1"
	user_dbus "$1"
	user_dbus_alias "$@"
}

user_simple_service() {
	redo-ifchange "$1.service"
	setuidgid "$i" system-control convert-systemd-units $eu $e "$hs/" "./$1.service"
	setuidgid "$i" install -d -m 0755 -- "$hs/$1/service/env"
	user_link_to_log "$1" "simple-servers-log"
	user_simple_alias "$@"
}

user_simple_socket() {
	redo-ifchange "$1.socket" "$1.service"
	setuidgid "$i" system-control convert-systemd-units $eu $e "$hs/" "./$1.socket"
	setuidgid "$i" install -d -m 0755 -- "$hs/$1/service/env"
	user_link_to_log "$1" "socket-servers-log"
	user_simple_alias "$@"
}

user_socket_with_dedicated_logger() {
	redo-ifchange "$1.socket" "$1.service" "cyclog@.service"
	setuidgid "$i" system-control convert-systemd-units $eu $e "$hs/" "./$1.socket"
	setuidgid "$i" install -d -m 0755 -- "$hs/$1/service/env"
	setuidgid "$i" system-control convert-systemd-units $eu $e "$hs/" "./cyclog@$1.service"
	setuidgid "$i" install -d -m 0755 -- "$hs/cyclog@$1/service/env"
	setuidgid "$i" rm -f -- "$hs/cyclog@$1/main"
	setuidgid "$i" ln -s -- "/var/log/user/$i/$1" "$hs/cyclog@$1/main"
	user_link_to_log "$1" "cyclog@$1"
}

user_service_with_dedicated_logger() {
	redo-ifchange "$1.service" "cyclog@.service"
	setuidgid "$i" system-control convert-systemd-units $eu $e "$hs/" "./$1.service"
	setuidgid "$i" install -d -m 0755 -- "$hs/$1/service/env"
	setuidgid "$i" system-control convert-systemd-units $eu $e "$hs/" "./cyclog@$1.service"
	setuidgid "$i" install -d -m 0755 -- "$hs/cyclog@$1/service/env"
	setuidgid "$i" rm -f -- "$hs/cyclog@$1/main"
	setuidgid "$i" ln -s -- "/var/log/user/$i/$1" "$hs/cyclog@$1/main"
	user_link_to_log "$1" "cyclog@$1"
}

user_fan_in_logger() {
	redo-ifchange "$1-log.service"
	setuidgid "$i" system-control convert-systemd-units $eu $e "$hs/" "./$1-log.service"
	setuidgid "$i" install -d -m 0755 -- "$hs/$1-log/service/env"
	setuidgid "$i" rm -f -- "$hs/$1-log/main"
	setuidgid "$i" ln -s -- "/var/log/user/$i/$1" "$hs/$1-log/main"
}

user_target() {
	redo-ifchange "$1.target"
	setuidgid "$i" system-control convert-systemd-units $eu $e "$ht/" "./$1.target"
}

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

	install -d -m 1755 "/var/log/user"
	install -d -m 0750 -o "$i" "/var/log/user/$i"
	install -d -m 0750 -o "$i" "/var/log/user/$i/mpd"
	install -d -m 0750 -o "$i" "/var/log/user/$i/pulseaudio"
	install -d -m 0750 -o "$i" "/var/log/user/$i/dbus"
	install -d -m 0750 -o "$i" "/var/log/user/$i/dbus-servers"
	install -d -m 0750 -o "$i" "/var/log/user/$i/socket-servers"
	install -d -m 0750 -o "$i" "/var/log/user/$i/simple-servers"
	setfacl -m "u:$i:rwx" "/var/log/user/$i" || setfacl -m "user:$i:rwxpD::allow" "/var/log/user/$i" || :
	setfacl -m "u:$i:rwx" "/var/log/user/$i/dbus" || setfacl -m "user:$i:rwxpD::allow" "/var/log/user/$i/dbus" || :

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
	install -m 0644 -- "per-user.conf" "$lr/user-dbus-daemon@$i/service/per-user.conf"
	system-control convert-systemd-units $e "$lr/" "./user-dbus-broker@$i.socket"
	system-control disable "$lr/user-dbus-broker@$i"
	echo "user-dbus-broker@$i" >> "$3"
	rm -f -- "$lr/user-dbus-broker@$i/log"
	ln -s -- "../user-dbus-log@$i" "$lr/user-dbus-broker@$i/log"
	install -m 0644 -- "per-user.conf" "$lr/user-dbus-broker@$i/service/per-user.conf"

	system-control convert-systemd-units $e "$lr/" "./user-services@$i.service"
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

	if test -n "$h" && test -d "$h/"
	then
		hc="$h/.config"
		install -d -o "$i" -m 0755 -- "$hc"
		install -d -o "$i" -m 0755 -- "$hc/service-bundles"
		ht="$hc/service-bundles/targets"
		install -d -o "$i" -m 0755 -- "$ht"
		hs="$hc/service-bundles/services"
		install -d -o "$i" -m 0755 -- "$hs"
		hd="$h/.local/share"
		install -d -o "$i" -m 0755 -- "$hd"
		hh="$h/.cache"
		install -d -o "$i" -m 0755 -- "$hh"

		if ! test -r "$hc/service-bundles/do-not-generate"
		then
			# Yes, this service bundle is root-owned; we don't want users messing it up.
			system-control convert-systemd-units $eu $e "$ht/" "./exit.target"
			install -d -o "$i" -m 0755 "$ht/exit/supervise"
			for t in halt reboot poweroff
			do
				rm -f -- "$ht/$t"
				ln -s -- "exit" "$ht/$t"
			done

			install -d -o "$i" -m 0755 -- "$hd/dbus-1"
			install -d -o "$i" -m 0755 -- "$hd/dbus-1/services"

			user_target "intrat"
			for t in sysinit basic normal multi-user emergency rescue workstation server
			do
				setuidgid "$i" rm -f -- "$ht/$t"
				setuidgid "$i" ln -s -- "intrat" "$ht/$t"
			done

			user_target "shutdown"
			user_target "sockets"

			user_fan_in_logger "dbus-servers"
			user_fan_in_logger "socket-servers"
			user_fan_in_logger "simple-servers"
			setuidgid "$i" userenv system-control --user enable "dbus-servers-log"
			setuidgid "$i" userenv system-control --user enable "socket-servers-log"
			setuidgid "$i" userenv system-control --user enable "simple-servers-log"

			user_socket_dbus_service "dbus"
			test -r "$hs/dbus/service/per-user.conf" || install -o "$i" -m 0644 -- "per-user.conf" "$hs/dbus/service/per-user.conf"

			user_simple_dbus_service "at-spi-dbus-bus" "org.a11y.Bus"
			user_simple_dbus_service "dconf-editor" "ca.desrt.dconf-editor"
			user_simple_dbus_service "dconf-service" "ca.desrt.dconf"
			user_simple_dbus_service "gconfd" "org.gnome.GConf"
			user_simple_dbus_service "gedit" "org.gnome.gedit"
			user_simple_dbus_service "gnome-keyring-daemon" "org.gnome.keyring" "org.freedesktop.secrets"
			user_simple_dbus_service "gnome-shell" "org.gnome.Shell"
			user_simple_dbus_service "gnome-settings-daemon" "org.gnome.SettingsDaemon.XSettings"
			user_simple_dbus_service "gnome-terminal-server" "org.gnome.Terminal"
			user_simple_dbus_service "gvfs-afc-volume-monitor" "org.gtk.Private.AfcVolumeMonitor"
			user_simple_dbus_service "gvfs-daemon" "org.gtk.vfs.Daemon"
			user_simple_dbus_service "gvfs-goa-volume-monitor" "org.gtk.Private.GoaVolumeMonitor"
			user_simple_dbus_service "gvfs-gphoto2-volume-monitor" "org.gtk.Private.GPhoto2VolumeMonitor" "org.gtk.vfs.GPhoto2VolumeMonitor"
			user_simple_dbus_service "gvfs-hal-volume-monitor" "org.gtk.Private.HalVolumeMonitor" "org.gtk.vfs.HalVolumeMonitor"
			user_simple_dbus_service "gvfs-metadata" "org.gtk.vfs.Metadata"
			user_simple_dbus_service "gvfs-mtp-volume-monitor" "org.gtk.Private.MTPVolumeMonitor"
			user_simple_dbus_service "gvfs-udisks2-volume-monitor" "org.gtk.Private.UDisks2VolumeMonitor"
			user_simple_dbus_service "ibus-daemon" "org.freedesktop.IBus"
			user_simple_dbus_service "knotify" "org.kde.knotify"
			user_simple_dbus_service "mate-notification-daemon" "org.freedesktop.mate.Notifications" "org.freedesktop.Notifications"
			user_simple_dbus_service "mate-screensaver" "org.mate.ScreenSaver"
			user_simple_dbus_service "obex" "org.bluez.obex" "dbus-org.bluez.obex"
			user_simple_dbus_service "org.gnome.Maps"
			user_simple_dbus_service "org.gnome.Weather.Application"
			user_simple_dbus_service "zeitgeist-daemon" "org.gnome.zeitgeist.Engine"
			user_simple_dbus_service "zeitgeist-datahub" "org.gnome.zeitgeist.datahub"
			user_simple_dbus_service "zeitgeist-fts" "org.gnome.zeitgeist.SimpleIndexer"
			user_simple_dbus_service "xdg-desktop-portal-gtk" "org.freedesktop.implo.portal.desktop.gtk"

			user_dbus_definition "org.gnome.Maps"
			user_dbus_definition "org.gnome.Weather.Application"
			setuidgid "$i" userenv system-control --user preset "zeitgeist-fts"

			user_display "gnome-terminal-server"	# The fact that the GNOME Terminal server requires an X display at startup is a bug.
			user_display "knotify"	# The fact that the KDE Notify server requires an X display at startup is a bug.
			user_display "mate-notification-daemon"	# The fact that the MATE Notification server requires an X display at startup is a bug.
			user_display "org.gnome.Weather.Application"	# The fact that the GNOME Weather server requires an X display at startup is a bug.
			user_display "org.gnome.Maps"	# The fact that the GNOME Maps server requires an X display at startup is a bug.

			user_simple_socket "dirmngr"
			user_simple_socket "gpg-agent"
			user_simple_socket "urxvt" "urxvtd"

			user_simple_service "emacs"

			user_socket_with_dedicated_logger "mpd"
			test -r "$hs/mpd/service/mpd.conf" || install -o "$i" -m 0644 -- "mpd.conf" "$hs/mpd/service/mpd.conf"
			install -d -o "$i" -m 0755 -- "$hc/mpd"
			test -r "$hc/mpd/mpd.conf" || setuidgid "$i" ln -s -- "../service-bundles/services/mpd/service/mpd.conf" "$hc/mpd/"
			install -d -o "$i" -m 0755 -- "$hd/mpd"
			install -d -o "$i" -m 0755 -- "$hd/mpd/playlists"
			test -r "$hd/mpd/database" || install -o "$i" -m 0755 -- /dev/null "$hd/mpd/database"
			install -d -o "$i" -m 0755 -- "$hh/mpd"
			test -r "$hh/mpd/state" || install -o "$i" -m 0755 -- /dev/null "$hh/mpd/state"

			user_dbus_service_with_dedicated_logger "pulseaudio" "org.pulseaudio.Server"

			install -o "$i" -m 0444 -- /dev/null "$hc/service-bundles/do-not-generate"
		fi

		redo-ifchange "$hc/service-bundles"
	fi
done
