/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <map>
#include <vector>
#include <iostream>
#include <iomanip>
#include <cstdio>
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
#include <endian.h>
#else
#include <sys/endian.h>
#endif
#include <unistd.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <netinet/ip.h>
#include <netinet/in.h>
#if defined(__FreeBSD__) || defined(__DragonFly__)
#include <net/if_dl.h>
#include <net/if_var.h>	
#include <netinet/in_var.h>
struct prf_ra : public in6_prflags::prf_ra {};
#include <netinet6/in6_var.h>
#include <netinet6/nd6.h>
#endif
#if defined(__OpenBSD__)
#include <net/if_dl.h>
#include <net/if_var.h>	
#include <netinet/in_var.h>
#include <netinet6/in6_var.h>
#include <netinet6/nd6.h>
#endif
#if defined(__LINUX__) || defined(__linux__)
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/if_link.h>
#include <linux/if_packet.h>
#endif
#include "popt.h"
#include "utils.h"
#include "fdutils.h"
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
		~InterfaceAddresses() { freeifaddrs(addr_list); addr_list = 0; }
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
#if defined(__FreeBSD__) || defined(__DragonFly__)
		{	"anycast",		IN6_IFF_ANYCAST		},
		{	"tentative",		IN6_IFF_TENTATIVE	},
		{	"deprecated",		IN6_IFF_DEPRECATED	},
		{	"autoconf",		IN6_IFF_AUTOCONF	},
		{	"prefer_source",	IN6_IFF_PREFER_SOURCE	},
#endif
	}, nd6flags[] = {
#if defined(__FreeBSD__) || defined(__DragonFly__)
		{	"performnud",		ND6_IFF_PERFORMNUD		},
		{	"accept_rtadv",		ND6_IFF_ACCEPT_RTADV		},
		{	"prefer_source",	ND6_IFF_PREFER_SOURCE		},
		{	"ifdisabled",		ND6_IFF_IFDISABLED		},
		{	"auto_linklocal",	ND6_IFF_AUTO_LINKLOCAL		},
		{	"no_radr",		ND6_IFF_NO_RADR			},
		{	"no_prefer_iface",	ND6_IFF_NO_PREFER_IFACE		},
		{	"no_set_ifroute",	ND6_IFF_DONT_SET_IFROUTE	},
		{	"no_dad",		ND6_IFF_NO_DAD			},
#endif
	}, capflags[] = {
#if defined(__FreeBSD__) || defined(__DragonFly__)
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
		{	"polling_nocount",	IFCAP_POLLING_NOCOUNT	},
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
#if defined(__LINUX__) || defined(__linux__)
		{	"link",		AF_PACKET	},
		{	"ether",	AF_PACKET	},
		{	"packet",	AF_PACKET	},
		{	"lladr",	AF_PACKET	},
		{	"lladdr",	AF_PACKET	},
#endif
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
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
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
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
		return 0;
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

#if defined(__FreeBSD__) || defined(__DragonFly__)
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
				if (0 != inet_ntop(addr4.sin_family, &addr4.sin_addr, ip, sizeof ip)) {
					std::fputs(prefix, o.file());
					o.SGRColour(true, Map256Colour(COLOUR_MAGENTA));
					std::fputs(ip, o.file());
					o.SGRColour(true);
					std::fputc(' ', o.file());
				}
				break;
			}
			case AF_INET6:
			{
				const struct sockaddr_in6 & addr6(reinterpret_cast<const struct sockaddr_in6 &>(addr));
				char ip[INET6_ADDRSTRLEN];
				if (0 != inet_ntop(addr6.sin6_family, &addr6.sin6_addr, ip, sizeof ip)) {
					std::fputs(prefix, o.file());
					o.SGRColour(true, Map256Colour(COLOUR_CYAN));
					std::fputs(ip, o.file());
					o.SGRColour(true);
					std::fprintf(o.file(), " scope %u", addr6.sin6_scope_id);
					std::fputc(' ', o.file());
				}
				break;
			}
			case AF_LOCAL:
			{
				const struct sockaddr_un & addru(reinterpret_cast<const struct sockaddr_un &>(addr));
				std::fputs(prefix, o.file());
				o.SGRColour(true, Map256Colour(COLOUR_BLUE));
				std::fputs(addru.sun_path, o.file());
				o.SGRColour(true);
				std::fputc(' ', o.file());
				break;
			}
