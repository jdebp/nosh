/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <map>
#include <vector>
#include <iostream>
#include <iomanip>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <cerrno>
#include <stdint.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#if defined(__LINUX__) || defined(__linux__)
#	include <endian.h>
#else
#	include <sys/endian.h>
#endif
#include <unistd.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <netinet/in.h>
#if defined(__FreeBSD__) || defined(__DragonFly__)
#	include <net/if_dl.h>
#	include <net/if_var.h>
#	include <netinet/in_var.h>
struct prf_ra : public in6_prflags::prf_ra {};
#	include <netinet6/in6_var.h>
#	include <netinet6/nd6.h>
#endif
#if defined(__OpenBSD__)
#	include <net/if_dl.h>
#	include <net/if_var.h>
#	include <netinet/in_var.h>
#	include <netinet6/in6_var.h>
#	include <netinet6/nd6.h>
#endif
#if defined(__NetBSD__)
#	include <net/if_dl.h>
#	include <netinet/in_var.h>
#	include <netinet6/in6_var.h>
#	include <netinet6/nd6.h>
#endif
#if defined(__LINUX__) || defined(__linux__)
#	include <linux/netlink.h>
#	include <linux/rtnetlink.h>
#	include <linux/if_link.h>
#	include <linux/if_packet.h>
#endif
#include <inttypes.h>
#include "popt.h"
#include "utils.h"
#include "fdutils.h"
#include "ttyname.h"
#include "FileDescriptorOwner.h"
#include "CharacterCell.h"
#include "ECMA48Output.h"
#include "TerminalCapabilities.h"
#include "IPAddress.h"

/* getifaddrs helpers *******************************************************
// **************************************************************************
*/

namespace {

	struct InterfaceAddresses {
		InterfaceAddresses(ifaddrs * a) : addr_list(a) {}
		~InterfaceAddresses() { freeifaddrs(addr_list); addr_list = nullptr; }
		operator ifaddrs * () const { return addr_list; }
	protected:
		ifaddrs * addr_list;
	};

}

/* Flags ********************************************************************
// **************************************************************************
*/

namespace {

	enum {
		ALIAS		= 0x0001,
		EUI64		= 0x0002,
		DEFAULTIF	= 0x0004,
	};

	static const
	struct flag_info {
		const char * name;
		unsigned int bits;
	} flags[] = {
		{	"alias",	ALIAS		},
		{	"defaultif",	DEFAULTIF	},
	}, ifflags[] = {
		{	"up",		IFF_UP		},
		{	"broadcast",	IFF_BROADCAST	},
		{	"debug",	IFF_DEBUG	},
		{	"loopback",	IFF_LOOPBACK	},
		{	"pointtopoint",	IFF_POINTOPOINT	},
#if defined(IFF_SMART)
		{	"smart",	IFF_SMART	},
#endif
#if defined(IFF_NOTRAILERS)
		{	"notrailers",	IFF_NOTRAILERS	},
#endif
#if defined(IFF_DRV_RUNNING)
		{	"drv_running",	IFF_DRV_RUNNING	},
#endif
#if defined(IFF_RUNNING)
		{	"running",	IFF_RUNNING	},
#endif
		{	"noarp",	IFF_NOARP	},
		{	"promisc",	IFF_PROMISC	},
		{	"allmulti",	IFF_ALLMULTI	},
#if defined(IFF_DRV_OACTIVE)
		{	"drv_oactive",	IFF_DRV_OACTIVE	},
#endif
#if defined(IFF_SIMPLEX)
		{	"simplex",	IFF_SIMPLEX	},
#endif
#if defined(IFF_LINK0)
		{	"link0",	IFF_LINK0	},
#endif
#if defined(IFF_LINK1)
		{	"link1",	IFF_LINK1	},
#endif
#if defined(IFF_LINK2)
		{	"link2",	IFF_LINK2	},
#endif
#if defined(IFF_LINK2)
		{	"link2",	IFF_LINK2	},
#endif
#if defined(IFF_MULTICAST)
		{	"multicast",	IFF_MULTICAST	},
#endif
#if defined(IFF_CANTCONFIG)
		{	"cantconfig",	IFF_CANTCONFIG	},
#endif
#if defined(IFF_PPROMISC)
		{	"ppromisc",	IFF_PPROMISC	},
#endif
#if defined(IFF_MONITOR)
		{	"monitor",	IFF_MONITOR	},
#endif
#if defined(IFF_STATICARP)
		{	"staticarp",	IFF_STATICARP	},
#endif
#if defined(IFF_DYING)
		{	"dying",	IFF_DYING	},
#endif
#if defined(IFF_RENAMING)
		{	"renaming",	IFF_RENAMING	},
#endif
#if defined(IFF_MASTER)
		{	"master",	IFF_MASTER	},
#endif
#if defined(IFF_SLAVE)
		{	"slave",	IFF_SLAVE	},
#endif
#if defined(IFF_PORTSEL)
		{	"portsel",	IFF_PORTSEL	},
#endif
#if defined(IFF_AUTOMEDIA)
		{	"automedia",	IFF_AUTOMEDIA	},
#endif
#if defined(IFF_DYNAMIC)
		{	"dynamic",	IFF_DYNAMIC	},
#endif
	}, in6flags[] = {
#if defined(SIOCGIFAFLAG_IN6)
		{	"anycast",		IN6_IFF_ANYCAST		},
		{	"tentative",		IN6_IFF_TENTATIVE	},
		{	"deprecated",		IN6_IFF_DEPRECATED	},
		{	"duplicated",		IN6_IFF_DUPLICATED	},
		{	"detached",		IN6_IFF_DETACHED	},
		{	"autoconf",		IN6_IFF_AUTOCONF	},
#if defined(IN6_IFF_PRIVACY)
		{	"autoconfprivacy",	IN6_IFF_PRIVACY		},
#endif
#if defined(IN6_IFF_PREFER_SOURCE)
		{	"prefer_source",	IN6_IFF_PREFER_SOURCE	},
#endif
#endif
	}, nd6flags[] = {
#if defined(SIOCGIFINFO_IN6)
		{	"performnud",		ND6_IFF_PERFORMNUD		},
#if defined(ND6_IFF_ACCEPT_RTADV)
		{	"accept_rtadv",		ND6_IFF_ACCEPT_RTADV		},
#endif
		{	"prefer_source",	ND6_IFF_PREFER_SOURCE		},
		{	"ifdisabled",		ND6_IFF_IFDISABLED		},
		{	"auto_linklocal",	ND6_IFF_AUTO_LINKLOCAL		},
#if defined(ND6_IFF_NO_RADR)
		{	"no_radr",		ND6_IFF_NO_RADR			},
#endif
#if defined(ND6_IFF_NO_PREFER_IFACE)
		{	"no_prefer_iface",	ND6_IFF_NO_PREFER_IFACE		},
#endif
#if defined(ND6_IFF_DONT_SET_IFROUTE)
		{	"no_set_ifroute",	ND6_IFF_DONT_SET_IFROUTE	},
#endif
#if defined(ND6_IFF_NO_DAD)
		{	"no_dad",		ND6_IFF_NO_DAD			},
#endif
#endif
	}, capflags[] = {
#if defined(SIOCGIFCAP) && !defined(__NetBSD__)
		{	"rxcsum",		IFCAP_RXCSUM		},
		{	"txcsum",		IFCAP_TXCSUM		},
		{	"netcons",		IFCAP_NETCONS		},
		{	"vlan_mtu",		IFCAP_VLAN_MTU		},
		{	"vlan_hwtagging",	IFCAP_VLAN_HWTAGGING	},
		{	"jumbo_mtu",		IFCAP_JUMBO_MTU		},
		{	"polling",		IFCAP_POLLING		},
		{	"hwcsum",		IFCAP_HWCSUM		},
		{	"tso4",			IFCAP_TSO4		},
		{	"tso6",			IFCAP_TSO6		},
		{	"lro",			IFCAP_LRO		},
		{	"wol_ucast",		IFCAP_WOL_UCAST		},
		{	"wol_mcast",		IFCAP_WOL_MCAST		},
		{	"wol_magic",		IFCAP_WOL_MAGIC		},
		{	"toe4",			IFCAP_TOE4		},
		{	"toe6",			IFCAP_TOE6		},
		{	"vlan_hwfilter",	IFCAP_VLAN_HWFILTER	},
#if defined(IFCAP_POLLING_NOCOUNT) // flag deleted in 2017
		{	"polling_nocount",	IFCAP_POLLING_NOCOUNT	},
#endif
		{	"tso",			IFCAP_TSO		},
		{	"linkstate",		IFCAP_LINKSTATE		},
		{	"netmap",		IFCAP_NETMAP		},
		{	"rxcsum_ipv6",		IFCAP_RXCSUM_IPV6	},
		{	"txcsum_ipv6",		IFCAP_TXCSUM_IPV6	},
		{	"hwstats",		IFCAP_HWSTATS		},
#endif
	};

}

