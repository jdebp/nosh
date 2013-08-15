/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <cctype>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <stdint.h>
#if !defined(_GNU_SOURCE)
#include <sys/syslimits.h>
#endif
#include <sys/mount.h>
#include <unistd.h>
#if !defined(__LINUX__) && !defined(__linux__)
#include <sys/types.h>
#include <sys/sysctl.h>
#include <netinet/in.h>
#include "uuid.h"
#else
#include "uuid/uuid.h"
#endif
#include "utils.h"
#include "jail.h"
#include "popt.h"
#include "CubeHash.h"

/* bottom level functions ***************************************************
// **************************************************************************
*/

static const char volatile_filename[] = "/run/machine-id";
static const char non_volatile_filename[] = "/etc/machine-id";
#if !defined(__LINUX__) && !defined(__linux__)
static const char volatile_hostuuid_filename[] = "/run/hostid";
static const char non_volatile_hostuuid_filename[] = "/etc/hostid";
#else
static const char volatile_hostid_filename[] = "/run/hostid";
static const char non_volatile_hostid_filename[] = "/etc/hostid";
#endif
static uuid_t machine_id;
static uint32_t hostid(0UL);

#if !defined(__LINUX__) && !defined(__linux__)
static
uuid_t
uuid_to_guid (
	const uuid_t & u
) {
	uuid_t g(u);
	g.time_low = ntohl(u.time_low);
	g.time_mid = ntohs(u.time_mid);
	g.time_hi_and_version = ntohs(u.time_hi_and_version);
	return g;
}

static
uuid_t
guid_to_uuid (
	const uuid_t & g
) {
	uuid_t u(g);
	u.time_low = htonl(g.time_low);
	u.time_mid = htons(g.time_mid);
	u.time_hi_and_version = htons(g.time_hi_and_version);
	return u;
}
#endif

static inline 
int 
x2c ( int c ) 
{
	if (std::isdigit(c)) return c - '0';
	if (std::isalpha(c)) {
		c = std::tolower(c);
		return c - 'a' + 10;
	}
	return EOF;
}

static inline 
int 
c2x ( int c ) 
{
	if (EOF == c || c < 0) return EOF;
	if (c < 10) return c + '0';
	if (c < 36) return c + 'a' - 10;
	return EOF;
}

static
bool
read_first_line_machine_id (
	std:: istream & i
) {
	if (i.fail()) return false;
#if defined(__LINUX__) || defined(__linux__)
	uuid_clear(machine_id);
#else
	uint32_t status;
	uuid_create_nil(&machine_id, &status);
#endif
	unsigned char * m(reinterpret_cast<unsigned char *>(&machine_id));
	for (unsigned int p(0U); p < 32U; ++p) {
		const int c(i.get());
		unsigned char & b(m[p / 2]);
		if (i.fail()) return false;
		if (!std::isxdigit(c)) return false;
		b <<= 4;
		b |= static_cast<unsigned>(x2c(c)) & 0x0F;
	}
	const int c(i.get());
	return i.eof() || '\n' == c;
}

static
bool
read_first_line_machine_id (
	const char * filename
) {
	std::ifstream s(filename);
	if (s.fail()) return false;
	return read_first_line_machine_id(s);
}

static
bool
read_uuid (
	const char * p
) {
#if defined(__LINUX__) || defined(__linux__)
	return 0 <= uuid_parse(p, machine_id);
#else
	uint32_t status;
	uuid_t guid;
	uuid_from_string(p, &guid, &status);
	machine_id = guid_to_uuid(guid);
	return uuid_s_ok == status;
#endif
}

static
bool
read_first_line_uuid (
	std:: istream & i
) {
	if (i.fail()) return false;
	char buf[37];
	i.getline(buf, sizeof buf, '\n');
	if (i.fail()) return false;
	return read_uuid(buf);
}

static
bool
read_first_line_uuid (
	const char * filename
) {
	std::ifstream s(filename);
	if (s.fail()) return false;
	return read_first_line_uuid(s);
}

static
void
write_one_line (
	std:: ostream & i
) {
	unsigned char * m(reinterpret_cast<unsigned char *>(&machine_id));
	for (std::size_t n(0U); n < sizeof machine_id; ++n) {
		const unsigned char b(m[n]);
		i.put(c2x((b >> 4) & 0x0F)).put(c2x(b & 0x0F));
	}
	i.put('\n');
}

/* inline functions *********************************************************
// **************************************************************************
*/

static inline
bool
read_container_id ()
{
	if (const char * c = std::getenv("container_uuid"))
		return read_uuid(c);
	return false;
}

