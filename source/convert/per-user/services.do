#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim:set filetype=sh:
#
# This prototype is copied into every (real) user's system-control/convert directory.
#
# This is run by the per-user external configuration import subsystem.
# It is used to compile all of the service bundles from source.

eu="--etc-bundle --user"
e="--no-systemd-quirks --escape-instance --bundle-root"

redo_ifchange_follow() {
	local i l
	for i
	do	
		while test -n "$i"
		do
			redo-ifchange "$i"
			l="`readlink \"$i\"`" || break
			case "$l" in
			/*)	i="$l" ;;
			*)	i="`dirname \"$i\"`/$l" || break ;;
			esac
		done
	done
}

make_raw_service_bundle() {
	system-control convert-systemd-units $eu $e "$hs/" "./$1$2"
	install -d -m 0755 -- "$hs/$1/service/env"
}

link_logger_service_to_log_directory() {
	rm -f -- "$hs/$2/main"
	ln -s -- "/var/log/user/$i/$1" "$hs/$2/main"
}

make_target_aliases() {
	local t="$1"
	shift
	for s
	do
		rm -f -- "$ht/$s"
		ln -s -- "$t" "$ht/$s"
	done
}

make_service_aliases() {
	local t="$1"
	shift
	for s
	do
		rm -f -- "$hs/$s"
		ln -s -- "$t" "$hs/$s"
	done
}

make_local_dbus_user_service_definitions() {
	for s
	do
		if ! test -e "$hd/dbus-1/services/$s.service"
		then
			printf > "$hd/dbus-1/services/$s.service" '# Avoid Desktop Bus activation.\n[D-BUS Service]\nName=%s\nUser=%s\nExec=/bin/exec system-control start --user %s' "$s" "$i" "$s"
			chown "$i" "$hd/dbus-1/services/$s.service"
		fi
	done
}

make_service_bundle_aliases_and_local_dbus_user_service_definitions() {
	make_service_aliases "$@"
	shift
	make_local_dbus_user_service_definitions "$@"
}

user_display() {
	if ! test -e "$hs/$1/service/env/DISPLAY"
	then
		system-control set-service-env "$hs/$1" DISPLAY ":0"
	fi
}

allow_dbus_bus_activation() {
	rm -f -- "$1/service/down"
}

link_service_to_logger_service() {
	rm -f -- "$hs/$1/log"
	ln -s -- "../$2" "$hs/$1/log"
	rm -f -- "$hs/$1/wants/log" "$hs/$1/after/log"
	ln -s -- "../log" "$hs/$1/wants/log"
	ln -s -- "../log" "$hs/$1/after/log"
}

user_simple_dbus_service() {
	redo_ifchange_follow "$1.service"
	make_raw_service_bundle "$1" ".service"
	link_service_to_logger_service "$1" "dbus-servers-log"
	make_service_bundle_aliases_and_local_dbus_user_service_definitions "$@"
	system-control --user preset "$1.service"
	allow_dbus_bus_activation "${hs}/$1"
}

user_simple_dbus_service_with_dedicated_logger() {
	redo_ifchange_follow "$1.service" "cyclog@.service"
	make_raw_service_bundle "$1" ".service"
	make_raw_service_bundle "cyclog@$1" ".service"
	link_logger_service_to_log_directory "$1" "cyclog@$1"
	link_service_to_logger_service "$1" "cyclog@$1"
	make_service_bundle_aliases_and_local_dbus_user_service_definitions "$@"
	system-control --user preset "$1.service"
	system-control --user preset --prefix "cyclog@" "$1.service"
	allow_dbus_bus_activation "${hs}/$1"
}

user_socket_dbus_service_with_dedicated_logger() {
	redo_ifchange_follow "$1.socket" "$1.service" "cyclog@.service"
	make_raw_service_bundle "$1" ".socket"
	make_raw_service_bundle "cyclog@$1" ".service"
	link_logger_service_to_log_directory "$1" "cyclog@$1"
	link_service_to_logger_service "$1" "cyclog@$1"
	make_service_bundle_aliases_and_local_dbus_user_service_definitions "$@"
	system-control --user preset "$1.socket"
	system-control --user preset --prefix "cyclog@" "$1.service"
	allow_dbus_bus_activation "${hs}/$1"
}

user_simple_service() {
	redo_ifchange_follow "$1.service"
	make_raw_service_bundle "$1" ".service"
	link_service_to_logger_service "$1" "simple-servers-log"
	make_service_aliases "$@"
	system-control --user preset "$1.service"
}

user_simple_socket() {
	redo_ifchange_follow "$1.socket" "$1.service"
	make_raw_service_bundle "$1" ".socket"
	link_service_to_logger_service "$1" "socket-servers-log"
	make_service_aliases "$@"
	system-control --user preset "$1.socket"
}

user_dbus_socket() {
	redo_ifchange_follow "$1.socket" "$1.service"
	make_raw_service_bundle "$1" ".socket"
	link_service_to_logger_service "$1" "dbus-servers-log"
	system-control --user preset "$1.socket"
}

user_socket_with_dedicated_logger() {
	redo_ifchange_follow "$1.socket" "$1.service" "cyclog@.service"
	make_raw_service_bundle "$1" ".socket"
	make_raw_service_bundle "cyclog@$1" ".service"
	link_logger_service_to_log_directory "$1" "cyclog@$1"
	link_service_to_logger_service "$1" "cyclog@$1"
	system-control --user preset "$1.socket"
	system-control --user preset --prefix "cyclog@" "$1.service"
}

user_service_with_dedicated_logger() {
	redo_ifchange_follow "$1.service" "cyclog@.service"
	make_raw_service_bundle "$1" ".service"
	make_raw_service_bundle "cyclog@$1" ".service"
	link_logger_service_to_log_directory "$1" "cyclog@$1"
	link_service_to_logger_service "$1" "cyclog@$1"
	make_service_aliases "$@"
	system-control --user preset "$1.service"
	system-control --user preset --prefix "cyclog@" "$1.service"
}

user_fan_in_logger() {
	redo_ifchange_follow "$1-log.service"
	make_raw_service_bundle "$1-log" ".service"
	link_logger_service_to_log_directory "$1" "$1-log"
	system-control --user preset "$1-log.service"
}

user_target() {
	redo_ifchange_follow "$1.target"
	system-control convert-systemd-units $eu $e "$ht/" "./$1.target"
}

relocate_user_service() {
        local e
	e=1
        if test -d "$hs/$1" && test \! -d "$hs/$2"
        then
                if system-control --user is-enabled "$hs/$1"
		then
			e=0
			system-control --user disable "$hs/$1"
		fi
		printf 1>&2 "Renaming %s's service bundle directory: %s to %s\n" "$i" "$1" "$2"
                mv -i -- "$hs/$1" "$hs/$2"
                test $e -ne 0 || system-control --user enable "$hs/$2"
        fi
}

make_user_log_directories() {
	for d
	do
		install -d -m 0750 "/var/log/user/$i/$d"
	done
}

relocate_user_log() {
        if test -d "/var/log/user/$i/$1" && test \! -d "/var/log/user/$i/$2"
        then
		printf 1>&2 "Renaming %s's log directory: %s to %s\n" "$i" "$1" "$2"
                mv -i -- "/var/log/user/$i/$1" "/var/log/user/$i/$2"
        fi
}

redo-ifchange user
read -r i < user
redo-ifchange home
read -r h < home

if test -z "$h" || test ! -d "$h/"
then
	echo 1>&2 "$h: Home missing or not a directory."
	exec false
fi

hd="$h/.local/share"
hh="$h/.cache"
hc="$h/.config"
ht="$hc/service-bundles/targets"
hs="$hc/service-bundles/services"
install -d -m 0755 -- "$hd"
install -d -m 0755 -- "$hh"
install -d -m 0755 -- "$hc"
install -d -m 0755 -- "$hc/service-bundles"
install -d -m 0755 -- "$ht"
install -d -m 0755 -- "$hs"
install -d -m 0755 -- "$hc/service-bundles/common"
install -d -m 0755 -- "$hc/service-bundles/common/env"

# system-wide external configuration import has already made the exit service bundle for us.
make_target_aliases "exit" halt reboot poweroff

install -d -m 0755 -- "$hd/dbus-1"
install -d -m 0755 -- "$hd/dbus-1/services"

# startup used to be called intrat.
test -d "$ht/startup" || test \! -d "$ht/intrat" || mv -f -- "$ht/intrat" "$ht/startup"

user_target "intrat"
make_target_aliases "intrat" sysinit basic

user_target "startup"
make_target_aliases "startup" normal multi-user emergency rescue workstation server

user_target "shutdown"
user_target "sockets"

make_user_log_directories "dbus-servers" "socket-servers" "simple-servers"
user_fan_in_logger "dbus-servers"
user_fan_in_logger "socket-servers"
user_fan_in_logger "simple-servers"
system-control --user enable "dbus-servers-log"
system-control --user enable "socket-servers-log"
system-control --user enable "simple-servers-log"

relocate_user_service "dbus" "dbus-daemon"
test -h "$hs/dbus" || ln -s "dbus-daemon" "$hs/dbus"
relocate_user_log "dbus" "dbus-daemon"
test -h "/var/log/user/$i/dbus" || ln -s "dbus-daemon" "/var/log/user/$i/dbus"

user_dbus_socket "dbus-daemon"
redo_ifchange_follow "per-user.conf"
test -r "$hs/dbus-daemon/service/per-user.conf" || install -m 0644 -- "per-user.conf" "$hs/dbus/service/per-user.conf"

relocate_user_service "org.gnome.Weather.Application" "gnome-weather-application"
relocate_user_service "org.gnome.Maps" "gnome-maps"

user_simple_dbus_service "at-spi-dbus-bus" "org.a11y.Bus"
user_simple_dbus_service "clock-applet" "org.mate.panel.applet.ClockAppletFactory"
user_simple_dbus_service "dconf-editor" "ca.desrt.dconf-editor"
user_simple_dbus_service "dconf-service" "ca.desrt.dconf"
user_simple_dbus_service "dunst" "org.knopwob.dunst"
user_simple_dbus_service "evolution-addressbook-factory" "org.gnome.evolution.dataserver.AddressBook9"
user_simple_dbus_service "evolution-calendar-factory" "org.gnome.evolution.dataserver.Calendar7"
user_simple_dbus_service "evolution-source-registry" "org.gnome.evolution.dataserver.Sources5"
user_simple_dbus_service "evolution-user-prompter" "org.gnome.evolution.dataserver.UserPrompter0"
user_simple_dbus_service "gconfd" "org.gnome.GConf"
user_simple_dbus_service "gedit" "org.gnome.gedit"
user_simple_dbus_service "gnome-keyring-daemon" "org.gnome.keyring" "org.freedesktop.secrets"
user_simple_dbus_service "gnome-maps" "org.gnome.Maps"
user_simple_dbus_service "gnome-settings-daemon" "org.gnome.SettingsDaemon.XSettings"
user_simple_dbus_service "gnome-shell" "org.gnome.Shell"
user_simple_dbus_service "gnome-terminal-server" "org.gnome.Terminal"
user_simple_dbus_service "gnome-weather-application" "org.gnome.Weather.Application"
user_simple_dbus_service "goa-daemon" "org.gnome.OnlineAccounts"
user_simple_dbus_service "goa-identity-service" "org.gnome.Identity"
user_simple_dbus_service "gvfs-afc-volume-monitor" "org.gtk.Private.AfcVolumeMonitor" "org.gtk.vfs.AfcVolumeMonitor"
user_simple_dbus_service "gvfs-daemon" "org.gtk.vfs.Daemon"
user_simple_dbus_service "gvfs-goa-volume-monitor" "org.gtk.Private.GoaVolumeMonitor" "org.gtk.vfs.GoaVolumeMonitor"
user_simple_dbus_service "gvfs-gphoto2-volume-monitor" "org.gtk.Private.GPhoto2VolumeMonitor" "org.gtk.vfs.GPhoto2VolumeMonitor"
user_simple_dbus_service "gvfs-hal-volume-monitor" "org.gtk.Private.HalVolumeMonitor" "org.gtk.vfs.HalVolumeMonitor"
user_simple_dbus_service "gvfs-metadata" "org.gtk.vfs.Metadata"
user_simple_dbus_service "gvfs-mtp-volume-monitor" "org.gtk.Private.MTPVolumeMonitor" "org.gtk.vfs.MTPVolumeMonitor"
user_simple_dbus_service "gvfs-udisks2-volume-monitor" "org.gtk.Private.UDisks2VolumeMonitor" "org.gtk.vfs.UDisks2VolumeMonitor"
user_simple_dbus_service "ibus-daemon" "org.freedesktop.IBus"
user_simple_dbus_service "kded4" "org.kde.kded4"
user_simple_dbus_service "kdeinit4" "org.kde.klauncher4"
user_simple_dbus_service "knotify4" "org.kde.knotify"
user_simple_dbus_service "mate-notification-daemon" "org.freedesktop.mate.Notifications" "org.freedesktop.Notifications"
user_simple_dbus_service "mate-screensaver" "org.mate.ScreenSaver"
user_simple_dbus_service "notification-area-applet" "org.mate.panel.applet.NotificationAreaAppletFactory"
user_simple_dbus_service "obex" "org.bluez.obex" "dbus-org.bluez.obex"
user_simple_dbus_service "telepathy-mission-control-5" "org.freedesktop.Telepathy.AccountManager"
user_simple_dbus_service "wnck-applet" "org.mate.panel.applet.WnckletFactory"
user_simple_dbus_service "xdg-desktop-portal-gtk" "org.freedesktop.implo.portal.desktop.gtk"
user_simple_dbus_service "xfconfd" "org.xfce.Xfconf"
user_simple_dbus_service "zeitgeist-daemon" "org.gnome.zeitgeist.Engine"
user_simple_dbus_service "zeitgeist-datahub" "org.gnome.zeitgeist.datahub"
user_simple_dbus_service "zeitgeist-fts" "org.gnome.zeitgeist.SimpleIndexer"

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
		basename "${i}" .service
	done
done |
awk '!x[$0]++' |
while read -r i
do
	test ! -L "${hs}/$i" || continue
	user_simple_dbus_service "$i"
	printf "dbus/%s\n" "$i" >> "$3"
done

system-control --user preset "zeitgeist-fts"

test -e "$hc/service-bundles/common/env/DISPLAY" || system-control set-service-env "$hc/service-bundles/common" DISPLAY ":0"
# We do not need to set a common DBUS_SESSION_BUS_ADDRESS because userenv in the service definitions will do that for us.

user_simple_socket "dirmngr"
user_simple_socket "gpg-agent"
user_simple_socket "urxvt" "urxvtd"
user_simple_socket "watchman"

user_simple_service "emacs"
user_simple_service "ncftpbatch"
user_simple_service "gam_server"
user_simple_service "speech-dispatcher"

make_user_log_directories "uscheduled"
user_socket_with_dedicated_logger "uscheduled" "uschedule"
if test -d "$h/.uschedule" &&
   test ! -e "$hc/uschedule"
then
	mv -f -- "$h/.uschedule" "$hc/uschedule"
else
	install -d -m 0700 -- "$hc/uschedule"
fi
test -e "$h/.uschedule" || ln -s -- ".config/uschedule" "$h/.uschedule"
install -d -m 0700 -- "$hc/uschedule/commands"

make_user_log_directories "mpd"
user_socket_with_dedicated_logger "mpd"
redo_ifchange_follow "mpd.conf"
test -r "$hs/mpd/service/mpd.conf" || install -m 0644 -- "mpd.conf" "$hs/mpd/service/mpd.conf"
install -d -m 0755 -- "$hc/mpd"
test -r "$hc/mpd/mpd.conf" || ln -s -- "../service-bundles/services/mpd/service/mpd.conf" "$hc/mpd/"
install -d -m 0755 -- "$hd/mpd"
install -d -m 0755 -- "$hd/mpd/playlists"
test -r "$hd/mpd/database" || install -m 0755 -- /dev/null "$hd/mpd/database"
install -d -m 0755 -- "$hh/mpd"
test -r "$hh/mpd/state" || install -m 0755 -- /dev/null "$hh/mpd/state"

make_user_log_directories "pulseaudio"
user_socket_dbus_service_with_dedicated_logger "pulseaudio" "org.pulseaudio.Server"