/* Address family names *****************************************************
// **************************************************************************
*/

namespace {

	static const
	struct family_info {
		const char * name;
		int family;
	} families[] = {
#if defined(AF_PACKET)
		{	"link",		AF_PACKET	},
		{	"ether",	AF_PACKET	},
		{	"packet",	AF_PACKET	},
		{	"lladr",	AF_PACKET	},
		{	"lladdr",	AF_PACKET	},
#endif
#if defined(AF_LINK)
		{	"link",		AF_LINK		},
		{	"ether",	AF_LINK		},
		{	"packet",	AF_LINK		},
		{	"lladr",	AF_LINK		},
		{	"lladdr",	AF_LINK		},
#endif
		{	"inet4",	AF_INET		},
		{	"ip4",		AF_INET		},
		{	"ipv4",		AF_INET		},
		{	"inet",		AF_INET		},
		{	"inet6",	AF_INET6	},
		{	"ip6",		AF_INET6	},
		{	"ipv6",		AF_INET6	},
#if defined(SIOCGIFINFO_IN6)
		{	"nd6",		AF_INET6	},
#endif
		{	"local",	AF_LOCAL	},
		{	"unix",		AF_UNIX		},
	};

	const char *
	get_family_name (
		int f
	) {
		for (const family_info * p(families), * const e(p + sizeof families/sizeof *families); p != e; ++p)
			if (p->family == f)
				return p->name;
		return nullptr;
	}

	int
	get_family (
		const char * name
	) {
		for (const family_info * p(families), * const e(p + sizeof families/sizeof *families); p != e; ++p)
			if (0 == std::strcmp(p->name, name))
				return p->family;
		return AF_UNSPEC;
	}

}

/* Outputting information ***************************************************
// **************************************************************************
*/

namespace {

#if defined(SIOCGIFAFLAG_IN6)
	bool
	get_in6_flags (
		const char * name,
		const sockaddr_in6 & addr,
		uint_least32_t & flags6
	) {
		const FileDescriptorOwner s(socket_close_on_exec(AF_INET6, SOCK_DGRAM, 0));
		if (0 > s.get()) return false;
		in6_ifreq r = {};
		std::strncpy(r.ifr_name, name, sizeof(r.ifr_name));
		r.ifr_addr = addr;
		if (0 > ioctl(s.get(), SIOCGIFAFLAG_IN6, &r)) return false;
		flags6 = r.ifr_ifru.ifru_flags6;
		return true;
	}
#endif

#if defined(SIOCGIFINFO_IN6)
	bool
	get_nd6_flags (
		const char * name,
		uint_least32_t & flags6
	) {
		const FileDescriptorOwner s(socket_close_on_exec(AF_INET6, SOCK_DGRAM, 0));
		if (0 > s.get()) return false;
		in6_ndireq r = {};
		std::strncpy(r.ifname, name, sizeof(r.ifname));
		if (0 > ioctl(s.get(), SIOCGIFINFO_IN6, &r)) return false;
		flags6 = r.ndi.flags;
		return true;
	}
#endif

#if defined(SIOCGIFCAP) && !defined(__NetBSD__)
	bool
	get_capabilities (
		const char * name,
		uint_least32_t & cap
	) {
		const FileDescriptorOwner s(socket_close_on_exec(AF_INET, SOCK_DGRAM, 0));
		if (0 > s.get()) return false;
		ifreq r = {};
		std::strncpy(r.ifr_name, name, sizeof(r.ifr_name));
		if (0 > ioctl(s.get(), SIOCGIFCAP, &r)) return false;
		cap = r.ifr_curcap;
		return true;
	}
#endif

	void
	print (
		ECMA48Output & o,
		const char * prefix,
		const sockaddr & addr
	) {
		switch (addr.sa_family) {
			case AF_INET:
			{
				const struct sockaddr_in & addr4(reinterpret_cast<const struct sockaddr_in &>(addr));
				char ip[INET_ADDRSTRLEN];
				if (nullptr != inet_ntop(addr4.sin_family, &addr4.sin_addr, ip, sizeof ip)) {
					std::cout << prefix;
					o.SGRColour(true, Map256Colour(COLOUR_MAGENTA));
					std::cout << ip;
					o.SGRColour(true);
					std::cout.put(' ');
				}
				break;
			}
			case AF_INET6:
			{
				const struct sockaddr_in6 & addr6(reinterpret_cast<const struct sockaddr_in6 &>(addr));
				char ip[INET6_ADDRSTRLEN];
				if (nullptr != inet_ntop(addr6.sin6_family, &addr6.sin6_addr, ip, sizeof ip)) {
					std::cout << prefix;
					o.SGRColour(true, Map256Colour(COLOUR_CYAN));
					std::cout << ip;
					o.SGRColour(true);
					std::cout << " scope " << addr6.sin6_scope_id;
					std::cout.put(' ');
				}
				break;
			}
			case AF_LOCAL:
			{
				const struct sockaddr_un & addru(reinterpret_cast<const struct sockaddr_un &>(addr));
				std::cout << prefix;
				o.SGRColour(true, Map256Colour(COLOUR_BLUE));
				std::cout << addru.sun_path;
				o.SGRColour(true);
				std::cout.put(' ');
				break;
			}
#if defined(AF_PACKET)
			case AF_PACKET:
			{
				const struct sockaddr_ll & addrl(reinterpret_cast<const struct sockaddr_ll &>(addr));
				std::cout << prefix;
				o.SGRColour(true, Map256Colour(COLOUR_YELLOW));
				if (addrl.sll_halen)
					for (std::size_t i(0U); i < addrl.sll_halen; ++i) {
						if (i) std::cout.put(':');
						std::cout << std::hex << std::setfill('0') << std::setw(2) << unsigned(addrl.sll_addr[i]) << std::dec << std::setfill(' ');
					}
				else
					std::cout.put(':');
				o.SGRColour(true);
				std::cout.put(' ');
				break;
			}
#endif
#if defined(AF_LINK)
			case AF_LINK:
			{
				const struct sockaddr_dl & addrl(reinterpret_cast<const struct sockaddr_dl &>(addr));
				std::cout << prefix;
				o.SGRColour(true, Map256Colour(COLOUR_YELLOW));
				if (addrl.sdl_alen)
					for (std::size_t i(0U); i < addrl.sdl_alen; ++i) {
						if (i) std::cout.put(':');
						std::cout << std::hex << std::setfill('0') << std::setw(2) << unsigned(uint8_t(LLADDR(&addrl)[i])) << std::dec << std::setfill(' ');
					}
				else
					std::cout.put(':');
				o.SGRColour(true);
				std::cout.put(' ');
				break;
			}
#endif
			default:
				std::cout << prefix << "sa_?" << int(addr.sa_family);
				break;
			case AF_UNSPEC:
				break;
		}
	}

#if defined(AF_PACKET)
	std::ostream &
	operator << (
		std::ostream & o,
		const rtnl_link_stats & d
	) {
		/// \todo: TODO: Print something meaningful.
		return o;
	}
#endif

#if defined(AF_LINK)
	std::ostream &
	operator << (
		std::ostream & o,
		const if_data & d
	) {
		o <<
			"metric " << d.ifi_metric << " "
			"mtu " << d.ifi_mtu << " "
			"\n\t\t"
			"type " << unsigned(d.ifi_type) << " "
			"linkstate " << unsigned(d.ifi_link_state) << " "
#if !defined(__OpenBSD__) && !defined(__NetBSD__)
			"physical " << unsigned(d.ifi_physical) << " "
#endif
			"baudrate " << d.ifi_baudrate << " "
		;
		return o;
	}
#endif

	inline
	void
	OpenBSDFixup (
		sockaddr & addr,
		const sockaddr * ifa_addr
	) {
#if defined(__OpenBSD__)
		// Fix up an OpenBSD bug where it does not set the address family properly but fills in the address.
		if (AF_UNSPEC == addr.sa_family && ifa_addr)
		addr.sa_family = ifa_addr->sa_family;
#else
		static_cast<void>(addr);	// Silences a compiler warning.
		static_cast<void>(ifa_addr);	// Silences a compiler warning.
#endif
	}

