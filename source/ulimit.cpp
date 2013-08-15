/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <climits>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "utils.h"
#include "popt.h"

/* Resource limit options ***************************************************
// **************************************************************************
*/

struct resource_limit_definition : public popt::compound_named_definition {
public:
	resource_limit_definition(char s, const char * l, const char * a, const char * d, int r, int sc) : compound_named_definition(s, l, a, d), resource(r), scale(sc), set(false) {}
	virtual void action(popt::processor &, const char *);
	virtual ~resource_limit_definition();
	void enact(bool hard, bool soft);
protected:
	int resource, scale;
	bool set;
	rlimit old;
	rlim_t val;
};
resource_limit_definition::~resource_limit_definition() {}
void resource_limit_definition::action(popt::processor &, const char * text)
{
	if (!set) {
		if (0 > getrlimit(resource, &old)) {
			const int error(errno);
			throw popt::error(long_name, std::strerror(error));
		}
	}
	if (0 == std::strcmp(text, "infinity")
	||  0 == std::strcmp(text, "unlimited")
	) {
		val = RLIM_INFINITY;
	} else 
	if (0 == std::strcmp(text, "hard")
	||  0 == std::strcmp(text, "=")
	) {
		val = old.rlim_max;
	} else {
		const char * o(text);
		val = std::strtoul(text, const_cast<char **>(&text), 0);
		if (text == o || *text)
			throw popt::error(o, "not a number");
		if (val > (ULONG_MAX / scale))
			throw popt::error(o, "rescaled number is too big");
		val *= scale;
	}
	set = true;
}
void resource_limit_definition::enact(bool hard, bool soft)
{
	if (!set) return;
	const rlimit now = { soft ? val : old.rlim_cur, hard ? val : old.rlim_max };
	if (0 > setrlimit(resource, &now)) {
		const int error(errno);
#if defined(__LINUX__) || defined(__linux__)
		if (EINTR == error && now.rlim_cur > now.rlim_max)
			throw popt::error(long_name, "Attempt to set soft limit higher than hard limit");
		else
#endif
			throw popt::error(long_name, std::strerror(error));
	}
}

struct memory_resource_limit_definition : public popt::compound_named_definition {
public:
	memory_resource_limit_definition(char s, const char * l, const char * a, const char * d, int sc) : compound_named_definition(s, l, a, d), scale(sc), set(false) {}
	virtual void action(popt::processor &, const char *);
	virtual ~memory_resource_limit_definition();
	void enact(bool hard, bool soft);
protected:
	int scale;
	bool set;
	rlim_t val;
};
memory_resource_limit_definition::~memory_resource_limit_definition() {}
void memory_resource_limit_definition::action(popt::processor &, const char * text)
{
	if (0 == std::strcmp(text, "infinity")
	||  0 == std::strcmp(text, "unlimited")
	) {
		val = RLIM_INFINITY;
	} else 
	{
		const char * o(text);
		val = std::strtoul(text, const_cast<char **>(&text), 0);
		if (text == o || *text)
			throw popt::error(o, "not a number");
		if (val > (ULONG_MAX / scale))
			throw popt::error(o, "rescaled number is too big");
		val *= scale;
	}
	set = true;
}
void memory_resource_limit_definition::enact(bool hard, bool soft)
{
	if (!set) return;
	static const int resources[] = { RLIMIT_AS, RLIMIT_DATA, RLIMIT_MEMLOCK, RLIMIT_STACK };
	for (size_t i(0); i < sizeof resources/sizeof *resources; ++i) {
		const int resource(resources[i]);
		rlimit old;
		if (0 > getrlimit(resource, &old)) {
			const int error(errno);
			throw popt::error(long_name, std::strerror(error));
		}
		rlimit now = { soft ? val : old.rlim_cur, hard ? val : old.rlim_max };
		if (now.rlim_cur > now.rlim_max) now.rlim_cur = now.rlim_max;
		if (0 > setrlimit(resource, &now)) {
			const int error(errno);
			throw popt::error(long_name, std::strerror(error));
		}
	}
}

/* Main functions ***********************************************************
// **************************************************************************
*/

