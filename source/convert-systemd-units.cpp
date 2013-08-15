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
#include <dirent.h>
#include <unistd.h>
#include <pwd.h>
#include "utils.h"
#include "fdutils.h"
#include "common-manager.h"
#include "popt.h"

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
		for ( const char ** p(systemd_prefixes); p < systemd_prefixes + sizeof systemd_prefixes/sizeof *systemd_prefixes; ++p) {
			path = ((std::string(*p) + "systemd/") + (local_session_mode ? "user/" : "system/")) + name;
			FILE * f = std::fopen(path.c_str(), "r");
			if (f) return f;
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
bool
ends_in (
	const std::string & s,
	const std::string & p,
	std::string & r
) {
	const std::string::size_type pl(p.length());
	const std::string::size_type sl(s.length());
	if (sl < pl) return false;
	if (s.substr(sl - pl, pl) != p) return false;
	r = s.substr(0, sl - pl);
	return true;
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
is_section_heading (
	std::string line,	///< line, already left trimmed by the caller
	std::string & section	///< overwritten only if we successfully parse the heading
) {
	line = rtrim(line);
	if (line.length() < 2 || '[' != line[0] || ']' != line[line.length() - 1]) return false;
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
create_links (
	const char * prog,
	const std::string & names,
	const std::string & service_basename,
	const std::string & subdir
) {
	const std::list<std::string> list(split_list(names));
	for (std::list<std::string>::const_iterator i(list.begin()); list.end() != i; ++i) {
		const std::string name(*i);
		std::string base, link, target;
		if (ends_in(name, ".target", base)) {
			target = roots[1] + (bundle_prefixes[0] + base);
			link = service_basename + subdir + base;
		} else
		if (ends_in(name, ".service", base)) {
			target = "../../" + base;
			link = service_basename + subdir + base;
		} else 
		{
			target = "../../" + name;
			link = service_basename + subdir + name;
		}
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
}

static
void
create_links (
	const char * prog,
	value * list,
	const std::string & basename,
	const std::string & subdir
) {
	if (list) create_links(prog, list->datum, basename, subdir);
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
	try {
		popt::bool_definition user_option('u', "user", "Communicate with the per-user manager.", local_session_mode);
		popt::definition * main_table[] = {
			&user_option
		};
		popt::top_table_definition main_option(sizeof main_table/sizeof *main_table, main_table, "Main options", "");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw EXIT_USAGE;
	}

	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing argument(s).");
		throw EXIT_FAILURE;
	}
	const std::string basename(args.front());
	args.erase(args.begin());
	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Unrecognized argument(s).");
		throw EXIT_FAILURE;
	}

	std::string socket_filename;
	const std::string socket_basename(basename + ".socket");
	FILE * socket_file(find(socket_basename, socket_filename));
	if (!socket_file) {
		const int error(errno);
		if (ENOENT != error) {
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, socket_basename.c_str(), std::strerror(error));
			throw EXIT_FAILURE;
		}
	}
	if (socket_file) {
		if (!is_regular(prog, socket_filename, socket_file)) {
			std::fclose(socket_file);
			throw EXIT_FAILURE;
		}
	}

	profile socket_profile;
	const bool is_socket_activated(socket_file);
	if (socket_file) {
		load(socket_profile, socket_file);
		std::fclose(socket_file); socket_file = NULL;
	}

	value * accept(socket_profile.use("socket", "accept"));
	value * listenstream(socket_profile.use("socket", "listenstream"));
	value * bindipv6only(socket_profile.use("socket", "bindipv6only"));
	value * backlog(socket_profile.use("socket", "backlog"));
	value * maxconnections(socket_profile.use("socket", "maxconnections"));
	value * keepalive(socket_profile.use("socket", "keepalive"));
	value * socketmode(socket_profile.use("socket", "socketmode"));
	value * socket_before(socket_profile.use("unit", "before"));
	value * socket_after(socket_profile.use("unit", "after"));
	value * socket_conflicts(socket_profile.use("unit", "conflicts"));
	value * socket_wants(socket_profile.use("unit", "wants"));
	value * socket_requires(socket_profile.use("unit", "requires"));
	value * socket_requisite(socket_profile.use("unit", "requisite"));
	value * socket_description(socket_profile.use("unit", "description"));
	value * socket_wantedby(socket_profile.use("install", "wantedby"));
	value * socket_defaultdependencies(socket_profile.use("unit", "defaultdependencies"));
	const bool is_template(is_bool_true(accept, false));

	std::string service_filename;
	const std::string service_basename((basename + (is_template ? "@" : "")) + ".service");
	FILE * service_file(find(service_basename, service_filename));
	if (!service_file) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, service_basename.c_str(), std::strerror(error));
		throw EXIT_FAILURE;
	}
	if (!is_regular(prog, service_filename, service_file)) {
		std::fclose(service_file);
		throw EXIT_FAILURE;
	}

	profile service_profile;
	load(service_profile, service_file);

	std::fclose(service_file); service_file = NULL;

	value * type(service_profile.use("service", "type"));
	value * description(service_profile.use("unit", "description"));
	value * workingdirectory(service_profile.use("service", "workingdirectory"));
	value * rootdirectory(service_profile.use("service", "rootdirectory"));
	value * execstart(service_profile.use("service", "execstart"));
	value * execstartpre(service_profile.use("service", "execstartpre"));
	value * execstoppost(service_profile.use("service", "execstoppost"));
	value * limitnofile(service_profile.use("service", "limitnofile"));
	value * limitcpu(service_profile.use("service", "limitcpu"));
	value * limitcore(service_profile.use("service", "limitcore"));
	value * limitnproc(service_profile.use("service", "limitnproc"));
	value * limitfsize(service_profile.use("service", "limitfsize"));
	value * limitas(service_profile.use("service", "limitas"));
	value * killmode(service_profile.use("service", "killmode"));
	value * rootdirectorystartonly(service_profile.use("service", "rootdirectorystartonly"));
	value * permissionsstartonly(service_profile.use("service", "permissionsstartonly"));
	value * standardinput(service_profile.use("service", "standardinput"));
	value * standardoutput(service_profile.use("service", "standardoutput"));
	value * standarderror(service_profile.use("service", "standarderror"));
	value * user(service_profile.use("service", "user"));
	value * umask(service_profile.use("service", "umask"));
	value * environment(service_profile.use("service", "environment"));
	value * environmentfile(service_profile.use("service", "environmentfile"));
	value * ttypath(service_profile.use("service", "ttypath"));
	value * ttyreset(service_profile.use("service", "ttyreset"));
	value * remainafterexit(service_profile.use("service", "remainafterexit"));
	value * restart(service_profile.use("service", "restart"));
	value * restartsec(service_profile.use("service", "restartsec"));
