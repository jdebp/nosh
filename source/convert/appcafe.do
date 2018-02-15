#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Convert the TrueOS appcafe external configuration formats.
# This is invoked by all.do .
#

set_if_unset() { if test -z "`system-control print-service-env \"$1\" \"$2\"`" ; then system-control set-service-env "$1" "$2" "$3" ; echo "$0: $1: Defaulted $2 to $3." ; fi ; }

redo-ifchange general-services appcafe-nginx.conf

dir="`system-control find appcafe-nginx`"

install -m 0644 "appcafe-nginx.conf" "${dir}/service/nginx.conf"
for i in mime.types fastcgi_params
do
	if ! test -r "${dir}/service/${i}"
	then
		rm -f -- "${dir}/service/${i}"
		ln -s -- "/usr/local/share/appcafe/${i}" "${dir}/service/${i}"
	fi
done

# These are not used, though.
set_if_unset appcafe.target root /usr/local/share/appcafe
set_if_unset appcafe-dispatcher root /usr/local/share/appcafe
set_if_unset appcafe-nginx root /usr/local/share/appcafe
set_if_unset appcafe-php-fpm root /usr/local/share/appcafe
set_if_unset appcafe-ssl-keygen root /usr/local/share/appcafe

# This is not needed any more, and is for backwards compatibility with the older service bundle.
system-control set-service-env appcafe-nginx conf "nginx.conf"

for target in appcafe
do
	system-control preset -- "${target}".target
done

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
	system-control preset ${service}.service
	system-control preset --prefix "cyclog@" ${service}.service
done

if ! fgrep -q "ssl on;" "appcafe-nginx.conf"
then
	system-control disable appcafe-ssl-keygen
fi

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
