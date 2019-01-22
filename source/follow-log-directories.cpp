
/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <string>
#include <map>
#include <cstdlib>
#include <csignal>
#include <cstring>
#include <climits>
#include <cerrno>
#include <iostream>
#include <iomanip>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include "kqueue_common.h"
#include <dirent.h>
#include <unistd.h>
#include "utils.h"
#include "fdutils.h"
#include "FileDescriptorOwner.h"
#include "DirStar.h"
#include "popt.h"

/* Cursors ******************************************************************
// **************************************************************************
*/

enum { 
	EXTERNAL_TAI64_LENGTH = 16,
	EXTERNAL_TAI64N_LENGTH = 24
};

static inline
bool
is_external_tai64n (
	const char stamp[EXTERNAL_TAI64N_LENGTH]
) {
	for (unsigned i(0); i < EXTERNAL_TAI64N_LENGTH; ++i) {
		const char c(stamp[i]);
		if (!std::isxdigit(c) || (!std::isdigit(c) && !std::islower(c))) return false;
	}
	return true;
}

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
is_lock (
	const dirent & e
) {
#if defined(_DIRENT_HAVE_D_NAMLEN)
	return (sizeof "lock" - 1) == e.d_namlen && 0 == memcmp(e.d_name, "lock", sizeof "lock" - 1);
#else
	return 0 == std::strcmp(e.d_name, "lock");
#endif
}

static inline
bool
is_old (
	const dirent & e
) {
#if defined(_DIRENT_HAVE_D_NAMLEN)
	if (EXTERNAL_TAI64N_LENGTH + 3 != e.d_namlen) return false;
#else
	std::size_t namlen(std::strlen(e.d_name));
	if (EXTERNAL_TAI64N_LENGTH + 3 != namlen) return false;
#endif
	if ('@' != e.d_name[0] || '.' != e.d_name[EXTERNAL_TAI64N_LENGTH + 1] || ('s' != e.d_name[EXTERNAL_TAI64N_LENGTH + 2] && 'u' != e.d_name[EXTERNAL_TAI64N_LENGTH + 2])) return false;
	return is_external_tai64n(e.d_name + 1);
}

namespace {

struct index : public std::pair<dev_t, ino_t> {
	index(const struct stat & s) : pair(s.st_dev, s.st_ino) {}
};

class Cursor : public index {
public:
	Cursor ( const struct stat & );
	~Cursor() {}
	std::string appname;
	FileDescriptorOwner main_dir, last_file, current_file;
	void eof();
	void process(const char *, std::size_t);
	bool at_or_beyond(const char stamp[EXTERNAL_TAI64N_LENGTH]) const;
	void read_last();
	void update(const char stamp[EXTERNAL_TAI64N_LENGTH]);
	char last[EXTERNAL_TAI64N_LENGTH];
protected:
	enum { BOL, STAMP, ONESPACE, BODY, SKIP } state;
	std::string message;
	char line_stamp[EXTERNAL_TAI64N_LENGTH];
	std::size_t line_stamp_pos;
	void process(char);
	void emit();
};

}

inline
Cursor::Cursor ( const struct stat & s ) :
	index(s),
	appname(),
	main_dir(-1),
	last_file(-1),
	current_file(-1),
	state(BOL),
	message(),
	line_stamp_pos(0)
{
	std::memset(last, '0', EXTERNAL_TAI64N_LENGTH);
}

inline
void 
Cursor::read_last() 
{
	if (-1 != last_file.get()) {
		char stamp[EXTERNAL_TAI64N_LENGTH + 1];
		const ssize_t rc(pread(last_file.get(), stamp, sizeof stamp, 0));
		if (sizeof stamp == rc && '\n' == stamp[EXTERNAL_TAI64N_LENGTH] && is_external_tai64n(stamp))
			std::memcpy(last, stamp, EXTERNAL_TAI64N_LENGTH);
	}
}

inline
void 
Cursor::update(
	const char stamp[EXTERNAL_TAI64N_LENGTH]
) {
	std::memcpy(last, stamp, EXTERNAL_TAI64N_LENGTH);
	if (-1 != last_file.get()) {
		const struct iovec v[2] = {
			{ const_cast<char *>(stamp), EXTERNAL_TAI64N_LENGTH },
			{ const_cast<char *>("\n"), 1 }
		};
		pwritev(last_file.get(), v, sizeof v/sizeof *v, 0);
	}
}

inline
void
Cursor::emit ()
{
	const struct iovec v[] = {
		{ const_cast<char *>("@"), 1 },
		{ line_stamp, EXTERNAL_TAI64N_LENGTH },
		{ const_cast<char *>(" "), 1 },
		{ const_cast<char *>(message.data()), message.length() },
		{ const_cast<char *>("\n"), 1 }
	};
	writev(STDOUT_FILENO, v, sizeof v/sizeof *v);
	message.clear();
}

