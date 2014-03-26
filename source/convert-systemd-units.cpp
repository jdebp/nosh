/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <map>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <iostream>
#include <fstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>	// Necessary for the SO_REUSEPORT macro.
#include <netinet/in.h>	// Necessary for the IPV6_V6ONLY macro.
#include <dirent.h>
#include <unistd.h>
#include <pwd.h>
#include "utils.h"
#include "fdutils.h"
#include "common-manager.h"
#include "popt.h"
#include "FileStar.h"

static 
const char * systemd_prefixes[] = {
	"/run/", "/etc/", "/lib/", "/usr/lib/", "/usr/local/lib/"
};

static
std::string
tolower (
	const std::string & s
) {
	std::string r;
	for (std::string::const_iterator p(s.begin()); s.end() != p; ++p) 
		r += std::tolower(*p);
	return r;
}

static inline
void
split_name (
	const char * s,
	std::string & dirname,
	std::string & basename
) {
	if (const char * slash = std::strrchr(s, '/')) {
		basename = slash + 1;
		dirname = std::string(s, slash + 1);
#if defined(__OS2__) || defined(__WIN32__) || defined(__NT__)
	} else if (const char * bslash = std::strrchr(s, '\\')) {
		basename = bslash + 1;
		dirname = std::string(s, bslash + 1);
	} else if (std::isalpha(s[0]) && ':' == s[1]) {
		basename = s + 2;
		dirname = std::string(s, s + 2);
#endif
	} else {
		basename = s;
		dirname = std::string();
	}
}

static inline
FILE *
find (
	const std::string & name,
	std::string & path
) {
	if (std::string::npos != name.find('/')) {
		path = name;
		FILE * f = std::fopen(path.c_str(), "r");
		if (f) return f;
	} else {
		int error(ENOENT);	// the most interesting error encountered
		for ( const char ** p(systemd_prefixes); p < systemd_prefixes + sizeof systemd_prefixes/sizeof *systemd_prefixes; ++p) {
			path = ((std::string(*p) + "systemd/") + (local_session_mode ? "user/" : "system/")) + name;
			FILE * f = std::fopen(path.c_str(), "r");
			if (f) return f;
			if (ENOENT == errno) 
				errno = error;	// Restore a more interesting error.
			else
				error = errno;	// Save this interesting error.
		}
	}
	return NULL;
}

static
bool
is_regular (
	const char * prog,
	const std::string & name,
	FILE * file
) {
	struct stat s;
	if (0 > fstat(fileno(file), &s)) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, name.c_str(), std::strerror(error));
		return false;
	}
	if (!S_ISREG(s.st_mode)) {
		std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, name.c_str(), "Not a regular file.");
		return false;
	}
	return true;
}

struct value {
	value() : used(false), datum() {}
	value(const std::string & v) : used(false), datum(v) {}
	bool used;
	std::string datum;
};

struct profile {
	typedef std::map<std::string, value> SecondLevel;
	typedef std::map<std::string, SecondLevel> FirstLevel;

	value * use ( 
		const std::string & k0, ///< must be all lowercase
		const std::string & k1 ///< must be all lowercase
	) 
	{
		FirstLevel::iterator i0(m0.find(k0));
		if (m0.end() == i0) return NULL;
		SecondLevel & m1(i0->second);
		SecondLevel::iterator i1(m1.find(k1));
		if (m1.end() == i1) return NULL;
		i1->second.used = true;
		return &i1->second;
	}
	void insert_or_replace ( 
		const std::string & k0, ///< must be all lowercase
		const std::string & k1, ///< must be all lowercase
		const std::string & v 
	) 
	{
		m0[k0][k1] = v;
	}

	FirstLevel m0;
};

static
std::string
ltrim (
	const std::string & s
) {
	for (std::string::size_type p(0); s.length() != p; ++p) {
		if (!std::isspace(s[p])) return s.substr(p, std::string::npos);
	}
	return std::string();
}

static
std::string
rtrim (
	const std::string & s
) {
	for (std::string::size_type p(s.length()); p > 0; ) {
		--p;
		if (!std::isspace(s[p])) return s.substr(0, p + 1);
	}
	return std::string();
}

static
std::list<std::string>
split_list (
	const std::string & s
) {
	std::list<std::string> r;
	std::string q;
	for (std::string::size_type p(0); s.length() != p; ++p) {
		const char c(s[p]);
		if (!std::isspace(c)) {
			q += c;
		} else {
			if (!q.empty()) {
				r.push_back(q);
				q.clear();
			}
		}
	}
	if (!q.empty()) r.push_back(q);
	return r;
}

static
std::string
strip_leading_minus (
	const std::string & s
) {
	if (s.length() < 1 || '-' != s[0]) return s;
	return s.substr(1, std::string::npos);
}

static
std::string
multi_line_comment (
	const std::string & s
) {
	std::string r;
	bool bol(true);
	for (std::string::const_iterator p(s.begin()); s.end() != p; ++p) {
		const char c(*p);
		if ('\n' == c) 
			bol = true;
		else if (bol) {
			r += '#';
			bol = false;
		}
		r += c;
	}
	if (!bol) r += '\n';
	return r;
}

