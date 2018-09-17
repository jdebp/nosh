/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define __STDC_FORMAT_MACROS
#include <set>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <clocale>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <inttypes.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>
#include "utils.h"
//#include "tai64utils.h"
#include "ProcessEnvironment.h"
#include "popt.h"

/* Time handling and parsing ************************************************
// **************************************************************************
*/

namespace {
struct NullableTAI64N {
	NullableTAI64N() : v(false) {}
	void reset() { v = false; }
	void zero() { v = true; s = 0; n = 0; }
	void today(const ProcessEnvironment &);
	void now(const ProcessEnvironment &);
	void boot(const ProcessEnvironment &);
	bool is_null() const { return !v; }
	uint_fast64_t query_seconds() const { return s; }
	uint_fast32_t query_nanoseconds() const { return n; }
	void set_seconds(uint_fast64_t b) { if (!v) { n = 0; v = true; } s = b; }
	void set_nanoseconds(uint_fast32_t b) { if (!v) { s = 0; v = true; } n = b; }
	void set(const ProcessEnvironment &, const std::time_t &, uint_fast32_t nano = 0U);
	void set(const ProcessEnvironment &, const timespec &);
protected:
	bool v;
	uint_least64_t s;
	uint_least32_t n;
};
}

void
NullableTAI64N::today(
	const ProcessEnvironment & envs
) {
	timespec realtime;
	clock_gettime(CLOCK_REALTIME, &realtime);
	std::time_t t(realtime.tv_sec);
	struct tm tm;
	localtime_r(&t, &tm);
	tm.tm_hour = 0;
	tm.tm_min = 0;
	tm.tm_sec = 0;
	const std::time_t t0(std::mktime(&tm));
	s = time_to_tai64(envs, TimeTAndLeap(t0, false));
	n = 0;
	v = true;
}

inline
void
NullableTAI64N::set(
	const ProcessEnvironment & envs,
	const std::time_t & t,
	uint_fast32_t nano
) {
	s = time_to_tai64(envs, TimeTAndLeap(t, false));
	n = nano;
	v = true;
}

inline
void
NullableTAI64N::set(
	const ProcessEnvironment & envs,
	const timespec & t
) {
	set(envs, t.tv_sec, t.tv_nsec);
}

void
NullableTAI64N::now(
	const ProcessEnvironment & envs
) {
	timespec realtime;
	clock_gettime(CLOCK_REALTIME, &realtime);
	set(envs, realtime);
}

void
NullableTAI64N::boot(
	const ProcessEnvironment & envs
) {
	timespec realtime;
	clock_gettime(CLOCK_REALTIME, &realtime);
	timespec uptime;
#if defined(__LINUX__) || defined(__linux__)
	clock_gettime(CLOCK_BOOTTIME, &uptime);
#else
	clock_gettime(CLOCK_UPTIME, &uptime);
#endif
	realtime.tv_sec -= uptime.tv_sec;
	if (realtime.tv_nsec < uptime.tv_nsec) {
		// Borrow from the seconds column.
		--realtime.tv_sec;
		realtime.tv_nsec += 1000000000UL;
	}
	realtime.tv_nsec -= uptime.tv_nsec;
	set(envs, realtime);
}

