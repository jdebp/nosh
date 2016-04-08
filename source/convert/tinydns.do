#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
#
# Special setup for tinydns.
# This is invoked by general-services.do .
#

set_if_unset() { if test -z "`system-control print-service-env \"$1\" \"$2\"`" ; then system-control set-service-env "$1" "$2" "$3" ; echo "$s: Defaulted $2 to $3." ; fi ; }

# These get us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf "`which printenv`" "$1" ; }
list_network_addresses() { read_rc network_addresses || echo 127.53.0.1 | while read -r i ; do echo "$i" ; done ; }

redo-ifchange rc.conf general-services "tinydns@.socket" "tinydns.service"

if s="`system-control find tinydns`"
then
	set_if_unset tinydns IP 127.53.0.1
	set_if_unset tinydns ROOT "root"

	system-control print-service-env tinydns >> "$3"

	test -r "$s/service/root/data" || echo '.' > "$s/service/root/data"
	test -r "$s/service/root/Makefile" || echo 'data.cdb : data ; tinydns-data' > "$s/service/root/Makefile"
fi

lr="/var/local/sv/"
e="--no-systemd-quirks --escape-instance --bundle-root"

list_network_addresses |
while read -r i
do
	test -z "$i" && continue
	service="tinydns@$i"
	s="$lr/${service}"

	system-control convert-systemd-units $e "$lr/" "./${service}.socket"
	system-control preset "${service}"
	rm -f -- "${s}/log"
	ln -s -- "../../../sv/cyclog@tinydns" "${s}/log"

	install -d -m 0755 "${s}/service/env"
	install -d -m 0755 "${s}"/service/root
	for i in alias childns ns mx host
	do
		echo '#!/command/execlineb -S0' > "${s}"/service/root/add-"$i"
		echo "tinydns-edit data data.new" >> "${s}"/service/root/add-"$i"
		echo "add $i \$@" >> "${s}"/service/root/add-"$i"
		chmod 0755 "${s}"/service/root/add-"$i"
	done
	test -r "${s}/service/root/data" || echo '.' > "${s}/service/root/data"
	test -r "${s}/service/root/Makefile" || echo 'data.cdb : data ; tinydns-data' > "${s}/service/root/Makefile"
	set_if_unset "${s}/" ROOT "root"

	if system-control is-enabled "${service}"
	then
		echo >> "$3" on "${service}"
	else
		echo >> "$3" off "${service}"
	fi
done
