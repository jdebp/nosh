#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Special setup for static networking services
# This is invoked by all.do .
#

# These get us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf printenv "$1" ; }
get_var1() { read_rc "$1" || true ; }
get_var2() { read_rc "$1"_"$2" || true ; }
get_var3() { read_rc "$1"_"$2"_"$3" || read_rc "$1"_"$3" || true ; }
get_var4() { read_rc "$1"_"$2"_"$3" || read_rc "$1"_"$4" || true ; }
car() { echo "$1" ; }
cdr() { shift ; echo "$@" ; }
if_yes() { case "$1" in [Yy][Ee][Ss]|[Tt][Rr][Uu][Ee]|[Oo][Nn]|1) echo "$2" ;; esac ; }
if_no_or_empty() { case "$1" in [Nn][Oo]|[Ff][Aa][Ll][Ss][Ee]|[Oo][Ff][Ff]|0|'') echo "$2" ;; esac ; }
list_static_arp() { for i in `get_var1 static_arp_pairs` ; do printf "%s\n" "$i" ; done ; }
list_static_ndp() { for i in `get_var1 static_ndp_pairs` ; do printf "%s\n" "$i" ; done ; }
list_static_ip4() { for i in `get_var1 static_routes` ; do printf "%s\n" "$i" ; done ; }
list_static_ip6() { for i in `get_var1 ipv6_static_routes` ; do printf "%s\n" "$i" ; done ; }
list_natd_interfaces() { read_rc natd_interface || true ; }
iface_escape() { printf "%s\n" "$@" | tr './:+-' '_____' ; } # cave! The - must come last.
get_wlandebug() { read_rc wlandebug_"`iface_escape \"$1\"`" || read_rc wlandebug_DEFAULT ; }
get_create_args() { read_rc create_args_"`iface_escape \"$1\"`" || read_rc create_args_DEFAULT ; }
get_ifconfig1() { read_rc ifconfig_"`iface_escape \"$1\"`" || read_rc ifconfig_DEFAULT ; }
get_ifconfig2() { read_rc ifconfig_"`iface_escape \"$1\"`"_"$2" || read_rc ifconfig_DEFAULT_"$2" ; }
get_ipv6_prefix1() { read_rc ipv6_prefix_"`iface_escape \"$1\"`" || read_rc ipv6_prefix_DEFAULT ; }
get_children() { read_rc "$2"lans_"`iface_escape \"$1\"`" ; }
list_available_network_interfaces() { /bin/exec ifconfig -l ; }
list_network_interfaces() { 
	local n
	local c
	local i

	if ! n="`read_rc network_interfaces`"
	then
		n="`list_available_network_interfaces`"
	elif test _"auto" = _"$n" || test _"AUTO" = _"$n"
	then
		n="`list_available_network_interfaces`"
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
			epair[0-9]*)		printf "%s " "${i}a" "${i}b" ;;
			*)			printf "%s " "$i" ;;
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
			SYNCDHCP|NOSYNCDHCP|DHCP|WPA|HOSTAP|NOAUTO|IPV4LL)
				;;
			inet|inet4|inet6|link|ether|ipx|atalk|lladr)
				if test _"$i" = _"$n"
				then
					test -z "$o" || printf "\n"
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
	printf "\n"
}
get_filtered_ifconfig2() { 
	local i
	local c

	if ! c="`get_ifconfig1 "$1"`"
	then
		case "$1" in
		lo0|lo)	c="inet 127.0.0.1/8 inet6 ::1/128" ;;
		*)	c="" ;;
		esac
	fi

	filter_ifconfig "$2" $c
}
get_filtered_ifconfig3() {
	local c
	local a

	if ! c="`get_ifconfig2 "$1" "$2"`"
	then
		case "$2:$1" in
		aliases:lo0)	c="inet 127.0.0.1/8 inet6 ::1/128" ;;
		aliases:lo)	c="inet 127.0.0.1/8 inet6 ::1/128" ;;
		ipv6:*) 
			if a="`read_rc \"$2\"_activate_all_interfaces`" && 
			   test _"NO" = _"`if_no_or_empty "$a" NO`"
			then
				c="inet6 ifdisabled" 
			else
				c="inet6 -ifdisabled" 
			fi
			;;
		*)	c="" ;;
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
		printf "%s::/%s eui64\n" "$i" "$l"
		printf "%s::/%s anycast\n" "$i" "$l"
	done
}
get_link_opts() {
	get_filtered_ifconfig2 "$1" link
	get_filtered_ifconfig2 "$1" ether
	get_filtered_ifconfig2 "$1" lladr
}
get_inet4_opts() {
	is_ip_interface "$1" || return 0
	get_filtered_ifconfig2 "$1" inet
	get_filtered_ifconfig2 "$1" inet4
	get_filtered_ifconfig3 "$1" ipv4 inet
}
get_inet6_opts() {
	local n
	local r
	local i

	is_ip_interface "$1" || return 0
	case "`uname`" in
	*BSD)	
		if is_ipv6_default_interface "$1"
		then
			printf -- "%s " defaultif
		else
			printf -- "%s " -defaultif
		fi
		if n="`read_rc ipv6_cpe_wanif`"
		then
			r='no_radr'
			for i in ${n}
			do
				test _"$i" != _"$1" || r='-no_radr accept_rtadv'
			done
		else
			r=""
		fi
		printf -- "%s " $r
		;;
	*)	;;
	esac
	get_filtered_ifconfig2 "$1" inet6
	get_filtered_ifconfig3 "$1" ipv6 inet6
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

