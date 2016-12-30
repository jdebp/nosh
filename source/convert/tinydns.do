#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
#
# Special setup for tinydns.
# This is invoked by all.do .
#

set_if_unset() { if test -z "`system-control print-service-env \"$1\" \"$2\"`" ; then system-control set-service-env "$1" "$2" "$3" ; echo "$s: Defaulted $2 to $3." ; fi ; }

# These get us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf "`which printenv`" "$1" ; }
list_network_addresses() { for i in `read_rc network_addresses || echo 127.53.0.1` ; do echo "$i" ; done ; }

redo-ifchange rc.conf general-services "tinydns@.socket" "tinydns.service"

generate_private() {
cat << EOF
.127.in-addr.arpa:127.53.0.1:a:::lo
=1.0.53.127.in-addr.arpa:127.53.0.1:::lo
.10.in-addr.arpa:127.53.0.1:a:::si
.10.in-addr.arpa:127.53.0.1:a:::lo
.168.192.in-addr.arpa:127.53.0.1:a:::si
.168.192.in-addr.arpa:127.53.0.1:a:::lo
EOF
}

generate_Makefile() {
cat << EOF
all: data.cdb root

data.cdb: data ; tinydns-data

data: private public root
	echo  > \$@.tmp '# Generated file; do not edit directly.'
	@echo >> \$@.tmp '%lo:127'
	@echo >> \$@.tmp '%si:10'
	@echo >> \$@.tmp '%si:192.168'
	@echo >> \$@.tmp '# Put your data in the file named public.'
	@echo >> \$@.tmp '# Run make when you are done.'
	cat >> \$@.tmp public
	@echo >> \$@.tmp '# Generated file; do not edit directly.'
	@echo >> \$@.tmp '# Change the file named private if you want these delegated somewhere.'
	cat >> \$@.tmp private root
	mv -f -- \$@.tmp \$@

root: root.zone
	@echo  > \$@.tmp '# Generated file; do not edit directly.'
	@echo >> \$@.tmp '# Delete root.zone to cause a re-fetch from ICANN.'
	awk -F : '/^&/ { print \$\$1 ":" \$\$2 ":" \$\$3 ":::lo"; } /^\+/ { print \$\$1 ":" \$\$2 ":::lo"; }' \$> | sort -t : -k 2 -k 1 >> \$@.tmp
	mv -f -- \$@.tmp \$@

root.zone:
	tcp-socket-connect lax.xfr.dns.icann.org. 53 axfr-get . \$@ \$@.tmp
EOF
}

if s="`system-control find tinydns`"
then
	set_if_unset tinydns IP 127.53.0.1
	set_if_unset tinydns ROOT "root"

	system-control print-service-env tinydns >> "$3"

	test -r "${s}/service/root/private" || generate_private > "${s}/service/root/private"
	test -r "${s}/service/root/public" || : > "${s}/service/root/public"
	test -e "${s}/service/root/Makefile.original" || mv -- "${s}/service/root/Makefile" "${s}/service/root/Makefile.original"
	test -r "${s}/service/root/Makefile" || generate_Makefile > "${s}/service/root/Makefile"
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
	install -d -m 0755 "${s}/service/root"
	for i in alias childns ns mx host
	do
		echo '#!/command/execlineb -S0' > "${s}"/service/root/add-"$i"
		echo "tinydns-edit public public.new" >> "${s}"/service/root/add-"$i"
		echo "add $i \$@" >> "${s}"/service/root/add-"$i"
		chmod 0755 "${s}"/service/root/add-"$i"
	done
	test -r "${s}/service/root/public" || : > "${s}/service/root/public"
	test -r "${s}/service/root/private" || generate_private > "${s}/service/root/private"
	test -e "${s}/service/root/Makefile.original" || mv -- "${s}/service/root/Makefile" "${s}/service/root/Makefile.original"
	test -r "${s}/service/root/Makefile" || generate_Makefile > "${s}/service/root/Makefile"
	set_if_unset "${s}/" ROOT "root"

	if system-control is-enabled "${service}"
	then
		echo >> "$3" on "${service}"
	else
		echo >> "$3" off "${service}"
	fi
done
