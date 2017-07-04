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
get_var3() { read_rc "$1"_"$2"_"$3" || read_rc "$1"_"$3" || true ; }
get_var4() { read_rc "$1"_"$2"_"$3" || read_rc "$1"_"$4" || true ; }
car() { echo "$1" ; }
cdr() { shift ; echo "$@" ; }
list_static_arp() { for i in `get_var1 static_arp_pairs` ; do printf "%s\n" "$i" ; done ; }
list_static_ndp() { for i in `get_var1 static_ndp_pairs` ; do printf "%s\n" "$i" ; done ; }
list_static_ip4() { for i in `get_var1 static_routes` ; do printf "%s\n" "$i" ; done ; }
list_static_ip6() { for i in `get_var1 ipv6_static_routes` ; do printf "%s\n" "$i" ; done ; }
list_natd_interfaces() { read_rc natd_interface || true ; }
get_ifconfig1() { read_rc ifconfig_"$1" || read_rc ifconfig_DEFAULT ; }
get_ifconfig2() { read_rc ifconfig_"$1"_"$2" || read_rc ifconfig_DEFAULT_"$2" ; }
get_ipv6_prefix1() { read_rc ipv6_prefix_"$1" || read_rc ipv6_prefix_DEFAULT ; }
list_auto_network_interfaces() {
	case "`uname`" in
	Linux)	/bin/ls /sys/class/net ;;
	*)	ifconfig -l ;;
	esac
}
list_network_interfaces() { 
	local n
	local c
	local i

	if ! n="`read_rc network_interfaces`"
	then
		n="`list_auto_network_interfaces`"
	elif test _"auto" = _"$n" || test _"AUTO" = _"$n"
	then
		n="`list_auto_network_interfaces`"
	fi
	c="`read_rc cloned_interfaces`" || :

	# This is "effectively mandatory" and must be first.
	case "`uname`" in
	Linux)	echo lo ;;
	*)	echo lo0 ;;
	esac

	for i in $n $c
	do
		case "$i" in
			lo0|lo)			;;
			epair[0-9]*[ab])	;;
			epair[0-9]*)		echo "${i}a" "${i}b" ;;
			*)			echo "$i" ;;
		esac
	done
}
is_physical_interface() {
	case "$1" in
		lo[0-9]*|lo)	return 1 ;;
		faith[0-9]*)	return 1 ;;
		stf[0-9]*)	return 1 ;;
		lp[0-9]*)	return 1 ;;
		sl[0-9]*)	return 1 ;;
	esac
	return 0
}
is_ip_interface() {
	case "$1" in
		pflog[0-9]*)	return 1 ;;
		pfsync[0-9]*)	return 1 ;;
		usbus[0-9]*)	return 1 ;;
		an[0-9]*)	return 1 ;;
		ath[0-9]*)	return 1 ;;
		ipw[0-9]*)	return 1 ;;
		ipfw[0-9]*)	return 1 ;;
		iwi[0-9]*)	return 1 ;;
		iwn[0-9]*)	return 1 ;;
		ral[0-9]*)	return 1 ;;
		wi[0-9]*)	return 1 ;;
		wl[0-9]*)	return 1 ;;
		wpi[0-9]*)	return 1 ;;
	esac
	return 0
}
is_ipv6_default_interface() {
	local n

	if n="`read_rc ipv6_default_interface`"
	then
		test _"$n" != _"$1" || return 0
	fi
	return 1
}
is_natd_interface() {
	for i in `list_natd_interfaces`
	do
		test _"$i" != _"$1" || return 0
	done
	return 1
}
is_ip4_address() { echo "$1" | grep -E -q '^[0-9]+(\.[0-9]+){0,3}$' ; }
filter_ifconfig() { 
	local i
	local n
	local o

	n="$1"
	shift

	o=""
	for i
	do
		case "$i" in
			SYNCDHCP|NOSYNCDHCP|DHCP|WPA|HOSTAP|NOAUTO)
				;;
			inet|inet4|inet6|link|ether|ipx|atalk|lladr)
				if test _"$i" = _"$n"
				then
					o="$i"
				else
					o=""
				fi
				;;
			*)
				test -z "$o" || printf "%s " "$i"
			       	;;
		esac
	done
	test -z "$o" || printf "\n"
}
get_filtered_ifconfig2() { 
	local i
	local c

	if ! c="`get_ifconfig1 "$1"`"
	then
		case "$1" in
		lo0)	c="inet 127.0.0.1 inet6 -ifdisabled" ;;
		*)	c="inet6 -ifdisabled" ;;
		esac
	fi

	filter_ifconfig "$2" $c
}
get_filtered_ifconfig3() {
	local i
	local c

	if ! c="`get_ifconfig2 "$1" "$2"`"
	then
		case "$2:$1" in
		aliases:lo0)	c="inet 127.0.0.1/8" ;;
		*)		c="" ;;
		esac
	fi

	filter_ifconfig "$3" $c
}
get_ipv6_prefix() {
	local i
	local l

	for i in `get_ipv6_prefix1 "$1" || true`
	do
		case "$i" in
			*/*)	l="${i#*/}" ; i="${i%/*}" ;;
			*)	l="64" ;;
		esac
		i="${i%::*}"
		i="${i%:}"
		printf "%s:: prefixlen %s eui64\n" "$i" "$l"
		printf "%s:: prefixlen %s anycast\n" "$i" "$l"
	done
}
get_link_opts() {
	get_filtered_ifconfig2 "$1" link
	get_filtered_ifconfig2 "$1" ether
	get_filtered_ifconfig2 "$1" lladr
}
get_inet4_opts() {
	get_filtered_ifconfig2 "$1" inet
	get_filtered_ifconfig2 "$1" inet4
	get_filtered_ifconfig3 "$1" ipv4 inet
}
get_inet6_opts() {
	get_filtered_ifconfig2 "$1" inet6
	get_filtered_ifconfig3 "$1" ipv6 inet6
	if is_ipv6_default_interface "$1"
	then
		printf -- "%s " defaultif
	else
		printf -- "%s " -defaultif
	fi
}
get_link_aliases() {
	get_filtered_ifconfig3 "$1" aliases link
	get_filtered_ifconfig3 "$1" aliases ether
	get_filtered_ifconfig3 "$1" aliases lladr
}
get_inet4_aliases() {
	get_filtered_ifconfig3 "$1" aliases inet
}
get_inet6_aliases() {
	get_filtered_ifconfig3 "$1" aliases inet6
	get_ipv6_prefix "$1"
}
if_yes() { case "$1" in [Yy][Ee][Ss]|[Tt][Rr][Uu][Ee]|[Oo][Nn]|1) echo "$2" ;; esac ; }

redo-ifchange rc.conf general-services "static_arp@.service" "static_ndp@.service" "static_ip4@.service" "static_ip6@.service" "natd@.service" "hostapd@.service" "dhclient@.service" "dhcpcd@.service" "wpa_supplicant@.service" "rfcomm_pppd@.service" "ppp@.service" "spppcontrol@.service" "ifscript@.service" "ifconfig@.service"

r="/var/local/sv"
e="--no-systemd-quirks --escape-instance --local-bundle --bundle-root"
if dhclient="`read_rc dhclient_program`" && test -n "${dhclient}"
then
	dhclient="`basename \"${dhclient}\"`"
else
	dhclient="dhclient"
fi

show_enable() {
	local i
	for i
	do
		if system-control is-enabled "$i"
		then
			echo on "$i"
		else
			echo off "$i"
		fi
	done
}

make_arp() {
	local service
	service="static_arp@$1"
	system-control convert-systemd-units $e "$r/" "./${service}.service"
	install -d -m 0755 "$r/${service}/service/env"
	system-control set-service-env "$r/${service}" addr "`car $2`"
	system-control set-service-env "$r/${service}" dest "`cdr $2`"
	rm -f -- "$r/${service}/log"
	ln -s -- "../../../sv/cyclog@network-interfaces" "$r/${service}/log"
	system-control preset "${service}"
}

make_ndp() {
	local service
	service="static_ndp@$i"
	system-control convert-systemd-units $e "$r/" "./${service}.service"
	install -d -m 0755 "$r/${service}/service/env"
	system-control set-service-env "$r/${service}" addr "`car $pair`"
	system-control set-service-env "$r/${service}" dest "`cdr $pair`"
	rm -f -- "$r/${service}/log"
	ln -s -- "../../../sv/cyclog@network-interfaces" "$r/${service}/log"
	system-control preset "${service}"
}

make_ip4_route() {
	local service
	service="static_ip4@$1"
	system-control convert-systemd-units $e "$r/" "./${service}.service"
	install -d -m 0755 "$r/${service}/service/env"
	system-control set-service-env "$r/${service}" route "$2"
	rm -f -- "$r/${service}/log"
	ln -s -- "../../../sv/cyclog@network-interfaces" "$r/${service}/log"
	system-control preset "${service}"
}

make_ip6_route() {
	local service
	service="static_ip6@$1"
	system-control convert-systemd-units $e "$r/" "./${service}.service"
	install -d -m 0755 "$r/${service}/service/env"
	system-control set-service-env "$r/${service}" route "$2"
	rm -f -- "$r/${service}/log"
	ln -s -- "../../../sv/cyclog@network-interfaces" "$r/${service}/log"
	system-control preset "${service}"
}

make_ifscript() {
	local service
	service="ifscript@$1"
	system-control convert-systemd-units $e "$r/" "./${service}.service"
	install -d -m 0755 "$r/${service}/service/env"
	if get_ifconfig1 "$1" | grep -q -E '\<NOAUTO\>'
	then
		system-control disable "${service}"
	else
		system-control preset "${service}"
	fi
	show_enable "${service}"
	rm -f -- "$r/${service}/log"
	ln -s -- "../../../sv/ifconfig-log" "$r/${service}/log"
	system-control preset ifconfig-log
}

make_ifconfig() {
	local service
	service="ifconfig@$1"
	system-control convert-systemd-units $e "$r/" "./${service}.service"
	install -d -m 0755 "$r/${service}/service/env"
	system-control set-service-env "$r/${service}" link_opts "`get_link_opts "$1"`"
	system-control set-service-env "$r/${service}" inet4_opts "`get_inet4_opts "$1"`"
	system-control set-service-env "$r/${service}" inet6_opts "`get_inet6_opts "$1"`"
	system-control set-service-env "$r/${service}" link_aliases "`get_link_aliases "$1"`"
	system-control set-service-env "$r/${service}" inet4_aliases "`get_inet4_aliases "$1"`"
	system-control set-service-env "$r/${service}" inet6_aliases "`get_inet6_aliases "$1"`"
	if get_ifconfig1 "$1" | grep -q -E '\<NOAUTO\>'
	then
		system-control disable "${service}"
	else
		system-control preset "${service}"
	fi
	show_enable "${service}"
	rm -f -- "$r/${service}/log"
	ln -s -- "../../../sv/ifconfig-log" "$r/${service}/log"
	system-control preset ifconfig-log
}

make_natd() {
	local service
	service="natd@$1"
	system-control convert-systemd-units $e "$r/" "./${service}.service"
	install -d -m 0755 "$r/${service}/service/env"
	system-control disable "${service}"
	if is_ip4_address "$1" 
	then
		system-control set-service-env "${service}" type "-interface"
	else
		system-control set-service-env "${service}" type "-alias"
	fi
	if get_ifconfig1 "$1" | grep -q -E '\<(SYNC|NOSYNC)DHCP\>'
	then
		system-control set-service-env "${service}" dynamic "-dynamic"
	else
		system-control set-service-env "${service}" dynamic ""
	fi
	if get_ifconfig1 "$1" | grep -q -E '\<NAT\>' || 
	   is_natd_interface "$1"
	then
		system-control preset "${service}"
	else
		system-control disable "${service}"
	fi
	show_enable "${service}"
	rm -f -- "$r/${service}/log"
	ln -s -- "../../../sv/natd-log" "$r/${service}/log"
	system-control preset natd-log
}

make_dhclient() {
	local service
	service="dhclient@$1"
	system-control convert-systemd-units $e "$r/" "./${service}.service"
	install -d -m 0755 "$r/${service}/service/env"
	if is_physical_interface "$1" &&
	   is_ip_interface "$1" &&
	   test 0 -lt "`expr \"${dhclient}\" : dhclient`" &&
	   get_ifconfig1 "$1" | grep -q -E '\<DHCP\>'
	then
		system-control preset "${service}"
	else
		system-control disable "${service}"
	fi
	show_enable "${service}"
	rm -f -- "$r/${service}/log"
	ln -s -- "../../../sv/dhclient-log" "$r/${service}/log"
	system-control preset dhclient-log
}

make_dhcpcd() {
	local service
	service="dhcpcd@$1"
	system-control convert-systemd-units $e "$r/" "./${service}.service"
	install -d -m 0755 "$r/${service}/service/env"
	if is_physical_interface "$1" &&
	   is_ip_interface "$1" &&
	   test 0 -lt "`expr \"${dhclient}\" : dhcpcd`" &&
	   get_ifconfig1 "$1" | grep -q -E '\<DHCP\>'
	then
		system-control preset "${service}"
	else
		system-control disable "${service}"
	fi
	show_enable "${service}"
	rm -f -- "$r/${service}/log"
	ln -s -- "../../../sv/dhcpcd-log" "$r/${service}/log"
	system-control preset dhcpcd-log
}

make_hostap() {
	local service
	service="hostapd@$1"
	system-control convert-systemd-units $e "$r/" "./${service}.service"
	install -d -m 0755 "$r/${service}/service/env"
	if get_ifconfig1 "$1" | grep -q -E '\<HOSTAP\>'
	then
		system-control preset "${service}"
	else
		system-control disable "${service}"
	fi
	show_enable "${service}"
	rm -f -- "$r/${service}/log"
	ln -s -- "../../../sv/cyclog@hostapd" "$r/${service}/log"
	system-control preset cyclog@hostapd
}

make_wpa() {
	local service
	service="wpa_supplicant@$1"
	system-control convert-systemd-units $e "$r/" "./${service}.service"
	install -d -m 0755 "$r/${service}/service/env"
	if get_ifconfig1 "$1" | grep -q -E '\<WPA\>'
	then
		system-control preset "${service}"
	else
		system-control disable "${service}"
	fi
	show_enable "${service}"
	rm -f -- "$r/${service}/log"
	ln -s -- "../../../sv/cyclog@wpa_supplicant" "$r/${service}/log"
	system-control preset cyclog@wpa_supplicant
}

make_ppp() {
	local service
	service="ppp@$i"
	system-control convert-systemd-units $e "$r/" "./${service}.service"
	install -d -m 0755 -- "$r/${service}/service/env"
	if test _"${ppp_server_enable}" = _"YES"
	then
		system-control preset "${service}"
	else
		system-control disable "${service}"
	fi
	system-control set-service-env "${service}" mode "`get_var3 ppp \"$i\" mode`"
	system-control set-service-env "${service}" nat "`get_var3 ppp \"$i\" nat`"
	system-control set-service-env "${service}" unit "`get_var3 ppp \"$i\" unit`"
	show_enable "${service}"
	system-control print-service-env "${service}"
	rm -f -- "$r/${service}/log"
	ln -s -- "../../../sv/ppp-log" "$r/${service}/log"
	system-control preset ppp-log
}

make_sppp() {
	local service
	service="spppcontrol@$i"
	system-control convert-systemd-units $e "$r/" "./${service}.service"
	install -d -m 0755 -- "$r/${service}/service/env"
	system-control preset "${service}"
	system-control set-service-env "${service}" args "`get_var2 spppconfig \"$i\"`"
	show_enable "${service}"
	system-control print-service-env "${service}"
	rm -f -- "$r/${service}/log"
	ln -s -- "../../sv/sppp-log" "$r/${service}/log"
	system-control preset sppp-log
}

make_rfcomm_pppd() {
	local service
	service="rfcomm_pppd@$i"
	system-control convert-systemd-units $e "$r/" "./${service}.service"
	install -d -m 0755 -- "$r/${service}/service/env"
	if test _"${rfcomm_pppd_server_enable}" = _"YES"
	then
		system-control preset "${service}"
	else
		system-control disable "${service}"
	fi
	system-control set-service-env "${service}" dun "`get_var3 rfcomm_pppd_server \"$i\" register_dun`"
	system-control set-service-env "${service}" sp "`get_var3 rfcomm_pppd_server \"$i\" register_sp`"
	system-control set-service-env "${service}" channel "`get_var3 rfcomm_pppd_server \"$i\" channel`"
	system-control set-service-env "${service}" addr "`get_var3 rfcomm_pppd_server \"$i\" bdaddr`"
	show_enable "${service}"
	system-control print-service-env "${service}"
	rm -f -- "$r/${service}/log"
	ln -s -- "../../../sv/rfcomm_pppd-log" "$r/${service}/log"
	system-control preset rfcomm_pppd-log
}

find "$r/" -maxdepth 1 -type d \( -name 'static_arp@*' -o -name 'static_ndp@*' -o -name 'static_ip4@*' -o -name 'static_ip6@*' -o -name 'natd@*' -o -name 'hostapd@*' -o -name 'wpa_supplicant@*' -o -name 'dhclient@*' -o -name 'dhcpcd@*' -o -name 'ppp@*' -o -name 'spppcontrol@*' -o -name 'rfcomm_pppd@*' \) -print0 |
xargs -0 system-control disable

system-control disable ifconfig-log natd-log dhclient-log dhcpcd-log natd-log rfcomm_pppd-log ppp-log sppp-log

if rfcomm_pppd_server_enable="`read_rc rfcomm_pppd_server_enable`"
then
	rfcomm_pppd_server_enable="`if_yes \"${rfcomm_pppd_server_enable}\" YES`"
fi
if ppp_server_enable="`read_rc ppp_server_enable`"
then
	ppp_server_enable="`if_yes \"${ppp_server_enable}\" YES`"
fi

list_static_arp |
while read -r i
do
	echo >> "$3" static_arp@${i}
	test -n "$i" || continue
	pair="`get_var2 static_arp \"$i\"`"
	test -n "$pair" || continue
	make_arp "$i" "$pair"
done

list_static_ndp |
while read -r i
do
	echo >> "$3" static_ndp@${i}
	test -n "$i" || continue
	pair="`get_var2 static_ndp \"$i\"`"
	test -n "$pair" || continue
	make_ndp "$i" "$pair"
done

list_static_ip4 |
while read -r i
do
	echo >> "$3" static_ip4@${i}
	test -n "$i" || continue
	pair="`get_var2 route \"$i\"`"
	test -n "$pair" || continue
	make_ip4_route "$i" "$pair"
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
#  * Link-local unicast and multicast IP6 addresses are null-routed.
make_ip6_route _v4mapped '::ffff:0.0.0.0 -prefixlen 96 ::1 -reject'
make_ip6_route _v4compat '::0.0.0.0 -prefixlen 96 ::1 -reject'
make_ip6_route _lla 'fe80:: -prefixlen 10 ::1 -reject'
make_ip6_route _llma 'ff02:: -prefixlen 16 ::1 -reject'

list_static_ip6 |
while read -r i
do
	echo >> "$3" static_ip6@${i}
	test -n "$i" || continue
	pair="`get_var2 ipv6_route \"$i\"`"
	test -n "$pair" || continue
	make_ip6_route "$i" "$pair"
done

if d="`get_var1 ipv6_defaultrouter`"
then
	case "${d}" in
		[Nn][Oo]|'')
			;;
		*)
			make_ip6_route "_default" "default ${d}"
			;;
	esac
fi

for i in `list_network_interfaces`
do
	test -n "$i" || continue
	make_ifscript "$i" >> "$3"
	make_ifconfig "$i" >> "$3"
	make_natd "$i" >> "$3"
	make_hostap "$i" >> "$3"
	make_wpa "$i" >> "$3"
	make_dhclient "$i" >> "$3"
	make_dhcpcd "$i" >> "$3"
done

for i in `get_var2 ppp profile`
do
	test -z "$i" && continue
	make_ppp "$i" >> "$3"
done

for i in `get_var2 sppp interfaces`
do
	test -z "$i" && continue
	make_sppp "$i" >> "$3"
done

for i in `get_var2 rfcomm_pppd_server profile`
do
	test -z "$i" && continue
	make_rfcomm_pppd "$i" >> "$3"
done