inline
void
Cursor::eof () 
{
	std::fprintf(stderr, "%s: At EOF, last is now %.*s.\n", appname.c_str(), EXTERNAL_TAI64N_LENGTH, last);
	switch (state) {
		case BODY:
			emit();
			update(line_stamp);
			// Fall through to:
			[[clang::fallthrough]];
		case SKIP:
		case STAMP:
		case ONESPACE:
			state = BOL;
			// Fall through to:
			[[clang::fallthrough]];
		case BOL:
			break;
	}
}

inline
void
Cursor::process (
	char c
) {
	switch (state) {
		case SKIP:
			if ('\n' == c)
				state = BOL;
			break;
		case BODY:
			if ('\n' == c) {
				emit();
				update(line_stamp);
				state = BOL;
			} else
				message += c;
			break;
		case ONESPACE:
			if (' ' == c)
				state = BODY;
			else 
			if ('\n' == c)
				state = BOL;
			else
				state = SKIP;
			break;
		case STAMP:
			if ('\n' == c)
				state = BOL;
			else
			if (std::isxdigit(c)) {
				line_stamp[line_stamp_pos++] = c;
				if (line_stamp_pos >= sizeof line_stamp/sizeof *line_stamp) {
					state = at_or_beyond(line_stamp) ? SKIP : ONESPACE;
				}
			} else
				state = SKIP;
			break;
		case BOL:
			if ('@' == c) {
				line_stamp_pos = 0;
				state = STAMP;
			} else 
			if ('\n' != c)
				state = SKIP;
			break;
	}
}

inline
void
Cursor::process (
	const char * b,
	std::size_t l
) {
	while (l) {
		process(*b);
		--l;
		++b;
	}
}

inline
bool
Cursor::at_or_beyond (
	const char stamp[EXTERNAL_TAI64N_LENGTH]
) const {
	return 0 <= std::memcmp(last, stamp, EXTERNAL_TAI64N_LENGTH);
}

typedef std::map<struct index, Cursor *> cursor_collection;
static cursor_collection cursors;

typedef std::map<int, Cursor *> fd_index;
static fd_index by_current_file_fd;
static fd_index by_main_dir_fd;

static inline
void
rescan (
	const FileDescriptorOwner & queue,
	const FileDescriptorOwner & scan_dir_fd,
	const char * scan_directory
) {
	FileDescriptorOwner duplicated_scan_dir_fd(dup(scan_dir_fd.get()));
	if (0 > duplicated_scan_dir_fd.get()) {
exit_scan:
		const int error(errno);
		std::fprintf(stderr, "FATAL: %s: %s\n", scan_directory, std::strerror(error));
		throw EXIT_FAILURE;
	}

	const DirStar scan_dir(duplicated_scan_dir_fd);
	if (!scan_dir) goto exit_scan;

	rewinddir(scan_dir);	// because the last pass left it at EOF.

	std::fprintf(stderr, "Scanning cursors in %s\n", scan_directory);

	for (;;) {
		errno = 0;
		const dirent * entry(readdir(scan_dir));
		if (!entry) {
			if (errno) goto exit_scan;
			break;
		}
#if defined(_DIRENT_HAVE_D_NAMLEN)
		if (1 > entry->d_namlen) continue;
#endif
		if ('.' == entry->d_name[0]) continue;
#if defined(_DIRENT_HAVE_D_TYPE)
		if (DT_DIR != entry->d_type && DT_LNK != entry->d_type) continue;
#endif

		const FileDescriptorOwner cursor_dir_fd(open_dir_at(scan_dir_fd.get(), entry->d_name));
		if (0 > cursor_dir_fd.get()) {
			std::fprintf(stderr, "ERROR: %s/%s: %s\n", scan_directory, entry->d_name, std::strerror(errno));
			continue;
		}
		struct stat cursor_dir_s;
		if (!is_directory(cursor_dir_fd.get(), cursor_dir_s)) continue;

		cursor_collection::iterator i(cursors.find(cursor_dir_s));
		if (i != cursors.end()) continue;

		FileDescriptorOwner main_dir_fd(open_dir_at(cursor_dir_fd.get(), "main/"));
		if (0 > main_dir_fd.get()) {
			std::fprintf(stderr, "ERROR: %s/%s/%s: %s\n", scan_directory, entry->d_name, "main", std::strerror(errno));
			continue;
		}
		FileDescriptorOwner last_file_fd(open_readwritecreate_at(cursor_dir_fd.get(), "last", 0644));
		if (0 > last_file_fd.get()) {
			std::fprintf(stderr, "ERROR: %s/%s/%s: %s\n", scan_directory, entry->d_name, "last", std::strerror(errno));
			continue;
		}

		Cursor * c(new Cursor(cursor_dir_s));
		if (!c) continue;

		std::fprintf(stderr, "New cursor %s/%s\n", scan_directory, entry->d_name);

		std::pair<cursor_collection::iterator, bool> it(cursors.insert(cursor_collection::value_type(cursor_dir_s,c)));
		if (!it.second) {
			delete c;
			continue;
		}
		c->main_dir.reset(main_dir_fd.release());
		c->last_file.reset(last_file_fd.release());
		c->appname = entry->d_name;

		c->read_last();

		by_main_dir_fd.insert(fd_index::value_type(c->main_dir.get(), c));

		struct kevent e[1];
		set_event(&e[0], c->main_dir.get(), EVFILT_VNODE, EV_ADD|EV_CLEAR, NOTE_WRITE|NOTE_EXTEND, 0, 0);
		if (0 > kevent(queue.get(), e, sizeof e/sizeof *e, 0, 0, 0)) {
			const int error(errno);
			std::fprintf(stderr, "FATAL: %s: %s\n", "kevent", std::strerror(error));
			throw EXIT_FAILURE;
		}
	}
}

