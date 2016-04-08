#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
#
# Convert all external configuration formats.
# Use the "redo" command to run this .do script, via "redo all".
# 2016-01-24: This line forces a rebuild because of the new dependency tree.
#
common="geom mdmfs mdconfig ataidle ftp-proxy dnscache tinydns axfrdns kdm savecore sppp ppp rfcomm_pppd webcamd mariadb mysql natd securelevel openldap static-networking pefs kernel-vt syslogd ip6addrctl volumes general-services terminal-services kernel-modules user-services host.conf sysctl.conf"

case "`uname`" in
*BSD)
	platform="appcafe jails v9-jails warden autobridge stf uhidd"
	;;
*)
	platform=""
	;;
esac

redo-ifchange "rc.conf" ${common} ${platform}
