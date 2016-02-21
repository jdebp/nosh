#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
#
# Special setup for static networking services
# This is invoked by general-services.do .
#

# These get us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf "`which printenv`" "$1" ; }
get_var1() { read_rc "$1" || true ; }
get_var2() { read_rc "$1"_"$2" || true ; }
car() { echo "$1" ; }
cdr() { shift ; echo "$@" ; }
list_static_arp() { for i in `get_var1 static_arp_pairs` ; do echo "$i" ; done }
list_static_ndp() { for i in `get_var1 static_ndp_pairs` ; do echo "$i" ; done }
list_static_ip4() { for i in `get_var1 static_routes` ; do echo "$i" ; done }
list_static_ip6() { for i in `get_var1 ipv6_static_routes` ; do echo "$i" ; done }

redo-ifchange rc.conf general-services "static_arp@.service" "static_ndp@.service" "static_ip4@.service" "static_ip6@.service"

r="/var/local/sv"
e="--no-systemd-quirks --escape-instance --bundle-root"

make_arp() {
	local service
	service="static_arp@$1"
	system-control convert-systemd-units $e "$r/" "./${service}.service"
	mkdir -p -m 0755 "$r/${service}/service/env"
	system-control disable "${service}"
	system-control set-service-env "$r/${service}" addr "`car $2`"
	system-control set-service-env "$r/${service}" dest "`cdr $2`"
	rm -f -- "$r/${service}/log"
	ln -s -- "../../../sv/cyclog@network-interfaces" "$r/${service}/log"
	rm -f -- "$r/${service}/wants/network-runtime"
	ln -s -- "../../../../sv/network-runtime" "$r/${service}/wants/"
	rm -f -- "$r/${service}/after/network-runtime"
	ln -s -- "../../../../sv/network-runtime" "$r/${service}/after/"
	rm -f -- "$r/${service}/wants/static-networking"
	rm -f -- "$r/${service}/wanted-by/server"
	rm -f -- "$r/${service}/wanted-by/static-networking"
	ln -s -- "/etc/service-bundles/targets/static-networking" "$r/${service}/wanted-by/"
	system-control preset "${service}"
}

make_ndp() {
	local service
	service="static_ndp@$i"
	system-control convert-systemd-units $e "$r/" "./${service}.service"
	mkdir -p -m 0755 "$r/${service}/service/env"
	system-control disable "${service}"
	system-control set-service-env "$r/${service}" addr "`car $pair`"
	system-control set-service-env "$r/${service}" dest "`cdr $pair`"
	rm -f -- "$r/${service}/log"
	ln -s -- "../../../sv/cyclog@network-interfaces" "$r/${service}/log"
	rm -f -- "$r/${service}/wants/network-runtime"
	ln -s -- "../../../../sv/network-runtime" "$r/${service}/wants/"
	rm -f -- "$r/${service}/after/network-runtime"
	ln -s -- "../../../../sv/network-runtime" "$r/${service}/after/"
	rm -f -- "$r/${service}/wants/static-networking"
	rm -f -- "$r/${service}/wanted-by/server"
	rm -f -- "$r/${service}/wanted-by/static-networking"
	ln -s -- "/etc/service-bundles/targets/static-networking" "$r/${service}/wanted-by/"
	system-control preset "${service}"
}

make_ip4() {
	local service
	service="static_ip4@$1"
	system-control convert-systemd-units $e "$r/" "./${service}.service"
	mkdir -p -m 0755 "$r/${service}/service/env"
	system-control disable "${service}"
	system-control set-service-env "$r/${service}" route "$2"
	rm -f -- "$r/${service}/log"
	ln -s -- "../../../sv/cyclog@network-interfaces" "$r/${service}/log"
	rm -f -- "$r/${service}/wants/network-runtime"
	ln -s -- "../../../../sv/network-runtime" "$r/${service}/wants/"
	rm -f -- "$r/${service}/after/network-runtime"
	ln -s -- "../../../../sv/network-runtime" "$r/${service}/after/"
	rm -f -- "$r/${service}/wants/static-networking"
	rm -f -- "$r/${service}/wanted-by/server"
	rm -f -- "$r/${service}/wanted-by/static-networking"
	ln -s -- "/etc/service-bundles/targets/static-networking" "$r/${service}/wanted-by/"
	system-control preset "${service}"
}

make_ip6() {
	local service
	service="static_ip6@$1"
	system-control convert-systemd-units $e "$r/" "./${service}.service"
	mkdir -p -m 0755 "$r/${service}/service/env"
	system-control disable "${service}"
	system-control set-service-env "$r/${service}" route "$2"
	rm -f -- "$r/${service}/log"
	ln -s -- "../../../sv/cyclog@network-interfaces" "$r/${service}/log"
	rm -f -- "$r/${service}/wants/network-runtime"
	ln -s -- "../../../../sv/network-runtime" "$r/${service}/wants/"
	rm -f -- "$r/${service}/after/network-runtime"
	ln -s -- "../../../../sv/network-runtime" "$r/${service}/after/"
	rm -f -- "$r/${service}/wants/static-networking"
	rm -f -- "$r/${service}/wanted-by/server"
	rm -f -- "$r/${service}/wanted-by/static-networking"
	ln -s -- "/etc/service-bundles/targets/static-networking" "$r/${service}/wanted-by/"
	system-control preset "${service}"
}

find "$r/" -maxdepth 1 -type d \( -name 'static_arp@*' -o -name 'static_ndp@*' -o -name 'static_ip4@*' -o -name 'static_ip6@*' \) |
while read -r n
do
	system-control disable "$n"
done

list_static_arp |
while read -r i
do
	echo >> "$3" static_arp_${i}
	test -n "$i" || continue
	pair="`get_var2 static_arp \"$i\"`"
	test -n "$pair" || continue
	make_arp "$i" "$pair"
done

list_static_ndp |
while read -r i
do
	echo >> "$3" static_ndp_${i}
	test -n "$i" || continue
	pair="`get_var2 static_ndp \"$i\"`"
	test -n "$pair" || continue
	make_ndp "$i" "$pair"
done

list_static_ip4 |
while read -r i
do
	echo >> "$3" static_ip4_${i}
	test -n "$i" || continue
	pair="`get_var2 route \"$i\"`"
	test -n "$pair" || continue
	make_ip4 "$i" "$pair"
done

if d="`get_var1 defaultrouter`"
then
	case "${d}" in
		[Nn][Oo]|'')
			;;
		*)
			make_ip4 "_default" "default ${d}"
			;;
	esac
fi

# Some IP6 static routes are pre-defined.
#  * IP4 mapped and compatible IP6 addresses are null-routed.
#  * Link-local unicast IP6 addresses are null-routed.
make_ip6 _v4mapped "::ffff:0.0.0.0 -prefixlen 96 ::1 -reject ${fib}"
make_ip6 _v4compat "::0.0.0.0 -prefixlen 96 ::1 -reject ${fib}"
make_ip6 _lla "fe80:: -prefixlen 10 ::1 -reject ${fib}"
make_ip6 _llma "ff02:: -prefixlen 16 ::1 -reject ${fib}"

list_static_ip6 |
while read -r i
do
	echo >> "$3" static_ip6_${i}
	test -n "$i" || continue
	pair="`get_var2 ipv6_route \"$i\"`"
	test -n "$pair" || continue
	make_ip6 "$i" "$pair"
done

if d="`get_var1 ipv6_defaultrouter`"
then
	case "${d}" in
		[Nn][Oo]|'')
			;;
		*)
			make_ip6 "_default" "default ${d}"
			;;
	esac
fi