static
std::string
quote (
	const std::string & s
) {
	std::string r;
	bool quote(false);
	for (std::string::const_iterator p(s.begin()); s.end() != p; ++p) {
		const char c(*p);
		if (!std::isalnum(c) && '/' != c && '-' != c && '_' != c && '.' != c) {
			quote = true;
			if ('\"' == c || '\\' == c)
				r += '\\';
		}
		r += c;
	}
	if (quote) r = "\"" + r + "\"";
	return r;
}

struct names {
	names(const char * a) : arg_name(a) { split_name(a, unit_dirname, unit_basename); }
	void set_prefix(const std::string & v, bool esc) { set(esc, escaped_prefix, prefix, v); }
	void set_instance(const std::string & v, bool esc) { set(esc, escaped_instance, instance, v); }
	void set_bundle_basename(const std::string & v) { bundle_basename = v; }
	void set_bundle_dirname(const std::string & v) { bundle_dirname = v; }
	const std::string & query_arg_name () const { return arg_name; }
	const std::string & query_unit_dirname () const { return unit_dirname; }
	const std::string & query_unit_basename () const { return unit_basename; }
	const std::string & query_escaped_unit_basename () const { return escaped_unit_basename; }
	const std::string & query_prefix () const { return prefix; }
	const std::string & query_escaped_prefix () const { return escaped_prefix; }
	const std::string & query_instance () const { return instance; }
	const std::string & query_escaped_instance () const { return escaped_instance; }
	const std::string & query_bundle_basename () const { return bundle_basename; }
	const std::string & query_bundle_dirname () const { return bundle_dirname; }
	std::string substitute ( const std::string & );
protected:
	std::string arg_name, unit_dirname, unit_basename, escaped_unit_basename, prefix, escaped_prefix, instance, escaped_instance, bundle_basename, bundle_dirname;
	static std::string unescape ( const std::string & );
	static std::string escape ( const std::string & );
	void set ( bool esc, std::string & escaped, std::string & normal, const std::string & value ) {
		if (esc)
			normal = unescape(escaped = value);
		else
			escaped = escape(normal = value);
	}
};

std::string 
names::unescape ( 
	const std::string & s
) {
	std::string r;
	for (std::string::const_iterator e(s.end()), q(s.begin()); e != q; ) {
		char c(*q++);
		if ('-' == c) {
			r += '/';
		} else
		if ('\\' != c) {
			r += c;
		} else
		if (e == q) {
			r += c;
		} else
		{
			c = *q++;
			if ('X' != c && 'x' != c)
				r += c;
			else {
				unsigned v(0U);
				for (;;) {
					if (e == q) break;
					c = *q;
					if (!std::isxdigit(c)) break;
					c = std::isdigit(c) ? (c - '0') : (std::tolower(c) - 'a' + 10);
					v = (v << 4) | c;
				}
				r += char(v);
			}
		}
	}
	return r;
}

std::string 
names::escape ( 
	const std::string & s
) {
	std::string r;
	for (std::string::const_iterator e(s.end()), q(s.begin()); e != q; ) {
		char c(*q++);
		if ('/' == c) {
			r += '-';
		} else
		if ('-' != c) {
			r += c;
		} else
		{
			char buf[6];
			snprintf(buf, sizeof buf, "\\x%02x", c);
			r += std::string(buf);
		}
	}
	return r;
}

std::string
names::substitute (
	const std::string & s
) {
	std::string r;
	for (std::string::const_iterator e(s.end()), q(s.begin()); e != q; ++q) {
		char c(*q);
		if ('%' != c) {
			r += c;
			continue;
		}
		++q;
		if (e == q) {
			r += c;
			--q;
			continue;
		}
		switch (c = *q) {
			case 'p': r += query_escaped_prefix(); break;
			case 'P': r += query_prefix(); break;
			case 'i': r += query_escaped_instance(); break;
			case 'I': r += query_instance(); break;
			case 'n': r += query_escaped_unit_basename(); break;
			case 'N': r += query_unit_basename(); break;
			case '%': default:	r += '%'; r += c; break;
		}
	}
	return r;
}

static
bool
is_bool_true (
	const value * v,
	bool def
) {
	if (!v) return def;
	const std::string r(tolower(v->datum));
	return "yes" == r || "on" == r || "true" == r;
}

static
bool
is_local_socket_name (
	const std::string & s
) {
	return s.length() > 0 && '/' == s[0];
}

static inline
bool
bracketed (
	const std::string & s
) {
	return s.length() > 1 && '[' == s[0] && ']' == s[s.length() - 1];
}

static
void
split_ip_socket_name (
	const std::string & s, 
	std::string & listenaddress, 
	std::string & listenport
) {
	const std::string::size_type colon(s.rfind(':'));
	if (std::string::npos == colon) {
		listenaddress = "::0";
		listenport = s;
	} else {
		listenaddress = s.substr(0, colon);
		listenport = s.substr(colon + 1, std::string::npos);
		if (bracketed(listenaddress))
			listenaddress = listenaddress.substr(1, colon - 2);
	}
}

static inline
bool
is_section_heading (
	std::string line,	///< line, already left trimmed by the caller
	std::string & section	///< overwritten only if we successfully parse the heading
) {
	line = rtrim(line);
	if (!bracketed(line)) return false;
	section = tolower(line.substr(1, line.length() - 2));
	return true;
}

static inline
bool
read_line (
	FILE * f,
	std::string & l
) {
	l.clear();
	for (;;) {
		const int c(std::fgetc(f));
		if (EOF == c) return !l.empty();
		if ('\n' == c) return true;
		l += static_cast<unsigned char>(c);
	}
}