	void
	output_listing (
		const char * prog,
		const ProcessEnvironment & envs,
		ECMA48Output & o,
		int names_only,
		bool up_only,
		bool down_only,
		int address_family,
		const char * interface_name
	) {
		ifaddrs * al(nullptr);
		if (0 > getifaddrs(&al)) {
			die_errno(prog, envs, "getifaddrs");
		}
		const InterfaceAddresses addr_list(al);

		// Create a map of interface names to linked-list pointers.
		typedef std::map<std::string, ifaddrs *> name_to_ifaddrs;
		name_to_ifaddrs start_points;
		for (ifaddrs * a(addr_list); a; a = a->ifa_next) {
			const std::string name(a->ifa_name);
			name_to_ifaddrs::iterator i(start_points.find(name));
			if (start_points.end() != i) continue;
			start_points[name] = a;
		}

		bool first(true);
		for (name_to_ifaddrs::const_iterator b(start_points.begin()), e(start_points.end()), p(b); p != e; ++p) {
			if (AF_UNSPEC != address_family) {
				bool found_any(false);
				for (const ifaddrs * a(p->second); a; a = a->ifa_next) {
					if (p->first != a->ifa_name) continue;
					if (a->ifa_addr && a->ifa_addr->sa_family == address_family) {
						found_any = true;
						break;
					}
				}
				if (!found_any) continue;
			}
			if (const ifaddrs * a = p->second) {
				if (up_only && !(a->ifa_flags & IFF_UP)) continue;
				if (down_only && (a->ifa_flags & IFF_UP)) continue;
			}
			if (interface_name && p->first != interface_name) continue;

			if (names_only && !first) std::cout.put(' ');
			first = false;
			std::cout << p->first;
			if (names_only) continue;

			std::cout.put('\n');
			const unsigned int * pflags(nullptr);
			// Process each group of items in the list that share a single interface name.
			for (const ifaddrs * a(p->second); a; a = a->ifa_next) {
				if (p->first != a->ifa_name) continue;

				if (AF_UNSPEC != address_family && a->ifa_addr && a->ifa_addr->sa_family != address_family) continue;

				// Decode the flags, if we have not done so already.
				if (!pflags || *pflags != a->ifa_flags) {
					pflags = &a->ifa_flags;
					std::cout.put('\t') << "link ";
					o.SGRColour(true, Map256Colour(COLOUR_GREEN));
					std::cout << (a->ifa_flags & IFF_UP ? "up" : "down");
					o.SGRColour(true);
					for (const flag_info * fp(ifflags), * const fe(fp + sizeof ifflags/sizeof *ifflags); fp != fe; ++fp)
						if (a->ifa_flags & fp->bits && fp->bits != IFF_UP)
							std::cout.put(' ') << fp->name;
					std::cout.put('\n');
#if defined(SIOCGIFINFO_IN6)
					uint_least32_t flags6;
					if (get_nd6_flags(a->ifa_name, flags6)) {
						std::cout.put('\t') << "nd6";
						for (const flag_info * const fb(nd6flags), * const fe(fb + sizeof nd6flags/sizeof *nd6flags), * fp(fb); fp != fe; ++fp)
							if (flags6 & fp->bits)
								std::cout.put(' ') << fp->name;
						std::cout.put('\n');
					}
#endif
#if defined(SIOCGIFCAP) && !defined(__NetBSD__)
					uint_least32_t cap;
					if (get_capabilities(a->ifa_name, cap)) {
						std::cout.put('\t') << "link";
						for (const flag_info * const fb(capflags), * const fe(fb + sizeof capflags/sizeof *capflags), * fp(fb); fp != fe; ++fp)
							if (cap & fp->bits)
								std::cout.put(' ') << fp->name;
						std::cout.put('\n');
					}
#endif
				}

				std::cout << '\t';
				if (const char * f = a->ifa_addr ? get_family_name(a->ifa_addr->sa_family) : nullptr)
					std::cout << f << ' ';
				else
					std::cout << "unknown-family ";
				if (const sockaddr * addr = a->ifa_addr)
					print(o, "address ", *addr);
				if (sockaddr * addr = a->ifa_netmask) {
					OpenBSDFixup(*addr, a->ifa_addr);

					unsigned prefixlen;
					if (IPAddress::IsPrefix(*addr, prefixlen))
						std::cout << "prefixlen " << prefixlen << ' ';
					else
						print(o, "netmask ", *addr);
				}
#if defined(__LINUX__) || defined(__linux__) || defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__) || defined(__NetBSD__)
				// In Linux, FreeBSD, NetBSD, and OpenBSD implementations, ifa_broadaddr and ifa_dstaddr are aliases for a single value.
				if (a->ifa_flags & IFF_BROADCAST) {
					if (sockaddr * addr = a->ifa_broadaddr) {
						OpenBSDFixup(*addr, a->ifa_addr);
						print(o, "broadcast ", *addr);
					}
				} else
				if (a->ifa_flags & IFF_POINTOPOINT) {
					if (sockaddr * addr = a->ifa_dstaddr) {
						OpenBSDFixup(*addr, a->ifa_addr);
						print(o, "dest ", *addr);
					}
				} else
				{
					if (sockaddr * addr = a->ifa_broadaddr)
						// OpenBSD ifconfig ignores this case, and the ifa_broadaddr data returned are bogus; so we do not attempt to fix things up.
						print(o, "bdaddr ", *addr);
				}
#else
				if (const sockaddr * addr = a->ifa_broadaddr)
					print(o, "broadcast ", *addr);
				if (const sockaddr * addr = a->ifa_dstaddr)
					print(o, "dest ", *addr);
#endif
#if defined(AF_PACKET)
				if (a->ifa_addr && AF_PACKET == a->ifa_addr->sa_family) {
					if (const void * data = a->ifa_data)
						std::cout << *reinterpret_cast<const rtnl_link_stats *>(data);
				}
#endif
#if defined(AF_LINK)
				if (a->ifa_addr && AF_LINK == a->ifa_addr->sa_family) {
					if (const void * data = a->ifa_data)
						std::cout << *reinterpret_cast<const if_data *>(data);
				}
#endif
#if defined(SIOCGIFAFLAG_IN6)
				if (a->ifa_addr && AF_INET6 == a->ifa_addr->sa_family) {
					uint_least32_t flags6;
					if (get_in6_flags(a->ifa_name, *reinterpret_cast<const sockaddr_in6 *>(a->ifa_addr), flags6)) {
						for (const flag_info * const fb(in6flags), * const fe(fb + sizeof in6flags/sizeof *in6flags), * fp(fb); fp != fe; ++fp)
							if (flags6 & fp->bits)
								std::cout << fp->name << ' ';
					}
				}
#endif
				std::cout.put('\n');
			}
		}
		if (names_only && !first) std::cout.put('\n');
		if (first && interface_name) {
			const char * msg(
				AF_UNSPEC == address_family && !up_only && !down_only ? "No such interface name." :
				"No matching interface(s)."
			);
			die_invalid(prog, envs, interface_name, msg);
		}
	}

	void
	list_cloner_interfaces (
		const char * prog,
		const ProcessEnvironment & envs
	) {
#if defined(SIOCIFGCLONERS)
		FileDescriptorOwner s(socket_close_on_exec(AF_LOCAL, SOCK_DGRAM, 0));
		if (0 > s.get()) {
	fatal:
			die_errno(prog, envs, prog);
		}

		if_clonereq request = {};

		request.ifcr_count = 0;
		request.ifcr_buffer = nullptr;
		if (0 > ioctl(s.get(), SIOCIFGCLONERS, &request)) goto fatal;

		char * buf(new char [request.ifcr_total * IFNAMSIZ]);
		if (!buf) goto fatal;
		request.ifcr_count = request.ifcr_total;
		request.ifcr_buffer = buf;
		if (0 > ioctl(s.get(), SIOCIFGCLONERS, &request)) {
			delete[] buf;
			goto fatal;
		}

		const char * p(buf);
		for (int i(0); i < request.ifcr_total; ++i, p += IFNAMSIZ) {
			if (i) std::cout.put(' ');
			const std::size_t len(strnlen(p, IFNAMSIZ));
			std::cout.write(p, len);
		}
		if (request.ifcr_total) std::cout.put('\n');

		delete[] buf;
#endif
	}

}

/* Parsing the command line *************************************************
// **************************************************************************
*/

namespace {

	bool
	is_negated (
		const char * & a
	) {
		if ('-' == a[0] && a[1]) {
			++a;
			return true;
		}
		if ('n' == a[0] && 'o' == a[1] && a[2]) {
			a += 2;
			return true;
		}
		return false;
	}

