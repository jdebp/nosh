#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
#
# Convert all external configuration formats.
# Use the "redo" command to run this .do script, via "redo all".
# 2016-01-24: This line forces a rebuild because of the new dependency tree.
#
redo-ifchange rc.conf geom mdmfs mdconfig dnscache tinydns axfrdns savecore sppp ppp rfcomm_pppd webcamd natd openldap static-networking pefs kernel-vt ip6addrctl volumes general-services terminal-services kernel-modules user-services host.conf sysctl.conf

case "`uname`" in
*BSD)
	redo-ifchange jails v9-jails warden
	;;
esac
