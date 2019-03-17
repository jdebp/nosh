## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************

{
	if ("auto" == $1 || "allow-auto" == $1) {
		iface="";
		auto[$2]=1;
		ipv4[$2] = ipv4[$2];
	} else
	if ("allow-hotplug" == $1 || "no-scripts" == $1 || "source" == $1 || "source-directory" == $1) {
		iface="";
	} else
	if ("iface" == $1) {
		iface=$2;
		family=$3;
		method=$4;
		++interfaces[iface];
		if ("inet" == family) {
			++ipv4s[iface];
			if (1 < ipv4s[iface]) {
				if ("" != aliases[iface]) {
					aliases[iface] = aliases[iface] "\n";
				}
				aliases[iface] = aliases[iface] family;
			}
		} else
		if ("inet6" == family) {
			++ipv6s[iface];
			if (1 < ipv6s[iface]) {
				if ("" != aliases[iface]) {
					aliases[iface] = aliases[iface] "\n";
				}
				aliases[iface] = aliases[iface] family;
			}
		}
		if ("auto" == method || "dhcp" == method) {
			dhcp[iface]=1
			ipv4[iface] = ipv4[iface];
		} else
		if ("ipv4ll" == method) {
			if ("inet" == family) {
				ipv4ll[iface]=1
				ipv4[iface] = ipv4[iface];
			} else
				;
		} else
		if ("loopback" == method) {
			if ("inet" == family) {
				if (1 < ipv4s[iface])
					aliases[iface] = aliases[iface] " 127.0.0.1/8";
				else
					ipv4_address[iface] = ipv4_address[iface] " 127.0.0.1/8";
				ipv4[iface] = ipv4[iface];
			} else
			if ("inet6" == family) {
				if (1 < ipv6s[iface])
					aliases[iface] = aliases[iface] " ::1/128";
				else
					ipv6_address[iface] = ipv6_address[iface] " ::1/128";
				ipv6[iface] = ipv6[iface];
				ipv4[iface] = ipv4[iface];
			} else
				;
		} else
			;
	} else
	if ("" == iface)
		;
	else
	if ("loopback" == method)
		;
	else
	if ("static" == method) {
		if ("address" == $1) {
			if ("inet" == family) {
				if (1 < ipv4s[iface])
					aliases[iface] = aliases[iface] " " $2;
				else
					ipv4_address[iface] = ipv4_address[iface] " " $2;
				ipv4[iface] = ipv4[iface];
			} else
			if ("inet6" == family) {
				if (1 < ipv6s[iface])
					aliases[iface] = aliases[iface] " " $2;
				else
					ipv6_address[iface] = ipv6_address[iface] " " $2;
				ipv6[iface] = ipv6[iface];
				ipv4[iface] = ipv4[iface];
			} else
				;
		} else
		if ("broadcast" == $1 && "+" == $2) {
			if ("inet" == family) {
				if (1 < ipv4s[iface])
					aliases[iface] = aliases[iface] " broadcast1";
				else
					ipv4_address[iface] = ipv4_address[iface] " broadcast1";
				ipv4[iface] = ipv4[iface];
			} else
			if ("inet6" == family) {
				if (1 < ipv6s[iface])
					aliases[iface] = aliases[iface] " broadcast1";
				else
					ipv6_address[iface] = ipv6_address[iface] " broadcast1";
				ipv6[iface] = ipv6[iface];
				ipv4[iface] = ipv4[iface];
			} else
				;
		} else
		if ("netmask" == $1 || "broadcast" == $1 || "prefixlen" == $1) {
			if ("inet" == family) {
				if (1 < ipv4s[iface])
					aliases[iface] = aliases[iface] " " $1 " " $2;
				else
					ipv4_address[iface] = ipv4_address[iface] " " $1 " " $2;
				ipv4[iface] = ipv4[iface];
			} else
			if ("inet6" == family) {
				if (1 < ipv6s[iface])
					aliases[iface] = aliases[iface] " " $1 " " $2;
				else
					ipv6_address[iface] = ipv6_address[iface] " " $1 " " $2;
				ipv6[iface] = ipv6[iface];
				ipv4[iface] = ipv4[iface];
			} else
				;
		} else
		if ("scope" == $1) {
			## "link" is an address family.
			if ("link" == $2)
				$2 = "link-local";
			if ("inet" == family) {
				if (1 < ipv4s[iface])
					aliases[iface] = aliases[iface] " " $1 " " $2;
				else
					ipv4_address[iface] = ipv4_address[iface] " " $1 " " $2;
				ipv4[iface] = ipv4[iface];
			} else
			if ("inet6" == family) {
				if (1 < ipv6s[iface])
					aliases[iface] = aliases[iface] " " $1 " " $2;
				else
					ipv6_address[iface] = ipv6_address[iface] " " $1 " " $2;
				ipv6[iface] = ipv6[iface];
				ipv4[iface] = ipv4[iface];
			} else
				;
		} else
		if ("pointopoint" == $1) {
			if ("inet" == family) {
				if (1 < ipv4s[iface])
					aliases[iface] = aliases[iface] " dest " $2;
				else
					ipv4_address[iface] = ipv4_address[iface] " dest " $2;
				ipv4[iface] = ipv4[iface];
			} else
			if ("inet6" == family) {
				if (1 < ipv6s[iface])
					aliases[iface] = aliases[iface] " dest " $2;
				else
					ipv6_address[iface] = ipv6_address[iface] " dest " $2;
				ipv6[iface] = ipv6[iface];
				ipv4[iface] = ipv4[iface];
			} else
				;
		} else
		if ("metric" == $1 || "hwaddress" == $1 || "mtu" == $1)
			link[iface] = link[iface] " " $1 " " $2;
		else
		if ("gateway" == $1) {
			;
		} else
		if ("media" == $1)
			;
		else
		if ("eui64" == $1)
			if ("inet" == family) {
				if (1 < ipv4s[iface])
					aliases[iface] = aliases[iface] " " $1;
				else
					ipv4_address[iface] = ipv4_address[iface] " " $1;
				ipv4[iface] = ipv4[iface];
			} else
			if ("inet6" == family) {
				if (1 < ipv6s[iface])
					aliases[iface] = aliases[iface] " " $1;
				else
					ipv6_address[iface] = ipv6_address[iface] " " $1;
				ipv6[iface] = ipv6[iface];
				ipv4[iface] = ipv4[iface];
			} else
				;
		else
		if ("accept_ra" == $1)
			ipv6[iface] = ipv6[iface] " accept_rtadv";
		else
		if ("autoconf" == $1)
			ipv6[iface] = ipv6[iface] " auto_linklocal";
		else
		if ("privext" == $1)
			;
		else
		if ("preferred-lifetime" == $1)
			;
		else
		if ("dad-attempts" == $1)
			;
		else
		if ("dad-lifetime" == $1)
			;
		else
			;
	} else
	if ("auto" == method) {
		if ("accept_ra" == $1)
			ipv6[iface] = ipv6[iface] " accept_rtadv";
		else
		if ("privext" == $1)
			;
		else
		if ("dhcp" == $1)
			;
		else
		if ("request_prefix" == $1)
			;
		else
		if ("ll-interval" == $1)
			;
		else
		if ("ll-attempts" == $1)
			;
		else
			;
	} else
	if ("manual" == method) {
		if ("metric" == $1 || "hwaddress" == $1 || "mtu" == $1)
			link[iface] = link[iface] " " $1 " " $2;
		else
			;
	} else
	if ("dhcp" == method) {
		if ("hostname" == $1)
			hostnames[iface]=$2;
		else
		if ("metric" == $1 || "hwaddress" == $1 || "mtu" == $1)
			link[iface] = link[iface] " " $1 " " $2;
		else
		if ("leasehours" == $1)
			;
		else
		if ("vendor" == $1)
			;
		else
		if ("client" == $1)
			;
		else
		if ("accept_ra" == $1)
			ipv6[iface] = ipv6[iface] " accept_rtadv";
		else
		if ("autoconf" == $1)
			ipv6[iface] = ipv6[iface] " auto_linklocal";
		else
		if ("request_prefix" == $1)
			;
		else
		if ("ll-attempts" == $1)
			;
		else
		if ("ll-interval" == $1)
			;
		else
			;
	} else
		;
}
END {
	if (length(interfaces)) {
		printf "network_interfaces=\"";
		for (n in interfaces) printf "%s ",n;
		printf "\"\n";
	}
	for (n in ipv4) {
		printf "ifconfig_%s=\"",n;
		if (auto[n]) printf "AUTO "; else printf "NOAUTO ";
		if (dhcp[n]) printf "DHCP ";
		if (wpa[n]) printf "WPA ";
		if (hostap[n]) printf "HOSTAP ";
		if (ipv4ll[n]) printf "IPV4LL ";
		printf "inet %s %s\"\n",ipv4_address[n],ipv4[n];
	}
	for (n in link)
		printf "ifconfig_%s_link=\"link %s\"\n",n,link[n];
	for (n in ipv6)
		printf "ifconfig_%s_ipv6=\"inet6 %s %s\"\n",n,ipv6_address[n],ipv6[n];
	for (n in aliases)
		printf "ifconfig_%s_aliases=\"%s\"\n",n,aliases[n];
	for (n in hostnames)
		printf "dhclient_hostname_%s=\"%s\"\n",n,hostnames[n];
}
