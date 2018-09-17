#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Special setup for ip6addrctl
# This is invoked by all.do .
#

# This gets us *only* the configuration variables, safely.
read_rc() { clearenv read-conf rc.conf printenv "$1" ; }
get_var() { read_rc $1 || true ; }

redo-ifchange rc.conf general-services

for i in ipv4 ipv6
do
	system-control disable cyclog@ip6addrctl@${i} ip6addrctl@${i}
done

prefer() {
	system-control preset --prefix cyclog@ ip6addrctl@"$1"
	system-control preset ip6addrctl@"$1"
}

case "`get_var ip6addrctl_policy`" in
ipv4_prefer)
	prefer ipv4
	;;
ipv6_prefer)
	prefer ipv6
	;;
[Nn][Oo][Nn][Ee])
	;;
[Aa][Uu][Tt][Oo])
	case "`read_rc ipv6_activate_all_interfaces || read_rc ipv6_enable || true`" in
	[Yy][Ee][Ss]|[Tt][Rr][Uu][Ee]|[Oo][Nn]|1)
		prefer ipv6
		;;
	[Nn][Oo]|[Ff][Aa][Ll][Ss][Ee]|[Oo][Ff][Ff]|0)
		prefer ipv4
		;;
	*)
		# We could look for ifconfig_*_ipv6 variables, but this is the 21st century and we have had 3 IPv6 Days now.
		prefer ipv6
		;;
	esac
	;;
*)
	echo 1>&2 "$0: \$ip6addrctl_policy is invalid: ${ip6addrctl_policy}. Obsolete \$ipv6_prefer is used instead."
	case "`get_var ipv6_prefer`" in
	[Yy][Ee][Ss]|[Tt][Rr][Uu][Ee]|[Oo][Nn]|1)
		prefer ipv6
		;;
	[Nn][Oo]|[Ff][Aa][Ll][Ss][Ee]|[Oo][Ff][Ff]|0)
		prefer ipv4
		;;
	*)
		;;
	esac
	;;
esac
