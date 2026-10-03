/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define _BSD_SOURCE 1
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <cerrno>
#include <sys/types.h>
#include "kqueue_common.h"
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include "popt.h"
#include "utils.h"
#include "ttyutils.h"
#include "SignalManagement.h"
#include "FileDescriptorOwner.h"

enum { PTY_BACK_END_FILENO = 4, CONTROL_TERMINAL_FILENO = 3 };

namespace {

termios original_attr;

int outer_tty_fd(-1);

inline
void
find_outer_tty()
{
	if (isatty(STDIN_FILENO))
		outer_tty_fd = STDIN_FILENO;
	else
	if (isatty(STDOUT_FILENO))
		outer_tty_fd = STDOUT_FILENO;
	else
	if (isatty(CONTROL_TERMINAL_FILENO))
		outer_tty_fd = CONTROL_TERMINAL_FILENO;
	else
		outer_tty_fd = -1;
}

inline
void
copy_window_size()
{
	if (-1 == outer_tty_fd) return;
	struct winsize size;
	if (0 <= tcgetwinsz_nointr(outer_tty_fd, size))
		tcsetwinsz_nointr(PTY_BACK_END_FILENO, size);
}

inline
void
copy_window_size_and_move_attributes()
{
	if (-1 == outer_tty_fd) return;
	if (0 <= tcgetattr_nointr(outer_tty_fd, original_attr)) {
		tcsetattr_nointr(outer_tty_fd, TCSADRAIN, disable_canonical_software_processing(original_attr));
		tcsetattr_nointr(PTY_BACK_END_FILENO, TCSADRAIN, original_attr);
	}
	struct winsize size;
	if (0 <= tcgetwinsz_nointr(outer_tty_fd, size))
		tcsetwinsz_nointr(PTY_BACK_END_FILENO, size);
}

inline
void
restore_attributes()
{
	if (-1 == outer_tty_fd) return;
	tcsetattr_nointr(outer_tty_fd, TCSADRAIN, original_attr);
}

inline
void
handle_read (
	std::vector<struct kevent> & ip,
	const struct kevent & event,
	const int write_fileno,
	size_t & num_read,
	char buffer[],
	const size_t max,
	bool & eof
) {
	if (num_read < max) {
		const size_t r(max - num_read);
		const ssize_t l(read(event.ident, buffer + num_read, r));
		if (l > 0)
			num_read += l;
		else if (0 == l)
			eof = true;
	}
	if (EV_EOF & event.flags)
		eof = true;
	// Stop polling for read if we have no more space in the buffer or have hit EOF.
	if (num_read >= max || eof)
		append_event(ip, event.ident, EVFILT_READ, EV_DISABLE, 0, 0, nullptr);
	// Start polling for write to the other end if we now have things in the buffer.
	if (num_read)
		append_event(ip, write_fileno, EVFILT_WRITE, EV_ENABLE, 0, 0, nullptr);
}

inline
void
handle_write (
	std::vector<struct kevent> & ip,
	const struct kevent & event,
	const int read_fileno,
	size_t & num_read,
	char buffer[],
	const size_t max,
	bool eof
) {
	if (num_read) {
		const ssize_t l(write(event.ident, buffer, num_read));
		if (l > 0) {
			memmove(buffer, buffer + l, num_read - l);
			num_read -= l;
		}
	}
	// Stop polling for write if we have emptied the buffer.
	if (!num_read)
		append_event(ip, event.ident, EVFILT_WRITE, EV_DISABLE, 0, 0, nullptr);
	// Start polling for read to the other end if we now have space in the buffer.
	if (num_read < max && !eof)
		append_event(ip, read_fileno, EVFILT_READ, EV_ENABLE, 0, 0, nullptr);
}

}

/* Signal handling **********************************************************
// **************************************************************************
*/

namespace {

sig_atomic_t child_signalled(false), window_resized(false), program_continued(false), terminate_signalled(false), interrupt_signalled(false), hangup_signalled(false);

inline
void
handle_signal (
	int signo
) {
	switch (signo) {
		case SIGCHLD:	child_signalled = true; break;
		case SIGWINCH:	window_resized = true; break;
		case SIGCONT:	program_continued = true; break;
		case SIGTERM:	terminate_signalled = true; break;
		case SIGINT:	interrupt_signalled = true; break;
		case SIGHUP:	hangup_signalled = true; break;
	}
}

}

/* Main function ************************************************************
// **************************************************************************
*/

