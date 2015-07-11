/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define __STDC_FORMAT_MACROS
#include <vector>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <inttypes.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include "utils.h"
#include "fdutils.h"
#include "unpack.h"
#include "popt.h"
#include "service-manager-client.h"
#include "service-manager.h"

/* JSON and INI output ******************************************************
// **************************************************************************
*/

static
std::string
to_json_string (
	const std::string & s
) {
	std::string r;
	for (std::string::const_iterator p(s.begin()); s.end() != p; ++p) {
		const char c(*p);
		if ('\\' == c || '\"' == c)
			r += '\\';
		r += c;
	}
	return "\"" + r + "\"";
}

static
std::string
to_ini_string (
	const std::string & s,
	bool quoting
) {
	std::string r, q;
	for (std::string::const_iterator p(s.begin()); s.end() != p; ++p) {
		const char c(*p);
		if ('\\' == c || '\"' == c)
			r += '\\';
		else if ('\r' == c) {
			r += "\\r";
			continue;
		} else if ('\n' == c) {
			r += "\\n";
			continue;
		} else if ('\0' == c) {
			r += "\\0";
			continue;
		} else {
			if (quoting && std::isspace(c)) q = "\"";
		}
		r += c;
	}
	return q + r + q;
}

static bool json(false);
static char inner_comma(' ');
static char outer_comma(' ');

static
void
write_document_start()
{
	if (json) {
		std::fputs("{\n", stdout);
		outer_comma = ' ';
	}
}

static
void
write_document_end()
{
	if (json)
		std::fputs("}\n", stdout);
}

static
void
write_section_start(
	const char * name
) {
	if (json) {
		std::fprintf(stdout, "%c%s:{", outer_comma, to_json_string(name).c_str());
		outer_comma = ',';
		inner_comma = ' ';
	} else
		std::fprintf(stdout, "[%s]\n", name);
}

static
void
write_section_end()
{
	if (json)
		std::fputs("}\n", stdout);
	else
		std::fputc('\n', stdout);
}

static
void
write_boolean_value(
	const char * name,
	bool value
) {
	if (json) {
		std::fprintf(stdout, "%c%s:%s", inner_comma, to_json_string(name).c_str(), value ? "true": "false");
		inner_comma = ',';
	} else
		std::fprintf(stdout, "%s=%s\n", name, value ? "yes" : "no");
}

static
void
write_string_value(
	const char * name,
	const char * value
) {
	if (json) {
		std::fprintf(stdout, "%c%s:%s", inner_comma, to_json_string(name).c_str(), to_json_string(value).c_str());
		inner_comma = ',';
	} else
		std::fprintf(stdout, "%s=%s\n", name, to_ini_string(value, false).c_str());
}

static
void
write_numeric_int_value(
	const char * name,
	int value
) {
	if (json) {
		std::fprintf(stdout, "%c%s:%d", inner_comma, to_json_string(name).c_str(), value);
		inner_comma = ',';
	} else
		std::fprintf(stdout, "%s=%d\n", name, value);
}

static
void
write_numeric_uint64_value(
	const char * name,
	uint_least64_t value
) {
	if (json) {
		std::fprintf(stdout, "%c%s:%" PRIu64, inner_comma, to_json_string(name).c_str(), value);
		inner_comma = ',';
	} else
		std::fprintf(stdout, "%s=%" PRIu64 "\n", name, value);
}

static inline
const char *
state_of (
	char c
) {
	switch (c) {
		case encore_status_stopped:	return "stopped";
		case encore_status_starting:	return "starting";
		case encore_status_started:	return "started";
		case encore_status_running:	return "running";
		case encore_status_stopping:	return "stopping";
		case encore_status_failed:	return "failed";
		default:			return "unknown";
	}
}

typedef std::list<std::string> Relations;

static
Relations
get_relations (
	int bundle_dir_fd,
	const char * relation
) {
	Relations r;
	const int relation_dir_fd(open_dir_at(bundle_dir_fd, relation));
	if (0 <= relation_dir_fd) {
		DIR * relation_dir(fdopendir(relation_dir_fd));
		if (relation_dir) for (;;) {
			const dirent * entry(readdir(relation_dir));
			if (!entry) break;
#if defined(_DIRENT_HAVE_D_TYPE)
			if (DT_LNK != entry->d_type)
				continue;
#endif
#if defined(_DIRENT_HAVE_D_NAMLEN)
			if (1 > entry->d_namlen) continue;
#endif
			if ('.' == entry->d_name[0]) continue;

			struct stat s;
			if (0 > fstatat(relation_dir_fd, entry->d_name, &s, AT_SYMLINK_NOFOLLOW)) continue;
			if (!S_ISLNK(s.st_mode)) continue;
			std::auto_ptr<char> buf(new(std::nothrow) char [s.st_size]);
			if (!buf.get()) continue;
			const int l(readlinkat(relation_dir_fd, entry->d_name, buf.get(), s.st_size));
			if (0 > l) continue;
			std::string d(buf.get(), l);
			if ("../" == d.substr(0, 3))
				d = d.substr(3, d.npos);
			r.push_back(d);
		}
		closedir(relation_dir);
	}
	return r;
}

static
void
write_string_array(
	const char * name,
	const Relations & values
) {
	if (json) {
		std::fprintf(stdout, "%c%s:[", inner_comma, to_json_string(name).c_str());
		inner_comma = ',';
	} else
		std::fprintf(stdout, "%s=", name);
	std::string array_comma("");
	for (Relations::const_iterator i(values.begin()); values.end() != i; ++i) {
		const std::string & value(*i);
		if (json) {
			std::fprintf(stdout, "%s%s", array_comma.c_str(), to_json_string(value).c_str());
			array_comma = ",";
		} else {
			std::fprintf(stdout, "%s%s", array_comma.c_str(), to_ini_string(value, true).c_str());
			array_comma = " ";
		}
	}
	if (json)
		std::fprintf(stdout, "]");
	else
		std::fprintf(stdout, "\n");
}

