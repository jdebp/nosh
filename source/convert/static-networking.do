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
car() { echo "$1" ; }
cdr() { shift ; echo "$@" ; }
list_static_arp() { for i in `get_var1 static_arp_pairs` ; do echo "$i" ; done ; }
list_static_ndp() { for i in `get_var1 static_ndp_pairs` ; do echo "$i" ; done ; }
list_static_ip4() { for i in `get_var1 static_routes` ; do echo "$i" ; done ; }
list_static_ip6() { for i in `get_var1 ipv6_static_routes` ; do echo "$i" ; done ; }
list_natd_interfaces() { read_rc natd_interface || true ; }
list_auto_network_interfaces() {
	case "`uname`" in
	Linux)	/bin/ls /sys/class/net ;;
	*)	ifconfig -l ;;
	esac
}
list_network_interfaces() { 
	local s
	if ! s="`read_rc network_interfaces`"
	then
		list_auto_network_interfaces
	elif test "auto" = "$s" || test "AUTO" = "$s"
	then
		list_auto_network_interfaces
	else
		echo "$s"
	fi
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
is_natd_interface() {
	for i in `list_natd_interfaces`
	do
		test _"$i" -ne _"$1" || return 0
	done
	return 1
}
is_ip4_address() { echo "$1" | grep -E -q '^[0-9]+(\.[0-9]+){0,3}$' ; }
get_ifconfig() { read_rc ifconfig_"$1" || read_rc ifconfig_DEFAULT || true ; }

redo-ifchange rc.conf general-services "static_arp@.service" "static_ndp@.service" "static_ip4@.service" "static_ip6@.service" "natd@.service" "hostapd@.service" "dhclient@.service" "wpa_supplicant@.service" "rfcomm_pppd@.service" "ppp@.service" "spppcontrol@.service"

r="/var/local/sv"
e="--no-systemd-quirks --escape-instance --local-bundle --bundle-root"

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
	if get_ifconfig "$1" | grep -q -E '\<NOAUTO\>'
	then
		system-control disable "${service}"
	else
		system-control preset "${service}"
	fi
	if system-control is-enabled "${service}"
	then
		echo on "${service}"
	else
		echo off "${service}"
	fi
	rm -f -- "$r/${service}/log"
	ln -s -- "../../../sv/ifconfig-log" "$r/${service}/log"
	system-control preset ifconfig-log
}

make_ifconfig() {
	local service
	service="ifconfig@$1"
	system-control convert-systemd-units $e "$r/" "./${service}.service"
	install -d -m 0755 "$r/${service}/service/env"
	if get_ifconfig "$1" | grep -q -E '\<NOAUTO\>'
	then
		system-control disable "${service}"
	else
		system-control preset "${service}"
	fi
	if system-control is-enabled "${service}"
	then
		echo on "${service}"
	else
		echo off "${service}"
	fi
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
	if get_ifconfig "$1" | grep -q -E '\<(SYNC|NOSYNC)DHCP\>'
	then
		system-control set-service-env "${service}" dynamic "-dynamic"
	else
		system-control set-service-env "${service}" dynamic ""
	fi
	if get_ifconfig "$1" | grep -q -E '\<NAT\>' || 
	   is_natd_interface "$1"
	then
		system-control preset "${service}"
	else
		system-control disable "${service}"
	fi
	if system-control is-enabled "${service}"
	then
		echo on "${service}"
	else
		echo off "${service}"
	fi
	rm -f -- "$r/${service}/log"
	ln -s -- "../../../sv/natd-log" "$r/${service}/log"
	system-control preset natd-log
}

make_dhclient() {
	local service
	service="dhclient@$1"
	system-control convert-systemd-units $e "$r/" "./${service}.service"
	install -d -m 0755 "$r/${service}/service/env"
	if get_ifconfig "$1" | grep -q -E '\<DHCP\>'
	then
		system-control preset "${service}"
	else
		system-control disable "${service}"
	fi
	if system-control is-enabled "${service}"
	then
		echo on "${service}"
	else
		echo off "${service}"
	fi
	rm -f -- "$r/${service}/log"
	ln -s -- "../../../sv/dhclient-log" "$r/${service}/log"
	system-control preset dhclient-log
}