static
void
load (
	profile & p,
	FILE * file
) {
	for (std::string line, section; read_line(file, line); ) {
		line = ltrim(line);
		if (line.length() < 1) continue;
		if ('#' == line[0] || ';' == line[0]) continue;
		if (is_section_heading(line, section)) continue;
		const std::string::size_type eq(line.find('='));
		const std::string var(line.substr(0, eq));
		const std::string val(eq == std::string::npos ? std::string() : line.substr(eq + 1, std::string::npos));
		p.insert_or_replace(section, tolower(var), val);
	}
}

static
void
create_link (
	const char * prog,
	const std::string & target,
	const std::string & link
) {
	if (0 > unlinkat(AT_FDCWD, link.c_str(), 0)) {
		const int error(errno);
		if (ENOENT != error)
			std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, link.c_str(), std::strerror(error));
	}
	if (0 > symlinkat(target.c_str(), AT_FDCWD, link.c_str())) {
		const int error(errno);
		std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, link.c_str(), std::strerror(error));
	}
}

static
void
create_links (
	const char * prog,
	const bool is_target,
	const std::string & names,
	const std::string & service_basename,
	const std::string & subdir
) {
	const std::list<std::string> list(split_list(names));
	for (std::list<std::string>::const_iterator i(list.begin()); list.end() != i; ++i) {
		const std::string & name(*i);
		std::string base, link, target;
		if (ends_in(name, ".target", base)) {
			if (is_target)
				target = "../../" + base;
			else
				target = "/etc/system-manager/targets/" + base;
			link = service_basename + subdir + base;
		} else
		if (ends_in(name, ".service", base)) {
			if (is_target)
				target = "/var/sv/" + base;
			else
				target = "../../" + base;
			link = service_basename + subdir + base;
		} else 
		{
			if (is_target)
				target = "/var/sv/" + base;
			else
				target = "../../" + name;
			link = service_basename + subdir + name;
		}
		create_link(prog, target, link);
	}
}

static
void
open (
	const char * prog,
	std::ofstream & file,
	const std::string & name
) {
	file.open(name.c_str(), std::ios::trunc|std::ios::out);
	if (!file) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, name.c_str(), std::strerror(error));
		throw EXIT_FAILURE;
	}
	chmod(name.c_str(), 0755);
}

static
void
report_unused(
	const char * prog,
	profile & p,
	const std::string & name
) {
	for (profile::FirstLevel::const_iterator i0(p.m0.begin()); p.m0.end() != i0; ++i0) {
		const std::string & section(i0->first);
		const profile::SecondLevel & m1(i0->second);
		for (profile::SecondLevel::const_iterator i1(m1.begin()); m1.end() != i1; ++i1) {
			const std::string & var(i1->first);
			const value & v(i1->second);
			if (!v.used) {
				const std::string & val(v.datum);
				std::fprintf(stderr, "%s: WARNING: %s: Unused setting: [%s] %s = %s\n", prog, name.c_str(), section.c_str(), var.c_str(), val.c_str());
			}
		}
	}
}

/* Main function ************************************************************
// **************************************************************************
*/

void
convert_systemd_units (
	const char * & /*next_prog*/,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	std::string bundle_root;
	bool unescape_instance(false), unescape_prefix(false);
	try {
		const char * bundle_root_str(0);
		popt::bool_definition user_option('u', "user", "Communicate with the per-user manager.", local_session_mode);
		popt::string_definition bundle_option('\0', "bundle-root", "Root directory for bundles.", "directory", bundle_root_str);
		popt::bool_definition unescape_instance_option('\0', "unescape-instance", "Unescape the instance part of a template instantiation.", unescape_instance);
		popt::definition * main_table[] = {
			&user_option,
			&bundle_option,
			&unescape_instance_option
		};
		popt::top_table_definition main_option(sizeof main_table/sizeof *main_table, main_table, "Main options", "");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		if (p.stopped()) throw EXIT_SUCCESS;
		if (bundle_root_str) bundle_root = bundle_root_str + std::string("/");
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw EXIT_USAGE;
	}

	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing argument(s).");
		throw EXIT_FAILURE;
	}

	struct names names(args.front());

	args.erase(args.begin());
	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Unrecognized argument(s).");
		throw EXIT_FAILURE;
	}

	bool is_socket_activated(false), is_target(false), is_socket_accept(false);

	std::string socket_filename;
	profile socket_profile;
	std::string service_filename;
	profile service_profile;

	{
		std::string bundle_basename;
		if (ends_in(names.query_unit_basename(), ".target", bundle_basename)) {
			is_target = true;
		} else
		if (ends_in(names.query_unit_basename(), ".socket", bundle_basename)) {
			is_socket_activated = true;
		} else
		if (ends_in(names.query_unit_basename(), ".service", bundle_basename)) {
		} else
		{
			bundle_basename = names.query_unit_basename();
		}
		names.set_bundle_basename(bundle_basename);
		names.set_bundle_dirname(bundle_root + names.query_bundle_basename());
	}

	if (is_socket_activated) {
		FileStar socket_file(find(names.query_arg_name(), socket_filename));
		if (!socket_file) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, names.query_arg_name().c_str(), std::strerror(error));
			throw EXIT_FAILURE;
		}
		if (!is_regular(prog, socket_filename, socket_file))
			throw EXIT_FAILURE;
		load(socket_profile, socket_file);
		socket_file = NULL;
	}

	{
		FileStar service_file;
		std::string service_unit_name;
		if (is_socket_activated) {
			value * accept(socket_profile.use("socket", "accept"));
			names.set_prefix(names.query_bundle_basename(), unescape_prefix);
			is_socket_accept = is_bool_true(accept, false);
			service_unit_name = names.query_unit_dirname() + names.query_prefix() + (is_socket_accept ? "@" : "") + ".service";
			service_file = find(service_unit_name, service_filename);
		} else {
			names.set_prefix(names.query_bundle_basename(), unescape_prefix);
			service_unit_name = names.query_arg_name();
			service_file = find(service_unit_name, service_filename);
			if (!service_file && ENOENT == errno) {
				std::string::size_type atc(names.query_bundle_basename().find('@'));
				if (names.query_bundle_basename().npos != atc) {
					names.set_prefix(names.query_unit_basename().substr(0, atc), unescape_prefix);
					++atc;
					names.set_instance(names.query_bundle_basename().substr(atc, names.query_bundle_basename().npos), unescape_instance);
					service_unit_name = names.query_unit_dirname() + names.query_prefix() + "@.service";
					service_file = find(service_unit_name, service_filename);
				}
			}
		}
				
		if (!service_file) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, service_unit_name.c_str(), std::strerror(error));
			throw EXIT_FAILURE;
		}

		if (!is_regular(prog, service_filename, service_file))
			throw EXIT_FAILURE;

		load(service_profile, service_file);

		service_file = NULL;
	}

	value * listenstream(socket_profile.use("socket", "listenstream"));
	value * listendatagram(socket_profile.use("socket", "listendatagram"));
