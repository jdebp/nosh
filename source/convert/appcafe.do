#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
#
# Convert the PC-BSD/TrueOS appcafe external configuration formats.
# This is invoked by all.do .
#

set_if_unset() { if test -z "`system-control print-service-env \"$1\" \"$2\"`" ; then system-control set-service-env "$1" "$2" "$3" ; echo "$0: $1: Defaulted $2 to $3." ; fi ; }

# These get us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf "`which printenv`" "$1" ; }
dump_rc() { clearenv read-conf rc.conf "`which printenv`" ; }

read_conf() {
	test -r /usr/local/etc/appcafe.conf || return 0;
	sed -e '/[[:space:]]*;/d' /usr/local/etc/appcafe.conf |
	awk "{ if (\"$1\" == \$1) print \$3; }"
}

redo-ifchange rc.conf general-services

set_if_unset appcafe.target root /usr/local/share/appcafe
set_if_unset appcafe-dispatcher root /usr/local/share/appcafe
set_if_unset appcafe-nginx root /usr/local/share/appcafe
set_if_unset appcafe-php-fpm root /usr/local/share/appcafe
set_if_unset appcafe-ssl-keygen root /usr/local/share/appcafe

ssl="`read_conf ssl`"
port="`read_conf port`"

for service in appcafe-dispatcher appcafe-nginx appcafe-php-fpm
do
	system-control preset ${service}.service
	system-control preset --prefix "cyclog@" ${service}.service
done

case "${ssl}" in
[Tt][Rr][Uu][Ee]|[Yy][Ee][Ss]|[Oo][Nn]|1)
	conf="/usr/local/share/appcafe/nginx.conf.ssl"
	system-control preset appcafe-ssl-keygen
	;;
*)
	conf="/usr/local/share/appcafe/nginx.conf"
	system-control disable appcafe-ssl-keygen
	;;
esac
system-control preset --prefix "cyclog@" appcafe-ssl-keygen

case "${port}" in
8885|'')
	;;
*)
	sed -e "s/:8885/:${port}/g" "${conf}".temp.new
	mv "${conf}".temp.new "${conf}".temp
	conf="${conf}".temp
	;;
esac

system-control set-service-env appcafe-nginx conf "${conf}"

for target in appcafe
do
	if system-control is-enabled "${target}.target"
	then
		echo >> "$3" on "${target}"
	else
		echo >> "$3" off "${target}"
	fi
	system-control print-service-env "${target}.target" >> "$3"
done
for service in appcafe-dispatcher appcafe-nginx appcafe-php-fpm appcafe-ssl-keygen
do
	if system-control is-enabled "${service}"
	then
		echo >> "$3" on "${service}"
	else
		echo >> "$3" off "${service}"
	fi
	system-control print-service-env "${service}" >> "$3"
done
