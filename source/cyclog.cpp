/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define __STDC_FORMAT_MACROS
#define _BSD_SOURCE 1
#include <vector>
#include <memory>
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
#include "FileDescriptorOwner.h"
#include "DirStar.h"

static uint64_t margin(512);
static uint64_t max_file_size (0x00ffffffULL);	// 16MiB.  Any larger and we start giving tools like "tail" fits.
static uint64_t max_total_size(0x3fffffffULL);	// 1GiB

/* Utilities ****************************************************************
// **************************************************************************
*/

namespace {

inline
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

inline
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

}

/* Loggers ******************************************************************
// **************************************************************************
*/

namespace {
struct logger {
	logger(const char * n, int df, int lf, const ProcessEnvironment & e) :
		dir_name(n),
		dir_fd(df),
		lock_fd(lf),
		current_fd(-1),
		bol(true),
		off(0),
		envs(e)
	{
	}
	~logger()
	{
		flush_and_synch_and_close("current");
	}

	void start ();
	void flush();
	void rotate();
	void put (char c);
	bool pending() const { return off > 0U; }

protected:
	const char * dir_name;
	const FileDescriptorOwner dir_fd, lock_fd;
	FileDescriptorOwner current_fd;
	bool bol;
	uint64_t current_size;
	char buf[4096];
	unsigned off;
	const ProcessEnvironment & envs;

	void close(const char * name);
	void flush_and_synch_and_close(const char * name);
	void synch_and_close(const char * name);
	void pause (const char * s, const char * n);
	bool need_rotate();
	void cap_total_size();
	int unlink_oldest_file();
	void write (const char *, std::size_t);
};

typedef std::list<std::shared_ptr<logger>> loglist;

inline
bool
any_pending(
	const loglist & loggers
) {
	for (loglist::const_iterator l(loggers.begin()), e(loggers.end()); l != e; ++l)
		if ((*l)->pending())
			return true;
	return false;
}

inline
void
flush_all (
	const loglist & loggers
) {
	for (loglist::const_iterator l(loggers.begin()), e(loggers.end()); l != e; ++l)
		(*l)->flush();
}

}

void logger::pause (const char * s, const char * n) {
	const int error(errno);
	std::fprintf(stderr, "%s %s/%s: %s, sleeping for 1 second.\n", s, dir_name, n, std::strerror(error));
	sleep(1);
}

void logger::close(const char * name) {
	if (0 <= current_fd.get()) {
		const int fd(current_fd.release());
		while (0 > ::close(fd)) pause("closing",name);
	}
}

void logger::synch_and_close(const char * name) {
	if (0 <= current_fd.get()) {
		while (0 > fsync(current_fd.get())) pause("syncing",name);
		while (0 > fchmod(current_fd.get(), 0744)) pause("fchmod 0744",name);
	}
	close(name);
}

void logger::flush_and_synch_and_close(const char * name) {
	flush();
	synch_and_close(name);
}