	bool
	parse_flag (
		const char * a,
		int address_family,
		uint_least32_t & flags_on,
		uint_least32_t & flags_off,
		uint_least32_t & ifflags_on,
		uint_least32_t & ifflags_off,
		uint_least32_t & in6flags_on,
		uint_least32_t & in6flags_off,
		uint_least32_t & nd6flags_on,
		uint_least32_t & nd6flags_off,
		uint_least32_t & capflags_on,
		uint_least32_t & capflags_off,
		bool & eui64,
		bool & broadcast1
	) {
		if (0 == std::strcmp("eui64", a)) {
			eui64 = true;
			return true;
		}
		if (0 == std::strcmp("broadcast1", a)) {
			broadcast1 = true;
			return true;
		}
		if (0 == std::strcmp("down", a)) {
			ifflags_off |= IFF_UP;
			return true;
		}
		const char * const original(a);
		const bool off(is_negated(a));
		for (const flag_info * fp(flags), * const fe(fp + sizeof flags/sizeof *flags); fp != fe; ++fp) {
			if (0 == std::strcmp(fp->name, a)) {
				(off ? flags_off : flags_on) |= fp->bits;
				return true;
			}
			if (0 == std::strcmp(fp->name, original)) {
				flags_on |= fp->bits;
				return true;
			}
		}
		for (const flag_info * fp(ifflags), * const fe(fp + sizeof ifflags/sizeof *ifflags); fp != fe; ++fp) {
			if (IFF_BROADCAST == fp->bits || IFF_POINTOPOINT == fp->bits) continue;
			if (0 == std::strcmp(fp->name, a)) {
				(off ? ifflags_off : ifflags_on) |= fp->bits;
				return true;
			}
			if (0 == std::strcmp(fp->name, original)) {
				ifflags_on |= fp->bits;
				return true;
			}
		}
		if (AF_INET6 == address_family) {
			for (const flag_info * fp(nd6flags), * const fe(fp + sizeof nd6flags/sizeof *nd6flags); fp != fe; ++fp) {
				if (0 == std::strcmp(fp->name, a)) {
					(off ? nd6flags_off : nd6flags_on) |= fp->bits;
					return true;
				}
				if (0 == std::strcmp(fp->name, original)) {
					nd6flags_on |= fp->bits;
					return true;
				}
			}
			for (const flag_info * fp(in6flags), * const fe(fp + sizeof in6flags/sizeof *in6flags); fp != fe; ++fp) {
				if (0 == std::strcmp(fp->name, a)) {
					(off ? in6flags_off : in6flags_on) |= fp->bits;
					return true;
				}
				if (0 == std::strcmp(fp->name, original)) {
					in6flags_on |= fp->bits;
					return true;
				}
			}
		}
		for (const flag_info * fp(capflags), * const fe(fp + sizeof capflags/sizeof *capflags); fp != fe; ++fp) {
			if (0 == std::strcmp(fp->name, a)) {
				(off ? capflags_off : capflags_on) |= fp->bits;
				return true;
			}
			if (0 == std::strcmp(fp->name, original)) {
				capflags_on |= fp->bits;
				return true;
			}
		}
		return false;
	}

	inline
	void
	initialize (
		sockaddr_storage & addr,
		int address_family
	) {
		addr.ss_family = address_family;
#if !defined(__LINUX__) && !defined(__linux__)
		switch (address_family) {
			case AF_INET:	addr.ss_len = sizeof(sockaddr_in); break;
			case AF_INET6:	addr.ss_len = sizeof(sockaddr_in6); break;
#if defined(AF_PACKET)
			case AF_PACKET:	addr.ss_len = sizeof(sockaddr_ll); break;
#endif
#if defined(AF_LINK)
			case AF_LINK:	addr.ss_len = sizeof(sockaddr_dl); break;
#endif
		}
#endif
	}

	bool
	parse_address (
		const char * ip,
		int address_family,
		sockaddr_storage & addr
	) {
		switch (address_family) {
			case AF_INET:
			{
				struct sockaddr_in & addr4(reinterpret_cast<struct sockaddr_in &>(addr));
				initialize(addr, address_family);
				if (0 < inet_pton(address_family, ip, &addr4.sin_addr))
					return true;
				// This bodge allows people to supply us with old-style hexadecimal integer netmasks.
				const char * p(ip);
				const unsigned long u(std::strtoul(p, const_cast<char **>(&p), 0));
				if (!*p && p != ip) {
					addr4.sin_addr.s_addr = htobe32(uint32_t(u));
					return true;
				}
				break;
			}
			case AF_INET6:
			{
				struct sockaddr_in6 & addr6(reinterpret_cast<struct sockaddr_in6 &>(addr));
				initialize(addr, address_family);
				if (0 < inet_pton(address_family, ip, &addr6.sin6_addr))
					return true;
				break;
			}
#if defined(AF_PACKET)
			case AF_PACKET:
			{
				struct sockaddr_ll & addrl(reinterpret_cast<struct sockaddr_ll &>(addr));
				initialize(addr, address_family);
#if 0	// Not yet
				if (0 < link_pton(address_family, ip, &addrl.sin6_addr))
					return true;
#endif
				break;
			}
#endif
#if defined(AF_LINK)
			case AF_LINK:
			{
				struct sockaddr_dl & addrl(reinterpret_cast<struct sockaddr_dl &>(addr));
				initialize(addr, address_family);
				link_addr(ip, &addrl);
				return true;
			}
#endif
		}
		return false;
	}

	inline
	bool
	initialize_netmask (
		int address_family,
		sockaddr_storage & addr,
		unsigned long prefixlen
	) {
		switch (address_family) {
			case AF_INET:
			{
				struct sockaddr_in & addr4(reinterpret_cast<struct sockaddr_in &>(addr));
				if (prefixlen > sizeof addr4.sin_addr * CHAR_BIT) return false;
				initialize(addr, address_family);
				IPAddress::SetPrefix(addr4.sin_addr, prefixlen);
				return true;
			}
			case AF_INET6:
			{
				struct sockaddr_in6 & addr6(reinterpret_cast<struct sockaddr_in6 &>(addr));
				if (prefixlen > sizeof addr6.sin6_addr * CHAR_BIT) return false;
				initialize(addr, address_family);
				IPAddress::SetPrefix(addr6.sin6_addr, prefixlen);
				return true;
			}
			// Explicitly unsupported for AF_LINK and AF_PACKET
		}
		return false;
	}

	bool
	parse_prefixlen (
		const char * a,
		int address_family,
		sockaddr_storage & addr
	) {
		const char * old(a);
		const unsigned long prefixlen(std::strtoul(a, const_cast<char **>(&a), 0));
		if (*a || old == a) return false;
		return initialize_netmask(address_family, addr, prefixlen);
	}

	void
	calculate_broadcast (
		sockaddr_storage & broadaddr,
		int address_family,
		const sockaddr_storage & addr,
		const sockaddr_storage & netmask
	) {
		switch (address_family) {
			case AF_INET:
			{
				struct sockaddr_in & broadaddr4(reinterpret_cast<struct sockaddr_in &>(broadaddr));
				const struct sockaddr_in & addr4(reinterpret_cast<const struct sockaddr_in &>(addr));
				const struct sockaddr_in & netmask4(reinterpret_cast<const struct sockaddr_in &>(netmask));
				initialize(broadaddr, address_family);
				broadaddr4.sin_addr.s_addr = (addr4.sin_addr.s_addr & netmask4.sin_addr.s_addr) | ~netmask4.sin_addr.s_addr;
				break;
			}
			case AF_INET6:
			{
				struct sockaddr_in6 & broadaddr6(reinterpret_cast<struct sockaddr_in6 &>(broadaddr));
				const struct sockaddr_in6 & addr6(reinterpret_cast<const struct sockaddr_in6 &>(addr));
				const struct sockaddr_in6 & netmask6(reinterpret_cast<const struct sockaddr_in6 &>(netmask));
				initialize(broadaddr, address_family);
				broadaddr6.sin6_addr = (addr6.sin6_addr & netmask6.sin6_addr) | ~netmask6.sin6_addr;
				break;
			}
			// Explicitly unsupported for AF_LINK and AF_PACKET
		}
	}

	bool
	fill_in_eui64 (
		const char * prog,
		const ProcessEnvironment & envs,
		const char * interface_name,
		sockaddr_in6 & dest
	) {
		ifaddrs * al(nullptr);
		if (0 > getifaddrs(&al)) {
			die_errno(prog, envs, "getifaddrs");
		}
		const InterfaceAddresses addr_list(al);
		for (ifaddrs * a(addr_list); a; a = a->ifa_next) {
			if (0 == std::strcmp(a->ifa_name, interface_name)
			&&  a->ifa_addr
			&&  AF_INET6 == a->ifa_addr->sa_family
			) {
				const struct sockaddr_in6 & src(reinterpret_cast<struct sockaddr_in6 &>(*a->ifa_addr));
				if (IPAddress::IsLinkLocal(src.sin6_addr)) {
					std::memcpy(dest.sin6_addr.s6_addr + 8, src.sin6_addr.s6_addr + 8, 8);
					return true;
				}
			}
		}
		return false;
	}