#if defined(__LINUX__) || defined(__linux__)
			case AF_PACKET:
			{
				const struct sockaddr_ll & addrl(reinterpret_cast<const struct sockaddr_ll &>(addr));
				std::fputs(prefix, o.file());
				o.SGRColour(true, Map256Colour(COLOUR_YELLOW));
				for (std::size_t i(0U); i < addrl.sll_halen; ++i) {
					if (i) std::fputc(':', o.file());
					std::fprintf(o.file(), "%02x", unsigned(addrl.sll_addr[i]));
				}
				o.SGRColour(true);
				std::fputc(' ', o.file());
				break;
			}
#endif
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
			case AF_LINK:
			{
				const struct sockaddr_dl & addrl(reinterpret_cast<const struct sockaddr_dl &>(addr));
				std::fputs(prefix, o.file());
				o.SGRColour(true, Map256Colour(COLOUR_YELLOW));
				for (std::size_t i(0U); i < addrl.sdl_alen; ++i) {
					if (i) std::fputc(':', o.file());
					std::fprintf(o.file(), "%02x", uint8_t(LLADDR(&addrl)[i]));
				}
				o.SGRColour(true);
				std::fputc(' ', o.file());
				break;
			}
#endif
			default:
				std::fprintf(o.file(), "%ssa_?%d ", prefix, int(addr.sa_family));
				break;
			case AF_UNSPEC:
				break;
		}
	}

#if defined(__LINUX__) || defined(__linux__)
	std::ostream &
	operator << (
		std::ostream & o,
		const rtnl_link_stats & d
	) {
		return o;
	}
#endif

#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
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
#if !defined(__OpenBSD__)
			"physical " << unsigned(d.ifi_physical) << " "
#endif
			"baudrate " << d.ifi_baudrate << " "
		;
		return o;
	}
#endif

	void
	output_listing (
		const char * prog,
		ECMA48Output & o,
		int names_only,
		bool up_only,
		bool down_only,
		int address_family,
		const char * interface_name
	) {
		ifaddrs * al(0);
		if (0 > getifaddrs(&al)) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "getifaddrs", std::strerror(error));
			throw EXIT_FAILURE;
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
			const unsigned int * pflags(0);
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
#if defined(__FreeBSD__) || defined(__DragonFly__)
					uint_least32_t flags6;
					if (get_nd6_flags(a->ifa_name, flags6)) {
						std::cout.put('\t') << "nd6";
						for (const flag_info * const fb(nd6flags), * const fe(fb + sizeof nd6flags/sizeof *nd6flags), * fp(fb); fp != fe; ++fp)
							if (flags6 & fp->bits)
								std::cout.put(' ') << fp->name;
						std::cout.put('\n');
					}
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
				if (const char * f = a->ifa_addr ? get_family_name(a->ifa_addr->sa_family) : 0)
					std::cout << f << ' ';
				else
					std::cout << "unknown-family ";
				if (const sockaddr * addr = a->ifa_addr)
					print(o, "address ", *addr);
				if (const sockaddr * addr = a->ifa_netmask) {
					unsigned prefixlen;
					if (IPAddress::IsPrefix(*addr, prefixlen))
						std::cout << "prefixlen " << prefixlen << ' ';
					else
						print(o, "netmask ", *addr);
				}
#if defined(__LINUX__) || defined(__linux__) || defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
				// In Linux, FreeBSD, and OpenBSD implementations, ifa_broadaddr and ifa_dstaddr are aliases for a single value.
				if (const sockaddr * addr = a->ifa_broadaddr)
					print(o, (a->ifa_flags & IFF_BROADCAST ? "broadcast " : a->ifa_flags & IFF_POINTOPOINT ? "dest " : "bdaddr "), *addr);
#else
				if (const sockaddr * addr = a->ifa_broadaddr)
					print(o, "broadcast ", *addr);
				if (const sockaddr * addr = a->ifa_dstaddr)
					print(o, "dest ", *addr);
#endif
#if defined(__LINUX__) || defined(__linux__)
				if (a->ifa_addr && AF_PACKET == a->ifa_addr->sa_family) {
					if (const void * data = a->ifa_data)
						std::cout << *reinterpret_cast<const rtnl_link_stats *>(data);
				}
#endif
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
				if (a->ifa_addr && AF_LINK == a->ifa_addr->sa_family) {
					if (const void * data = a->ifa_data)
						std::cout << *reinterpret_cast<const if_data *>(data);
				}
