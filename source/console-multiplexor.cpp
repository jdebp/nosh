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

/* Signal handling **********************************************************
// **************************************************************************
*/

static sig_atomic_t terminate_signalled(false), interrupt_signalled(false), hangup_signalled(false);

static
void
handle_signal (
	int signo
) {
	switch (signo) {
		case SIGTERM:	terminate_signalled = true; break;
		case SIGINT:	interrupt_signalled = true; break;
		case SIGHUP:	hangup_signalled = true; break;
	}
}

/* A virtual terminal back-end **********************************************
// **************************************************************************
*/

namespace {
class VirtualTerminal 
{
public:
	typedef unsigned short coordinate;
	VirtualTerminal(bool display_only, const char * prog, const char * dirname, int dir_fd);
	~VirtualTerminal();

	int query_buffer_fd() const { return buffer_fd; }
	const char * query_dir_name() const { return dir_name; }
	int query_input_fd() const { return input_fd.get(); }
	FILE * query_buffer_file() const { return buffer_file; }
	void WriteInputMessage(uint32_t);
	bool MessageAvailable() const { return message_pending > 0U; }
	void FlushMessages();
	bool query_polling_for_write() const { return polling_for_write; }
	void set_polling_for_write(bool v) { polling_for_write = v; }

protected:
	VirtualTerminal(const VirtualTerminal & c);

	FileDescriptorOwner dir_fd;
	const char * dir_name;
	FileDescriptorOwner input_fd;
	char display_stdio_buffer[128U * 1024U];
	const int buffer_fd;
	FILE * const buffer_file;
	char message_buffer[4096];
	std::size_t message_pending;
	bool polling_for_write;
};
}

VirtualTerminal::VirtualTerminal(
	bool display_only,
	const char * prog, 
	const char * dirname, 
	int d
) :
	dir_fd(d),
	dir_name(dirname),
	input_fd(display_only ? -1 : open_writeexisting_at(dir_fd.get(), "input")),
	buffer_fd(open_read_at(dir_fd.get(), "display")),
	buffer_file(buffer_fd < 0 ? 0 : fdopen(buffer_fd, "r")),
	message_pending(0U),
	polling_for_write(false)
{
	if (buffer_fd < 0) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "display", std::strerror(error));
		throw EXIT_FAILURE;
	}
	if (!display_only && 0 > input_fd.get()) {
		const int error(errno);
		close(buffer_fd);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "input", std::strerror(error));
		throw EXIT_FAILURE;
	}
	if (!buffer_file) {
		const int error(errno);
		close(buffer_fd);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "display", std::strerror(error));
		throw EXIT_FAILURE;
	}
	std::setvbuf(buffer_file, display_stdio_buffer, _IOFBF, sizeof display_stdio_buffer);
}

VirtualTerminal::~VirtualTerminal()
{
	std::fclose(buffer_file);
}

void 
VirtualTerminal::WriteInputMessage(uint32_t m)
{
	if (sizeof m > (sizeof message_buffer - message_pending)) return;
	std::memmove(message_buffer + message_pending, &m, sizeof m);
	message_pending += sizeof m;
}

void
VirtualTerminal::FlushMessages()
{
	const ssize_t l(write(input_fd.get(), message_buffer, message_pending));
	if (l > 0) {
		std::memmove(message_buffer, message_buffer + l, message_pending - l);
		message_pending -= l;
	}
}

typedef std::shared_ptr<VirtualTerminal> VirtualTerminalPtr;
typedef std::list<VirtualTerminalPtr> VirtualTerminalList;

/* input FIFO ***************************************************************
// **************************************************************************
*/

class InputFIFO :
	public FileDescriptorOwner
{
public:
	InputFIFO(int);
	void ReadInput();
	bool HasMessage() const { return input_read >= sizeof(uint32_t); }
	uint32_t PullMessage();
protected:
	char input_buffer[4];
	std::size_t input_read;
};

InputFIFO::InputFIFO(int i) : 
	FileDescriptorOwner(i),
	input_read(0U)
{
}

void
InputFIFO::ReadInput()
{
	const ssize_t l(read(fd, input_buffer + input_read, sizeof input_buffer - input_read));
	if (l > 0)
		input_read += l;
}

uint32_t
InputFIFO::PullMessage()
{
	uint32_t b(0);
	if (input_read >= sizeof b) {
		std::memcpy(&b, input_buffer, sizeof b);
		input_read -= sizeof b;
		std::memmove(input_buffer, input_buffer + sizeof b, input_read);
	}
	return b;
}

/* Support routines  ********************************************************
// **************************************************************************
*/

enum { CELL_LENGTH = 16U, HEADER_LENGTH = 16U };

