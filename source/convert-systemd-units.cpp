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
#include <sys/resource.h>
#include <sys/socket.h>	// Necessary for the SO_REUSEPORT macro.
#include <netinet/in.h>	// Necessary for the IPV6_V6ONLY macro.
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include "utils.h"
#include "runtime-dir.h"
#include "fdutils.h"
#include "service-manager-client.h"
#include "popt.h"
#include "FileStar.h"
#include "DirStar.h"
#include "FileDescriptorOwner.h"
#include "bundle_creation.h"
#include "machine_id.h"

/* Unit names ***************************************************************
// **************************************************************************
*/

namespace {

const std::string slash("/");

struct names {
	names(const char * a);
	void set_prefix(const std::string & v, bool esc, bool alt, bool ext) { set(esc, alt, ext, escaped_prefix, prefix, v); }
	void set_instance(const std::string & v, bool esc, bool alt, bool ext) { set(esc, alt, ext, escaped_instance, instance, v); }
	void set_bundle(const std::string & r, const std::string & b) { bundle_basename = b; bundle_dirname = r + b; }
	void set_machine_id(const std::string & v) { machine_id = v; }
	void set_user(const std::string & u);
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
	const std::string & query_group_name () const { return group_name; }
	const std::string & query_GID () const { return GID; }
	const std::string & query_runtime_dir_by_name () const { return runtime_dir_by_name; }
	const std::string & query_runtime_dir_by_UID () const { return runtime_dir_by_UID; }
	const std::string & query_vartmp_dir () const { return vartmp_dir; }
	const std::string & query_tmp_dir () const { return tmp_dir; }
	const std::string & query_config_dir () const { return config_dir; }
	const std::string & query_state_dir () const { return state_dir; }
	const std::string & query_cache_dir () const { return cache_dir; }
	const std::string & query_log_dir () const { return log_dir; }
	std::string substitute ( const std::string & );
	std::list<std::string> substitute ( const std::list<std::string> & );
protected:
	std::string unit_dirname, unit_basename, escaped_unit_basename, prefix, escaped_prefix, instance, escaped_instance, bundle_dirname, bundle_basename, machine_id, user_name, UID, group_name, GID, runtime_dir_by_name, runtime_dir_by_UID, state_dir, cache_dir, tmp_dir, vartmp_dir, config_dir, log_dir;
	void set ( bool esc, bool alt, bool ext, std::string & escaped, std::string & normal, const std::string & value ) {
		if (esc)
			escaped = systemd_name_escape(alt, ext, normal = value);
		else
			normal = systemd_name_unescape(alt, ext, escaped = value);
	}
};

inline
void
split_name (
	const char * s,
	std::string & dirname,
	std::string & basename
) {
	if (const char * slashpos = std::strrchr(s, '/')) {
		basename = slashpos + 1;
		dirname = std::string(s, slashpos + 1);
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

inline
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

inline
std::string
effective_group_name ()
{
	if (struct group * g = getgrgid(getegid()))
		if (g->gr_name) {
			const std::string n(g->gr_name);
			endgrent();
			return n;
		}
	endgrent();
	return "nobody";
}

inline
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

inline
std::string
leading_slashify (
	const std::string & s
) {
	return slash == s.substr(0, 1) ? s : slash + s;
}

}

#if defined(__LINUX__) || defined(__linux__)
#	define GROUP_0_NAME	"root"
#else
#	define GROUP_0_NAME	"wheel"
#endif

names::names(const char * a) : 
	user_name(per_user_mode ? effective_user_name() : "root"),
	UID("0"),
	group_name(per_user_mode ? effective_group_name() : GROUP_0_NAME),
	GID("0"),
	runtime_dir_by_name("/run"),
	runtime_dir_by_UID("/run"),
	state_dir("/var/lib/"),
	cache_dir("/var/cache/"),
	tmp_dir("/tmp/"),
	vartmp_dir("/var/tmp/"),
	config_dir("/etc/"),
	log_dir(per_user_mode ? effective_user_log_dir() : "/var/log/sv/")
{ 
	split_name(a, unit_dirname, unit_basename); 
	escaped_unit_basename = systemd_name_escape(false, false, unit_basename); 
	if (per_user_mode) {
		std::string home_dir;
		get_user_dirs_for(user_name, UID, GID, runtime_dir_by_name, runtime_dir_by_UID, home_dir);
		cache_dir = home_dir + "/.cache";
		state_dir = home_dir + "/.config";
		config_dir = home_dir + "/.config";
	}
}

void
names::set_user(
	const std::string & v
) {
	user_name = v;
	if (per_user_mode) {
		std::string home_dir;
		get_user_dirs_for(user_name, UID, GID, runtime_dir_by_name, runtime_dir_by_UID, home_dir);
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
			case 'r': r += query_runtime_dir_by_UID(); break;
			case 'V': r += query_vartmp_dir(); break;
			case 'T': r += query_tmp_dir(); break;
			case 'E': r += query_config_dir(); break;
			case 'S': r += query_state_dir(); break;
			case 'C': r += query_cache_dir(); break;
			case 'L': r += query_log_dir(); break;
			case 'g': r += query_group_name(); break;
			case 'G': r += query_GID(); break;
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

/* Profile file loader ******************************************************
// **************************************************************************
*/

namespace {

struct value {
	typedef std::list<std::string> settings;

	value() : used(false), d() {}
#if 0	// This code is unused for now.
	value(const std::string & v) : used(false), d() { d.push_back(v); }
#endif

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

const char * systemd_prefixes[] = {
	// Administrator-supplied units have the highest precedence.
	"/etc/",
#if defined(__LINUX__) || defined(__linux__)
	"/usr/local/lib/",
#else
	"/usr/local/etc/",
#endif
	// Generated units come next.
	"/run/",
	// Followed by packaged units.
#if !defined(__LINUX__) && !defined(__linux__)
	"/usr/local/lib/",
	"/usr/local/share/",
	"/usr/share/",
#endif
	"/usr/lib/",
	"/lib/",
	// Followed by whatever is in the local directory, as an extension to the systemd search behaviour.
	"",
};

inline
FILE *
find (
	std::string & path,
	const std::string & base
) {
	if (!path.empty())
		return std::fopen((path + base).c_str(), "r");
	int error(ENOENT);	// the most interesting error encountered
	for ( const char ** p(systemd_prefixes); p < systemd_prefixes + sizeof systemd_prefixes/sizeof *systemd_prefixes; ++p) {
		path = (std::string(*p) + "systemd/") + (per_user_mode ? "user/" : "system/");
		FILE * f = std::fopen((path + base).c_str(), "r");
		if (f) return f;
		if (ENOENT == errno) 
			errno = error;	// Restore a more interesting error.
		else
			error = errno;	// Save this interesting error.
	}
	path = "";
	return NULL;
}

inline
bool
ends_with (
	const char * s,
	const char * p
) {
	const std::size_t sl(std::strlen(s));
	const std::size_t pl(std::strlen(p));
	return pl <= sl && 0 == memcmp(s + sl - pl, p, pl);
}

inline
bool
bracketed (
	const std::string & s
) {
	return s.length() > 1 && '[' == s[0] && ']' == s[s.length() - 1];
}

inline
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

inline
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

inline
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

inline
void
load (
	const char * prog,
	profile & p,
	FILE * file,
	const std::string & file_name
) {
	if (!is_regular(prog, file_name, file))
		throw EXIT_FAILURE;
	load(p, file);
}

inline
void
load (
	const char * prog,
	profile & p,
	std::list<std::string> & source_filenames,
	const std::string & path_name,
	const std::string & base_name
) {
	const std::string snippet_dir_name(path_name + base_name);
	FileDescriptorOwner snippet_dir_fd(open_dir_at(AT_FDCWD, snippet_dir_name.c_str()));
	if (0 > snippet_dir_fd.get()) {
		const int error(errno);
		if (ENOENT == error) return;
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, snippet_dir_name.c_str(), std::strerror(error));
		throw EXIT_FAILURE;
	}
	const DirStar snippet_dir(snippet_dir_fd);
	if (!snippet_dir) {
abort_scan:
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, snippet_dir_name.c_str(), std::strerror(error));
		throw EXIT_FAILURE;
	}
	for (;;) {
		errno = 0;
		const dirent * entry(readdir(snippet_dir));
		if (!entry) {
			if (errno) goto abort_scan;
			break;
		}
#if defined(_DIRENT_HAVE_D_NAMLEN)
		if (1 > entry->d_namlen) continue;
#endif
		if ('.' == entry->d_name[0]) continue;
		if (!ends_with(entry->d_name, ".conf")) continue;
#if defined(_DIRENT_HAVE_D_TYPE)
		if (DT_REG != entry->d_type && DT_LNK != entry->d_type) {
			std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, snippet_dir_name.c_str(), entry->d_name, "Not a regular file.");
			throw EXIT_FAILURE;
		}
#endif

		FileDescriptorOwner snippet_file_fd(open_read_at(snippet_dir.fd(), entry->d_name));
		if (0 > snippet_file_fd.get()) {
bad_file:
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, snippet_dir_name.c_str(), entry->d_name, std::strerror(error));
			throw EXIT_FAILURE;
		}
		struct stat s;
		if (0 > fstat(snippet_file_fd.get(), &s)) goto bad_file;
		if (!S_ISREG(s.st_mode)) {
			std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, snippet_dir_name.c_str(), entry->d_name, "Not a regular file.");
			throw EXIT_FAILURE;
		}

		FileStar snippet_file(fdopen(snippet_file_fd.get(), "r"));
		if (!snippet_file) goto bad_file;
		snippet_file_fd.release();

		const std::string snippet_file_name(snippet_dir_name + "/" + entry->d_name);
		source_filenames.push_back(snippet_file_name);
		load(prog, p, snippet_file, snippet_file_name);
	}
}

void
load (
	const char * prog,
	profile & p,
	FILE * file,
	std::list<std::string> & source_filenames,
	std::string & filename,
	const std::string & unit_path,
	const std::string & unit_base,
	const std::string & prefix,
	const std::string & instance,
	const std::string & suffix
) {
	filename = unit_path + unit_base;
	if (!file) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, filename.c_str(), std::strerror(error));
		throw EXIT_FAILURE;
	}
	source_filenames.push_back(filename);
	load(prog, p, file, filename);
	load(prog, p, source_filenames, unit_path, unit_base + ".d");
	if (!instance.empty())
		load(prog, p, source_filenames, unit_path, prefix + "@" + instance + suffix + ".d");
}

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
				for (value::settings::const_iterator i2(v.all_settings().begin()), e2(v.all_settings().end()); e2 != i2; ++i2) {
				       const std::string & val(*i2);
				       std::fprintf(stderr, "%s: WARNING: %s: Unused setting: [%s] %s = %s\n", prog, name.c_str(), section.c_str(), var.c_str(), val.c_str());
				}
			}
		}
	}
}

}

