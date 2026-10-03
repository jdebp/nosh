/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define _BSD_SOURCE 1
#include <set>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <cerrno>
#include <sys/types.h>
#include "kqueue_common.h"
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>
#include "popt.h"
#include "utils.h"
#if defined(__LINUX__) || defined(__linux__)
#include "fdutils.h"
#endif
#include "ttyutils.h"
#include "SignalManagement.h"
#include "FileDescriptorOwner.h"
#include "UTF8Encoder.h"
#include "UTF8Decoder.h"
#include "VisEncoder.h"
#include "ControlCharacters.h"

/* Line discipline utilities ************************************************
// **************************************************************************
*/

namespace {

enum { NETWORK_IN_FILENO = 6, NETWORK_OUT_FILENO = 7 };

termios original_out_attr, original_in_attr;

inline
termios
enable_input_crfix (
	const termios & ti
) {
	termios t(ti);
	t.c_iflag |= ICRNL;
	return t;
}

inline
termios
enable_output_crfix (
	const termios & ti
) {
	termios t(ti);
	t.c_oflag |= OPOST|ONLCR;
	t.c_oflag &= ~(OCRNL
#if defined(ONOEOT)
			|ONOEOT
#endif
			|ONOCR|ONLRET);
	return t;
}

inline
void
save_attributes()
{
	if (0 <= tcgetattr_nointr(STDIN_FILENO, original_in_attr))
		tcsetattr_nointr(STDIN_FILENO, TCSADRAIN, enable_input_crfix(disable_canonical_software_processing(original_in_attr)));
	if (0 <= tcgetattr_nointr(STDOUT_FILENO, original_out_attr))
		// We don't want to disable canonical input processing if only standard output is a terminal.
		// We just want the line discipline to do the CR stuffing for us.
		tcsetattr_nointr(STDOUT_FILENO, TCSADRAIN, enable_output_crfix(original_out_attr));
}

inline
void
restore_attributes()
{
	tcsetattr_nointr(STDIN_FILENO, TCSADRAIN, original_in_attr);
	tcsetattr_nointr(STDOUT_FILENO, TCSADRAIN, original_out_attr);
}

}

/* Unicode Network Virtual Terminal *****************************************
// **************************************************************************
*/

namespace {

class UnicodeNetworkVirtualTerminal
{
public:
	struct Options {
		Options() : no_remote_echo(false), local_echo(false), pass_through_overlength_remote_data(false) {}
		bool no_remote_echo;
		bool local_echo;
		bool pass_through_overlength_remote_data;
	};
	UnicodeNetworkVirtualTerminal(const Options &);
	~UnicodeNetworkVirtualTerminal() {}

	bool done() const { return network_in_eof && (network_out_eof || !local_output.query_pending()); }
	void handle_local_in(const struct kevent &);
	bool query_local_in_eof() const { return local_in_eof; }
	void handle_local_out(int fileno) { local_output.try_flush(fileno); }
	bool query_local_out_pending() const { return local_output.query_pending(); }
	bool query_local_out_full() const { return local_output.query_full(); }
	void handle_network_in(const struct kevent &);
	bool query_network_in_eof() const { return network_in_eof; }
	void handle_network_out(const struct kevent &);
	bool query_network_out_pending() const { return network_output.query_pending(); }
	bool query_network_out_shutdown_pending() const { return network_out_shutdown_pending; }
	bool query_network_out_eof() const { return network_out_eof; }
	void set_network_out_shutdown_done() { network_out_shutdown_pending = false; }
	bool query_network_out_full() const { return network_output.query_full(); }
	void send_window_size(int fileno);

	void ack(unsigned char option);
	void nak(unsigned char option);
	void request(unsigned char option, bool);
	void demand(unsigned char option, bool);

