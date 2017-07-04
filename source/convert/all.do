#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
#
# Convert all external configuration formats.
# Use the "redo" command to run this .do script, via "redo all".
# 2016-01-24: This line forces a rebuild because of the new dependency tree.
#
common="ataidle axfrdns brltty dnscache ftp-proxy general-services geom host.conf hostname iovctl java-home kdm kernel-modules kernel-vt mdconfig mysql nfs ntp openldap openvpn pefs savecore securelevel static-networking sysctl.conf syslogd system-installer terminal-services tinydns user-services volumes webcamd"

case "`uname`" in
*BSD)
	platform="appcafe autobridge ip6addrctl jails ldconfig mdmfs mixer stf uhidd v9-jails warden"
	;;
*)
	platform=""
	;;
esac

redo-ifchange "rc.conf" ${common} ${platform}
