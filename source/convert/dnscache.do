#!/bin/sh -e
#
# Special setup for dnscache.
# This is invoked by general-services.do .
#

s="`system-control find dnscache`"

set_if_unset() { if test -z "`rcctl get \"$1\" \"$2\"`" ; then rcctl set "$1" "$2" "$3" ; echo "$s: Defaulted $2 to $3." ; fi ; }

set_if_unset dnscache IPSEND 0.0.0.0
set_if_unset dnscache IP 127.0.0.1
set_if_unset dnscache DATALIMIT 3000000
set_if_unset dnscache CACHESIZE 1000000
set_if_unset dnscache ROOT "$s/service/root"

rcctl get dnscache >> "$3"

mkdir -p -m 0755 "$s/service/root"
mkdir -p -m 0755 "$s/service/root/ip"
mkdir -p -m 0755 "$s/service/root/servers"

test -r "$s/service/seed" || dd if=/dev/urandom of="$s/service/seed" bs=128 count=1

test -r "$s/service/root/servers/@" || echo '127.53.0.1' > "$s/service/root/servers/@"
touch "$s/service/root/ip/127.0.0.1"