#endif
#if defined(__FreeBSD__) || defined(__DragonFly__)
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
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, interface_name, msg);
			throw EXIT_FAILURE;
		}
	}

	void
	list_cloner_interfaces (
		const char * prog
	) {
#if defined(SIOCIFGCLONERS)
		FileDescriptorOwner s(socket_close_on_exec(AF_LOCAL, SOCK_DGRAM, 0));
		if (0 > s.get()) {
	fatal:
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s\n", prog, std::strerror(error));
			throw EXIT_FAILURE;
		}

		if_clonereq request = {};

		request.ifcr_count = 0;
		request.ifcr_buffer = 0;
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
		const bool off(is_negated(a));
		for (const flag_info * fp(flags), * const fe(fp + sizeof flags/sizeof *flags); fp != fe; ++fp) {
			if (0 == std::strcmp(fp->name, a)) {
				(off ? flags_off : flags_on) |= fp->bits;
				return true;
			}
		}
		for (const flag_info * fp(ifflags), * const fe(fp + sizeof ifflags/sizeof *ifflags); fp != fe; ++fp) {
			if (IFF_BROADCAST == fp->bits || IFF_POINTOPOINT == fp->bits) continue;
			if (0 == std::strcmp(fp->name, a)) {
				(off ? ifflags_off : ifflags_on) |= fp->bits;
				return true;
			}
		}
		if (AF_INET6 == address_family) {
			for (const flag_info * fp(nd6flags), * const fe(fp + sizeof nd6flags/sizeof *nd6flags); fp != fe; ++fp) {
				if (0 == std::strcmp(fp->name, a)) {
					(off ? nd6flags_off : nd6flags_on) |= fp->bits;
					return true;
				}
			}
			for (const flag_info * fp(in6flags), * const fe(fp + sizeof in6flags/sizeof *in6flags); fp != fe; ++fp) {
				if (0 == std::strcmp(fp->name, a)) {
					(off ? in6flags_off : in6flags_on) |= fp->bits;
					return true;
				}
			}
		}
		for (const flag_info * fp(capflags), * const fe(fp + sizeof capflags/sizeof *capflags); fp != fe; ++fp) {
			if (0 == std::strcmp(fp->name, a)) {
				(off ? capflags_off : capflags_on) |= fp->bits;
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
#if defined(__LINUX__) || defined(__linux__)
			case AF_PACKET:	addr.ss_len = sizeof(sockaddr_ll); break;
#endif
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
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
#if defined(__LINUX__) || defined(__linux__)
			case AF_PACKET:
			{
				struct sockaddr_ll & addrl(reinterpret_cast<struct sockaddr_ll &>(addr));
				initialize(addr, address_family);
				break;
			}
#endif
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
			case AF_LINK:
			{
				struct sockaddr_dl & addrl(reinterpret_cast<struct sockaddr_dl &>(addr));
				initialize(addr, address_family);
				break;
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
		const char * interface_name,
		sockaddr_in6 & dest
	) {
		ifaddrs * al(0);
		if (0 > getifaddrs(&al)) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "getifaddrs", std::strerror(error));
			throw EXIT_FAILURE;
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
					std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing address.");
					throw static_cast<int>(EXIT_USAGE);
				} else
				if (!parse_address(args.front(), address_family, netmask)) {
					std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Expected a netmask address.");
					throw static_cast<int>(EXIT_USAGE);
				} else
				if (done_netmask) {
					std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Multiple netmasks/prefixlens.");
					throw static_cast<int>(EXIT_USAGE);
				} else
					done_netmask = true;
				args.erase(args.begin());
			} else
			if (0 == std::strcmp("prefixlen", args.front())) {
				args.erase(args.begin());
				if (args.empty()) {
					std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing number.");
					throw static_cast<int>(EXIT_USAGE);
				} else
				if (!parse_prefixlen(args.front(), address_family, netmask)) {
					std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Expected a prefix length.");
					throw static_cast<int>(EXIT_USAGE);
				} else
				if (done_netmask) {
					std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Multiple netmasks/prefixlens.");
					throw static_cast<int>(EXIT_USAGE);
				} else
					done_netmask = true;
				args.erase(args.begin());
			} else
			if (0 == std::strcmp("broadcast", args.front())) {
				args.erase(args.begin());
				ifflags_on |= IFF_BROADCAST;
				ifflags_off |= IFF_POINTOPOINT;
				if (args.empty()) {
					std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing address.");
					throw static_cast<int>(EXIT_USAGE);
				} else
				if (!parse_address(args.front(), address_family, broadaddr)) {
					std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Expected a broadcast address.");
					throw static_cast<int>(EXIT_USAGE);
				} else
				if (done_broadaddr) {
					std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Multiple broadcast addresses.");
					throw static_cast<int>(EXIT_USAGE);
				} else
					done_broadaddr = true;
				args.erase(args.begin());
			} else
			if (0 == std::strcmp("dest", args.front())) {
				args.erase(args.begin());
				ifflags_on |= IFF_POINTOPOINT;
				ifflags_off |= IFF_BROADCAST;
				if (args.empty()) {
					std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing address.");
					throw static_cast<int>(EXIT_USAGE);
				} else
				if (!parse_address(args.front(), address_family, destaddr)) {
					std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Expected a destination address.");
					throw static_cast<int>(EXIT_USAGE);
				}
				if (done_destaddr) {
					std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Multiple point-to-point addresses.");
					throw static_cast<int>(EXIT_USAGE);
				} else
					done_destaddr = true;
				args.erase(args.begin());
			} else
			if (0 == std::strcmp("scope", args.front())) {
				args.erase(args.begin());
				if (args.empty()) {
					std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing scope.");
					throw static_cast<int>(EXIT_USAGE);
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
							std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Expected a scope ID.");
							throw static_cast<int>(EXIT_USAGE);
						}
					}
					if (done_scope) {
						std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Multiple scopes.");
						throw static_cast<int>(EXIT_USAGE);
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
					std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing address.");
					throw static_cast<int>(EXIT_USAGE);
				} else
				{
					const char * slash(done_netmask ? 0 : std::strchr(args.front(), '/'));
					if (slash)
						*const_cast<char *>(slash) = '\0';
					if (!parse_address(args.front(), address_family, addr)) {
						std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Expected an address (or a flag).");
						throw static_cast<int>(EXIT_USAGE);
					}
					if (slash) {
						*const_cast<char *>(slash) = '/';
						if (!parse_prefixlen(slash + 1, address_family, netmask)) {
							std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, slash + 1, "Expected a prefix length.");
							throw static_cast<int>(EXIT_USAGE);
						}
						if (done_netmask) {
							std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Multiple netmasks/prefixlens.");
							throw static_cast<int>(EXIT_USAGE);
						} else
							done_netmask = true;
					}
					if (done_addr) {
						std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Multiple addresses.");
						throw static_cast<int>(EXIT_USAGE);
					} else
						done_addr = true;
					args.erase(args.begin());
				}
			}
		}

		// Sanity checks and completions.
		if ((flags_on & ALIAS) && (flags_off & ALIAS)) {
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, family_name, "It makes no sense to both add and delete an alias.");
			throw static_cast<int>(EXIT_USAGE);
		} else
		if ((flags_on & DEFAULTIF) && (flags_off & DEFAULTIF)) {
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, family_name, "It makes no sense to make something both the default and not the default.");
			throw static_cast<int>(EXIT_USAGE);
		} else
		if (done_broadaddr && done_destaddr) {
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, family_name, "Broadcast and point-to-point are mutually exclusive.");
			throw static_cast<int>(EXIT_USAGE);
		} else
		if (!done_addr) {
			if (done_netmask || done_scope || done_broadaddr || done_destaddr) {
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, family_name, "Netmask/prefixlen/scope/broadcast/dest used without address.");
				throw static_cast<int>(EXIT_USAGE);
			}
		} else
		{
			if (eui64) {
				if (AF_INET6 != address_family) {
					std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, family_name, "EUI-64 only applies to IP version 6.");
					throw static_cast<int>(EXIT_USAGE);
				}
				unsigned prefixlen;
				if (!done_netmask || !IPAddress::IsPrefix(reinterpret_cast<sockaddr &>(netmask), prefixlen) || prefixlen > 64U) {
					std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, family_name, "EUI-64 requires a prefixlen of 64 or less.");
					throw static_cast<int>(EXIT_USAGE);
				}
				if (!fill_in_eui64(prog, interface_name, reinterpret_cast<struct sockaddr_in6 &>(addr))) {
					std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, family_name, "Cannot find an existing link-local address for EUI-64.");
					throw static_cast<int>(EXIT_USAGE);
				}
			}
			if (!done_netmask) {
				switch (address_family) {
					case AF_INET:	initialize_netmask(address_family, netmask, 32); break;
					case AF_INET6:	initialize_netmask(address_family, netmask, 128); break;
#if defined(__LINUX__) || defined(__linux__)
					case AF_PACKET:	initialize_netmask(address_family, netmask, 48); break;
#endif
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
					case AF_LINK:	initialize_netmask(address_family, netmask, 48); break;
#endif
					default:
						std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, family_name, "Do not know how to default netmask/prefixlen.");
						throw static_cast<int>(EXIT_USAGE);
				}
			}
			if (broadcast1) {
				if (done_broadaddr) {
					std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, family_name, "broadcast and broadcast1 are mutually exclusive.");
					throw static_cast<int>(EXIT_USAGE);
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

#if defined(__FreeBSD__) || defined(__DragonFly__)
namespace {

	inline
	void
	clone_create (
		const char * prog,
		const char * interface_name
	) {
		const FileDescriptorOwner s(socket_close_on_exec(AF_INET, SOCK_DGRAM, 0));
		if (0 > s.get()) {
	fail:
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: clone_create: %s: %s\n", prog, interface_name, std::strerror(error));
			throw EXIT_FAILURE;
		}
		ifreq request = {};
		std::strncpy(request.ifr_name, interface_name, sizeof(request.ifr_name));
		if (0 > ioctl(s.get(), SIOCIFCREATE2, &request)) goto fail;
	}

	inline
	void
	clone_destroy (
		const char * prog,
		const char * interface_name
	) {
		const FileDescriptorOwner s(socket_close_on_exec(AF_INET, SOCK_DGRAM, 0));
		if (0 > s.get()) {
	fail:
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: clone_destroy: %s: %s\n", prog, interface_name, std::strerror(error));
			throw EXIT_FAILURE;
		}
		ifreq request = {};
		std::strncpy(request.ifr_name, interface_name, sizeof(request.ifr_name));
		if (0 > ioctl(s.get(), SIOCIFDESTROY, &request)) goto fail;
	}

}
#endif

#if defined(__OpenBSD__)
namespace {

	inline
	void
	clone_create (
		const char * prog,
		const char * interface_name
	) {
		const FileDescriptorOwner s(socket_close_on_exec(AF_INET, SOCK_DGRAM, 0));
		if (0 > s.get()) {
	fail:
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: clone_create: %s: %s\n", prog, interface_name, std::strerror(error));
			throw EXIT_FAILURE;
		}
		ifreq request = {};
		std::strncpy(request.ifr_name, interface_name, sizeof(request.ifr_name));
		if (0 > ioctl(s.get(), SIOCIFCREATE, &request)) goto fail;
	}

	inline
	void
	clone_destroy (
		const char * prog,
		const char * interface_name
	) {
		const FileDescriptorOwner s(socket_close_on_exec(AF_INET, SOCK_DGRAM, 0));
		if (0 > s.get()) {
	fail:
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: clone_destroy: %s: %s\n", prog, interface_name, std::strerror(error));
			throw EXIT_FAILURE;
		}
		ifreq request = {};
		std::strncpy(request.ifr_name, interface_name, sizeof(request.ifr_name));
		if (0 > ioctl(s.get(), SIOCIFDESTROY, &request)) goto fail;
	}

}
#endif

#if defined(__LINUX__) || defined(__linux__)
namespace {

	inline
	void
	clone_create (
		const char * /*prog*/,
		const char * /*interface_name*/
	) {
	}

	inline
	void
	clone_destroy (
		const char * /*prog*/,
		const char * /*interface_name*/
	) {
	}

}
#endif

/* Adding and removing addresses ********************************************
// **************************************************************************
*/

#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
namespace {

#if false && defined(__OpenBSD__)
	typedef sockaddr sockaddr_in4;
#else
	typedef sockaddr_in sockaddr_in4;
#endif

	void
	delete_address (
		const char * prog,
		const char * interface_name,
		const sockaddr_in4 & addr,
		const sockaddr_in4 & netmask,
		const sockaddr_in4 & broadaddr,
		const sockaddr_in4 & destaddr
	) {
		const FileDescriptorOwner s(socket_close_on_exec(AF_INET, SOCK_DGRAM, 0));
		if (0 > s.get()) {
	fail:
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: delete address: %s\n", prog, interface_name, std::strerror(error));
			throw EXIT_FAILURE;
		}
		in_aliasreq request = {};
		std::strncpy(request.ifra_name, interface_name, sizeof(request.ifra_name));
		request.ifra_addr = addr;
		request.ifra_mask = netmask;
		request.ifra_broadaddr = broadaddr;
		request.ifra_dstaddr = destaddr;
		if (0 > ioctl(s.get(), SIOCDIFADDR, &request)) goto fail;
	}

	void
	delete_address (
		const char * prog,
		const char * interface_name,
		const sockaddr_in6 & addr,
		const sockaddr_in6 & netmask,
		const sockaddr_in6 & broadaddr,
		const sockaddr_in6 & destaddr
	) {
		const FileDescriptorOwner s(socket_close_on_exec(AF_INET6, SOCK_DGRAM, 0));
		if (0 > s.get()) {
	fail:
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: delete address: %s\n", prog, interface_name, std::strerror(error));
			throw EXIT_FAILURE;
		}
		in6_aliasreq request = {};
		std::strncpy(request.ifra_name, interface_name, sizeof(request.ifra_name));
		request.ifra_addr = addr;
		request.ifra_prefixmask = netmask;
		request.ifra_broadaddr = broadaddr;
		request.ifra_dstaddr = destaddr;
		if (0 > ioctl(s.get(), SIOCDIFADDR_IN6, &request)) goto fail;
	}

	void
	delete_address (
		const char * prog,
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
			case AF_INET:
			{
				const sockaddr_in4 & addr4(reinterpret_cast<const sockaddr_in4 &>(addr));
				const sockaddr_in4 & netmask4(reinterpret_cast<const sockaddr_in4 &>(netmask));
				const sockaddr_in4 & broadaddr4(reinterpret_cast<const sockaddr_in4 &>(broadaddr));
				const sockaddr_in4 & destaddr4(reinterpret_cast<const sockaddr_in4 &>(destaddr));
				delete_address(prog, interface_name, addr4, netmask4, broadaddr4, destaddr4);
				break;
			}
			case AF_INET6:
			{
				sockaddr_in6 & addr6(reinterpret_cast<sockaddr_in6 &>(addr));
				const sockaddr_in6 & netmask6(reinterpret_cast<const sockaddr_in6 &>(netmask));
				const sockaddr_in6 & broadaddr6(reinterpret_cast<const sockaddr_in6 &>(broadaddr));
				const sockaddr_in6 & destaddr6(reinterpret_cast<const sockaddr_in6 &>(destaddr));
				addr6.sin6_scope_id = scope;
				delete_address(prog, interface_name, addr6, netmask6, broadaddr6, destaddr6);
				break;
			}
			default:
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, family_name, "Do not know how to delete addresses in this family.");
				throw static_cast<int>(EXIT_USAGE);
		}
	}

	void
	add_address (
		const char * prog,
		const char * interface_name,
		const sockaddr_in4 & addr,
		const sockaddr_in4 & netmask,
		const sockaddr_in4 & broadaddr,
		const sockaddr_in4 & destaddr
	) {
		const FileDescriptorOwner s(socket_close_on_exec(AF_INET, SOCK_DGRAM, 0));
		if (0 > s.get()) {
	fail:
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: add address: %s\n", prog, interface_name, std::strerror(error));
			throw EXIT_FAILURE;
		}
		in_aliasreq request = {};
		std::strncpy(request.ifra_name, interface_name, sizeof(request.ifra_name));
		request.ifra_addr = addr;
		request.ifra_mask = netmask;
		request.ifra_broadaddr = broadaddr;
		request.ifra_dstaddr = destaddr;
		if (0 > ioctl(s.get(), SIOCAIFADDR, &request)) goto fail;
	}

	void
	add_address (
		const char * prog,
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
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: add address: %s\n", prog, interface_name, std::strerror(error));
			throw EXIT_FAILURE;
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

	void
	add_address (
		const char * prog,
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
			case AF_INET:
			{
				const sockaddr_in4 & addr4(reinterpret_cast<const sockaddr_in4 &>(addr));
				const sockaddr_in4 & netmask4(reinterpret_cast<const sockaddr_in4 &>(netmask));
				const sockaddr_in4 & broadaddr4(reinterpret_cast<const sockaddr_in4 &>(broadaddr));
				const sockaddr_in4 & destaddr4(reinterpret_cast<const sockaddr_in4 &>(destaddr));
				add_address(prog, interface_name, addr4, netmask4, broadaddr4, destaddr4);
				break;
			}
			case AF_INET6:
			{
				sockaddr_in6 & addr6(reinterpret_cast<sockaddr_in6 &>(addr));
				const sockaddr_in6 & netmask6(reinterpret_cast<const sockaddr_in6 &>(netmask));
				const sockaddr_in6 & broadaddr6(reinterpret_cast<const sockaddr_in6 &>(broadaddr));
				const sockaddr_in6 & destaddr6(reinterpret_cast<const sockaddr_in6 &>(destaddr));
				addr6.sin6_scope_id = scope;
				add_address(prog, interface_name, addr6, netmask6, broadaddr6, destaddr6, addr_flags);
				break;
			}
			default:
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, family_name, "Do not know how to add addresses in this family.");
				throw static_cast<int>(EXIT_USAGE);
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
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, family_name, "Network mask is not a valid prefix.");
			throw static_cast<int>(EXIT_USAGE);
		}
		const int interface_index(if_nametoindex(interface_name));
		if (interface_index <= 0) {
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, family_name, "Cannot obtain the index of that interface.");
			throw EXIT_FAILURE;
		}

		const FileDescriptorOwner s(socket_close_on_exec(AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE));
		if (0 > s.get()) {
	fail:
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: netlink: %s\n", prog, interface_name, std::strerror(error));
			throw EXIT_FAILURE;
		}

		char buf[4096];
		rtnl_im & msg(*reinterpret_cast<rtnl_im *>(buf));
		std::size_t buflen(NLMSG_ALIGN(NLMSG_LENGTH(sizeof msg.m)));

		if (address_family == addr.ss_family)
			append_attribute(buf, buflen, IFA_LOCAL, addr);
		if (address_family == broadaddr.ss_family)
			append_attribute(buf, buflen, IFA_BROADCAST, broadaddr);
		if (address_family == destaddr.ss_family)
			append_attribute(buf, buflen, IFA_ADDRESS, addr);	// That IFA_ADDRESS is the point-to-point dest is hidden in a comment in an obscure header.

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
		if (e.error) {
			std::fprintf(stderr, "%s: FATAL: %s: netlink: %s\n", prog, interface_name, std::strerror(-e.error));
			throw EXIT_FAILURE;
		}
	}

	inline
	void
	delete_address (
		const char * prog,
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
		send_rtnetlink (prog, address_family, family_name, interface_name, 0, RTM_DELADDR, addr, netmask, broadaddr, destaddr, addr_flags, scope);
	}

	inline
	void
	add_address (
		const char * prog,
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
		send_rtnetlink (prog, address_family, family_name, interface_name, NLM_F_CREATE|NLM_F_REPLACE, RTM_NEWADDR, addr, netmask, broadaddr, destaddr, addr_flags, scope);
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
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, interface_name, std::strerror(error));
			throw EXIT_FAILURE;
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
#if defined(__FreeBSD__) || defined(__DragonFly__)
		if (nd6flags_on || nd6flags_off) {
			in6_ndireq request = {};
			std::strncpy(request.ifname, interface_name, sizeof(request.ifname));
			if (0 > ioctl(s.get(), SIOCGIFINFO_IN6, &request)) goto fail;
			request.ndi.flags |= nd6flags_on;
			request.ndi.flags &= ~nd6flags_off;
			if (0 > ioctl(s.get(), SIOCSIFINFO_IN6, &request)) goto fail;
		}
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
#endif
	}
}

