/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define __STDC_FORMAT_MACROS
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <cerrno>
#include <ctime>
#include <inttypes.h>
#include <sys/stat.h>
#include "kqueue_common.h"
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include "utils.h"
#include "fdutils.h"
#include "popt.h"
#include "SignalManagement.h"

static uint64_t margin(512);
static uint64_t max_file_size (0x00ffffffULL);	// 16MiB.  Any larger and we start giving tools like "tail" fits.
static uint64_t max_total_size(0x3fffffffULL);	// 1GiB

/* Utilities ****************************************************************
// **************************************************************************
*/

static inline
bool
is_current (
	const dirent & e
) {
#if defined(_DIRENT_HAVE_D_NAMLEN)
	return (sizeof "current" - 1) == e.d_namlen && 0 == memcmp(e.d_name, "current", sizeof "current" - 1);
#else
	return 0 == std::strcmp(e.d_name, "current");
#endif
}

static inline
bool
is_old (
	const dirent & e
) {
#if defined(_DIRENT_HAVE_D_NAMLEN)
	if (27 != e.d_namlen) return false;
#else
	std::size_t namlen(std::strlen(e.d_name));
	if (27 != namlen) return false;
#endif
	if ('@' != e.d_name[0] || '.' != e.d_name[25] || ('s' != e.d_name[26] && 'u' != e.d_name[26])) return false;
	for (unsigned i(0); i < 24; ++i) {
		const char c(e.d_name[1 + i]);
		if (!std::isxdigit(c) || (!std::isdigit(c) && !std::islower(c))) return false;
	}
	return true;
}

/* Loggers ******************************************************************
// **************************************************************************
*/

namespace {
struct logger {
	logger(const char * n, int df, int lf, const ProcessEnvironment & e) : 
		next(first), 
		prevnext(&first), 
		dir_name(n),
		dir_fd(df), 
		lock_fd(lf), 
		current_fd(-1),
		bol(true),
		off(0),
		envs(e)
	{ 
		if (first) first->prevnext = &next; 
		first = this; 
	}
	~logger() 
	{ 
		close("current");
		if (prevnext) *prevnext = next; 
		if (next) next->prevnext = prevnext; 
		prevnext = &next; 
		next = 0; 
		::close(lock_fd);
		::close(dir_fd);
	}

	static logger * first;
	logger * next, **prevnext;

	void start ();
	void flush();
	void rotate();
	void put (char c);

protected:
	const char * dir_name;
	int dir_fd, lock_fd, current_fd;
	bool bol;
	uint64_t current_size;
	char buf[4096];
	unsigned off;
	const ProcessEnvironment & envs;

	void close(const char * name);
	void flush_and_close(const char * name);
	void pause (const char * s, const char * n);
	bool need_rotate();
	void cap_total_size();
	int unlink_oldest_file();
	void write (const char *, std::size_t);
};
}

logger * logger::first(0);

void logger::pause (const char * s, const char * n) {
	const int error(errno);
	std::fprintf(stderr, "%s %s/%s: %s, sleeping for 1 second.\n", s, dir_name, n, std::strerror(error));
	sleep(1);
}

void logger::close(const char * name) {
	if (0 <= current_fd) {
		while (0 > ::close(current_fd)) pause("closing",name);
		current_fd = -1;
	}
}

void logger::flush_and_close(const char * name) {
	if (0 <= current_fd) {
		flush();
		while (0 > fsync(current_fd)) pause("syncing",name);
		while (0 > fchmod(current_fd, 0744)) pause("fchmod",name);
	}
	close(name);
}

void logger::flush() {
	if (0 <= current_fd) {
		while (off > 0) {
			const int n(::write(current_fd, buf, off));
			if (0 >= n) {
				pause("flushing", "current");
				continue;
			}
			std::memmove(buf, buf + n, off - n);
			off -= n;
		}
	}
}

bool logger::need_rotate() {
	return (bol ? current_size + margin : current_size) >= max_file_size; 
}

int 	/// \returns state of the log directory
	/// \retval -1 An error happened, check errno.
	/// \retval 0 The directory is still oversize and requires another pass.
	/// \retval 1 The directory has been fully size capped.
