/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <iostream>
#include <cstddef>
#include <cstdlib>
#include <csignal>
#include <cstring>
#include <cerrno>
#include <new>
#include <memory>
#include <unistd.h>
#include <dirent.h>
#include <ttyent.h>
#include <fstab.h>
#include <fcntl.h>
#include "utils.h"
#include "fdutils.h"
#include "service-manager-client.h"
#include "popt.h"
#include "FileStar.h"
#include "FileDescriptorOwner.h"
#include "DirStar.h"
#include "terminal_database.h"
#include "home-dir.h"

/* Common Internals *********************************************************
// **************************************************************************
*/

static inline
bool
enable_disable ( 
	const char * prog,
	bool make,
	const std::string & path,
	const int bundle_dir_fd,
	const std::string & source,
	const std::string & target
) {
	const std::string source_dir_name(path + "/" + source);
	const std::string base(basename_of(path.c_str()));
	FileDescriptorOwner source_dir_fd(open_dir_at(bundle_dir_fd, (source + "/").c_str()));
	if (source_dir_fd.get() < 0) {
		const int error(errno);
		if (ENOENT == error) return true;
		std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, source_dir_name.c_str(), std::strerror(error));
		return false;
	}
	const DirStar source_dir(source_dir_fd);
	if (!source_dir) {
		const int error(errno);
		std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, source_dir_name.c_str(), std::strerror(error));
		return false;
	}
	bool failed(false);
	for (;;) {
		errno = 0;
		const dirent * entry(readdir(source_dir));
		if (!entry) {
			const int error(errno);
			if (error) {
				std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, source_dir_name.c_str(), std::strerror(error));
				failed = true;
			}
			break;
		}
#if defined(_DIRENT_HAVE_D_NAMLEN)
		if (1 > entry->d_namlen) continue;
#endif
		if ('.' == entry->d_name[0]) continue;
#if defined(_DIRENT_HAVE_D_TYPE)
		if (DT_DIR != entry->d_type && DT_LNK != entry->d_type) continue;
#endif

		const FileDescriptorOwner target_dir_fd(open_dir_at(source_dir.fd(), entry->d_name));
		if (0 > target_dir_fd.get()) {
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, source_dir_name.c_str(), entry->d_name, std::strerror(error));
			failed = true;
			continue;
		}
		if (make)
			mkdirat(target_dir_fd.get(), target.c_str(), 0755);
		if (0 > unlinkat(target_dir_fd.get(), (target + "/" + base).c_str(), 0)) {
			const int error(errno);
			if (ENOENT != error) {
				std::fprintf(stderr, "%s: ERROR: %s/%s/%s/%s: %s\n", prog, source_dir_name.c_str(), entry->d_name, target.c_str(), base.c_str(), std::strerror(error));
				failed = true;
			}
		}
		if (make) {
			if (0 > symlinkat(path.c_str(), target_dir_fd.get(), (target + "/" + base).c_str())) {
				const int error(errno);
				std::fprintf(stderr, "%s: ERROR: %s/%s/%s/%s: %s\n", prog, source_dir_name.c_str(), entry->d_name, target.c_str(), base.c_str(), std::strerror(error));
				failed = true;
			}
		}
	}
	return !failed;
}

static inline
bool
enable_disable ( 
	const char * prog,
	bool make,
	const std::string & arg,
	const std::string & path,
	const int bundle_dir_fd
) {
	bool failed(false);
	if (!enable_disable(prog, make, path, bundle_dir_fd, "wanted-by", "wants"))
		failed = true;
	if (!enable_disable(prog, make, path, bundle_dir_fd, "stopped-by", "conflicts"))
		failed = true;
	if (!enable_disable(prog, make, path, bundle_dir_fd, "stopped-by", "after"))
		failed = true;
	if (!enable_disable(prog, make, path, bundle_dir_fd, "requires", "required-by"))
		failed = true;
	const FileDescriptorOwner service_dir_fd(open_service_dir(bundle_dir_fd));
	if (make) {
		const int rc(unlinkat(service_dir_fd.get(), "down", 0));
		if (0 > rc && ENOENT != errno) {
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s: %s/%s: %s\n", prog, arg.c_str(), path.c_str(), "down", std::strerror(error));
			failed = true;
		}
	} else {
		FileDescriptorOwner fd(open_read_at(service_dir_fd.get(), "down"));
		if (0 > fd.get()) {
			if (ENOENT == errno)
				fd.reset(open_writecreate_at(service_dir_fd.get(), "down", 0400));
			if (0 > fd.get()) {
				const int error(errno);
				std::fprintf(stderr, "%s: ERROR: %s: %s/%s: %s\n", prog, arg.c_str(), path.c_str(), "down", std::strerror(error));
				failed = true;
			}
		} else
			fchmod(fd.get(), 0400);
	}
	return !failed;
}