void
pty_run (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));

	bool pass_through(false);
	try {
		popt::bool_definition pass_through_option('t', "mirror", "Operate in pass-through TTY mode.", pass_through);
		popt::definition * top_table[] = {
			&pass_through_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{prog}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}

	if (args.empty()) die_missing_next_program(prog, envs);

	if (pass_through) {
		find_outer_tty();
		// Do these before any child can potentially run and look at the window size and attributes of the pseudo-terminal.
		copy_window_size_and_move_attributes();
	}

	const pid_t child(fork());

	if (0 > child) {
		die_errno(prog, envs, "fork");
	}

	if (0 == child) {
		close(PTY_BACK_END_FILENO);
		return;
	}

	std::vector<struct kevent> ip;
#if !defined(__LINUX__) && !defined(__linux__)
	append_event(ip, child, EVFILT_PROC, EV_ADD, NOTE_EXIT, 0, nullptr);
#endif

	ReserveSignalsForKQueue kqueue_reservation(SIGINT, SIGTERM, SIGHUP, SIGPIPE, SIGCHLD, SIGCONT, SIGWINCH, 0);
	PreventDefaultForFatalSignals ignored_signals(SIGINT, SIGTERM, SIGHUP, SIGPIPE, 0);

	const FileDescriptorOwner queue(kqueue());
	if (0 > queue.get()) {
		const int error(errno);
		// The child might not have opened the front-end yet and made it its controlling terminal.
		// This is an edge case of an edge case.
		kill(child, SIGTERM);
		kill(child, SIGHUP);
		die_errno(prog, envs, error, "kqueue");
	}

	append_event(ip, STDIN_FILENO, EVFILT_READ, EV_ADD, 0, 0, nullptr);
	append_event(ip, STDOUT_FILENO, EVFILT_WRITE, EV_ADD, 0, 0, nullptr);
	append_event(ip, PTY_BACK_END_FILENO, EVFILT_READ, EV_ADD, 0, 0, nullptr);
	append_event(ip, PTY_BACK_END_FILENO, EVFILT_WRITE, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGCHLD, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGPIPE, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	if (pass_through) {
		append_event(ip, SIGCONT, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
		append_event(ip, SIGWINCH, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	}
	append_event(ip, STDIN_FILENO, EVFILT_READ, EV_ENABLE, 0, 0, nullptr);
	append_event(ip, PTY_BACK_END_FILENO, EVFILT_READ, EV_ENABLE, 0, 0, nullptr);
	append_event(ip, STDOUT_FILENO, EVFILT_WRITE, EV_DISABLE, 0, 0, nullptr);
	append_event(ip, PTY_BACK_END_FILENO, EVFILT_WRITE, EV_DISABLE, 0, 0, nullptr);

	char input_buffer[1024], output_buffer[8192];
	size_t in_read(0), out_read(0);
	bool in_eof(false), out_eof(false);
	int status(WAIT_STATUS_RUNNING), code(0);

	while (WAIT_STATUS_RUNNING == status || WAIT_STATUS_PAUSED == status || !out_eof || out_read) {
		if (terminate_signalled||interrupt_signalled||hangup_signalled) {
			if (WAIT_STATUS_RUNNING == status || WAIT_STATUS_PAUSED == status) {
				if (terminate_signalled) kill(child, SIGTERM);
				if (interrupt_signalled) kill(child, SIGINT);
				if (hangup_signalled) kill(child, SIGHUP);
				if (WAIT_STATUS_PAUSED == status) kill(child, SIGCONT);
			}
			terminate_signalled = interrupt_signalled = hangup_signalled = false;
		}
		if (pass_through) {
			if (program_continued) {
				program_continued = false;
				window_resized = false;	// Because we are about to copy the window size anyway.
				copy_window_size_and_move_attributes();
			}
			if (window_resized) {
				window_resized = false;
				copy_window_size();
			}
		}
		if (child_signalled) {
			child_signalled = false;
			for (;;) {
				// Reap any children, in case someone designated us to be a local reaper.
				pid_t c;
				int status1, code1;
				if (0 >= wait_nonblocking_for_anychild_stopexit(c, status1, code1)) break;
				if (c != child) continue;
				// But only care about the status of one specific child.
				status = status1;
				code = code1;
				if (WAIT_STATUS_PAUSED == status) {
					if (!pass_through) {
						kill(c, SIGCONT);
					}
				}
			}
			continue;
		}

		struct kevent p[8];
		const int rc(kevent(queue.get(), ip.data(), ip.size(), p, sizeof p/sizeof *p, nullptr));
		ip.clear();

		if (0 > rc) {
			if (EINTR == errno) continue;
			const int error(errno);
			if (pass_through)
				restore_attributes();
			close(PTY_BACK_END_FILENO);
			if (WAIT_STATUS_RUNNING == status || WAIT_STATUS_PAUSED == status)
				wait_blocking_for_exit_of(child, status, code);
			die_errno(prog, envs, error, "kevent");
		}

		for (size_t i(0); i < static_cast<size_t>(rc); ++i) {
			const struct kevent & e(p[i]);
			switch (e.filter) {
#if !defined(__LINUX__) && !defined(__linux__)
				case EVFILT_PROC:
				{
					// Reap any children, in case someone designated us to be a local reaper.
					int status1, code1;
					if (0 > wait_nonblocking_for_stopexit_of(e.ident, status1, code1)) break;
					if (static_cast<pid_t>(e.ident) == child) {
						// But only care about the status of one specific child.
						status = status1;
						code = code1;
						if (WAIT_STATUS_PAUSED == status) {
							if (!pass_through) {
								kill(e.ident, SIGCONT);
							}
						}
					}
					break;
				}
#endif
				case EVFILT_SIGNAL:
					handle_signal(e.ident);
					break;
				case EVFILT_READ:
					if (STDIN_FILENO == e.ident)
						handle_read(ip, e, PTY_BACK_END_FILENO, in_read, input_buffer, sizeof input_buffer/sizeof *input_buffer, in_eof);
					else
					if (PTY_BACK_END_FILENO == e.ident)
						handle_read(ip, e, STDOUT_FILENO, out_read, output_buffer, sizeof output_buffer/sizeof *output_buffer, out_eof);
					break;
				case EVFILT_WRITE:
					if (STDOUT_FILENO == e.ident)
						handle_write(ip, e, PTY_BACK_END_FILENO, out_read, output_buffer, sizeof output_buffer/sizeof *output_buffer, out_eof);
					else
					if (PTY_BACK_END_FILENO == e.ident)
						handle_write(ip, e, STDIN_FILENO, in_read, input_buffer, sizeof input_buffer/sizeof *input_buffer, in_eof);
					break;
			}
		}
	}

	if (pass_through)
		restore_attributes();
	close(PTY_BACK_END_FILENO);
	if (WAIT_STATUS_RUNNING == status || WAIT_STATUS_PAUSED == status)
		wait_blocking_for_exit_of(child, status, code);
	throw WAIT_STATUS_EXITED == status ? code : static_cast<int>(EXIT_TEMPORARY_FAILURE);
}