logger::unlink_oldest_file() {
	const int scan_dir_fd(dup(dir_fd));
	if (0 > scan_dir_fd) return -1;
	DIR * scan_dir(fdopendir(scan_dir_fd));
	if (!scan_dir) {
		const int error(errno);
		::close(dir_fd);
		errno = error;
		return -1;
	}
	uint64_t total(0U), reclaim(0U);
	bool seen_old(false);
	char earliest_old[NAME_MAX] = "@z";	// This is guaranteed bigger than any proper old file name.
	rewinddir(scan_dir);	// because the last pass left it at EOF.
	for (;;) {
		errno = 0;
		const dirent * entry(readdir(scan_dir));
		if (!entry) {
			const int error(errno);
			if (error) {
				closedir(scan_dir);
				errno = error;
				return -1 ;
			}
			break;
		}
#if defined(_DIRENT_HAVE_D_TYPE)
		if (DT_REG != entry->d_type && DT_LNK != entry->d_type) continue;
#endif
		if (is_current(*entry)) {
			struct stat s;
			if (0 > fstatat(dir_fd, entry->d_name, &s, 0)) {
				const int error(errno);
				closedir(scan_dir);
				errno = error;
				return -1;
			}
			total += s.st_size;
		} else
		if (is_old(*entry)) {
			struct stat s;
			if (0 > fstatat(dir_fd, entry->d_name, &s, 0)) {
				const int error(errno);
				closedir(scan_dir);
				errno = error;
				return -1;
			}
			total += s.st_size;
			if (0 < std::strcmp(earliest_old, entry->d_name)) {
				std::strncpy(earliest_old, entry->d_name, sizeof earliest_old);
				reclaim = s.st_size;
			}
			seen_old = true;
		}
	}
	closedir(scan_dir);
	if (!seen_old) return 1;
	if (total > max_total_size) {
		std::fprintf(stderr, "Removed  %s/%s to reclaim %"  PRIu64 " bytes\n", dir_name, earliest_old, reclaim);
		unlinkat(dir_fd, earliest_old, 0);
		total -= reclaim;
	}
	return total <= max_total_size;
}

void logger::cap_total_size() {
	for (;;) {
		switch (unlink_oldest_file()) {
			case 1: return;
			case -1: pause("scanning", "."); break;
		}
	}
}

void logger::rotate() {
	if (0 <= current_fd) {
		timespec now;
		clock_gettime(CLOCK_REALTIME, &now);
		const uint64_t secs(time_to_tai64(envs, TimeTAndLeap(now.tv_sec, false)));
		const uint32_t nano(now.tv_nsec);

		char * name_u(0);
		asprintf(&name_u, "@%016" PRIx64 "%08" PRIx32 ".u", secs, nano);
		while (0 > renameat(dir_fd, "current", dir_fd, name_u)) pause("renaming","current");
		std::fprintf(stderr, "Flushing   %s/%s.\n", dir_name, name_u);

		flush_and_close(name_u);

		char * name_s(0);
		asprintf(&name_s, "@%016" PRIx64 "%08" PRIx32 ".s", secs, nano);
		while (0 > renameat(dir_fd, name_u, dir_fd, name_s)) pause("renaming",name_u);
		std::fprintf(stderr, "Closed     %s/%s.\n", dir_name, name_s);

		free(name_s);
		free(name_u);
	}
	cap_total_size();
	if (0 > current_fd) {
corrupted_current:
		for (;;) {
			current_fd = open_readwritecreate_at(dir_fd, "current", 0744);
			if (0 <= current_fd) break;
			pause("opening", "current");
		}
		struct stat s;
		if (0 > fstat(current_fd, &s) || !(s.st_mode & 0100)) {
			timespec now;
			clock_gettime(CLOCK_REALTIME, &now);
			const uint64_t secs(time_to_tai64(envs, TimeTAndLeap(now.tv_sec, false)));
			const uint32_t nano(now.tv_nsec);

			char * name_u(0);
			asprintf(&name_u, "@%016" PRIx64 "%08" PRIx32 ".u", secs, nano);
			while (0 > renameat(dir_fd, "current", dir_fd, name_u)) pause("renaming","current");
			std::fprintf(stderr, "Recovering %s/%s.\n", dir_name, name_u);

			close(name_u);

			free(name_u);

			goto corrupted_current;
		}
		while (0 > fchmod(current_fd, 0644)) pause("fchmod","current");
		for (;;) {
			const off_t o(lseek(current_fd, 0, SEEK_END));
			if (0 <= o) {
				current_size = o;
				break;
			}
			pause("seeking to end of","current");
		}
		if (current_size > 0) {
			char last;
			const int rc(pread(current_fd, &last, sizeof last, current_size - 1));
			if (rc > 0) {
				bol = '\n' == last;
			}
		}
	}
}

void logger::write (const char * ptr, std::size_t len) {
	while (len > 0) {
		std::size_t l(len);
		if (l > (sizeof buf - off)) l = (sizeof buf - off);
		std::memcpy(buf + off, ptr, l);
		current_size += l;
		ptr += l;
		len -= l;
		off += l;
		if (off >= sizeof buf) flush();
	}
}

void logger::put (char c) {
	if (bol) {
		timespec now;
		clock_gettime(CLOCK_REALTIME, &now);
		const uint64_t secs(time_to_tai64(envs, TimeTAndLeap(now.tv_sec, false)));
		const uint32_t nano(now.tv_nsec);
		char stamp[27];
		snprintf(stamp, sizeof stamp, "@%016" PRIx64 "%08" PRIx32 " ", secs, nano);
		write(stamp, sizeof stamp - 1);
		bol = false;
	}
	write(&c, 1);
	bol = ('\n' == c);
	if (need_rotate())
		rotate();
}

void logger::start () {
	rotate();
	if (need_rotate())
		rotate();
}

/* Main function ************************************************************
// **************************************************************************
*/

