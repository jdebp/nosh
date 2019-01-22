/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define __STDC_FORMAT_MACROS
#define _XOPEN_SOURCE_EXTENDED
#include <vector>
#include <memory>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <clocale>
#include <cerrno>
#include <stdint.h>
#include <sys/stat.h>
#include "kqueue_common.h"
#include <sys/param.h>
#include <unistd.h>
#include <fcntl.h>
#include "utils.h"
#include "fdutils.h"
#include "popt.h"
#include "InputMessage.h"
#include "FileDescriptorOwner.h"
#include "FileStar.h"
#include "SignalManagement.h"
#include "CharacterCell.h"
#include "VirtualTerminalBackEnd.h"
#include "InputFIFO.h"

/* Realizing a virtual terminal *********************************************
// **************************************************************************
*/

namespace {

typedef std::shared_ptr<VirtualTerminalBackEnd> VirtualTerminalPtr;
typedef std::list<VirtualTerminalPtr> VirtualTerminalList;

class Realizer
{
public:
	Realizer(FILE *, VirtualTerminalList &, VirtualTerminalList::iterator &);
	~Realizer();

	void set_refresh_needed() { update_needed = true; }
	bool exit_signalled() const { return terminate_signalled||interrupt_signalled||hangup_signalled; }
	void handle_update_event ();
	void handle_signal (int);
	void handle_input_event (uint32_t);

protected:
	enum { CELL_LENGTH = 16U, HEADER_LENGTH = 16U };

	bool update_needed;
	sig_atomic_t terminate_signalled, interrupt_signalled, hangup_signalled;
	const FileStar buffer_file;
	VirtualTerminalList & vts;
	VirtualTerminalList::iterator & current_vt;

	void paint_all_cells();

private:
};

}

Realizer::Realizer (
	FILE * f,
	VirtualTerminalList & l,
	VirtualTerminalList::iterator & c
) :
	update_needed(true),
	terminate_signalled(false),
	interrupt_signalled(false),
	hangup_signalled(false),
	buffer_file(f),
	vts(l),
	current_vt(c)
{
}

Realizer::~Realizer() {}

inline
void
Realizer::paint_all_cells (
) {
	VirtualTerminalBackEnd & vt(**current_vt);

	// The stdio buffers may well be out of synch, so we need to reset them.
	std::rewind(buffer_file);
	std::fflush(buffer_file);

	uint8_t header[HEADER_LENGTH] = { 
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		static_cast<uint8_t>(vt.query_cursor_glyph()), 
		static_cast<uint8_t>(vt.query_cursor_attributes()), 
		static_cast<uint8_t>(vt.query_pointer_attributes()), 
		0,
	};
	const uint32_t bom(0xFEFF);
	std::memcpy(&header[ 0], &bom, sizeof bom);
	const uint16_t cols(vt.query_w()), rows(vt.query_h());
	std::memcpy(&header[ 4], &cols, sizeof cols);
	std::memcpy(&header[ 6], &rows, sizeof rows);
	const uint16_t x(vt.query_cursor_x()), y(vt.query_cursor_y());
	std::memcpy(&header[ 8], &x, sizeof x);
	std::memcpy(&header[10], &y, sizeof y);
	std::fwrite(header, sizeof header, 1U, buffer_file);

	if (HEADER_LENGTH != ftello(buffer_file))
		std::fseek(buffer_file, HEADER_LENGTH, SEEK_SET);

	for (unsigned row(0); row < rows; ++row) {
		for (unsigned col(0); col < cols; ++col) {
			const CharacterCell & cell(vt.at(row, col));
			unsigned char b[CELL_LENGTH] = { 
				cell.foreground.alpha, cell.foreground.red, cell.foreground.green, cell.foreground.blue, 
				cell.background.alpha, cell.background.red, cell.background.green, cell.background.blue, 
				0, 0, 0, 0, 
				static_cast<uint8_t>(cell.attributes & 0xFF), static_cast<uint8_t>(cell.attributes >> 8U),
				0, 0
			};
			std::memcpy(&b[8], &cell.character, 4);
			std::fwrite(b, sizeof b, 1U, buffer_file);
		}
	}
	std::fflush(buffer_file);
	ftruncate(fileno(buffer_file), ftello(buffer_file));
}