	void
	parse_addresses_flags_and_options (
		const char * prog,
		const ProcessEnvironment & envs,
		std::vector<const char *> & args,
		const char * family_name,
		const char * interface_name,
		int address_family,
		uint_least32_t & flags_on,
		uint_least32_t & flags_off,
		uint_least32_t & ifflags_on,
		uint_least32_t & ifflags_off,
		uint_least32_t & in6flags_on,
		uint_least32_t & in6flags_off,
		uint_least32_t & nd6flags_on,
		uint_least32_t & nd6flags_off,
		uint_least32_t & capflags_on,
		uint_least32_t & capflags_off,
		sockaddr_storage & addr,
		sockaddr_storage & netmask,
		sockaddr_storage & broadaddr,
		sockaddr_storage & destaddr,
		unsigned long & scope
	) {
		bool eui64(false), broadcast1(false);
		bool done_addr(false), done_netmask(false), done_broadaddr(false), done_destaddr(false), done_scope(false);
		// Parse the arguments.
		while (!args.empty()) {
			if (parse_flag(args.front(), address_family, flags_on, flags_off, ifflags_on, ifflags_off, in6flags_on, in6flags_off, nd6flags_on, nd6flags_off, capflags_on, capflags_off, eui64, broadcast1)) {
				args.erase(args.begin());
			} else
			if (0 == std::strcmp("netmask", args.front())) {
				args.erase(args.begin());
				if (args.empty()) {
					die_missing_argument(prog, envs, "address");
				} else
				if (!parse_address(args.front(), address_family, netmask)) {
					die_invalid_argument(prog, envs, args.front(), "Expected a netmask address.");
				} else
				if (done_netmask) {
					die_invalid_argument(prog, envs, args.front(), "Multiple netmasks/prefixlens.");
				} else
					done_netmask = true;
				args.erase(args.begin());
			} else
			if (0 == std::strcmp("prefixlen", args.front())) {
				args.erase(args.begin());
				if (args.empty()) {
					die_missing_argument(prog, envs, "number");
				} else
				if (!parse_prefixlen(args.front(), address_family, netmask)) {
					die_invalid_argument(prog, envs, args.front(), "Expected a prefix length.");
				} else
				if (done_netmask) {
					die_invalid_argument(prog, envs, args.front(), "Multiple netmasks/prefixlens.");
				} else
					done_netmask = true;
				args.erase(args.begin());
			} else
			if (0 == std::strcmp("broadcast", args.front())) {
				args.erase(args.begin());
				ifflags_on |= IFF_BROADCAST;
				ifflags_off |= IFF_POINTOPOINT;
				if (args.empty()) {
					die_missing_argument(prog, envs, "address");
				} else
				if (!parse_address(args.front(), address_family, broadaddr)) {
					die_invalid_argument(prog, envs, args.front(), "Expected a broadcast address.");
				} else
				if (done_broadaddr) {
					die_invalid_argument(prog, envs, args.front(), "Multiple broadcast addresses.");
				} else
					done_broadaddr = true;
				args.erase(args.begin());
			} else
			if (0 == std::strcmp("dest", args.front())) {
				args.erase(args.begin());
				ifflags_on |= IFF_POINTOPOINT;
				ifflags_off |= IFF_BROADCAST;
				if (args.empty()) {
					die_missing_argument(prog, envs, "address");
				} else
				if (!parse_address(args.front(), address_family, destaddr)) {
					die_invalid_argument(prog, envs, args.front(), "Expected a destination address.");
				}
				if (done_destaddr) {
					die_invalid_argument(prog, envs, args.front(), "Multiple point-to-point addresses.");
				} else
					done_destaddr = true;
				args.erase(args.begin());
			} else
			if (0 == std::strcmp("scope", args.front())) {
				args.erase(args.begin());
				if (args.empty()) {
					die_missing_argument(prog, envs, "scope");
				} else
				{
					const char * p(args.front());
#if defined(__LINUX__) || defined(__linux__)
					if (0 == std::strcmp(p, "host")) {
						scope = RT_SCOPE_HOST;
					} else
					if (0 == std::strcmp(p, "link-local")) {
						scope = RT_SCOPE_LINK;
					} else
					if (0 == std::strcmp(p, "global")) {
						scope = RT_SCOPE_UNIVERSE;
					} else
					if (0 == std::strcmp(p, "site")) {
						scope = RT_SCOPE_SITE;
					} else
#endif
					{
						scope = std::strtoul(p, const_cast<char **>(&p), 0);
						if (*p || args.front() == p) {
							die_invalid_argument(prog, envs, args.front(), "Expected a scope ID.");
						}
					}
					if (done_scope) {
						die_invalid_argument(prog, envs, args.front(), "Multiple scopes.");
					} else
						done_scope = true;
					args.erase(args.begin());
				}
			} else
			{
				if (0 == std::strcmp("address", args.front())) {
					args.erase(args.begin());
				}
				if (args.empty()) {
					die_missing_argument(prog, envs, "address");
				} else
				{
					const char * slash(done_netmask ? nullptr : std::strchr(args.front(), '/'));
					if (slash)
						*const_cast<char *>(slash) = '\0';
					if (!parse_address(args.front(), address_family, addr)) {
						die_invalid_argument(prog, envs, args.front(), "Expected an address (or a flag).");
					}
					if (slash) {
						*const_cast<char *>(slash) = '/';
						if (!parse_prefixlen(slash + 1, address_family, netmask)) {
							die_invalid_argument(prog, envs, slash + 1, "Expected a prefix length.");
						}
						if (done_netmask) {
							die_invalid_argument(prog, envs, args.front(), "Multiple netmasks/prefixlens.");
						} else
							done_netmask = true;
					}
					if (done_addr) {
						die_invalid_argument(prog, envs, args.front(), "Multiple addresses.");
					} else
						done_addr = true;
					args.erase(args.begin());
				}
			}
		}

		// Sanity checks and completions.
		if ((flags_on & ALIAS) && (flags_off & ALIAS)) {
			die_invalid_argument(prog, envs, family_name, "It makes no sense to both add and delete an alias.");
		} else
		if ((flags_on & DEFAULTIF) && (flags_off & DEFAULTIF)) {
			die_invalid_argument(prog, envs, family_name, "It makes no sense to make something both the default and not the default.");
		} else
		if (done_broadaddr && done_destaddr) {
			die_invalid_argument(prog, envs, family_name, "Broadcast and point-to-point are mutually exclusive.");
		} else
		if (!done_addr) {
			if (done_netmask || done_scope || done_broadaddr || done_destaddr) {
				die_invalid_argument(prog, envs, family_name, "Netmask/prefixlen/scope/broadcast/dest used without address.");
			}
		} else
		{
			if (eui64) {
				if (AF_INET6 != address_family) {
					die_invalid_argument(prog, envs, family_name, "EUI-64 only applies to IP version 6.");
				}
				unsigned prefixlen;
				if (!done_netmask || !IPAddress::IsPrefix(reinterpret_cast<sockaddr &>(netmask), prefixlen) || prefixlen > 64U) {
					die_invalid_argument(prog, envs, family_name, "EUI-64 requires a prefixlen of 64 or less.");
				}
				if (!fill_in_eui64(prog, envs, interface_name, reinterpret_cast<struct sockaddr_in6 &>(addr))) {
					die_invalid_argument(prog, envs, family_name, "Cannot find an existing link-local address for EUI-64.");
				}
			}
			if (!done_netmask) {
				switch (address_family) {
					case AF_INET:	initialize_netmask(address_family, netmask, 32); break;
					case AF_INET6:	initialize_netmask(address_family, netmask, 128); break;
#if defined(AF_PACKET)
					case AF_PACKET:	initialize_netmask(address_family, netmask, 48); break;
#endif
#if defined(AF_LINK)
					case AF_LINK:	initialize_netmask(address_family, netmask, 48); break;
#endif
					default:
						die_invalid_argument(prog, envs, family_name, "Do not know how to default netmask/prefixlen.");
				}
			}
			if (broadcast1) {
				if (done_broadaddr) {
					die_invalid_argument(prog, envs, family_name, "broadcast and broadcast1 are mutually exclusive.");
				}
				// We are guaranteed to have a netmask at this point.
				calculate_broadcast(broadaddr, address_family, addr, netmask);
				done_broadaddr = true;
			}
#if defined(__LINUX__) || defined(__linux__) || defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
			// In Linux, FreeBSD, and OpenBSD implementations, ifa_broadaddr and ifa_dstaddr are aliases for a single value.
			if (done_destaddr)
				broadaddr = destaddr;
			else
			if (done_broadaddr)
				destaddr = broadaddr;
#endif
		}
	}

}

