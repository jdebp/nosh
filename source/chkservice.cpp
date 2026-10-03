/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define _BSD_SOURCE 1
#include <map>
#include <unordered_map>
#include <set>
#include <algorithm>
#include <iostream>
#include <cstddef>
#include <cstdlib>
#include <csignal>
#include <cstring>
#include <cerrno>
#include <sys/stat.h>
#if defined(__LINUX__) || defined(__linux__)
#include <sys/resource.h>
#endif
#include <unistd.h>
#include "kqueue_common.h"
#include "utils.h"
#include "fdutils.h"
#include "service-manager-client.h"
#include "service-manager.h"
#include "unpack.h"
#include "popt.h"
#include "FileDescriptorOwner.h"
#include "CharacterCell.h"
#include "InputMessage.h"
#include "TerminalCapabilities.h"
#include "TUIDisplayCompositor.h"
#include "TUIOutputBase.h"
#include "TUIInputBase.h"
#include "TUIVIO.h"
#include "SignalManagement.h"

/* Time *********************************************************************
// **************************************************************************
*/

namespace {

inline
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

}

/* Service bundles **********************************************************
// **************************************************************************
*/

namespace {

struct index : public std::pair<dev_t, ino_t> {
	index(const struct stat & s) : pair(s.st_dev, s.st_ino) {}
	std::size_t hash() const { return static_cast<std::size_t>(first) + static_cast<std::size_t>(second); }
};

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
	const ColourPair & colour_of_state () const;
	const char * name_of_state () const;
	bool valid_status() const { return UNKNOWN != state && UNLOADED != state && NOTAPI != state && FIFO_ERROR != state && STATUS_ERROR != state && LOADING != state; }
protected:
	bundle(const bundle &);
	static state_type state_of(bool ready_after_run, uint32_t main_pid, bool exited_run, char c) ;
};

inline ColourPair C(uint_fast8_t f, uint_fast8_t b) { return ColourPair(Map256Colour(f), Map256Colour(b)); }

static const
struct { ColourPair colour; const char * name; }
state_table[] = {
	// The indices here must match bundle::state_type above.
	{  C(COLOUR_CYAN,	COLOUR_BLACK),	"unknown"	},
	{  C(COLOUR_CYAN,	COLOUR_BLACK),	"no API"	},
	{  C(COLOUR_RED,	COLOUR_BLACK),	"!ok"	},
	{  C(COLOUR_RED,	COLOUR_BLACK),	"!status"	},
	{  C(COLOUR_CYAN,	COLOUR_BLACK),	"unloaded"	},
	{  C(COLOUR_CYAN,	COLOUR_BLUE),	"loading"	},
	{  C(COLOUR_WHITE,	COLOUR_BLUE),	"stopped"	},
	{  C(COLOUR_YELLOW,	COLOUR_BLUE),	"starting"	},
	{  C(COLOUR_MAGENTA,	COLOUR_BLUE),	"started"	},
	{  C(COLOUR_GREEN,	COLOUR_BLUE),	"ready"	},
	{  C(COLOUR_GREEN,	COLOUR_BLUE),	"running"	},
	{  C(COLOUR_YELLOW,	COLOUR_BLUE),	"stopping"	},
	{  C(COLOUR_GREEN,	COLOUR_BLUE),	"done"	},
	{  C(COLOUR_RED,	COLOUR_BLUE),	"restart"	},
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
const ColourPair &
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

struct TUI :
	public TerminalCapabilities,
	public TUIOutputBase,
	public TUIInputBase
{
	TUI(const ProcessEnvironment & e, bundle_info_map & m, TUIDisplayCompositor & comp, const TUIOutputBase::Options &);
	~TUI();

	bool quit_flagged() const { return pending_quit_event; }
	bool exit_signalled() const { return terminate_signalled||interrupt_signalled||hangup_signalled; }
	void handle_signal (int);
	void handle_stdin (int);
	void handle_sort_needed ();

protected:
	TUIInputBase::WheelToKeyboard handler0;
	TUIInputBase::GamingToCursorKeypad handler1;
	TUIInputBase::EMACSToCursorKeypad handler2;
	TUIInputBase::CUAToExtendedKeys handler3;
	TUIInputBase::LessToExtendedKeys handler4;
	TUIInputBase::ConsumerKeysToExtendedKeys handler5;
	TUIInputBase::CalculatorToEditingKeypad handler6;
	class EventHandler : public TUIInputBase::EventHandler {
	public:
		EventHandler(TUI & i) : TUIInputBase::EventHandler(i), ui(i) {}
		~EventHandler();
	protected:
		virtual bool ExtendedKey(uint_fast16_t k, uint_fast8_t m);
		TUI & ui;
	} handler7;
	friend class EventHandler;
	const ProcessEnvironment & envs;
	sig_atomic_t terminate_signalled, interrupt_signalled, hangup_signalled, usr1_signalled, usr2_signalled;
	bundle_info_map & bundle_map;
	bundle_pointer_list bundles;
	TUIVIO vio;
	std::size_t current_row;
	std::size_t top_row;
	bool sort_needed, pending_quit_event;
	unsigned sort_mode;
	TUIDisplayCompositor::coordinate window_x;
	const ColourPair header, normal, name, exception, config, control;

	virtual void redraw_new();

	void sort_bundles();
	bool less_than (const bundle & a, const bundle & b);

	void write_header(const TUIDisplayCompositor::coordinate & row, long col);
	void write_one_line(const TUIDisplayCompositor::coordinate & row, long col, const CharacterCell::attribute_type &, const bundle & sb, const uint64_t z);
	void write_timestamp(const TUIDisplayCompositor::coordinate & row, long & col, const CharacterCell::attribute_type &, const ColourPair &, const uint64_t s, unsigned width);

private:
	char stdin_buffer[1U * 1024U];
};

}

