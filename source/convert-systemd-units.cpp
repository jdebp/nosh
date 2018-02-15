/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <map>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <cerrno>
#include <iostream>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>	// Necessary for the SO_REUSEPORT macro.
#include <netinet/in.h>	// Necessary for the IPV6_V6ONLY macro.
#include <unistd.h>
#include <pwd.h>
#include "utils.h"
#include "runtime-dir.h"
#include "fdutils.h"
#include "service-manager-client.h"
#include "popt.h"
#include "FileStar.h"
#include "FileDescriptorOwner.h"
#include "bundle_creation.h"
#include "machine_id.h"

static 
const char * systemd_prefixes[] = {
	"/run/", "/etc/", "/lib/", "/usr/lib/", "/usr/local/lib/"
};

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
			path = ((std::string(*p) + "systemd/") + (per_user_mode ? "user/" : "system/")) + name;
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
	typedef std::list<std::string> settings;
	value() : used(false), d() {}
	value(const std::string & v) : used(false), d() { d.push_back(v); }
	void append(const std::string & v) { d.push_back(v); }
	std::string last_setting() const { return d.empty() ? std::string() : d.back(); }
	const settings & all_settings() const { return d; }
	bool used;
protected:
	settings d;
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
	void append ( 
		const std::string & k0, ///< must be all lowercase
		const std::string & k1, ///< must be all lowercase
		const std::string & v 
	) 
	{
		m0[k0][k1].append(v);
	}

	FirstLevel m0;
};

static
bool
strip_leading_minus (
	const std::string & s,
	std::string & r
) {
	if (s.length() < 1 || '-' != s[0]) {
		r = s;
		return false;
	} else {
		r = s.substr(1, std::string::npos);
		return true;
	}
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
escape_newlines (
	const std::string & s
) {
	std::string r;
	for (std::string::const_iterator p(s.begin()); s.end() != p; ++p) {
		const char c(*p);
		if ('\n' == c) 
			r += " \\";
		r += c;
	}
	return r;
}

static
std::string
quote (
	const std::string & s
) {
	std::string r;
	bool quote(s.empty());
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

/// \brief Convert from nosh and systemd quoting and escaping rules to the ones for sh.
/// sh metacharacters other than $ and # are also disabled by escaping them.
static
std::string
escape_metacharacters (
	const std::string & s
) {
	std::string r;
	enum { NORMAL, DQUOT, SQUOT } state(NORMAL);
	for (std::string::const_iterator p(s.begin()); s.end() != p; ++p) {
		char c(*p);
		switch (state) {
			case NORMAL:
				if ('\\' == c && p + 1 != s.end()) {
					r += c; 
					c = *++p;
				} else if ('\'' == c) state = SQUOT;
				else if ('\"' == c) state = DQUOT;
				else if (';' == c || '|' == c || '&' == c)
					r += '\\';
				break;
			case DQUOT:
				if ('\\' == c && p + 1 != s.end()) {
					// nosh and systemd escaping within double quotes differs from that of sh.
					// sh has some quite wacky rules where \ is mostly not an escape character.
					c = *++p;
					if ('`' == c || '\\' == c || '\"' == c)
						r += '\\'; 
				} else if ('\"' == c) state = NORMAL;
				break;
			case SQUOT:
				if ('\\' == c && p + 1 != s.end()) {
					// nosh and systemd escaping within single quotes differs from that of sh.
					// In the sh rules \ is never an escape character and There is no way to quote ' .
					c = *++p;
					if ('\'' == c)
						r += "'\\'"; 
				} else if ('\'' == c) state = NORMAL;
				break;
		}
		r += c;
	}
	return r;
}

static inline
std::string
leading_slashify (
	const std::string & s
) {
	return "/" == s.substr(0, 1) ? s : "/" + s;
}

static inline
std::string
shell_expand (
	const std::string & s
) {
	enum { NORMAL, DQUOT, SQUOT } state(NORMAL);
	for (std::string::const_iterator p(s.begin()); s.end() != p; ++p) {
		const char c(*p);
		switch (state) {
			case NORMAL:
				if ('\\' == c && p != s.end()) ++p;
				else if ('\'' == c) state = SQUOT;
				else if ('\"' == c) state = DQUOT;
				else if ('$' == c)
					return "sh -c " + quote("exec " + escape_metacharacters(s));
				break;
			case DQUOT:
				if ('\\' == c && p != s.end()) ++p;
				else if ('\"' == c) state = NORMAL;
				else if ('$' == c)
					return "sh -c " + quote("exec " + escape_metacharacters(s));
				break;
			case SQUOT:
				if ('\\' == c && p != s.end()) ++p;
				else if ('\'' == c) state = NORMAL;
				break;
		}
	}
	return s;
}

static inline
std::string
effective_user_name ()
{
	if (struct passwd * p = getpwuid(geteuid()))
		if (p->pw_name) {
			const std::string n(p->pw_name);
			endpwent();
			return n;
		}
	endpwent();
	return "nobody";
}

static const std::string slash("/");

static inline
std::string
effective_user_log_dir()
{
	std::string r("/var/log/user/");
	// Do not use cuserid() here.
	// BSD libc defines L_cuserid as 17.
	// But GNU libc is even worse and defines it as a mere 9.
	if (struct passwd * p = getpwuid(geteuid()))
		r += p->pw_name;
	endpwent();
	return r + slash;
}

namespace {
struct names {
	names(const char * a);
	void set_prefix(const std::string & v, bool esc, bool alt, bool ext) { set(esc, alt, ext, escaped_prefix, prefix, v); }
	void set_instance(const std::string & v, bool esc, bool alt, bool ext) { set(esc, alt, ext, escaped_instance, instance, v); }
	void set_bundle(const std::string & r, const std::string & b) { bundle_basename = b; bundle_dirname = r + b; }
	void set_machine_id(const std::string & v) { machine_id = v; }
	void set_user(const std::string & u);
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
	const std::string & query_machine_id () const { return machine_id; }
	const std::string & query_user_name () const { return user_name; }
	const std::string & query_UID () const { return UID; }
	const std::string & query_runtime_dir_by_name () const { return runtime_dir_by_name; }
	const std::string & query_runtime_dir_by_UID () const { return runtime_dir_by_UID; }
	const std::string & query_state_dir () const { return state_dir; }
	const std::string & query_cache_dir () const { return cache_dir; }
	const std::string & query_log_dir () const { return log_dir; }
	std::string substitute ( const std::string & );
	std::list<std::string> substitute ( const std::list<std::string> & );
protected:
	std::string arg_name, unit_dirname, unit_basename, escaped_unit_basename, prefix, escaped_prefix, instance, escaped_instance, bundle_basename, bundle_dirname, machine_id, user_name, UID, runtime_dir_by_name, runtime_dir_by_UID, state_dir, cache_dir, log_dir;
	void set ( bool esc, bool alt, bool ext, std::string & escaped, std::string & normal, const std::string & value ) {
		if (esc)
			escaped = systemd_name_escape(alt, ext, normal = value);
		else
			normal = systemd_name_unescape(alt, ext, escaped = value);
	}
};
}

names::names(const char * a) : 
	arg_name(a), 
	user_name(per_user_mode ? effective_user_name() : "root"), 
	UID("0"),
	runtime_dir_by_name("/run"),
	runtime_dir_by_UID("/run"),
	state_dir("/var/lib/"),
	cache_dir("/var/cache/"),
	log_dir(per_user_mode ? effective_user_log_dir() : "/var/log/sv/")
{ 
	split_name(a, unit_dirname, unit_basename); 
	escaped_unit_basename = systemd_name_escape(false, false, unit_basename); 
	if (per_user_mode) {
		std::string home_dir;
		get_user_dirs_for(user_name, UID, runtime_dir_by_name, runtime_dir_by_UID, home_dir);
		cache_dir = home_dir + "/.cache";
		state_dir = home_dir + "/.config";
	}
}

void
names::set_user(
	const std::string & v
) {
	user_name = v;
	if (per_user_mode) {
		std::string home_dir;
		get_user_dirs_for(user_name, UID, runtime_dir_by_name, runtime_dir_by_UID, home_dir);
		cache_dir = home_dir + "/.cache";
		state_dir = home_dir + "/.config";
	}
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
			case 'f': r += leading_slashify(query_instance()); break;
			case 'n': r += query_escaped_unit_basename(); break;
			case 'N': r += query_unit_basename(); break;
			case 'm': r += query_machine_id(); break;
			case 't': r += query_runtime_dir_by_name(); break;
			case 'T': r += query_runtime_dir_by_UID(); break;
			case 'S': r += query_state_dir(); break;
			case 'C': r += query_cache_dir(); break;
			case 'L': r += query_log_dir(); break;
			case 'u': r += query_user_name(); break;
			case 'U': r += query_UID(); break;
			case '%': default:	r += '%'; r += c; break;
		}
	}
	return r;
}

std::list<std::string> 
names::substitute ( 
	const std::list<std::string> & l
) {
	std::list<std::string> r;
	for (std::list<std::string>::const_iterator i(l.begin()); l.end() != i; ++i)
		r.push_back(substitute(*i));
	return r;
}

#if 0 // This code is unused, for now.
static
bool
is_numeric ( 
	const std::string & v
) {
	for (std::list<std::string>::const_iterator i(v.begin()); v.end() != i; ++i) {
		if (v.begin() == i) {
			if (!std::isdigit(*i) && '-' != *i && '+' != *i) return false;
		} else {
			if (!std::isdigit(*i)) return false;
		}
	}
	return true;
}
#endif

static
bool
is_string (
	const value & v,
	const std::string & s
) {
	const std::string r(tolower(v.last_setting()));
	return s == r;
}

static
bool
is_string (
	const value & v,
	const std::string & s0,
	const std::string & s1
) {
	const std::string r(tolower(v.last_setting()));
	return s0 == r || s1 == r;
}

static
bool
is_string (
	const value * v,
	const std::string & s,
	const bool def
) {
	return v ? is_string(*v, s) : def;
}

static
bool
is_string (
	const value * v,
	const std::string & s
) {
	return v && is_string(*v, s);
}

static
bool
is_string (
	const value * v,
	const char * s0,
	const char * s1
) {
	return v && is_string(*v, s0, s1);
}

static
bool
is_bool_true (
	const value & v
) {
	const std::string r(tolower(v.last_setting()));
	return is_bool_true(r) || "1" == r;
}

static
bool
is_bool_true (
	const value * v,
	bool def
) {
	if (v) return is_bool_true(*v);
	return def;
}

static
bool
is_bool_true (
	const value * v,
	const value * w,
	bool def
) {
	if (v) return is_bool_true(*v);
	if (w) return is_bool_true(*w);
	return def;
}

static
bool
is_local_socket_name (
	const std::string & s
) {
	return s.length() > 0 && '/' == s[0];
}

static inline
void
split_limit (
	const std::string & t,
	const std::string & o,
	std::string & s,
	std::string & h
) {
	const std::string::size_type colon(t.rfind(':'));
	const std::string option(" -" + o + " ");
	if (std::string::npos == colon) {
		if ("m" == o)
			s += option + quote(t);
		else
			s += option + "hard";
		h += option + quote(t);
	} else {
		const std::string::size_type len(t.length());
		if (1U < len) {
			if (len - 1U == colon) {
				s += option + quote(t.substr(0, colon));
			} else
			if (0 == colon) {
				h += option + quote(t.substr(colon + 1, std::string::npos));
			} else {
				s += option + quote(t.substr(0, colon));
				h += option + quote(t.substr(colon + 1, std::string::npos));
			}
		}
	}
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

static
void
split_netlink_socket_name (
	const std::string & s, 
	std::string & protocol, 
	std::string & multicast_group
) {
	const std::string::size_type space(s.rfind(' '));
	if (std::string::npos == space) {
		multicast_group = "1";
		protocol = s;
	} else {
		protocol = s.substr(0, space);
		multicast_group = s.substr(space + 1, std::string::npos);
	}
}

#if defined(__LINUX__) || defined(__linux__)
static inline
std::string
set_controller_command (
	unsigned levels,
	const char * controller,
	bool enable
) {
	const std::string flag(enable ? "+" : "-");
	const std::string p("foreground set-control-group-knob ");
	const std::string s("cgroup.subtree_control " + quote(flag + controller) + " ;\n");
	std::string r, d;
	while (levels > 0U) {
		d += "../";
		r = p + d + s + r;
		--levels;
	}
	return r;
}
#endif

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
		p.append(section, tolower(var), val);
	}
}