	static sig_atomic_t window_resized;
protected:
	const Options & options;
	bool network_in_eof, local_in_eof, network_out_shutdown_pending, network_out_eof, remote_echo, rfc1073_naws;
	enum { READ_BUF_SIZE = 4096 };	// Big enough to gulp a 80-by-50 screenfull of text in one go.
	enum { SE = 0xF0, NOP = 0xF1, BRK = 0xF3, IP = 0xF4, AO = 0xF5, AYT = 0xF6, EC = 0xF7, AL = 0xF8, GA = 0xF9, SB = 0xFA, WILL = 0xFB, WILL_NOT = 0xFC, DO = 0xFD, DO_NOT = 0xFE, IAC = 0xFF };
	enum { RFC856_BINARY = 0, RFC857_ECHO = 1, RFC858_ZAPGA = 3, RFC859_STATUS = 5, RFC860_MARK = 6, RFC1091_TERM = 24, RFC1073_NAWS = 31, RFC1079_SPEED = 32, RFC1372_TOGGFLOW = 33, RFC1184_LINEMODE = 34, RFC1096_XDISPLOC = 35, RFC1408_ENV = 36, RFC1572_NEWENV = 39 };

	class Buffer
	{
	public:
		Buffer() : pending(0U) { std::fill(buffer, buffer + sizeof buffer, '\xDA'); }
		~Buffer() {}
		bool query_pending() const { return pending > 0U; }
		bool query_full() const { return pending >= sizeof buffer; }
		void try_flush(int fileno);
	protected:
		// The buffer must be capable of holding both:
		// * a vis(3) encoding of a full READ_BUF_SIZE buffer; and
		// * a TELNET escaping of a full READ_BUF_SIZE buffer.
		unsigned char buffer[READ_BUF_SIZE * 8U];
		std::size_t pending;
		void append_raw(std::size_t l, const unsigned char * p);
	};
	class BufferWithSanitizedUTF8 :
		public UTF8Encoder::UTF8CharacterSink,
		public UTF8Decoder::UCS32CharacterSink,
		public Buffer
	{
	public:
		BufferWithSanitizedUTF8(bool pte) : pass_through_errors(pte), utf8encoder(*this), utf8decoder(*this) {}
		~BufferWithSanitizedUTF8() {}
	protected:
		const bool pass_through_errors;
		void take(unsigned char c) { utf8decoder.Process(c); }
		virtual void append(std::size_t l, const unsigned char * p) { append_raw(l, p); }
		UTF8Encoder utf8encoder;
		/// \name Concrete UTF-8 decoder sink.
		/// Our implementation of UTF8Decoder::UCS32CharacterSink
		/// @{
		virtual void ProcessDecodedUTF8(char32_t character, bool decoder_error, bool overlong);
		/// @}
		UTF8Decoder utf8decoder;
		/// \name Concrete UTF-8 encoder sink.
		/// Our implementation of UTF8Encoder::UTF8CharacterSink
		/// @{
		virtual void ProcessEncodedUTF8(std::size_t l, const char * p);
		/// @}
	};

	class ToNetwork :
		public BufferWithSanitizedUTF8
	{
	public:
		ToNetwork(bool pass_through_errors) : BufferWithSanitizedUTF8(pass_through_errors) {}
		~ToNetwork() {}

		void take(std::size_t l, const unsigned char * p);
		void send_option(unsigned char cmd, unsigned char option);
		void send_subneg(unsigned char option, const unsigned char buf[], std::size_t len);
	protected:
		using BufferWithSanitizedUTF8::take;
		virtual void append(std::size_t l, const unsigned char * p);
	} network_output;