TUI::TUI(
	const ProcessEnvironment & e,
	bundle_info_map & m,
	TUIDisplayCompositor & comp,
	const TUIOutputBase::Options & options
) :
	TerminalCapabilities(e),
	TUIOutputBase(*this, std::cout, STDOUT_FILENO, options, comp),
	TUIInputBase(static_cast<const TerminalCapabilities &>(*this), std::cin, STDIN_FILENO),
	handler0(*this),
	handler1(*this),
	handler2(*this),
	handler3(*this),
	handler4(*this),
	handler5(*this),
	handler6(*this),
	handler7(*this),
	envs(e),
	terminate_signalled(false),
	interrupt_signalled(false),
	hangup_signalled(false),
	usr1_signalled(false),
	usr2_signalled(false),
	bundle_map(m),
	vio(comp),
	current_row(0U),
	top_row(0U),
	sort_needed(true),
	pending_quit_event(false),
	sort_mode(4U),
	window_x(0U),
	header(C(COLOUR_BLUE, COLOUR_YELLOW)),
	normal(C(COLOUR_WHITE, COLOUR_BLACK)),
	name(C(COLOUR_YELLOW, COLOUR_BLACK)),
	exception(C(COLOUR_RED, COLOUR_BLACK)),
	config(C(COLOUR_CYAN, COLOUR_BLACK)),
	control(C(COLOUR_GREEN, COLOUR_BLACK))
{
}

TUI::~TUI(
) {
}

inline
void
TUI::write_timestamp (
	const TUIDisplayCompositor::coordinate & row,
	long & col,
	const CharacterCell::attribute_type & attr,
	const ColourPair & colour,
	const uint64_t s,
	unsigned width
) {
	char buf[64];
	const struct tm t(convert(envs, s));
	std::size_t len(std::strftime(buf, sizeof buf, "%F %T %z", &t));
	if (len > width) len = width;
	if (len < width)
		vio.PrintNCharsAttr(row, col, attr, normal, ' ', width - len);
	vio.PrintCharStrAttr7Bit(row, col, attr, colour, buf, len);
}