show_settings() {
	local i
	for i
	do
		system-control print-service-env "${i}" | sed -e 's/^/	/'
	done
}

make_arp() {
	local service
	service="static_arp@$1"
	system-control convert-systemd-units $e "$r/" "./${service}.service"
	install -d -m 0755 "$r/${service}/service/env"
	system-control set-service-env "$r/${service}" addr "`car $2`"
	system-control set-service-env "$r/${service}" dest "`cdr $2`"
	system-control preset "${service}"
	show_enable "${service}"
	show_settings "${service}"
	rm -f -- "$r/${service}/log"
	ln -s -- "../../../sv/cyclog@network-interfaces" "$r/${service}/log"
}

make_ndp() {
	local service
	service="static_ndp@$i"
	system-control convert-systemd-units $e "$r/" "./${service}.service"
	install -d -m 0755 "$r/${service}/service/env"
	system-control set-service-env "$r/${service}" addr "`car $pair`"
	system-control set-service-env "$r/${service}" dest "`cdr $pair`"
	system-control preset "${service}"
	show_enable "${service}"
	show_settings "${service}"
	rm -f -- "$r/${service}/log"
	ln -s -- "../../../sv/cyclog@network-interfaces" "$r/${service}/log"
}

make_ip4_route() {
	local service
	service="static_ip4@$1"
	system-control convert-systemd-units $e "$r/" "./${service}.service"
	install -d -m 0755 "$r/${service}/service/env"
	system-control set-service-env "$r/${service}" route "$2"
	system-control preset "${service}"
	show_enable "${service}"
	show_settings "${service}"
	rm -f -- "$r/${service}/log"
	ln -s -- "../../../sv/cyclog@network-interfaces" "$r/${service}/log"
}

make_ip6_route() {
	local service
	service="static_ip6@$1"
	system-control convert-systemd-units $e "$r/" "./${service}.service"
	install -d -m 0755 "$r/${service}/service/env"
	system-control set-service-env "$r/${service}" route "$2"
	system-control preset "${service}"
	show_enable "${service}"
	show_settings "${service}"
	rm -f -- "$r/${service}/log"
	ln -s -- "../../../sv/cyclog@network-interfaces" "$r/${service}/log"
}

make_netif() {
	local service
	service="netif@$1"
	system-control convert-systemd-units $e "$r/" "./${service}.service"
	install -d -m 0755 "$r/${service}/service/env"
	if get_ifconfig1 "$1" | grep -q -E '\<NOAUTO\>'
	then
		system-control disable "${service}"
	else
		system-control preset "${service}"
	fi
	show_enable "${service}"
	show_settings "${service}"
	rm -f -- "$r/${service}/log"
	ln -s -- "../../../sv/ifconfig-log" "$r/${service}/log"
	system-control preset ifconfig-log
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
	show_settings "${service}"
	rm -f -- "$r/${service}/log"
	ln -s -- "../../../sv/ifconfig-log" "$r/${service}/log"
	system-control preset ifconfig-log
}