/* Preset information *******************************************************
// **************************************************************************
*/

static inline
bool
checkyesno (
	const std::string & s
) {
	if ("0" == s) return false;
	if ("1" == s) return true;
	const std::string l(tolower(s));
	if ("no" == l) return false;
	if ("yes" == l) return true;
	if ("false" == l) return false;
	if ("true" == l) return true;
	if ("off" == l) return false;
	if ("on" == l) return true;
	return false;
}

static
const char *
default_rcconf_files[] = {
	"/etc/rc.conf.local",
	"/etc/rc.conf",
	"/etc/defaults/rc.conf",
};

static
const char * const *
rcconf_filev = default_rcconf_files;
static
std::size_t
rcconf_filec = sizeof default_rcconf_files/sizeof *default_rcconf_files;

static inline
bool
scan_rcconf_file (
	bool & wants,	///< always set to a value
	const char * prog,
	const std::string & wanted,
	const std::string & rcconf_file
) {
	FILE * f(std::fopen(rcconf_file.c_str(), "r"));
	if (!f) return false;
	std::vector<std::string> env_strings(read_file(prog, rcconf_file.c_str(), f));
	for (std::vector<std::string>::const_iterator j(env_strings.begin()); j != env_strings.end(); ++j) {
		const std::string & s(*j);
		const std::string::size_type p(s.find('='));
		const std::string var(s.substr(0, p));
		if (var == wanted) {
			const std::string val(p == std::string::npos ? std::string() : s.substr(p + 1, std::string::npos));
			wants = checkyesno(val);
			return true;
		}
	}
	return false;
}

static inline
bool	/// \returns setting \retval true explicit \retval false defaulted
query_rcconf_preset (
	bool & wants,	///< always set to a value
	const char * prog,
	const ProcessEnvironment & envs,
	const std::string & name
) {
	const std::string wanted(name + "_enable");
	if (per_user_mode) {
		const std::string h(effective_user_home_dir(envs));
		const std::string
		user_rcconf_files[2] = {
			h + "/.config/rc.conf",
		};
		for (const std::string * q(user_rcconf_files); q < user_rcconf_files + sizeof user_rcconf_files/sizeof *user_rcconf_files; ++q)
			if (scan_rcconf_file(wants, prog, wanted, *q))
				return true;
	} else {
		for (size_t i(0); i < rcconf_filec; ++i) {
			const std::string rcconf_file(rcconf_filev[i]);
			if (scan_rcconf_file(wants, prog, wanted, rcconf_file))
				return true;
		}
	}
	wants = false;
	return false;
}

static inline
bool
initial_space (
	const std::string & s
) {
	return s.length() > 0 && std::isspace(s[0]);
}

