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
#include <sys/event.h>
#include <sys/param.h>
#include <unistd.h>
#include <fcntl.h>
#include "utils.h"
#include "fdutils.h"
#include "popt.h"
#include "InputMessage.h"
#include "FileDescriptorOwner.h"

/* Signal handling **********************************************************
// **************************************************************************
*/

static sig_atomic_t terminate_signalled(false), interrupt_signalled(false), hangup_signalled(false), update_needed(false);

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

protected:
	VirtualTerminal(const VirtualTerminal & c);

	FileDescriptorOwner dir_fd;
	const char * dir_name;
	FileDescriptorOwner input_fd;
	const int buffer_fd;
	FILE * const buffer_file;
	char message_buffer[4096];
	std::size_t message_pending;
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
	message_pending(0U)
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
	const int l(write(input_fd.get(), message_buffer, message_pending));
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
	const int l(read(fd, input_buffer + input_read, sizeof input_buffer - input_read));
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
						current_vt = next_vt;
						update_needed = true;
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
						current_vt = vts.begin();
						update_needed = true;
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
	VirtualTerminal & vt,
	FILE * const buffer_file
) {
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
console_multiplexor ( 
	const char * & /*next_prog*/,
	std::vector<const char *> & args
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
		throw EXIT_FAILURE;
	}
	const char * dirname(args.front());
	args.erase(args.begin());

	FileDescriptorOwner dir_fd(open_dir_at(AT_FDCWD, dirname));
	if (0 > dir_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, dirname, std::strerror(error));
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
	FILE * const buffer_file(fdopen(buffer_fd.get(), "w"));
	if (!buffer_file) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "display", std::strerror(error));
		throw EXIT_FAILURE;
	}
	buffer_fd.release();

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

#if !defined(__LINUX__) && !defined(__linux__)
	struct sigaction sa;
	sa.sa_flags=0;
	sigemptyset(&sa.sa_mask);
	// We only need to set handlers for those signals that would otherwise directly terminate the process.
	sa.sa_handler=SIG_IGN;
	sigaction(SIGTERM,&sa,NULL);
	sigaction(SIGINT,&sa,NULL);
	sigaction(SIGHUP,&sa,NULL);
	sigaction(SIGPIPE,&sa,NULL);
#endif

	const int queue(kqueue());
	if (0 > queue) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kqueue", std::strerror(error));
		throw EXIT_FAILURE;
	}

	struct kevent p[16];
	{
		std::size_t index(0U);
		EV_SET(&p[index++], input.get(), EVFILT_READ, EV_ADD, 0, 0, 0);
		EV_SET(&p[index++], SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		EV_SET(&p[index++], SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		EV_SET(&p[index++], SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		EV_SET(&p[index++], SIGPIPE, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		if (0 > kevent(queue, p, index, 0, 0, 0)) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kevent", std::strerror(error));
			throw EXIT_FAILURE;
		}
	}
	for (VirtualTerminalList::const_iterator t(vts.begin()); t != vts.end(); ++t) {
		const VirtualTerminal & vt(**t);
		std::size_t index(0U);
		EV_SET(&p[index++], vt.query_buffer_fd(), EVFILT_VNODE, EV_ADD|EV_CLEAR, NOTE_WRITE, 0, 0);
		EV_SET(&p[index++], vt.query_input_fd(), EVFILT_WRITE, EV_ADD, 0, 0, 0);
		if (0 > kevent(queue, p, index, 0, 0, 0)) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kevent", std::strerror(error));
			throw EXIT_FAILURE;
		}
	}

	update_needed = true;

	while (true) {
		if (terminate_signalled||interrupt_signalled||hangup_signalled) 
			break;
		if (update_needed) {
			update_needed = false;
			VirtualTerminal & vt(**current_vt);
			copy(vt, buffer_file);
			symlinkat(vt.query_dir_name(), dir_fd.get(), "active.new");
			renameat(dir_fd.get(), "active.new", dir_fd.get(), "active");
		}

		for (VirtualTerminalList::iterator t(vts.begin()); t != vts.end(); ++t) {
			const VirtualTerminal & vt(**t);
			std::size_t index(0U);
			EV_SET(&p[index++], vt.query_input_fd(), EVFILT_WRITE, vt.MessageAvailable() ? EV_ENABLE : EV_DISABLE, 0, 0, 0);
			if (0 > kevent(queue, p, index, 0, 0, 0)) {
				const int error(errno);
#if defined(__LINUX__) || defined(__linux__)
				if (ENOENT == error && !vt.MessageAvailable()) continue;	// This works around a Linux bug when disabling a disabled event.
#endif
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kevent", std::strerror(error));
				throw EXIT_FAILURE;
			}
		}

		const int rc(kevent(queue, p, 0, p, sizeof p/sizeof *p, 0));

		if (0 > rc) {
			const int error(errno);
			if (EINTR == error) continue;
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "poll", std::strerror(error));
			throw EXIT_FAILURE;
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
					handle_input_event(input, vts, current_vt);
			}
			if (EVFILT_WRITE == e.filter) {
				for (VirtualTerminalList::iterator t(vts.begin()); t != vts.end(); ++t) {
					VirtualTerminal & vt(**t);
					if (vt.query_input_fd() == static_cast<int>(e.ident))
						vt.FlushMessages();
				}
			}
		}
	}

	unlinkat(dir_fd.get(), "active", 0);
	throw EXIT_SUCCESS;
}