static
void
load (
	const char * prog,
	profile & p,
	FILE * file,
	const std::string & unit_name,
	const std::string & file_name
) {
	if (!file) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, unit_name.c_str(), std::strerror(error));
		throw EXIT_FAILURE;
	}
	if (!is_regular(prog, file_name, file))
		throw EXIT_FAILURE;
	load(p, file);
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
report_unused (
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
				for (value::settings::const_iterator i(v.all_settings().begin()); v.all_settings().end() != i; ++i) {
				       const std::string & val(*i);
				       std::fprintf(stderr, "%s: WARNING: %s: Unused setting: [%s] %s = %s\n", prog, name.c_str(), section.c_str(), var.c_str(), val.c_str());
				}
			}
		}
	}
}

/* Main function ************************************************************
// **************************************************************************
*/

void
convert_systemd_units [[gnu::noreturn]] (
	const char * & /*next_prog*/,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	std::string bundle_root;
	bool escape_instance(false), escape_prefix(false), alt_escape(false), ext_escape(false), etc_bundle(false), local_bundle(false), systemd_quirks(true), generation_comment(true);
	try {
		const char * bundle_root_str(0);
		bool no_ext_escape(false), no_systemd_quirks(false), no_generation_comment(false);
		popt::bool_definition user_option('u', "user", "Create a bundle that runs under the per-user manager.", per_user_mode);
		popt::string_definition bundle_option('\0', "bundle-root", "directory", "Root directory for bundles.", bundle_root_str);
		popt::bool_definition escape_instance_option('\0', "escape-instance", "Escape the instance part of a template instantiation.", escape_instance);
		popt::bool_definition escape_prefix_option('\0', "escape-prefix", "Escape the prefix part of a template instantiation.", escape_prefix);
		popt::bool_definition alt_escape_option('\0', "alt-escape", "Use an alternative escape algorithm.", alt_escape);
		popt::bool_definition no_ext_escape_option('\0', "no-ext-escape", "Do not use an extended escape sequences.", no_ext_escape);
		popt::bool_definition etc_bundle_option('\0', "etc-bundle", "Consider this service to live in the /etc/service-bundles/ area.", etc_bundle);
		popt::bool_definition local_bundle_option('\0', "local-bundle", "Consider this service to live in a service bundle area like /var/local/sv/.", local_bundle);
		popt::bool_definition no_systemd_quirks_option('\0', "no-systemd-quirks", "Turn off systemd quirks.", no_systemd_quirks);
		popt::bool_definition no_generation_comment_option('\0', "no-generation-comment", "Turn off the comment that mentions the source file.", no_generation_comment);
		popt::definition * main_table[] = {
			&user_option,
			&bundle_option,
			&escape_instance_option,
			&escape_prefix_option,
			&alt_escape_option,
			&no_ext_escape_option,
			&etc_bundle_option,
			&local_bundle_option,
			&no_systemd_quirks_option,
			&no_generation_comment_option
		};
		popt::top_table_definition main_option(sizeof main_table/sizeof *main_table, main_table, "Main options", "unit");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		if (p.stopped()) throw EXIT_SUCCESS;
		if (bundle_root_str) bundle_root = bundle_root_str + std::string("/");
		ext_escape = !no_ext_escape;
		systemd_quirks = !no_systemd_quirks;
		generation_comment = !no_generation_comment;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw static_cast<int>(EXIT_USAGE);
	}

	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing argument(s).");
		throw static_cast<int>(EXIT_USAGE);
	}

	struct names names(args.front());

	args.erase(args.begin());
	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Unrecognized argument(s).");
		throw EXIT_FAILURE;
	}

	bool is_socket_activated(false), is_target(false), merge_run_into_start(false);

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
		names.set_bundle(bundle_root, bundle_basename);
	}

	names.set_prefix(names.query_bundle_basename(), escape_prefix, alt_escape, ext_escape);

	machine_id::erase();
	if (!machine_id::read_non_volatile() && !machine_id::read_fallbacks(envs))
	       machine_id::create();
	names.set_machine_id(machine_id::human_readable_form_compact());

	bool is_instance(false), is_socket_accept(false);
	if (is_socket_activated) {
		std::string socket_unit_name(names.query_arg_name());
		FileStar socket_file(find(socket_unit_name, socket_filename));

		if (!socket_file && ENOENT == errno) {
			std::string::size_type atc(names.query_bundle_basename().find('@'));
			if (names.query_bundle_basename().npos != atc) {
				names.set_prefix(names.query_bundle_basename().substr(0, atc), escape_prefix, alt_escape, ext_escape);
				++atc;
				names.set_instance(names.query_bundle_basename().substr(atc, names.query_bundle_basename().npos), escape_instance, alt_escape, ext_escape);
				is_instance = true;
				socket_unit_name = names.query_unit_dirname() + names.query_escaped_prefix() + "@.socket";
				socket_file = find(socket_unit_name, socket_filename);
			}
		}

		load(prog, socket_profile, socket_file, socket_unit_name, socket_filename);

		value * accept(socket_profile.use("socket", "accept"));
		is_socket_accept = is_bool_true(accept, false);

		const std::string service_unit_name(names.query_unit_dirname() + names.query_escaped_prefix() + (is_socket_accept ? "@" : "") + ".service");
		FileStar service_file(find(service_unit_name, service_filename));
				
		load(prog, service_profile, service_file, service_unit_name, service_filename);
	} else {
		std::string service_unit_name(names.query_arg_name());
		FileStar service_file(find(service_unit_name, service_filename));

		if (!service_file && ENOENT == errno) {
			std::string::size_type atc(names.query_bundle_basename().find('@'));
			if (names.query_bundle_basename().npos != atc) {
				names.set_prefix(names.query_bundle_basename().substr(0, atc), escape_prefix, alt_escape, ext_escape);
				++atc;
				names.set_instance(names.query_bundle_basename().substr(atc, names.query_bundle_basename().npos), escape_instance, alt_escape, ext_escape);
				is_instance = true;
				service_unit_name = (names.query_unit_dirname() + names.query_escaped_prefix() + "@.") + (is_target ? "target" : "service");
				service_file = find(service_unit_name, service_filename);
			}
		}
				
		load(prog, service_profile, service_file, service_unit_name, service_filename);
	}

	value * listenstream(socket_profile.use("socket", "listenstream"));
	value * listenfifo(socket_profile.use("socket", "listenfifo"));
	value * listennetlink(socket_profile.use("socket", "listennetlink"));
	value * listendatagram(socket_profile.use("socket", "listendatagram"));
	value * listensequentialpacket(socket_profile.use("socket", "listensequentialpacket"));
	value * filedescriptorname(socket_profile.use("socket", "filedescriptorname"));
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
	value * socketuser(socket_profile.use("socket", "socketuser"));
	value * socketgroup(socket_profile.use("socket", "socketgroup"));
	value * passcredentials(socket_profile.use("socket", "passcredentials"));
	value * passsecurity(socket_profile.use("socket", "passsecurity"));
	value * nodelay(socket_profile.use("socket", "nodelay"));
	value * freebind(socket_profile.use("socket", "freebind"));
	value * receivebuffer(socket_profile.use("socket", "receivebuffer"));
	value * netlinkraw(socket_profile.use("socket", "netlinkraw"));	// This is an extension to systemd.
	value * sslshim(socket_profile.use("socket", "sslshim"));	// This is an extension to systemd.
	value * socket_before(socket_profile.use("unit", "before"));
	value * socket_after(socket_profile.use("unit", "after"));
	value * socket_conflicts(socket_profile.use("unit", "conflicts"));
	value * socket_wants(socket_profile.use("unit", "wants"));
	value * socket_requires(socket_profile.use("unit", "requires"));
	value * socket_requisite(socket_profile.use("unit", "requisite"));
	value * socket_partof(socket_profile.use("unit", "partof"));
	value * socket_description(socket_profile.use("unit", "description"));
	value * socket_defaultdependencies(socket_profile.use("unit", "defaultdependencies"));
	value * socket_earlysupervise(socket_profile.use("unit", "earlysupervise"));	// This is an extension to systemd.
	value * socket_wantedby(socket_profile.use("install", "wantedby"));
	value * socket_requiredby(socket_profile.use("install", "requiredby"));
	value * socket_stoppedby(socket_profile.use("install", "stoppedby"));	// This is an extension to systemd.
	value * socket_ucspirules(socket_profile.use("socket", "ucspirules"));	// This is an extension to systemd.
	value * socket_logucspirules(socket_profile.use("socket", "logucspirules"));	// This is an extension to systemd.

	value * type(service_profile.use("service", "type"));
	value * busname(service_profile.use("service", "busname"));
	value * workingdirectory(service_profile.use("service", "workingdirectory"));
	value * rootdirectory(service_profile.use("service", "rootdirectory"));
#if defined(__LINUX__) || defined(__linux__)
	value * slice(service_profile.use("service", "slice"));
	value * delegate(service_profile.use("service", "delegate"));
#else
	value * jailid(service_profile.use("service", "jailid"));	// This is an extension to systemd.
#endif
	value * runtimedirectory(service_profile.use("service", "runtimedirectory"));
	value * runtimedirectoryowner(service_profile.use("service", "runtimedirectoryowner"));
	value * runtimedirectorymode(service_profile.use("service", "runtimedirectorymode"));
	value * systemdworkingdirectory(service_profile.use("service", "systemdworkingdirectory"));	// This is an extension to systemd.
	value * systemduserenvironment(service_profile.use("service", "systemduserenvironment"));	// This is an extension to systemd.
	value * systemdusergroups(service_profile.use("service", "systemdusergroups"));	// This is an extension to systemd.
	value * machineenvironment(service_profile.use("service", "machineenvironment"));	// This is an extension to systemd.
	value * execstart(service_profile.use("service", "execstart"));
	value * execstartpre(service_profile.use("service", "execstartpre"));
	value * execrestartpre(service_profile.use("service", "execrestartpre"));	// This is an extension to systemd.
	value * execstoppost(service_profile.use("service", "execstoppost"));
	value * limitnofile(service_profile.use("service", "limitnofile"));
	value * limitcpu(service_profile.use("service", "limitcpu"));
	value * limitcore(service_profile.use("service", "limitcore"));
	value * limitnproc(service_profile.use("service", "limitnproc"));
	value * limitfsize(service_profile.use("service", "limitfsize"));
	value * limitas(service_profile.use("service", "limitas"));
	value * limitrss(service_profile.use("service", "limitrss"));
	value * limitdata(service_profile.use("service", "limitdata"));
	value * limitstack(service_profile.use("service", "limitstack"));
	value * limitmemory(service_profile.use("service", "limitmemory"));	// This is an extension to systemd.
	value * limitmemlock(service_profile.use("service", "limitmemlock"));
#if defined(RLIMIT_NICE)
	value * limitnice(service_profile.use("service", "limitnice"));
#endif
#if defined(RLIMIT_SIGPENDING)
	value * limitsigpending(service_profile.use("service", "limitsigpending"));
#endif
#if defined(RLIMIT_PIPE)
	value * limitpipe(service_profile.use("service", "limitpipe"));
#endif
#if defined(RLIMIT_MSGQUEUE)
	value * limitmsgqueue(service_profile.use("service", "limitmsgqueue"));
#endif
#if defined(RLIMIT_LOCKS)
	value * limitlocks(service_profile.use("service", "limitlocks"));
