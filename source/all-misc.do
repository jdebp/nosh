#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:

exec redo-ifchange \
	systemd/system/service-manager.socket \
	convert/per-user/at-spi-dbus-bus.service convert/per-user/clock-applet.service convert/per-user/dbus-daemon.service convert/per-user/dconf-service.service convert/per-user/evolution-addressbook-factory.service convert/per-user/evolution-calendar-factory.service convert/per-user/evolution-source-registry.service convert/per-user/evolution-user-prompter.service convert/per-user/gam_server.service convert/per-user/gconfd.service convert/per-user/gnome-maps.service convert/per-user/gnome-settings-daemon.service convert/per-user/gnome-terminal-server.service convert/per-user/gnome-weather-application.service convert/per-user/goa-daemon.service convert/per-user/goa-identity-service.service convert/per-user/gvfs-afc-volume-monitor.service convert/per-user/gvfs-daemon.service convert/per-user/gvfs-goa-volume-monitor.service convert/per-user/gvfs-gphoto2-volume-monitor.service convert/per-user/gvfs-hal-volume-monitor.service convert/per-user/gvfs-metadata.service convert/per-user/gvfs-mtp-volume-monitor.service convert/per-user/gvfs-udisks2-volume-monitor.service convert/per-user/mate-notification-daemon.service convert/per-user/mpd.service convert/per-user/notification-area-applet.service convert/per-user/obex.service convert/per-user/per-user.conf convert/per-user/wnck-applet.service convert/per-user/xfconfd.service convert/per-user/zeitgeist-fts.service \
	convert/dhclient@.service convert/ifconfig@.service convert/run-user-directory@.service convert/user-dbus-daemon.service \
	;