inline
void
TUI::write_header (
	const TUIDisplayCompositor::coordinate & row,
	long col
) {
	const CharacterCell::attribute_type normal_attr(CharacterCell::INVERSE);
	const CharacterCell::attribute_type sorting(CharacterCell::INVERSE|CharacterCell::BOLD);
	vio.PrintFormatted7Bit(row, col, static_cast<std::size_t>(-1), 0U == sort_mode || 1U == sort_mode ? sorting : normal_attr, header, "%-8s", "STATE");
	vio.PrintNCharsAttr(row, col, normal_attr, header, ' ', 1U);
	vio.PrintFormatted7Bit(row, col, static_cast<std::size_t>(-1), normal_attr, header, "%-8s", "ENABLED");
	vio.PrintNCharsAttr(row, col, normal_attr, header, ' ', 1U);
	vio.PrintFormatted7Bit(row, col, static_cast<std::size_t>(-1), normal_attr, header, "%-2s", "WP");
	vio.PrintNCharsAttr(row, col, normal_attr, header, ' ', 1U);
	vio.PrintFormatted7Bit(row, col, static_cast<std::size_t>(-1), 2U == sort_mode || 1U == sort_mode ? sorting : normal_attr, header, "%-7s", "PID");
	vio.PrintNCharsAttr(row, col, normal_attr, header, ' ', 1U);
	vio.PrintFormatted7Bit(row, col, static_cast<std::size_t>(-1), 3U == sort_mode ? sorting : normal_attr, header, "%-25s", "TIME");
	vio.PrintNCharsAttr(row, col, normal_attr, header, ' ', 1U);
	vio.PrintFormatted7Bit(row, col, static_cast<std::size_t>(-1), sorting, header, "%s", "PATH");
}