	class ToLocal :
		public BufferWithSanitizedUTF8
	{
	public:
		ToLocal(UnicodeNetworkVirtualTerminal & u, bool pass_through_errors) : BufferWithSanitizedUTF8(pass_through_errors), state(NORMAL), unvt(u) {}
		~ToLocal() {}

		void take(std::size_t l, const unsigned char * p);
		void echo(std::size_t l, const unsigned char * p);
	protected:
		using BufferWithSanitizedUTF8::take;
		enum { NORMAL, SEEN_CR, SEEN_IAC, SEEN_WILL, SEEN_WILL_NOT, SEEN_DO, SEEN_DO_NOT, SEEN_SB, SEEN_SB_CR, SEEN_SB_IAC } state;
		UnicodeNetworkVirtualTerminal & unvt;
		typedef std::set<unsigned char> OptionsSet;
		OptionsSet options_pending_d, options_pending_w;
	} local_output;
};

UnicodeNetworkVirtualTerminal::UnicodeNetworkVirtualTerminal(
	const UnicodeNetworkVirtualTerminal::Options & o
) :
	options(o),
	network_in_eof(false),
	local_in_eof(false),
	network_out_shutdown_pending(false),
	network_out_eof(false),
	remote_echo(false),
	rfc1073_naws(false),
	network_output(true /* always pass through local UTF-8 overlength data */),
	local_output(*this, o.pass_through_overlength_remote_data)
{
}

inline
void
UnicodeNetworkVirtualTerminal::ToNetwork::send_option(
	const unsigned char cmd,
	const unsigned char opt
) {
	unsigned char b[3] = { IAC, cmd, opt };
	append_raw(sizeof b, b);
}

void
UnicodeNetworkVirtualTerminal::ToNetwork::send_subneg(
	const unsigned char option,
	const unsigned char buf[],
	const std::size_t len
) {
	static unsigned char beg[2] = { IAC, SB }, end[2] = { IAC, SE };
	append_raw(sizeof beg, beg);
	append_raw(1U, &option);
	append(len, buf);	// Needs to do IAC escaping.
	append_raw(sizeof end, end);
}

void
UnicodeNetworkVirtualTerminal::ack(
	unsigned char /*option*/
) {
	// We don't even send options.
}

void
UnicodeNetworkVirtualTerminal::nak(
	unsigned char /*option*/
) {
	// We don't even send options.
}

void
UnicodeNetworkVirtualTerminal::request(
	unsigned char option,
	bool on
) {
	switch (option) {
		case RFC859_STATUS:	// Nosy Parker!
		case RFC1079_SPEED:	// no serial device
		case RFC1091_TERM:	// no terminal type
		case RFC1096_XDISPLOC:	// no X server
		case RFC1184_LINEMODE:	// This is a modern full-duplex character-mode NVT.
		case RFC1372_TOGGFLOW:	// no serial device
		case RFC1408_ENV:	// Nosy Parker!
		case RFC1572_NEWENV:	// Nosy Parker!
			// Silently reject these:
			network_output.send_option(DO_NOT, option);
			break;
		case RFC856_BINARY:
			// Operating over TCP with UTF-8, we are effectively doing this anyway.
			network_output.send_option(on ? DO : DO_NOT, option);
			break;
		case RFC857_ECHO:
			if (!options.no_remote_echo) {
				remote_echo = on;
				network_output.send_option(remote_echo ? DO : DO_NOT, option);
			} else
				network_output.send_option(DO_NOT, option);
			break;
		case RFC858_ZAPGA:
			// Yes, it is the 21st century; we have full-duplex standard I/O.
			network_output.send_option(on ? DO : DO_NOT, option);
			break;
		case RFC860_MARK:
			// This is trivial to support; some mad "TELNET BBS" systems use this.
			network_output.send_option(on ? DO : DO_NOT, option);
			break;
		case RFC1073_NAWS:
			rfc1073_naws = on;
			network_output.send_option(on ? DO : DO_NOT, option);
			window_resized = on;	// Force a fresh resize event.
			break;
		default:
			if (on)
				std::fprintf(stderr, "Denied TELNET option WILL %d.\n", option);
			network_output.send_option(DO_NOT, option);
			break;
	}
}

void
UnicodeNetworkVirtualTerminal::demand(
	unsigned char option,
	bool on
) {
	switch (option) {
		case RFC859_STATUS:	// Nosy Parker!
		case RFC1079_SPEED:	// no serial device
		case RFC1091_TERM:	// no terminal type
		case RFC1096_XDISPLOC:	// no X server
		case RFC1184_LINEMODE:	// This is a modern full-duplex character-mode NVT.
		case RFC1372_TOGGFLOW:	// no serial device
		case RFC1408_ENV:	// Nosy Parker!
		case RFC1572_NEWENV:	// Nosy Parker!
			// Silently reject these:
			network_output.send_option(WILL_NOT, option);
			break;
		case RFC856_BINARY:
			// Operating over TCP with UTF-8, we are effectively doing this anyway.
			network_output.send_option(on ? WILL : WILL_NOT, option);
			break;
		case RFC857_ECHO:
			// We are not echoing network input back out, sunshine; that is madness.
			network_output.send_option(WILL_NOT, option);
			break;
		case RFC858_ZAPGA:
			// We are not sending go-aheads in the first place.
			// RFC 854 allowed for them never being sent in the client-to-server direction.
			network_output.send_option(WILL, option);
			break;
		case RFC860_MARK:
			// Any answer, positive or negative, as long as it is in sequence, works here.
			network_output.send_option(on ? WILL : WILL_NOT, option);
			break;
		case RFC1073_NAWS:
			rfc1073_naws = on;
			network_output.send_option(on ? WILL : WILL_NOT, option);
			window_resized = on;
			break;
		default:
			if (on)
				std::fprintf(stderr, "Denied TELNET option DO %d.\n", option);
			network_output.send_option(WILL_NOT, option);
			break;
	}
}

inline
void
UnicodeNetworkVirtualTerminal::Buffer::try_flush(
	int fileno
) {
	if (pending) {
		const ssize_t l(write(fileno, buffer, pending));
		if (l > 0) {
			std::memmove(buffer, buffer + l, pending - l);
			pending -= l;
		}
	}
}

inline
void
UnicodeNetworkVirtualTerminal::Buffer::append_raw(
	std::size_t l,
	const unsigned char * p
) {
	if (l > sizeof buffer - pending) l = sizeof buffer - pending;
	std::memmove(buffer + pending, p, l);
	pending += l;
}

void
UnicodeNetworkVirtualTerminal::BufferWithSanitizedUTF8::ProcessDecodedUTF8(
	char32_t character,
	bool decoder_error,
	bool /*overlong*/
) {
	if (decoder_error && !pass_through_errors) return;
	utf8encoder.Process(character);
}

void
UnicodeNetworkVirtualTerminal::BufferWithSanitizedUTF8::ProcessEncodedUTF8(
	std::size_t l,
	const char * p
) {
	append(l, reinterpret_cast<const unsigned char *>(p));
}

void
UnicodeNetworkVirtualTerminal::ToLocal::take(
	std::size_t l,
	const unsigned char * p
) {
	while (l--) {
		const unsigned char c(*p++);
		switch (state) {
			case NORMAL:
				switch (c) {
					case IAC:	state = SEEN_IAC; break;
					case CR:	state = SEEN_CR; break;
					default:	take(c); break;
				}
				break;
			case SEEN_CR:
				// RFC 5198 does not describe the real world.
				// MUDs still in use in the 2020s send LF+CR instead of CR+LF, meaning that CR followed by non-NUL non-LF that is the first character of the new line is actually the usual case.
				// Se we act more like a line printer than an abstract NVT.
				if (NUL == c) {
					// Treat CR+NUL as CR, and LF+CR+NUL as LF+CR.
					take(CR);
					state = NORMAL;
				} else
				if (IAC == c) {
					// IAC can legitimately occur after a LF+CR.
					state = SEEN_IAC;
				} else
				if (CR != c) {
					// Stay in the same state until some non-IAC, non-NUL, non-CR comes along.
					// This is where CR+LF becomes just LF, and the aforementioned usual case happens.
					take(c);
					state = NORMAL;
				}
				break;
			case SEEN_IAC:
				if (IAC == c) {
					take(c);
					state = NORMAL;
				} else
				switch (c) {
					case SB:	state = SEEN_SB; break;
					case WILL:	state = SEEN_WILL; break;
					case WILL_NOT:	state = SEEN_WILL_NOT; break;
					case DO:	state = SEEN_DO; break;
					case DO_NOT:	state = SEEN_DO_NOT; break;
					default:	state = NORMAL; break;
				}
				break;
			case SEEN_WILL:
				if (options_pending_w.end() == options_pending_w.find(c))
					unvt.request(c, true);
				else {
					unvt.ack(c);
					options_pending_w.erase(c);
				}
				state = NORMAL;
				break;
			case SEEN_WILL_NOT:
				if (options_pending_w.end() == options_pending_w.find(c))
					unvt.request(c, false);
				else {
					unvt.nak(c);
					options_pending_w.erase(c);
				}
				state = NORMAL;
				break;
			case SEEN_DO:
				if (options_pending_d.end() == options_pending_d.find(c))
					unvt.demand(c, true);
				else {
					unvt.ack(c);
					options_pending_d.erase(c);
				}
				state = NORMAL;
				break;
			case SEEN_DO_NOT:
				if (options_pending_d.end() == options_pending_d.find(c))
					unvt.demand(c, false);
				else {
					unvt.nak(c);
					options_pending_d.erase(c);
				}
				state = NORMAL;
				break;
			case SEEN_SB:
				switch (c) {
					case IAC:	state = SEEN_SB_IAC; break;
					case CR:	state = SEEN_SB_CR; break;
					default:	take(c); break;
				}
				break;
			case SEEN_SB_CR:
				if (LF == c) {
					// FIXME: CR+LF in a subnegotiation?
					state = SEEN_SB;
				} else
				if (NUL == c) {
					// FIXME: CR+NUL in a subnegotiation?
					state = SEEN_SB;
				}
				if (IAC == c) {
					state = SEEN_SB_IAC;
				} else
				{
					take(c);
					state = SEEN_SB;
				}
				break;
			case SEEN_SB_IAC:
				if (SE == c) {
					state = NORMAL;
				}
				if (IAC != c) {
					state = SEEN_SB;
				} else
				{
					// FIXME
				}
				break;
		}
	}
}

void
UnicodeNetworkVirtualTerminal::ToLocal::echo(
	std::size_t l,
	const unsigned char * p
) {
	const std::string r(VisEncoder::process_only_unsafe(std::string(reinterpret_cast<const char *>(p), l)));
	append_raw(r.length(), reinterpret_cast<const unsigned char *>(r.data()));
}

void
UnicodeNetworkVirtualTerminal::ToNetwork::take(
	std::size_t l,
	const unsigned char * p
) {
	while (l--) {
		const unsigned char c(*p++);
		if (LF == c) take(CR);
		take(c);
		if (CR == c) take(NUL);
	}
}

void
UnicodeNetworkVirtualTerminal::ToNetwork::append(
	std::size_t l,
	const unsigned char * p
) {
	while (l) {
		const unsigned char * iac = static_cast<const unsigned char *>(std::memchr(p, IAC, l));
		if (!iac) {
			// In the simple case where there is no IAC to escape we just send everything left.
			append_raw(l, p);
			p += l;
			l = 0;
		} else {
			const std::size_t n(iac - p + 1);	// Includes IAC character at the end.
			append_raw(n, iac);
			append_raw(1, iac);	// Send it again.
			l -= n;
			p += n;
		}
	}
}

inline
void
UnicodeNetworkVirtualTerminal::handle_local_in(
	const struct kevent & event
) {
	if ((EV_EOF|EV_ERROR) & event.flags && 0 == event.data) {
		local_in_eof = network_out_shutdown_pending = true;
		return;
	}
	unsigned char buffer[READ_BUF_SIZE];
	const ssize_t l(read(event.ident, buffer, sizeof buffer));
	if (l > 0) {
		network_output.take(l, buffer);
		if (options.local_echo && !remote_echo)
			local_output.echo(l, buffer);
	} else
	if (0 == l || (EV_EOF|EV_ERROR) & event.flags)
		local_in_eof = network_out_shutdown_pending = true;
}

inline
void
UnicodeNetworkVirtualTerminal::handle_network_in(
	const struct kevent & event
) {
	if ((EV_EOF|EV_ERROR) & event.flags && 0 == event.data) {
		network_in_eof = true;
		return;
	}
	unsigned char buffer[READ_BUF_SIZE];
	const ssize_t l(read(event.ident, buffer, sizeof buffer));
	if (l > 0) {
		local_output.take(l, buffer);
	} else
	if (0 == l || (EV_EOF|EV_ERROR) & event.flags)
		network_in_eof = true;
}

void
UnicodeNetworkVirtualTerminal::handle_network_out(
	const struct kevent & event
) {
	if ((EV_EOF|EV_ERROR) & event.flags) {
		network_out_eof = true;
		return;
	}
	network_output.try_flush(event.ident);
}

void
UnicodeNetworkVirtualTerminal::send_window_size(
	int fileno
) {
	struct winsize size;
	if (0 <= tcgetwinsz_nointr(fileno, size)) {
		const unsigned char b[4] = {
			static_cast<unsigned char>((size.ws_col >> 8U) & 0xFF),
			static_cast<unsigned char>((size.ws_col >> 0U) & 0xFF),
			static_cast<unsigned char>((size.ws_row >> 8U) & 0xFF),
			static_cast<unsigned char>((size.ws_row >> 0U) & 0xFF),
		};
		network_output.send_subneg(RFC1073_NAWS, b, sizeof b);
	}
}

}