static
std::string
get_log (
	int bundle_dir_fd
) {
	struct stat s;
	if (0 > fstatat(bundle_dir_fd, "log", &s, AT_SYMLINK_NOFOLLOW)) return std::string();
	if (!S_ISLNK(s.st_mode)) return std::string();
	std::auto_ptr<char> buf(new(std::nothrow) char [s.st_size]);
	if (!buf.get()) return std::string();
	const int l(readlinkat(bundle_dir_fd, "log", buf.get(), s.st_size));
	if (0 > l) return std::string();
	return std::string(buf.get(), l);
}

/* Main function ************************************************************
// **************************************************************************
*/

void
service_show (
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));

	try {
		popt::bool_definition json_option('\0', "json", "Output in JSON format.", json);
		popt::definition * top_table[] = {
			&json_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "directory");

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
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing directory name(s).");
		throw static_cast<int>(EXIT_USAGE);
	}

	write_document_start();
	for (std::vector<const char *>::const_iterator i(args.begin()); i != args.end(); ++i) {
		const char * name(*i);

		int bundle_dir_fd(open_dir_at(AT_FDCWD, name));
		if (0 > bundle_dir_fd) {
			const int error(errno);
			std::fprintf(stderr, "%s: %s\n", name, std::strerror(error));
			continue;
		}
		const Relations wants(get_relations(bundle_dir_fd, "wants"));
		const Relations before(get_relations(bundle_dir_fd, "before"));
		const Relations after(get_relations(bundle_dir_fd, "after"));
		const Relations conflicts(get_relations(bundle_dir_fd, "conflicts"));
		const Relations required_by(get_relations(bundle_dir_fd, "required-by"));
		const Relations wanted_by(get_relations(bundle_dir_fd, "wanted-by"));
		const Relations stopped_by(get_relations(bundle_dir_fd, "stopped-by"));
		const std::string log_service(get_log(bundle_dir_fd));

		int service_dir_fd(open_service_dir(bundle_dir_fd));
		int supervise_dir_fd(open_supervise_dir(bundle_dir_fd));
		close(bundle_dir_fd); bundle_dir_fd = -1;
		if (0 > supervise_dir_fd) {
			const int error(errno);
			std::fprintf(stderr, "%s: %s: %s\n", name, "supervise", std::strerror(error));
			close(service_dir_fd);
			continue;
		}
		if (0 > service_dir_fd) {
			const int error(errno);
			std::fprintf(stderr, "%s: %s: %s\n", name, "service", std::strerror(error));
			close(supervise_dir_fd);
			continue;
		}

		const bool initially_up(is_initially_up(service_dir_fd));
		const bool run_on_empty(!is_done_after_exit(service_dir_fd));
		close(service_dir_fd); service_dir_fd = -1;
		int ok_fd(open_writeexisting_at(supervise_dir_fd, "ok"));
		if (0 > ok_fd) {
			const int error(errno);
			close(supervise_dir_fd);
			if (ENXIO == error)
				std::fprintf(stderr, "%s: No supervisor is running\n", name);
			else
				std::fprintf(stderr, "%s: %s: %s\n", name, "supervise/ok", std::strerror(error));
			continue;
		}
		close(ok_fd), ok_fd = -1;
		int status_fd(open_read_at(supervise_dir_fd, "status"));
		close(supervise_dir_fd), supervise_dir_fd = -1;
		if (0 > status_fd) {
			const int error(errno);
			std::fprintf(stderr, "%s: %s: %s\n", name, "status", std::strerror(error));
			continue;
		}
		char status[20];
		const int b(read(status_fd, status, sizeof status));
		close(status_fd), status_fd = -1;
		write_section_start(name);
		if (b < 18)
			write_boolean_value("Loading", true);
		else {
			const uint64_t s(unpack_bigendian(status, 8));
//			const uint32_t n(unpack_bigendian(status + 8, 4));
			const uint32_t p(unpack_littleendian(status + 12, 4));

			if (b < 20) {
				// supervise doesn't turn off the want flag.
				if (p) {
					if ('u' == status[17]) status[17] = '\0';
				} else {
					if ('d' == status[17]) status[17] = '\0';
				}
			}
			write_string_value("DaemontoolsState", p ? "up" : "down");
			if (b >= 20)
				write_string_value("DaemontoolsEncoreState", state_of(status[18]));
			write_numeric_int_value("MainPID", p);
			write_numeric_uint64_value("Timestamp", s);
			bool leap;
			const uint64_t z(tai64_to_time(s, leap));
			write_numeric_uint64_value("UTCTimestamp", z);
			const char * const want('u' == status[17] ? "up" : 'O' == status[17] ? "once at most" : 'o' == status[17] ? "once" : 'd' == status[17] ? "down" : "nothing");
			write_string_value("Want", want);
			write_boolean_value("Paused", status[16]);
		}
		write_boolean_value("Preset", initially_up);
		write_boolean_value("RemainAfterExit", run_on_empty);
		write_string_array("Wants", wants);
		write_string_array("Before", before);
		write_string_array("After", after);
		write_string_array("Conflicts", conflicts);
		write_string_array("Required-By", required_by);
		write_string_array("Wanted-By", wanted_by);
		write_string_array("Stopped-By", stopped_by);
		write_string_value("LogService", log_service.c_str());
		write_section_end();
	}
	write_document_end();
	throw EXIT_SUCCESS;
}