inline
void
Realizer::handle_update_event (
) {
	if (update_needed) {
		update_needed = false;
		paint_all_cells();
	}
}

inline
void
Realizer::handle_signal (
	int signo
) {
	switch (signo) {
		case SIGTERM:	terminate_signalled = true; break;
		case SIGINT:	interrupt_signalled = true; break;
		case SIGHUP:	hangup_signalled = true; break;
	}
}

inline
void
Realizer::handle_input_event(
	uint32_t b
) {
	switch (b & INPUT_MSG_MASK) {
		case INPUT_MSG_SESSION:	
		{
			const uint32_t f((b & 0x00FFFF00) >> 8U);
			VirtualTerminalList::iterator next_vt(vts.begin());
			for (uint16_t i(0U); next_vt != vts.end(); ++i, ++next_vt) {
				if (i >= f) {
					const bool changed(current_vt != next_vt);
					current_vt = next_vt;
					if (changed) set_refresh_needed();
					break;
				}
			}
			break;
		}
		case INPUT_MSG_CKEY:	
		{
			const uint32_t c((b & 0x00FFFF00) >> 8U);
			switch (c) {
				case CONSUMER_KEY_LOCK:
				case CONSUMER_KEY_LOGON:
				case CONSUMER_KEY_LOGOFF:
				case CONSUMER_KEY_HALT_TASK:
				case CONSUMER_KEY_TASK_MANAGER:
					break;
				case CONSUMER_KEY_SELECT_TASK:
					if (current_vt != vts.begin()) {
						current_vt = vts.begin();
						set_refresh_needed();
					}
					break;
				case CONSUMER_KEY_NEXT_TASK:
				{
					VirtualTerminalList::iterator next_vt(current_vt);
					++next_vt;
					if (next_vt != vts.end()) {
						current_vt = next_vt;
						set_refresh_needed();
					}
					break;
				}
				case CONSUMER_KEY_PREVIOUS_TASK:
					if (current_vt != vts.begin()) {
						--current_vt;
						set_refresh_needed();
					}
					break;
				default:	
					(*current_vt)->WriteInputMessage(b);
					break;
			}
			break;
		}
		default:
			(*current_vt)->WriteInputMessage(b);
			break;
	}
}

/* Main function ************************************************************
// **************************************************************************
*/

