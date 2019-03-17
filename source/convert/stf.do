#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Special setup for stf.
# This is invoked by all.do .
#

# This gets us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf printenv "$1" ; }
get_conf() { read_rc "$1" || true ; }

redo-ifchange rc.conf general-services

ipv4addr="`get_conf stf_interface_ipv4addr`"

case "${ipv4addr}" in
[Nn][Oo]|'')
	exit 0
	;;
esac

ipv4plen="`get_conf stf_interface_ipv4plen`"
ipv6ifid="`get_conf stf_interface_ipv6_ifid`"
ipv6slaid="`get_conf stf_interface_ipv6_slaid`"

getip6laddr() {
	local proto addr rest

	ifconfig "$1" "inet6" 2>/dev/null | 
	while read -r proto keyw addr rest
	do
		if ! test _"${keyw}" = _"address"
		then
			rest="${addr} ${rest}"
			addr="${keyw}"
			keyw="address"
		fi
		case "${proto}/${addr}/${rest}" in
		inet6/fe80::*/*)
			echo ${addr}
			return
			;;
		inet6/fe80::/*tentative*)
			sleep "`sysctl -N net.inet6.ip6.dad_count`"
			getip6laddr "$1"
			return
			;;
		esac
	done
}

IFS=".$IFS" set ${ipv4addr}
ipv4hex="`printf \"%02x%02x:%02x%02x\" \"$1\" \"$2\" \"$3\" \"$4\"`"

case "${ipv6ifid}" in
[Aa][Uu][Tt][Oo]|'')
	get_conf ipv6_network_interfaces |
	while read -r i
	do
		laddr="`getip6laddr \"${i}\"`"
		test -n "${laddr}" && break
	done
	ipv6ifid="`expr \"${laddr}\" : 'fe80::\(.*\)%\(.*\)'`"
	test -z "${ipv6ifid}" && ipv6ifid=0:0:0:1
	;;
esac

system-control set-service-env stf ipv4 "${ipv4hex}"
system-control set-service-env stf slaid "${ipv6slaid:-0}"
system-control set-service-env stf ifid "${ipv6ifid}"
system-control set-service-env stf prefixlen "`expr 16 + \"${ipv4plen:-0}\"`"

system-control print-service-env stf >> "$3"