void
cyclog [[gnu::noreturn]] (
	const char * & /*next_prog*/,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	try {
		unsigned long mts(max_total_size), mfs(max_file_size), m(margin);
		popt::unsigned_number_definition max_total_size_option('\0', "max-total-size", "bytes", "Specify the maximum total size of all log files.", mts, 0);
		popt::unsigned_number_definition max_file_size_option('\0', "max-file-size", "bytes", "Specify the maximum file size of a log files.", mfs, 0);
		popt::unsigned_number_definition margin_option('\0', "margin", "bytes", "Specify the margin for line ends at the end of a log files.", m, 0);
		popt::definition * top_table[] = {
			&max_total_size_option,
			&max_file_size_option,
			&margin_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{log(s)...}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		if (p.stopped()) throw EXIT_SUCCESS;
		if (mfs < 4096) mfs = 4096;
		if (mts < mfs) mts = mfs;
		if (m < 512) m = 512; else if (m > 8192) m = 8192;
		max_total_size = mts;
		max_file_size = mfs;
		margin = m;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw static_cast<int>(EXIT_USAGE);
	}
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing log directory name.");
		throw static_cast<int>(EXIT_USAGE);
	}

	for (std::vector<const char *>::const_iterator i(args.begin()); i != args.end(); ++i) {
		const char * name(*i);
		const int dir_fd(open_dir_at(AT_FDCWD, name));
		if (0 > dir_fd) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, name, std::strerror(error));
			throw EXIT_FAILURE;
		}
		const int lock_fd(open_lockfile_at(dir_fd, "lock"));
		if (0 > lock_fd) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s/lock: %s\n", prog, name, std::strerror(error));
			throw EXIT_FAILURE;
		}
		logger *l(new logger(name, dir_fd, lock_fd, envs));
		l->start();
	}

	ReserveSignalsForKQueue kqueue_reservation(SIGTERM, SIGINT, SIGHUP, SIGTSTP, SIGALRM, SIGPIPE, SIGQUIT, 0);
	PreventDefaultForFatalSignals ignored_signals(SIGTERM, SIGINT, SIGHUP, SIGTSTP, SIGALRM, SIGPIPE, SIGQUIT, 0);

	const int queue(kqueue());
	if (0 > queue) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kqueue", std::strerror(error));
		throw EXIT_FAILURE;
	}

	struct kevent p[16];
	{
		std::size_t index(0);
		set_event(&p[index++], STDIN_FILENO, EVFILT_READ, EV_ADD, 0, 0, 0);
		set_event(&p[index++], SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		set_event(&p[index++], SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		set_event(&p[index++], SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		set_event(&p[index++], SIGTSTP, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		set_event(&p[index++], SIGALRM, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		set_event(&p[index++], SIGPIPE, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		set_event(&p[index++], SIGQUIT, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		if (0 > kevent(queue, p, index, 0, 0, 0)) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kevent", std::strerror(error));
			throw EXIT_FAILURE;
		}
	}

	char buf[4096];
	bool pending(false);
	struct timespec zero = { 0, 0 };
	for (;;) {
		const int rc(kevent(queue, 0, 0, p, sizeof p/sizeof *p, pending ? &zero : 0));
		if (0 > rc) {
			const int error(errno);
			if (EINTR == error) continue;
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kevent", std::strerror(error));
			throw EXIT_FAILURE;
		} else
		if (0 == rc) {
			for (logger * l(logger::first); l; l = l->next) 
				l->flush();
			pending = false;
		} else
		for (size_t i(0); i < static_cast<size_t>(rc); ++i) {
			if (EVFILT_READ == p[i].filter && STDIN_FILENO == p[i].ident) {
				const ssize_t rd(read(STDIN_FILENO, buf, sizeof buf));
				if (0 > rd) {
					const int error(errno);
					if (EINTR != error) {
						std::fprintf(stderr, "%s: FATAL: %s\n", prog, std::strerror(error));
						goto terminated;
					}
				} else if (0 == rd)
					goto terminated;
				pending = true;
				for (ssize_t j(0); j < rd; ++j) {
					const char c(buf[j]);
					for (logger * l(logger::first); l; l = l->next) 
						l->put(c);
				}
			} else
			if (EVFILT_SIGNAL == p[i].filter) {
				switch (p[i].ident) {
					case SIGHUP:
					case SIGTERM:
					case SIGINT:
					case SIGPIPE:
					case SIGQUIT:
						std::fprintf(stderr, "%s: INFO: %s\n", prog, "Terminated.");
						goto terminated;
					case SIGALRM:
						std::fprintf(stderr, "%s: INFO: %s\n", prog, "Forced log rotation.");
						for (logger * l(logger::first); l; l = l->next)
							l->rotate();
						break;
					case SIGTSTP:
						std::fprintf(stderr, "%s: INFO: %s\n", prog, "Paused.");
						raise(SIGSTOP);
						std::fprintf(stderr, "%s: INFO: %s\n", prog, "Continued.");
						break;
			       }
			}
		}
	}
terminated:
	while (logger * l = logger::first)
		delete l;
	throw EXIT_SUCCESS;
}