namespace {

inline 
int 
x2d ( int c ) 
{
	if (std::isdigit(c)) return c - '0';
	if (std::isalpha(c)) {
		c = std::tolower(c);
		return c - 'a' + 10;
	}
	return EOF;
}

uint_fast64_t
convert (
	const char * buf,
	std::size_t len
) {
	uint_fast64_t r(0U);
	while (len) {
		--len;
		r <<= 4;
		r |= x2d(*buf++);
	}
	return r;
}

inline
void
evaluate_time_expression (
	const char * prog,
	const ProcessEnvironment & envs,
	const char * e,
	NullableTAI64N & r
) {
	std::set<const char *> seen;
	r.reset();
	for (;;) {
		if (!e) {
			r.reset();
			return;
		}
		seen.insert(e);
		if ('@' == *e) {
			const char * t(e + 1);
			const std::size_t l(std::strlen(t));
			if (16U != l && 24U != l) {
invalid_timestamp:
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "Invalid TAI64N timestamp", e);
				throw static_cast<int>(EXIT_PERMANENT_FAILURE);
			}
			for (std::size_t i(0U); i < l; ++i)
				if (!std::isxdigit(t[i]))
					goto invalid_timestamp;
			r.set_seconds(convert(t, 16U));
			if (l >= 24U) {
				r.set_nanoseconds(convert(t + 16U, 8U));
				if (r.query_nanoseconds() >= 1000000000UL) 
					goto invalid_timestamp;
			}
			return;
		} else
		if ('$' == *e) {
			const char * v(envs.query(e + 1));
			if (!v) {
				r.reset();
				return;
			}
			if (seen.end() != seen.find(v)) {
				std::fprintf(stderr, "%s: FATAL: %s: %s -> %s\n", prog, "Expresion evaluation loop", e, v);
				throw static_cast<int>(EXIT_PERMANENT_FAILURE);
			}
			e = v;
		} else
		if ('>' == *e) {
			if (!e[1]) {
				std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing filename");
				throw static_cast<int>(EXIT_PERMANENT_FAILURE);
			}
			struct stat b;
			if (0 > stat(e + 1, &b)) {
				const int error(errno);
				if (ENOENT != error) {
					std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e + 1, std::strerror(error));
					throw static_cast<int>(EXIT_PERMANENT_FAILURE);
				}
				r.reset();
				return;
			}
			r.set(envs, b.st_mtim);
			return;
		} else
		if ('<' == *e) {
			if (!e[1]) {
				std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing filename");
				throw static_cast<int>(EXIT_PERMANENT_FAILURE);
			}
			struct stat b;
			if (0 > stat(e + 1, &b)) {
				const int error(errno);
				if (ENOENT != error) {
					std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e + 1, std::strerror(error));
					throw static_cast<int>(EXIT_PERMANENT_FAILURE);
				}
				r.reset();
				return;
			}
			r.set(envs, b.st_atim);
			return;
		} else
		if ('0' == *e) {
#if defined(__LINUX__) || defined(__linux__) || defined(__OpenBSD__)
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "This operating system's API does not report creation times.", e);
			throw static_cast<int>(EXIT_PERMANENT_FAILURE);
#else
			if (!e[1]) {
				std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing filename");
				throw static_cast<int>(EXIT_PERMANENT_FAILURE);
			}
			struct stat b;
			if (0 > stat(e + 1, &b)) {
				const int error(errno);
				if (ENOENT != error) {
					std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e + 1, std::strerror(error));
					throw static_cast<int>(EXIT_PERMANENT_FAILURE);
				}
				r.reset();
				return;
			}
			r.set(envs, b.st_birthtim);
			return;