/* Creating and destroying interfaces ***************************************
// **************************************************************************
*/

namespace {

#if defined(SIOCIFCREATE2) && defined(SIOCIFDESTROY)

	inline
	void
	clone_create (
		const char * prog,
		const ProcessEnvironment & envs,
		const char * interface_name
	) {
		const FileDescriptorOwner s(socket_close_on_exec(AF_INET, SOCK_DGRAM, 0));
		if (0 > s.get()) {
	fail:
			die_errno(prog, envs, interface_name);
		}
		ifreq request = {};
		std::strncpy(request.ifr_name, interface_name, sizeof(request.ifr_name));
		if (0 > ioctl(s.get(), SIOCIFCREATE2, &request)) goto fail;
	}

	inline
	void
	clone_destroy (
		const char * prog,
		const ProcessEnvironment & envs,
		const char * interface_name
	) {
		const FileDescriptorOwner s(socket_close_on_exec(AF_INET, SOCK_DGRAM, 0));
		if (0 > s.get()) {
	fail:
			die_errno(prog, envs, interface_name);
		}
		ifreq request = {};
		std::strncpy(request.ifr_name, interface_name, sizeof(request.ifr_name));
		if (0 > ioctl(s.get(), SIOCIFDESTROY, &request)) goto fail;
	}

#elif defined(SIOCIFCREATE) && defined(SIOCIFDESTROY)

	inline
	void
	clone_create (
		const char * prog,
		const ProcessEnvironment & envs,
		const char * interface_name
	) {
		const FileDescriptorOwner s(socket_close_on_exec(AF_INET, SOCK_DGRAM, 0));
		if (0 > s.get()) {
	fail:
			die_errno(prog, envs, interface_name);
		}
		ifreq request = {};
		std::strncpy(request.ifr_name, interface_name, sizeof(request.ifr_name));
		if (0 > ioctl(s.get(), SIOCIFCREATE, &request)) goto fail;
	}

	inline
	void
	clone_destroy (
		const char * prog,
		const ProcessEnvironment & envs,
		const char * interface_name
	) {
		const FileDescriptorOwner s(socket_close_on_exec(AF_INET, SOCK_DGRAM, 0));
		if (0 > s.get()) {
	fail:
			die_errno(prog, envs, interface_name);
		}
		ifreq request = {};
		std::strncpy(request.ifr_name, interface_name, sizeof(request.ifr_name));
		if (0 > ioctl(s.get(), SIOCIFDESTROY, &request)) goto fail;
	}

#else

	inline
	void
	clone_create (
		const char * /*prog*/,
		const ProcessEnvironment & /*envs*/,
		const char * /*interface_name*/
	) {
	}

	inline
	void
	clone_destroy (
		const char * /*prog*/,
		const ProcessEnvironment & /*envs*/,
		const char * /*interface_name*/
	) {
	}

#endif

}

/* Adding and removing addresses ********************************************
// **************************************************************************
*/

#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__) || defined(__NetBSD__)
namespace {

	typedef sockaddr_in sockaddr_in4;

#if defined(SIOCDIFADDR)
	void
	delete_address (
		const char * prog,
		const ProcessEnvironment & envs,
		const char * interface_name,
		const sockaddr_in4 & addr,
		const sockaddr_in4 & netmask,
		const sockaddr_in4 & broadaddr,
		const sockaddr_in4 & destaddr
	) {
		const FileDescriptorOwner s(socket_close_on_exec(AF_INET, SOCK_DGRAM, 0));
		if (0 > s.get()) {
	fail:
			die_errno(prog, envs, interface_name);
		}
		in_aliasreq request = {};
		std::strncpy(request.ifra_name, interface_name, sizeof(request.ifra_name));
		request.ifra_addr = addr;
		request.ifra_mask = netmask;
		request.ifra_broadaddr = broadaddr;
		request.ifra_dstaddr = destaddr;
		if (0 > ioctl(s.get(), SIOCDIFADDR, &request)) goto fail;
	}
#endif

#if defined(SIOCDIFADDR_IN6)
	void
	delete_address (
		const char * prog,
		const ProcessEnvironment & envs,
		const char * interface_name,
		const sockaddr_in6 & addr,
		const sockaddr_in6 & netmask,
		const sockaddr_in6 & broadaddr,
		const sockaddr_in6 & destaddr
	) {
		const FileDescriptorOwner s(socket_close_on_exec(AF_INET6, SOCK_DGRAM, 0));
		if (0 > s.get()) {
	fail:
			die_errno(prog, envs, interface_name);
		}
		in6_aliasreq request = {};
		std::strncpy(request.ifra_name, interface_name, sizeof(request.ifra_name));
		request.ifra_addr = addr;
		request.ifra_prefixmask = netmask;
		request.ifra_broadaddr = broadaddr;
		request.ifra_dstaddr = destaddr;
		if (0 > ioctl(s.get(), SIOCDIFADDR_IN6, &request)) goto fail;
	}
#endif

	void
	delete_address (
		const char * prog,
		const ProcessEnvironment & envs,
		int address_family,
		const char * family_name,
		const char * interface_name,
		sockaddr_storage addr,
		const sockaddr_storage & netmask,
		const sockaddr_storage & broadaddr,
		const sockaddr_storage & destaddr,
		unsigned short /*addr_flags*/,
		unsigned long scope
	) {
		switch (address_family) {
#if defined(SIOCDIFADDR)
			case AF_INET:
			{
				const sockaddr_in4 & addr4(reinterpret_cast<const sockaddr_in4 &>(addr));
				const sockaddr_in4 & netmask4(reinterpret_cast<const sockaddr_in4 &>(netmask));
				const sockaddr_in4 & broadaddr4(reinterpret_cast<const sockaddr_in4 &>(broadaddr));
				const sockaddr_in4 & destaddr4(reinterpret_cast<const sockaddr_in4 &>(destaddr));
				delete_address(prog, envs, interface_name, addr4, netmask4, broadaddr4, destaddr4);
				break;
			}
#endif
#if defined(SIOCDIFADDR_IN6)
			case AF_INET6:
			{
				sockaddr_in6 & addr6(reinterpret_cast<sockaddr_in6 &>(addr));
				const sockaddr_in6 & netmask6(reinterpret_cast<const sockaddr_in6 &>(netmask));
				const sockaddr_in6 & broadaddr6(reinterpret_cast<const sockaddr_in6 &>(broadaddr));
				const sockaddr_in6 & destaddr6(reinterpret_cast<const sockaddr_in6 &>(destaddr));
				addr6.sin6_scope_id = scope;
				delete_address(prog, envs, interface_name, addr6, netmask6, broadaddr6, destaddr6);
				break;
			}
#endif
			default:
				die_invalid_argument(prog, envs, family_name, "Do not know how to delete addresses in this family.");
		}
	}

#if defined(SIOCDIFADDR)
	void
	add_address (
		const char * prog,
		const ProcessEnvironment & envs,
		const char * interface_name,
		const sockaddr_in4 & addr,
		const sockaddr_in4 & netmask,
		const sockaddr_in4 & broadaddr,
		const sockaddr_in4 & destaddr
	) {
		const FileDescriptorOwner s(socket_close_on_exec(AF_INET, SOCK_DGRAM, 0));
		if (0 > s.get()) {
	fail:
			die_errno(prog, envs, interface_name);
		}
		in_aliasreq request = {};
		std::strncpy(request.ifra_name, interface_name, sizeof(request.ifra_name));
		request.ifra_addr = addr;
		request.ifra_mask = netmask;
		request.ifra_broadaddr = broadaddr;
		request.ifra_dstaddr = destaddr;
		if (0 > ioctl(s.get(), SIOCAIFADDR, &request)) goto fail;
	}
#endif

#if defined(SIOCAIFADDR_IN6)
	void
	add_address (
		const char * prog,
		const ProcessEnvironment & envs,
		const char * interface_name,
		const sockaddr_in6 & addr,
		const sockaddr_in6 & netmask,
		const sockaddr_in6 & broadaddr,
		const sockaddr_in6 & destaddr,
		unsigned short addr_flags
	) {
		const FileDescriptorOwner s(socket_close_on_exec(AF_INET6, SOCK_DGRAM, 0));
		if (0 > s.get()) {
	fail:
			die_errno(prog, envs, interface_name);
		}
		in6_aliasreq request = {};
		std::strncpy(request.ifra_name, interface_name, sizeof(request.ifra_name));
		request.ifra_addr = addr;
		request.ifra_prefixmask = netmask;
		request.ifra_broadaddr = broadaddr;
		request.ifra_dstaddr = destaddr;
		request.ifra_flags = addr_flags;
		request.ifra_lifetime.ia6t_vltime = ND6_INFINITE_LIFETIME;
		request.ifra_lifetime.ia6t_pltime = ND6_INFINITE_LIFETIME;
		if (0 > ioctl(s.get(), SIOCAIFADDR_IN6, &request)) goto fail;
	}
#endif