static inline
bool
read_boot_id ()
{
#if defined(__LINUX__) || defined(__linux__)
	return read_first_line_uuid("/sys/class/dmi/id/product_uuid");
#else
	int oid[CTL_MAXNAME];
	std::size_t len = sizeof oid/sizeof *oid;
	if (0 > sysctlnametomib("smbios.system.uuid", oid, &len)) return false;
	std::size_t siz(sizeof machine_id);
	if (0 > sysctl(oid, len, &machine_id, &siz, 0, 0)) return false;
	return true;
#endif
}

static inline
bool
read_non_volatile ()
{
	return read_first_line_machine_id(non_volatile_filename);
}

static inline
void
write_non_volatile (
	const char * prog
) {
	std::ofstream s(non_volatile_filename);
	if (!s.fail()) {
		write_one_line(s);
		return;
	}
	if ((EROFS != errno)
#if defined(MS_BIND)
		|| (0 > mount(volatile_filename, non_volatile_filename, "bind", MS_BIND, 0))
#endif
	   ) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, non_volatile_filename, std::strerror(error));
		throw EXIT_FAILURE;
	}
}

static inline
bool
read_volatile ()
{
	return read_first_line_machine_id(volatile_filename);
}

static inline
void
write_volatile (
	const char * prog
) {
	std::ofstream s(volatile_filename);
	if (!s.fail()) {
		write_one_line(s);
		return;
	}
	const int error(errno);
	std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, volatile_filename, std::strerror(error));
	throw EXIT_FAILURE;
}

static inline
bool
read_host_uuid ()
{
#if defined(__LINUX__) || defined(__linux__)
	return false;
#else
	int oid[2] = {CTL_KERN, KERN_HOSTUUID};
	std::size_t siz(0);
	if (0 > sysctl(oid, sizeof oid/sizeof *oid, 0, &siz, 0, 0)) return false;
	char * buf(static_cast<char *>(malloc(siz)));
	if (!buf) return false;
	if (0 > sysctl(oid, sizeof oid/sizeof *oid, buf, &siz, 0, 0)) {
		free(buf);
		return false;
	}
	const bool r(read_uuid(buf));
	free(buf);
	return r;
#endif
}

static inline
void
write_host_uuid (
	const char * prog
) {
#if !defined(__LINUX__) && !defined(__linux__)
	int oid[2] = {CTL_KERN, KERN_HOSTUUID};
	char * buf(0);
	uint32_t status;
	const uuid_t guid(uuid_to_guid(machine_id));
	uuid_to_string(&guid, &buf, &status);
	if (0 > sysctl(oid, sizeof oid/sizeof *oid, 0, 0, buf, std::strlen(buf) + 1)) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kern.hostuuid", std::strerror(error));
	}
	free(buf);
#endif
}

static inline
void
calculate_host_id () 
{
	// This is Dan Bernstein's SHA-3-AHS256 proposal from 2010-11.
	CubeHash h(16U, 16U, 32U, 32U, 256U);
	const unsigned char * m(reinterpret_cast<unsigned char *>(&machine_id));
	h.Update(m, sizeof machine_id);
	h.Final();
	for ( std::size_t j(0);j < 4; ++j)
		hostid = (hostid << 8) | h.hashval[j];
}

static inline
void
write_non_volatile_hostuuid (
	const char * prog
) {
#if !defined(__LINUX__) && !defined(__linux__)
	char * buf(0);
	uint32_t status;
	const uuid_t guid(uuid_to_guid(machine_id));
	uuid_to_string(&guid, &buf, &status);
	std::ofstream s(non_volatile_hostuuid_filename);
	if (!s.fail()) {
		s.write(buf, 36).put('\n');
		free(buf);
		return;
	}
	free(buf);
	if ((EROFS != errno)
#if defined(MS_BIND)
		|| (0 > mount(volatile_hostuuid_filename, non_volatile_hostuuid_filename, "bind", MS_BIND, 0))
#endif
	   ) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, non_volatile_hostuuid_filename, std::strerror(error));
		throw EXIT_FAILURE;
	}
#endif
}

static inline
void
write_volatile_hostuuid (
	const char * prog
) {
#if !defined(__LINUX__) && !defined(__linux__)
	char * buf(0);
	uint32_t status;
	const uuid_t guid(uuid_to_guid(machine_id));
	uuid_to_string(&guid, &buf, &status);
	std::ofstream s(volatile_hostuuid_filename);
	if (!s.fail()) {
		s.write(buf, 36).put('\n');
		free(buf);
		return;
	}
	free(buf);
	const int error(errno);
	std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, volatile_hostuuid_filename, std::strerror(error));
	throw EXIT_FAILURE;
#endif
}