static inline
void
process (
	Cursor & c,
	int fd
) {
	for (;;) {
		char buf[65536];
		const int n(read(fd, buf, sizeof buf));
		if (n <= 0) break;
		c.process(buf, n);
	}
}

static inline
void
catch_up (
	const FileDescriptorOwner & queue,
	Cursor & c,
	const char * scan_directory
) {
	for (;;) {
		FileDescriptorOwner duplicated_main_dir_fd(dup(c.main_dir.get()));
		if (0 > duplicated_main_dir_fd.get()) {
exit_scan:
			const int error(errno);
			std::fprintf(stderr, "FATAL: %s/%s/%s: %s\n", scan_directory, c.appname.c_str(), "main", std::strerror(error));
			throw EXIT_FAILURE;
		}

		DirStar main_dir(duplicated_main_dir_fd);
		if (!main_dir) goto exit_scan;

		rewinddir(main_dir);	// because the last pass left it at EOF.

		std::fprintf(stderr, "Scanning %s/%s/%s for old files\n", scan_directory, c.appname.c_str(), "main");

		bool seen_old(false);
		char earliest_old[EXTERNAL_TAI64N_LENGTH + 4]; // decorated with @.s
		for (;;) {
			errno = 0;
			const dirent * entry(readdir(main_dir));
			if (!entry) {
				if (errno) goto exit_scan;
				break;
			}
#if defined(_DIRENT_HAVE_D_NAMLEN)
			if (1 > entry->d_namlen) continue;
#endif
			if ('.' == entry->d_name[0]) continue;
#if defined(_DIRENT_HAVE_D_TYPE)
			if (DT_REG != entry->d_type && DT_LNK != entry->d_type) continue;
#endif

			if (is_current(*entry) || is_lock(*entry)) continue;

			if (!is_old(*entry)) {
				std::fprintf(stderr, "%s/%s/%s/%s is not an old file.\n", scan_directory, c.appname.c_str(), "main", entry->d_name);
				continue;
			}

			if (c.at_or_beyond(entry->d_name + 1)) {
				std::fprintf(stderr, "%s/%s/%s/%s is older than last (%.*s).\n", scan_directory, c.appname.c_str(), "main", entry->d_name, EXTERNAL_TAI64N_LENGTH, c.last);
				continue;
			}

			if (seen_old && 0 >= std::strcmp(earliest_old, entry->d_name)) {
				std::fprintf(stderr, "%s/%s/%s/%s is not the earliest (%.*s).\n", scan_directory, c.appname.c_str(), "main", entry->d_name, EXTERNAL_TAI64N_LENGTH + 3, earliest_old);
				continue;
			}

			std::memcpy(earliest_old, entry->d_name, sizeof earliest_old);
			seen_old = true;
		}

		if (!seen_old) break;

		std::fprintf(stderr, "Catching up %s/%s/%s/%s\n", scan_directory, c.appname.c_str(), "main", earliest_old);

		const FileDescriptorOwner oldest_file_fd(open_read_at(c.main_dir.get(), earliest_old));
		if (0 > oldest_file_fd.get()) {
			std::fprintf(stderr, "ERROR: %s/%s/%s/%s: %s\n", scan_directory, c.appname.c_str(), "main", earliest_old, std::strerror(errno));
			continue;
		}

		process(c, oldest_file_fd.get());
		c.eof();
		c.update(earliest_old + 1);	// Skip the initial @ in the name for the timestamp.
	}

	FileDescriptorOwner current_file_fd(open_read_at(c.main_dir.get(), "current"));
	if (0 > current_file_fd.get()) {
		std::fprintf(stderr, "ERROR: %s/%s/%s/%s: %s\n", scan_directory, c.appname.c_str(), "main", "current", std::strerror(errno));
		return;
	}

	std::fprintf(stderr, "Catching up %s/%s/%s/%s\n", scan_directory, c.appname.c_str(), "main", "current");

	c.current_file.reset(current_file_fd.release());
	by_current_file_fd.insert(fd_index::value_type(c.current_file.get(), &c));

	process(c, c.current_file.get());

	std::fprintf(stderr, "Synchronized %s/%s/%s/%s, now waiting for changes.\n", scan_directory, c.appname.c_str(), "main", "current");

	struct kevent e[1];
	set_event(&e[0], c.current_file.get(), EVFILT_VNODE, EV_ADD|EV_CLEAR, NOTE_WRITE|NOTE_EXTEND, 0, 0);
	if (0 > kevent(queue.get(), e, sizeof e/sizeof *e, 0, 0, 0)) {
		const int error(errno);
		std::fprintf(stderr, "FATAL: %s: %s\n", "kevent", std::strerror(error));
		throw EXIT_FAILURE;
	}
}