make_ifconfig() {
	local service
	local m
	local c
	service="ifconfig@$1"
	rm -f -- "$r/${service}/wants/*"
	rm -f -- "$r/${service}/after/*"
	rm -f -- "$r/${service}/before/*"
	system-control convert-systemd-units $e "$r/" "./${service}.service"
	install -d -m 0755 "$r/${service}/service/env"
	system-control set-service-env "$r/${service}" create_args "${2:+$2 }`get_create_args "$1"`"
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
	for m in $3
	do
		rm -f -- "$r/${service}/wants/kmod@$m"
		rm -f -- "$r/${service}/after/kmod@$m"
		ln -s -- "../../../sv/kmod@$m" "$r/${service}/wants/"
		ln -s -- "../../../sv/kmod@$m" "$r/${service}/after/"
	done
	for c in $4
	do
		rm -f -- "$r/${service}/wants/ifconfig@$c"
		rm -f -- "$r/${service}/before/ifconfig@$c"
		rm -f -- "$r/${service}/wants/ifscript@$c"
		rm -f -- "$r/${service}/before/ifscript@$c"
		ln -s -- "../../ifconfig@$c" "$r/${service}/wants/"
		ln -s -- "../../ifconfig@$c" "$r/${service}/before/"
		ln -s -- "../../ifscript@$c" "$r/${service}/wants/"
		ln -s -- "../../ifscript@$c" "$r/${service}/before/"
	done
	show_enable "${service}"
	show_settings "${service}"
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
	show_settings "${service}"
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
	   get_ifconfig1 "$1" | grep -q -E '\<DHCP\>' &&
	   ! get_ifconfig1 "$1" | grep -q -E '\<NOAUTO\>'
	then
		system-control preset "${service}"
	else
		system-control disable "${service}"
	fi
	show_enable "${service}"
	show_settings "${service}"
	rm -f -- "$r/${service}/log"
	ln -s -- "../../../sv/dhclient-log" "$r/${service}/log"
	system-control preset dhclient-log
}

make_udhcpc() {
	local service
	service="udhcpc@$1"
	system-control convert-systemd-units $e "$r/" "./${service}.service"
	install -d -m 0755 "$r/${service}/service/env"
	if is_physical_interface "$1" &&
	   is_ip_interface "$1" &&
	   test 0 -lt "`expr \"${dhclient}\" : udhcpc`" &&
	   get_ifconfig1 "$1" | grep -q -E '\<DHCP\>' &&
	   ! get_ifconfig1 "$1" | grep -q -E '\<NOAUTO\>'
	then
		system-control preset "${service}"
	else
		system-control disable "${service}"
	fi
	show_enable "${service}"
	show_settings "${service}"
	rm -f -- "$r/${service}/log"
	ln -s -- "../../../sv/udhcpc-log" "$r/${service}/log"
	system-control preset udhcpc-log
}

make_dhcpcd() {
	local service
	service="dhcpcd@$1"
	system-control convert-systemd-units $e "$r/" "./${service}.service"
	install -d -m 0755 "$r/${service}/service/env"
	if is_physical_interface "$1" &&
	   is_ip_interface "$1" &&
	   test 0 -lt "`expr \"${dhclient}\" : dhcpcd`" &&
	   get_ifconfig1 "$1" | grep -q -E '\<DHCP\>' &&
	   ! get_ifconfig1 "$1" | grep -q -E '\<NOAUTO\>'
	then
		system-control preset "${service}"
	else
		system-control disable "${service}"
	fi
	show_enable "${service}"
	show_settings "${service}"
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
	show_settings "${service}"
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
	show_settings "${service}"
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
	show_settings "${service}"
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
	show_settings "${service}"
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
	show_settings "${service}"
	rm -f -- "$r/${service}/log"
	ln -s -- "../../../sv/rfcomm_pppd-log" "$r/${service}/log"
	system-control preset rfcomm_pppd-log
}

make_snort() {
	local service
	service="snort@$1"
	system-control convert-systemd-units $e "$r/" "./${service}.service"
	install -d -m 0755 "$r/${service}/service/env"
	if test _"${snort_enable}" = _"YES"
	then
		system-control preset "${service}"
	else
		system-control disable "${service}"
	fi
	show_enable "${service}"
	show_settings "${service}"
	rm -f -- "$r/${service}/log"
	ln -s -- "../../../sv/snort-log" "$r/${service}/log"
	system-control preset snort-log
}