#endif
		} else
		if ('i' == *e) {
			struct tm tm = { 0 };
			tm.tm_isdst = -1;
			const char * end(strptime(e + 1, "%FT%T %z", &tm));
			if (!end || *end)
				end = strptime(e + 1, "%F %T %z", &tm);
			if (!end || *end) {
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "Bad ISO 8601 date and time stamp", e + 1);
				throw static_cast<int>(EXIT_PERMANENT_FAILURE);
			}
			r.set(envs, std::mktime(&tm), 0);
			return;
		} else
		if ('D' == *e) {
			const std::time_t now(std::time(0));
			struct tm tm;
			localtime_r(&now, &tm);
			const char * end(strptime(e + 1, "%F", &tm));
			if (!end || *end)
				end = strptime(e + 1, "%x", &tm);
			if (!end || *end)
				end = strptime(e + 1, "%Ex", &tm);
			if (!end || *end) {
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "Bad date stamp", e + 1);
				throw static_cast<int>(EXIT_PERMANENT_FAILURE);
			}
			r.set(envs, std::mktime(&tm), 0);
			return;
		} else
		if ('T' == *e) {
			const std::time_t now(std::time(0));
			struct tm tm;
			localtime_r(&now, &tm);
			const char * end(strptime(e + 1, "%T", &tm));
			if (!end || *end)
				end = strptime(e + 1, "%X", &tm);
			if (!end || *end)
				end = strptime(e + 1, "%EX", &tm);
			if (!end || *end) {
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "Bad time stamp", e + 1);
				throw static_cast<int>(EXIT_PERMANENT_FAILURE);
			}
			r.set(envs, std::mktime(&tm), 0);
			return;
		} else
		if (!std::strcmp("now", e)) {
			r.now(envs);
			return;
		} else
		if (!std::strcmp("today", e)) {
			r.today(envs);
			return;
		} else
		if (!std::strcmp("null", e)) {
			r.reset();
			return;
		} else
		if (!std::strcmp("zero", e)) {
			r.zero();
			return;
		} else
		if (!std::strcmp("boot", e)
		||  !std::strcmp("startup", e)
		) {
			r.boot(envs);
			return;
		} else
		{
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "Invalid expression", e);
			throw static_cast<int>(EXIT_PERMANENT_FAILURE);
		}
	}
}

inline
void
set (
	ProcessEnvironment & envs,
	const char * var,
	const NullableTAI64N & t
) {
	if (t.is_null())
		envs.unset(var);
	else
	{
		char val[64];
		std::snprintf(val, sizeof val, "@%016" PRIxFAST64 "%08" PRIxFAST32, t.query_seconds(), t.query_nanoseconds());
		envs.set(var, val);
	}
}

enum unit { NSEC = 1, MSEC, USEC, SEC, MIN, HRS, DAY, WKS, FTN, MON, YRS };

inline
int
get_unit(const char * & s)
{
	switch (*s) {
		default:
			return 0;
		case 'd':
			if ('a' != s[1] && 'y' != s[2])
				s += 1;
			else
			{
				if ('s' == s[3]) s += 4; else s += 3;
			}
			return DAY;
		case 'f':
			if ('o' == s[1] && 'r' == s[2] && 't' == s[3] && 'n' == s[4] && 'i' == s[5] && 'g' == s[6] && 'h' == s[7] && 't' == s[8]) {
				if ('s' == s[9]) s += 10; else s += 9;
			} else
				return 0;
			return FTN;
		case 'h':
			if ('o' == s[1] && 'u' == s[2] && 'r' == s[3]) {
				if ('s' == s[4]) s += 5; else s += 4;
			} else
			{
				if ('r' == s[1]) s += 2; else s += 1;
			}
			return HRS;
		case 'M':
			s += 1;
			return MON;
		case 'm':
			if ('s' == s[1]) {
				if ('e' == s[2] && 'c' == s[3]) s += 4; else s += 2;
				return MSEC;
			} else
			if ('i' == s[1]) {
				if ('n' != s[2]) return 0;
				if ('u' == s[3] && 't' == s[4] && 'e' == s[5]) {
					if ('s' == s[6]) s += 7; else s += 6;
				} else
				{
					if ('s' == s[3]) s += 4; else s += 3;
				}
				return MIN;
			} else
			if ('o' == s[1]) {
				if ('n' != s[2]) return 0;
				if ('t' == s[3] && 'h' == s[4]) {
					if ('s' == s[5]) s += 6; else s += 5;
				} else
				{
					if ('s' == s[3]) s += 4; else s += 3;
				}
				return MON;
			} else
			{
				s += 1;
				return MIN;
			}
		case 'n':
			if ('s' != s[1]) return 0;
			if ('e' == s[2] && 'c' == s[3]) s += 4; else s += 2;
			return NSEC;
		case 's':
			if ('e' == s[1] && 'c' == s[2]) {
				if ('o' == s[3] && 'n' == s[4] && 'd' == s[5]) {
					if ('s' == s[6]) s += 7; else s += 6;
				} else
					s += 3;
			} else
				s += 1;
			return SEC;
		case 'u':
			if ('s' != s[1]) return 0;
			if ('e' == s[2] && 'c' == s[3]) s += 4; else s += 2;
			return USEC;
		case 'w':
			if ('e' == s[1] && 'e' == s[2] && 'k' == s[3]) {
				if ('s' == s[4]) s += 5; else s += 4;
			} else
			{
				if ('k' == s[1]) s += 2; else s += 1;
			}
			return WKS;
		case 'y':
			if ('e' == s[1] && 'a' == s[2] && 'r' == s[3]) {
				if ('s' == s[4]) s += 5; else s += 4;
			} else
			{
				if ('r' == s[1]) s += 2; else s += 1;
			}
			return YRS;
		case '\xCE':	// U+003BC Greek Letter mu
			if ('\xBC' != s[1] && 's' != s[2]) return 0;
			if ('e' == s[3] && 'c' == s[4]) s += 5; else s += 3;
			return USEC;
	}
}

}

