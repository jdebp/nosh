/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <cstring>
#include <fstream>
#include "machine_id.h"
#include "jail.h"
#include "ProcessEnvironment.h"

/* library functions for accessing the machine ID ***************************
// **************************************************************************
*/

namespace {

	const char volatile_filename[] = "/run/machine-id";
	const char non_volatile_filename[] = "/etc/machine-id";
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
	const char volatile_hostuuid_filename[] = "/run/hostid";
	const char non_volatile_hostuuid_filename[] = "/etc/hostid";
	const char non_volatile_local_etc_machineid_filename[] = "/usr/local/etc/machine-id";
#endif

}

namespace machine_id {

uuid_t the_machine_id;

}

/* bottom level functions ***************************************************
// **************************************************************************
*/

namespace {

#if !defined(__LINUX__) && !defined(__linux__)
inline
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

inline
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

inline 
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

inline 
int 
c2x ( int c ) 
{
	if (EOF == c || c < 0) return EOF;
	if (c < 10) return c + '0';
	if (c < 36) return c + 'a' - 10;
	return EOF;
}

bool
read_first_line_machine_id (
	std:: istream & i
) {
	if (i.fail()) return false;
	machine_id::erase();
	unsigned char * m(reinterpret_cast<unsigned char *>(&machine_id::the_machine_id));
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

bool
read_first_line_machine_id (
	const char * filename
) {
	std::ifstream s(filename);
	if (s.fail()) return false;
	return read_first_line_machine_id(s);
}

bool
read_uuid (
	const char * p
) {
#if defined(__LINUX__) || defined(__linux__)
	return 0 <= uuid_parse(p, machine_id::the_machine_id);
#else
	uint32_t status;
	uuid_t guid;
	uuid_from_string(p, &guid, &status);
	machine_id::the_machine_id = guid_to_uuid(guid);
	return uuid_s_ok == status;
#endif
}

#if defined(__FreeBSD__) || defined(__DragonFly__)
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

bool
read_first_line_uuid (
	const char * filename
) {
	std::ifstream s(filename);
	if (s.fail()) return false;
	return read_first_line_uuid(s);
}
#endif

void
write_one_line_machine_id (
	std:: ostream & i
) {
	unsigned char * m(reinterpret_cast<unsigned char *>(&machine_id::the_machine_id));
	for (std::size_t n(0U); n < sizeof machine_id::the_machine_id; ++n) {
		const unsigned char b(m[n]);
		i.put(c2x((b >> 4) & 0x0F)).put(c2x(b & 0x0F));
	}
	i.put('\n');
}

#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
inline
void
write_one_line_hostuuid (
	std:: ostream & i
) {
	char * buf(0);
	uint32_t status;
	const uuid_t guid(uuid_to_guid(machine_id::the_machine_id));
	uuid_to_string(&guid, &buf, &status);
	i.write(buf, 36).put('\n');
	const int error(errno);
	free(buf);
	errno = error;
}
#endif

inline
void
write_or_bind_mount_machine_id (
	const char * prog,
	const char * write_filename,
	const char * mount_filename
) {
	std::ofstream s(write_filename);
	if (!s.fail()) {
		write_one_line_machine_id(s);
		return;
	}
	if ((EROFS != errno)
#if defined(MS_BIND)
		|| (0 > mount(mount_filename, write_filename, "bind", MS_BIND, 0))
#endif
	   ) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, write_filename, std::strerror(error));
		throw EXIT_FAILURE;
	}
#if !defined(MS_BIND)
	static_cast<void>(mount_filename);	// Silence a compiler warning.
#endif
}

#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
inline
void
write_or_bind_mount_hostuuid (
	const char * prog,
	const char * write_filename,
	const char * mount_filename
) {
	std::ofstream s(write_filename);
	if (!s.fail()) {
		write_one_line_hostuuid(s);
		return;
	}
	if ((EROFS != errno)
#if defined(MS_BIND)
		|| (0 > mount(mount_filename, write_filename, "bind", MS_BIND, 0))
#endif
	   ) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, write_filename, std::strerror(error));
		throw EXIT_FAILURE;
	}
#if !defined(MS_BIND)
	static_cast<void>(mount_filename);	// Silence a compiler warning.
#endif
}
#endif

inline
bool
read_container_id (const ProcessEnvironment & envs)
{
	if (const char * c = envs.query("container_uuid"))
		return read_uuid(c);
	return false;
}

inline
bool
read_backwards_compatible_non_volatile ()
{
	return	
#if defined(__FreeBSD__) || defined(__DragonFly__)
		read_first_line_uuid(non_volatile_hostuuid_filename) ||
		read_first_line_uuid(non_volatile_local_etc_machineid_filename) ||
#endif
		read_first_line_machine_id("/var/lib/dbus/machine-id") || 
		read_first_line_machine_id("/var/db/dbus/machine-id");
}

inline
void
write_backwards_compatible_non_volatile (const char * prog)
{
#if defined(__FreeBSD__) || defined(__DragonFly__)
	write_or_bind_mount_hostuuid(prog, non_volatile_hostuuid_filename, volatile_hostuuid_filename);
	write_or_bind_mount_machine_id(prog, non_volatile_local_etc_machineid_filename, volatile_filename);
#else
	static_cast<void>(prog);	// Silence a compiler warning.
#endif
}

