#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Special setup for dnscache.
# This is invoked by all.do .
#
# See
#   https://jdebp.uk/FGA/dns-private-address-split-horizon.html#WhatToDo
#   https://jdebp.uk/FGA/dns-split-horizon.html
#

set_if_unset() { if test -z "`system-control print-service-env \"$1\" \"$2\"`" ; then system-control set-service-env "$1" "$2" "$3" ; echo "$s: Defaulted $2 to $3." ; fi ; }
dir_not_empty() { test -n "`/bin/ls -A \"$1\"`" ; }

# These get us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf printenv "$1" ; }
list_network_addresses() { printf '%s\n' `read_rc dnscache_network_addresses || echo 127.0.0.1` ; }
show() {
	local service
	for service
	do
		if system-control is-enabled "${service}"
		then
			echo on "${service}"
		else
			echo off "${service}"
		fi
		system-control print-service-env "${service}" | sed -e 's/^/	/'
	done
}

redo-ifchange rc.conf general-services "dnscache@.socket" "dnscache.service"

list_tinydns_graft_points() {
	# A private content server will out of the box also say "no such domain" for reverse lookups where the global DNS does.
	# So no non-existent reverse lookup domains are asked of it, out of the box.
	# walldns provides positive answers, so grafts point there.
	# It is up to the administrator to re-assign the grafts when populating IP addresses begins.

	# forward lookups for homenet and test domains
	# These are blackholed or non-delegated in the global DNS.
	printf '%s\n' 'home.arpa' 'test'
}

list_walldns_graft_points() {
	# reverse lookups for machine-local IPv4 and IPv6 addresses
	# dnscache and DNS client libraries swallow these
	printf '%s.in-addr.arpa\n' '0' '127'
	printf '%s.ip6.arpa\n' '0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0' '1.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0'
	# equivalent 6-to-4 reverse lookups
	printf '%s.ip6.arpa\n' '0.0.2.0.0.2' 'F.7.2.0.0.2'

	# reverse lookups for link-local IPv4 addresses
	# DNS client libraries and NSS and dnscache swallow these
	printf '%s.in-addr.arpa\n' '254.169'
	# equivalent 6-to-4 reverse lookups
	printf '%s.ip6.arpa\n' 'E.F.9.A.2.0.0.2'

	# reverse lookups for link-local IPv6 addresses
	# Their subdomains are "no such domain" in the global DNS.
	printf '%s.ip6.arpa\n' '8.E.F' '9.E.F' 'A.E.F' 'B.E.F'

	# reverse lookups for site-local IPv4 addresses
	# These are blackholed in the global DNS.
	printf '%s.in-addr.arpa\n' '10' '16.172' '17.172' '18.172' '19.172' '20.172' '21.172' '22.172' '23.172' '24.172' '25.172' '26.172' '27.172' '28.172' '29.172' '30.172' '31.172' '168.192'
	# equivalent 6-to-4 reverse lookups
	printf '%s.ip6.arpa\n' 'A.0.2.0.0.2' '1.C.A.2.0.0.2' '8.A.0.C.2.0.0.2'

	# reverse lookups for (old) site-local IPv6 addresses
	# Their subdomains are "no such domain" in the global DNS.
	printf '%s.ip6.arpa\n' 'C.E.F' 'D.E.F' 'E.E.F' 'F.E.F'

	# reverse lookups for unique-local addresses
	# Their subdomains are "no such domain" in the global DNS.
	printf '%s.ip6.arpa\n' 'C.F' 'D.F'

	# reverse lookups for interface-local-scope multicast IPv4 addresses
	# Their subdomains are "no such domain" in the global DNS.
	printf '%s.ip6.arpa\n' '1.0.F.F'

	# reverse lookups for link-local-scope multicast IPv4 addresses
	# Their subdomains are "no such domain" in the global DNS.
	printf '%s.ip6.arpa\n' '2.0.F.F'

	# reverse lookups for site-scope multicast IPv4 addresses
	# Their subdomains are "no such domain" in the global DNS.
	printf '%s.ip6.arpa\n' 5.0.F.F

	# reverse lookups for organization-scope multicast IPv4 addresses
	# Their subdomains are "no such domain" in the global DNS.
	printf '%s.ip6.arpa\n' 8.0.F.F

	# reverse lookups for IPv4 IANA test network address ranges
	# Their subdomains are "no such domain" in the global DNS.
	printf '%s.in-addr.arpa\n' '2.0.192' '100.51.198' '113.0.203'
	# equivalent 6-to-4 reverse lookups
	printf '%s.ip6.arpa\n' '2.0.0.0.0.C.2.0.0.2' '4.6.3.3.6.C.2.0.0.2' '1.7.0.0.B.C.2.0.0.2'

	# reverse lookups for doco-only IPv6 addresses
	# Their subdomains are "no such domain" in the global DNS.
	printf '%s.ip6.arpa\n' '8.b.d.0.1.0.0.2'

	# NAT64 discovery invariant information
	printf '%s.in-addr.arpa\n' '170.0.0.192' '171.0.0.192'
	printf '%s.ip6.arpa\n' 'A.A.0.0.0.0.0.C.2.0.0.2' 'A.B.0.0.0.0.0.C.2.0.0.2'
	printf '%s\n' 'ipv4only.arpa'

	# forward lookups for mDNS domains
	# DNS client libraries and NSS and dnscache swallow these
	printf '%s\n' 'local'

	# reverse lookups for special broadcast IPv4 addresses
	printf '%s.in-addr.arpa\n' '255.255.255.255'
}