//	value * ignoresigpipe(service_profile.use("service", "ignoresigpipe"));
	value * service_defaultdependencies(service_profile.use("unit", "defaultdependencies"));
	value * service_after(service_profile.use("unit", "after"));
	value * service_before(service_profile.use("unit", "before"));
	value * service_conflicts(service_profile.use("unit", "conflicts"));
	value * service_wants(service_profile.use("unit", "wants"));
	value * service_requires(service_profile.use("unit", "requires"));
	value * service_requisite(service_profile.use("unit", "requisite"));
	value * service_wantedby(service_profile.use("install", "wantedby"));

	// Actively prevent certain unsupported combinations.

	if (type && "simple" != tolower(type->datum) && "forking" != tolower(type->datum) && "oneshot" != tolower(type->datum)) {
		std::fprintf(stderr, "%s: FATAL: %s: %s: %s\n", prog, service_filename.c_str(), type->datum.c_str(), "Only simple services are supported.");
		throw EXIT_FAILURE;
	}
	if (!execstart) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, basename.c_str(), "Missing mandatory ExecStart entry.");
		throw EXIT_FAILURE;
	}
	if (is_socket_activated) {
		if (!listenstream) {
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, socket_filename.c_str(), "Missing mandatory ListenStream entry.");
			throw EXIT_FAILURE;
		}
	}
	// We silently set "process" as the default killmode, not "control-group".
	if (killmode && "process" != tolower(killmode->datum)) {
		std::fprintf(stderr, "%s: FATAL: %s: %s: %s\n", prog, service_filename.c_str(), killmode->datum.c_str(), "Unsupported service stop mechanism.");
		throw EXIT_FAILURE;
	}

	// Make the directories.

	const std::string service_dirname(basename + "/service");

	mkdir(basename.c_str(), 0755);
	mkdir(service_dirname.c_str(), 0755);
	mkdir((basename + "/after").c_str(), 0755);
	mkdir((basename + "/before").c_str(), 0755);
	mkdir((basename + "/wants").c_str(), 0755);
	mkdir((basename + "/conflicts").c_str(), 0755);
	mkdir((basename + "/wanted-by").c_str(), 0755);
	mkdir((basename + "/required-by").c_str(), 0755);
	mkdir((basename + "/stopped-by").c_str(), 0755);

	// Construct various common command strings.

	std::string chroot;
	if (rootdirectory) chroot += "chroot " + quote(rootdirectory->datum) + "\n";
	const bool chrootall(!is_bool_true(rootdirectorystartonly, false));
	std::string setuidgid;
	if (user) {
		setuidgid += "setuidgid " + quote(user->datum) + "\n";
		// This replicates a systemd bug.
		if (passwd * pw = getpwnam(user->datum.c_str())) {
			if (pw->pw_dir)
				setuidgid += "setenv HOME " + quote(pw->pw_dir) + "\n";
		}
	}
	const bool setuidgidall(!is_bool_true(permissionsstartonly, false));
	// systemd always runs services in / by default; daemontools runs them in the service directory.
	std::string chdir;
	chdir += "chdir " + (workingdirectory ? quote(workingdirectory->datum) : "/") + "\n";
	std::string softlimit;
	if (limitnofile||limitcpu||limitcore||limitnproc||limitfsize||limitas) {
		softlimit += "softlimit ";
		if (limitnofile) softlimit += "-o " + quote(limitnofile->datum);
		if (limitcpu) softlimit += "-t " + quote(limitcpu->datum);
		if (limitcore) softlimit += "-c " + quote(limitcore->datum);
		if (limitnproc) softlimit += "-p " + quote(limitnproc->datum);
		if (limitfsize) softlimit += "-f " + quote(limitfsize->datum);
		if (limitas) softlimit += "-a " + quote(limitas->datum);
		softlimit += "\n";
	}
	std::string env;
	if (environmentfile) env += "read-conf " + quote(environmentfile->datum) + "\n";
	if (environment) {
		const std::list<std::string> list(split_list(environment->datum));
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
	const bool sleeponeshot(!is_bool_true(remainafterexit, false));
	const bool oneshot(type && "oneshot" == tolower(type->datum));
	const bool stdin_socket(standardinput && "socket" == tolower(standardinput->datum));
	const bool stdout_inherit(standardoutput && "inherit" == tolower(standardoutput->datum));
	const bool stderr_inherit(standarderror && "inherit" == tolower(standarderror->datum));
	// We "un-use" anything that isn't "inherit"/"socket".
	if (standardinput && !stdin_socket) standardinput->used = false;
	if (standardoutput && !stdout_inherit) standardoutput->used = false;
	if (standarderror && !stderr_inherit) standarderror->used = false;
	std::string redirect;
	if (stdout_inherit) redirect += "fdmove -c 1 0\n";
	if (stderr_inherit) redirect += "fdmove -c 2 1\n";

	// Open the service script files.

	std::ofstream start, stop, restart_script, real_run, real_service;
	open(prog, start, service_dirname + "/start");
	open(prog, stop, service_dirname + "/stop");
	open(prog, restart_script, service_dirname + "/restart");
	open(prog, real_run, service_dirname + "/run");
	if (is_socket_activated)
		open(prog, real_service, service_dirname + "/service");
	std::ostream & run(oneshot ? start : real_run);
	std::ostream & service(is_socket_activated ? real_service : run);

	// Write the script files.

	if (!oneshot) {
		start << "#!/bin/nosh\n" << multi_line_comment("Start file generated from " + service_filename);
		start << redirect;
		start << softlimit;
		if (setuidgidall) start << setuidgid;
		start << chdir;
		start << env;
		start << um;
		if (chrootall) start << chroot;
		if (execstartpre) start << strip_leading_minus(execstartpre->datum) << "\n"; else start << "true\n";
	} else {
		real_run << "#!/bin/nosh\n" << multi_line_comment("Run file generated from " + service_filename);
		if (sleeponeshot)
			real_run << "sleep 86400\n";
		else
			real_run << "true\n";
	}

	if (is_socket_activated) {
		run << "#!/bin/nosh\n" << multi_line_comment("Run file generated from " + socket_filename);
		if (socket_description) run << multi_line_comment(socket_description->datum);
		if (is_local_socket_name(listenstream->datum)) {
			run << "local-socket-listen --systemd-compatibility ";
			if (backlog) run << "--backlog " << quote(backlog->datum) << " ";
			if (socketmode) run << "--socketmode " << quote(socketmode->datum) << " ";
			run << quote(listenstream->datum) << "\n";
			if (is_template) {
				run << "local-socket-accept ";
				if (maxconnections) run << "--connection-limit " << quote(maxconnections->datum) << " ";
				run << "\n";
			}
		} else {
			run << "tcp-socket-listen --systemd-compatibility ";
			if (backlog) run << "--backlog " << quote(backlog->datum) << " ";
			if (bindipv6only && "both" == tolower(bindipv6only->datum)) run << "--combineand6 ";
			run << "0 " << quote(listenstream->datum) << "\n";
			if (is_template) {
				run << "tcp-socket-accept ";
				if (maxconnections) run << "--connection-limit " << quote(maxconnections->datum) << " ";
				if (!is_bool_true(keepalive, false)) run << "--no-keepalives ";
				run << "\n";
			}
		}
		run << "./service\n";
		service << "#!/bin/nosh\n" << multi_line_comment("Service file generated from " + service_filename);
	} else
		run << "#!/bin/nosh\n" << multi_line_comment("Run file generated from " + service_filename);
	if (description) service << multi_line_comment(description->datum);
	if (is_socket_activated) {
		if (!is_template) {
			if (stdin_socket) service << "fdmove -c 0 3\n";
		}
	}
	service << redirect;
	service << softlimit;
	service << setuidgid;
	service << chdir;
	service << env;
	service << um;
	service << chroot;
	if (ttypath) service << "vc-get-tty " << quote(ttypath->datum) << "\n";
	if (is_bool_true(ttyreset, false)) service << "vc-reset-tty\n";
	service << strip_leading_minus(execstart->datum) << "\n";

	if (!restartsec && restart && "always" == tolower(restart->datum)) {
		restart_script << "#!/bin/nosh\n" << multi_line_comment("Restart file generated from " + service_filename) << "exec true\n";
	} else {
		restart_script << "#!/bin/sh\n" << multi_line_comment("Restart file generated from " + service_filename);
		if (restartsec) restart_script << "sleep " << restartsec->datum << "\n";
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
	stop << redirect;
	stop << softlimit;
	if (setuidgidall) stop << setuidgid;
	stop << chdir;
	stop << env;
	stop << um;
	if (chrootall) stop << chroot;
	if (execstoppost) stop << strip_leading_minus(execstoppost->datum) << "\n"; else stop << "true\n";

	// Set the dependency and installation information.

	create_links(prog, socket_after, basename, "/after/");
	create_links(prog, service_after, basename, "/after/");
	create_links(prog, socket_before, basename, "/before/");
	create_links(prog, service_before, basename, "/before/");
	create_links(prog, socket_wants, basename, "/wants/");
	create_links(prog, service_wants, basename, "/wants/");
	create_links(prog, socket_requires, basename, "/wants/");
	create_links(prog, service_requires, basename, "/wants/");
	create_links(prog, socket_requisite, basename, "/wants/");
	create_links(prog, service_requisite, basename, "/wants/");
	create_links(prog, socket_conflicts, basename, "/conflicts/");
	create_links(prog, service_conflicts, basename, "/conflicts/");
	create_links(prog, socket_wantedby, basename, "/wanted-by/");
	create_links(prog, service_wantedby, basename, "/wanted-by/");
	const bool defaultdependencies(
			(is_socket_activated && is_bool_true(socket_defaultdependencies, true)) || 
			is_bool_true(service_defaultdependencies, true)
	);
	if (defaultdependencies) {
		if (is_socket_activated)
			create_links(prog, "sockets.target", basename, "/wanted-by/");
		create_links(prog, "basic.target", basename, "/after/");
		create_links(prog, "basic.target", basename, "/wants/");
		create_links(prog, "shutdown.target", basename, "/before/");
		create_links(prog, "shutdown.target", basename, "/stopped-by/");
	}

	// Issue the final reports.

	report_unused(prog, socket_profile, socket_filename);
	report_unused(prog, service_profile, service_filename);

	throw EXIT_SUCCESS;
}
