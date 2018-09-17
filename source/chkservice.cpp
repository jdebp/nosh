/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define _XOPEN_SOURCE_EXTENDED
#include <vector>
#include <map>
#include <unordered_map>
#include <set>
#include <algorithm>
#include <iostream>
#include <cstddef>
#include <cstdlib>
#include <csignal>
#include <cstring>
#include <clocale>
#include <cerrno>
#include <new>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>
#include "kqueue_common.h"
#include "utils.h"
#include "fdutils.h"
#include "service-manager-client.h"
#include "service-manager.h"
#include "unpack.h"
#include "popt.h"
#include "BaseTUI.h"
#include "FileDescriptorOwner.h"
#include "SignalManagement.h"
#if defined(__LINUX__) || defined(__linux__)
#include <ncursesw/curses.h>	// for colours
#define _BSD_SOURCE 1
#include <sys/resource.h>
#else
#include <curses.h>	// for colours
#endif

/* Time *********************************************************************
// **************************************************************************
*/

static inline
struct tm
convert(
	const ProcessEnvironment & envs,
	const uint64_t & s
) {
	const TimeTAndLeap z(tai64_to_time(envs, s));
	struct tm tm;
	localtime_r(&z.time, &tm);
	if (z.leap) ++tm.tm_sec;
	return tm;
}

/* Service bundles **********************************************************
// **************************************************************************
*/

namespace {
struct index : public std::pair<dev_t, ino_t> {
	index(const struct stat & s) : pair(s.st_dev, s.st_ino) {}
	std::size_t hash() const { return static_cast<std::size_t>(first) + static_cast<std::size_t>(second); }
};

struct bundle;

struct bundle {
	bundle() : 
		bundle_dir_fd(-1),
		supervise_dir_fd(-1),
		service_dir_fd(-1),
		state(UNKNOWN)
	{
	}
	~bundle() {}

	FileDescriptorOwner bundle_dir_fd, supervise_dir_fd, service_dir_fd;
	std::string path, name, suffix;

	enum state_type {
		UNKNOWN, NOTAPI, FIFO_ERROR, STATUS_ERROR, UNLOADED, LOADING,
		STOPPED, STARTING, STARTED, READY, RUNNING, STOPPING, DONE, RESTART
	} state;
	char want_flag, paused;
	int pid;
	bool initially_up;
	uint64_t seconds;
	uint32_t nanoseconds;

	void load_data();
	int colour_of_state () const;
	const char * name_of_state () const;
	bool valid_status() const { return UNKNOWN != state && UNLOADED != state && NOTAPI != state && FIFO_ERROR != state && STATUS_ERROR != state && LOADING != state; }
protected:
	bundle(const bundle &);
	static state_type state_of(bool ready_after_run, uint32_t main_pid, bool exited_run, char c) ;
};

static
struct { unsigned colour; const char * name; }
state_table[] = {
	// The indices here must match bundle::state_type above.
	{ 12, "unknown" },
	{ 12, "no API" },
	{ 11, "!ok" },
	{ 11, "!status" },
	{ 12, "unloaded" },
	{ 10, "loading" },
	{  4, "stopped" },
	{  5, "starting" },
	{  7, "started" },
	{  8, "ready" },
	{  8, "running" },
	{  6, "stopping" },
	{  8, "done" },
	{  9, "restart" },
};
}

inline
bundle::state_type
bundle::state_of (
	bool ready_after_run,
	uint32_t main_pid,
	bool exited_run,
	char c
) {
	switch (c) {
		case encore_status_stopped:	
			if (ready_after_run)
				return exited_run ? DONE : STOPPED;
			else
				return STOPPED;
		case encore_status_starting:	return STARTING;
		case encore_status_started:	return STARTED;
		case encore_status_running:	
			if (ready_after_run)
				return main_pid ? STARTED : READY;
			else
				return RUNNING;
		case encore_status_stopping:	return STOPPING;
		case encore_status_failed:	return RESTART;
		default:			return UNKNOWN;
	}
}

