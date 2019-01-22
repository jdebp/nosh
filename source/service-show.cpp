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
#include "FileDescriptorOwner.h"
#include "DirStar.h"

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
	const std::string & name,
	int value
) {
	if (json) {
		std::fprintf(stdout, "%c%s:%d", inner_comma, to_json_string(name).c_str(), value);
		inner_comma = ',';
	} else
		std::fprintf(stdout, "%s=%d\n", name.c_str(), value);
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
	const std::string & name,
	uint_least64_t value
) {
	if (json) {
		std::fprintf(stdout, "%c%s:%" PRIu64, inner_comma, to_json_string(name).c_str(), value);
		inner_comma = ',';
	} else
		std::fprintf(stdout, "%s=%" PRIu64 "\n", name.c_str(), value);
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

static
const char * const
status_event[4] = {
	"Start",
	"Run",
	"Restart",
	"Stop",
};

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
	FileDescriptorOwner relation_dir_fd(open_dir_at(bundle_dir_fd, relation));
	if (0 <= relation_dir_fd.get()) {
		const DirStar relation_dir(relation_dir_fd);
		if (relation_dir) for (;;) {
			const dirent * entry(readdir(relation_dir));
			if (!entry) break;
#if defined(_DIRENT_HAVE_D_NAMLEN)
			if (1 > entry->d_namlen) continue;
#endif
			if ('.' == entry->d_name[0]) continue;
#if defined(_DIRENT_HAVE_D_TYPE)
			if (DT_LNK != entry->d_type)
				continue;
#endif

			struct stat s;
			if (0 > fstatat(relation_dir.fd(), entry->d_name, &s, AT_SYMLINK_NOFOLLOW)) continue;
			if (!S_ISLNK(s.st_mode)) continue;
			std::vector<char> buf(s.st_size, char());
			const int l(readlinkat(relation_dir.fd(), entry->d_name, buf.data(), s.st_size));
			if (0 > l) continue;
			std::string d(buf.data(), l);
			if ("../" == d.substr(0, 3))
				d = d.substr(3, d.npos);
			r.push_back(d);
		}
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
	std::vector<char> buf(s.st_size, char());
	const int l(readlinkat(bundle_dir_fd, "log", buf.data(), s.st_size));
	if (0 > l) return std::string();
	return std::string(buf.data(), l);
}

/* Main function ************************************************************
// **************************************************************************
*/

void
service_show [[gnu::noreturn]] (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));

	try {
		popt::bool_definition json_option('\0', "json", "Output in JSON format.", json);
		popt::definition * top_table[] = {
			&json_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{directory}");

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

		const FileDescriptorOwner bundle_dir_fd(open_dir_at(AT_FDCWD, name));
		if (0 > bundle_dir_fd.get()) {
			const int error(errno);
			std::fprintf(stderr, "%s: %s\n", name, std::strerror(error));
			continue;
		}
		const Relations wants(get_relations(bundle_dir_fd.get(), "wants"));
		const Relations expects(get_relations(bundle_dir_fd.get(), "expects"));
		const Relations before(get_relations(bundle_dir_fd.get(), "before"));
		const Relations after(get_relations(bundle_dir_fd.get(), "after"));
		const Relations conflicts(get_relations(bundle_dir_fd.get(), "conflicts"));
		const Relations requires(get_relations(bundle_dir_fd.get(), "requires"));
		const Relations required_by(get_relations(bundle_dir_fd.get(), "required-by"));
		const Relations wanted_by(get_relations(bundle_dir_fd.get(), "wanted-by"));
		const Relations stopped_by(get_relations(bundle_dir_fd.get(), "stopped-by"));
		const std::string log_service(get_log(bundle_dir_fd.get()));

		const FileDescriptorOwner supervise_dir_fd(open_supervise_dir(bundle_dir_fd.get()));
		if (0 > supervise_dir_fd.get()) {
			const int error(errno);
			std::fprintf(stderr, "%s: %s: %s\n", name, "supervise", std::strerror(error));
			continue;
		}
		const FileDescriptorOwner service_dir_fd(open_service_dir(bundle_dir_fd.get()));
		if (0 > service_dir_fd.get()) {
			const int error(errno);
			std::fprintf(stderr, "%s: %s: %s\n", name, "service", std::strerror(error));
			continue;
		}

		const bool initially_up(is_initially_up(service_dir_fd.get()));
		const bool run_on_empty(!is_done_after_exit(service_dir_fd.get()));
		const bool ready_after_run(is_ready_after_run(service_dir_fd.get()));
		const FileDescriptorOwner ok_fd(open_writeexisting_at(supervise_dir_fd.get(), "ok"));
		if (0 > ok_fd.get()) {
			const int error(errno);
			if (ENXIO == error)
				std::fprintf(stderr, "%s: No supervisor is running\n", name);
			else
				std::fprintf(stderr, "%s: %s: %s\n", name, "supervise/ok", std::strerror(error));
			continue;
		}
		const FileDescriptorOwner status_fd(open_read_at(supervise_dir_fd.get(), "status"));
		if (0 > status_fd.get()) {
			const int error(errno);
			std::fprintf(stderr, "%s: %s: %s\n", name, "status", std::strerror(error));
			continue;
		}
		char status[STATUS_BLOCK_SIZE];
		const ssize_t b(read(status_fd.get(), status, sizeof status));
		write_section_start(name);
		if (b < DAEMONTOOLS_STATUS_BLOCK_SIZE)
			write_boolean_value("Loading", true);
		else {
			const uint64_t s(unpack_bigendian(status, 8));
//			const uint32_t n(unpack_bigendian(status + 8, 4));
			const uint32_t p(unpack_littleendian(status + THIS_PID_OFFSET, 4));

			char & want_flag(status[WANT_FLAG_OFFSET]);
			if (b < ENCORE_STATUS_BLOCK_SIZE) {
				// supervise doesn't turn off the want flag.
				if (p) {
					if ('u' == want_flag) want_flag = '\0';
				} else {
					if ('d' == want_flag) want_flag = '\0';
				}
			}
			write_string_value("DaemontoolsState", p ? "up" : "down");
			if (b >= ENCORE_STATUS_BLOCK_SIZE)
				write_string_value("DaemontoolsEncoreState", state_of(status[ENCORE_STATUS_OFFSET]));
			write_numeric_int_value("MainPID", p);
			write_numeric_uint64_value("Timestamp", s);
			const TimeTAndLeap z(tai64_to_time(envs, s));
			write_numeric_uint64_value("UTCTimestamp", z.time);
			const char * const want('u' == want_flag ? "up" : 'O' == want_flag ? "once at most" : 'o' == want_flag ? "once" : 'd' == want_flag ? "down" : "nothing");
			write_string_value("Want", want);
			write_boolean_value("Paused", status[PAUSE_FLAG_OFFSET]);
			for (unsigned int j(0U); j < 4U; ++j) {
				if (b >= (EXIT_STATUSES_OFFSET + j * EXIT_STATUS_SIZE) + EXIT_STATUS_SIZE) {
					const uint8_t code(status[EXIT_STATUSES_OFFSET + j * EXIT_STATUS_SIZE]);
					const uint32_t number(unpack_bigendian(status + (EXIT_STATUSES_OFFSET + j * EXIT_STATUS_SIZE + 1U), 4));
					const uint64_t stamp(unpack_bigendian(status + (EXIT_STATUSES_OFFSET + j * EXIT_STATUS_SIZE + 5U), 8));
					write_numeric_int_value(status_event[j] + std::string("ExitStatusCode"), code);
					write_numeric_int_value(status_event[j] + std::string("ExitStatusNumber"), number);
					write_numeric_uint64_value(status_event[j] + std::string("Timestamp"), stamp);
					const TimeTAndLeap zulu(tai64_to_time(envs, stamp));
					write_numeric_uint64_value(status_event[j] + std::string("UTCTimestamp"), zulu.time);
				}
			}
		}
		write_boolean_value("Enabled", initially_up);
		write_boolean_value("RemainAfterExit", run_on_empty);
		write_boolean_value("ReadyAfterRun", ready_after_run);
		write_string_array("Wants", wants);
		write_string_array("Wanted-By", wanted_by);
		write_string_array("Before", before);
		write_string_array("After", after);
		write_string_array("Expects", expects);
		write_string_array("Requires", requires);
		write_string_array("Required-By", required_by);
		write_string_array("Conflicts", conflicts);
		write_string_array("Stopped-By", stopped_by);
		write_string_value("LogService", log_service.c_str());
		write_section_end();
	}
	write_document_end();
	throw EXIT_SUCCESS;
}