make_avahi_autoipd() {
	local service
	service="avahi-autoipd@$1"
	system-control convert-systemd-units $e "$r/" "./${service}.service"
	install -d -m 0755 "$r/${service}/service/env"
	if is_physical_interface "$1" &&
	   is_ip_interface "$1" &&
	   test 0 -lt "`expr \"${autoipd}\" : avahi-autoipd`" &&
	   get_ifconfig1 "$1" | grep -q -E '\<IPV4LL\>' &&
	   ! get_ifconfig1 "$1" | grep -q -E '\<NOAUTO\>'
	then
		system-control preset "${service}"
	else
		system-control disable "${service}"
	fi
	show_enable "${service}"
	show_settings "${service}"
	rm -f -- "$r/${service}/log"
	ln -s -- "../../../sv/avahi-autoipd-log" "$r/${service}/log"
	system-control preset avahi-autoipd-log
}

# $1: interface name
# $2: extra create_args
# $3: kernel modules wants+after
# $4: netif wants+after
make_primary_services() {
	make_netif "$1"
	make_ifscript "$1"
	make_ifconfig "$1" "$2" "$3"
	make_natd "$1"
	make_hostap "$1"
	make_wpa "$1"
	make_dhclient "$1"
	make_udhcpc "$1"
	make_dhcpcd "$1"
	make_avahi_autoipd "$1"
}
 
make_wlandebug() {
	local service
	service="wlandebug@$i"
	system-control convert-systemd-units $e "$r/" "./${service}.service"
	install -d -m 0755 -- "$r/${service}/service/env"
	system-control preset "${service}"
	system-control set-service-env "${service}" flags "`get_wlandebug \"$i\"`"
	show_enable "${service}"
	show_settings "${service}"
	rm -f -- "$r/${service}/log"
	ln -s -- "../../../sv/wlandebug-log" "$r/${service}/log"
	system-control preset wlandebug-log
}

redo-ifchange rc.conf general-services "netif@.service" "static_arp@.service" "static_ndp@.service" "static_ip4@.service" "static_ip6@.service" "natd@.service" "hostapd@.service" "dhclient@.service" "udhcpc@.service" "dhcpcd@.service" "wpa_supplicant@.service" "rfcomm_pppd@.service" "ppp@.service" "spppcontrol@.service" "avahi-autoipd@.service" "ifscript@.service" "ifconfig@.service" "snort@.service" "wlandebug@.service"

r="/var/local/sv"
e="--no-systemd-quirks --escape-instance --local-bundle --bundle-root"
if dhclient="`read_rc dhclient_program`" && test -n "${dhclient}"
then
	dhclient="`basename \"${dhclient}\"`"
else
	dhclient="dhclient"
fi
if autoipd="`read_rc autoipd_program`" && test -n "${autoipd}"
then
	autoipd="`basename \"${autoipd}\"`"
else
	autoipd="avahi-autoipd"
fi


find "$r/" -maxdepth 1 -type d \( -name 'static_arp@*' -o -name 'static_ndp@*' -o -name 'static_ip4@*' -o -name 'static_ip6@*' -o -name 'netif@*' -o -name 'natd@*' -o -name 'hostapd@*' -o -name 'wpa_supplicant@*' -o -name 'dhclient@*' -o -name 'dhcpcd@*' -o -name 'ppp@*' -o -name 'spppcontrol@*' -o -name 'rfcomm_pppd@*' -o -name 'snort@*' -o -name 'avahi-autoipd@*' -o -name 'wlandebug@*' \) -print0 |
xargs -0 system-control disable

find "$r/" -maxdepth 1 -type d \( -name 'ifconfig@*' -o -name 'ifscript@*' -o -name 'natd@*' -o -name 'hostapd@*' -o -name 'wpa_supplicant@*' -o -name 'dhclient@*' -o -name 'dhcpcd@*' -o -name 'ppp@*' -o -name 'spppcontrol@*' -o -name 'rfcomm_pppd@*' -o -name 'snort@*' -o -name 'avahi-autoipd@*' -o -name 'wlandebug@*' \) -exec rm -f -- {}/wanted-by/static-networking {}/wanted-by/workstation \;

system-control disable ifconfig-log natd-log dhclient-log dhcpcd-log snort-log rfcomm_pppd-log ppp-log sppp-log wlandebug-log avahi-autoipd-log

if rfcomm_pppd_server_enable="`read_rc rfcomm_pppd_server_enable`"
then
	rfcomm_pppd_server_enable="`if_yes \"${rfcomm_pppd_server_enable}\" YES`"