/* Settings processing utility functions ************************************
// **************************************************************************
*/

namespace {

std::string
english_list (
	const std::list<std::string> & items
) {
	std::string r;
	for (std::list<std::string>::const_iterator b(items.begin()), p(b), e(items.end()), l(e), c(e); p != e; l = c) {
		c = p++;
		if (c != b) {
			if (p == e) {
				if (l != b) r += ", ";
				r += "and ";
			} else
				r += ", ";
		}
		r += *c;
	}
	return r;
}

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

std::string
strip_leading_minus (
	const std::string & s
) {
	if (s.length() < 1 || '-' != s[0]) return s;
	return s.substr(1, std::string::npos);
}

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

inline std::string quote ( const std::string & s ) { return quote_for_nosh(s); }

inline
bool
remove_last_component (
	std::string & dirname,
	std::string & basename
) {
	const std::string::size_type slashpos(dirname.rfind('/'));
	if (std::string::npos != slashpos) {
		basename = std::string(dirname, slashpos + 1, std::string::npos);
		dirname = std::string(dirname, 0, 0 == slashpos ? 1 : slashpos);
		return true;
	} else {
		basename = dirname;
		dirname = std::string();
		return false;
	}
}

bool
has_dot_or_dotdot_anywhere (
	std::string dirname
) {
	std::string basename;
	for (;;) {
		const bool more(remove_last_component(dirname, basename));
		if ("." == basename || ".." == basename) return true;
		if (!more) return false;
	}
}

/// \brief Convert from nosh and systemd quoting and escaping rules to the ones for sh.
/// sh metacharacters other than $ (if dollar is true) and # (always) are also disabled by escaping them.
std::string
escape_metacharacters (
	const std::string & s,
	bool dollar
) {
	std::string r;
	enum { NORMAL, DQUOT, SQUOT } state(NORMAL);
	for (std::string::const_iterator p(s.begin()), e(s.end()); e != p; ++p) {
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
				else if (!dollar && '$' == c)
					r += '\\';
				break;
			case DQUOT:
				if ('\\' == c && p + 1 != s.end()) {
					// nosh and systemd escaping within double quotes differs from that of sh.
					// sh has some quite wacky rules where \ is mostly not an escape character.
					c = *++p;
					if ('`' == c || '\\' == c || '\"' == c)
						r += '\\'; 
					else if (!dollar && '$' == c)
						r += '\\';
				} else if ('\"' == c) state = NORMAL;
				break;
			case SQUOT:
				if ('\\' == c && p + 1 != s.end()) {
					// nosh and systemd escaping within single quotes differs from that of sh.
					// In the sh rules \ is never an escape character and there is no way to quote ' .
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

inline
std::string
shell_expand (
	const std::string & s
) {
	enum { NORMAL, DQUOT, SQUOT } state(NORMAL);
	for (std::string::const_iterator p(s.begin()), e(s.end()); e != p; ++p) {
		const char c(*p);
		switch (state) {
			case NORMAL:
				if ('\\' == c && p != s.end()) ++p;
				else if ('\'' == c) state = SQUOT;
				else if ('\"' == c) state = DQUOT;
				else if ('$' == c)
					return "sh -c " + quote("exec " + escape_metacharacters(s, true));
				break;
			case DQUOT:
				if ('\\' == c && p != s.end()) ++p;
				else if ('\"' == c) state = NORMAL;
				else if ('$' == c)
					return "sh -c " + quote("exec " + escape_metacharacters(s, true));
				break;
			case SQUOT:
				if ('\\' == c && p != s.end()) ++p;
				else if ('\'' == c) state = NORMAL;
				break;
		}
	}
	return s;
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

bool
is_string (
	const value & v,
	const std::string & s
) {
	const std::string r(tolower(v.last_setting()));
	return s == r;
}

bool
is_string (
	const value & v,
	const std::string & s0,
	const std::string & s1
) {
	const std::string r(tolower(v.last_setting()));
	return s0 == r || s1 == r;
}

bool
is_string (
	const value * v,
	const std::string & s,
	const bool def
) {
	return v ? is_string(*v, s) : def;
}

bool
is_string (
	const value * v,
	const std::string & s
) {
	return v && is_string(*v, s);
}

bool
is_string (
	const value * v,
	const char * s0,
	const char * s1
) {
	return v && is_string(*v, s0, s1);
}

bool
is_bool_true (
	const value & v
) {
	const std::string r(tolower(v.last_setting()));
	return ::is_bool_true(r) || "1" == r;
}

bool
is_bool_true (
	const value * v,
	bool def
) {
	if (v) return is_bool_true(*v);
	return def;
}

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

bool
is_local_socket_name (
	const std::string & s
) {
	return s.length() > 0 && '/' == s[0];
}

inline
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
inline
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
		popt::top_table_definition main_option(sizeof main_table/sizeof *main_table, main_table, "Main options", "{unit}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		if (p.stopped()) throw EXIT_SUCCESS;
		if (bundle_root_str) bundle_root = bundle_root_str + slash;
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
		throw static_cast<int>(EXIT_USAGE);
	}

	bool is_socket_activated(false), is_timer_activated(false), is_target(false), merge_run_into_start(false);

	{
		std::string bundle_basename;
		if (ends_in(names.query_unit_basename(), ".target", bundle_basename)) {
			is_target = true;
		} else
		if (ends_in(names.query_unit_basename(), ".socket", bundle_basename)) {
			is_socket_activated = true;
		} else
		if (ends_in(names.query_unit_basename(), ".timer", bundle_basename)) {
			is_timer_activated = true;
		} else
		if (ends_in(names.query_unit_basename(), ".service", bundle_basename)) {
		} else
		{
			bundle_basename = names.query_unit_basename();
		}
		names.set_bundle(bundle_root, bundle_basename);
	}

	machine_id::erase();
	if (!machine_id::read_non_volatile() && !machine_id::read_fallbacks(envs))
	       machine_id::create();
	names.set_machine_id(machine_id::human_readable_form_compact());

	std::string socket_filename;
	profile socket_profile;
	std::string timer_filename;
	profile timer_profile;
	std::string service_filename;
	profile service_profile;
	std::list<std::string> source_filenames;
	bool is_instance(false), is_socket_accept(false);

	if (is_socket_activated) {
		const std::string suffix(".socket");
		std::string prefix(names.query_bundle_basename()), instance;

		std::string socket_unit_path(names.query_unit_dirname());
		std::string socket_unit_base(prefix  + suffix);
		FileStar socket_file(find(socket_unit_path, socket_unit_base));
		if (!socket_file) {
			std::string::size_type atc(prefix.find('@'));
			if (ENOENT == errno && std::string::npos != atc) {
				instance = prefix.substr(atc + 1, std::string::npos);
				prefix = prefix.substr(0, atc);
				names.set_instance(instance, escape_instance, alt_escape, ext_escape);
				is_instance = true;

				socket_unit_path = names.query_unit_dirname();
				socket_unit_base = prefix + "@" + suffix;
				socket_file = find(socket_unit_path, socket_unit_base);
			}
		}
		load(prog, socket_profile, socket_file, source_filenames, socket_filename, socket_unit_path, socket_unit_base, prefix, instance, suffix);

		names.set_prefix(prefix, escape_prefix, alt_escape, ext_escape);

		value * accept(socket_profile.use("socket", "accept"));
		is_socket_accept = is_bool_true(accept, false);

		std::string service_unit_path(names.query_unit_dirname());
		std::string service_unit_base(prefix + std::string(is_socket_accept ? "@" : "") + ".service");
		instance = "";
		FileStar service_file(find(service_unit_path, service_unit_base));
		load(prog, service_profile, service_file, source_filenames, service_filename, service_unit_path, service_unit_base, prefix, instance, ".service");
	} else 
	if (is_timer_activated) {
		const std::string suffix(".timer");
		std::string prefix(names.query_bundle_basename()), instance;

		std::string timer_unit_path(names.query_unit_dirname());
		std::string timer_unit_base(prefix + suffix);
		FileStar timer_file(find(timer_unit_path, timer_unit_base));
		if (!timer_file) {
			std::string::size_type atc(prefix.find('@'));
			if (ENOENT == errno && std::string::npos != atc) {
				instance = prefix.substr(atc + 1, std::string::npos);
				prefix = prefix.substr(0, atc);
				names.set_instance(instance, escape_instance, alt_escape, ext_escape);
				is_instance = true;

				timer_unit_path = names.query_unit_dirname();
				timer_unit_base = prefix + "@.timer";
				timer_file = find(timer_unit_path, timer_unit_base);
			}
		}
		load(prog, timer_profile, timer_file, source_filenames, timer_filename, timer_unit_path, timer_unit_base, prefix, instance, suffix);

		names.set_prefix(prefix, escape_prefix, alt_escape, ext_escape);

		std::string service_unit_path(names.query_unit_dirname());
		std::string service_unit_base(prefix + ".service");
		instance = "";
		FileStar service_file(find(service_unit_path, service_unit_base));
		load(prog, service_profile, service_file, source_filenames, service_filename, service_unit_path, service_unit_base, prefix, instance, ".service");
	} else 
	{
		const std::string suffix(is_target ? ".target" : ".service");
		std::string prefix(names.query_bundle_basename()), instance;

		std::string service_unit_path(names.query_unit_dirname());
		std::string service_unit_base(prefix + suffix);
		FileStar service_file(find(service_unit_path, service_unit_base));
		if (!service_file) {
			std::string::size_type atc(prefix.find('@'));
			if (ENOENT == errno && std::string::npos != atc) {
				instance = prefix.substr(atc + 1, std::string::npos);
				prefix = prefix.substr(0, atc);
				names.set_instance(instance, escape_instance, alt_escape, ext_escape);
				is_instance = true;

				service_unit_path = names.query_unit_dirname();
				service_unit_base = prefix + "@" + suffix;
				service_file = find(service_unit_path, service_unit_base);
			}
		}
		load(prog, service_profile, service_file, source_filenames, service_filename, service_unit_path, service_unit_base, prefix, instance, suffix);

		names.set_prefix(prefix, escape_prefix, alt_escape, ext_escape);
	}
	const std::string source_filename_list(english_list(source_filenames));

	value * onactivesec(timer_profile.use("timer", "onactivesec"));
	value * onbootsec(timer_profile.use("timer", "onbootsec"));
	value * onstartupsec(timer_profile.use("timer", "onstartupsec"));
	value * onunitactivesec(timer_profile.use("timer", "onunitactivesec"));
	value * onunitinactivesec(timer_profile.use("timer", "onunitinactivesec"));
	value * oncalendar(timer_profile.use("timer", "oncalendar"));
	value * timer_description(timer_profile.use("unit", "description"));
	value * timer_defaultdependencies(timer_profile.use("unit", "defaultdependencies"));
	value * timer_earlysupervise(timer_profile.use("unit", "earlysupervise"));	// This is an extension to systemd.
	value * timer_before(timer_profile.use("unit", "before"));
	value * timer_after(timer_profile.use("unit", "after"));
	value * timer_conflicts(timer_profile.use("unit", "conflicts"));
	value * timer_wants(timer_profile.use("unit", "wants"));
	value * timer_requires(timer_profile.use("unit", "requires"));
	value * timer_requisite(timer_profile.use("unit", "requisite"));
	value * timer_partof(timer_profile.use("unit", "partof"));
	value * timer_wantedby(timer_profile.use("install", "wantedby"));
	value * timer_requiredby(timer_profile.use("install", "requiredby"));
	value * timer_stoppedby(timer_profile.use("install", "stoppedby"));	// This is an extension to systemd.
	value * timer_requiresmountsfor(timer_profile.use("unit", "requiresmountsfor"));
	value * timer_wantsmountsfor(timer_profile.use("unit", "wantsmountsfor"));	// This is an extension to systemd.
	value * timer_aftermountsfor(timer_profile.use("unit", "aftermountsfor"));	// This is an extension to systemd.

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
	value * socket_description(socket_profile.use("unit", "description"));
	value * socket_defaultdependencies(socket_profile.use("unit", "defaultdependencies"));
	value * socket_earlysupervise(socket_profile.use("unit", "earlysupervise"));	// This is an extension to systemd.
	value * socket_before(socket_profile.use("unit", "before"));
	value * socket_after(socket_profile.use("unit", "after"));
	value * socket_conflicts(socket_profile.use("unit", "conflicts"));
	value * socket_wants(socket_profile.use("unit", "wants"));
	value * socket_requires(socket_profile.use("unit", "requires"));
	value * socket_requisite(socket_profile.use("unit", "requisite"));
	value * socket_partof(socket_profile.use("unit", "partof"));
	value * socket_wantedby(socket_profile.use("install", "wantedby"));
	value * socket_requiredby(socket_profile.use("install", "requiredby"));
	value * socket_stoppedby(socket_profile.use("install", "stoppedby"));	// This is an extension to systemd.
	value * socket_requiresmountsfor(socket_profile.use("unit", "requiresmountsfor"));
	value * socket_wantsmountsfor(socket_profile.use("unit", "wantsmountsfor"));	// This is an extension to systemd.
	value * socket_aftermountsfor(socket_profile.use("unit", "aftermountsfor"));	// This is an extension to systemd.
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
	value * runtimedirectoryowner(service_profile.use("service", "runtimedirectoryowner"));	// This is an extension to systemd.
	value * runtimedirectorygroup(service_profile.use("service", "runtimedirectorygroup"));	// This is an extension to systemd.
	value * runtimedirectorymode(service_profile.use("service", "runtimedirectorymode"));
	value * runtimedirectorypreserve(service_profile.use("service", "runtimedirectorypreserve"));
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
	value * environmentuseronly(service_profile.use("service", "environmentuseronly"));	// This is an extension to systemd.
	value * environmentappendpath(service_profile.use("service", "environmentappendpath"));	// This is an extension to systemd.
#if defined(__LINUX__) || defined(__linux__)
	value * utmpidentifier(service_profile.use("service", "utmpidentifier"));
#endif
	value * ttypath(service_profile.use("service", "ttypath"));
	value * ttyfromenv(service_profile.use("service", "ttyfromenv"));	// This is an extension to systemd.
	value * ttyreset(service_profile.use("service", "ttyreset"));
	value * ttyprompt(service_profile.use("service", "ttyprompt"));		// This is an extension to systemd.
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
	value * memorymin(service_profile.use("service", "memorymin"));
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
	value * service_description(service_profile.use("unit", "description"));
	value * service_defaultdependencies(service_profile.use("unit", "defaultdependencies"));
	value * service_earlysupervise(service_profile.use("unit", "earlysupervise"));	// This is an extension to systemd.
	value * service_before(service_profile.use("unit", "before"));
	value * service_after(service_profile.use("unit", "after"));
	value * service_conflicts(service_profile.use("unit", "conflicts"));
	value * service_wants(service_profile.use("unit", "wants"));
	value * service_requires(service_profile.use("unit", "requires"));
	value * service_requisite(service_profile.use("unit", "requisite"));
	value * service_partof(service_profile.use("unit", "partof"));
	value * service_wantedby(service_profile.use("install", "wantedby"));
	value * service_requiredby(service_profile.use("install", "requiredby"));
	value * service_stoppedby(service_profile.use("install", "stoppedby"));	// This is an extension to systemd.
	value * service_requiresmountsfor(service_profile.use("unit", "requiresmountsfor"));
	value * service_wantsmountsfor(service_profile.use("unit", "wantsmountsfor"));	// This is an extension to systemd.
	value * service_aftermountsfor(service_profile.use("unit", "aftermountsfor"));	// This is an extension to systemd.
	value * service_ucspirules(service_profile.use("service", "ucspirules"));	// This is an extension to systemd.
	value * service_logucspirules(service_profile.use("service", "logucspirules"));	// This is an extension to systemd.

	if (user)
		names.set_user(names.substitute(user->last_setting()));

	// Actively prevent certain unsupported combinations.

	if (type && "simple" != tolower(type->last_setting()) && "exec" != tolower(type->last_setting()) && "forking" != tolower(type->last_setting()) && "oneshot" != tolower(type->last_setting()) && "dbus" != tolower(type->last_setting())) {
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
			if (restart ? "no" != tolower(restart->last_setting()) && "never" != tolower(restart->last_setting()) : !systemd_quirks || is_timer_activated)
				std::fprintf(stderr, "%s: WARNING: %s: restart=%s: %s\n", prog, service_filename.c_str(), restart ? restart->last_setting().c_str() : "always", "Single-shot service may never reach ready");
		} else
		if (!execstart) {
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, service_filename.c_str(), "Missing mandatory ExecStart entry.");
			throw EXIT_FAILURE;
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
		for (value::settings::const_iterator i(runtimedirectory->all_settings().begin()), e(runtimedirectory->all_settings().end()); e != i; ++i) {
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
	if (memorymin) {
		/// FIXME: memmin?
		control_group_knobs += "foreground set-control-group-knob --infinity-is-max --multiplier-suffixes memory.min " + quote(names.substitute(memorymin->last_setting())) + " ;\n";
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
			for (std::list<std::string>::const_iterator i(list.begin()), e(list.end()); e != i; ++i)
				jvm += " --version " + quote(*i);
		}
		if (jvmoperatingsystems) {
			const std::list<std::string> list(split_list(names.substitute(jvmoperatingsystems->last_setting())));
			for (std::list<std::string>::const_iterator i(list.begin()), e(list.end()); e != i; ++i)
				jvm += " --operating-system " + quote(*i);
		}
		if (jvmmanufacturers) {
			const std::list<std::string> list(split_list(names.substitute(jvmmanufacturers->last_setting())));
			for (std::list<std::string>::const_iterator i(list.begin()), e(list.end()); e != i; ++i)
				jvm += " --manufacturer " + quote(*i);
		}
		jvm += "\n";
	}
	if (is_bool_true(jvmdefault, false))
		jvm += "find-default-jvm\n";
	if (oomscoreadjust)
		// The -- is necessary because the adjustment could be a negative number, starting with a dash,
		priority += "oom-kill-protect -- " + quote(names.substitute(oomscoreadjust->last_setting())) + "\n";
#if defined(__LINUX__) || defined(__linux__)
	const bool is_private_tmp(is_bool_true(privatetmp, false));
	const bool is_private_network(is_bool_true(privatenetwork, false));
	const bool is_private_devices(is_bool_true(privatedevices, false));
	const bool is_private_homes(is_bool_true(protecthome, false));
	const bool is_private_mount(is_private_tmp||is_private_devices||is_private_homes||readonlypaths||readwritedirectories||inaccessiblepaths);
	const bool is_private(is_private_mount||is_private_network);
	const bool is_protect_os(is_string(protectsystem, "strict") || is_string(protectsystem, "full") || is_bool_true(protectsystem, false));
	const bool is_protect_etc(is_string(protectsystem, "strict") || is_string(protectsystem, "full"));
	const bool is_protect_homes(is_string(protecthome, "read-only"));
	const bool is_protect_sysctl(is_bool_true(protectkerneltunables, false));
	const bool is_protect_cgroups(is_bool_true(protectcontrolgroups, false));
	const bool is_protect_mount(is_protect_os||is_protect_etc||is_protect_homes||is_protect_sysctl||is_protect_cgroups);
	std::string mount_namespace_commands;
	if (mountflags||is_private||is_protect_mount) {
		mount_namespace_commands += "unshare";
		if (mountflags||is_private_mount||is_protect_mount) mount_namespace_commands += " --mount";
		if (is_private_network) mount_namespace_commands += " --network";
		mount_namespace_commands += "\n";
		if (is_private_mount||is_protect_mount) {
			mount_namespace_commands += "set-mount-object --recursive slave /\n";
			if (is_private_mount) {
				mount_namespace_commands += "make-private-fs";
				if (is_private_tmp) mount_namespace_commands += " --temp";
				if (is_private_devices) mount_namespace_commands += " --devices";
				if (is_private_homes) mount_namespace_commands += " --homes";
				if (inaccessiblepaths) {
					for (value::settings::const_iterator i(inaccessiblepaths->all_settings().begin()), e(inaccessiblepaths->all_settings().end()); e != i; ++i) {
						const std::string dir(names.substitute(*i));
						mount_namespace_commands += " --hide " + quote(dir);
					}
				}
				mount_namespace_commands += "\n";
			}
			if (is_protect_mount) {
				mount_namespace_commands += "make-read-only-fs";
				if (is_protect_os) mount_namespace_commands += " --os";
				if (is_protect_etc) mount_namespace_commands += " --etc";
				if (is_protect_homes) mount_namespace_commands += " --homes";
				if (is_protect_sysctl) mount_namespace_commands += " --sysctl";
				if (is_protect_cgroups) mount_namespace_commands += " --cgroups";
				if (readonlypaths) {
					for (value::settings::const_iterator i(readonlypaths->all_settings().begin()), e(readonlypaths->all_settings().end()); e != i; ++i) {
						const std::string dir(names.substitute(*i));
						mount_namespace_commands += " --include " + quote(dir);
					}
				}
				if (readwritedirectories) {
					for (value::settings::const_iterator i(readwritedirectories->all_settings().begin()), e(readwritedirectories->all_settings().end()); e != i; ++i) {
						const std::string dir(names.substitute(*i));
						mount_namespace_commands += " --except " + quote(dir);
					}
				}
				mount_namespace_commands += "\n";
			}
		}
		/// \bug FIXME is_private_network needs to set up a lo0 device.
		if (mountflags) 
			mount_namespace_commands += "set-mount-object --recursive " + quote(mountflags->last_setting()) + " /\n";
		else
		if (is_private_mount||is_protect_mount)
			mount_namespace_commands += "set-mount-object --recursive shared /\n";
	}
#endif
	const bool chrootall(!is_bool_true(rootdirectorystartonly, false));
	std::string setsid;
	if (is_bool_true(sessionleader, false)) setsid += "setsid\n";
	if (is_bool_true(processgroupleader, false)) setsid += "setpgrp\n";
	if (localreaper) setsid += "local-reaper " + quote(names.substitute(localreaper->last_setting())) + "\n";
	std::string chroot;
	if (rootdirectory) chroot += "chroot " + quote(names.substitute(rootdirectory->last_setting())) + "\n";
	std::string envuidgid, setuidgid;
	const bool no_setuidgid(is_bool_true(environmentuseronly, false));
	if (user) {
		if (no_setuidgid || rootdirectory) {
			envuidgid += "envuidgid";
			if (group)
				envuidgid += " --primary-group " + quote(names.substitute(group->last_setting()));
			if (is_bool_true(systemdusergroups, systemd_quirks))
				envuidgid += " --supplementary";
			envuidgid += " -- " + quote(names.query_user_name()) + "\n";
			if (!no_setuidgid)
				setuidgid += "setuidgid-fromenv\n";
		} else {
			setuidgid += "setuidgid";
			if (group)
				setuidgid += " --primary-group " + quote(names.substitute(group->last_setting()));
			if (is_bool_true(systemdusergroups, systemd_quirks))
				setuidgid += " --supplementary";
			setuidgid += " -- " + quote(names.query_user_name()) + "\n";
		}
		if (is_bool_true(sessionleader, false)) setsid += "setlogin -- " + quote(names.query_user_name()) + "\n";
	} else if (group) {
		envuidgid += "envgid -- " + quote(names.substitute(group->last_setting())) + "\n";
		if (!no_setuidgid)
			setuidgid += "setgid-fromenv\n";
	}
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
		for (value::settings::const_iterator i(runtimedirectory->all_settings().begin()), e(runtimedirectory->all_settings().end()); e != i; ++i) {
			const std::list<std::string> list(split_list(names.substitute(*i)));
			for (std::list<std::string>::const_iterator j(list.begin()), je(list.end()); je != j; ++j) {
				if (j->empty()) {
					std::fprintf(stderr, "%s: ERROR: %s: %s: Empty runtime directory\n", prog, service_filename.c_str(), i->c_str());
					continue;
				}
				if ('/' == j->front() || has_dot_or_dotdot_anywhere(*j)) {
					std::fprintf(stderr, "%s: ERROR: %s: %s: Disallowed runtime directory\n", prog, service_filename.c_str(), j->c_str());
					continue;
				}
				const std::string dir(names.query_runtime_dir_by_name() + slash + *j);
				if (!dirs.empty()) dirs += ' ';
				if (!dirs_slash.empty()) dirs_slash += ' ';
				dirs += quote(dir);
				dirs_slash += quote(dir);
				if ('/' != dir.back()) dirs_slash += slash;
			}
		}
		createrundir += "foreground install -d";
		if (runtimedirectorymode) createrundir += " -m " + quote(names.substitute(runtimedirectorymode->last_setting()));
		if (user || runtimedirectoryowner)
			createrundir += " -o " + quote(names.substitute((runtimedirectoryowner ? runtimedirectoryowner : user)->last_setting()));
		if (group || runtimedirectorygroup)
			createrundir += " -g " + quote(names.substitute((runtimedirectorygroup ? runtimedirectorygroup : group)->last_setting()));
		createrundir += " -- " + dirs + " ;\n";
		if (!is_bool_true(runtimedirectorypreserve, false)) {
			// The trailing slash is a(nother) safety measure, to help ensure that this is a directory that we are removing, uncondintally, as the superuser.
			removerundir += "foreground rm -r -f -- " + dirs_slash + " ;\n";
		}
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
		if (limitnice) {
			const std::string n(limitnice->last_setting());
			if (!n.empty() && ('+' == n[0] || '-' == n[0]))
				priority += "nice -n " + quote(n) + "\n";
			else
				split_limit(n,"e",s,h);
		}
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
	if (is_bool_true(systemduserenvironment, systemd_quirks) || (per_user_mode && is_dbus)) {
		if (user) {
			env += "envuidgid";
			if (group)
				env += " --primary-group " + quote(names.substitute(group->last_setting()));
			if (is_bool_true(systemdusergroups, systemd_quirks))
				env += " --supplementary";
			env += " -- " + quote(names.query_user_name()) + "\n";
		} else {
			env += "getuidgid\n";
			if (group)
				env += "envgid -- " + quote(names.substitute(group->last_setting())) + "\n";
		}
		env += "userenv-fromenv";
		// Do not replicate systemd useless features unless required.
		if (!is_bool_true(systemduserenvironment, systemd_quirks))
			env += " --no-set-user --no-set-shell";
		if (per_user_mode && is_dbus)
			env += " --set-path --set-tools --set-term --set-timezone --set-locale --set-dbus --set-xdg --set-other";
		env += "\n";
	}
	if (environmentfile) {
		for (value::settings::const_iterator i(environmentfile->all_settings().begin()), e(environmentfile->all_settings().end()); e != i; ++i) {
			std::string val;
			const bool minus(strip_leading_minus(*i, val));
			env += "read-conf " + ((minus ? "--oknofile " : "") + quote(names.substitute(val))) + "\n";
		}
	}
	if (environmentdirectory) {
		const std::string opt(is_bool_true(fullenvironmentdirectory, false) ? "--full " : "");
		for (value::settings::const_iterator i(environmentdirectory->all_settings().begin()), e(environmentdirectory->all_settings().end()); e != i; ++i) {
			const std::string & val(*i);
			env += "envdir " + opt + quote(names.substitute(val)) + "\n";
		}
	}
	if (environment) {
		for (value::settings::const_iterator i(environment->all_settings().begin()), e(environment->all_settings().end()); e != i; ++i) {
			const std::string & datum(*i);
			const std::list<std::string> list(names.substitute(split_list(datum)));
			for (std::list<std::string>::const_iterator j(list.begin()), je(list.end()); je != j; ++j) {
				const std::string & s(*j);
				const std::string::size_type eq(s.find('='));
				const std::string var(s.substr(0, eq));
				const std::string val(eq == std::string::npos ? std::string() : s.substr(eq + 1, std::string::npos));
				env += "setenv " + quote(var) + " " + quote(val) + "\n";
			}
		}
	}
	if (unsetenvironment) {
		for (value::settings::const_iterator i(unsetenvironment->all_settings().begin()), e(unsetenvironment->all_settings().end()); e != i; ++i) {
			const std::string & datum(*i);
			const std::list<std::string> list(names.substitute(split_list(datum)));
			for (std::list<std::string>::const_iterator j(list.begin()), je(list.end()); je != j; ++j) {
				const std::string & var(*j);
				env += "unsetenv " + quote(var) + "\n";
			}
		}
	}
	if (environmentappendpath) {
		for (value::settings::const_iterator i(environmentappendpath->all_settings().begin()), e(environmentappendpath->all_settings().end()); e != i; ++i) {
			const std::string & datum(*i);
			const std::list<std::string> list(names.substitute(split_list(datum)));
			for (std::list<std::string>::const_iterator j(list.begin()), je(list.end()); je != j; ++j) {
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
	const bool stdout_append(standardoutput && "append:" == tolower(standardoutput->last_setting()).substr(0,7));
	const bool stdout_socket(is_string(standardoutput, "socket"));
	const bool stdout_tty(is_string(standardoutput, "tty", "tty-force"));
	const bool stdout_inherit(is_string(standardoutput, "inherit"));
	const bool stderr_file(standarderror && "file:" == tolower(standarderror->last_setting()).substr(0,5));
	const bool stderr_append(standarderror && "append:" == tolower(standarderror->last_setting()).substr(0,7));
	const bool stderr_socket(is_string(standarderror, "socket"));
	const bool stderr_tty(is_string(standarderror, "tty", "tty-force"));
	const bool stderr_inherit(is_string(standarderror, "inherit"));
	const bool stderr_log(is_string(standarderror, "log"));
	// We "un-use" anything that isn't "inherit"/"tty"/"socket".
	if (standardinput && (!stdin_file && !stdin_socket && !stdin_tty)) standardinput->used = false;
	if (standardoutput && (!stdout_file && !stdout_append && !stdout_inherit && !stdout_socket && !stdout_tty)) standardoutput->used = false;
	if (standarderror && (!stderr_file && !stderr_append && !stderr_inherit && !stderr_socket && !stderr_tty && !stderr_log)) standarderror->used = false;
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
		if (is_bool_true(ttyreset, false)) login_prompt += "vc-reset-tty --hard-reset\n";
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
	if (stdout_append) redirect += "fdredir --append 1 " + quote(names.substitute(standardoutput->last_setting().substr(5,std::string::npos))) + "\n";
	if (stderr_file) redirect += "fdredir --write 2 " + quote(names.substitute(standarderror->last_setting().substr(5,std::string::npos))) + "\n";
	if (stderr_append) redirect += "fdredir --append 2 " + quote(names.substitute(standarderror->last_setting().substr(5,std::string::npos))) + "\n";
	if (bannerfile)
		greeting_message += "login-banner " + quote(names.substitute(bannerfile->last_setting())) + "\n";
	if (bannerline)
		for (value::settings::const_iterator i(bannerline->all_settings().begin()), e(bannerline->all_settings().end()); e != i; ++i) {
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
	perilogue_setup_environment << env;
	if (setuidgidall) perilogue_setup_environment << envuidgid;
	perilogue_setup_environment << hardlimit;
	perilogue_setup_environment << softlimit;
	perilogue_setup_environment << um;
	if (chrootall) {
		perilogue_setup_environment << chroot;
#if defined(__LINUX__) || defined(__linux__)
		perilogue_setup_environment << mount_namespace_commands;
#endif
	}
	perilogue_setup_environment << chdir;
	perilogue_setup_environment << jvm;
	perilogue_setup_environment << redirect;

	std::stringstream perilogue_drop_privileges;
	if (setuidgidall) perilogue_drop_privileges << setuidgid;

	std::stringstream setup_environment;
	setup_environment << jail;
	setup_environment << move_to_control_group;
	setup_environment << priority;
	setup_environment << env;
	setup_environment << envuidgid;
	setup_environment << setsid;
	setup_environment << hardlimit;
	setup_environment << softlimit;
	setup_environment << um;
	setup_environment << chroot;
#if defined(__LINUX__) || defined(__linux__)
	setup_environment << mount_namespace_commands;
#endif
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
			for (value::settings::const_iterator i(execstartpre->all_settings().begin()), e(execstartpre->all_settings().end()); e != i; ++i ) {
				execute_command << "foreground "; 
				execute_command << names.substitute(shell_expand(strip_leading_minus(*i)));
				execute_command << " ;\n"; 
			}
		}
	}
	if (execstart) {
		for (value::settings::const_iterator i(execstart->all_settings().begin()), e(execstart->all_settings().end()); e != i; ) {
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
		start << multi_line_comment("Start file generated from " + source_filename_list);
	run << "#!/bin/nosh\n";
	if (generation_comment)
		run << multi_line_comment("Run file generated from " + source_filename_list);
	if (is_socket_activated) {
		if (socket_description) {
			for (value::settings::const_iterator i(socket_description->all_settings().begin()), e(socket_description->all_settings().end()); e != i; ++i) {
				const std::string & val(*i);
				run_or_start << multi_line_comment(names.substitute(val));
			}
		}
		if (filedescriptorname) {
			std::string socknames;
			for (value::settings::const_iterator i(filedescriptorname->all_settings().begin()), e(filedescriptorname->all_settings().end()); e != i; ++i) {
				if (filedescriptorname->all_settings().begin() != i)
					socknames += ":";
				socknames += names.substitute(*i);
			}
			run_or_start << "setenv LISTEN_FDNAMES " << quote(socknames) << "\n";
		}
		if (listenstream) {
			for (value::settings::const_iterator i(listenstream->all_settings().begin()), e(listenstream->all_settings().end()); e != i; ++i) {
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
			for (value::settings::const_iterator i(listendatagram->all_settings().begin()), e(listendatagram->all_settings().end()); e != i; ++i) {
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
			for (value::settings::const_iterator i(listensequentialpacket->all_settings().begin()), e(listensequentialpacket->all_settings().end()); e != i; ++i) {
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
			for (value::settings::const_iterator i(listenfifo->all_settings().begin()), e(listenfifo->all_settings().end()); e != i; ++i) {
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
			for (value::settings::const_iterator i(listennetlink->all_settings().begin()), e(listennetlink->all_settings().end()); e != i; ++i) {
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
			std::string prefix("af-unknown");
			bool local(false);
			if (listenstream) {
				for (value::settings::const_iterator i(listenstream->all_settings().begin()), e(listenstream->all_settings().end()); e != i; ++i) {
					const std::string sockname(names.substitute(*i));
					if (is_local_socket_name(sockname)) {
						prefix = "local-stream-";
						local = true;
					} else {
						prefix = "tcp-";
					}
				}
			}
			if (listensequentialpacket) {
				for (value::settings::const_iterator i(listensequentialpacket->all_settings().begin()), e(listensequentialpacket->all_settings().end()); e != i; ++i) {
					const std::string sockname(names.substitute(*i));
					if (is_local_socket_name(sockname)) {
						prefix = "local-seqpacket-";
						local = true;
					} else {
						prefix = "ip-";
					}
				}
			}
			if (listenstream || listensequentialpacket) {
				run_or_start << prefix << "socket-accept";
				if (maxconnections) run_or_start << " --connection-limit " << quote(maxconnections->last_setting());
				if (!local) {
					if (is_bool_true(keepalive, false)) run_or_start << " --keepalives";
					if (is_bool_true(nodelay, false)) run_or_start << " --no-delay";
				}
				run_or_start << "\n";
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
			for (value::settings::const_iterator i(service_description->all_settings().begin()), e(service_description->all_settings().end()); e != i; ++i) {
				const std::string & val(*i);
				service << multi_line_comment(names.substitute(val));
			}
		}
		service << socket_redirect;
		service << execute_command.str();
	} else
	if (is_timer_activated) {
		const char * flags(systemd_quirks ? "--systemd-compatibility --gnu-compatibility " : "");
		if (timer_description) {
			for (value::settings::const_iterator i(timer_description->all_settings().begin()), e(timer_description->all_settings().end()); e != i; ++i) {
				const std::string & val(*i);
				run_or_start << multi_line_comment(names.substitute(val));
			}
		}
		start << "foreground touch stamp-on-start ;\n";
		run_or_start << "unsetenv wake_time\n";
		if (onbootsec) {
			for (value::settings::const_iterator i(onbootsec->all_settings().begin()), e(onbootsec->all_settings().end()); e != i; ++i) {
				const std::string & v(*i);
				run_or_start <<
					"time-env-set since_boot boot\n"
					"time-env-add " << flags << "-- since_boot " << quote(v) << "\n"
					"time-env-unset-if-later since_boot now\n"
					"time-env-set-if-earlier wake_time $since_boot\n"
				;
			}
		}
		if (onstartupsec) {
			for (value::settings::const_iterator i(onstartupsec->all_settings().begin()), e(onstartupsec->all_settings().end()); e != i; ++i) {
				const std::string & v(*i);
				run_or_start <<
					"time-env-set since_startup startup\n"
					"time-env-add " << flags << "-- since_startup " << quote(v) << "\n"
					"time-env-unset-if-later since_startup now\n"
					"time-env-set-if-earlier wake_time $since_startup\n"
				;
			}
		}
		if (onactivesec) {
			for (value::settings::const_iterator i(onactivesec->all_settings().begin()), e(onactivesec->all_settings().end()); e != i; ++i) {
				const std::string & v(*i);
				run_or_start <<
					"time-env-set since_start >stamp-on-start\n"
					"time-env-add " << flags << "-- since_start " << quote(v) << "\n"
					"time-env-unset-if-later since_start now\n"
					"time-env-set-if-earlier wake_time $since_start\n"
				;
			}
		}
		if (onunitactivesec) {
			for (value::settings::const_iterator i(onunitactivesec->all_settings().begin()), e(onunitactivesec->all_settings().end()); e != i; ++i) {
				const std::string & v(*i);
				run_or_start <<
					"time-env-set since_service >stamp-on-service\n"
					"time-env-add " << flags << "-- since_service " << quote(v) << "\n"
					"time-env-unset-if-later since_service now\n"
					"time-env-set-if-earlier wake_time $since_service\n"
				;
			}
		}
		if (onunitinactivesec) {
			for (value::settings::const_iterator i(onunitinactivesec->all_settings().begin()), e(onunitinactivesec->all_settings().end()); e != i; ++i) {
				const std::string & v(*i);
				run_or_start <<
					"time-env-set since_restart >stamp-on-restart\n"
					"time-env-add " << flags << "-- since_restart " << quote(v) << "\n"
					"time-env-unset-if-later since_restart now\n"
					"time-env-set-if-earlier wake_time $since_restart\n"
				;
			}
		}
		if (oncalendar) {
			run_or_start <<
				"time-env-set calendar_time now\n"
				"time-env-next-matching calendar_time -- " << quote(oncalendar->last_setting()) << "\n"
				"time-env-set-if-earlier wake_time $calendar_time\n"
			;
		}
		run_or_start <<
			setup_environment.str() <<
			drop_privileges.str() <<
			"time-pause-until $wake_time\n"
			"foreground touch stamp-on-service ;\n"
			"./service\n"
		;

		std::ofstream service;
		open(prog, service, service_dirname + "/service");

		service << "#!/bin/nosh\n";
		if (generation_comment)
			service << multi_line_comment("Service file generated from " + service_filename);
		if (service_description) {
			for (value::settings::const_iterator i(service_description->all_settings().begin()), e(service_description->all_settings().end()); e != i; ++i) {
				const std::string & val(*i);
				service << multi_line_comment(names.substitute(val));
			}
		}
		service << execute_command.str();
	} else
	{
		if (service_description) {
			for (value::settings::const_iterator i(service_description->all_settings().begin()), e(service_description->all_settings().end()); e != i; ++i) {
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
				for (value::settings::const_iterator i(execstartpre->all_settings().begin()), e(execstartpre->all_settings().end()); e != i; ) {
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
	if (is_timer_activated) {
		restart_script << "touch stamp-on-restart\n";
	}
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
		for (value::settings::const_iterator i(execrestartpre->all_settings().begin()), e(execrestartpre->all_settings().end()); e != i; ) {
			std::list<std::string>::const_iterator j(i++);
			if (execrestartpre->all_settings().begin() != j) s << " ;\n"; 
			if (execrestartpre->all_settings().end() != i) s << "foreground "; 
			const std::string & val(*j);
			s << names.substitute(shell_expand(strip_leading_minus(val)));
		}
		restart_script << escape_newlines(escape_metacharacters(s.str(), false)) << "\n";
	}
	if (is_string(restart, "always", !systemd_quirks || is_timer_activated)) {
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
		stop << multi_line_comment("Stop file generated from " + source_filename_list);
	if (execstoppost || runtimedirectory) {
		stop << perilogue_setup_environment.str();
		stop << removerundir;
		stop << perilogue_drop_privileges.str();
		if (execstoppost) {
			for (value::settings::const_iterator i(execstoppost->all_settings().begin()), e(execstoppost->all_settings().end()); e != i; ) {
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
	const bool mounts_are_relative(!per_user_mode && etc_bundle);
	const bool read_only_bundle_fs(!per_user_mode && etc_bundle);

#define CREATE_LINKS(l,s) (l ? create_links (prog,envs,names.query_bundle_dirname(),per_user_mode,is_target,services_are_relative,targets_are_relative,bundle_dir_fd,names.substitute((l)->last_setting()),(s)) : static_cast<void>(0))

	CREATE_LINKS(socket_after, "after/");
	CREATE_LINKS(timer_after, "after/");
	CREATE_LINKS(service_after, "after/");
	CREATE_LINKS(socket_before, "before/");
	CREATE_LINKS(timer_before, "before/");
	CREATE_LINKS(service_before, "before/");
	CREATE_LINKS(socket_wants, "wants/");
	CREATE_LINKS(timer_wants, "wants/");
	CREATE_LINKS(service_wants, "wants/");
	CREATE_LINKS(socket_requires, "wants/");
	CREATE_LINKS(timer_requires, "wants/");
	CREATE_LINKS(service_requires, "wants/");
	CREATE_LINKS(socket_requisite, "needs/");
	CREATE_LINKS(timer_requisite, "needs/");
	CREATE_LINKS(service_requisite, "needs/");
	CREATE_LINKS(socket_stoppedby, "stopped-by/");
	CREATE_LINKS(timer_stoppedby, "stopped-by/");
	CREATE_LINKS(service_stoppedby, "stopped-by/");
	CREATE_LINKS(socket_conflicts, "conflicts/");
	CREATE_LINKS(timer_conflicts, "conflicts/");
	CREATE_LINKS(service_conflicts, "conflicts/");
	CREATE_LINKS(socket_wantedby, "wanted-by/");
	CREATE_LINKS(timer_wantedby, "wanted-by/");
	CREATE_LINKS(service_wantedby, "wanted-by/");
	CREATE_LINKS(socket_requiredby, "wanted-by/");
	CREATE_LINKS(timer_requiredby, "wanted-by/");
	CREATE_LINKS(service_requiredby, "wanted-by/");
	CREATE_LINKS(socket_partof, "requires/");
	CREATE_LINKS(timer_partof, "requires/");
	CREATE_LINKS(service_partof, "requires/");
	if (socket_wantsmountsfor)
		for (value::settings::const_iterator i(socket_wantsmountsfor->all_settings().begin()), e(socket_wantsmountsfor->all_settings().end()); e != i; ++i) {
			const std::string v(names.substitute(*i));
			create_mount_links (prog, names.query_bundle_dirname(), mounts_are_relative, true, bundle_dir_fd, v, "wants/");
		}
	if (timer_wantsmountsfor)
		for (value::settings::const_iterator i(timer_wantsmountsfor->all_settings().begin()), e(timer_wantsmountsfor->all_settings().end()); e != i; ++i) {
			const std::string v(names.substitute(*i));
			create_mount_links (prog, names.query_bundle_dirname(), mounts_are_relative, true, bundle_dir_fd, v, "wants/");
		}
	if (service_wantsmountsfor)
		for (value::settings::const_iterator i(service_wantsmountsfor->all_settings().begin()), e(service_wantsmountsfor->all_settings().end()); e != i; ++i) {
			const std::string v(names.substitute(*i));
			create_mount_links (prog, names.query_bundle_dirname(), mounts_are_relative, true, bundle_dir_fd, v, "wants/");
		}
	if (socket_aftermountsfor)
		for (value::settings::const_iterator i(socket_aftermountsfor->all_settings().begin()), e(socket_aftermountsfor->all_settings().end()); e != i; ++i) {
			const std::string v(names.substitute(*i));
			create_mount_links (prog, names.query_bundle_dirname(), mounts_are_relative, true, bundle_dir_fd, v, "after/");
		}
	if (timer_aftermountsfor)
		for (value::settings::const_iterator i(timer_aftermountsfor->all_settings().begin()), e(timer_aftermountsfor->all_settings().end()); e != i; ++i) {
			const std::string v(names.substitute(*i));
			create_mount_links (prog, names.query_bundle_dirname(), mounts_are_relative, true, bundle_dir_fd, v, "after/");
		}
	if (service_aftermountsfor)
		for (value::settings::const_iterator i(service_aftermountsfor->all_settings().begin()), e(service_aftermountsfor->all_settings().end()); e != i; ++i) {
			const std::string v(names.substitute(*i));
			create_mount_links (prog, names.query_bundle_dirname(), mounts_are_relative, true, bundle_dir_fd, v, "after/");
		}
	if (socket_requiresmountsfor)
		for (value::settings::const_iterator i(socket_requiresmountsfor->all_settings().begin()), e(socket_requiresmountsfor->all_settings().end()); e != i; ++i) {
			const std::string v(names.substitute(*i));
			create_mount_links (prog, names.query_bundle_dirname(), mounts_are_relative, true, bundle_dir_fd, v, "wants/");
			create_mount_links (prog, names.query_bundle_dirname(), mounts_are_relative, true, bundle_dir_fd, v, "after/");
		}
	if (timer_requiresmountsfor)
		for (value::settings::const_iterator i(timer_requiresmountsfor->all_settings().begin()), e(timer_requiresmountsfor->all_settings().end()); e != i; ++i) {
			const std::string v(names.substitute(*i));
			create_mount_links (prog, names.query_bundle_dirname(), mounts_are_relative, true, bundle_dir_fd, v, "wants/");
			create_mount_links (prog, names.query_bundle_dirname(), mounts_are_relative, true, bundle_dir_fd, v, "after/");
		}
	if (service_requiresmountsfor)
		for (value::settings::const_iterator i(service_requiresmountsfor->all_settings().begin()), e(service_requiresmountsfor->all_settings().end()); e != i; ++i) {
			const std::string v(names.substitute(*i));
			create_mount_links (prog, names.query_bundle_dirname(), mounts_are_relative, true, bundle_dir_fd, v, "wants/");
			create_mount_links (prog, names.query_bundle_dirname(), mounts_are_relative, true, bundle_dir_fd, v, "after/");
		}
	const bool defaultdependencies(
			is_socket_activated ? is_bool_true(socket_defaultdependencies, service_defaultdependencies, true) :
			is_timer_activated ? is_bool_true(timer_defaultdependencies, service_defaultdependencies, true) :
			is_bool_true(service_defaultdependencies, true)
	);
	const bool earlysupervise(
			is_socket_activated ? is_bool_true(socket_earlysupervise, service_earlysupervise, read_only_bundle_fs) :
			is_timer_activated ? is_bool_true(timer_earlysupervise, service_earlysupervise, read_only_bundle_fs) :
			is_bool_true(service_earlysupervise, read_only_bundle_fs)
	);
	if (defaultdependencies) {
		if (is_socket_activated)
			create_links(prog, envs, names.query_bundle_dirname(), per_user_mode, is_target, services_are_relative, targets_are_relative, bundle_dir_fd, "sockets.target", "wanted-by/");
		if (is_timer_activated)
			create_links(prog, envs, names.query_bundle_dirname(), per_user_mode, is_target, services_are_relative, targets_are_relative, bundle_dir_fd, "timers.target", "wanted-by/");
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
		for (value::settings::const_iterator i(listenstream->all_settings().begin()), e(listenstream->all_settings().end()); e != i; ++i) {
			const std::string sockname(names.substitute(*i));
			if (is_local_socket_name(sockname))
				make_mount_interdependencies(prog, names.query_bundle_dirname(), etc_bundle, true, bundle_dir_fd, sockname);
		}
	}
	if (listendatagram) {
		for (value::settings::const_iterator i(listendatagram->all_settings().begin()), e(listendatagram->all_settings().end()); e != i; ++i) {
			const std::string sockname(names.substitute(*i));
			if (is_local_socket_name(sockname))
				make_mount_interdependencies(prog, names.query_bundle_dirname(), etc_bundle, true, bundle_dir_fd, sockname);
		}
	}
	if (listensequentialpacket) {
		for (value::settings::const_iterator i(listensequentialpacket->all_settings().begin()), e(listensequentialpacket->all_settings().end()); e != i; ++i) {
			const std::string sockname(names.substitute(*i));
			if (is_local_socket_name(sockname))
				make_mount_interdependencies(prog, names.query_bundle_dirname(), etc_bundle, true, bundle_dir_fd, sockname);
		}
	}
	if (listenfifo) {
		for (value::settings::const_iterator i(listenfifo->all_settings().begin()), e(listenfifo->all_settings().end()); e != i; ++i) {
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
	report_unused(prog, timer_profile, timer_filename);
	report_unused(prog, service_profile, service_filename);

	throw EXIT_SUCCESS;
}