	void
	add_address (
		const char * prog,
		const ProcessEnvironment & envs,
		int address_family,
		const char * family_name,
		const char * interface_name,
		sockaddr_storage addr,
		const sockaddr_storage & netmask,
		const sockaddr_storage & broadaddr,
		const sockaddr_storage & destaddr,
		unsigned short addr_flags,
		unsigned long scope
	) {
		switch (address_family) {
#if defined(SIOCAIFADDR)
			case AF_INET:
			{
				const sockaddr_in4 & addr4(reinterpret_cast<const sockaddr_in4 &>(addr));
				const sockaddr_in4 & netmask4(reinterpret_cast<const sockaddr_in4 &>(netmask));
				const sockaddr_in4 & broadaddr4(reinterpret_cast<const sockaddr_in4 &>(broadaddr));
				const sockaddr_in4 & destaddr4(reinterpret_cast<const sockaddr_in4 &>(destaddr));
				add_address(prog, envs, interface_name, addr4, netmask4, broadaddr4, destaddr4);
				break;
			}
#endif
#if defined(SIOCAIFADDR_IN6)
			case AF_INET6:
			{
				sockaddr_in6 & addr6(reinterpret_cast<sockaddr_in6 &>(addr));
				const sockaddr_in6 & netmask6(reinterpret_cast<const sockaddr_in6 &>(netmask));
				const sockaddr_in6 & broadaddr6(reinterpret_cast<const sockaddr_in6 &>(broadaddr));
				const sockaddr_in6 & destaddr6(reinterpret_cast<const sockaddr_in6 &>(destaddr));
				addr6.sin6_scope_id = scope;
				add_address(prog, envs, interface_name, addr6, netmask6, broadaddr6, destaddr6, addr_flags);
				break;
			}
#endif
			default:
				die_invalid_argument(prog, envs, family_name, "Do not know how to add addresses in this family.");
		}
	}

}
#endif

#if defined(__LINUX__) || defined(__linux__)
namespace {

	struct rtnl_im {
		nlmsghdr h;
		ifaddrmsg m;
	};
	struct rtnl_em {
		nlmsghdr h;
		nlmsgerr e;
	};

	void
	append_attribute (
		char * buf,
		std::size_t & len,
		unsigned short type,
		const sockaddr_storage & addr
	) {
		rtattr * attr(reinterpret_cast<rtattr *>(buf + len));
		attr->rta_type = type;
		attr->rta_len = 0;
		switch (addr.ss_family) {
			case AF_INET:
			{
				const struct sockaddr_in & addr4(reinterpret_cast<const struct sockaddr_in &>(addr));
				attr->rta_len = RTA_LENGTH(sizeof addr4.sin_addr);
				std::memcpy(RTA_DATA(attr), &addr4.sin_addr, sizeof addr4.sin_addr);
				break;
			}
			case AF_INET6:
			{
				const struct sockaddr_in6 & addr6(reinterpret_cast<const struct sockaddr_in6 &>(addr));
				attr->rta_len = RTA_LENGTH(sizeof addr6.sin6_addr);
				std::memcpy(RTA_DATA(attr), &addr6.sin6_addr, sizeof addr6.sin6_addr);
				break;
			}
			case AF_PACKET:
			{
				const struct sockaddr_ll & addrl(reinterpret_cast<const struct sockaddr_ll &>(addr));
				attr->rta_len = RTA_LENGTH(addrl.sll_halen);
				std::memcpy(RTA_DATA(attr), &addrl.sll_addr[0], addrl.sll_halen);
				break;
			}
		}
		len += attr->rta_len;
	}

	void
	send_rtnetlink (
		const char * prog,
		const ProcessEnvironment & envs,
		int address_family,
		const char * family_name,
		const char * interface_name,
		unsigned short nl_flags,
		unsigned short type,
		const sockaddr_storage & addr,
		const sockaddr_storage & netmask,
		const sockaddr_storage & broadaddr,
		const sockaddr_storage & destaddr,
		unsigned short addr_flags,
		unsigned long scope
	) {
		unsigned prefixlen;
		if (!IPAddress::IsPrefix(reinterpret_cast<const sockaddr &>(netmask), prefixlen)) {
			die_invalid_argument(prog, envs, family_name, "Network mask is not a valid prefix.");
		}
		const int interface_index(if_nametoindex(interface_name));
		if (interface_index <= 0)
			die_invalid(prog, envs, family_name, "Cannot obtain the index of that interface.");

		const FileDescriptorOwner s(socket_close_on_exec(AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE));
		if (0 > s.get()) {
	fail:
			die_errno(prog, envs, interface_name);
		}

		char buf[4096];
		rtnl_im & msg(*reinterpret_cast<rtnl_im *>(buf));
		std::size_t buflen(NLMSG_ALIGN(NLMSG_LENGTH(sizeof msg.m)));

		if (address_family == addr.ss_family)
			append_attribute(buf, buflen, IFA_LOCAL, addr);
		if (address_family == broadaddr.ss_family)
			append_attribute(buf, buflen, IFA_BROADCAST, broadaddr);
		if (address_family == destaddr.ss_family)
			append_attribute(buf, buflen, IFA_ADDRESS, destaddr);	// That IFA_ADDRESS is the point-to-point dest is hidden in a comment in an obscure header.

		msg.h.nlmsg_len = buflen;
		msg.h.nlmsg_flags = NLM_F_REQUEST|NLM_F_ACK|nl_flags;
		msg.h.nlmsg_type = type;
		msg.m.ifa_family = address_family;
		msg.m.ifa_prefixlen = prefixlen;
		msg.m.ifa_flags = addr_flags;
		msg.m.ifa_index = interface_index;
		msg.m.ifa_scope = scope;

		if (0 > send(s.get(), buf, buflen, 0)) goto fail;
		const int rc(recv(s.get(), buf, sizeof buf, 0));
		if (0 > rc) goto fail;
		const nlmsgerr & e(reinterpret_cast<const rtnl_em *>(buf)->e);
		if (e.error)
			die_errno(prog, envs, -e.error, interface_name);
	}

	inline
	void
	delete_address (
		const char * prog,
		const ProcessEnvironment & envs,
		int address_family,
		const char * family_name,
		const char * interface_name,
		const sockaddr_storage & addr,
		const sockaddr_storage & netmask,
		const sockaddr_storage & broadaddr,
		const sockaddr_storage & destaddr,
		unsigned short addr_flags,
		unsigned long scope
	) {
		send_rtnetlink (prog, envs, address_family, family_name, interface_name, 0, RTM_DELADDR, addr, netmask, broadaddr, destaddr, addr_flags, scope);
	}

	inline
	void
	add_address (
		const char * prog,
		const ProcessEnvironment & envs,
		int address_family,
		const char * family_name,
		const char * interface_name,
		const sockaddr_storage & addr,
		const sockaddr_storage & netmask,
		const sockaddr_storage & broadaddr,
		const sockaddr_storage & destaddr,
		unsigned short addr_flags,
		unsigned long scope
	) {
		send_rtnetlink (prog, envs, address_family, family_name, interface_name, NLM_F_CREATE|NLM_F_REPLACE, RTM_NEWADDR, addr, netmask, broadaddr, destaddr, addr_flags, scope);
	}

}
#endif

/* Changing flags ***********************************************************
// **************************************************************************
*/

namespace {