#endif
	value * killmode(service_profile.use("service", "killmode"));
	value * killsignal(service_profile.use("service", "killsignal"));
	value * sendsigkill(service_profile.use("service", "sendsigkill"));
	value * sendsighup(service_profile.use("service", "sendsighup"));
	value * rootdirectorystartonly(service_profile.use("service", "rootdirectorystartonly"));
	value * permissionsstartonly(service_profile.use("service", "permissionsstartonly"));
	value * standardinput(service_profile.use("service", "standardinput"));
	value * standardoutput(service_profile.use("service", "standardoutput"));
	value * standarderror(service_profile.use("service", "standarderror"));
	value * user(service_profile.use("service", "user"));
	value * group(service_profile.use("service", "group"));
	value * umask(service_profile.use("service", "umask"));
	value * environment(service_profile.use("service", "environment"));
	value * unsetenvironment(service_profile.use("service", "unsetenvironment"));
	value * environmentfile(service_profile.use("service", "environmentfile"));
	value * environmentdirectory(service_profile.use("service", "environmentdirectory"));	// This is an extension to systemd.
	value * fullenvironmentdirectory(service_profile.use("service", "fullenvironmentdirectory"));	// This is an extension to systemd.
	value * environmentuser(service_profile.use("service", "environmentuser"));	// This is an extension to systemd.
	value * environmentappendpath(service_profile.use("service", "environmentappendpath"));	// This is an extension to systemd.
#if defined(__LINUX__) || defined(__linux__)
	value * utmpidentifier(service_profile.use("service", "utmpidentifier"));
#endif
	value * ttypath(service_profile.use("service", "ttypath"));
	value * ttyfromenv(service_profile.use("service", "ttyfromenv"));	// This is an extension to systemd.
	value * ttyreset(service_profile.use("service", "ttyreset"));
	value * ttyprompt(service_profile.use("service", "ttyprompt"));	// This is an extension to systemd.
	value * bannerfile(service_profile.use("service", "bannerfile"));	// This is an extension to systemd.
	value * bannerline(service_profile.use("service", "bannerline"));	// This is an extension to systemd.
	value * ttyvhangup(service_profile.use("service", "ttyvhangup"));
//	value * ttyvtdisallocate(service_profile.use("service", "ttyvtdisallocate"));
	value * remainafterexit(service_profile.use("service", "remainafterexit"));
	value * processgroupleader(service_profile.use("service", "processgroupleader"));	// This is an extension to systemd.
	value * sessionleader(service_profile.use("service", "sessionleader"));	// This is an extension to systemd.
	value * localreaper(service_profile.use("service", "localreaper"));	// This is an extension to systemd.
	value * restart(service_profile.use("service", "restart"));
	value * restartsec(service_profile.use("service", "restartsec"));
//	value * ignoresigpipe(service_profile.use("service", "ignoresigpipe"));
#if defined(__LINUX__) || defined(__linux__)
	value * privatetmp(service_profile.use("service", "privatetmp"));
	value * privatedevices(service_profile.use("service", "privatedevices"));
	value * privatenetwork(service_profile.use("service", "privatenetwork"));
	value * protecthome(service_profile.use("service", "protecthome"));
	value * protectsystem(service_profile.use("service", "protectsystem"));
	value * protectcontrolgroups(service_profile.use("service", "protectcontrolgroups"));
//	value * protectkernelmodules(service_profile.use("service", "protectkernelmodules"));
	value * protectkerneltunables(service_profile.use("service", "protectkerneltunables"));
	value * readwritedirectories(service_profile.use("service", "readwritedirectories"));
	value * readonlypaths(service_profile.use("service", "readonlypaths"));
	value * inaccessiblepaths(service_profile.use("service", "inaccessiblepaths"));
	value * mountflags(service_profile.use("service", "mountflags"));
	value * ioschedulingclass(service_profile.use("service", "ioschedulingclass"));
	value * ioschedulingpriority(service_profile.use("service", "ioschedulingpriority"));
	value * cpuschedulingresetonfork(service_profile.use("service", "cpuschedulingresetonfork"));
	value * numainterleave(service_profile.use("service", "numainterleave"));
#endif
	value * numamembind(service_profile.use("service", "numamembind"));
	value * numacpunodebind(service_profile.use("service", "numacpunodebind"));
#if defined(__LINUX__) || defined(__linux__)
	value * numaphyscpubind(service_profile.use("service", "numaphyscpubind"));
	value * numalocalalloc(service_profile.use("service", "numalocalalloc"));
	value * numapreferred(service_profile.use("service", "numapreferred"));
	value * cpuaccounting(service_profile.use("service", "cpuaccounting"));
	value * cpuweight(service_profile.use("service", "cpuweight"));
	value * cpuquota(service_profile.use("service", "cpuquota"));
	value * tasksaccounting(service_profile.use("service", "tasksaccounting"));
	value * tasksmax(service_profile.use("service", "tasksmax"));
	value * memoryaccounting(service_profile.use("service", "memoryaccounting"));
	value * memorylow(service_profile.use("service", "memorylow"));
	value * memoryhigh(service_profile.use("service", "memoryhigh"));
	value * memorymax(service_profile.use("service", "memorymax"));
	value * memoryswapmax(service_profile.use("service", "memoryswapmax"));
	value * ioaccounting(service_profile.use("service", "ioaccounting"));
	value * rdmaaccounting(service_profile.use("service", "rdmaaccounting"));	// This is an extension to systemd.
	value * ioweight(service_profile.use("service", "ioweight"));
	value * iodeviceweight(service_profile.use("service", "iodeviceweight"));
	value * ioreadbandwidthmax(service_profile.use("service", "ioreadbandwidthmax"));
	value * ioreadiopsmax(service_profile.use("service", "ioreadiopsmax"));
	value * iowritebandwidthmax(service_profile.use("service", "iowritebandwidthmax"));
	value * iowriteiopsmax(service_profile.use("service", "iowriteiopsmax"));
	value * rdmahcahandlesmax(service_profile.use("service", "rdmahcahandlesmax"));	// This is an extension to systemd.
	value * rdmahcaobjectsmax(service_profile.use("service", "rdmahcaobjectsmax"));	// This is an extension to systemd.
#endif
	value * cpuaffinity(service_profile.use("service", "cpuaffinity"));
	value * jvmdefault(service_profile.use("service", "jvmdefault"));	// This is an extension to systemd.
	value * jvmversions(service_profile.use("service", "jvmversions"));	// This is an extension to systemd.
	value * jvmoperatingsystems(service_profile.use("service", "jvmoperatingsystems"));	// This is an extension to systemd.
	value * jvmmanufacturers(service_profile.use("service", "jvmmanufacturers"));	// This is an extension to systemd.
	value * oomscoreadjust(service_profile.use("service", "oomscoreadjust"));
	value * cpuschedulingpolicy(service_profile.use("service", "cpuschedulingpolicy"));
	value * cpuschedulingpriority(service_profile.use("service", "cpuschedulingpriority"));
	value * service_defaultdependencies(service_profile.use("unit", "defaultdependencies"));
	value * service_earlysupervise(service_profile.use("unit", "earlysupervise"));	// This is an extension to systemd.
	value * service_after(service_profile.use("unit", "after"));
	value * service_before(service_profile.use("unit", "before"));
	value * service_conflicts(service_profile.use("unit", "conflicts"));
	value * service_wants(service_profile.use("unit", "wants"));
	value * service_requires(service_profile.use("unit", "requires"));
	value * service_requisite(service_profile.use("unit", "requisite"));
	value * service_partof(service_profile.use("unit", "partof"));
	value * service_description(service_profile.use("unit", "description"));
	value * service_wantedby(service_profile.use("install", "wantedby"));
	value * service_requiredby(service_profile.use("install", "requiredby"));
	value * service_stoppedby(service_profile.use("install", "stoppedby"));	// This is an extension to systemd.
	value * service_ucspirules(service_profile.use("service", "ucspirules"));	// This is an extension to systemd.
	value * service_logucspirules(service_profile.use("service", "logucspirules"));	// This is an extension to systemd.

	if (user)
		names.set_user(names.substitute(user->last_setting()));

	// Actively prevent certain unsupported combinations.

	if (type && "simple" != tolower(type->last_setting()) && "forking" != tolower(type->last_setting()) && "oneshot" != tolower(type->last_setting()) && "dbus" != tolower(type->last_setting())) {
		std::fprintf(stderr, "%s: FATAL: %s: %s: %s\n", prog, service_filename.c_str(), type->last_setting().c_str(), "Not a supported service type.");
		throw EXIT_FAILURE;
	}
	const bool is_oneshot(is_string(type, "oneshot", systemd_quirks && !execstart));
	const bool is_dbus(is_string(type, "dbus", systemd_quirks && busname));
	if (is_dbus) {
		if (!busname && systemd_quirks)
			std::fprintf(stderr, "%s: WARNING: %s: %s\n", prog, service_filename.c_str(), "Ignoring that the BusName entry is missing.");
	} else {
		if (busname && (type || !systemd_quirks)) 
			busname->used = false;
	}
	const bool is_remain(is_bool_true(remainafterexit, is_target));
	if (!is_remain) {
		if (is_oneshot) {
			if (restart ? "no" != tolower(restart->last_setting()) && "never" != tolower(restart->last_setting()) : !systemd_quirks)
				std::fprintf(stderr, "%s: WARNING: %s: restart=%s: %s\n", prog, service_filename.c_str(), restart ? restart->last_setting().c_str() : "always", "Single-shot service may never reach ready");
		} else {
			if (!execstart) {
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, service_filename.c_str(), "Missing mandatory ExecStart entry.");
				throw EXIT_FAILURE;
			}
		}
	}
	if (is_socket_activated) {
		if (!listenstream && !listendatagram && !listenfifo && !listennetlink && !listensequentialpacket) {
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, socket_filename.c_str(), "Missing mandatory ListenStream/ListenDatagram/ListenFIFO/ListenSequentialPacket entry.");
			throw EXIT_FAILURE;
		}
		if (listendatagram) {
			if (is_socket_accept) {
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, socket_filename.c_str(), "ListenDatagram sockets may not have Accept=yes.");
				throw EXIT_FAILURE;
			}
		}
		if (listenfifo) {
			if (is_socket_accept) {
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, socket_filename.c_str(), "ListenFIFO sockets may not have Accept=yes.");
				throw EXIT_FAILURE;
			}
		}
		if (listennetlink) {
			if (is_socket_accept) {
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, socket_filename.c_str(), "ListenNetlink sockets may not have Accept=yes.");
				throw EXIT_FAILURE;
			}
		}
	}
	const bool is_ucspirules(is_bool_true(socket_ucspirules, service_ucspirules, false));
	if (is_ucspirules && (!is_socket_activated || !is_socket_accept)) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, socket_filename.c_str(), "UCSPI rules only apply to accepting sockets.");
		throw EXIT_FAILURE;
	}
	// We silently set "control-group" as the default killmode, not "process", "mixed", or "none".
	if (killmode && "control-group" != tolower(killmode->last_setting())) {
		std::fprintf(stderr, "%s: FATAL: %s: %s: %s\n", prog, service_filename.c_str(), killmode->last_setting().c_str(), "Unsupported service stop mechanism.");
		throw EXIT_FAILURE;
	}
	const bool killsignal_is_term(is_string(killsignal, "sigterm"));
	if (killsignal && !killsignal_is_term)
		killsignal->used = false;
	if (runtimedirectory) {
		for (value::settings::const_iterator i(runtimedirectory->all_settings().begin()); runtimedirectory->all_settings().end() != i; ++i) {
			const std::string dir(names.substitute(*i));
			// Yes, this is more draconian than systemd.
			if (dir.length() < 1 || '.' == dir[0]) {
				std::fprintf(stderr, "%s: FATAL: %s: %s: %s\n", prog, service_filename.c_str(), dir.c_str(), "Hidden runtime directories are not permitted.");
				throw EXIT_FAILURE;
			}
			// This is something that we are going to pass to the "rm" command run as the superuser, remember.
			const std::string::size_type slashpos(dir.find('/'));
			if (std::string::npos != slashpos) {
				std::fprintf(stderr, "%s: FATAL: %s: %s: %s\n", prog, service_filename.c_str(), dir.c_str(), "Slash is not permitted in runtime directory names.");
				throw EXIT_FAILURE;
			}
			// This is something that we are going to pass to the "foreground" command, too.
			const std::string::size_type semi(dir.find(';'));
			if (std::string::npos != semi) {
				std::fprintf(stderr, "%s: FATAL: %s: %s: %s\n", prog, service_filename.c_str(), dir.c_str(), "Semicolon is not permitted in runtime directory names.");
				throw EXIT_FAILURE;
			}
		}
	}

	// Make the directories.

	const std::string service_dirname(names.query_bundle_dirname() + "/service");

	mkdirat(AT_FDCWD, names.query_bundle_dirname().c_str(), 0755);
	const FileDescriptorOwner bundle_dir_fd(open_dir_at(AT_FDCWD, (names.query_bundle_dirname() + slash).c_str()));
	if (0 > bundle_dir_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, names.query_bundle_dirname().c_str(), std::strerror(error));
		throw EXIT_FAILURE;
	}
	make_service(bundle_dir_fd.get());
	make_orderings_and_relations(bundle_dir_fd.get());
	const FileDescriptorOwner service_dir_fd(open_service_dir(bundle_dir_fd.get()));
	if (0 > service_dir_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, names.query_bundle_dirname().c_str(), "service", std::strerror(error));
		throw EXIT_FAILURE;
	}

	// Construct various common command strings.

	std::string jail, enable_control_group_controllers, create_control_group, move_to_control_group, control_group_knobs;