inline
void
TUI::write_one_line (
	const TUIDisplayCompositor::coordinate & row,
	long col,
	const CharacterCell::attribute_type & attr,
	const bundle & sb,
	const uint64_t z
) {
	vio.PrintFormatted7Bit(row, col, static_cast<std::size_t>(-1), attr, sb.colour_of_state(), "%-8s", sb.name_of_state());
	vio.PrintNCharsAttr(row, col, attr, normal, ' ', 1U);
	vio.PrintFormatted7Bit(row, col, static_cast<std::size_t>(-1), attr, config, "%-8s", sb.initially_up ? "enabled" : "disabled");
	vio.PrintNCharsAttr(row, col, attr, normal, ' ', 1U);
	vio.PrintFormatted7Bit(row, col, static_cast<std::size_t>(-1), attr, control, "%c%c", sb.valid_status() && sb.want_flag ? sb.want_flag : '_', sb.valid_status() && sb.paused ? 'p' : '_');
	vio.PrintNCharsAttr(row, col, attr, normal, ' ', 1U);
	if (sb.valid_status()) {
		if (-1 != sb.pid)
			vio.PrintFormatted7Bit(row, col, static_cast<std::size_t>(-1), attr, normal, "%7u", sb.pid);
		else
			vio.PrintNCharsAttr(row, col, attr, normal, ' ', 7U);
		vio.PrintNCharsAttr(row, col, attr, normal, ' ', 1U);
		write_timestamp(row, col, attr, z < sb.seconds ? exception : normal, sb.seconds, 25U);
	} else {
		vio.PrintNCharsAttr(row, col, attr, normal, ' ', 7U + 25U + 1U);
	}
	vio.PrintNCharsAttr(row, col, attr, normal, ' ', 1U);
	vio.PrintCharStrAttrUTF8(row, col, attr, normal, sb.path.data(), sb.path.length());
	vio.PrintCharStrAttrUTF8(row, col, attr, name, sb.name.data(), sb.name.length());
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

inline
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
TUI::handle_sort_needed (
) {
	if (sort_needed) {
		sort_needed = false;
		sort_bundles();
		set_refresh_needed();
	}
}

void
TUI::handle_stdin (
	int n		///< number of characters available; can be <= 0 erroneously
) {
	for (;;) {
		int l(read(STDIN_FILENO, stdin_buffer, sizeof stdin_buffer));
		if (0 >= l) break;
		HandleInput(stdin_buffer, l);
		if (l >= n) break;
		n -= l;
	}
	BreakInput();
}

void
TUI::handle_signal (
	int signo
) {
	switch (signo) {
		case SIGWINCH:	set_resized(); break;
		case SIGTERM:	terminate_signalled = true; break;
		case SIGINT:	interrupt_signalled = true; break;
		case SIGHUP:	hangup_signalled = true; break;
		case SIGUSR1:	usr1_signalled = true; break;
		case SIGUSR2:	usr2_signalled = true; break;
		case SIGTSTP:	tstp_signal(); break;
		case SIGCONT:	continued(); break;
	}
}

void
TUI::redraw_new (
) {
	timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	const uint64_t z(time_to_tai64(envs, TimeTAndLeap(now.tv_sec, false)));

	// Scroll so that the current row is visible.
	if (top_row > current_row) {
		optimize_scroll_up(top_row - current_row);
		top_row = current_row;
	} else
	if (current_row + 1U >= top_row + c.query_h()) {
		optimize_scroll_down(current_row - c.query_h() - top_row + 1);
		top_row = current_row - c.query_h() + 2U;
	}

	// Find the x cursor position, sideways scrolling as necessary.
	TUIDisplayCompositor::coordinate cursor_x;
	switch (sort_mode) {
		default:
		case 0U:	cursor_x = 0; break;
		case 1U:	cursor_x = 0; break;
		case 2U:	cursor_x = 21; break;
		case 3U:	cursor_x = 29; break;
		case 4U:	cursor_x = 55; break;
	}
	if (window_x > cursor_x) window_x = cursor_x; else if (cursor_x >= window_x + c.query_w()) window_x = cursor_x - c.query_w() + 1U;

	vio.CLSToSpace(ColourPair::def);

	write_header(0U, -window_x);
	std::size_t row(0U);
	for (bundle_pointer_list::const_iterator i(bundles.begin()), e(bundles.end()); e != i; ++i, ++row) {
		if (row < top_row) continue;
		if (row + 1U >= top_row + c.query_h()) break;
		const CharacterCell::attribute_type attr(row == current_row ? CharacterCell::INVERSE : 0U);
		write_one_line(row - top_row + 1U, -window_x, attr, **i, z);
	}

	c.move_cursor(current_row - top_row + 1U, cursor_x - window_x);
	c.set_cursor_state(CursorSprite::VISIBLE|CursorSprite::BLINK, CursorSprite::BOX);
}

bool
TUI::EventHandler::ExtendedKey(
	uint_fast16_t k,
	uint_fast8_t m
) {
	switch (k) {
		default:	return TUIInputBase::EventHandler::ExtendedKey(k, m);
		case EXTENDED_KEY_LEFT_ARROW:
			if (ui.sort_mode > 0U) { --ui.sort_mode; ui.sort_needed = true; }
			return true;
		case EXTENDED_KEY_RIGHT_ARROW:
			if (ui.sort_mode < 4U) { ++ui.sort_mode; ui.sort_needed = true; }
			return true;
		case EXTENDED_KEY_DOWN_ARROW:
			if (ui.current_row + 1 < ui.bundles.size()) { ++ui.current_row; ui.set_refresh_needed(); }
			return true;
		case EXTENDED_KEY_UP_ARROW:
			if (ui.current_row > 0) { --ui.current_row; ui.set_refresh_needed(); }
			return true;
		case EXTENDED_KEY_END:
			if (std::size_t s = ui.bundles.size()) {
				if (ui.current_row + 1 != s) { ui.current_row = s - 1; ui.set_refresh_needed(); }
				return true;
			} else
				[[clang::fallthrough]];
		case EXTENDED_KEY_HOME:
			if (ui.current_row != 0) { ui.current_row = 0; ui.set_refresh_needed(); }
			return true;
		case EXTENDED_KEY_PAGE_DOWN:
			if (ui.bundles.size() && ui.current_row + 1 < ui.bundles.size()) {
				unsigned n(ui.c.query_h() - 1U);
				if (ui.current_row + n < ui.bundles.size())
					ui.current_row += n;
				else
					ui.current_row = ui.bundles.size() - 1;
				ui.set_refresh_needed();
			}
			return true;
		case EXTENDED_KEY_PAGE_UP:
			if (ui.current_row > 0) {
				unsigned n(ui.c.query_h() - 1U);
				if (ui.current_row > n)
					ui.current_row -= n;
				else
					ui.current_row = 0;
				ui.set_refresh_needed();
			}
			return true;
		case EXTENDED_KEY_CANCEL:
		case EXTENDED_KEY_CLOSE:
		case EXTENDED_KEY_EXIT:
			ui.pending_quit_event = true;
			return true;
		case EXTENDED_KEY_STOP:
			ui.suspend_self();
			return true;
		case EXTENDED_KEY_REFRESH:
			ui.invalidate_cur();
			ui.set_update_needed();
			return true;
	}
}

// Seat of the class
TUI::EventHandler::~EventHandler() {}

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
	TUIOutputBase::Options options;
	try {
		popt::bool_definition cursor_application_mode_option('\0', "cursor-keypad-application-mode", "Set the cursor keypad to application mode instead of normal mode.", options.cursor_application_mode);
		popt::bool_definition calculator_application_mode_option('\0', "calculator-keypad-application-mode", "Set the calculator keypad to application mode instead of normal mode.", options.calculator_application_mode);
		popt::bool_definition no_alternate_screen_buffer_option('\0', "no-alternate-screen-buffer", "Prevent switching to the XTerm alternate screen buffer.", options.no_alternate_screen_buffer);
		popt::bool_string_definition scnm_option('\0', "inversescreen", "Switch inverse screen mode on/off.", options.scnm);
		popt::tui_level_definition tui_level_option('T', "tui-level", "Specify the level of TUI character set.");
		popt::definition * tui_table[] = {
			&cursor_application_mode_option,
			&calculator_application_mode_option,
			&no_alternate_screen_buffer_option,
			&scnm_option,
			&tui_level_option,
		};
		popt::bool_definition user_option('u', "user", "Communicate with the per-user manager.", per_user_mode);
		popt::table_definition tui_table_option(sizeof tui_table/sizeof *tui_table, tui_table, "Terminal quirks options");
		popt::definition * main_table[] = {
			&user_option,
			&tui_table_option,
		};
		popt::top_table_definition main_option(sizeof main_table/sizeof *main_table, main_table, "Main options", "{service(s)...}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
		options.tui_level = tui_level_option.value() < options.TUI_LEVELS ? tui_level_option.value() : options.TUI_LEVELS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}
	if (args.empty()) {
		die_missing_argument(prog, envs, "service bundle name(s)");
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

	const FileDescriptorOwner queue(kqueue());
	if (0 > queue.get()) {
		die_errno(prog, envs, "kqueue");
	}
	std::vector<struct kevent> ip;

	append_event(ip, STDIN_FILENO, EVFILT_READ, EV_ADD, 0, 0, nullptr);
	ReserveSignalsForKQueue kqueue_reservation(SIGTERM, SIGINT, SIGHUP, SIGPIPE, SIGUSR1, SIGUSR2, SIGWINCH, SIGTSTP, SIGCONT, 0);
	PreventDefaultForFatalSignals ignored_signals(SIGTERM, SIGINT, SIGHUP, SIGPIPE, SIGUSR1, SIGUSR2, 0);
	append_event(ip, SIGWINCH, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGPIPE, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGTSTP, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGCONT, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);

	TUIDisplayCompositor compositor(false /* no software cursor */, 24, 80);
	TUI ui(envs, bundle_map, compositor, options);

	while (true) {
		if (ui.exit_signalled() || ui.quit_flagged())
			break;
		ui.handle_sort_needed();
		ui.handle_resize_event();
		ui.handle_refresh_event();
		ui.handle_update_event();

		struct kevent p[128];
		const int rc(kevent(queue.get(), ip.data(), ip.size(), p, sizeof p/sizeof *p, nullptr));
		ip.clear();

		if (0 > rc) {
			if (EINTR == errno) continue;
			die_errno(prog, envs, "kevent");
		}

		for (std::size_t i(0); i < static_cast<std::size_t>(rc); ++i) {
			const struct kevent & e(p[i]);
			switch (e.filter) {
				case EVFILT_SIGNAL:
					ui.handle_signal(e.ident);
					break;
				case EVFILT_READ:
					if (STDIN_FILENO == e.ident)
						ui.handle_stdin(e.data);
					break;
			}
		}
	}

	throw EXIT_SUCCESS;
}