/* Default IPv6 interface ***************************************************
// **************************************************************************
*/

#if defined(__FreeBSD__) || defined(__DragonFly__)
namespace {

	void
	set_defaultif (
		const char * prog,
		int address_family,
		const char * family_name,
		const char * interface_name,
		bool defaultif_on,
		bool defaultif_off
	) {
		int interface_index(if_nametoindex(interface_name));
		if (interface_index <= 0) {
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, family_name, "Cannot obtain the index of that interface.");
			throw EXIT_FAILURE;
		}
		const FileDescriptorOwner s(socket_close_on_exec(address_family, SOCK_DGRAM, 0));
		if (0 > s.get()) {
	fail:
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, interface_name, std::strerror(error));
			throw EXIT_FAILURE;
		}
		in6_ndifreq request = {};
		std::strncpy(request.ifname, interface_name, sizeof(request.ifname));
		if (defaultif_off) {
			if (0 > ioctl(s.get(), SIOCGDEFIFACE_IN6, &request)) goto fail;
			if (request.ifindex == interface_index) {
				interface_index = 0;
				defaultif_on = true;
			}
		}
		if (defaultif_on) {
			request.ifindex = interface_index;
			if (0 > ioctl(s.get(), SIOCSDEFIFACE_IN6, &request)) goto fail;
		}
	}

}
#endif