#if defined(__LINUX__) || defined(__linux__)
	bool cpu_accounting(is_bool_true(cpuaccounting, false));
	bool memory_accounting(is_bool_true(memoryaccounting, false));
	bool tasks_accounting(is_bool_true(tasksaccounting, false));
	bool io_accounting(is_bool_true(ioaccounting, false));
	bool rdma_accounting(is_bool_true(rdmaaccounting, false));
	if (cpuweight) {
		control_group_knobs += "foreground set-control-group-knob cpu.weight " + quote(names.substitute(cpuweight->last_setting())) + " ;\n";
		cpu_accounting = true;
	}
	if (cpuquota) {
		control_group_knobs += "foreground set-control-group-knob cpu.max " + quote(names.substitute(cpuquota->last_setting())) + " ;\n";
		cpu_accounting = true;
	}
	if (tasksmax) {
		control_group_knobs += "foreground set-control-group-knob --percent-of /proc/sys/kernel/threads-max --infinity-is-max pids.max " + quote(names.substitute(tasksmax->last_setting())) + " ;\n";
		tasks_accounting = true;
	}
	if (memorylow) {
		/// FIXME: memmax?
		control_group_knobs += "foreground set-control-group-knob --infinity-is-max --multiplier-suffixes memory.low " + quote(names.substitute(memorylow->last_setting())) + " ;\n";
		memory_accounting = true;
	}
	if (memoryhigh) {
		/// FIXME: memmax?
		control_group_knobs += "foreground set-control-group-knob --infinity-is-max --multiplier-suffixes memory.high " + quote(names.substitute(memoryhigh->last_setting())) + " ;\n";
		memory_accounting = true;
	}
	if (memorymax) {
		/// FIXME: memmax?
		control_group_knobs += "foreground set-control-group-knob --infinity-is-max --multiplier-suffixes memory.max " + quote(names.substitute(memorymax->last_setting())) + " ;\n";
		memory_accounting = true;
	}
	if (memoryswapmax) {
		/// FIXME: memmax?
		control_group_knobs += "foreground set-control-group-knob --infinity-is-max --multiplier-suffixes memory.swap.max " + quote(names.substitute(memoryswapmax->last_setting())) + " ;\n";
		memory_accounting = true;
	}
	if (ioweight) {
		control_group_knobs += "foreground set-control-group-knob io.weight " + quote(names.substitute(ioweight->last_setting())) + " ;\n";
		io_accounting = true;
	}
	if (iodeviceweight) {
		control_group_knobs += "foreground set-control-group-knob --device-name-key io.weight " + quote(names.substitute(iodeviceweight->last_setting())) + " ;\n";
		io_accounting = true;
	}
	if (ioreadbandwidthmax) {
		control_group_knobs += "foreground set-control-group-knob --device-name-key --nested-key rbs --infinity-is-max --multiplier-suffixes io.weight " + quote(names.substitute(ioreadbandwidthmax->last_setting())) + " ;\n";
		io_accounting = true;
	}
	if (ioreadiopsmax) {
		control_group_knobs += "foreground set-control-group-knob --device-name-key --nested-key rios --infinity-is-max --multiplier-suffixes io.weight " + quote(names.substitute(ioreadiopsmax->last_setting())) + " ;\n";
		io_accounting = true;
	}
	if (iowritebandwidthmax) {
		control_group_knobs += "foreground set-control-group-knob --device-name-key --nested-key wbs --infinity-is-max --multiplier-suffixes io.weight " + quote(names.substitute(iowritebandwidthmax->last_setting())) + " ;\n";
		io_accounting = true;
	}
	if (iowriteiopsmax) {
		control_group_knobs += "foreground set-control-group-knob --device-name-key --nested-key wios --infinity-is-max --multiplier-suffixes io.weight " + quote(names.substitute(iowriteiopsmax->last_setting())) + " ;\n";
		io_accounting = true;
	}
	if (rdmahcahandlesmax) {
		control_group_knobs += "foreground set-control-group-knob --device-name-key --nested-key hca_handle --infinity-is-max --multiplier-suffixes rdma.max " + quote(names.substitute(rdmahcahandlesmax->last_setting())) + " ;\n";
		rdma_accounting = true;
	}
	if (rdmahcaobjectsmax) {
		control_group_knobs += "foreground set-control-group-knob --device-name-key --nested-key hca_object --infinity-is-max --multiplier-suffixes rdma.max " + quote(names.substitute(rdmahcaobjectsmax->last_setting())) + " ;\n";
		rdma_accounting = true;
	}
	const unsigned control_group_levels(1U + !!slice + !!is_instance);
	if (cpu_accounting)
		enable_control_group_controllers += set_controller_command(control_group_levels, "cpu", cpu_accounting);
	if (memory_accounting)
		enable_control_group_controllers += set_controller_command(control_group_levels, "memory", memory_accounting);
	if (io_accounting)
		enable_control_group_controllers += set_controller_command(control_group_levels, "io", io_accounting);
	if (rdma_accounting)
		enable_control_group_controllers += set_controller_command(control_group_levels, "rdma", rdma_accounting);
	if (tasks_accounting)
		enable_control_group_controllers += set_controller_command(control_group_levels, "pids", tasks_accounting);
	std::string control_group_name("..");
	if (slice) {
		control_group_name += "/../" + names.substitute(slice->last_setting());
		create_control_group += "foreground create-control-group " + quote(control_group_name) + " ;\n";
	}
	if (is_instance) {
		control_group_name += slash + (names.query_escaped_prefix() + "@.") + (is_target ? "target" : "service");
		create_control_group += "foreground create-control-group " + quote(control_group_name) + " ;\n";
	}
	control_group_name += slash + (names.query_bundle_basename() + ".") + (is_target ? "target" : "service");
	move_to_control_group += "move-to-control-group " + quote(control_group_name) + "\n";
	if (is_bool_true(delegate, false))
		control_group_knobs += "foreground delegate-control-group-to " + quote(names.query_user_name()) + " ;\n";
#else
	static_cast<void>(is_instance);	// Silence a compiler warning.
	if (jailid) jail += "jexec " + quote(names.substitute(jailid->last_setting())) + "\n";
#endif
	std::string priority;
#if defined(__LINUX__) || defined(__linux__)
	if (ioschedulingclass || ioschedulingpriority) {
		priority += "ionice";
		if (ioschedulingclass)
			priority += " --class " + quote(names.substitute(ioschedulingclass->last_setting()));
		if (ioschedulingpriority)
			priority += " --classdata " + quote(names.substitute(ioschedulingpriority->last_setting()));
		priority += "\n";
	}
	if (cpuschedulingpolicy || cpuschedulingpriority || cpuschedulingresetonfork) {
		priority += "chrt";
		if (is_bool_true(cpuschedulingresetonfork, false))
			priority += " --reset-on-fork";
		if (cpuschedulingpolicy)
			priority += " --" + quote(names.substitute(cpuschedulingpolicy->last_setting()));
		if (cpuschedulingpriority)
			priority += " " + quote(names.substitute(cpuschedulingpriority->last_setting()));
		else
			priority += " 0";
		priority += "\n";
	}
	if (numalocalalloc || numainterleave || numamembind || numacpunodebind || numaphyscpubind || numapreferred) {
		priority += "numactl";
		if (is_bool_true(numalocalalloc, false))
			priority += " --localalloc";
		if (numainterleave)
			priority += " --interleave " + quote(names.substitute(numainterleave->last_setting()));
		if (numamembind)
			priority += " --membind " + quote(names.substitute(numamembind->last_setting()));
		if (numacpunodebind)
			priority += " --cpunodebind " + quote(names.substitute(numacpunodebind->last_setting()));
		if (numaphyscpubind)
			priority += " --physcpubind " + quote(names.substitute(numaphyscpubind->last_setting()));
		if (numapreferred)
			priority += " --preferred " + quote(names.substitute(numapreferred->last_setting()));
		priority += "\n";
	}
#else
	if (cpuschedulingpolicy) {
		const std::string policy(tolower(names.substitute(cpuschedulingpolicy->last_setting())));
		if ("batch" == policy || "other" == policy) {
			if (cpuschedulingpriority)
				cpuschedulingpriority->used = false;
		} else
		if ("fifo" == policy || "rr" == policy) {
			priority += "rtprio";
			if (cpuschedulingpriority)
				priority += " " + quote(names.substitute(cpuschedulingpriority->last_setting()));
			else
				priority += " 0";
			priority += "\n";
		} else
		if ("idle" == policy) {
			priority += "idprio";
			if (cpuschedulingpriority)
				priority += " " + quote(names.substitute(cpuschedulingpriority->last_setting()));
			else
				priority += " 0";
			priority += "\n";
		} else
		{
			if (cpuschedulingpriority)
				cpuschedulingpriority->used = false;
			cpuschedulingpolicy->used = false;
		}
	}
	if (numamembind || numacpunodebind) {
		priority += "numactl";
		/// \todo TODO: And --mempolicy
		if (numamembind)
			priority += " --memdomain " + quote(names.substitute(numamembind->last_setting()));
		if (numacpunodebind)
			priority += " --cpudomain " + quote(names.substitute(numacpunodebind->last_setting()));
		priority += "\n";
	}
#endif
	if (cpuaffinity) {
#if defined(__LINUX__) || defined(__linux__)
		priority += "taskset --cpu ";
#else
		priority += "cpuset -C -l ";
#endif
		for (value::settings::const_iterator b(cpuaffinity->all_settings().begin()), i(b), e(cpuaffinity->all_settings().end()); e != i; ++i) {
			if (i != b) priority += ",";
			priority += quote(names.substitute(*i));
		}
		priority += "\n";
	}
	std::string jvm;
	if (jvmversions || jvmoperatingsystems || jvmmanufacturers) {
		jvm += "find-matching-jvm";
		if (jvmversions) {
			const std::list<std::string> list(split_list(names.substitute(jvmversions->last_setting())));
			for (std::list<std::string>::const_iterator i(list.begin()); list.end() != i; ++i)
				jvm += " --version " + quote(*i);
		}
		if (jvmoperatingsystems) {
			const std::list<std::string> list(split_list(names.substitute(jvmoperatingsystems->last_setting())));
			for (std::list<std::string>::const_iterator i(list.begin()); list.end() != i; ++i)
				jvm += " --operating-system " + quote(*i);
		}
		if (jvmmanufacturers) {
			const std::list<std::string> list(split_list(names.substitute(jvmmanufacturers->last_setting())));
			for (std::list<std::string>::const_iterator i(list.begin()); list.end() != i; ++i)
				jvm += " --manufacturer " + quote(*i);
		}
		jvm += "\n";
	}
	if (is_bool_true(jvmdefault, false))
		jvm += "find-default-jvm\n";
	if (oomscoreadjust)
		// The -- is necessary because the adjustment could be a negative number, starting with a dash,
		priority += "oom-kill-protect -- " + quote(names.substitute(oomscoreadjust->last_setting())) + "\n";
	std::string chroot;
	if (rootdirectory) chroot += "chroot " + quote(names.substitute(rootdirectory->last_setting())) + "\n";