	void
	set_flags_and_options (
		const char * prog,
		const ProcessEnvironment & envs,
		int address_family,
		const char * interface_name,
		uint_least32_t ifflags_on,
		uint_least32_t ifflags_off,
		uint_least32_t nd6flags_on,
		uint_least32_t nd6flags_off,
		uint_least32_t capflags_on,
		uint_least32_t capflags_off
	) {
		const FileDescriptorOwner s(socket_close_on_exec(address_family, SOCK_DGRAM, 0));
		if (0 > s.get()) {
	fail:
			die_errno(prog, envs, interface_name);
		}
		if (ifflags_on || ifflags_off) {
			ifreq request = {};
			std::strncpy(request.ifr_name, interface_name, sizeof(request.ifr_name));
			if (0 > ioctl(s.get(), SIOCGIFFLAGS, &request)) goto fail;
#if defined(__FreeBSD__) || defined(__DragonFly__)
			uint_least32_t f(uint16_t(request.ifr_flags) | (uint_least32_t(request.ifr_flagshigh) << 16U));
			f |= ifflags_on;
			f &= ~ifflags_off;
			request.ifr_flags = uint16_t(f);
			request.ifr_flagshigh = uint16_t(f >> 16U);
#else
			request.ifr_flags |= ifflags_on;
			request.ifr_flags &= ~ifflags_off;
#endif
			if (0 > ioctl(s.get(), SIOCSIFFLAGS, &request)) goto fail;
		}
#if defined(SIOCGIFINFO_IN6) && defined(SIOCSIFINFO_IN6)
		if (nd6flags_on || nd6flags_off) {
			in6_ndireq request = {};
			std::strncpy(request.ifname, interface_name, sizeof(request.ifname));
			if (0 > ioctl(s.get(), SIOCGIFINFO_IN6, &request)) goto fail;
			request.ndi.flags |= nd6flags_on;
			request.ndi.flags &= ~nd6flags_off;
			if (0 > ioctl(s.get(), SIOCSIFINFO_IN6, &request)) goto fail;
		}
#endif
#if defined(SIOCGIFCAP) && defined(SIOCSIFCAP) && !defined(__NetBSD__)
		if (capflags_on || capflags_off) {
			ifreq request = {};
			std::strncpy(request.ifr_name, interface_name, sizeof(request.ifr_name));
			if (0 > ioctl(s.get(), SIOCGIFCAP, &request)) goto fail;
			int f(request.ifr_curcap);
			f |= capflags_on;
			f &= ~capflags_off;
			request.ifr_reqcap = f;
			if (0 > ioctl(s.get(), SIOCSIFCAP, &request)) goto fail;
		}
#else
		static_cast<void>(capflags_on);		// Silences a compiler warning.
		static_cast<void>(capflags_off);	// Silences a compiler warning.
#endif
	}

}

/* Default IPv6 interface ***************************************************
// **************************************************************************
*/

namespace {

#if defined(SIOCGDEFIFACE_IN6) && defined(SIOCSDEFIFACE_IN6)

	void
	set_defaultif (
		const char * prog,
		const ProcessEnvironment & envs,
		int address_family,
		const char * family_name,
		const char * interface_name,
		bool defaultif_on,
		bool defaultif_off
	) {
		int interface_index(if_nametoindex(interface_name));
		if (interface_index <= 0)
			die_invalid(prog, envs, family_name, "Cannot obtain the index of that interface.");
		const FileDescriptorOwner s(socket_close_on_exec(address_family, SOCK_DGRAM, 0));
		if (0 > s.get()) {
	fail:
			die_errno(prog, envs, interface_name);
		}
		in6_ndifreq request = {};
		std::strncpy(request.ifname, interface_name, sizeof(request.ifname));
		if (defaultif_off) {
			if (0 > ioctl(s.get(), SIOCGDEFIFACE_IN6, &request)) goto fail;
			if (request.ifindex == static_cast<unsigned int>(interface_index)) {
				interface_index = 0;
				defaultif_on = true;
			}
		}
		if (defaultif_on) {
			request.ifindex = interface_index;
			if (0 > ioctl(s.get(), SIOCSDEFIFACE_IN6, &request)) goto fail;
		}
	}

#else

	inline
	void
	set_defaultif (
		const char * /*prog*/,
		const ProcessEnvironment & /*envs*/,
		int /*address_family*/,
		const char * /*family_name*/,
		const char * /*interface_name*/,
		bool /*defaultif_on*/,
		bool /*defaultif_off*/
	) {
	}

#endif

}

/* Main function ************************************************************
// **************************************************************************
*/

namespace {
	const sockaddr_storage zero_storage = {};
}

void
ifconfig [[gnu::noreturn]] (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	bool colours(ECMA48Output::query_use_colours(envs, STDOUT_FILENO));
	const char * prog(basename_of(args[0]));
	bool all(false), up_only(false), down_only(false), clones(false), list(false);
	try {
		popt::bool_definition all_option('a', "all", "Show all interfaces.", all);
		popt::bool_definition up_only_option('u', "up-only", "Only interfaces that are up.", up_only);
		popt::bool_definition down_only_option('d', "down-only", "Only interfaces that are down.", down_only);
		popt::bool_definition clones_option('C', "clones", "List clone interfaces.", clones);
		popt::bool_definition list_option('l', "list", "List all interfaces.", list);
		popt::bool_definition colours_option('\0', "colour", "Force output in colour even if standard output is not a terminal.", colours);
		popt::definition * top_table[] = {
			&all_option,
			&up_only_option,
			&down_only_option,
			&clones_option,
			&list_option,
			&colours_option,
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{interface} [create|destroy] [family] [settings...]");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}

	TerminalCapabilities caps(envs);
	ECMA48Output o(caps, std::cout, true /* C1 is 7-bit aliased */, false /* C1 is not raw 8-bit */);
	if (!colours)
		caps.colour_level = caps.NO_COLOURS;

	if (all || list) {
		if (clones || (list && all)) {
			die_usage(prog, envs, "-a, -C, and -l are mutually exclusive.");
		}
		int address_family(AF_UNSPEC);
		if (!args.empty()) {
			const char * family_name(args.front());
			args.erase(args.begin());
			address_family = get_family(family_name);
			if (AF_UNSPEC == address_family) {
				die_invalid_argument(prog, envs, family_name, "Unknown address family.");
			}
		}
		if (!args.empty()) die_unexpected_argument(prog, args, envs);
		output_listing(prog, envs, o, list, up_only, down_only, address_family, nullptr);
	} else
	if (clones) {
		if (all || list) {
			die_usage(prog, envs, "-a, -C, and -l are mutually exclusive.");
		}
		if (!args.empty()) die_unexpected_argument(prog, args, envs);
		list_cloner_interfaces (prog, envs);
	} else
	if (args.empty()) {
		die_missing_argument(prog, envs, "interface name, -a, -l, or -C");
	} else
	{
		const char * interface_name(args.front());
		args.erase(args.begin());
		int address_family(AF_UNSPEC);
		if (args.empty()) {
			output_listing(prog, envs, o, list, up_only, down_only, address_family, interface_name);
		} else
		if (0 == std::strcmp(args.front(), "destroy")) {
			args.erase(args.begin());
			if (!args.empty()) die_unexpected_argument(prog, args, envs);
			clone_destroy(prog, envs, interface_name);
		} else
		{
			if (0 == std::strcmp(args.front(), "create")) {
				args.erase(args.begin());
				clone_create(prog, envs, interface_name);
			}
			if (!args.empty()) {
				const char * family_name(args.front());
				args.erase(args.begin());
				address_family = get_family(family_name);
				if (AF_UNSPEC == address_family) {
					die_invalid_argument(prog, envs, family_name, "Unknown address family.");
				} else
				if (args.empty()) {
					output_listing(prog, envs, o, list, up_only, down_only, address_family, interface_name);
				} else
				{
					uint_least32_t flags_on(0U), flags_off(0U);
					uint_least32_t ifflags_on(0U), ifflags_off(0U);
					uint_least32_t in6flags_on(0U), in6flags_off(0U);
					uint_least32_t nd6flags_on(0U), nd6flags_off(0U);
					uint_least32_t capflags_on(0U), capflags_off(0U);
					// Whether an address is set is determined by the sa_family in the sockaddr for the address.
					sockaddr_storage addr = zero_storage, netmask = zero_storage, broadaddr = zero_storage, destaddr = zero_storage;
					unsigned long scope(0UL);

					parse_addresses_flags_and_options(prog, envs, args, family_name, interface_name, address_family, flags_on, flags_off, ifflags_on, ifflags_off, in6flags_on, in6flags_off, nd6flags_on, nd6flags_off, capflags_on, capflags_off, addr, netmask, broadaddr, destaddr, scope);

					// Actually enact stuff.
					if (ifflags_on || ifflags_off || nd6flags_on || nd6flags_off || capflags_on || capflags_off)
						set_flags_and_options(prog, envs, address_family, interface_name, ifflags_on, ifflags_off, nd6flags_on, nd6flags_off, capflags_on, capflags_off);
					if ((flags_on & DEFAULTIF) || (flags_off & DEFAULTIF))
						set_defaultif (prog, envs, address_family, family_name, interface_name, flags_on & DEFAULTIF, flags_off & DEFAULTIF);
					if (address_family == addr.ss_family) {
						const unsigned long addr_flags(in6flags_on & ~in6flags_off);
						if (flags_off & ALIAS)
							delete_address(prog, envs, address_family, family_name, interface_name, addr, netmask, broadaddr, destaddr, addr_flags, scope);
						else
							add_address(prog, envs, address_family, family_name, interface_name, addr, netmask, broadaddr, destaddr, addr_flags, scope);
					}
				}
			}
		}
	}

	throw EXIT_SUCCESS;
}