void logger::flush() {
	if (0 <= current_fd.get()) {
		while (off > 0) {
			const int n(::write(current_fd.get(), buf, off));
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
	FileDescriptorOwner scan_dir_fd(dup(dir_fd.get()));
	if (0 > scan_dir_fd.get()) return -1;
	DirStar scan_dir(scan_dir_fd);
	if (!scan_dir) {
		const int error(errno);
		scan_dir_fd.reset(-1);
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
				scan_dir = nullptr;
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
			if (0 > fstatat(dir_fd.get(), entry->d_name, &s, 0)) {
				const int error(errno);
				scan_dir = nullptr;
				errno = error;
				return -1;
			}
			total += s.st_size;
		} else
		if (is_old(*entry)) {
			struct stat s;
			if (0 > fstatat(dir_fd.get(), entry->d_name, &s, 0)) {
				const int error(errno);
				scan_dir = nullptr;
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
	scan_dir = nullptr;
	if (!seen_old) return 1;
	if (total > max_total_size) {
		std::fprintf(stderr, "Removed  %s/%s to reclaim %"  PRIu64 " bytes\n", dir_name, earliest_old, reclaim);
		unlinkat(dir_fd.get(), earliest_old, 0);
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
	if (0 <= current_fd.get()) {
		timespec now;
		clock_gettime(CLOCK_REALTIME, &now);
		const uint64_t secs(time_to_tai64(envs, TimeTAndLeap(now.tv_sec, false)));
		const uint32_t nano(now.tv_nsec);

		char * name_u(nullptr);
		asprintf(&name_u, "@%016" PRIx64 "%08" PRIx32 ".u", secs, nano);
		while (0 > renameat(dir_fd.get(), "current", dir_fd.get(), name_u)) pause("renaming","current");
		std::fprintf(stderr, "Synching   %s/%s.\n", dir_name, name_u);

		synch_and_close(name_u);

		char * name_s(nullptr);
		asprintf(&name_s, "@%016" PRIx64 "%08" PRIx32 ".s", secs, nano);
		while (0 > renameat(dir_fd.get(), name_u, dir_fd.get(), name_s)) pause("renaming",name_u);
		std::fprintf(stderr, "Closed     %s/%s.\n", dir_name, name_s);

		free(name_s);
		free(name_u);
	}
	cap_total_size();
	if (0 > current_fd.get()) {
corrupted_current:
		for (;;) {
			current_fd.reset(open_readwritecreate_at(dir_fd.get(), "current", 0744));
			if (0 <= current_fd.get()) break;
			pause("opening", "current");
		}
		struct stat s;
		if (0 > fstat(current_fd.get(), &s) || !(s.st_mode & 0100)) {
			timespec now;
			clock_gettime(CLOCK_REALTIME, &now);
			const uint64_t secs(time_to_tai64(envs, TimeTAndLeap(now.tv_sec, false)));
			const uint32_t nano(now.tv_nsec);

			char * name_u(nullptr);
			asprintf(&name_u, "@%016" PRIx64 "%08" PRIx32 ".u", secs, nano);
			while (0 > renameat(dir_fd.get(), "current", dir_fd.get(), name_u)) pause("renaming","current");
			std::fprintf(stderr, "Recovering %s/%s.\n", dir_name, name_u);

			close(name_u);

			free(name_u);

			goto corrupted_current;
		}
		while (0 > fchmod(current_fd.get(), 0644)) pause("fchmod 0644","current");
		for (;;) {
			const off_t o(lseek(current_fd.get(), 0, SEEK_END));
			if (0 <= o) {
				current_size = o;
				break;
			}
			pause("seeking to end of","current");
		}
		if (current_size > 0) {
			char last;
			const int rc(pread(current_fd.get(), &last, sizeof last, current_size - 1));
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
		popt::size_definition max_total_size_option('\0', "max-total-size", "bytes", "Specify the maximum total size of all log files.", mts, 0);
		popt::size_definition max_file_size_option('\0', "max-file-size", "bytes", "Specify the maximum file size of a log files.", mfs, 0);
		popt::size_definition margin_option('\0', "margin", "bytes", "Specify the margin for line ends at the end of a log files.", m, 0);
		popt::definition * top_table[] = {
			&max_total_size_option,
			&max_file_size_option,
			&margin_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{log(s)...}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
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
		die(prog, envs, e);
	}
	if (args.empty()) {
		die_missing_argument(prog, envs, "log directory name");
	}

	loglist loggers;
	for (std::vector<const char *>::const_iterator i(args.begin()); i != args.end(); ++i) {
		const char * name(*i);
		const int dir_fd(open_dir_at(AT_FDCWD, name));
		if (0 > dir_fd) {
			die_errno(prog, envs, name);
		}
		const int lock_fd(open_lockfile_at(dir_fd, "lock"));
		if (0 > lock_fd) {
			die_errno(prog, envs, name);
		}
		logger *l(new logger(name, dir_fd, lock_fd, envs));
		loggers.push_back(std::shared_ptr<logger>(l));
		l->start();
	}

	ReserveSignalsForKQueue kqueue_reservation(SIGTERM, SIGINT, SIGHUP, SIGTSTP, SIGALRM, SIGPIPE, SIGQUIT, 0);
	PreventDefaultForFatalSignals ignored_signals(SIGTERM, SIGINT, SIGHUP, SIGTSTP, SIGALRM, SIGPIPE, SIGQUIT, 0);

	const int queue(kqueue());
	if (0 > queue) {
		die_errno(prog, envs, "kqueue");
	}

	std::vector<struct kevent> ip;
	append_event(ip, STDIN_FILENO, EVFILT_READ, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGTSTP, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGALRM, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGPIPE, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGQUIT, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);

	char buf[4096];
	bool terminate_requested(false);
	struct timespec zero = { 0, 0 };
	for (;;) {
		const bool pending(any_pending(loggers));
		if (terminate_requested && !pending) break;
		struct kevent p[16];
		const int rc(kevent(queue, ip.data(), ip.size(), p, sizeof p/sizeof *p, pending ? &zero : nullptr));
		ip.clear();
		bool any_action(false);
		if (0 > rc) {
			if (EINTR == errno) continue;
			die_errno(prog, envs, "kevent");
		} else
		for (size_t i(0); i < static_cast<size_t>(rc); ++i) {
			if (EVFILT_READ == p[i].filter && STDIN_FILENO == p[i].ident) {
				if (EV_ERROR & p[i].flags) {
					std::fprintf(stderr, "%s: FATAL: %s\n", prog, std::strerror(p[i].data));
				input_error:
					terminate_requested = true;
					continue;
				}
				const ssize_t rd(read(STDIN_FILENO, buf, sizeof buf));
				if (0 > rd) {
					if (EINTR != errno) {
						const int error(errno);
						std::fprintf(stderr, "%s: FATAL: %s\n", prog, std::strerror(error));
						goto input_error;
					}
				} else if (0 == rd) {
					std::fprintf(stderr, "%s: INFO: %s\n", prog, "Input EOF.");
					terminate_requested = true;
					continue;
				}
				any_action = true;
				for (ssize_t j(0); j < rd; ++j) {
					const char c(buf[j]);
					for (loglist::const_iterator l(loggers.begin()), e(loggers.end()); l != e; ++l)
						(*l)->put(c);
				}
				if (EV_EOF & p[i].flags) {
					std::fprintf(stderr, "%s: INFO: %s\n", prog, "Input will EOF.");
					flush_all(loggers);
				}
			} else
			if (EVFILT_SIGNAL == p[i].filter) {
				any_action = true;
				switch (p[i].ident) {
					case SIGHUP:
					case SIGTERM:
					case SIGINT:
					case SIGPIPE:
					case SIGQUIT:
						std::fprintf(stderr, "%s: INFO: %s\n", prog, "Terminated.");
						terminate_requested = true;
						break;
					case SIGALRM:
						std::fprintf(stderr, "%s: INFO: %s\n", prog, "Forced log rotation.");
						for (loglist::const_iterator l(loggers.begin()), e(loggers.end()); l != e; ++l)
							(*l)->rotate();
						break;
					case SIGTSTP:
						std::fprintf(stderr, "%s: INFO: %s\n", prog, "Paused.");
						raise(SIGSTOP);
						break;
					case SIGCONT:
						std::fprintf(stderr, "%s: INFO: %s\n", prog, "Continued.");
						break;
			       }
			}
		}
		if (!any_action) 
			flush_all(loggers);
	}
	throw EXIT_SUCCESS;
}
