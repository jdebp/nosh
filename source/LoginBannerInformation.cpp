/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <map>
#include <string>
#include <cstring>
#include <cstdlib>
#include "config/hasutmpx.h"
#include "config/hasutmp.h"
#if defined(HAS_UTMPX)
#include <utmpx.h>
#elif defined(HAS_UTMP)
#include <utmp.h>
#include <paths.h>
#else
#error "Don't know how to count logged in users on your platform."
#endif
#include <unistd.h>
#include <paths.h>
#include "ttyname.h"
#include "utils.h"
#include "FileStar.h"
#include "ProcessEnvironment.h"
#include "LoginBannerInformation.h"

#include <cstdio>

/* system information *******************************************************
// **************************************************************************
*/

namespace {

const char etc_release_filename[] = "/etc/os-release";
const char lib_release_filename[] = "/usr/lib/os-release";
const char etc_machineinfo_filename[] = "/etc/machine-info";
const char run_machineinfo_filename[] = "/run/machine-info";

inline
unsigned long
count_users()
{
	unsigned long count(0);
#if defined(HAS_UTMPX)
	setutxent();
	while (struct utmpx * u = getutxent()) {
		if (USER_PROCESS == u->ut_type)
			++count;
	}
	endutxent();
#elif defined(HAS_UTMP)
	const FileStar file(std::fopen(_PATH_UTMP, "r"));
	if (file) {
		for (;;) {
			struct utmp u;
			const size_t n(std::fread(&u, sizeof u, 1, file));
			if (n < 1) break;
			if (u.ut_name[0]) ++count;
		}
	}
#else
#error "Don't know how to count logged in users on your platform."
#endif
	return count;
}

inline
struct utsname
get_utsname()
{
	struct utsname uts;
	uname(&uts);
	return uts;
}

}

LoginBannerInformation::LoginBannerInformation(
	const char * prog,
	const ProcessEnvironment & envs
) :
	uts(get_utsname()),
	line(get_line_name(envs)),
	users(count_users()),
	pretty_sysname(uts.sysname),
	pretty_nodename(uts.nodename),
	deployment(),
	location(),
	hostname(envs.query("HOSTNAME")),		// The same convention as the machineenv command.
	nisdomainname(envs.query("DOMAINNAME")),	// The same convention as the machineenv command.
	dnsdomainname(envs.query("LOCALDOMAIN"))	// Convention in both the BIND and djbdns DNS clients.
{
	if (!nisdomainname) {
		if (0 <= getdomainname(nisdomainname_buf, sizeof nisdomainname_buf))
			nisdomainname = nisdomainname_buf;
	}
	if (!hostname) {
		if (0 <= gethostname(hostname_buf, sizeof hostname_buf))
			hostname = hostname_buf;
	}
	if (!dnsdomainname) {
		if (!hostname)
			dnsdomainname = "";
		else
		if (const char * dot = std::strchr(hostname, '.'))
			dnsdomainname = dot;
		else
			dnsdomainname = std::strchr(hostname, '\0');
	}

	const char * release_filename("");
	FileStar os_release(std::fopen(release_filename = etc_release_filename, "r"));
	if (!os_release) {
		if (ENOENT == errno)
			os_release = std::fopen(release_filename = lib_release_filename, "r");
	}
	if (!os_release) {
		if (ENOENT != errno)
			message_error_errno(prog, envs, release_filename);
	}
	if (os_release) {
		try {
			std::vector<std::string> env_strings(read_file(prog, envs, release_filename, os_release));
			for (std::vector<std::string>::const_iterator i(env_strings.begin()); i != env_strings.end(); ++i) {
				const std::string & s(*i);
				const std::string::size_type p(s.find('='));
				const std::string var(s.substr(0, p));
				const std::string val(p == std::string::npos ? std::string() : s.substr(p + 1, std::string::npos));
				if ("PRETTY_NAME" == var)
					pretty_sysname = val;
			}
		} catch (const char * r) {
			message_error(prog, envs, release_filename, r);
		}
		os_release = nullptr;
	}

	const char * machineinfo_filename("");
	FileStar machine_info(std::fopen(machineinfo_filename = etc_machineinfo_filename, "r"));
	if (!machine_info) {
		if (ENOENT == errno)
			machine_info = std::fopen(machineinfo_filename = run_machineinfo_filename, "r");
	}
	if (!machine_info) {
		if (ENOENT != errno)
			message_error_errno(prog, envs, machineinfo_filename);
	}
	if (machine_info) {
		try {
			std::vector<std::string> env_strings(read_file(prog, envs, machineinfo_filename, machine_info));
			for (std::vector<std::string>::const_iterator i(env_strings.begin()); i != env_strings.end(); ++i) {
				const std::string & s(*i);
				const std::string::size_type p(s.find('='));
				const std::string var(s.substr(0, p));
				const std::string val(p == std::string::npos ? std::string() : s.substr(p + 1, std::string::npos));
				if ("PRETTY_HOSTNAME" == var)
					pretty_nodename = val;
				else
				if ("DEPLOYMENT" == var)
					deployment = val;
				else
				if ("LOCATION" == var)
					location = val;
			}
		} catch (const char * r) {
			message_error(prog, envs, machineinfo_filename, r);
		}
		machine_info = nullptr;
	}
}