#if defined(__LINUX__) || defined(__linux__)
	const bool is_private_tmp(is_bool_true(privatetmp, false));
	const bool is_private_network(is_bool_true(privatenetwork, false));
	const bool is_private_devices(is_bool_true(privatedevices, false));
	const bool is_private_homes(is_bool_true(protecthome, false));
	const bool is_private_mount(is_private_tmp||is_private_devices||is_private_homes||readonlypaths||readwritedirectories||inaccessiblepaths);
	const bool is_private(is_private_mount||is_private_network);
	const bool is_protect_os(is_bool_true(protectsystem, false));
	const bool is_protect_etc(is_string(protectsystem, "full") || is_bool_true(protectsystem, false));
	const bool is_protect_homes(is_string(protecthome, "read-only"));
	const bool is_protect_sysctl(is_bool_true(protectkerneltunables, false));
	const bool is_protect_cgroups(is_bool_true(protectcontrolgroups, false));
	const bool is_protect_mount(is_protect_os||is_protect_etc||is_protect_homes||is_protect_sysctl||is_protect_cgroups);
	if (mountflags||is_private||is_protect_mount) {
		chroot += "unshare";
		if (mountflags||is_private_mount||is_protect_mount) chroot += " --mount";
		if (is_private_network) chroot += " --network";
		chroot += "\n";
		if (is_private_mount||is_protect_mount) {
			chroot += "set-mount-object --recursive slave /\n";
			if (is_private_mount) {
				chroot += "make-private-fs";
				if (is_private_tmp) chroot += " --temp";
				if (is_private_devices) chroot += " --devices";
				if (is_private_homes) chroot += " --homes";
				if (inaccessiblepaths) {
					for (value::settings::const_iterator i(inaccessiblepaths->all_settings().begin()); inaccessiblepaths->all_settings().end() != i; ++i) {
						const std::string dir(names.substitute(*i));
						chroot += " --hide " + quote(dir);
					}
				}
				chroot += "\n";
			}
			if (is_protect_mount) {
				chroot += "make-read-only-fs";
				if (is_protect_os) chroot += " --os";
				if (is_protect_etc) chroot += " --etc";
				if (is_protect_homes) chroot += " --homes";
				if (is_protect_sysctl) chroot += " --sysctl";
				if (is_protect_cgroups) chroot += " --cgroups";
				if (readonlypaths) {
					for (value::settings::const_iterator i(readonlypaths->all_settings().begin()); readonlypaths->all_settings().end() != i; ++i) {
						const std::string dir(names.substitute(*i));
						chroot += " --include " + quote(dir);
					}
				}
				if (readwritedirectories) {
					for (value::settings::const_iterator i(readwritedirectories->all_settings().begin()); readwritedirectories->all_settings().end() != i; ++i) {
						const std::string dir(names.substitute(*i));
						chroot += " --except " + quote(dir);
					}
				}
				chroot += "\n";
			}
		}
		/// \bug FIXME is_private_network needs to set up a lo0 device.
		if (mountflags) 
			chroot += "set-mount-object --recursive " + quote(mountflags->last_setting()) + " /\n";
		else
		if (is_private_mount||is_protect_mount)
			chroot += "set-mount-object --recursive shared /\n";
	}