#if defined(IPV6_V6ONLY)
	value * bindipv6only(socket_profile.use("socket", "bindipv6only"));
#endif
#if defined(SO_REUSEPORT)
	value * reuseport(socket_profile.use("socket", "reuseport"));
#endif
	value * backlog(socket_profile.use("socket", "backlog"));
	value * maxconnections(socket_profile.use("socket", "maxconnections"));
	value * keepalive(socket_profile.use("socket", "keepalive"));
	value * socketmode(socket_profile.use("socket", "socketmode"));
	value * nagle(socket_profile.use("socket", "nagle"));
	value * ucspirules(socket_profile.use("socket", "ucspirules"));
	value * freebind(socket_profile.use("socket", "freebind"));
	value * socket_before(socket_profile.use("unit", "before"));
	value * socket_after(socket_profile.use("unit", "after"));
	value * socket_conflicts(socket_profile.use("unit", "conflicts"));
	value * socket_wants(socket_profile.use("unit", "wants"));
	value * socket_requires(socket_profile.use("unit", "requires"));
	value * socket_requisite(socket_profile.use("unit", "requisite"));
	value * socket_description(socket_profile.use("unit", "description"));
	value * socket_defaultdependencies(socket_profile.use("unit", "defaultdependencies"));
	value * socket_earlysupervise(socket_profile.use("unit", "earlysupervise"));	// This is an extension to systemd.
	value * socket_wantedby(socket_profile.use("install", "wantedby"));
	value * socket_requiredby(socket_profile.use("install", "requiredby"));
	value * socket_stoppedby(socket_profile.use("install", "stoppedby"));	// This is an extension to systemd.

	value * type(service_profile.use("service", "type"));
	value * workingdirectory(service_profile.use("service", "workingdirectory"));
	value * rootdirectory(service_profile.use("service", "rootdirectory"));
	value * systemdworkingdirectory(service_profile.use("service", "systemdworkingdirectory"));	// This is an extension to systemd.
	value * execstart(service_profile.use("service", "execstart"));
	value * execstartpre(service_profile.use("service", "execstartpre"));
	value * execstoppost(service_profile.use("service", "execstoppost"));
	value * limitnofile(service_profile.use("service", "limitnofile"));
	value * limitcpu(service_profile.use("service", "limitcpu"));
	value * limitcore(service_profile.use("service", "limitcore"));
	value * limitnproc(service_profile.use("service", "limitnproc"));
	value * limitfsize(service_profile.use("service", "limitfsize"));
	value * limitas(service_profile.use("service", "limitas"));
	value * limitdata(service_profile.use("service", "limitdata"));
	value * limitmemory(service_profile.use("service", "limitmemory"));	// This is an extension to systemd.
	value * killmode(service_profile.use("service", "killmode"));
	value * rootdirectorystartonly(service_profile.use("service", "rootdirectorystartonly"));
	value * permissionsstartonly(service_profile.use("service", "permissionsstartonly"));
	value * standardinput(service_profile.use("service", "standardinput"));
	value * standardoutput(service_profile.use("service", "standardoutput"));
	value * standarderror(service_profile.use("service", "standarderror"));
	value * user(service_profile.use("service", "user"));
	value * group(service_profile.use("service", "group"));
	value * umask(service_profile.use("service", "umask"));
	value * environment(service_profile.use("service", "environment"));
	value * environmentfile(service_profile.use("service", "environmentfile"));
	value * environmentdirectory(service_profile.use("service", "environmentdirectory"));	// This is an extension to systemd.
	value * environmentuser(service_profile.use("service", "environmentuser"));	// This is an extension to systemd.
	value * ttypath(service_profile.use("service", "ttypath"));
	value * ttyreset(service_profile.use("service", "ttyreset"));
	value * ttyprompt(service_profile.use("service", "ttyprompt"));
	value * ttyvhangup(service_profile.use("service", "ttyvhangup"));