void
bundle::load_data(
) {
	initially_up = is_initially_up(service_dir_fd.get());

	const FileDescriptorOwner ok_fd(open_writeexisting_at(supervise_dir_fd.get(), "ok"));
	if (0 > ok_fd.get()) {
		const int error(errno);
		if (ENXIO == error) {
			state = UNLOADED;
		} else
		if (ENOENT == error) {
			state = NOTAPI;
		} else
		{
			state = FIFO_ERROR;
		}
		return;
	}

	const FileDescriptorOwner status_fd(open_read_at(supervise_dir_fd.get(), "status"));
	if (0 > status_fd.get()) {
		state = STATUS_ERROR;
		return;
	}

	char status[STATUS_BLOCK_SIZE];
	const int b(read(status_fd.get(), status, sizeof status));

	if (b < DAEMONTOOLS_STATUS_BLOCK_SIZE) {
		state = LOADING;
		return;
	}

	seconds = unpack_bigendian(status, 8);
	nanoseconds = unpack_bigendian(status + 8, 4);
	const uint32_t p(unpack_littleendian(status + THIS_PID_OFFSET, 4));

	want_flag = status[WANT_FLAG_OFFSET];
	paused = status[PAUSE_FLAG_OFFSET];
	if (b < ENCORE_STATUS_BLOCK_SIZE) {
		// supervise doesn't turn off the want flag.
		if (p) {
			if ('u' == want_flag) want_flag = '\0';
		} else {
			if ('d' == want_flag) want_flag = '\0';
		}
	}
	pid = 0 == p ? -1 : static_cast<uint32_t>(-1) == p ? 0 : static_cast<int>(p);
	const char state_byte(b >= ENCORE_STATUS_BLOCK_SIZE ? status[ENCORE_STATUS_OFFSET] : p ? encore_status_running : encore_status_stopped);
	const bool exited_run(has_exited_run(b, status));
	const bool ready_after_run(is_ready_after_run(service_dir_fd.get()));
	state = state_of(ready_after_run, p, exited_run, state_byte);
}

namespace std {
template <> struct hash<struct index> {
	size_t operator() (const struct index & v) const { return v.hash(); }
};
}

inline
int
bundle::colour_of_state (
) const {
	return state_table[state].colour;
}

inline
const char *
bundle::name_of_state (
) const {
	return state_table[state].name;
}

namespace {
struct bundle_info_map : public std::unordered_map<index, bundle> {
	bundle & add_bundle(const struct stat &, FileDescriptorOwner &, FileDescriptorOwner &, FileDescriptorOwner &, const std::string &, const std::string &, const std::string &);
};
typedef std::list<bundle *> bundle_pointer_list;
}

inline
bundle &
bundle_info_map::add_bundle (
	const struct stat & bundle_dir_s,
	FileDescriptorOwner & bundle_dir_fd,
	FileDescriptorOwner & supervise_dir_fd,
	FileDescriptorOwner & service_dir_fd,
	const std::string & path,
	const std::string & name,
	const std::string & suffix
) {
	const index ix(bundle_dir_s);
	bundle_info_map::iterator i(find(ix));
	if (end() != i)
		return i->second;
	bundle & b((*this)[ix]);
	b.path = path;
	b.name = name;
	b.suffix = suffix;
	b.bundle_dir_fd.reset(bundle_dir_fd.release());
	b.supervise_dir_fd.reset(supervise_dir_fd.release());
	b.service_dir_fd.reset(service_dir_fd.release());
	return b;
}

/* Full-screen TUI **********************************************************
// **************************************************************************
*/

namespace {
struct TUI : public BaseTUI {
	TUI(const char * p, ProcessEnvironment & e, bundle_info_map & m);

	bool quit_flagged() const { return pending_quit_event; }
	void handle_non_kevents ();
protected:
	const char * prog;
	const ProcessEnvironment & envs;
	bundle_info_map & bundle_map;
	bundle_pointer_list bundles;
	std::size_t current_row;
	bool sort_needed, redraw_needed, pending_quit_event;
	unsigned sort_mode;

	void setup_colours();
	virtual void redraw();
	virtual void unicode_keypress(wint_t);
	virtual void ncurses_keypress(wint_t);

