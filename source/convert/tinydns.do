#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Special setup for tinydns.
# This is invoked by all.do .
#

set_if_unset() { if test -z "`system-control print-service-env \"$1\" \"$2\"`" ; then system-control set-service-env "$1" "$2" "$3" ; echo "$s: Defaulted $2 to $3." ; fi ; }

# These get us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf printenv "$1" ; }
list_network_addresses() { ( read_rc network_addresses || echo 127.53.0.1 ) | fmt -w 1 ; }

redo-ifchange rc.conf general-services "tinydns@.socket" "tinydns.service"

generate_private() {
awk -F : '/^\./ { print $0 ":::si"; print $0 ":::lo"; next; } { print; }' << EOF
# BEGIN http://jdebp.eu./FGA/dns-private-address-split-horizon.html#WhatToDo
.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.ip6.arpa:127.53.0.1:a
.1.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.ip6.arpa:127.53.0.1:a
.f.7.2.0.0.2.ip6.arpa:127.53.0.1:a
.e.f.9.a.2.0.0.2.ip6.arpa:127.53.0.1:a
.e.2.0.0.2.ip6.arpa:127.53.0.1:a
.D.F.ip6.arpa:127.53.0.1:a
.8.E.F.ip6.arpa:127.53.0.1:a
.9.E.F.ip6.arpa:127.53.0.1:a
.A.E.F.ip6.arpa:127.53.0.1:a
.B.E.F.ip6.arpa:127.53.0.1:a
.C.E.F.ip6.arpa:127.53.0.1:a
.D.E.F.ip6.arpa:127.53.0.1:a
.E.E.F.ip6.arpa:127.53.0.1:a
.F.E.F.ip6.arpa:127.53.0.1:a
.0.in-addr.arpa:127.53.0.1:a
.10.in-addr.arpa:127.53.0.1:a
.127.in-addr.arpa:127.53.0.1:a
.254.169.in-addr.arpa:127.53.0.1:a
.16.172.in-addr.arpa:127.53.0.1:a
.17.172.in-addr.arpa:127.53.0.1:a
.18.172.in-addr.arpa:127.53.0.1:a
.19.172.in-addr.arpa:127.53.0.1:a
.20.172.in-addr.arpa:127.53.0.1:a
.21.172.in-addr.arpa:127.53.0.1:a
.22.172.in-addr.arpa:127.53.0.1:a
.23.172.in-addr.arpa:127.53.0.1:a
.24.172.in-addr.arpa:127.53.0.1:a
.25.172.in-addr.arpa:127.53.0.1:a
.26.172.in-addr.arpa:127.53.0.1:a
.27.172.in-addr.arpa:127.53.0.1:a
.28.172.in-addr.arpa:127.53.0.1:a
.29.172.in-addr.arpa:127.53.0.1:a
.30.172.in-addr.arpa:127.53.0.1:a
.31.172.in-addr.arpa:127.53.0.1:a
.2.0.192.in-addr.arpa:127.53.0.1:a
.168.192.in-addr.arpa:127.53.0.1:a
.255.255.255.255.in-addr.arpa:127.53.0.1:a
# END http://jdebp.eu./FGA/dns-private-address-split-horizon.html#WhatToDo

.localhost:127.53.0.1:a
.test:127.53.0.1:a
.invalid:127.53.0.1:a
.example:127.53.0.1:a
.example.org:127.53.0.1:a
.example.net:127.53.0.1:a
.example.com:127.53.0.1:a

# RFC 7686
.onion:127.53.0.1:a

.:127.53.0.1:a
=1.0.53.127.in-addr.arpa:127.53.0.1
EOF
}

generate_Makefile() {
cat << EOF
all: data.cdb root

data.cdb: data ; tinydns-data

data: private public root
	@echo  > \$@.tmp '# data is a generated file; do not edit it directly.'
	@echo >> \$@.tmp '%lo:127'
	@echo >> \$@.tmp '%si:10'
	@echo >> \$@.tmp '%si:192.168'
	@echo >> \$@.tmp '# Put your data in the file named public.'
	@echo >> \$@.tmp '# Run make when you are done.'
	cat >> \$@.tmp public
	@echo >> \$@.tmp '# data is a generated file; do not edit it directly.'
	@echo >> \$@.tmp '# Change the file named private if you want these delegated somewhere.'
	cat >> \$@.tmp private root
	mv -f -- \$@.tmp \$@

root: root.zone
	@echo  > \$@.tmp '# root is a generated file; do not edit it directly.'
	@echo >> \$@.tmp '# Delete root.zone to cause a re-fetch from ICANN.'
	awk -F : '/^&/ { print \$\$1 ":" \$\$2 ":" \$\$3 ":::lo"; } /^\+/ { print \$\$1 ":" \$\$2 ":::lo"; }' root.zone | sort -t : -k 2 -k 1 >> \$@.tmp
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

	redo-ifchange "${s}/service/root"

	test -r "${s}/service/root/private" || generate_private > "${s}/service/root/private"
	if ! test -r "${s}/service/root/public" 
	then
		if test -e "${s}/service/root/data"
		then
			mv -- "${s}/service/root/data" "${s}/service/root/public"
		else
			: > "${s}/service/root/public"
		fi
	fi
	if test -e "${s}/service/root/data" && ! test -e "${s}/service/root/data.original" 
	then
		mv -- "${s}/service/root/data" "${s}/service/root/data.original"
	fi
	test -e "${s}/service/root/Makefile.original" || test \! -e "${s}/service/root/Makefile" || mv -- "${s}/service/root/Makefile" "${s}/service/root/Makefile.original"
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
	test -r "${s}/service/root/Makefile" || generate_Makefile > "${s}/service/root/Makefile"
	set_if_unset "${s}/" ROOT "root"

	if system-control is-enabled "${service}"
	then
		echo >> "$3" on "${service}"
	else
		echo >> "$3" off "${service}"
	fi
done