//	value * ttyvtdisallocate(service_profile.use("service", "ttyvtdisallocate"));
	value * remainafterexit(service_profile.use("service", "remainafterexit"));
	value * processgroupleader(service_profile.use("service", "processgroupleader"));	// This is an extension to systemd.
	value * sessionleader(service_profile.use("service", "sessionleader"));	// This is an extension to systemd.
	value * restart(service_profile.use("service", "restart"));
	value * restartsec(service_profile.use("service", "restartsec"));
//	value * ignoresigpipe(service_profile.use("service", "ignoresigpipe"));
#if defined(__LINUX__) || defined(__linux__)
	value * privatetmp(service_profile.use("service", "privatetmp"));
	value * privatedevices(service_profile.use("service", "privatedevices"));
	value * privatenetwork(service_profile.use("service", "privatenetwork"));
	value * mountflags(service_profile.use("service", "mountflags"));
#endif
	value * service_defaultdependencies(service_profile.use("unit", "defaultdependencies"));
	value * service_earlysupervise(service_profile.use("unit", "earlysupervise"));	// This is an extension to systemd.
	value * service_after(service_profile.use("unit", "after"));
	value * service_before(service_profile.use("unit", "before"));
	value * service_conflicts(service_profile.use("unit", "conflicts"));
	value * service_wants(service_profile.use("unit", "wants"));
	value * service_requires(service_profile.use("unit", "requires"));
	value * service_requisite(service_profile.use("unit", "requisite"));
	value * service_description(service_profile.use("unit", "description"));
	value * service_wantedby(service_profile.use("install", "wantedby"));
	value * service_requiredby(service_profile.use("install", "requiredby"));
	value * service_stoppedby(service_profile.use("install", "stoppedby"));	// This is an extension to systemd.

	// Actively prevent certain unsupported combinations.

	if (type && "simple" != tolower(type->datum) && "forking" != tolower(type->datum) && "oneshot" != tolower(type->datum)) {
		std::fprintf(stderr, "%s: FATAL: %s: %s: %s\n", prog, service_filename.c_str(), type->datum.c_str(), "Only simple services are supported.");
		throw EXIT_FAILURE;
	}
	if (!execstart && !is_target) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, service_filename.c_str(), "Missing mandatory ExecStart entry.");
		throw EXIT_FAILURE;
	}
	if (is_socket_activated) {
		if (!listenstream && !listendatagram) {
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, socket_filename.c_str(), "Missing mandatory ListenStream/ListenDatagram entry.");
			throw EXIT_FAILURE;
		}
		if (listendatagram) {
			if (is_socket_accept) {
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, socket_filename.c_str(), "ListenDatagram sockets may not have Accept=yes.");
				throw EXIT_FAILURE;
			}
		}
	}
	// We silently set "process" as the default killmode, not "control-group".
	if (killmode && "process" != tolower(killmode->datum)) {
		std::fprintf(stderr, "%s: FATAL: %s: %s: %s\n", prog, service_filename.c_str(), killmode->datum.c_str(), "Unsupported service stop mechanism.");
		throw EXIT_FAILURE;
	}

	// Make the directories.

	const std::string service_dirname(names.query_bundle_dirname() + "/service");

	mkdir(names.query_bundle_dirname().c_str(), 0755);
	mkdir(service_dirname.c_str(), 0755);
	mkdir((names.query_bundle_dirname() + "/after").c_str(), 0755);
	mkdir((names.query_bundle_dirname() + "/before").c_str(), 0755);
	mkdir((names.query_bundle_dirname() + "/wants").c_str(), 0755);
	mkdir((names.query_bundle_dirname() + "/conflicts").c_str(), 0755);
	mkdir((names.query_bundle_dirname() + "/wanted-by").c_str(), 0755);
	mkdir((names.query_bundle_dirname() + "/required-by").c_str(), 0755);
	mkdir((names.query_bundle_dirname() + "/stopped-by").c_str(), 0755);

#define CREATE_LINKS(p,i,l,b,s) (l ? create_links ((p),(i),names.substitute((l)->datum),(b),(s)) : static_cast<void>(0))

	// Construct various common command strings.

	std::string chroot;
	if (rootdirectory) chroot += "chroot " + quote(names.substitute(rootdirectory->datum)) + "\n";
#if defined(__LINUX__) || defined(__linux__)
	const bool is_private_tmp(is_bool_true(privatetmp, false));
	const bool is_private_network(is_bool_true(privatenetwork, false));
	const bool is_private_devices(is_bool_true(privatedevices, false));
	if (is_private_tmp||is_private_network||is_private_devices) {
		chroot += "unshare ";
		if (is_private_tmp||is_private_devices) chroot += "--mount ";
		if (is_private_network) chroot += "--network ";
		chroot += "\n";
		if (is_private_tmp||is_private_devices) {
			chroot += "set-mount-object --recursive slave /\n";
			chroot += "make-private-fs ";
			if (is_private_tmp) chroot += "--temp ";
			if (is_private_devices) chroot += "--devices ";
			chroot += "\n";
		}
	}
	if (mountflags) 
		chroot += "set-mount-object --recursive " + quote(mountflags->datum) + " /\n";
	else if (is_private_tmp||is_private_devices)
		chroot += "set-mount-object --recursive shared /\n";