/* Commands *****************************************************************
// **************************************************************************
*/

void
time_print_tai64n [[gnu::noreturn]] ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	bool no_newline(false);
	try {
		popt::bool_definition no_newline_option('n', 0, "Do not print a newline.", no_newline);
		popt::definition * top_table[] = {
			&no_newline_option,
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{expression}");

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
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing expression.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * expr(args.front());
	args.erase(args.begin());
	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Unexpected argument.");
		throw static_cast<int>(EXIT_USAGE);
	}

	std::setlocale(LC_TIME, "");

	NullableTAI64N t;
	evaluate_time_expression(prog, envs, expr, t);
	if (t.is_null())
		std::fprintf(stdout, "null");
	else
		std::fprintf(stdout, "@%016" PRIxFAST64 "%08" PRIxFAST32 " ", t.query_seconds(), t.query_nanoseconds());
	if (!no_newline)
		std::fputc('\n', stdout);

	throw EXIT_SUCCESS;
}

void
time_env_set ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::definition * top_table[] = {
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{variable} {expression}");

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
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing variable name.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * var(args.front());
	args.erase(args.begin());
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing expression.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * expr(args.front());
	args.erase(args.begin());
	next_prog = arg0_of(args);

	std::setlocale(LC_TIME, "");

	NullableTAI64N t;
	evaluate_time_expression(prog, envs, expr, t);
	set(envs, var, t);
}

void
time_env_set_if_earlier ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::definition * top_table[] = {
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{variable} {expression}");

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
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing variable name.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * var(args.front());
	args.erase(args.begin());
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing expression.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * expr(args.front());
	args.erase(args.begin());
	next_prog = arg0_of(args);

	std::setlocale(LC_TIME, "");

	NullableTAI64N tr;
	evaluate_time_expression(prog, envs, expr, tr);
	if (tr.is_null()) return;

	NullableTAI64N tl;
	evaluate_time_expression(prog, envs, envs.query(var), tl);
	if (!tl.is_null()
	&&  ((tl.query_seconds() < tr.query_seconds()) || ((tl.query_seconds() == tr.query_seconds()) && (tl.query_nanoseconds() <= tr.query_nanoseconds())))
	) {
		return;
	}

	// right is non-null and earlier than left.
	set(envs, var, tr);
}

