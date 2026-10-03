/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <list>
#include <map>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <iostream>
#include <sstream>
#include <unistd.h>
#include <cstdlib>
#include "config/hasutmpx.h"
#include "config/hasutmpxaddr.h"
#include "config/hasutmpxexit.h"
#include "config/hasutmpxsession.h"
#include "config/hasutmpxss.h"
#include "config/hasutmp.h"
#if defined(HAS_UTMPX)
#include <utmpx.h>
#elif defined(HAS_UTMP)
#include <utmp.h>
#include <paths.h>
#else
#error "Don't know how to enumerate logged in users on your platform."
#endif
#if defined(HAS_UTMPX_ADDR) || defined(HAS_UTMPX_SS)
#include <arpa/inet.h>
#include <netinet/in.h>
#endif
#include "popt.h"
#include "utils.h"
#include "u32string.h"
#include "VisEncoder.h"
#include "FileStar.h"

/* Internals ****************************************************************
// **************************************************************************
*/

namespace {

typedef std::list<std::string> FieldList;
typedef std::map<std::string, std::string> Record;
typedef std::list<Record> Table;

template<typename T>
std::string
str (
	const T & x
) {
	std::ostringstream os;
	os << x;
	return os.str();
}

std::string
timestr (
	const time_t & t,
	const char * time_format
) {
	tm b;
	localtime_r(&t, &b);
	char buf[64];
	std::size_t len(std::strftime(buf, sizeof buf, time_format, &b));
	return std::string(buf, len);
}

#if !defined(__GLIBC__) || !__WORDSIZE_TIME64_COMPAT32
std::string
timestr (
	const struct timeval & t,
	const char * time_format
) {
	return timestr(t.tv_sec, time_format);
}
#endif

#if defined(HAS_UTMPX_ADDR)
std::string
addrstr (
	const in6_addr & host_a
) {
	in6_addr net_a;
	for (std::size_t i(0U); i < sizeof net_a.s6_addr32/sizeof *net_a.s6_addr32; ++i)
		net_a.s6_addr32[sizeof net_a.s6_addr32/sizeof *net_a.s6_addr32 - 1U - i] = host_a.s6_addr32[i];
	char addr[INET6_ADDRSTRLEN];
	if (nullptr == inet_ntop(AF_INET6, &net_a, addr, sizeof addr))
		addr[0] = '\0';
	return addr;
}
#endif

#if defined(HAS_UTMPX_SS)
std::string
addrstr (
	const sockaddr & addr
) {
	switch (addr.sa_family) {
		case AF_INET:
		{
			const struct sockaddr_in & addr4(reinterpret_cast<const struct sockaddr_in &>(addr));
			char ip[INET_ADDRSTRLEN];
			if (nullptr == inet_ntop(addr4.sin_family, &addr4.sin_addr, ip, sizeof ip))
				ip[0] = '\0';
			return ip;
		}
		case AF_INET6:
		{
			const struct sockaddr_in6 & addr6(reinterpret_cast<const struct sockaddr_in6 &>(addr));
			char ip[INET6_ADDRSTRLEN];
			if (nullptr == inet_ntop(addr6.sin6_family, &addr6.sin6_addr, ip, sizeof ip))
				ip[0] = '\0';
			return ip;
		}
		default:
			return std::string();
	}
}
#endif

std::string
rtrim (
	const char * s,
	std::size_t l
) {
	while (l > 0) {
		--l;
		if (s[l] && !std::isspace(s[l]))
			return std::string(s, l + 1);
	}
	return std::string();
}

Table
read_table (
	const char * time_format,
	const bool no_init
) {
	Table r;
#if defined(HAS_UTMPX)
	setutxent();
	while (struct utmpx * u = getutxent()) {
		if (INIT_PROCESS != u->ut_type && LOGIN_PROCESS != u->ut_type && USER_PROCESS != u->ut_type) continue;
		if (no_init && (INIT_PROCESS == u->ut_type || LOGIN_PROCESS == u->ut_type)) continue;
		r.push_back(Record());
		Record & b(r.back());
		b["id"] = rtrim(u->ut_id, sizeof u->ut_id);
		b["line"] = rtrim(u->ut_line, sizeof u->ut_line);
		b["name"] = rtrim(u->ut_user, sizeof u->ut_user);
		b["host"] = rtrim(u->ut_host, sizeof u->ut_host);
		b["pid"] = str(u->ut_pid);
		switch (u->ut_type) {
			case INIT_PROCESS:	b["type"] = "init";	break;
			case USER_PROCESS:	b["type"] = "user";	break;
			case LOGIN_PROCESS:	b["type"] = "login";	break;
			default:		break;	// Never happens because of earlier test.
		}
#if defined(__GLIBC__) && __WORDSIZE_TIME64_COMPAT32
		b["time"] = timestr(u->ut_tv.tv_sec, time_format);
#else
		b["time"] = timestr(u->ut_tv, time_format);
#endif
#if defined(HAS_UTMPX_SESSION)
		b["session"] = str(u->ut_session);
#endif
#if defined(HAS_UTMPX_EXIT)
		b["exit"] = str(u->ut_exit.e_exit);
		b["termination"] = str(u->ut_exit.e_termination);
#endif
#if defined(HAS_UTMPX_ADDR)
		b["address"] = addrstr(*reinterpret_cast<const in6_addr *>(u->ut_addr_v6));
#endif
#if defined(HAS_UTMPX_SS)
		b["address"] = addrstr(*reinterpret_cast<const sockaddr *>(&u->ut_ss));
#endif
	}
	endutxent();
#elif defined(HAS_UTMP)
	static_cast<void>(no_init);	// Silence a compiler warning.
	const FileStar file(std::fopen(_PATH_UTMP, "r"));
	if (file) {
		struct utmp u;
		for (;;) {
			const size_t n(std::fread(&u, sizeof u, 1, file));
			if (n < 1) break;
			if (!u.ut_name[0] || !u.ut_line[0]) continue;
			r.push_back(Record());
			Record & b(r.back());
			b["line"] = rtrim(u.ut_line, sizeof u.ut_line);
			b["name"] = rtrim(u.ut_name, sizeof u.ut_name);
			b["host"] = rtrim(u.ut_host, sizeof u.ut_host);
#if defined(__GLIBC__) && __WORDSIZE_TIME64_COMPAT32
			b["time"] = timestr(u.ut_tv.tv_sec, time_format);
#else
			b["time"] = timestr(u.ut_time, time_format);
#endif
		}
	}
#else
#error "Don't know how to enumerate logged in users on your platform."
#endif
	return r;
}

inline
std::string
display_name_for (
	const std::string & s
) {
	std::string r(s);
	for (std::string::iterator e(r.end()), p(r.begin()); p != e; ++p) {
		if (std::isalpha(*p))
			*p = std::toupper(*p);
	}
	return r;
}

}