static inline
void
mark_as_behind (
	const FileDescriptorOwner & queue,
	Cursor & c,
	const char * scan_directory
) {
	if (-1 != c.current_file.get()) {
		process(c, c.current_file.get());
		c.eof();

		struct kevent e[1];
		set_event(&e[0], c.current_file.get(), EVFILT_VNODE, EV_DELETE, NOTE_WRITE|NOTE_EXTEND, 0, 0);
		if (0 > kevent(queue.get(), e, sizeof e/sizeof *e, 0, 0, 0)) {
			const int error(errno);
			std::fprintf(stderr, "FATAL: %s: %s\n", "kevent", std::strerror(error));
			throw EXIT_FAILURE;
		}

		by_current_file_fd.erase(c.current_file.get());
		c.current_file.reset(-1);

		std::fprintf(stderr, "Desynchronized from %s/%s/%s/%s\n", scan_directory, c.appname.c_str(), "main", "current");
	}
}

/* Main function ************************************************************
// **************************************************************************
*/

void
follow_log_directories [[gnu::noreturn]] (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::top_table_definition main_option(0, 0, "Main options", "{directory}");

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

	if (1 != args.size()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "One directory name is required.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * const scan_directory(args[0]);

	const FileDescriptorOwner queue(kqueue());
	if (0 > queue.get()) {
		const int error(errno);
		std::fprintf(stderr, "FATAL: %s: %s\n", "kqueue", std::strerror(error));
		throw EXIT_FAILURE;
	}

	FileDescriptorOwner scan_dir_fd(open_dir_at(AT_FDCWD, scan_directory));
	if (0 > scan_dir_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "FATAL: %s: %s\n", scan_directory, std::strerror(error));
		throw EXIT_FAILURE;
	} else {
		struct kevent e[1];
		set_event(&e[0], scan_dir_fd.get(), EVFILT_VNODE, EV_ADD|EV_CLEAR, NOTE_WRITE|NOTE_EXTEND, 0, 0);
		if (0 > kevent(queue.get(), e, sizeof e/sizeof *e, 0, 0, 0)) {
			const int error(errno);
			std::fprintf(stderr, "FATAL: %s: %s\n", "kevent", std::strerror(error));
			throw EXIT_FAILURE;
		}
	}

	bool rescan_needed(true);
	for (;;) {
		if (rescan_needed) {
			rescan(queue, scan_dir_fd, scan_directory);
			rescan_needed = false;
		}

		for (cursor_collection::iterator i(cursors.begin()); cursors.end() != i; ++i) {
			if (-1 == i->second->current_file.get()) 
				catch_up(queue, *i->second, scan_directory);
		}

		struct kevent p[20];
		const int rc(kevent(queue.get(), 0, 0, p, sizeof p/sizeof *p, 0));
		if (0 > rc) {
			const int error(errno);
			if (EINTR == error) continue;
			std::fprintf(stderr, "FATAL: %s: %s\n", "kevent", std::strerror(error));
			throw EXIT_FAILURE;
		}

		for (size_t i(0); i < static_cast<std::size_t>(rc); ++i) {
			const struct kevent & e(p[i]);
			switch (e.filter) {
				case EVFILT_VNODE:
				{
					const int fd(e.ident);
					if (fd == scan_dir_fd.get()) {
						rescan_needed = true;
						break;
					}
					fd_index::iterator mi(by_main_dir_fd.find(fd));
					if (mi != by_main_dir_fd.end()) {
						Cursor & c(*mi->second);
						mark_as_behind(queue, c, scan_directory);
						break;
					}
					fd_index::iterator ci(by_current_file_fd.find(fd));
					if (ci != by_current_file_fd.end()) {
						Cursor & c(*ci->second);
						process(c, c.current_file.get());
						break;
					}
					break;
				}
				default:
					break;
			}
		}
	}

	throw EXIT_SUCCESS;
}