#if defined(__FreeBSD__) || defined(__DragonFly__)
const int host_uuid_oid[2] = {CTL_KERN, KERN_HOSTUUID};
#elif defined(__OpenBSD__)
const int host_uuid_oid[2] = {CTL_HW, HW_UUID};
#endif

inline
bool
read_host_uuid ()
{
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
	std::size_t siz(0);
	if (0 > sysctl(host_uuid_oid, sizeof host_uuid_oid/sizeof *host_uuid_oid, 0, &siz, 0, 0)) return false;
	if (37 > siz) return false;
	char * buf(static_cast<char *>(malloc(siz)));
	if (!buf) return false;
	if (0 > sysctl(host_uuid_oid, sizeof host_uuid_oid/sizeof *host_uuid_oid, buf, &siz, 0, 0)) {
		free(buf);
		return false;
	}
	const bool r(read_uuid(buf));
	free(buf);
	return r;
#elif defined(__LINUX__) || defined(__linux__)
	return false;
#else
#error "Don't know how to manipulate volatile host UUID on your platform."
#endif
}

inline
void
write_host_uuid (
	const char * prog
) {
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
	char * buf(0);
	uint32_t status;
	const uuid_t guid(uuid_to_guid(machine_id::the_machine_id));
	uuid_to_string(&guid, &buf, &status);
	if (0 > sysctl(host_uuid_oid, sizeof host_uuid_oid/sizeof *host_uuid_oid, 0, 0, buf, std::strlen(buf) + 1)) {
		const int error(errno);
		std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "kern.hostuuid", std::strerror(error));
	}
	free(buf);
#elif defined(__LINUX__) || defined(__linux__)
	static_cast<void>(prog);	// Silences a compiler warning.
#else
#error "Don't know how to manipulate volatile host UUID on your platform."
#endif
}

bool
read_volatile ()
{
	return read_first_line_machine_id(volatile_filename) || read_host_uuid();
}

void
write_volatile (
	const char * prog
) {
	write_host_uuid(prog);

	std::ofstream s(volatile_filename);
	if (!s.fail()) {
		write_one_line_machine_id(s);
		return;
	}
	const int error(errno);
	std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, volatile_filename, std::strerror(error));
	throw EXIT_FAILURE;
}

}

/* API functions ************************************************************
// **************************************************************************
*/

namespace machine_id {

void
erase()
{
#if defined(__LINUX__) || defined(__linux__)
	uuid_clear(the_machine_id);
#else
	uint32_t status;
	uuid_create_nil(&the_machine_id, &status);
#endif
}

void
create()
{
#if defined(__LINUX__) || defined(__linux__)
	uuid_generate(the_machine_id);
#else
	uint32_t status;
	uuid_create(&the_machine_id, &status);
#endif
}

bool
is_null ()
{
#if !defined(__LINUX__) && !defined(__linux__)
	uint32_t status;
	return uuid_is_nil(&the_machine_id, &status);
#else
	return uuid_is_null(the_machine_id);
#endif
}

std::string
human_readable_form()
{
#if defined(__LINUX__) || defined(__linux__)
	char buf[37];
	uuid_unparse(the_machine_id, buf);
	return std::string(buf);
#else
	char * buf(0);
	uint32_t s;
	const uuid_t guid(uuid_to_guid(the_machine_id));
	uuid_to_string(&guid, &buf, &s);
	const std::string r(buf);
	free(buf);
	return r;
#endif
}

std::string
human_readable_form_compact () 
{
	std::string r;
	unsigned char * m(reinterpret_cast<unsigned char *>(&the_machine_id));
	for (std::size_t n(0U); n < sizeof the_machine_id; ++n) {
		const unsigned char b(m[n]);
		r += c2x((b >> 4) & 0x0F);
		r += c2x(b & 0x0F);
	}
	return r;
}

bool
read_non_volatile ()
{
	return read_first_line_machine_id(non_volatile_filename);
}

void
write_non_volatile (
	const char * prog
) {
	write_or_bind_mount_machine_id(prog, non_volatile_filename, volatile_filename);
}

bool 
validate ()
{
	if (is_null()) return false;
	unsigned char * m(reinterpret_cast<unsigned char *>(&the_machine_id));
	unsigned char & variant(m[8]);
	unsigned char & version(m[6]);
	if (((variant & 0xC0) != 0x80)) {
		variant = (variant & 0x3F) | 0x80;
		version = (version & 0x0F) | 0x40;
	} else if ((version & 0xF0) < 0x10 || (version & 0xF0) > 0x50)
		version = (version & 0x0F) | 0x40;
	return true;
}

bool
read_fallbacks (const ProcessEnvironment & envs)
{
	return
		read_volatile() ||
		read_backwards_compatible_non_volatile() ||
		(am_in_jail(envs) && read_container_id(envs))
	;
}

void
write_fallbacks (const char * prog)
{
	write_volatile(prog);
	write_backwards_compatible_non_volatile(prog);
}

}