fi
if ppp_server_enable="`read_rc ppp_server_enable`"
then
	ppp_server_enable="`if_yes \"${ppp_server_enable}\" YES`"
fi
if snort_enable="`read_rc snort_enable`"
then
	snort_enable="`if_yes \"${snort_enable}\" YES`"
fi

list_static_arp |
while read -r i
do
	test -n "$i" || continue
	pair="`get_var2 static_arp \"$i\"`"
	test -n "$pair" || continue
	make_arp "$i" "$pair" >> "$3"
done

list_static_ndp |
while read -r i
do
	test -n "$i" || continue
	pair="`get_var2 static_ndp \"$i\"`"
	test -n "$pair" || continue
	make_ndp "$i" "$pair" >> "$3"
done

list_static_ip4 |
while read -r i
do
	test -n "$i" || continue
	pair="`get_var2 route \"$i\"`"
	test -n "$pair" || continue
	make_ip4_route "$i" "$pair" >> "$3"
done

if d="`get_var1 defaultrouter`"
then
	case "${d}" in
		[Nn][Oo]|'')
			;;
		*)
			make_ip4_route "_default" "default ${d}" >> "$3"
			;;
	esac
fi

# Some static routes are pre-defined.
#  * IP4 mapped and compatible IP6 addresses are null-routed.
#  * Interface-local multicast IP6 addresses are null-routed.
#  * Link-local unicast and multicast IP6 addresses are null-routed.
#  * 6to4 mappings of interface-local multicast IP4 addresses are null-routed.
#  * 6to4 mappings of link-local unicast and multicast IP4 addresses are null-routed.
#  * interface-local IP4 addresses are null-routed.
#  * link-local unicast and multicast IP4 addresses are null-routed.
make_ip6_route _v4mapped '::ffff:0.0.0.0 -prefixlen 96 ::1 -reject' >> "$3"
make_ip6_route _v4compat '::0.0.0.0 -prefixlen 96 ::1 -reject' >> "$3"
make_ip6_route _lla 'fe80:: -prefixlen 10 ::1 -reject' >> "$3"
make_ip6_route _ilma 'ff01:: -prefixlen 16 ::1 -reject' >> "$3"
make_ip6_route _llma 'ff02:: -prefixlen 16 ::1 -reject' >> "$3"
make_ip6_route _6to4lla '2002:A9FE:: -prefixlen 32 ::1 -reject' >> "$3"
make_ip6_route _6to4ilma '2002:7F00:: -prefixlen 24 ::1 -reject' >> "$3"
make_ip6_route _6to4llma '2002:e000:: -prefixlen 20 ::1 -reject' >> "$3"
make_ip6_route _6to4cna '2002:0000:: -prefixlen 24 ::1 -reject' >> "$3"
make_ip4_route _lla '169.254.0.0 -prefixlen 16 127.0.0.1 -reject' >> "$3"
make_ip4_route _ilma '127.0.0.0 -prefixlen 8 127.0.0.1 -reject' >> "$3"
make_ip4_route _llma '224.0.0.0 -prefixlen 4 127.0.0.1 -reject' >> "$3"

list_static_ip6 |
while read -r i
do
	test -n "$i" || continue
	pair="`get_var2 ipv6_route \"$i\"`"
	test -n "$pair" || continue
	make_ip6_route "$i" "$pair" >> "$3"
done

if d="`get_var1 ipv6_defaultrouter`"
then
	case "${d}" in
		[Nn][Oo]|'')
			;;
		*)
			make_ip6_route "_default" "default ${d}" >> "$3"
			;;
	esac
fi

for i in `list_network_interfaces`
do
	test -n "$i" || continue
	if w="`get_children \"$i\" w`"
	then
		for j in $w
		do
			test -n "$j" || continue
			make_primary_services "$j" "wlandev \"$i\"" "" "" >> "$3"
			make_wlandebug "$j" >> "$3"
		done
	fi
	if v="`get_children \"$i\" v`"
	then
		for j in $v
		do
			test -n "$j" || continue
			if expr "$j" : '^[[:digit:]]*$' >/dev/null
			then
				j="$i.$j"
			fi
			make_primary_services "$j" "vlandev \"$i\"" "if_vlan" "" >> "$3"
		done
	fi
	make_primary_services "$i" "" "" "${w:+$w }${v:+$v }" >> "$3"
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

for i in `get_var2 snort interfaces`
do
	test -z "$i" && continue
	make_snort "$i" >> "$3"
done