static inline
bool
wildmat (
	std::string::const_iterator p,
	const std::string::const_iterator & pe,
	std::string::const_iterator n,
	const std::string::const_iterator & ne
) {
	for (;;) {
		if (p == pe && n == ne) return true;
		if (p == pe || n == ne) return false;
		switch (*p) {
			case '*':
				do { ++p; } while (p != pe && '*' == *p);
				if (p == pe)
					return true;
				// We are now guaranteed that there's a non-zero-length pattern following the asterisk.
				for (std::string::const_iterator b(ne); b != n; --b) {
					// Optimize away the recursion in the case where the first remaining pattern character does not match the first attempted name character.
					if (b != ne && '\\' != *p && '?' != *p && '[' != *p && *b != *p)
						continue;
					if (wildmat(p, pe, b, ne)) 
						return true;
				}
				return false;
			case '\\':
				++p;
				if (p == pe) return false;
				// Fall through to:
				[[clang::fallthrough]];
			default:
				if (*p != *n) return false;
				// Fall through to:
				[[clang::fallthrough]];
			case '?':
				++p;
				++n;
				break;
			case '[':
			{
				++p;
				const bool invert(p != pe && '^' == *p);
				if (invert) ++p;
				bool found(false);
				if (p != pe && (']' == *p || '-' == *p)) {
					if (*p == *n)
						found = true;
					++p;
				}
				const std::string::const_iterator re(find(p, pe, ']'));
				if (re == pe) return false;	// Malformed pattern matches nothing.
				while (p != re) {
					const char first(*p++);
					if (p != re && '-' == *p) {
						// Multiple element range from first to last, inclusive
						++p;
						if (re == p) return false;	// Malformed pattern matches nothing.
						const char last(*p++);
						if (first <= *n && *n <= last)
							found = true;
					} else {
						// Range of 1 element
						if (first == *n)
							found = true;
					}
				}
				if (invert == found) return false;
				++n;
				break;
			}
		}
	}
}

static inline
bool
wildmat (
	const std::string & pattern,
	const std::string & name
) {
	return wildmat(pattern.begin(), pattern.end(), name.begin(), name.end());
}

static inline
bool
matches (
	const std::string & pattern,
	const std::string & name,
	const std::string & suffix
) {
	std::string base;
	if (suffix.empty()) {
		if (ends_in(pattern, ".target", base))
			return wildmat(base, name);
		else
		if (ends_in(pattern, ".service", base))
			return wildmat(base, name);
		else
		if (ends_in(pattern, ".socket", base))
			return wildmat(base, name);
		else
		if (ends_in(pattern, ".timer", base))
			return wildmat(base, name);
		else
			return wildmat(pattern, name);
	} else {
		if (ends_in(pattern, suffix, base))
			return wildmat(base, name);
		else
			return wildmat(pattern, name);
	}
}

static
bool
scan (
	const std::string & name,
	const std::string & suffix,
	FILE * file,
	bool & wants_enable
) {
	for (std::string line; read_line(file, line); ) {
		line = ltrim(line);
		if (line.length() < 1) continue;
		if ('#' == line[0] || ';' == line[0]) continue;
		std::string remainder;
		if (begins_with(line, "enable", remainder) && initial_space(remainder)) {
			remainder = rtrim(ltrim(remainder));
			if (matches(remainder, name, suffix)) {
				wants_enable = true;
				return true;
			}
		} else
		if (begins_with(line, "disable", remainder) && initial_space(remainder)) {
			remainder = rtrim(ltrim(remainder));
			if (matches(remainder, name, suffix)) {
				wants_enable = false;
				return true;
			}
		}
	}
	return false;
}

static inline
void
scan_preset_files (
	std::string & earliest,
	const std::string & preset_dir_name,
	const std::string & name,
	const std::string & suffix,
	bool & wants_enable
) {
	FileDescriptorOwner preset_dir_fd(open_dir_at(AT_FDCWD, preset_dir_name.c_str()));
	if (preset_dir_fd.get() < 0) return;
	const DirStar preset_dir(preset_dir_fd);
	if (!preset_dir) return;
	for (;;) {
		const dirent * entry(readdir(preset_dir));
		if (!entry) break;
#if defined(_DIRENT_HAVE_D_NAMLEN)
		if (1 > entry->d_namlen) continue;
#endif
		if ('.' == entry->d_name[0]) continue;
#if defined(_DIRENT_HAVE_D_TYPE)
		if (DT_REG != entry->d_type && DT_LNK != entry->d_type) continue;
#endif
		const std::string d_name(entry->d_name);
		if (!earliest.empty() && earliest <= d_name) continue;
		const std::string p(preset_dir_name + d_name);
		const int f(open_read_at(preset_dir.fd(), entry->d_name));
		if (0 > f) continue;
		FileStar preset_file(fdopen(f, "rt"));
		if (!preset_file) continue;
		if (scan(name, suffix, preset_file, wants_enable))
			earliest = d_name;
	}
}

static
const char *
preset_directories[] = {
	"/usr/local/etc/system-control/presets/",
	"/etc/system-control/presets/",
	"/etc/systemd/system-preset/",
	"/usr/local/share/system-control/presets/",
	"/usr/local/lib/systemd/system-preset/",
	"/usr/share/system-control/presets/",
	"/usr/lib/systemd/system-preset/",
	"/lib/systemd/system-preset/",
};