	void sort_bundles();
	bool less_than (const bundle & a, const bundle & b);
	void invalidate() { redraw_needed = true; }
	void write_one_line(const bundle & sb, const uint64_t z);
	void write_timestamp(const uint64_t s);
	void set_colour_pair(unsigned);
	void set_normal_colour();
	void set_exception_colour();
	void set_config_colour();
	void set_control_colour();
};

struct { short fg, bg; }
default_colours[13] = {
	{	COLOR_WHITE,	COLOR_BLACK	},	// normal
	{	COLOR_RED,	COLOR_BLACK	},	// exception
	{	COLOR_CYAN,	COLOR_BLACK	},	// config
	{	COLOR_BLACK,	COLOR_GREEN	},	// control
	{	COLOR_WHITE,	COLOR_BLUE	},	// stopped
	{	COLOR_YELLOW,	COLOR_BLUE	},	// starting
	{	COLOR_YELLOW,	COLOR_BLUE	},	// stopping
	{	COLOR_MAGENTA,	COLOR_BLUE	},	// started
	{	COLOR_GREEN,	COLOR_BLUE	},	// ready, running, and done
	{	COLOR_RED,	COLOR_BLUE	},	// restart
	{	COLOR_CYAN,	COLOR_BLUE	},	// loading
	{	COLOR_RED,	COLOR_BLACK	},	// !ok, !status
	{	COLOR_CYAN,	COLOR_BLACK	},	// unknown, no API, unloaded
};
}

TUI::TUI(
	const char * p,
	ProcessEnvironment & e,
	bundle_info_map & m
) :
	prog(p),
	envs(e),
	bundle_map(m),
	current_row(0),
	sort_needed(true),
	redraw_needed(true),
	pending_quit_event(false),
	sort_mode(4U)
{
	setup_colours();
}

void
TUI::handle_non_kevents (
) {
	if (sort_needed) {
		sort_needed = false;
		sort_bundles();
		redraw_needed = true;
	}
	if (redraw_needed) {
		redraw_needed = false;
		redraw();
		set_update();
	}
	BaseTUI::handle_non_kevents();
}

void
TUI::setup_colours(
) {
	setup_default_colours(COLOR_GREEN, COLOR_BLACK);
	for (std::size_t i(0); i < sizeof default_colours/sizeof *default_colours; ++i)
		init_pair(1 + i, default_colours[i].fg, default_colours[i].bg);
}

inline
void
TUI::set_colour_pair(
	unsigned i
) {
	wcolor_set(window, i, 0);
}

inline
void
TUI::set_normal_colour(
) {
	set_colour_pair(1 + 0);
}

inline
void
TUI::set_exception_colour(
) {
	set_colour_pair(1 + 1);
}

inline
void
TUI::set_config_colour(
) {
	set_colour_pair(1 + 2);
}

inline
void
TUI::set_control_colour(
) {
	set_colour_pair(1 + 3);
}

inline
void
TUI::write_timestamp (
	const uint64_t s
) {
	char buf[64];
	const struct tm t(convert(envs, s));
	const size_t len(std::strftime(buf, sizeof buf, " %F %T %z", &t));
	waddnstr(window, buf, len);
}

inline
void
TUI::write_one_line (
	const bundle & sb,
	const uint64_t z
) {
	set_colour_pair(1 + sb.colour_of_state());
	wprintw(window, "%-8s", sb.name_of_state());
	set_normal_colour();
	waddch(window, ' ');
	set_config_colour();
	wprintw(window, "%-8s", sb.initially_up ? "enabled" : "disabled");
	set_normal_colour();
	waddch(window, ' ');
	set_control_colour();
	wprintw(window, "%c%c", sb.valid_status() && sb.want_flag ? sb.want_flag : ' ', sb.valid_status() && sb.paused ? 'p' : ' ');
	set_normal_colour();
	if (sb.valid_status()) {
		if (-1 != sb.pid)
			wprintw(window, " %7u", sb.pid);
		else
			wprintw(window, " %7s", "");
		if (z < sb.seconds)
			set_exception_colour();
		write_timestamp(sb.seconds);
		if (z < sb.seconds)
			set_normal_colour();
		wprintw(window, " %s\t%s", sb.name.c_str(), sb.path.c_str());
	} else {
		wprintw(window, " %7s %25s %s\t%s", "", "", sb.name.c_str(), sb.path.c_str());
	}
	waddch(window, '\n');
}