void
time_env_unset_if_later ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::definition * top_table[] = {
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{variable} {expression}");

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
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing variable name.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * var(args.front());
	args.erase(args.begin());
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing expression.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * expr(args.front());
	args.erase(args.begin());
	next_prog = arg0_of(args);

	std::setlocale(LC_TIME, "");

	NullableTAI64N tr;
	evaluate_time_expression(prog, envs, expr, tr);
	if (!tr.is_null()) {
		NullableTAI64N tl;
		evaluate_time_expression(prog, envs, envs.query(var), tl);
		if (tl.is_null()
		||  (tl.query_seconds() > tr.query_seconds())
		||  ((tl.query_seconds() == tr.query_seconds()) && (tl.query_nanoseconds() >= tr.query_nanoseconds()))
		) {
			return;
		}
	}

	// right is null or later than left.
	envs.unset(var);
}

void
time_env_add ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	bool systemd_bugs(false), gnu_bugs(false);
	try {
		popt::bool_definition systemd_bugs_option('\0', "systemd-compatibility", "Support systemd timer misfeatures.", systemd_bugs);
		popt::bool_definition gnu_bugs_option('\0', "gnu-compatibility", "Support GNU cron misfeatures.", gnu_bugs);
		popt::definition * top_table[] = {
			&systemd_bugs_option,
			&gnu_bugs_option,
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{variable} {expression}");

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
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing variable name.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * var(args.front());
	args.erase(args.begin());
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing expression.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * expr(args.front());
	args.erase(args.begin());
	next_prog = arg0_of(args);

	std::setlocale(LC_TIME, "");

	NullableTAI64N t;
	evaluate_time_expression(prog, envs, envs.query(var), t);
	if (t.is_null())
		envs.unset(var);
	else 
	if (systemd_bugs || gnu_bugs) {
		// Operate in terms of a broken down calendar time comprising a tm and nano.
		TimeTAndLeap z(tai64_to_time(envs, t.query_seconds()));
		struct tm tm;
		localtime_r(&z.time, &tm);
		uint_fast64_t nano(t.query_nanoseconds());
		if (z.leap)
			// Fold in the leap second in the "posix" TZ case.
			++tm.tm_sec;

		while (*expr) {
			while (std::isspace(*expr)) {
				++expr;
				continue;
			}
			const char *start(expr);
			unsigned long ul(std::strtoul(expr, const_cast<char **>(&expr), 0));
			if (start == expr) {
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "Invalid number", expr);
				throw static_cast<int>(EXIT_PERMANENT_FAILURE);
			}

			switch (get_unit(expr)) {
				default:
					std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "Invalid unit", expr);
					throw static_cast<int>(EXIT_PERMANENT_FAILURE);
				case NSEC:	nano += ul; break;
				case USEC:	nano += 1000UL * ul; break;
				case MSEC:	nano += 1000000UL * ul; break;
				case SEC:	tm.tm_sec += ul; break;
				case MIN:	if (systemd_bugs) tm.tm_sec +=       60UL * ul; else tm.tm_min += ul;		break;
				case HRS:	if (systemd_bugs) tm.tm_sec +=     3600UL * ul; else tm.tm_hour += ul;		break;
				case DAY:	if (systemd_bugs) tm.tm_sec +=    86400UL * ul; else tm.tm_mday += ul;		break;
				case WKS:	if (systemd_bugs) tm.tm_sec +=   604800UL * ul; else tm.tm_mday += 7UL * ul;	break;
				case FTN:	if (systemd_bugs) tm.tm_sec +=  1209600UL * ul; else tm.tm_mday += 14UL * ul;	break;
				case MON:	if (systemd_bugs) tm.tm_sec +=  2629800UL * ul; else tm.tm_mon += ul;		break;
				case YRS:	if (systemd_bugs) tm.tm_sec += 31557600UL * ul; else tm.tm_year += ul;		break;
			}
		}

		// Renormalize back to NullableTAI64N.
		if (nano >= 1000000000UL) {
			tm.tm_sec += nano / 1000000000UL;
			nano = nano % 1000000000UL;
		}
		z.time = std::mktime(&tm);
		z.leap = false;
		t.set_seconds(time_to_tai64(envs, z));
		t.set_nanoseconds(nano);
	} else
	{
		while (*expr) {
			while (std::isspace(*expr)) {
				++expr;
				continue;
			}
			const char *start(expr);
			unsigned long ul(std::strtoul(expr, const_cast<char **>(&expr), 0));
			if (start == expr) {
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "Invalid number", expr);
				throw static_cast<int>(EXIT_PERMANENT_FAILURE);
			}

			// Operate in terms of the original NullableTAI64N.
			uint_fast64_t nano(t.query_nanoseconds());

			switch (const int unit = get_unit(expr)) {
				case NSEC:	nano += ul; break;
				case USEC:	nano += 1000UL * ul; break;
				case MSEC:	nano += 1000000UL * ul; break;
				case SEC:	nano += 1000000000UL * ul; break;
				default:
				{
					// Operate in terms of a broken down calendar time comprising a tm and nano.
					TimeTAndLeap z(tai64_to_time(envs, t.query_seconds()));
					struct tm tm;
					localtime_r(&z.time, &tm);
					if (z.leap)
						// Fold in the leap second in the "posix" TZ case.
						++tm.tm_sec;

					switch (unit) {
						default:
							std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "Invalid unit", expr);
							throw static_cast<int>(EXIT_PERMANENT_FAILURE);
						case MIN:	tm.tm_min += ul; break;
						case HRS:	tm.tm_hour += ul; break;
						case DAY:	tm.tm_mday += ul; break;
						case WKS:	tm.tm_mday += 7UL * ul; break;
						case FTN:	tm.tm_mday += 14UL * ul; break;
						case MON:	tm.tm_mon += ul; break;
						case YRS:	tm.tm_year += ul; break;
					}

					// Renormalize back to (a possibly itself still denormal) NullableTAI64N.
					z.time = std::mktime(&tm);
					z.leap = false;
					t.set_seconds(time_to_tai64(envs, z));

					break;
				}
			}

			if (nano >= 1000000000UL) {
				t.set_seconds(t.query_seconds() + nano / 1000000000UL);
				nano = nano % 1000000000UL;
			}
			t.set_nanoseconds(nano);
		}
	}

	set(envs, var, t);
}