/* Signal handling **********************************************************
// **************************************************************************
*/

namespace {

sig_atomic_t UnicodeNetworkVirtualTerminal::window_resized(false);

sig_atomic_t program_continued(false), terminal_stop_signalled(false), terminate_signalled(false), interrupt_signalled(false), hangup_signalled(false);

inline
void
handle_signal (
	int signo
) {
	switch (signo) {
		case SIGTSTP:	terminal_stop_signalled = true; break;
		case SIGWINCH:	UnicodeNetworkVirtualTerminal::window_resized = true; break;
		case SIGCONT:	program_continued = true; break;
		case SIGTERM:	terminate_signalled = true; break;
		case SIGINT:	interrupt_signalled = true; break;
		case SIGHUP:	hangup_signalled = true; break;
	}
}

inline
void
conditional_reraise (
	sig_atomic_t & flag,
	int signo
) {
	if (flag) {
		flag = false;
		restore_attributes();
		TemporarilyUnblockSignals u(signo, 0);
		killpg(0, signo);
		throw EXIT_FAILURE;
	}
}

}

/* Main function ************************************************************
// **************************************************************************
*/

void
nvt_client (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	UnicodeNetworkVirtualTerminal::Options options;

	try {
		popt::bool_definition no_remote_echo_option('\0', "no-remote-echo", "Do not allow the remote end to request ECHO.", options.no_remote_echo);
		popt::bool_definition local_echo_option('\0', "local-echo", "Turn on local echo when there is no ECHO by the remote end.", options.local_echo);
		popt::bool_definition pass_through_overlength_remote_data_option('\0', "pass-through-overlength-remote-data", "Pass through overlength but otherwise valid UTF-8 received from the remote end.", options.pass_through_overlength_remote_data);
		popt::definition * top_table[] = {
			&no_remote_echo_option,
			&local_echo_option,
			&pass_through_overlength_remote_data_option,
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}

	if (!args.empty()) die_unexpected_argument(prog, args, envs);
#if defined(__LINUX__) || defined(__linux__)
	struct stat s;
	// epoll_ctl(), and hence kevent(), cannot poll anything other than devices, sockets, and pipes.
	// So we have to turn regular file standard input into a pipe.
	if (0 <= fstat(STDIN_FILENO, &s)
	&&  !(S_ISCHR(s.st_mode) || S_ISFIFO(s.st_mode) || S_ISSOCK(s.st_mode))
	) {
		auto_reap_children();

		int fds[2];
		if (0 > pipe_close_on_exec(fds)) {
			die_errno(prog, envs, "pipe");
		}
		const pid_t child(fork());
		if (0 > child) {
			die_errno(prog, envs, "fork");
		} else
		if (0 == child) {
			dup2(fds[1], STDOUT_FILENO);
			set_non_blocking(STDOUT_FILENO, false);
			// The original pipe descriptors are all open O_CLOEXEC and will be closed when we exec cat.
			args.insert(args.begin(), "--");
			args.insert(args.begin(), "cat");
			next_prog = arg0_of(args);
			return;
		} else
		{
			dup2(fds[0], STDIN_FILENO);
			// Close the write end of the pipe in the parent so that EOF propagates.
			close(fds[1]);
		}
	}
#endif

	save_attributes();

	std::vector<struct kevent> ip;

	ReserveSignalsForKQueue kqueue_reservation(SIGINT, SIGTERM, SIGHUP, SIGPIPE, SIGCONT, SIGTSTP, 0);
	PreventDefaultForFatalSignals ignored_signals(SIGINT, SIGTERM, SIGHUP, SIGPIPE, 0);

	const FileDescriptorOwner queue(kqueue());
	if (0 > queue.get()) {
		die_errno(prog, envs, "kqueue");
	}

	append_event(ip, STDIN_FILENO, EVFILT_READ, EV_ADD, 0, 0, nullptr);
	append_event(ip, STDOUT_FILENO, EVFILT_WRITE, EV_ADD, 0, 0, nullptr);
	append_event(ip, NETWORK_IN_FILENO, EVFILT_READ, EV_ADD, 0, 0, nullptr);
	append_event(ip, NETWORK_OUT_FILENO, EVFILT_WRITE, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGPIPE, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGCONT, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGTSTP, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, STDIN_FILENO, EVFILT_READ, EV_ENABLE, 0, 0, nullptr);
	append_event(ip, NETWORK_IN_FILENO, EVFILT_READ, EV_ENABLE, 0, 0, nullptr);
	append_event(ip, STDOUT_FILENO, EVFILT_WRITE, EV_DISABLE, 0, 0, nullptr);
	append_event(ip, NETWORK_OUT_FILENO, EVFILT_WRITE, EV_DISABLE, 0, 0, nullptr);

	UnicodeNetworkVirtualTerminal unvt(options);

	while (!unvt.done()) {
		conditional_reraise(terminate_signalled, SIGTERM);
		conditional_reraise(interrupt_signalled, SIGHUP);
		conditional_reraise(hangup_signalled, SIGHUP);
		if (terminal_stop_signalled) {
			terminal_stop_signalled = false;
			restore_attributes();
			TemporarilyUnblockSignals u(SIGTSTP, 0);
			killpg(0, SIGTSTP);
		}
		if (program_continued) {
			program_continued = false;
			UnicodeNetworkVirtualTerminal::window_resized = true;	// Force a size update.
			save_attributes();
		}
		if (UnicodeNetworkVirtualTerminal::window_resized) {
			UnicodeNetworkVirtualTerminal::window_resized = false;
			unvt.send_window_size(STDOUT_FILENO);
		}

		struct kevent p[8];
		const int rc(kevent(queue.get(), ip.data(), ip.size(), p, sizeof p/sizeof *p, nullptr));
		ip.clear();

		if (0 > rc) {
			if (EINTR == errno) continue;
			const int error(errno);
			restore_attributes();
			die_errno(prog, envs, error, "kevent");
		}

		for (size_t i(0); i < static_cast<size_t>(rc); ++i) {
			const struct kevent & e(p[i]);
			switch (e.filter) {
				case EVFILT_SIGNAL:
					handle_signal(e.ident);
					break;
				case EVFILT_READ:
					if (STDIN_FILENO == e.ident) {
						unvt.handle_local_in(e);
						if (unvt.query_network_out_full() || unvt.query_local_in_eof())
							append_event(ip, e.ident, EVFILT_READ, EV_DISABLE, 0, 0, nullptr);
						if (unvt.query_network_out_pending())
							append_event(ip, NETWORK_OUT_FILENO, EVFILT_WRITE, EV_ENABLE, 0, 0, nullptr);
						else if (unvt.query_network_out_shutdown_pending()) {
							shutdown(NETWORK_OUT_FILENO, SHUT_WR);
							unvt.set_network_out_shutdown_done();
						}
						if (unvt.query_local_out_pending())	// Because of local echo.
							append_event(ip, STDOUT_FILENO, EVFILT_WRITE, EV_ENABLE, 0, 0, nullptr);
					} else
					if (NETWORK_IN_FILENO == e.ident) {
						unvt.handle_network_in(e);
						if (unvt.query_local_out_full() || unvt.query_network_in_eof())
							append_event(ip, e.ident, EVFILT_READ, EV_DISABLE, 0, 0, nullptr);
						if (unvt.query_local_out_pending())
							append_event(ip, STDOUT_FILENO, EVFILT_WRITE, EV_ENABLE, 0, 0, nullptr);
						if (unvt.query_network_out_pending())	// Because of option responses.
							append_event(ip, NETWORK_OUT_FILENO, EVFILT_WRITE, EV_ENABLE, 0, 0, nullptr);
					}
					break;
				case EVFILT_WRITE:
					if (STDOUT_FILENO == e.ident) {
						unvt.handle_local_out(e.ident);
						if (!unvt.query_local_out_pending())
							append_event(ip, e.ident, EVFILT_WRITE, EV_DISABLE, 0, 0, nullptr);
					} else
					if (NETWORK_OUT_FILENO == e.ident) {
						unvt.handle_network_out(e);
						if (!unvt.query_network_out_pending() || unvt.query_network_out_eof())
							append_event(ip, e.ident, EVFILT_WRITE, EV_DISABLE, 0, 0, nullptr);
					}
					break;
			}
		}
	}

	restore_attributes();
	throw EXIT_SUCCESS;
}