void
ulimit ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	try {
		bool hard(false), soft(false);
		popt::bool_definition hard_option('H', "hard", "Set hard limits.", hard);
		popt::bool_definition soft_option('S', "soft", "Set soft limits.", soft);
		memory_resource_limit_definition memory_option('a', "memory", "KiB", "Limit memory.", 1024);
		resource_limit_definition coredump_option('c', "coresize", "blocks", "Limit the size of core dumps.", RLIMIT_CORE, 512);
		resource_limit_definition datasize_option('d', "data-segment", "KiB", "Limit the size of the data segment.", RLIMIT_DATA, 1024);
#if defined(RLIMIT_NICE)
		resource_limit_definition nice_option('e', "nice", "blocks", "Limit the nice value.", RLIMIT_NICE, 1);
#endif
		resource_limit_definition filesize_option('f', "filesize", "blocks", "Limit the size of files.", RLIMIT_FSIZE, 512);
#if defined(RLIMIT_SIGPENDING)
		resource_limit_definition pending_option('i', "pending-signals", "count", "Limit the count of pending signals.", RLIMIT_SIGPENDING, 1);
#endif
		resource_limit_definition locked_memory_option('l', "locked-memory", "KiB", "Limit locked memory.", RLIMIT_MEMLOCK, 1024);
		resource_limit_definition RSS_option('m', "RSS", "KiB", "Limit the resident set size.", RLIMIT_RSS, 1024);
		resource_limit_definition open_files_option('n', "open-files", "count", "Limit the count of open files.", RLIMIT_NOFILE, 1);
#if defined(RLIMIT_PIPE)
		resource_limit_definition pipesize_option('p', "pipe-size", "count", "Limit pipe buffer size.", RLIMIT_PIPE, 1);
#endif
#if defined(RLIMIT_MSGQUEUE)
		resource_limit_definition queuesize_option('q', "queue-size", "count", "Limit POSIX message queue size.", RLIMIT_MSGQUEUE, 1);
#endif
		resource_limit_definition stacksize_option('s', "stack-segment", "KiB", "Limit the size of the stack segment.", RLIMIT_STACK, 1024);
		resource_limit_definition CPU_option('t', "CPU", "seconds", "Limit CPU time.", RLIMIT_CPU, 1);
		resource_limit_definition processes_option('u', "processes", "count", "Limit the number of processes.", RLIMIT_NPROC, 1);
		resource_limit_definition address_space_option('v', "virtual-memory", "KiB", "Limit virtual memory.", RLIMIT_AS, 1024);
#if defined(RLIMIT_LOCKS)
		resource_limit_definition locks_option('x', "locks", "count", "Limit the count of locks.", RLIMIT_LOCKS, 1);
#endif
		popt::definition * memory_table[] = {
			&memory_option,
			&address_space_option,
			&datasize_option,
			&locked_memory_option,
			&stacksize_option,
		};
		popt::table_definition memory_table_option(sizeof memory_table/sizeof *memory_table, memory_table, "Memory options");
		popt::definition * file_size_table[] = {
#if defined(RLIMIT_PIPE)
			&pipesize_option,
#endif
			&filesize_option,
			&coredump_option
		};
		popt::table_definition file_size_option(sizeof file_size_table/sizeof *file_size_table, file_size_table, "File size options");
		popt::definition * efficiency_table[] = {
			&CPU_option,
			&RSS_option
		};
		popt::table_definition efficiency_option(sizeof efficiency_table/sizeof *efficiency_table, efficiency_table, "Efficiency options");
		popt::definition * miscellaneous_table[] = {
#if defined(RLIMIT_NICE)
			&nice_option,
#endif
#if defined(RLIMIT_SIGPENDING)
			&pending_option,
#endif
			&open_files_option,
#if defined(RLIMIT_MSGQUEUE)
			&queuesize_option,
#endif
			&processes_option,
#if defined(RLIMIT_LOCKS)
			&locks_option,
#endif
		};
		popt::table_definition miscellaneous_option(sizeof miscellaneous_table/sizeof *miscellaneous_table, miscellaneous_table, "Miscellaneous options");
		popt::definition * top_table[] = {
			&hard_option,
			&soft_option,
			&miscellaneous_option,
			&file_size_option,
			&efficiency_option,
			&memory_table_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "prog");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
		if (!hard) soft = true;
		memory_option.enact(hard, soft);
		coredump_option.enact(hard, soft);
		datasize_option.enact(hard, soft);
#if defined(RLIMIT_NICE)
		nice_option.enact(hard, soft);
#endif
		filesize_option.enact(hard, soft);
#if defined(RLIMIT_SIGPENDING)
		pending_option.enact(hard, soft);
#endif
		locked_memory_option.enact(hard, soft);
		RSS_option.enact(hard, soft);
		open_files_option.enact(hard, soft);
#if defined(RLIMIT_PIPE)
		pipesize_option.enact(hard, soft);
#endif
#if defined(RLIMIT_MSGQUEUE)
		queuesize_option.enact(hard, soft);
#endif
		stacksize_option.enact(hard, soft);
		CPU_option.enact(hard, soft);
		processes_option.enact(hard, soft);
		address_space_option.enact(hard, soft);
#if defined(RLIMIT_LOCKS)
		locks_option.enact(hard, soft);
#endif
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw EXIT_FAILURE;
	}
}