static inline
bool	/// \returns setting \retval true explicit \retval false defaulted
query_systemd_preset (
	bool & wants,	///< always set to a value
	const ProcessEnvironment & envs,
	const std::string & name,
	const std::string & suffix
) {
	wants = true;
	std::string earliest;
	if (per_user_mode) {
		const std::string h(effective_user_home_dir(envs));
		const std::string
		user_preset_directories[9] = {
			"/usr/local/etc/system-control/user-presets/",
			"/etc/system-control/user-presets/",
			"/etc/systemd/user-preset/",
			h + "/.config/system-control/presets/",
			"/usr/local/share/system-control/user-presets/",
			"/usr/local/lib/systemd/user-preset/",
			"/usr/share/system-control/user-presets/",
			"/usr/lib/systemd/user-preset/",
			"/lib/systemd/user-preset/",
		};
		for (const std::string * q(user_preset_directories); q < user_preset_directories + sizeof user_preset_directories/sizeof *user_preset_directories; ++q)
			scan_preset_files (earliest, *q, name, suffix, wants);
	} else {
		for (size_t i(0); i < sizeof preset_directories/sizeof *preset_directories; ++i)
			scan_preset_files (earliest, preset_directories[i], name, suffix, wants);
	}
	return !earliest.empty();
}

static inline
bool	/// \returns setting \retval true explicit \retval false defaulted
query_ttys_preset (
	bool & wants,	///< always set to a value
	const std::string & name
) {
	if (!setttyent()) {
		wants = false;
		return false;
	}
	const struct ttyent *entry(getttynam(name.c_str()));
	wants = entry && is_on(*entry);
	endttyent();
	return entry != 0;
}

static inline
std::string 
unescape ( 
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
				for (unsigned n(0U);n < 2U; ++n) {
					if (e == q) break;
					c = *q;
					if (!std::isxdigit(c)) break;
					++q;
					const unsigned char d(std::isdigit(c) ? (c - '0') : (std::tolower(c) - 'a' + 10));
					v = (v << 4) | d;
				}
				r += char(v);
			}
		}
	}
	return r;
}

static inline
bool
is_auto (
	const struct fstab & entry
) {
	const std::list<std::string> options(split_fstab_options(entry.fs_mntops));
	return !has_option(options, "noauto");
}

static inline
bool	/// \returns setting \retval true explicit \retval false defaulted
query_fstab_preset (
	bool & wants,	///< always set to a value
	const std::string & escaped_name
) {
	if (!setfsent()) {
		wants = true;
		return false;
	}
	const std::string name(unescape(escaped_name));
	const struct fstab *entry(getfsfile(name.c_str()));
	if (!entry) entry = getfsspec(name.c_str());
	wants = entry && is_auto(*entry);
	endfsent();
	return entry != 0;
}

static inline
bool
determine_preset (
	const char * prog,
	const ProcessEnvironment & envs,
	bool system,
	bool rcconf,
	bool ttys,
	bool fstab,
	const std::string & prefix,
	const std::string & name,
	const std::string & suffix
) {
	bool wants(false);
	// systemd (and system-manager) settings take precedence over compatibility ones.
	if (system && query_systemd_preset(wants, envs, prefix + name, suffix))
		return wants;
	// The newer BSD rc.conf takes precedence over the older Sixth Edition ttys .
	if (rcconf && query_rcconf_preset(wants, prog, envs, name))
		return wants;
	if (ttys && query_ttys_preset(wants, name))
		return wants;
	if (fstab && query_fstab_preset(wants, name))
		return wants;
	return wants;
}

/* System control subcommands ***********************************************
// **************************************************************************
*/