bool
TUI::less_than (
	const bundle & a,
	const bundle & b
) {
	switch (sort_mode) {
		by_name:
		default:
		case 4U:
			// By name and path
			if (a.name < b.name) return true;
			if (a.name == b.name) {
				if (a.path < b.path) return true;
			}
			break;
		case 3U:
			// By timestamp, name, and path
			if (!a.valid_status()) {
				if (!b.valid_status()) goto by_name;
				break;
			}
			if (!b.valid_status()) return true;
			if (a.seconds < b.seconds) return true;
			if (a.seconds == b.seconds) {
				if (a.nanoseconds < b.nanoseconds) return true;
				if (a.nanoseconds == b.nanoseconds) goto by_name;
			}
			break;
		by_pid:
		case 2U:
			// By process ID, name, and path
			if (!a.valid_status()) {
				if (!b.valid_status()) goto by_name;
				break;
			}
			if (!b.valid_status()) return true;
			// Rely on -1U being greater than every other positive signed integer.
			if (static_cast<unsigned int>(a.pid) < static_cast<unsigned int>(b.pid)) return true;
			// Both process IDs are the same when they are both -1.
			if (a.pid == b.pid) goto by_name;
			break;
		case 1U:
			// By state, process ID, name, and path
			if (a.state < b.state) return true;
			if (a.state == b.state) goto by_pid;
			break;
		case 0U:
			// By state, name, and path
			if (a.state < b.state) return true;
			if (a.state == b.state) goto by_name;
			break;
	}
	return false;
}

void
TUI::sort_bundles(
) {
	bundles.clear();
	for (bundle_info_map::iterator i(bundle_map.begin()), ie(bundle_map.end()); ie != i; ++i) {
		bundle & b(i->second);
		// insertion sort
		for (bundle_pointer_list::iterator j(bundles.begin()), je(bundles.end()); je != j; ++j) {
			if (less_than(b, **j)) {
				bundles.insert(j, &b);
				goto done;
			}
		}
		bundles.push_back(&b);
done:		;
	}
}

void
TUI::redraw (
) {
	timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	const uint64_t z(time_to_tai64(envs, TimeTAndLeap(now.tv_sec, false)));

	werase(window);
	wmove(window, 0, 0);
	wresize(window, bundles.size(), 4096);
	std::size_t row(0);
	for (bundle_pointer_list::const_iterator b(bundles.begin()), e(bundles.end()), i(b); e != i; ++i) {
		wattrset(window, row == current_row ? A_REVERSE : A_NORMAL);
		wcolor_set(window, 0, 0);
		wclrtoeol(window);
		write_one_line(**i, z);
		++row;
	}
	wattrset(window, A_NORMAL);
	wcolor_set(window, 0, 0);
	switch (sort_mode) {
		default:
		case 0U:	wmove(window, current_row, 0); break;
		case 1U:	wmove(window, current_row, 0); break;
		case 2U:	wmove(window, current_row, 21); break;
		case 3U:	wmove(window, current_row, 29); break;
		case 4U:	wmove(window, current_row, 55); break;
	}
}

void
TUI::unicode_keypress(
	wint_t c
) {
	switch (c) {
		case '\x03':
		case '\x1c':
		case 'Q': case 'q':
			pending_quit_event = true;
			break;
		case '<':
			if (sort_mode > 0U) { --sort_mode; sort_needed = true; }
			break;
		case '>':
			if (sort_mode < 4U) { ++sort_mode; sort_needed = true; }
			break;
	}
}

void
TUI::ncurses_keypress(
	wint_t k
) {
	switch (k) {
		case KEY_DOWN:
			if (current_row + 1 < bundles.size()) { ++current_row; invalidate(); }
			break;
		case KEY_UP:
			if (current_row > 0) { --current_row; invalidate(); }
			break;
		case KEY_HOME:
			if (current_row != 0) { current_row = 0; invalidate(); }
			break;
		case KEY_END:
			if (bundles.size() && current_row + 1 != bundles.size()) { current_row = bundles.size() - 1; invalidate(); }
			break;
		case KEY_NPAGE:
			if (bundles.size() && current_row + 1 != bundles.size()) {
				unsigned n(page_rows());
				if (current_row + n < bundles.size())
					current_row += n;
				else
					current_row = bundles.size() - 1;
				invalidate();
			}
			break;
		case KEY_PPAGE:
			if (current_row != 0) {
				unsigned n(page_rows());
				if (current_row > n)
					current_row -= n;
				else
					current_row = 0;
				invalidate();
			}
			break;
	}
}

