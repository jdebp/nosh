#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Convert the TrueOS appcafe-nginx external configuration formats.
# This is invoked by appcafe.do .
#

redo-ifchange rc.conf
if test -r /usr/local/etc/appcafe.conf
then
	redo-ifchange /usr/local/etc/appcafe.conf
else
	redo-ifcreate /usr/local/etc/appcafe.conf
	exit 0
fi

# This gets us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf printenv "$1" ; }

read_conf() {
	read_rc appcafe_nginx_"$1" && return 0
	sed -e '/[[:space:]]*;/d' /usr/local/etc/appcafe.conf |
	awk "{ if (\"$1\" == \$1) print \$3; }"
}

ssl="`read_conf ssl`"
port="`read_conf port`"

case "${ssl}" in
[Tt][Rr][Uu][Ee]|[Yy][Ee][Ss]|[Oo][Nn]|1)
	conf="/usr/local/share/appcafe/nginx.conf.ssl"
	;;
*)
	conf="/usr/local/share/appcafe/nginx.conf"
	;;
esac
redo-ifchange "${conf}"

case "${port}" in
8885|'')
	cat "${conf}" > "$3"
	;;
*)
	sed -e "s/:8885/:${port}/g" "${conf}" > "$3"
	;;
esac