void
enable [[gnu::noreturn]] ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	try {
		bool quiet(false);
		popt::bool_definition user_option('u', "user", "Communicate with the per-user manager.", per_user_mode);
		popt::bool_definition quiet_option('q', "quiet", "Compatibility option; ignored.", quiet);
		popt::definition * main_table[] = {
			&user_option,
			&quiet_option,
		};
		popt::top_table_definition main_option(sizeof main_table/sizeof *main_table, main_table, "Main options", "{service(s)...}");

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

	bool failed(false);
	for (std::vector<const char *>::const_iterator i(args.begin()); args.end() != i; ++i) {
		std::string path, name, suffix;
		const FileDescriptorOwner bundle_dir_fd(open_bundle_directory(envs, "", *i, path, name, suffix));
		if (0 > bundle_dir_fd.get()) {
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, *i, std::strerror(error));
			continue;
		}
		const std::string p(path + name);
		if (!enable_disable(prog, true, *i, p, bundle_dir_fd.get()))
			failed = true;
	}

	throw failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

void
disable [[gnu::noreturn]] ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	try {
		bool quiet(false);
		popt::bool_definition user_option('u', "user", "Communicate with the per-user manager.", per_user_mode);
		popt::bool_definition quiet_option('q', "quiet", "Compatibility option; ignored.", quiet);
		popt::definition * main_table[] = {
			&user_option,
			&quiet_option,
		};
		popt::top_table_definition main_option(sizeof main_table/sizeof *main_table, main_table, "Main options", "{service(s)...}");

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

	bool failed(false);
	for (std::vector<const char *>::const_iterator i(args.begin()); args.end() != i; ++i) {
		std::string path, name, suffix;
		const FileDescriptorOwner bundle_dir_fd(open_bundle_directory(envs, "", *i, path, name, suffix));
		if (0 > bundle_dir_fd.get()) {
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, *i, std::strerror(error));
			continue;
		}
		const std::string p(path + name);
		if (!enable_disable(prog, false, *i, p, bundle_dir_fd.get()))
			failed = true;
	}

	throw failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

void
preset [[gnu::noreturn]] ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	const char * prefix("");
	const char * rcconf(0);
	bool no_rcconf(false), no_system(false), ttys(false), fstab(false), dry_run(false);
	try {
		popt::bool_definition user_option('u', "user", "Communicate with the per-user manager.", per_user_mode);
		popt::bool_definition no_system_option('\0', "no-systemd", "Do not process system-manager/systemd preset files.", no_system);
		popt::bool_definition no_rcconf_option('\0', "no-rcconf", "Do not process /etc/rc.conf presets.", no_rcconf);
		popt::bool_definition ttys_option('\0', "ttys", "Process /etc/ttys presets.", ttys);
		popt::bool_definition fstab_option('\0', "fstab", "Process /etc/fstab presets.", fstab);
		popt::bool_definition dry_run_option('n', "dry-run", "Don't actually enact the enable/disable.", dry_run);
		popt::string_definition prefix_option('p', "prefix", "string", "Prefix each name with this (template) name.", prefix);
		popt::string_definition rcconf_file_option('\0', "rcconf-file", "filename", "Use this file as rc.conf.", rcconf);
		popt::definition * main_table[] = {
			&user_option,
			&no_system_option,
			&no_rcconf_option,
			&rcconf_file_option,
			&ttys_option,
			&fstab_option,
			&dry_run_option,
			&prefix_option
		};
		popt::top_table_definition main_option(sizeof main_table/sizeof *main_table, main_table, "Main options", "{service(s)...}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
		if (rcconf) {
			rcconf_filev = &rcconf;
			rcconf_filec = 1U;
		}
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw static_cast<int>(EXIT_USAGE);
	}

	bool failed(false);
	const std::string p(prefix);
	for (std::vector<const char *>::const_iterator i(args.begin()); args.end() != i; ++i) {
		std::string path, name, suffix;
		const FileDescriptorOwner bundle_dir_fd(open_bundle_directory(envs, prefix, *i, path, name, suffix));
		if (0 > bundle_dir_fd.get()) {
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s%s%s: %s\n", prog, path.c_str(), prefix, name.c_str(), std::strerror(error));
			failed = true;
			continue;
		}
		const bool make(determine_preset(prog, envs, !no_system, !no_rcconf, ttys, fstab, p, name, suffix));
		if (dry_run)
			std::fprintf(stdout, "%s %s\n", make ? "enable" : "disable", (path + p + name).c_str());
		else
		if (!enable_disable(prog, make, p + *i, path + p + name, bundle_dir_fd.get()))
			failed = true;
	}

	throw failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
