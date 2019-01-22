#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Convert all external configuration formats.
# Use the "redo" command to run this .do script, via "redo all".
# 2016-01-24: This line forces a rebuild because of the new dependency tree.
#
common="ataidle axfrdns brltty dbus-services dnscache ftp-proxy general-services geom host.conf hostname iovctl java-home kdm kernel-modules kernel-vt mdconfig mysql nfs ntp openldap openvpn pefs rc.conf savecore securelevel static-networking sysctl.conf syslogd system-installer terminal-services tinydns user-services volumes webcamd"

case "`uname`" in
Linux)
	platform="motd"
	;;
*BSD)
	platform="appcafe autobridge ip6addrctl jails ldconfig mdmfs mixer stf uhidd v9-jails warden"
	;;
*)
	platform=""
	;;
esac

if test -x local.do
then
	local="local"
else
	local=""
	redo-ifcreate local.do
fi

redo-ifchange ${common} ${platform} ${local}