make_hostap() {
	local service
	service="hostapd@$1"
	system-control convert-systemd-units $e "$r/" "./${service}.service"
	install -d -m 0755 "$r/${service}/service/env"
	if get_ifconfig "$1" | grep -q -E '\<HOSTAP\>'
	then
		system-control preset "${service}"
	else
		system-control disable "${service}"
	fi
	if system-control is-enabled "${service}"
	then
		echo on "${service}"
	else
		echo off "${service}"
	fi
	rm -f -- "$r/${service}/log"
	ln -s -- "../../../sv/cyclog@hostapd" "$r/${service}/log"
	system-control preset cyclog@hostapd
}

make_wpa() {
	local service
	service="wpa_supplicant@$1"
	system-control convert-systemd-units $e "$r/" "./${service}.service"
	install -d -m 0755 "$r/${service}/service/env"
	if get_ifconfig "$1" | grep -q -E '\<WPA\>'
	then
		system-control preset "${service}"
	else
		system-control disable "${service}"
	fi
	if system-control is-enabled "${service}"
	then
		echo on "${service}"
	else
		echo off "${service}"
	fi
	rm -f -- "$r/${service}/log"
	ln -s -- "../../../sv/cyclog@wpa_supplicant" "$r/${service}/log"
	system-control preset cyclog@wpa_supplicant
}

make_ppp() {
	local service
	service="ppp@$i"
	system-control convert-systemd-units $e "$r/" "./${service}.service"
	install -d -m 0755 -- "$r/${service}/service/env"
	system-control preset "${service}"
	system-control set-service-env "${service}" mode "`get_var3 ppp \"$i\" mode`"
	system-control set-service-env "${service}" nat "`get_var3 ppp \"$i\" nat`"
	system-control set-service-env "${service}" unit "`get_var3 ppp \"$i\" unit`"
	if system-control is-enabled "${service}"
	then
		echo on "${service}"
	else
		echo off "${service}"
	fi
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
	if system-control is-enabled "${service}"
	then
		echo on "${service}"
	else
		echo off "${service}"
	fi
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
	system-control preset "${service}"
	system-control set-service-env "${service}" dun "`get_var3 rfcomm_pppd_server \"$i\" register_dun`"
	system-control set-service-env "${service}" sp "`get_var3 rfcomm_pppd_server \"$i\" register_sp`"
	system-control set-service-env "${service}" channel "`get_var3 rfcomm_pppd_server \"$i\" channel`"
	system-control set-service-env "${service}" addr "`get_var3 rfcomm_pppd_server \"$i\" bdaddr`"
	if system-control is-enabled "${service}"
	then
		echo on "${service}"
	else
		echo off "${service}"
	fi
	system-control print-service-env "${service}"
	rm -f -- "$r/${service}/log"
	ln -s -- "../../../sv/rfcomm_pppd-log" "$r/${service}/log"
	system-control preset rfcomm_pppd-log
}

find "$r/" -maxdepth 1 -type d \( -name 'static_arp@*' -o -name 'static_ndp@*' -o -name 'static_ip4@*' -o -name 'static_ip6@*' -o -name 'natd@*' -o -name 'hostapd@*' -o -name 'wpa_supplicant@*' -o -name 'dhclient@*' -o -name 'ppp@*' -o -name 'spppcontrol@*' -o -name 'rfcomm_pppd@*' \) -print0 |
xargs -0 system-control disable

system-control disable natd-log dhclient-log rfcomm_pppd-log ppp-log sppp-log

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
#  * Link-local unicast IP6 addresses are null-routed.
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
	is_physical_interface "$i" || continue
	make_ifscript "$i" >> "$3"
	make_ifconfig "$i" >> "$3"
	make_natd "$i" >> "$3"
	make_hostap "$i" >> "$3"
	make_wpa "$i" >> "$3"
	make_dhclient "$i" >> "$3"
done

get_var2 ppp profile |
while read -r i
do
	test -z "$i" && continue
	make_ppp "$i" >> "$3"
done

get_var2 sppp interfaces |
while read -r i
do
	test -z "$i" && continue
	make_sppp "$i" >> "$3"
done

get_var2 rfcomm_pppd_server profile |
while read -r i
do
	test -z "$i" && continue
	make_rfcomm_pppd "$i" >> "$3"
done