/* Main function ************************************************************
// **************************************************************************
*/

void
list_logins [[gnu::noreturn]]  (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	std::vector<const char *> next_args;
	FieldList fields;
	const char * time_format("%F %T %z");
	bool no_init(false);
	try {
		popt::string_list_definition field_option('F', "field", "field", "Include this field.", fields);
		popt::string_definition time_format_option('\0', "time-format", "format-string", "Use an alternative time display format.", time_format);
		popt::bool_definition no_init_option('\0', "no-init-records", "Do not include INIT records.", no_init);
		popt::definition * top_table[] = {
			&field_option,
			&time_format_option,
			&no_init_option,
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}

	Table table(read_table(time_format, no_init));

	// Print the headings.
	for (FieldList::const_iterator b(fields.begin()), e(fields.end()), i(b); i != e; ++i) {
		if (i != b) std::cout.put('\t');
		std::cout << display_name_for(*i);
	}
	std::cout.put('\n');

	// Filter the lines that are wanted.
	if (!args.empty()) {
		for (Table::const_iterator te(table.end()), tp(table.begin()); tp != te; ) {
			const Record & r(*tp);
			const Record::const_iterator line(r.find("line"));
			if (line == r.end()) {
				tp = table.erase(tp);
				continue;
			}
			bool wanted(false);
			for (std::vector<const char *>::const_iterator ap(args.begin()), ae(args.end()); ap != ae; ++ap) {
				if (line->second == *ap) {
					wanted = true;
					break;
				}
			}
			if (wanted)
				++tp;
			else
				tp = table.erase(tp);
		}
	}

	// Print the table rows.
	for (Table::const_iterator te(table.end()), tp(table.begin()); tp != te; ++tp) {
		const Record & r(*tp);
		for (FieldList::const_iterator fb(fields.begin()), fe(fields.end()), f(fb); f != fe; ++f) {
			if (f != fb) std::cout.put('\t');
			const Record::const_iterator v(r.find(*f));
			if (v != r.end()) {
				std::string s;
				ConvertToUTF8(s, v->second);
				std::cout << VisEncoder::process(s);
			}
		}
		std::cout.put('\n');
	}

	throw EXIT_SUCCESS;
}
