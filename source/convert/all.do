#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
#
# Convert all external configuration formats.
# Use the "redo" command to run this .do script, via "redo all".
# 2016-01-24: This line forces a rebuild because of the new dependency tree.
#
redo-ifchange rc.conf geom mdmfs mdconfig ataidle ftp-proxy dnscache tinydns axfrdns kdm savecore sppp ppp rfcomm_pppd webcamd natd securelevel openldap static-networking pefs kernel-vt ip6addrctl volumes general-services terminal-services kernel-modules user-services host.conf sysctl.conf

case "`uname`" in
*BSD)
	redo-ifchange appcafe jails v9-jails warden
	;;
esac