void
console_multiplexor [[gnu::noreturn]] ( 
	const char * & /*next_prog*/,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	bool display_only(false);

	try {
		popt::bool_definition display_only_option('\0', "display-only", "Only render the display; do not send input.", display_only);
		popt::definition * top_table[] = {
			&display_only_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{vc-multiplexed} {virtual-terminal(s)...}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw static_cast<int>(EXIT_USAGE);
	}

	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing multiplexor virtual terminal directory name.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * dirname(args.front());
	args.erase(args.begin());

	const int queue(kqueue());
	if (0 > queue) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kqueue", std::strerror(error));
		throw EXIT_FAILURE;
	}
	std::vector<struct kevent> ip;

	FileDescriptorOwner upper_vt_dir_fd(open_dir_at(AT_FDCWD, dirname));
	if (0 > upper_vt_dir_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, dirname, std::strerror(error));
		throw EXIT_FAILURE;
	}

	// We need an explicit lock file, because we cannot lock FIFOs.
	FileDescriptorOwner lock_fd(open_lockfile_at(upper_vt_dir_fd.get(), "lock"));
	if (0 > lock_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "lock", std::strerror(error));
		throw EXIT_FAILURE;
	}
	// We are allowed to open the read end of a FIFO in non-blocking mode without having to wait for a writer.
	mkfifoat(upper_vt_dir_fd.get(), "input", 0620);
	InputFIFO upper_input_fifo(open_read_at(upper_vt_dir_fd.get(), "input"));
	if (0 > upper_input_fifo.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "input", std::strerror(error));
		throw EXIT_FAILURE;
	}
	if (0 > fchown(upper_input_fifo.get(), -1, getegid())) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "input", std::strerror(error));
		throw EXIT_FAILURE;
	}
	// We have to keep a client (write) end descriptor open to the input FIFO.
	// Otherwise, the first console client process triggers POLLHUP when it closes its end.
	// Opening the FIFO for read+write isn't standard, although it would work on Linux.
	FileDescriptorOwner upper_input_write_fd(open_writeexisting_at(upper_vt_dir_fd.get(), "input"));
	if (0 > upper_input_write_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "input", std::strerror(error));
		throw EXIT_FAILURE;
	}
	FileDescriptorOwner upper_buffer_fd(open_writecreate_at(upper_vt_dir_fd.get(), "display", 0640));
	if (0 > upper_buffer_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "display", std::strerror(error));
		throw EXIT_FAILURE;
	}
	if (0 > fchown(upper_buffer_fd.get(), -1, getegid())) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "display", std::strerror(error));
		throw EXIT_FAILURE;
	}
	FileStar upper_buffer_file(fdopen(upper_buffer_fd.get(), "w"));
	if (!upper_buffer_file) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "display", std::strerror(error));
		throw EXIT_FAILURE;
	}
	upper_buffer_fd.release();

	char display_stdio_buffer[128U * 1024U];
	std::setvbuf(upper_buffer_file, display_stdio_buffer, _IOFBF, sizeof display_stdio_buffer);

	VirtualTerminalList vts;

	for (std::vector<const char *>::const_iterator i(args.begin()); i != args.end(); ++i) {
		const char * name(*i);
		FileDescriptorOwner vt_dir_fd(open_dir_at(AT_FDCWD, name));
		if (0 > vt_dir_fd.get()) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, name, std::strerror(error));
			throw EXIT_FAILURE;
		}
		FileDescriptorOwner vt_buffer_fd(open_read_at(vt_dir_fd.get(), "display"));
		if (0 > vt_buffer_fd.get()) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, name, "display", std::strerror(error));
			throw EXIT_FAILURE;
		}
		FileStar vt_buffer_file(fdopen(vt_buffer_fd.get(), "r"));
		if (!vt_buffer_file) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, name, "display", std::strerror(error));
			throw EXIT_FAILURE;
		}
		vt_buffer_fd.release();
		FileDescriptorOwner vt_input_fd(display_only ? -1 : open_writeexisting_at(vt_dir_fd.get(), "input"));
		if (!display_only && vt_input_fd.get() < 0) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, name, "input", std::strerror(error));
			throw EXIT_FAILURE;
		}
		vt_dir_fd.release();

		vts.push_back(VirtualTerminalPtr(new VirtualTerminalBackEnd(name, vt_buffer_file.release(), vt_input_fd.release())));
	}

	if (vts.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing VT list.");
		throw static_cast<int>(EXIT_USAGE);
	}

	append_event(ip, upper_input_fifo.get(), EVFILT_READ, EV_ADD, 0, 0, 0);
	ReserveSignalsForKQueue kqueue_reservation(SIGTERM, SIGINT, SIGHUP, SIGPIPE, 0);
	PreventDefaultForFatalSignals ignored_signals(SIGTERM, SIGINT, SIGHUP, SIGPIPE, 0);
	append_event(ip, SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	append_event(ip, SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	append_event(ip, SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	append_event(ip, SIGPIPE, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);

	for (VirtualTerminalList::const_iterator t(vts.begin()); t != vts.end(); ++t) {
		const VirtualTerminalBackEnd & lower_vt(**t);
		append_event(ip, lower_vt.query_buffer_fd(), EVFILT_VNODE, EV_ADD|EV_DISABLE|EV_CLEAR, NOTE_WRITE, 0, 0);
		append_event(ip, lower_vt.query_input_fd(), EVFILT_WRITE, EV_ADD|EV_DISABLE, 0, 0, 0);
	}

	VirtualTerminalList::iterator current_vt(vts.begin());
	VirtualTerminalList::iterator old_vt(vts.end());
	Realizer realizer(upper_buffer_file.release(), vts, current_vt);

	// Each vt could need a FIFO enable/disable, plus 2 potential session switch enables/disables.
	std::size_t max_events(vts.size() * 2 + 4);
	// We also want to grab a bunch of input events in a single gulp.
	if (max_events < 256) max_events = 256;
	std::vector<struct kevent> p(max_events);

	const struct timespec immediate_timeout = { 0, 0 };

	while (true) {
		if (realizer.exit_signalled())
			break;
		realizer.handle_update_event();
		if (old_vt != current_vt) {
			const VirtualTerminalBackEnd & cvt(**current_vt);
			symlinkat(cvt.query_dir_name(), upper_vt_dir_fd.get(), "active.new");
			renameat(upper_vt_dir_fd.get(), "active.new", upper_vt_dir_fd.get(), "active");

			for (VirtualTerminalList::iterator t(vts.begin()); t != vts.end(); ++t) {
				const VirtualTerminalBackEnd & lower_vt(**t);
				if (t == current_vt)
					append_event(ip, lower_vt.query_buffer_fd(), EVFILT_VNODE, EV_ENABLE, NOTE_WRITE, 0, 0);
				if (t == old_vt)
					append_event(ip, lower_vt.query_buffer_fd(), EVFILT_VNODE, EV_DISABLE, NOTE_WRITE, 0, 0);
			}

			old_vt = current_vt;
		}

		const int rc(kevent(queue, ip.data(), ip.size(), p.data(), p.size(), (*current_vt)->query_reload_needed() ? &immediate_timeout : 0));
		ip.clear();

		if (0 > rc) {
			const int error(errno);
			if (EINTR == error) continue;
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "poll", std::strerror(error));
			throw EXIT_FAILURE;
		}

		if (0 == rc) {
			VirtualTerminalBackEnd & lower_vt(**current_vt);
			if (lower_vt.query_reload_needed()) {
				lower_vt.reload();
				realizer.set_refresh_needed();
			}
			continue;
		}

		for (size_t i(0); i < static_cast<size_t>(rc); ++i) {
			const struct kevent & e(p[i]);
			switch (e.filter) {
			case EVFILT_VNODE:
				if ((*current_vt)->query_buffer_fd() == static_cast<int>(e.ident))
					(*current_vt)->set_reload_needed();
				break;
			case EVFILT_SIGNAL:
				realizer.handle_signal(e.ident);
				break;
			case EVFILT_READ:
				if (upper_input_fifo.get() == static_cast<int>(e.ident))
					upper_input_fifo.ReadInput();
				break;
			case EVFILT_WRITE:
				for (VirtualTerminalList::iterator t(vts.begin()); t != vts.end(); ++t) {
					VirtualTerminalBackEnd & lower_vt(**t);
					if (lower_vt.query_input_fd() == static_cast<int>(e.ident))
						lower_vt.FlushMessages();
				}
				break;
			}
		}

		while (upper_input_fifo.HasMessage())
			realizer.handle_input_event(upper_input_fifo.PullMessage());
		for (VirtualTerminalList::iterator t(vts.begin()); t != vts.end(); ++t) {
			VirtualTerminalBackEnd & lower_vt(**t);
			if (lower_vt.MessageAvailable()) {
				if (!lower_vt.query_polling_for_write()) {
					append_event(ip, lower_vt.query_input_fd(), EVFILT_WRITE, EV_ENABLE, 0, 0, 0);
					lower_vt.set_polling_for_write(true);
				}
			} else {
				if (lower_vt.query_polling_for_write()) {
					append_event(ip, lower_vt.query_input_fd(), EVFILT_WRITE, EV_DISABLE, 0, 0, 0);
					lower_vt.set_polling_for_write(false);
				}
			}
		}
	}

	unlinkat(upper_vt_dir_fd.get(), "active", 0);
	throw EXIT_SUCCESS;
}