# Old system: One pre-packaged dnscache server for the entire machine
if s="`system-control find dnscache`"
then
	install -d -m 0755 "${s}/service/root/ip"
	install -d -m 0755 "${s}/service/root/servers"
	# We do not use an on-disc seed file any more.
	test \! -e "${s}/service/seed" || chmod 0 "${s}/service/seed" 
	test -r "${s}/service/root/servers/@" || echo '127.53.0.1' > "${s}/service/root/servers/@"
	test -r "${s}/service/root/servers/localhost" || echo '127.53.1.1' > "${s}/service/root/servers/localhost"
	list_tinydns_graft_points |
	while read -r d
	do
		if ! test -r "${s}/service/root/servers/${d}"
		then
			ln "${s}/service/root/servers/@" "${s}/service/root/servers/${d}"
		fi
	done
	list_walldns_graft_points |
	while read -r d
	do
		if ! test -r "${s}/service/root/servers/${d}"
		then
			ln "${s}/service/root/servers/localhost" "${s}/service/root/servers/${d}"
		fi
	done
	dir_not_empty "${s}/service/root/ip" || touch "${s}/service/root/ip/127.0.0.1"
	set_if_unset dnscache IP 127.0.0.1
	set_if_unset dnscache CACHESIZE 1000000
	set_if_unset dnscache ROOT "root"

	show "${s}" >> "$3"
fi

# New system: Individual dnscache servers for every IP adddress in dnscache_netework_addresses in rc.conf

test -h /var/local/service-bundles/targets || { install -d -m 0755 /var/local/service-bundles && ln -s /etc/service-bundles/targets /var/local/service-bundles/ ; }
lr="/var/local/service-bundles/services/"
e="--no-systemd-quirks --escape-instance --local-bundle"

list_network_addresses |
while read -r i
do
	test -z "$i" && continue
	service="dnscache@$i"
	s="$lr/${service}"

	system-control convert-systemd-units $e --bundle-root "$lr/" "./${service}.socket"
	rm -f -- "${s}/log"
	ln -s -f -- "../../../../service-bundles/services/cyclog@dnscache" "${s}/log"

	install -d -m 0755 "${s}/service/env"
	install -d -m 0755 "${s}/service/root"
	install -d -m 0755 "${s}/service/root/ip"
	install -d -m 0755 "${s}/service/root/servers"
	# We do not use an on-disc seed file any more.
	test \! -e "${s}/service/seed" || chmod 0 "${s}/service/seed" 
	test -r "${s}/service/root/servers/@" || echo '127.53.0.1' > "${s}/service/root/servers/@"
	test -r "${s}/service/root/servers/localhost" || echo '127.53.1.1' > "${s}/service/root/servers/localhost"
	list_tinydns_graft_points |
	while read -r d
	do
		if ! test -r "${s}/service/root/servers/${d}"
		then
			ln "${s}/service/root/servers/@" "${s}/service/root/servers/${d}"
		fi
	done
	list_walldns_graft_points |
	while read -r d
	do
		if ! test -r "${s}/service/root/servers/${d}"
		then
			ln "${s}/service/root/servers/localhost" "${s}/service/root/servers/${d}"
		fi
	done
	dir_not_empty "${s}/service/root/ip" || touch "${s}/service/root/ip/127.0.0.1"
	set_if_unset "${s}/" CACHESIZE 1000000
	set_if_unset "${s}/" ROOT "root"

	system-control preset --rcconf-file rc.conf "${service}"
	show "${s}" >> "$3"
done