#endif
	const bool chrootall(!is_bool_true(rootdirectorystartonly, false));
	std::string setsid;
	if (is_bool_true(sessionleader, false)) setsid += "setsid\n";
	if (is_bool_true(processgroupleader, false)) setsid += "setpgrp\n";
	std::string setuidgid;
	if (user) {
		const std::string u(names.substitute(user->datum));
		setuidgid += "setuidgid " + quote(u) + "\n";
		if (passwd * pw = getpwnam(u.c_str())) {
			// This replicates a systemd bug.
			if (pw->pw_dir)
				setuidgid += "setenv HOME " + quote(pw->pw_dir) + "\n";
			// This replicates a systemd useless feature.
			if (pw->pw_shell)
				setuidgid += "setenv SHELL " + quote(pw->pw_shell) + "\n";
		}
	} else {
		if (group)
			setuidgid += "setgid " + quote(names.substitute(group->datum)) + "\n";
	}
	const bool setuidgidall(!is_bool_true(permissionsstartonly, false));
	// systemd always runs services in / by default; daemontools runs them in the service directory.
	std::string chdir;
	if (workingdirectory)
		chdir += "chdir " + quote(names.substitute(workingdirectory->datum)) + "\n";
	else if (is_bool_true(systemdworkingdirectory, true))
		chdir += "chdir /\n";
	std::string softlimit;
	if (limitnofile||limitcpu||limitcore||limitnproc||limitfsize||limitas||limitdata||limitmemory) {
		softlimit += "softlimit";
		if (limitnofile) softlimit += " -o " + quote(limitnofile->datum);
		if (limitcpu) softlimit += " -t " + quote(limitcpu->datum);
		if (limitcore) softlimit += " -c " + quote(limitcore->datum);
		if (limitnproc) softlimit += " -p " + quote(limitnproc->datum);
		if (limitfsize) softlimit += " -f " + quote(limitfsize->datum);
		if (limitas) softlimit += " -a " + quote(limitas->datum);
		if (limitdata) softlimit += " -d " + quote(limitdata->datum);
		if (limitmemory) softlimit += " -m " + quote(limitmemory->datum);
		softlimit += "\n";
	}
	std::string env;
	if (environmentfile) env += "read-conf " + quote(names.substitute(environmentfile->datum)) + "\n";
	if (environmentdirectory) env += "envdir " + quote(names.substitute(environmentdirectory->datum)) + "\n";
	if (environmentuser) env += "envuidgid " + quote(names.substitute(environmentuser->datum)) + "\n";
	if (environment) {
		const std::list<std::string> list(split_list(names.substitute(environment->datum)));
		for (std::list<std::string>::const_iterator i(list.begin()); list.end() != i; ++i) {
			const std::string s(*i);
			const std::string::size_type eq(s.find('='));
			const std::string var(s.substr(0, eq));
			const std::string val(eq == std::string::npos ? std::string() : s.substr(eq + 1, std::string::npos));
			env += "setenv " + quote(var) + " " + quote(val) + "\n";
		}
	}
	std::string um;
	if (umask) um += "umask " + quote(umask->datum) + "\n";
	const bool is_oneshot(type && "oneshot" == tolower(type->datum));
	const bool is_remain(is_bool_true(remainafterexit, false));
	const bool stdin_socket(standardinput && "socket" == tolower(standardinput->datum));
	const bool stdin_tty(standardinput && ("tty" == tolower(standardinput->datum) || "tty-force" == tolower(standardinput->datum)));
	const bool stdout_inherit(standardoutput && "inherit" == tolower(standardoutput->datum));
	const bool stderr_inherit(standarderror && "inherit" == tolower(standarderror->datum));
	// We "un-use" anything that isn't "inherit"/"socket".
	if (standardinput && !stdin_socket && !stdin_tty) standardinput->used = false;
	if (standardoutput && !stdout_inherit) standardoutput->used = false;
	if (standarderror && !stderr_inherit) standarderror->used = false;
	std::string tty(ttypath ? names.substitute(ttypath->datum) : "/dev/console");
	std::string redirect, login_prompt;
	if (ttypath || stdin_tty) redirect += "vc-get-tty " + quote(tty) + "\n";
	if (stdin_tty) {
		redirect += "open-controlling-tty";
		if (!is_bool_true(ttyvhangup, false)) 
#if defined(__LINUX__) || defined(__linux__)
			redirect += " --no-vhangup";
#else
			redirect += " --no-revoke";
#endif
		redirect += "\n";
	}
	if (stdout_inherit && !stdin_tty) redirect += "fdmove -c 1 0\n";
	if (stderr_inherit && !stdin_tty) redirect += "fdmove -c 2 1\n";
	if (stdin_tty) {
		if (is_bool_true(ttyreset, false)) login_prompt += "vc-reset-tty\n";
		if (is_bool_true(ttyprompt, false)) login_prompt += "login-prompt\n";
	}

	// Open the service script files.

	std::ofstream start, stop, restart_script, real_run, real_service, remain;
	open(prog, start, service_dirname + "/start");
	open(prog, stop, service_dirname + "/stop");
	open(prog, restart_script, service_dirname + "/restart");
	open(prog, real_run, service_dirname + "/run");
	if (is_socket_activated)
		open(prog, real_service, service_dirname + "/service");
	std::ostream & run(is_oneshot ? start : real_run);
	std::ostream & service(is_socket_activated ? real_service : run);
	if (is_remain)
		open(prog, remain, service_dirname + "/remain");

	// Write the script files.

	if (!is_oneshot) {
		start << "#!/bin/nosh\n" << multi_line_comment("Start file generated from " + service_filename);
		start << redirect;
		start << softlimit;
		if (setuidgidall) start << setuidgid;
		start << chdir;
		start << env;
		start << um;
		if (chrootall) start << chroot;
		if (execstartpre) start << names.substitute(strip_leading_minus(execstartpre->datum)) << "\n"; else start << "true\n";
	} else {
		real_run << "#!/bin/nosh\n" << multi_line_comment("Run file generated from " + service_filename);
		if (!is_remain)
			real_run << "pause\n";
		else
			real_run << "true\n";
	}

	if (is_socket_activated) {
		run << "#!/bin/nosh\n" << multi_line_comment("Run file generated from " + socket_filename);
		if (socket_description) run << multi_line_comment(names.substitute(socket_description->datum));
		if (listenstream) {
			if (is_local_socket_name(listenstream->datum)) {
				run << "local-stream-socket-listen --systemd-compatibility ";
				if (backlog) run << "--backlog " << quote(backlog->datum) << " ";
				if (socketmode) run << "--socketmode " << quote(socketmode->datum) << " ";
				run << quote(listenstream->datum) << "\n";
				run << setsid;
				run << redirect;
				run << chroot;
				run << chdir;
				run << env;
				run << softlimit;
				run << setuidgid;
				run << um;
				if (is_socket_accept) {
					run << "local-stream-socket-accept ";
					if (maxconnections) run << "--connection-limit " << quote(maxconnections->datum) << " ";
					run << "\n";
				}
			} else {
				std::string listenaddress, listenport;
				split_ip_socket_name(listenstream->datum, listenaddress, listenport);
				run << "tcp-socket-listen --systemd-compatibility ";
				if (backlog) run << "--backlog " << quote(backlog->datum) << " ";
#if defined(IPV6_V6ONLY)
				if (bindipv6only && "both" == tolower(bindipv6only->datum)) run << "--combine4and6 ";
#endif
#if defined(SO_REUSEPORT)
				if (is_bool_true(reuseport, false)) run << "--reuse-port ";
#endif
				if (is_bool_true(freebind, false)) run << "--bind-to-any ";
				run << quote(listenaddress) << " " << quote(listenport) << "\n";
				run << setsid;
				run << redirect;
				run << chroot;
				run << chdir;
				run << softlimit;
				run << setuidgid;
				run << um;
				run << env;
				if (is_socket_accept) {
					run << "tcp-socket-accept ";
					if (maxconnections) run << "--connection-limit " << quote(maxconnections->datum) << " ";
					if (is_bool_true(keepalive, false)) run << "--keepalives ";
					if (!is_bool_true(nagle, true)) run << "--no-delay ";
					run << "\n";
				}
			}
		}
		if (listendatagram) {
			if (is_local_socket_name(listenstream->datum)) {
				run << "local-datagram-socket-listen --systemd-compatibility ";
				if (backlog) run << "--backlog " << quote(backlog->datum) << " ";
				if (socketmode) run << "--socketmode " << quote(socketmode->datum) << " ";
				run << quote(listendatagram->datum) << "\n";
				run << setsid;
				run << redirect;
				run << chroot;
				run << chdir;
				run << env;
				run << softlimit;
				run << setuidgid;
				run << um;
			} else {
				std::string listenaddress, listenport;
				split_ip_socket_name(listendatagram->datum, listenaddress, listenport);
				run << "udp-socket-listen --systemd-compatibility ";
#if defined(IPV6_V6ONLY)
				if (bindipv6only && "both" == tolower(bindipv6only->datum)) run << "--combine4and6 ";
#endif
#if defined(SO_REUSEPORT)
				if (is_bool_true(reuseport, false)) run << "--reuse-port ";
#endif
				run << quote(listenaddress) << " " << quote(listenport) << "\n";
				run << setsid;
				run << redirect;
				run << chroot;
				run << chdir;
				run << env;
				run << softlimit;
				run << setuidgid;
				run << um;
			}
		}
		if (is_bool_true(ucspirules, false)) run << "ucspi-socket-rules-check\n";
		run << "./service\n";
		service << "#!/bin/nosh\n" << multi_line_comment("Service file generated from " + service_filename);
	} else
		run << "#!/bin/nosh\n" << multi_line_comment("Run file generated from " + service_filename);
	if (service_description) service << multi_line_comment(names.substitute(service_description->datum));
	if (is_socket_activated) {
		if (!is_socket_accept) {
			if (stdin_socket) service << "fdmove -c 0 3\n";
		}
	} else {
		service << setsid;
		service << redirect;
		service << chroot;
		service << chdir;
		service << softlimit;
		service << setuidgid;
		service << um;
		service << env;
		service << login_prompt;
	}
	service << (execstart ? names.substitute(strip_leading_minus(execstart->datum)) : is_remain ? "true" : "pause") << "\n";

	// nosh is not suitable here, since the restart script is passed arguments.
	restart_script << "#!/bin/sh\n" << multi_line_comment("Restart file generated from " + service_filename);
	if (restartsec) restart_script << "sleep " << restartsec->datum << "\n";
	if (restart && "always" == tolower(restart->datum)) {
		restart_script << "exec true\n";
	} else
	if ((!restart || "no" == tolower(restart->datum))) {
		restart_script << "exec false\n";
	} else 
	{
		restart_script << 
			"case \"$1\" in\n"
			"\te*)\n"
			"\t\tif [ \"$2\" -ne 0 ]\n"
			"\t\tthen\n"
			"\t\texec " << (restart && ("on-failure" == tolower(restart->datum)) ? "true" : "false") << "\n"
			"\t\telse\n"
			"\t\texec " << (restart && ("on-success" == tolower(restart->datum)) ? "true" : "false") << "\n"
			"\t\tfi\n"
			"\t\t;;\n"
			"\tt*)\n"
			"\t\t;;\n"
			"\tk*)\n"
			"\t\t;;\n"
			"\ta*)\n"
			"\t\texec " << (restart && ("on-abort" == tolower(restart->datum)) ? "true" : "false") << "\n"
			"\t\t;;\n"
			"\tc*|*)\n"
			"\t\texec " << (restart && ("on-abort" == tolower(restart->datum)) ? "true" : "false") << "\n"
			"\t\t;;\n"
			"esac\n"
			"exec false\n";
	}

	stop << "#!/bin/nosh\n" << multi_line_comment("Stop file generated from " + service_filename);
	if (execstoppost) {
		stop << softlimit;
		if (setuidgidall) stop << setuidgid;
		stop << chdir;
		stop << env;
		stop << um;
		if (chrootall) stop << chroot;
		stop << names.substitute(strip_leading_minus(execstoppost->datum)) << "\n"; 
	} else 
		stop << "true\n";

	// Set the dependency and installation information.

	CREATE_LINKS(prog, is_target, socket_after, names.query_bundle_dirname(), "/after/");
	CREATE_LINKS(prog, is_target, service_after, names.query_bundle_dirname(), "/after/");
	CREATE_LINKS(prog, is_target, socket_before, names.query_bundle_dirname(), "/before/");
	CREATE_LINKS(prog, is_target, service_before, names.query_bundle_dirname(), "/before/");
	CREATE_LINKS(prog, is_target, socket_wants, names.query_bundle_dirname(), "/wants/");
	CREATE_LINKS(prog, is_target, service_wants, names.query_bundle_dirname(), "/wants/");
	CREATE_LINKS(prog, is_target, socket_requires, names.query_bundle_dirname(), "/wants/");
	CREATE_LINKS(prog, is_target, service_requires, names.query_bundle_dirname(), "/wants/");
	CREATE_LINKS(prog, is_target, socket_requisite, names.query_bundle_dirname(), "/wants/");
	CREATE_LINKS(prog, is_target, service_requisite, names.query_bundle_dirname(), "/wants/");
	CREATE_LINKS(prog, is_target, socket_conflicts, names.query_bundle_dirname(), "/conflicts/");
	CREATE_LINKS(prog, is_target, service_conflicts, names.query_bundle_dirname(), "/conflicts/");
	CREATE_LINKS(prog, is_target, socket_wantedby, names.query_bundle_dirname(), "/wanted-by/");
	CREATE_LINKS(prog, is_target, service_wantedby, names.query_bundle_dirname(), "/wanted-by/");
	CREATE_LINKS(prog, is_target, socket_requiredby, names.query_bundle_dirname(), "/wanted-by/");
	CREATE_LINKS(prog, is_target, service_requiredby, names.query_bundle_dirname(), "/wanted-by/");
	CREATE_LINKS(prog, is_target, socket_stoppedby, names.query_bundle_dirname(), "/stopped-by/");
	CREATE_LINKS(prog, is_target, service_stoppedby, names.query_bundle_dirname(), "/stopped-by/");
	const bool defaultdependencies(
			(is_socket_activated && is_bool_true(socket_defaultdependencies, true)) || 
			is_bool_true(service_defaultdependencies, true)
	);
	const bool earlysupervise(
			(is_socket_activated && is_bool_true(socket_earlysupervise, false)) || 
			is_bool_true(service_earlysupervise, false)
	);
	if (defaultdependencies) {
		if (is_socket_activated)
			create_links(prog, is_target, "sockets.target", names.query_bundle_dirname(), "/wanted-by/");
		create_links(prog, is_target, "basic.target", names.query_bundle_dirname(), "/after/");
		create_links(prog, is_target, "basic.target", names.query_bundle_dirname(), "/wants/");
		create_links(prog, is_target, "shutdown.target", names.query_bundle_dirname(), "/before/");
		create_links(prog, is_target, "shutdown.target", names.query_bundle_dirname(), "/stopped-by/");
	}
	if (earlysupervise) {
		create_link(prog, "/run/system-manager/early-supervise/" + names.query_bundle_basename(), names.query_bundle_dirname() + "/supervise");
	}

	// Issue the final reports.

	report_unused(prog, socket_profile, socket_filename);
	report_unused(prog, service_profile, service_filename);

	throw EXIT_SUCCESS;
}