#endif
	const bool chrootall(!is_bool_true(rootdirectorystartonly, false));
	std::string setsid;
	if (is_bool_true(sessionleader, false)) setsid += "setsid\n";
	if (is_bool_true(processgroupleader, false)) setsid += "setpgrp\n";
	if (localreaper) setsid += "local-reaper " + quote(names.substitute(localreaper->last_setting())) + "\n";
	std::string envuidgid, setuidgid;
	if (user) {
		if (rootdirectory) {
			envuidgid += "envuidgid";
			if (group)
				envuidgid += " --primary-group " + quote(names.substitute(group->last_setting()));
			envuidgid += " -- " + quote(names.query_user_name()) + "\n";
			setuidgid += "setuidgid-fromenv\n";
		} else {
			setuidgid += "setuidgid";
			if (group)
				setuidgid += " --primary-group " + quote(names.substitute(group->last_setting()));
			if (is_bool_true(systemdusergroups, systemd_quirks))
				setuidgid += " --supplementary";
			setuidgid += " -- " + quote(names.query_user_name()) + "\n";
		}
	} else {
		if (group)
			setuidgid += "setgid -- " + quote(names.substitute(group->last_setting())) + "\n";
	}
	if (is_bool_true(systemduserenvironment, systemd_quirks))
		// This replicates systemd useless features.
		setuidgid += "userenv\n";
	const bool setuidgidall(!is_bool_true(permissionsstartonly, false));
	// systemd always runs services in / by default; daemontools runs them in the service directory.
	std::string chdir;
	if (workingdirectory)
		chdir += "chdir " + quote(names.substitute(workingdirectory->last_setting())) + "\n";
	else if (rootdirectory || is_bool_true(systemdworkingdirectory, systemd_quirks && !is_socket_activated))
		chdir += "chdir /\n";
	std::string createrundir, removerundir;
	if (runtimedirectory) {
		std::string dirs, dirs_slash;
		for (value::settings::const_iterator i(runtimedirectory->all_settings().begin()); runtimedirectory->all_settings().end() != i; ++i) {
			const std::string dir(names.query_runtime_dir_by_name() + slash + names.substitute(*i));
			if (!dirs.empty()) dirs += ' ';
			if (!dirs_slash.empty()) dirs_slash += ' ';
			dirs += quote(dir);
			dirs_slash += quote(dir) + slash;
		}
		createrundir += "foreground install -d";
		if (runtimedirectorymode) createrundir += " -m " + quote(names.substitute(runtimedirectorymode->last_setting()));
		if (user || runtimedirectoryowner) {
			createrundir += " -o ";
			if (runtimedirectoryowner)
				createrundir += quote(names.substitute(runtimedirectoryowner->last_setting()));
			else
				createrundir += quote(names.substitute(user->last_setting()));
		}
		if (group)
			createrundir += " -g " + quote(names.substitute(group->last_setting()));
		createrundir += " -- " + dirs + " ;\n";
		// The trailing slash is a(nother) safety measure, to help ensure that this is a directory that we are removing, uncondintally, as the superuser.
		removerundir += "foreground rm -r -f -- " + dirs_slash + " ;\n";
	}
	std::string hardlimit, softlimit;
	if (limitnofile||limitcpu||limitcore||limitnproc||limitfsize||limitas||limitrss||limitdata||limitstack||limitmemory||limitmemlock) {
		std::string s, h;
		if (limitnofile) split_limit(limitnofile->last_setting(),"o",s,h);
		if (limitcpu) split_limit(limitcpu->last_setting(),"t",s,h);
		if (limitcore) split_limit(limitcore->last_setting(),"c",s,h);
		if (limitnproc) split_limit(limitnproc->last_setting(),"p",s,h);
		if (limitfsize) split_limit(limitfsize->last_setting(),"f",s,h);
		if (limitas) split_limit(limitas->last_setting(),"a",s,h);
		if (limitrss) split_limit(limitrss->last_setting(),"r",s,h);
		if (limitdata) split_limit(limitdata->last_setting(),"d",s,h);
		if (limitstack) split_limit(limitstack->last_setting(),"s",s,h);
		if (limitmemory) split_limit(limitmemory->last_setting(),"m",s,h);
		if (limitmemlock) split_limit(limitmemlock->last_setting(),"l",s,h);
		if (!s.empty()) softlimit += "softlimit" + s + "\n";
		if (!h.empty()) hardlimit += "hardlimit" + h + "\n";
	}
	if (0
#if defined(RLIMIT_NICE)
	||  limitnice
#endif
#if defined(RLIMIT_SIGPENDING)
	||  limitsigpending
#endif
#if defined(RLIMIT_PIPE)
	||  limitpipe
#endif
#if defined(RLIMIT_MSGQUEUE)
	||  limitmsgqueue
#endif
#if defined(RLIMIT_LOCKS)
	||  limitlocks
#endif
	) {
		std::string s, h;
#if defined(RLIMIT_NICE)
		if (limitnice) split_limit(limitnice->last_setting(),"e",s,h);
#endif
#if defined(RLIMIT_SIGPENDING)
		if (limitsigpending) split_limit(limitsigpending->last_setting(),"i",s,h);
#endif
#if defined(RLIMIT_PIPE)
		if (limitpipe) split_limit(limitpipe->last_setting(),"p",s,h);
#endif
#if defined(RLIMIT_MSGQUEUE)
		if (limitmsgqueue) split_limit(limitmsgqueue->last_setting(),"q",s,h);
#endif
#if defined(RLIMIT_LOCKS)
		if (limitlocks) split_limit(limitlocks->last_setting(),"x",s,h);
#endif
		if (!s.empty()) softlimit += "ulimit -S" + s + "\n";
		if (!h.empty()) hardlimit += "ulimit -H" + h + "\n";
	}
	std::string env;
	if (is_bool_true(machineenvironment, false)) env += "machineenv\n";
	if (environmentfile) {
		for (value::settings::const_iterator i(environmentfile->all_settings().begin()); environmentfile->all_settings().end() != i; ++i) {
			std::string val;
			const bool minus(strip_leading_minus(*i, val));
			env += "read-conf " + ((minus ? "--oknofile " : "") + quote(names.substitute(val))) + "\n";
		}
	}
	if (environmentdirectory) {
		const std::string opt(is_bool_true(fullenvironmentdirectory, false) ? "--full " : "");
		for (value::settings::const_iterator i(environmentdirectory->all_settings().begin()); environmentdirectory->all_settings().end() != i; ++i) {
			const std::string & val(*i);
			env += "envdir " + opt + quote(names.substitute(val)) + "\n";
		}
	}
	if (environmentuser) env += "envuidgid " + quote(names.substitute(environmentuser->last_setting())) + "\n";
	if (environment) {
		for (value::settings::const_iterator i(environment->all_settings().begin()); environment->all_settings().end() != i; ++i) {
			const std::string & datum(*i);
			const std::list<std::string> list(names.substitute(split_list(datum)));
			for (std::list<std::string>::const_iterator j(list.begin()); list.end() != j; ++j) {
				const std::string & s(*j);
				const std::string::size_type eq(s.find('='));
				const std::string var(s.substr(0, eq));
				const std::string val(eq == std::string::npos ? std::string() : s.substr(eq + 1, std::string::npos));
				env += "setenv " + quote(var) + " " + quote(val) + "\n";
			}
		}
	}
	if (unsetenvironment) {
		for (value::settings::const_iterator i(unsetenvironment->all_settings().begin()); unsetenvironment->all_settings().end() != i; ++i) {
			const std::string & datum(*i);
			const std::list<std::string> list(names.substitute(split_list(datum)));
			for (std::list<std::string>::const_iterator j(list.begin()); list.end() != j; ++j) {
				const std::string & var(*j);
				env += "unsetenv " + quote(var) + "\n";
			}
		}
	}
	if (environmentappendpath) {
		for (value::settings::const_iterator i(environmentappendpath->all_settings().begin()); environmentappendpath->all_settings().end() != i; ++i) {
			const std::string & datum(*i);
			const std::list<std::string> list(names.substitute(split_list(datum)));
			for (std::list<std::string>::const_iterator j(list.begin()); list.end() != j; ++j) {
				const std::string & s(*j);
				const std::string::size_type eq(s.find('='));
				const std::string var(s.substr(0, eq));
				const std::string val(eq == std::string::npos ? std::string() : s.substr(eq + 1, std::string::npos));
				env += "appendpath " + quote(var) + " " + quote(val) + "\n";
			}
		}
	}
	std::string um;
	if (umask) um += "umask " + quote(umask->last_setting()) + "\n";
	const bool is_use_hangup(is_bool_true(sendsighup, false));
	const bool is_use_kill(is_bool_true(sendsigkill, true));
	std::string redirect, login_prompt, greeting_message, socket_redirect;
	const bool stdin_file(standardinput && "file:" == tolower(standardinput->last_setting()).substr(0,5));
	const bool stdin_socket(is_string(standardinput, "socket"));
	const bool stdin_tty(is_string(standardinput, "tty", "tty-force"));
	const bool stdout_file(standardoutput && "file:" == tolower(standardoutput->last_setting()).substr(0,5));
	const bool stdout_socket(is_string(standardoutput, "socket"));
	const bool stdout_tty(is_string(standardoutput, "tty", "tty-force"));
	const bool stdout_inherit(is_string(standardoutput, "inherit"));
	const bool stderr_file(standarderror && "file:" == tolower(standarderror->last_setting()).substr(0,5));
	const bool stderr_socket(is_string(standarderror, "socket"));
	const bool stderr_tty(is_string(standarderror, "tty", "tty-force"));
	const bool stderr_inherit(is_string(standarderror, "inherit"));
	const bool stderr_log(is_string(standarderror, "log"));
	// We "un-use" anything that isn't "inherit"/"tty"/"socket".
	if (standardinput && (!stdin_file && !stdin_socket && !stdin_tty)) standardinput->used = false;
	if (standardoutput && (!stdout_file && !stdout_inherit && !stdout_socket && !stdout_tty)) standardoutput->used = false;
	if (standarderror && (!stderr_file && !stderr_inherit && !stderr_socket && !stderr_tty && !stderr_log)) standarderror->used = false;
	if (is_socket_activated) {
		if (is_socket_accept) {
			// There is no non-UCSPI mode for per-connection services.
			// In ideal mode, input/output are the socket and error is the log.
			// In quirks mode, we just force the same behaviour as ideal mode.
			if (!systemd_quirks) {
				if (stdin_socket) 
					std::fprintf(stderr, "%s: INFO: %s: Superfluous setting: [%s] %s\n", prog, service_filename.c_str(), "Service", "StandardInput");
				else if (standardinput) 
					std::fprintf(stderr, "%s: WARNING: %s: Forcing setting: [%s] %s = %s\n", prog, service_filename.c_str(), "Service", "StandardInput", "socket");

				if (stdout_inherit || stdout_socket)
					std::fprintf(stderr, "%s: INFO: %s: Superfluous setting: [%s] %s\n", prog, service_filename.c_str(), "Service", "StandardOutput");
				else if (standardoutput) 
					std::fprintf(stderr, "%s: WARNING: %s: Forcing setting: [%s] %s = %s\n", prog, service_filename.c_str(), "Service", "StandardOutput", "socket");

				if (stderr_log)
					std::fprintf(stderr, "%s: INFO: %s: Superfluous setting: [%s] %s\n", prog, service_filename.c_str(), "Service", "StandardError");
				else if (stderr_inherit)
					socket_redirect += "fdmove -c 2 1\n";
				else if (standarderror)
					std::fprintf(stderr, "%s: WARNING: %s: Forcing setting: [%s] %s = %s\n", prog, service_filename.c_str(), "Service", "StandardError", "log");
			} else {
				if (!stdin_socket) 
					std::fprintf(stderr, "%s: WARNING: %s: Forcing setting: [%s] %s = %s\n", prog, service_filename.c_str(), "Service", "StandardInput", "socket");
				if (!stdout_inherit && !stdout_socket) 
					std::fprintf(stderr, "%s: WARNING: %s: Forcing setting: [%s] %s = %s\n", prog, service_filename.c_str(), "Service", "StandardOutput", "socket");
				if (standarderror && !stderr_log)
					std::fprintf(stderr, "%s: WARNING: %s: Forcing setting: [%s] %s = %s\n", prog, service_filename.c_str(), "Service", "StandardError", "log");
			}
		} else {
			// Listening-socket services are complicated.
			// Standard I/O descriptors redirected to the socket are done with redirections after the listen.
			if (stdin_socket) socket_redirect += "fdmove -c 0 3\n";
			if (stdout_socket) socket_redirect += "fdmove -c 1 3\n";
			if (stderr_socket) socket_redirect += "fdmove -c 2 3\n";
			if (stdin_tty) {
				// Listening-socket services can be attached to terminal devices as well.
				// In ideal mode, input/output/error are the terminal device.
				// In quirks mode, we just force the same behaviour as ideal mode.
				if (!systemd_quirks) {
					if (stdout_inherit || stdout_tty)
						std::fprintf(stderr, "%s: INFO: %s: Superfluous setting: [%s] %s\n", prog, service_filename.c_str(), "Service", "StandardOutput");
					else if (standardoutput) 
						std::fprintf(stderr, "%s: WARNING: %s: Forcing setting: [%s] %s = %s\n", prog, service_filename.c_str(), "Service", "StandardOutput", "tty");

					if (stderr_inherit || stderr_tty)
						std::fprintf(stderr, "%s: INFO: %s: Superfluous setting: [%s] %s\n", prog, service_filename.c_str(), "Service", "StandardError");
					else if (stderr_log)
						;	// Dealt with below.
					else if (standarderror)
						std::fprintf(stderr, "%s: WARNING: %s: Forcing setting: [%s] %s = %s\n", prog, service_filename.c_str(), "Service", "StandardError", "tty");
				} else {
					if (!stdout_inherit && !stdout_tty) 
						std::fprintf(stderr, "%s: WARNING: %s: Forcing setting: [%s] %s = %s\n", prog, service_filename.c_str(), "Service", "StandardOutput", "tty");
					if (standarderror && !stderr_inherit && !stderr_tty && !stderr_log)
						std::fprintf(stderr, "%s: WARNING: %s: Forcing setting: [%s] %s = %s\n", prog, service_filename.c_str(), "Service", "StandardError", "tty");
				}
			} else {
				// There are no half measures ("Don't adopt a controlling terminal and only redirect standard output.") available.
				// In ideal mode, input comes from another service, output goes to a logging service, and error always defaults to the log even if output is redirected.
				// In quirks mode, error always defaults to output and inherits its redirection.
				// Redirection is done after the listen, because inherit means possibly inherit standard input that has been redirected to the socket.
				if (stdout_tty || stderr_tty)
					std::fprintf(stderr, "%s: WARNING: %s: Redirection ignored for non-controlling-terminal service.\n", prog, service_filename.c_str());
				if (stdout_inherit) {
					socket_redirect += "fdmove -c 1 0\n";
					if ((!standarderror && systemd_quirks) || stderr_inherit) socket_redirect += "fdmove -c 2 1\n";
				}
				if (!systemd_quirks) {
					if (stderr_log)
						std::fprintf(stderr, "%s: INFO: %s: Superfluous setting: [%s] %s\n", prog, service_filename.c_str(), "Service", "StandardError");
				}
			}
		}
	} else {
		if (stdin_socket || stdout_socket || stderr_socket)
			std::fprintf(stderr, "%s: WARNING: %s: Redirection ignored for non-socket service.\n", prog, service_filename.c_str());
		if (stdin_tty) {
			// Non-socket services can take controlling terminals.
			// In ideal mode, input/output/error default to the terminal device.
			// In quirks mode, we just force the same behaviour as ideal mode.
			if (!systemd_quirks) {
				if (stdout_inherit || stdout_tty)
					std::fprintf(stderr, "%s: INFO: %s: Superfluous setting: [%s] %s\n", prog, service_filename.c_str(), "Service", "StandardOutput");
				else if (standardoutput) 
					std::fprintf(stderr, "%s: WARNING: %s: Forcing setting: [%s] %s = %s\n", prog, service_filename.c_str(), "Service", "StandardOutput", "tty");

				if (stderr_inherit || stderr_tty)
					std::fprintf(stderr, "%s: INFO: %s: Superfluous setting: [%s] %s\n", prog, service_filename.c_str(), "Service", "StandardError");
				else if (stderr_log)
					;	// Dealt with below.
				else if (standarderror)
					std::fprintf(stderr, "%s: WARNING: %s: Forcing setting: [%s] %s = %s\n", prog, service_filename.c_str(), "Service", "StandardError", "tty");
			} else {
				if (!stdout_inherit && !stdout_tty) 
					std::fprintf(stderr, "%s: WARNING: %s: Forcing setting: [%s] %s = %s\n", prog, service_filename.c_str(), "Service", "StandardOutput", "tty");
				if (standarderror && !stderr_inherit && !stderr_tty && !stderr_log)
					std::fprintf(stderr, "%s: WARNING: %s: Forcing setting: [%s] %s = %s\n", prog, service_filename.c_str(), "Service", "StandardError", "tty");
			}
		} else {
			// There are no half measures ("Don't adopt a controlling terminal and only redirect standard output.") available.
			// In ideal mode, input comes from another service, output goes to a logging service, and error always defaults to the log even if output is redirected.
			// In quirks mode, error always defaults to output and inherits its redirection.
			if (stdout_tty || stderr_tty)
				std::fprintf(stderr, "%s: WARNING: %s: Redirection ignored for non-controlling-terminal service.\n", prog, service_filename.c_str());
			if (stdout_inherit) {
				redirect += "fdmove -c 1 0\n";
				if ((!standarderror && systemd_quirks) || stderr_inherit) redirect += "fdmove -c 2 1\n";
			}
			if (!systemd_quirks) {
				if (stderr_log)
					std::fprintf(stderr, "%s: INFO: %s: Superfluous setting: [%s] %s\n", prog, service_filename.c_str(), "Service", "StandardError");
			}
		}
	}
	if ((ttypath || stdin_tty) && !is_bool_true(ttyfromenv, false)) {
		const std::string tty(ttypath ? names.substitute(ttypath->last_setting()) : "/dev/console");
		redirect += "vc-get-tty " + quote(tty) + "\n";
	}
	if (stdin_tty) {
		if (stderr_log) redirect += "fdmove -c 4 2\n";
		redirect += "open-controlling-tty";
		if (is_bool_true(ttyvhangup, false)) 
#if defined(__LINUX__) || defined(__linux__)
			redirect += " --vhangup";
#else
			redirect += " --revoke";
#endif
		redirect += "\n";
		if (stderr_log) redirect += "fdmove 2 4\n";
		if (is_bool_true(ttyreset, false)) login_prompt += "vc-reset-tty\n";
		if (is_bool_true(ttyprompt, false)) login_prompt += "login-prompt\n";
#if defined(__LINUX__) || defined(__linux__)
		if (utmpidentifier)
			login_prompt += "login-process --id " + quote(names.substitute(utmpidentifier->last_setting())) + "\n";
#endif
	} else {
		if (ttyvhangup) ttyvhangup->used = false;
		if (ttyreset) ttyreset->used = false;
		if (ttyprompt) ttyprompt->used = false;
#if defined(__LINUX__) || defined(__linux__)
		if (utmpidentifier) utmpidentifier->used = false;
#endif
	}
	if (stdin_file) redirect += "fdredir --read 0 " + quote(names.substitute(standardinput->last_setting().substr(5,std::string::npos))) + "\n";
	if (stdout_file) redirect += "fdredir --write 1 " + quote(names.substitute(standardoutput->last_setting().substr(5,std::string::npos))) + "\n";
	if (stderr_file) redirect += "fdredir --write 2 " + quote(names.substitute(standarderror->last_setting().substr(5,std::string::npos))) + "\n";
	if (bannerfile)
		greeting_message += "login-banner " + quote(names.substitute(bannerfile->last_setting())) + "\n";
	if (bannerline)
		for (value::settings::const_iterator i(bannerline->all_settings().begin()); bannerline->all_settings().end() != i; ++i) {
			const std::string & val(*i);
			greeting_message += "line-banner " + quote(names.substitute(val)) + "\n";
		}

	// Open the service script files.

	std::ofstream start, stop, restart_script, run, remain;
	open(prog, start, service_dirname + "/start");
	open(prog, stop, service_dirname + "/stop");
	open(prog, restart_script, service_dirname + "/restart");
	open(prog, run, service_dirname + "/run");

	// Write the script files.

	std::stringstream perilogue_setup_environment;
	perilogue_setup_environment << jail;
	perilogue_setup_environment << create_control_group;
	perilogue_setup_environment << move_to_control_group;
	perilogue_setup_environment << enable_control_group_controllers;
	perilogue_setup_environment << priority;
	if (setuidgidall) perilogue_setup_environment << envuidgid;
	perilogue_setup_environment << env;
	perilogue_setup_environment << hardlimit;
	perilogue_setup_environment << softlimit;
	perilogue_setup_environment << um;
	if (chrootall) perilogue_setup_environment << chroot;
	perilogue_setup_environment << chdir;
	perilogue_setup_environment << jvm;
	perilogue_setup_environment << redirect;

	std::stringstream perilogue_drop_privileges;
	if (setuidgidall) perilogue_drop_privileges << setuidgid;

	std::stringstream setup_environment;
	setup_environment << jail;
	setup_environment << move_to_control_group;
	setup_environment << priority;
	setup_environment << envuidgid;
	setup_environment << env;
	setup_environment << setsid;
	setup_environment << hardlimit;
	setup_environment << softlimit;
	setup_environment << um;
	setup_environment << chroot;
	setup_environment << chdir;
	setup_environment << jvm;
	setup_environment << redirect;
	if (merge_run_into_start && runtimedirectory)
		setup_environment << createrundir;

	std::stringstream drop_privileges;
	drop_privileges << setuidgid;

	std::stringstream execute_command;
	execute_command << login_prompt;
	execute_command << greeting_message;
	if (merge_run_into_start) {
		if (execstartpre) {
			for (value::settings::const_iterator i(execstartpre->all_settings().begin()); execstartpre->all_settings().end() != i; ++i ) {
				execute_command << "foreground "; 
				execute_command << names.substitute(shell_expand(strip_leading_minus(*i)));
				execute_command << " ;\n"; 
			}
		}
	}
	if (execstart) {
		for (value::settings::const_iterator i(execstart->all_settings().begin()); execstart->all_settings().end() != i; ) {
			std::list<std::string>::const_iterator j(i++);
			if (execstart->all_settings().begin() != j) execute_command << " ;\n"; 
			if (execstart->all_settings().end() != i) execute_command << "foreground "; 
			const std::string & val(*j);
			execute_command << names.substitute(shell_expand(strip_leading_minus(val)));
		}
	} else
		execute_command << (is_remain || is_oneshot ? "true" : "pause") << "\n";

	std::ostream & run_or_start(merge_run_into_start ? start : run);
	start << "#!/bin/nosh\n";
	if (generation_comment)
		start << multi_line_comment("Start file generated from " + (is_socket_activated ? socket_filename : service_filename));
	run << "#!/bin/nosh\n";
	if (generation_comment)
		run << multi_line_comment("Run file generated from " + (is_socket_activated ? socket_filename : service_filename));
	if (is_socket_activated) {
		if (socket_description) {
			for (value::settings::const_iterator i(socket_description->all_settings().begin()); socket_description->all_settings().end() != i; ++i) {
				const std::string & val(*i);
				run_or_start << multi_line_comment(names.substitute(val));
			}
		}
		if (filedescriptorname) {
			std::string socknames;
			for (value::settings::const_iterator i(filedescriptorname->all_settings().begin()); filedescriptorname->all_settings().end() != i; ++i) {
				if (filedescriptorname->all_settings().begin() != i)
					socknames += ":";
				socknames += names.substitute(*i);
			}
			run_or_start << "setenv LISTEN_FDNAMES " << quote(socknames) << "\n";
		}
		if (listenstream) {
			for (value::settings::const_iterator i(listenstream->all_settings().begin()); listenstream->all_settings().end() != i; ++i) {
				const std::string sockname(names.substitute(*i));
				if (is_local_socket_name(sockname)) {
					run_or_start << "local-stream-socket-listen ";
					if (!is_socket_accept) run_or_start << "--systemd-compatibility ";
					if (backlog) run_or_start << "--backlog " << quote(backlog->last_setting()) << " ";
					if (socketmode) run_or_start << "--mode " << quote(names.substitute(socketmode->last_setting())) << " ";
					if (socketuser) run_or_start << "--user " << quote(names.substitute(socketuser->last_setting())) << " ";
					if (socketgroup) run_or_start << "--group " << quote(names.substitute(socketgroup->last_setting())) << " ";
					if (passcredentials) run_or_start << "--pass-credentials ";
					if (passsecurity) run_or_start << "--pass-security ";
					run_or_start << quote(sockname) << "\n";
				} else {
					std::string listenaddress, listenport;
					split_ip_socket_name(sockname, listenaddress, listenport);
					run_or_start << "tcp-socket-listen ";
					if (!is_socket_accept) run_or_start << "--systemd-compatibility ";
					if (backlog) run_or_start << "--backlog " << quote(backlog->last_setting()) << " ";
#if defined(IPV6_V6ONLY)
					if (is_string(bindipv6only, "both")) run_or_start << "--combine4and6 ";
#endif
#if defined(SO_REUSEPORT)
					if (is_bool_true(reuseport, false)) run_or_start << "--reuse-port ";
#endif
					if (is_bool_true(freebind, false)) run_or_start << "--bind-to-any ";
					run_or_start << quote(listenaddress) << " " << quote(listenport) << "\n";
				}
			}
		}
		if (listendatagram) {
			for (value::settings::const_iterator i(listendatagram->all_settings().begin()); listendatagram->all_settings().end() != i; ++i) {
				const std::string sockname(names.substitute(*i));
				if (is_local_socket_name(sockname)) {
					run_or_start << "local-datagram-socket-listen --systemd-compatibility ";
					if (backlog) run_or_start << "--backlog " << quote(backlog->last_setting()) << " ";
					if (socketmode) run_or_start << "--mode " << quote(names.substitute(socketmode->last_setting())) << " ";
					if (socketuser) run_or_start << "--user " << quote(names.substitute(socketuser->last_setting())) << " ";
					if (socketgroup) run_or_start << "--group " << quote(names.substitute(socketgroup->last_setting())) << " ";
					if (passcredentials) run_or_start << "--pass-credentials ";
					if (passsecurity) run_or_start << "--pass-security ";
					run_or_start << quote(sockname) << "\n";
				} else {
					std::string listenaddress, listenport;
					split_ip_socket_name(sockname, listenaddress, listenport);
					run_or_start << "udp-socket-listen --systemd-compatibility ";
#if defined(IPV6_V6ONLY)
					if (is_string(bindipv6only, "both")) run_or_start << "--combine4and6 ";
#endif
#if defined(SO_REUSEPORT)
					if (is_bool_true(reuseport, false)) run_or_start << "--reuse-port ";
#endif
					if (is_bool_true(freebind, false)) run_or_start << "--bind-to-any ";
					run_or_start << quote(listenaddress) << " " << quote(listenport) << "\n";
				}
			}
		}
		if (listensequentialpacket) {
			for (value::settings::const_iterator i(listensequentialpacket->all_settings().begin()); listensequentialpacket->all_settings().end() != i; ++i) {
				const std::string sockname(names.substitute(*i));
				if (is_local_socket_name(sockname)) {
					run_or_start << "local-seqpacket-socket-listen ";
					if (!is_socket_accept) run_or_start << "--systemd-compatibility ";
					if (backlog) run_or_start << "--backlog " << quote(backlog->last_setting()) << " ";
					if (socketmode) run_or_start << "--mode " << quote(names.substitute(socketmode->last_setting())) << " ";
					if (socketuser) run_or_start << "--user " << quote(names.substitute(socketuser->last_setting())) << " ";
					if (socketgroup) run_or_start << "--group " << quote(names.substitute(socketgroup->last_setting())) << " ";
					if (passcredentials) run_or_start << "--pass-credentials ";
					if (passsecurity) run_or_start << "--pass-security ";
					run_or_start << quote(sockname) << "\n";
				} else {
					std::string listenaddress, listenport;
					split_ip_socket_name(sockname, listenaddress, listenport);
					run_or_start << "ip-seqpacket-socket-listen ";
					if (!is_socket_accept) run_or_start << "--systemd-compatibility ";
					if (backlog) run_or_start << "--backlog " << quote(backlog->last_setting()) << " ";
#if defined(IPV6_V6ONLY)
					if (is_string(bindipv6only, "both")) run_or_start << "--combine4and6 ";
#endif
#if defined(SO_REUSEPORT)
					if (is_bool_true(reuseport, false)) run_or_start << "--reuse-port ";
#endif
					if (is_bool_true(freebind, false)) run_or_start << "--bind-to-any ";
					run_or_start << quote(listenaddress) << " " << quote(listenport) << "\n";
				}
			}
		}
		if (listenfifo) {
			for (value::settings::const_iterator i(listenfifo->all_settings().begin()); listenfifo->all_settings().end() != i; ++i) {
				const std::string fifoname(names.substitute(*i));
				run_or_start << "fifo-listen --systemd-compatibility ";
#if 0 // This does not apply to FIFOs and we want it to generate a diagnostic when present and unused.
				if (backlog) run_or_start << "--backlog " << quote(backlog->last_setting()) << " ";
#else
				if (backlog) backlog->used = false;
#endif
				if (socketmode) run_or_start << "--mode " << quote(names.substitute(socketmode->last_setting())) << " ";
				if (socketuser) run_or_start << "--user " << quote(names.substitute(socketuser->last_setting())) << " ";
				if (socketgroup) run_or_start << "--group " << quote(names.substitute(socketgroup->last_setting())) << " ";
#if 0 // This does not apply to FIFOs and we want it to generate a diagnostic when present and unused.
				if (passcredentials) run_or_start << "--pass-credentials ";
				if (passsecurity) run_or_start << "--pass-security ";
#else
				if (passcredentials) passcredentials->used = false;
				if (passsecurity) passsecurity->used = false;
#endif
				run_or_start << quote(fifoname) << "\n";
			}
		}
		if (listennetlink) {
			for (value::settings::const_iterator i(listennetlink->all_settings().begin()); listennetlink->all_settings().end() != i; ++i) {
				const std::string sockname(names.substitute(*i));
				std::string protocol, multicast_group;
				split_netlink_socket_name(sockname, protocol, multicast_group);
				run_or_start << "netlink-datagram-socket-listen --systemd-compatibility ";
				if (is_bool_true(netlinkraw, false)) run_or_start << "--raw ";
				if (backlog) run_or_start << "--backlog " << quote(backlog->last_setting()) << " ";
				if (receivebuffer) run_or_start << "--receive-buffer-size " << quote(receivebuffer->last_setting()) << " ";
				if (passcredentials) run_or_start << "--pass-credentials ";
				if (passsecurity) run_or_start << "--pass-security ";
				run_or_start << quote(protocol) << " " << quote(multicast_group) << "\n";
			}
		}
		run_or_start << setup_environment.str();
		run_or_start << drop_privileges.str();
		if (is_socket_accept) {
			if (listenstream) {
				for (value::settings::const_iterator i(listenstream->all_settings().begin()); listenstream->all_settings().end() != i; ++i) {
					const std::string sockname(names.substitute(*i));
					if (is_local_socket_name(sockname)) {
						run_or_start << "local-stream-socket-accept ";
						if (maxconnections) run_or_start << "--connection-limit " << quote(maxconnections->last_setting()) << " ";
						run_or_start << "\n";
					} else {
						run_or_start << "tcp-socket-accept ";
						if (maxconnections) run_or_start << "--connection-limit " << quote(maxconnections->last_setting()) << " ";
						if (is_bool_true(keepalive, false)) run_or_start << "--keepalives ";
						if (is_bool_true(nodelay, false)) run_or_start << "--no-delay ";
						run_or_start << "\n";
					}
				}
			}
			if (listensequentialpacket) {
				for (value::settings::const_iterator i(listensequentialpacket->all_settings().begin()); listensequentialpacket->all_settings().end() != i; ++i) {
					const std::string sockname(names.substitute(*i));
					if (is_local_socket_name(sockname)) {
						run_or_start << "local-seqpacket-socket-accept ";
						if (maxconnections) run_or_start << "--connection-limit " << quote(maxconnections->last_setting()) << " ";
						run_or_start << "\n";
					} else {
						run_or_start << "ip-seqpacket-socket-accept ";
						if (maxconnections) run_or_start << "--connection-limit " << quote(maxconnections->last_setting()) << " ";
						if (is_bool_true(keepalive, false)) run_or_start << "--keepalives ";
						if (is_bool_true(nodelay, false)) run_or_start << "--no-delay ";
						run_or_start << "\n";
					}
				}
			}
		} else {
			if (maxconnections) maxconnections->used = false;
		}
		if (is_ucspirules) {
			run_or_start << "ucspi-socket-rules-check";
			if (is_bool_true(socket_logucspirules, service_logucspirules, false))
				run_or_start << " --verbose";
			run_or_start << "\n";
		}
		if (is_socket_accept) {
			if (sslshim) {
				run_or_start << "ssl-run\n";
			}
		}
		run_or_start << "./service\n";

		std::ofstream service;
		open(prog, service, service_dirname + "/service");

		service << "#!/bin/nosh\n";
		if (generation_comment)
			service << multi_line_comment("Service file generated from " + service_filename);
		if (service_description) {
			for (value::settings::const_iterator i(service_description->all_settings().begin()); service_description->all_settings().end() != i; ++i) {
				const std::string & val(*i);
				service << multi_line_comment(names.substitute(val));
			}
		}
		service << socket_redirect;
		service << execute_command.str();
	} else {
		if (service_description) {
			for (value::settings::const_iterator i(service_description->all_settings().begin()); service_description->all_settings().end() != i; ++i) {
				const std::string & val(*i);
				run_or_start << multi_line_comment(names.substitute(val));
			}
		}
		run_or_start << setup_environment.str();
		run_or_start << drop_privileges.str();
		run_or_start << execute_command.str();
	}
	if (merge_run_into_start) {
		run << (is_remain || is_oneshot ? "true" : "pause") << "\n";
	} else {
		if (execstartpre || runtimedirectory || !create_control_group.empty() || !control_group_knobs.empty()) {
			start << perilogue_setup_environment.str();
			start << control_group_knobs;
			start << createrundir;
			start << perilogue_drop_privileges.str();
			if (execstartpre) {
				for (value::settings::const_iterator i(execstartpre->all_settings().begin()); execstartpre->all_settings().end() != i; ) {
					std::list<std::string>::const_iterator j(i++);
					if (execstartpre->all_settings().begin() != j) start << " ;\n"; 
					if (execstartpre->all_settings().end() != i) start << "foreground "; 
					const std::string & val(*j);
					start << names.substitute(shell_expand(strip_leading_minus(val)));
				}
				start << "\n";
			} else
				start << "true\n";
		} else 
			start << "true\n";
	}

	// nosh is not suitable here, since the restart script is passed arguments.
	restart_script << "#!/bin/sh\n";
	if (generation_comment)
		restart_script << multi_line_comment("Restart file generated from " + service_filename);
	if (restartsec) {
		const std::string seconds(restartsec->last_setting());
		// Optimize away explicit zero-length sleeps.
		if ("0" != seconds)
			restart_script << "sleep " << restartsec->last_setting() << "\n";
	} else if (systemd_quirks && !is_socket_activated) {
		restart_script << "sleep 0.1\n";
	}
	if (execrestartpre) {
		std::stringstream s;
		// This allows execline builtins to be used even if neither execline nor the execline-shims are installed.
		s << "/bin/exec\n";
		s << perilogue_setup_environment.str();
		s << perilogue_drop_privileges.str();
		for (value::settings::const_iterator i(execrestartpre->all_settings().begin()); execrestartpre->all_settings().end() != i; ) {
			std::list<std::string>::const_iterator j(i++);
			if (execrestartpre->all_settings().begin() != j) s << " \\;\n"; 
			if (execrestartpre->all_settings().end() != i) s << "foreground "; 
			const std::string & val(*j);
			s << names.substitute(escape_metacharacters(strip_leading_minus(val)));
		}
		restart_script << escape_newlines(s.str()) << "\n";
	}
	if (is_string(restart, "always", !systemd_quirks)) {
		restart_script << "exec true\t# ignore script arguments\n";
	} else
	if (!restart || "no" == tolower(restart->last_setting()) || "never" == tolower(restart->last_setting()) ) {
		restart_script << "exec false\t# ignore script arguments\n";
	} else 
	{
		const bool 
			on_true (is_string(restart, "on-success")),
			on_false(is_string(restart, "on-failure")),
			on_term (on_true), 
			on_abort(is_string(restart, "on-failure") || is_string(restart, "on-abort") || is_string(restart, "on-abnormal")), 
			on_crash(on_abort), 
			on_kill (on_abort); 
		restart_script << 
			"case \"$1\" in\n"
			"\te*)\n"
			"\t\tif test 0 -ne \"$2\"\n"
			"\t\tthen\n"
			"\t\t\texec " << (on_false ? "true" : "false") << "\n"
			"\t\telse\n"
			"\t\t\texec " << (on_true  ? "true" : "false") << "\n"
			"\t\tfi\n"
			"\t\t;;\n"
			"\tt*)\n"
			"\t\texec " << (on_term  ? "true" : "false") << "\n"
			"\t\t;;\n"
			"\tk*)\n"
			"\t\texec " << (on_kill  ? "true" : "false") << "\n"
			"\t\t;;\n"
			"\ta*)\n"
			"\t\texec " << (on_abort ? "true" : "false") << "\n"
			"\t\t;;\n"
			"\tc*|*)\n"
			"\t\texec " << (on_crash ? "true" : "false") << "\n"
			"\t\t;;\n"
			"esac\n"
			"exec false\n";
	}

	stop << "#!/bin/nosh\n";
	if (generation_comment)
		stop << multi_line_comment("Stop file generated from " + (is_socket_activated ? socket_filename : service_filename));
	if (execstoppost || runtimedirectory) {
		stop << perilogue_setup_environment.str();
		stop << removerundir;
		stop << perilogue_drop_privileges.str();
		if (execstoppost) {
			for (value::settings::const_iterator i(execstoppost->all_settings().begin()); execstoppost->all_settings().end() != i; ) {
				std::list<std::string>::const_iterator j(i++);
				if (execstoppost->all_settings().begin() != j) stop << " ;\n"; 
				if (execstoppost->all_settings().end() != i) stop << "foreground "; 
				const std::string & val(*j);
				stop << names.substitute(shell_expand(strip_leading_minus(val)));
			}
			stop << "\n";
		} else
			stop << "true\n";
	} else 
		stop << "true\n";

	// Set the dependency and installation information.
	const bool services_are_relative(per_user_mode || (!etc_bundle && !local_bundle));
	const bool targets_are_relative(per_user_mode || etc_bundle);