static inline
void
write_non_volatile_hostid (
	const char * prog
) {
#if defined(__LINUX__) || defined(__linux__)
	std::ofstream s(non_volatile_hostid_filename);
	if (!s.fail()) {
		s.write(reinterpret_cast<const char *>(&hostid), sizeof hostid);
		return;
	}
	if ((EROFS != errno)
#if defined(MS_BIND)
		|| (0 > mount(volatile_hostid_filename, non_volatile_hostid_filename, "bind", MS_BIND, 0))
#endif
	   ) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, non_volatile_hostid_filename, std::strerror(error));
		throw EXIT_FAILURE;
	}
#endif
}

static inline
void
write_volatile_hostid (
	const char * prog
) {
#if !defined(__LINUX__) && !defined(__linux__)
	int oid[2] = {CTL_KERN, KERN_HOSTID};
	const unsigned long h(hostid);
	if (0 > sysctl(oid, sizeof oid/sizeof *oid, 0, 0, &h, sizeof h)) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kern.hostid", std::strerror(error));
	}
#else
	std::ofstream s(volatile_hostid_filename);
	if (!s.fail()) {
		s.write(reinterpret_cast<const char *>(&hostid), sizeof hostid);
		return;
	}
	const int error(errno);
	std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, non_volatile_hostid_filename, std::strerror(error));
	throw EXIT_FAILURE;
#endif
}

static inline
bool
read_backwards_compatible_non_volatile ()
{
	return	
#if !defined(__LINUX__) && !defined(__linux__)
		read_first_line_uuid("/etc/hostid") ||
#endif
		read_first_line_machine_id("/var/lib/dbus/machine-id") || 
		read_first_line_machine_id("/var/db/dbus/machine-id");
}

static inline
void
create()
{
#if defined(__LINUX__) || defined(__linux__)
	uuid_generate(machine_id);
#else
	uint32_t status;
	uuid_create(&machine_id, &status);
#endif
}

static inline
bool
is_null ()
{
#if !defined(__LINUX__) && !defined(__linux__)
	uint32_t status;
	return uuid_is_nil(&machine_id, &status);
#else
	return uuid_is_null(machine_id);
#endif
}

static inline
bool 
validate ()
{
	if (is_null()) return false;
	unsigned char * m(reinterpret_cast<unsigned char *>(&machine_id));
	unsigned char & variant(m[8]);
	unsigned char & version(m[6]);
	if (((variant & 0xC0) != 0x80)) {
		variant = (variant & 0x3F) | 0x80;
		version = (version & 0x0F) | 0x40;
	} else if ((version & 0xF0) < 0x10 || (version & 0xF0) > 0x50)
		version = (version & 0x0F) | 0x40;
	return true;
}

static inline
bool
read_fallbacks ()
{
	return read_volatile() || read_host_uuid() || read_backwards_compatible_non_volatile() || (am_in_jail() ? read_container_id : read_boot_id)();
}

/* Main function ************************************************************
// **************************************************************************
*/

void
setup_machine_id ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	bool verbose(false);
	try {
		popt::bool_definition verbose_option('f', "verbose", "Display the UUID.", verbose);
		popt::definition * top_table[] = {
			&verbose_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw EXIT_FAILURE;
	}

	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Unexpected argument(s).");
		throw EXIT_USAGE;
	}

#if defined(__LINUX__) || defined(__linux__)
	uuid_clear(machine_id);
#else
	uint32_t status;
	uuid_create_nil(&machine_id, &status);
#endif
	bool rewrite(false);
	if (!read_non_volatile() || is_null()) {
		if (!read_fallbacks() || is_null())
			create();
		rewrite = true;
	}
	if (!validate())
		rewrite = true;
	calculate_host_id();
	if (verbose) {
#if defined(__LINUX__) || defined(__linux__)
		char buf[37];
		uuid_unparse(machine_id, buf);
#else
		char * buf(0);
		uint32_t s;
		const uuid_t guid(uuid_to_guid(machine_id));
		uuid_to_string(&guid, &buf, &s);
#endif
		std::fprintf(stdout, "Machine ID: %s\n", buf);
		std::fprintf(stdout, "POSIX Host ID: %08lx\n", static_cast<unsigned long>(hostid));
#if !defined(__LINUX__) && !defined(__linux__)
		free(buf);
#endif
	}
	write_volatile(prog);
	write_volatile_hostid(prog);
	write_volatile_hostuuid(prog);
	write_host_uuid(prog);
	if (rewrite)
		write_non_volatile(prog);
	write_non_volatile_hostid(prog);
	write_non_volatile_hostuuid(prog);

	throw EXIT_SUCCESS;
}
