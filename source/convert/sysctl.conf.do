#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
#
# Special setup for sysctl.conf.
# This is invoked by all.do .
#

# This gets us *only* the configuration variables, safely.
dump_rc() { clearenv read-conf rc.conf printenv ; }

redo-ifchange rc.conf

echo > "$3" "# Automatically re-generated from rc.conf"
dump_rc |
awk >> "$3" -F = '
/^harvest_/ { 
	n = substr($1,9); 
	if ("p_to_p" == n) n = "point_to_point" ; 
	if ("yes" == $2 || "YES" == $2 || "true" == $2 || "1" == $2 || "on" == $2) v = "1"; else v = "0"; 
	print "kern.random.sys.harvest."n"="v; 
}
/^tcp_/ { 
	n = substr($1,5); 
	if ("extensions" != n && "keepalive" != n && "drop_synfin" != n) next;
	if ("extensions" == n) n = "rfc1323" ; 
	if ("keepalive" == n) n = "always_keepalive" ; 
	if ("yes" == $2 || "YES" == $2 || "true" == $2 || "1" == $2 || "on" == $2) v = "1"; else v = "0"; 
	print "net.inet.tcp."n"="v; 
}
/^ipv6_/ { 
	n = substr($1,6); 
	if ("ipv4mapping" != n && "privacy" != n && "cpe_wanif" != n && "gateway_enable" != n && "default_interface" != n) next;
	if ("ipv4mapping" == n) n = "v6only" ; 
	if ("privacy" == n) n = "use_tempaddr" ; 
	if ("cpe_wanif" == n) n = "no_radr" ; 
	if ("gateway_enable" == n) n = "forwarding" ; 
	if ("default_interface" == n) { n = "use_defaultzone" ; v = "1" ; }
	if ("yes" == $2 || "YES" == $2 || "true" == $2 || "1" == $2 || "on" == $2) v = "1"; else v = "0"; 
	if ("v6only" == n) v = !v;
	print "net.inet6.ip6."n"="v; 
	if ("use_tempaddr" == n) {
		n = "prefer_tempaddr";
		print "net.inet6.ip6."n"="v; 
	}
	if ("no_radr" == n) {
		n = "rfc6204w3";
		print "net.inet6.ip6."n"="v; 
	}
}
/^ip_portrange_/ { 
	n = substr($1,14); 
	v = 0 + $2;
	if (v > 0) print "net.inet.ip.portrange."n"="v; 
}
/^nfs_/ { 
	n = substr($1,5); 
	if ("access_cache" == n) {
		n = "access_cache_timeout" ; 
		v = $2;
		print "vfs.nfs."n"="v; 
	} else
	if ("bufpackets" == n) {
		v = $2;
		if (length(v))
			print "vfs.nfs."n"="v; 
	} else
	if ("reserved_port_only" == n) {
		n = "nfs_privport";
		if ("yes" == $2 || "YES" == $2 || "true" == $2 || "1" == $2 || "on" == $2) v = "1"; else v = "0"; 
		print "vfs.nfssrv."n"="v; 
		print "vfs.nfsd."n"="v; 
	}
}
/^nfs4_/ { 
	n = substr($1,6); 
	if ("server_enable" != n) next;
	n = "mfs_max_nfsvers";
	if ("yes" == $2 || "YES" == $2 || "true" == $2 || "1" == $2 || "on" == $2) v = "4"; else v = "3"; 
	print "vfs.nfsd."n"="v; 
}
/^icmp_/ { 
	n = substr($1,6); 
	if ("bmcastecho" != n && "drop_redirect" != n && "log_redirect" != n) next;
	if ("yes" == $2 || "YES" == $2 || "true" == $2 || "1" == $2 || "on" == $2) v = "1"; else v = "0"; 
	print "net.inet.icmp."n"="v; 
}
/^arp/ { 
	n = substr($1,4); 
	if ("proxy_all" != n) next;
	if ("proxy_all" == n) n = "proxyall" ; 
	if ("yes" == $2 || "YES" == $2 || "true" == $2 || "1" == $2 || "on" == $2) v = "1"; else v = "0"; 
	print "net.link.ether.inet."n"="v; 
}
/^/ { 
	n = $1; 
	if ("gateway_enable" != n && "forward_sourceroute" != n && "accept_sourceroute" != n && "log_in_vain" != n) next;
	if ("gateway_enable" == n) n = "forwarding" ; 
	if ("forward_sourceroute" == n) n = "sourceroute" ; 
	if ("log_in_vain" != n) {
		if ("yes" == $2 || "YES" == $2 || "true" == $2 || "1" == $2 || "on" == $2) v = "1"; else v = "0";
		print "net.inet.ip."n"="v; 
	} else {
		print "net.inet.tcp."n"="v; 
		print "net.inet.udp."n"="v; 
	}
}
'