#define CREATE_LINKS(l,s) (l ? create_links (prog,envs,names.query_bundle_dirname(),per_user_mode,is_target,services_are_relative,targets_are_relative,bundle_dir_fd,names.substitute((l)->last_setting()),(s)) : static_cast<void>(0))

	CREATE_LINKS(socket_after, "after/");
	CREATE_LINKS(service_after, "after/");
	CREATE_LINKS(socket_before, "before/");
	CREATE_LINKS(service_before, "before/");
	CREATE_LINKS(socket_wants, "wants/");
	CREATE_LINKS(service_wants, "wants/");
	CREATE_LINKS(socket_requires, "wants/");
	CREATE_LINKS(service_requires, "wants/");
	CREATE_LINKS(socket_requisite, "needs/");
	CREATE_LINKS(service_requisite, "needs/");
	CREATE_LINKS(socket_stoppedby, "stopped-by/");
	CREATE_LINKS(service_stoppedby, "stopped-by/");
	CREATE_LINKS(socket_conflicts, "conflicts/");
	CREATE_LINKS(service_conflicts, "conflicts/");
	CREATE_LINKS(socket_wantedby, "wanted-by/");
	CREATE_LINKS(service_wantedby, "wanted-by/");
	CREATE_LINKS(socket_requiredby, "wanted-by/");
	CREATE_LINKS(service_requiredby, "wanted-by/");
	CREATE_LINKS(socket_partof, "requires/");
	CREATE_LINKS(service_partof, "requires/");
	const bool defaultdependencies(
			is_socket_activated ? is_bool_true(socket_defaultdependencies, service_defaultdependencies, true) :
			is_bool_true(service_defaultdependencies, true)
	);
	const bool earlysupervise(
			is_socket_activated ? is_bool_true(socket_earlysupervise, service_earlysupervise, !per_user_mode && etc_bundle) :
			is_bool_true(service_earlysupervise, !per_user_mode && etc_bundle)
	);
	if (defaultdependencies) {
		if (is_socket_activated)
			create_links(prog, envs, names.query_bundle_dirname(), per_user_mode, is_target, services_are_relative, targets_are_relative, bundle_dir_fd, "sockets.target", "wanted-by/");
		if (is_dbus) {
			create_links(prog, envs, names.query_bundle_dirname(), per_user_mode, is_target, services_are_relative, targets_are_relative, bundle_dir_fd, "dbus.socket", "after/");
			create_links(prog, envs, names.query_bundle_dirname(), per_user_mode, is_target, services_are_relative, targets_are_relative, bundle_dir_fd, "dbus.socket", "wants/");
		}
		if (!is_target) {
			create_links(prog, envs, names.query_bundle_dirname(), per_user_mode, is_target, services_are_relative, targets_are_relative, bundle_dir_fd, "basic.target", "after/");
			create_links(prog, envs, names.query_bundle_dirname(), per_user_mode, is_target, services_are_relative, targets_are_relative, bundle_dir_fd, "basic.target", "wants/");
			create_links(prog, envs, names.query_bundle_dirname(), per_user_mode, is_target, services_are_relative, targets_are_relative, bundle_dir_fd, "shutdown.target", "before/");
			create_links(prog, envs, names.query_bundle_dirname(), per_user_mode, is_target, services_are_relative, targets_are_relative, bundle_dir_fd, "shutdown.target", "stopped-by/");
		}
	}
	if (earlysupervise) {
		create_link(prog, names.query_bundle_dirname(), bundle_dir_fd, names.query_runtime_dir_by_name() + "/service-bundles/early-supervise/" + names.query_bundle_basename(), "supervise");
	}
	if (listenstream) {
		for (value::settings::const_iterator i(listenstream->all_settings().begin()); listenstream->all_settings().end() != i; ++i) {
			const std::string sockname(names.substitute(*i));
			if (is_local_socket_name(sockname))
				make_mount_interdependencies(prog, names.query_bundle_dirname(), etc_bundle, true, bundle_dir_fd, sockname);
		}
	}
	if (listendatagram) {
		for (value::settings::const_iterator i(listendatagram->all_settings().begin()); listendatagram->all_settings().end() != i; ++i) {
			const std::string sockname(names.substitute(*i));
			if (is_local_socket_name(sockname))
				make_mount_interdependencies(prog, names.query_bundle_dirname(), etc_bundle, true, bundle_dir_fd, sockname);
		}
	}
	if (listensequentialpacket) {
		for (value::settings::const_iterator i(listensequentialpacket->all_settings().begin()); listensequentialpacket->all_settings().end() != i; ++i) {
			const std::string sockname(names.substitute(*i));
			if (is_local_socket_name(sockname))
				make_mount_interdependencies(prog, names.query_bundle_dirname(), etc_bundle, true, bundle_dir_fd, sockname);
		}
	}
	if (listenfifo) {
		for (value::settings::const_iterator i(listenfifo->all_settings().begin()); listenfifo->all_settings().end() != i; ++i) {
			const std::string fifoname(names.substitute(*i));
			make_mount_interdependencies(prog, names.query_bundle_dirname(), etc_bundle, true, bundle_dir_fd, fifoname);
		}
	}
	flag_file(prog, service_dirname, service_dir_fd, "ready_after_run", is_oneshot);
	flag_file(prog, service_dirname, service_dir_fd, "remain", is_remain);
	flag_file(prog, service_dirname, service_dir_fd, "use_hangup", is_use_hangup);
	flag_file(prog, service_dirname, service_dir_fd, "no_kill_signal", !is_use_kill);

	// Issue the final reports.

	report_unused(prog, socket_profile, socket_filename);
	report_unused(prog, service_profile, service_filename);

	throw EXIT_SUCCESS;
}