/* System control subcommands ***********************************************
// **************************************************************************
*/

void
chkservice [[gnu::noreturn]] ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::bool_definition user_option('u', "user", "Communicate with the per-user manager.", per_user_mode);
		popt::definition * main_table[] = {
			&user_option
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
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing service bundle name(s).");
		throw static_cast<int>(EXIT_USAGE);
	}

#if defined(__LINUX__) || defined(__linux__)
	// Linux's default file handle limit of 1024 is far too low for normal usage patterns.
	const rlimit file_limit = { 16384U, 16384U };
	setrlimit(RLIMIT_NOFILE, &file_limit);
#endif

	bundle_info_map bundle_map;

	for (std::vector<const char *>::const_iterator i(args.begin()); args.end() != i; ++i) {
		std::string path, name, suffix;
		FileDescriptorOwner bundle_dir_fd(open_bundle_directory(envs, "", *i, path, name, suffix));
		if (0 > bundle_dir_fd.get()) {
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, *i, std::strerror(error));
			continue;
		}
		struct stat bundle_dir_s;
		if (!is_directory(bundle_dir_fd.get(), bundle_dir_s)) {
			std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, *i, std::strerror(ENOTDIR));
			continue;
		}
		FileDescriptorOwner supervise_dir_fd(open_supervise_dir(bundle_dir_fd.get()));
		if (0 > supervise_dir_fd.get()) {
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, *i, "supervise", std::strerror(error));
			continue;
		}
		FileDescriptorOwner service_dir_fd(open_service_dir(bundle_dir_fd.get()));
		if (0 > service_dir_fd.get()) {
			const int error(errno);
			std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, *i, "service", std::strerror(error));
			continue;
		}

		bundle_map.add_bundle(bundle_dir_s, bundle_dir_fd, supervise_dir_fd, service_dir_fd, path, name, suffix);
	}

	for (bundle_info_map::iterator i(bundle_map.begin()), e(bundle_map.end()); e != i; ++i)
		i->second.load_data();

	// Without this, ncursesw operates in 8-bit compatibility mode.
	std::setlocale(LC_ALL, "");

	ReserveSignalsForKQueue kqueue_reservation(SIGWINCH, 0);

	const int queue(kqueue());
	if (0 > queue) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kqueue", std::strerror(error));
		throw EXIT_FAILURE;
	}

	struct kevent p[3];
	{
		std::size_t index(0U);
		set_event(&p[index++], SIGWINCH, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		set_event(&p[index++], STDIN_FILENO, EVFILT_READ, EV_ADD, 0, 0, 0);
		if (0 > kevent(queue, p, index, 0, 0, 0)) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kevent", std::strerror(error));
			throw EXIT_FAILURE;
		}
	}

	TUI ui(prog, envs, bundle_map);

	while (!ui.quit_flagged()) {
		ui.handle_non_kevents();

		const int rc(kevent(queue, p, 0, p, sizeof p/sizeof *p, 0));

		if (0 > rc) {
			if (ui.resize_needed()) continue;
			const int error(errno);
			if (EINTR == error) continue;
#if defined(__LINUX__) || defined(__linux__)
			if (EINVAL == error) continue;	// This works around a Linux bug when an inotify queue overflows.
			if (0 == error) continue;	// This works around another Linux bug.
#endif
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "poll", std::strerror(error));
			throw EXIT_FAILURE;
		}

		for (size_t i(0); i < static_cast<size_t>(rc); ++i) {
			const struct kevent & e(p[i]);
			if (EVFILT_SIGNAL == e.filter)
				ui.handle_signal(e.ident);
			if (EVFILT_READ == e.filter && STDIN_FILENO == e.ident)
				ui.handle_stdin(e.data);
		}
	}

	throw EXIT_SUCCESS;
}