#if defined(__OpenBSD__) || defined(__LINUX__) || defined(__linux__)
namespace {

	inline
	void
	set_defaultif (
		const char * /*prog*/,
		int /*address_family*/,
		const char * /*family_name*/,
		const char * /*interface_name*/,
		bool /*defaultif_on*/,
		bool /*defaultif_off*/
	) {
	}

}
#endif

/* Main function ************************************************************
// **************************************************************************
*/

namespace {
	const sockaddr_storage zero_storage = {};
}

void
ifconfig [[gnu::noreturn]] 
( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	bool colours(isatty(STDOUT_FILENO));
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
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw static_cast<int>(EXIT_USAGE);
	}

	TerminalCapabilities caps(envs);
	ECMA48Output o(caps, stdout, true /* C1 is 7-bit aliased */);
	if (!colours)
		caps.colour_level = caps.NO_COLOURS;

	if (all || list) {
		if (clones || (list && all)) {
			std::fprintf(stderr, "%s: FATAL: %s\n", prog, "-a, -C, and -l are mutually exclusive.");
			throw static_cast<int>(EXIT_USAGE);
		}
		int address_family(AF_UNSPEC);
		if (!args.empty()) {
			const char * family_name(args.front());
			args.erase(args.begin());
			address_family = get_family(family_name);
			if (AF_UNSPEC == address_family) {
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, family_name, "Unknown address family.");
				throw static_cast<int>(EXIT_USAGE);
			}
		}
		if (!args.empty()) {
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Unexpected argument.");
			throw static_cast<int>(EXIT_USAGE);
		}
		output_listing(prog, o, list, up_only, down_only, address_family, 0);
	} else
	if (clones) {
		if (all || list) {
			std::fprintf(stderr, "%s: FATAL: %s\n", prog, "-a, -C, and -l are mutually exclusive.");
			throw static_cast<int>(EXIT_USAGE);
		}
		if (!args.empty()) {
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Unexpected argument.");
			throw static_cast<int>(EXIT_USAGE);
		}
		list_cloner_interfaces (prog);
	} else
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing interface name, -a, -l, or -C.");
		throw static_cast<int>(EXIT_USAGE);
	} else
	{
		const char * interface_name(args.front());
		args.erase(args.begin());
		int address_family(AF_UNSPEC);
		if (args.empty()) {
			output_listing(prog, o, list, up_only, down_only, address_family, interface_name);
		} else 
		if (0 == std::strcmp(args.front(), "destroy")) {
			args.erase(args.begin());
			if (!args.empty()) {
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Unexpected argument.");
				throw static_cast<int>(EXIT_USAGE);
			}
			clone_destroy(prog, interface_name);
		} else
		{
			if (0 == std::strcmp(args.front(), "create")) {
				args.erase(args.begin());
				clone_create(prog, interface_name);
			}
			if (!args.empty()) {
				const char * family_name(args.front());
				args.erase(args.begin());
				address_family = get_family(family_name);
				if (AF_UNSPEC == address_family) {
					std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, family_name, "Unknown address family.");
					throw static_cast<int>(EXIT_USAGE);
				} else
				if (args.empty()) {
					output_listing(prog, o, list, up_only, down_only, address_family, interface_name);
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

					parse_addresses_flags_and_options(prog, args, family_name, interface_name, address_family, flags_on, flags_off, ifflags_on, ifflags_off, in6flags_on, in6flags_off, nd6flags_on, nd6flags_off, capflags_on, capflags_off, addr, netmask, broadaddr, destaddr, scope);

					// Actually enact stuff.
					if (ifflags_on || ifflags_off || nd6flags_on || nd6flags_off || capflags_on || capflags_off)
						set_flags_and_options(prog, address_family, interface_name, ifflags_on, ifflags_off, nd6flags_on, nd6flags_off, capflags_on, capflags_off);
					if ((flags_on & DEFAULTIF) || (flags_off & DEFAULTIF))
						set_defaultif (prog, address_family, family_name, interface_name, flags_on & DEFAULTIF, flags_off & DEFAULTIF);
					if (address_family == addr.ss_family) {
						const unsigned long addr_flags(in6flags_on & ~in6flags_off);
						if (flags_off & ALIAS)
							delete_address(prog, address_family, family_name, interface_name, addr, netmask, broadaddr, destaddr, addr_flags, scope);
						else
							add_address(prog, address_family, family_name, interface_name, addr, netmask, broadaddr, destaddr, addr_flags, scope);
					}
				}
			}
		}
	}

	throw EXIT_SUCCESS;
}
