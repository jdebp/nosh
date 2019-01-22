#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Special setup for nsswitch.conf.
# This is invoked by host.conf.do .
#

nss="/etc/`basename "$1"`"

redo-ifchange "$nss" general-services

if ! command -v pc-nssconf >/dev/null
then
	redo-ifcreate /usr/local/bin/pc-nssconf /usr/bin/pc-nssconf
	exec true
fi

if system-control is-enabled "pc_activedirectory" 2>/dev/null
then
	p="winbind"
	m="ldap"
elif system-control is-enabled "pc_ldap" 2>/dev/null
then
	p="ldap"
	m="winbind"
else
	pc-nssconf -f "$nss" -d group -s "-winbind" -s "-ldap" -d passwd -s "-winbind" -s "-ldap" > "$3"
	exec true
fi

pc-nssconf -f "$nss" -d group -s "$p" -s "$m" -d passwd -s "$p" -s "$m" "- > "$3"