static inline
void
handle_input_event(
	bool & update_needed,
	InputFIFO & input_fd,
	VirtualTerminalList & vts,
	VirtualTerminalList::iterator & current_vt
) {
	input_fd.ReadInput();
	while (input_fd.HasMessage()) {
		const uint32_t b(input_fd.PullMessage());
		switch (b & INPUT_MSG_MASK) {
			case INPUT_MSG_SESSION:	
			{
				const uint32_t f((b & 0x00FFFF00) >> 8U);
				VirtualTerminalList::iterator next_vt(vts.begin());
				for (uint16_t i(0U); next_vt != vts.end(); ++i, ++next_vt) {
					if (i >= f) {
						const bool changed(current_vt != next_vt);
						current_vt = next_vt;
						update_needed |= changed;
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
							update_needed = true;
						}
						break;
					case CONSUMER_KEY_NEXT_TASK:
					{
						VirtualTerminalList::iterator next_vt(current_vt);
						++next_vt;
						if (next_vt != vts.end()) {
							current_vt = next_vt;
							update_needed = true;
						}
						break;
					}
					case CONSUMER_KEY_PREVIOUS_TASK:
						if (current_vt != vts.begin()) {
							--current_vt;
							update_needed = true;
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
}

static inline
void
copy (
	const VirtualTerminal & vt,
	FILE * const buffer_file
) {
	// The stdio buffers may well be out of synch, so we need to reset them.
	std::rewind(buffer_file);
	std::rewind(vt.query_buffer_file());
	std::fflush(buffer_file);
	std::fflush(vt.query_buffer_file());

	uint8_t header0[4] = { 0, 0, 0, 0 };
	std::fread(header0, sizeof header0, 1U, vt.query_buffer_file());
	std::fwrite(header0, sizeof header0, 1U, buffer_file);
	uint16_t header1[4] = { 0, 0, 0, 0 };
	std::fread(header1, sizeof header1, 1U, vt.query_buffer_file());
	std::fwrite(header1, sizeof header1, 1U, buffer_file);
	uint8_t header2[4] = { 0, 0, 0, 0 };
	std::fread(header2, sizeof header2, 1U, vt.query_buffer_file());
	std::fwrite(header2, sizeof header2, 1U, buffer_file);

	// Don't fseek() if we can avoid it; it causes duplicate VERY LARGE reads to re-fill the stdio buffer.
	if (HEADER_LENGTH != ftello(vt.query_buffer_file()))
		std::fseek(vt.query_buffer_file(), HEADER_LENGTH, SEEK_SET);
	std::fseek(buffer_file, HEADER_LENGTH, SEEK_SET);

	const unsigned cols(header1[0]), rows(header1[1]);
	for (unsigned row(0); row < rows; ++row) {
		for (unsigned col(0); col < cols; ++col) {
			unsigned char b[CELL_LENGTH] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
			std::fread(b, sizeof b, 1U, vt.query_buffer_file());
			std::fwrite(b, sizeof b, 1U, buffer_file);
		}
	}
	ftruncate(fileno(buffer_file), ftello(vt.query_buffer_file()));
	std::fflush(buffer_file);
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
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "vc-multiplexed virtual-terminals...");

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

	FileDescriptorOwner dir_fd(open_dir_at(AT_FDCWD, dirname));
	if (0 > dir_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, dirname, std::strerror(error));
		throw EXIT_FAILURE;
	}

	// We need an explicit lock file, because we cannot lock FIFOs.
	FileDescriptorOwner lock_fd(open_lockfile_at(dir_fd.get(), "lock"));
	if (0 > lock_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "lock", std::strerror(error));
		throw EXIT_FAILURE;
	}
	// We are allowed to open the read end of a FIFO in non-blocking mode without having to wait for a writer.
	mkfifoat(dir_fd.get(), "input", 0620);
	InputFIFO input(open_read_at(dir_fd.get(), "input"));
	if (0 > input.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "input", std::strerror(error));
		throw EXIT_FAILURE;
	}
	// We have to keep a client (write) end descriptor open to the input FIFO.
	// Otherwise, the first console client process triggers POLLHUP when it closes its end.
	// Opening the FIFO for read+write isn't standard, although it would work on Linux.
	FileDescriptorOwner input_write_fd(open_writeexisting_at(dir_fd.get(), "input"));
	if (0 > input_write_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "input", std::strerror(error));
		throw EXIT_FAILURE;
	}
	FileDescriptorOwner buffer_fd(open_writecreate_at(dir_fd.get(), "display", 0640));
	if (0 > buffer_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "display", std::strerror(error));
		throw EXIT_FAILURE;
	}
	FileStar const buffer_file(fdopen(buffer_fd.get(), "w"));
	if (!buffer_file) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "display", std::strerror(error));
		throw EXIT_FAILURE;
	}
	buffer_fd.release();

	char display_stdio_buffer[128U * 1024U];
	std::setvbuf(buffer_file, display_stdio_buffer, _IOFBF, sizeof display_stdio_buffer);

	VirtualTerminalList vts;
	
	for (std::vector<const char *>::const_iterator i(args.begin()); i != args.end(); ++i) {
		const char * name(*i);
		const int vt_dir_fd(open_dir_at(AT_FDCWD, name));
		if (0 > vt_dir_fd) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, name, std::strerror(error));
			throw EXIT_FAILURE;
		}
		vts.push_back(VirtualTerminalPtr(new VirtualTerminal(display_only, prog, name, vt_dir_fd)));
	}

	if (vts.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing VT list.");
		throw static_cast<int>(EXIT_USAGE);
	}

	VirtualTerminalList::iterator current_vt(vts.begin());

	ReserveSignalsForKQueue kqueue_reservation(SIGTERM, SIGINT, SIGHUP, SIGPIPE, 0);
	PreventDefaultForFatalSignals ignored_signals(SIGTERM, SIGINT, SIGHUP, SIGPIPE, 0);

	const int queue(kqueue());
	if (0 > queue) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kqueue", std::strerror(error));
		throw EXIT_FAILURE;
	}

	{
		std::vector<struct kevent> p(vts.size() * 2 + 16);
		std::size_t index(0U);
		set_event(&p[index++], input.get(), EVFILT_READ, EV_ADD, 0, 0, 0);
		set_event(&p[index++], SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		set_event(&p[index++], SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		set_event(&p[index++], SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		set_event(&p[index++], SIGPIPE, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		for (VirtualTerminalList::const_iterator t(vts.begin()); t != vts.end(); ++t) {
			const VirtualTerminal & vt(**t);
			set_event(&p[index++], vt.query_buffer_fd(), EVFILT_VNODE, EV_ADD|EV_DISABLE|EV_CLEAR, NOTE_WRITE, 0, 0);
			set_event(&p[index++], vt.query_input_fd(), EVFILT_WRITE, EV_ADD|EV_DISABLE, 0, 0, 0);
		}
		if (0 > kevent(queue, p.data(), index, 0, 0, 0)) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kevent", std::strerror(error));
			throw EXIT_FAILURE;
		}
	}

	bool update_needed(true);

	VirtualTerminalList::iterator old_vt(vts.end());

	// Each vt could need a FIFO enable/disable, plus 2 potential session switch enables/disables.
	std::size_t max_events(vts.size() * 2 + 4);
	// We also want to grab a bunch of input events in a single gulp.
	if (max_events < 2096) max_events = 4096;
	std::vector<struct kevent> p(max_events);
	std::size_t index(0U);

	const struct timespec immediate_timeout = { 0, 0 };

	while (true) {
		if (terminate_signalled||interrupt_signalled||hangup_signalled) 
			break;
		if (old_vt != current_vt) {
			const VirtualTerminal & cvt(**current_vt);
			symlinkat(cvt.query_dir_name(), dir_fd.get(), "active.new");
			renameat(dir_fd.get(), "active.new", dir_fd.get(), "active");

			for (VirtualTerminalList::iterator t(vts.begin()); t != vts.end(); ++t) {
				const VirtualTerminal & vt(**t);
				if (t == current_vt)
					set_event(&p[index++], vt.query_buffer_fd(), EVFILT_VNODE, EV_ENABLE, NOTE_WRITE, 0, 0);
				if (t == old_vt)
					set_event(&p[index++], vt.query_buffer_fd(), EVFILT_VNODE, EV_DISABLE, NOTE_WRITE, 0, 0);
			}

			old_vt = current_vt;
		}

		const int rc(kevent(queue, p.data(), index, p.data(), p.size(), update_needed ? &immediate_timeout : 0));

		if (0 > rc) {
			const int error(errno);
			if (EINTR == error) continue;
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "poll", std::strerror(error));
			throw EXIT_FAILURE;
		}

		index = 0;

		if (0 == rc) {
			if (update_needed) {
				update_needed = false;
				const VirtualTerminal & vt(**current_vt);
				copy(vt, buffer_file);
			}
			continue;
		}

		for (size_t i(0); i < static_cast<size_t>(rc); ++i) {
			const struct kevent & e(p[i]);
			if (EVFILT_VNODE == e.filter) {
				if ((*current_vt)->query_buffer_fd() == static_cast<int>(e.ident))
					update_needed = true;
			}
			if (EVFILT_SIGNAL == e.filter)
				handle_signal(e.ident);
			if (EVFILT_READ == e.filter) {
				if (input.get() == static_cast<int>(e.ident))
					handle_input_event(update_needed, input, vts, current_vt);
			}
			if (EVFILT_WRITE == e.filter) {
				for (VirtualTerminalList::iterator t(vts.begin()); t != vts.end(); ++t) {
					VirtualTerminal & vt(**t);
					if (vt.query_input_fd() == static_cast<int>(e.ident))
						vt.FlushMessages();
				}
			}
		}

		for (VirtualTerminalList::iterator t(vts.begin()); t != vts.end(); ++t) {
			VirtualTerminal & vt(**t);
			if (vt.MessageAvailable()) {
				if (!vt.query_polling_for_write()) {
					set_event(&p[index++], vt.query_input_fd(), EVFILT_WRITE, EV_ENABLE, 0, 0, 0);
					vt.set_polling_for_write(true);
				}
			} else {
				if (vt.query_polling_for_write()) {
					set_event(&p[index++], vt.query_input_fd(), EVFILT_WRITE, EV_DISABLE, 0, 0, 0);
					vt.set_polling_for_write(false);
				}
			}
		}
	}

	unlinkat(dir_fd.get(), "active", 0);
	throw EXIT_SUCCESS;
}