void
time_pause_until ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::definition * top_table[] = {
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{expression}");

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
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing expression.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * expr(args.front());
	args.erase(args.begin());
	next_prog = arg0_of(args);

	std::setlocale(LC_TIME, "");

	NullableTAI64N t;
	evaluate_time_expression(prog, envs, expr, t);
	if (t.is_null()) {
		// A null time sleeping forever meshes with how we convert systemd timer units with no effective activation times.
#if defined(__LINUX__) || defined(__linux__)
		sigset_t s;
		sigemptyset(&s);
		sigsuspend(&s);
#else
		sigpause(0);
#endif
	} else
	for (;;) {
		NullableTAI64N n;
		n.now(envs);
		if (n.query_seconds() > t.query_seconds()) break;	// The time has passed.
		uint_fast64_t s(t.query_seconds() - n.query_seconds());
		if (86400U <= s)
			sleep(86400U);	// Wake up daily to check for clock changes.
		else
		if (3600U <= s)
			sleep(3600U);	// Wake up hourly to check for clock changes.
		else
		if (600U <= s)
			sleep(600U);	// Wake up every 10 minutes to check for clock changes.
		else
		if (60U <= s)
			sleep(60U);	// Wake up once per minute to check for clock changes.
		else
		{
			timespec i;
			if (n.query_nanoseconds() > t.query_nanoseconds()) {
				if (0U == s) break;	// The time has passed.
				--s;			// Borrow from the seconds column.
				i.tv_nsec = 1000000000UL + t.query_nanoseconds() - n.query_nanoseconds();
			} else
				i.tv_nsec = t.query_nanoseconds() - n.query_nanoseconds();
			i.tv_sec = s;
			nanosleep(&i, 0);
		}
	}
}