void
softlimit ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	try {
		bool hard(false), soft(true);
		memory_resource_limit_definition memory_option('m', "memory", "bytes", "Limit memory.", 1);
		resource_limit_definition address_space_option('a', "virtual-memory", "bytes", "Limit virtual memory.", RLIMIT_AS, 1);
		resource_limit_definition coredump_option('c', "coresize", "bytes", "Limit the size of core dumps.", RLIMIT_CORE, 1);
		resource_limit_definition datasize_option('d', "data-segment", "bytes", "Limit the size of the data segment.", RLIMIT_DATA, 1);
		resource_limit_definition filesize_option('f', "filesize", "bytes", "Limit the size of files.", RLIMIT_FSIZE, 1);
		resource_limit_definition locked_memory_option('l', "locked-memory", "bytes", "Limit locked memory.", RLIMIT_MEMLOCK, 1);
		resource_limit_definition open_files_option('o', "open-files", "count", "Limit the count of open files.", RLIMIT_NOFILE, 1);
		resource_limit_definition processes_option('p', "processes", "count", "Limit the number of processes.", RLIMIT_NPROC, 1);
		resource_limit_definition RSS_option('r', "RSS", "bytes", "Limit the resident set size.", RLIMIT_RSS, 1);
		resource_limit_definition stacksize_option('s', "stack-segment", "bytes", "Limit the size of the stack segment.", RLIMIT_STACK, 1);
		resource_limit_definition CPU_option('t', "CPU", "seconds", "Limit CPU time.", RLIMIT_CPU, 1);
		popt::definition * memory_table[] = {
			&memory_option,
			&address_space_option,
			&datasize_option,
			&locked_memory_option,
			&stacksize_option,
		};
		popt::table_definition memory_table_option(sizeof memory_table/sizeof *memory_table, memory_table, "Memory options");
		popt::definition * file_size_table[] = {
			&filesize_option,
			&coredump_option
		};
		popt::table_definition file_size_option(sizeof file_size_table/sizeof *file_size_table, file_size_table, "File size options");
		popt::definition * efficiency_table[] = {
			&CPU_option,
			&RSS_option
		};
		popt::table_definition efficiency_option(sizeof efficiency_table/sizeof *efficiency_table, efficiency_table, "Efficiency options");
		popt::definition * miscellaneous_table[] = {
			&open_files_option,
			&processes_option
		};
		popt::table_definition miscellaneous_option(sizeof miscellaneous_table/sizeof *miscellaneous_table, miscellaneous_table, "Miscellaneous options");
		popt::definition * top_table[] = {
			&miscellaneous_option,
			&file_size_option,
			&efficiency_option,
			&memory_table_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "prog");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
		if (!hard) soft = true;
		memory_option.enact(hard, soft);
		address_space_option.enact(hard, soft);
		coredump_option.enact(hard, soft);
		datasize_option.enact(hard, soft);
		filesize_option.enact(hard, soft);
		locked_memory_option.enact(hard, soft);
		open_files_option.enact(hard, soft);
		processes_option.enact(hard, soft);
		RSS_option.enact(hard, soft);
		stacksize_option.enact(hard, soft);
		CPU_option.enact(hard, soft);
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw EXIT_FAILURE;
	}
}
