/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

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

enum { PTY_MASTER_FILENO = 4 };

/* Signal handling **********************************************************
// **************************************************************************
*/

static sig_atomic_t child_signalled(false), window_resized(false), program_continued(false), terminate_signalled(false), interrupt_signalled(false), hangup_signalled(false);

static
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

/* Main function ************************************************************
// **************************************************************************
*/

void
pty_run ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
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
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing next program.");
		throw static_cast<int>(EXIT_USAGE);
	}

	const int child(fork());

	if (0 > child) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "fork", std::strerror(error));
		throw EXIT_FAILURE;
	}

	if (0 == child) {
		close(PTY_MASTER_FILENO);
		return;
	}

	ReserveSignalsForKQueue kqueue_reservation(SIGINT, SIGTERM, SIGHUP, SIGPIPE, SIGCHLD, SIGCONT, SIGWINCH, 0);
	PreventDefaultForFatalSignals ignored_signals(SIGINT, SIGTERM, SIGHUP, SIGPIPE, 0);

	const int queue(kqueue());
	if (0 > queue) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kqueue", std::strerror(error));
		throw EXIT_FAILURE;
	}

	struct kevent p[16];
	{
		size_t index(0);
		set_event(&p[index++], STDIN_FILENO, EVFILT_READ, EV_ADD, 0, 0, 0);
		set_event(&p[index++], STDOUT_FILENO, EVFILT_WRITE, EV_ADD, 0, 0, 0);
		set_event(&p[index++], PTY_MASTER_FILENO, EVFILT_READ, EV_ADD, 0, 0, 0);
		set_event(&p[index++], PTY_MASTER_FILENO, EVFILT_WRITE, EV_ADD, 0, 0, 0);
		set_event(&p[index++], SIGCHLD, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		set_event(&p[index++], SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		set_event(&p[index++], SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		set_event(&p[index++], SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		set_event(&p[index++], SIGPIPE, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		if (pass_through) {
		set_event(&p[index++], SIGCONT, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		set_event(&p[index++], SIGWINCH, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		}
		if (0 > kevent(queue, p, index, 0, 0, 0)) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kevent", std::strerror(error));
			throw EXIT_FAILURE;
		}
	}

	char inb[1024], outb[1024];
	size_t inl(0), outl(0);
	bool ine(false), oute(false);
	int status(WAIT_STATUS_RUNNING), code(0);
	termios original_attr;

	program_continued = true;

	while (WAIT_STATUS_RUNNING == status || WAIT_STATUS_PAUSED == status || !oute || outl) {
		if (terminate_signalled||interrupt_signalled||hangup_signalled) {
			if (WAIT_STATUS_RUNNING == status || WAIT_STATUS_PAUSED == status) {
				if (terminate_signalled) kill(child, SIGTERM);
				if (interrupt_signalled) kill(child, SIGINT);
				if (hangup_signalled) kill(child, SIGHUP);
				if (WAIT_STATUS_PAUSED == status) kill(child, SIGCONT);
			}
			break;
		}
		if (child_signalled) {
			child_signalled = false;
			for (;;) {
				pid_t c;
				if (0 >= wait_nonblocking_for_anychild_stopexit(c, status, code)) break;
				if (c != child) continue;
				if (WAIT_STATUS_PAUSED == status) {
					if (!pass_through) {
						kill(c, SIGCONT);
					}
				}
			}
		}
		if (pass_through && window_resized) {
			window_resized = false;
			static winsize size;
			if (0 <= tcgetwinsz_nointr(STDIN_FILENO, size))
				tcsetwinsz_nointr(PTY_MASTER_FILENO, size);
		}
		if (pass_through && program_continued) {
			program_continued = false;
			static winsize size;
			if (0 <= tcgetattr_nointr(STDIN_FILENO, original_attr)) {
				tcsetattr_nointr(STDIN_FILENO, TCSADRAIN, make_raw(original_attr));
				tcsetattr_nointr(PTY_MASTER_FILENO, TCSADRAIN, original_attr);
			}
			if (0 <= tcgetwinsz_nointr(STDIN_FILENO, size))
				tcsetwinsz_nointr(PTY_MASTER_FILENO, size);
		}

		// Read from stdin if we have emptied the in buffer and haven't hit EOF.
		set_event(&p[0], STDIN_FILENO, EVFILT_READ, !ine && !inl ? EV_ENABLE : EV_DISABLE, 0, 0, 0);
		// Read from master if we have emptied the out buffer and haven't hit EOF.
		set_event(&p[2], PTY_MASTER_FILENO, EVFILT_READ, !oute && !outl ? EV_ENABLE : EV_DISABLE, 0, 0, 0);
		// Write to stdout if we have things in the out buffer.
		set_event(&p[1], STDOUT_FILENO, EVFILT_WRITE, outl ? EV_ENABLE : EV_DISABLE, 0, 0, 0);
		// Write to master if we have things in the in buffer.
		set_event(&p[3], PTY_MASTER_FILENO, EVFILT_WRITE, inl ? EV_ENABLE : EV_DISABLE, 0, 0, 0);

		const int rc(kevent(queue, p, 4, p, sizeof p/sizeof *p, 0));

		if (0 > rc) {
			if (EINTR == errno) continue;
			if (pass_through)
				tcsetattr_nointr(STDIN_FILENO, TCSADRAIN, original_attr);
			if (WAIT_STATUS_RUNNING == status || WAIT_STATUS_PAUSED == status)
				wait_blocking_for_exit_of(child, status, code);
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kevent", std::strerror(error));
			throw EXIT_FAILURE;
		}

		bool stdin_ready(false), stdout_ready(false), masterin_ready(false), masterout_ready(false), master_hangup(false);

		for (size_t i(0); i < static_cast<size_t>(rc); ++i) {
			const struct kevent & e(p[i]);
			if (EVFILT_SIGNAL == e.filter)
				handle_signal(e.ident);
			if (EVFILT_READ == e.filter && STDIN_FILENO == e.ident)
				stdin_ready = true;
			if (EVFILT_WRITE == e.filter && STDOUT_FILENO == e.ident)
				stdout_ready = true;
			if (EVFILT_READ == e.filter && PTY_MASTER_FILENO == e.ident) {
				masterin_ready = true;
				master_hangup |= EV_EOF & e.flags;
			}
			if (EVFILT_WRITE == e.filter && PTY_MASTER_FILENO == e.ident)
				masterout_ready = true;
		}

		if (stdin_ready) {
			if (!inl) {
				const ssize_t l(read(STDIN_FILENO, inb, sizeof inb));
				if (l > 0) 
					inl = l;
				else if (0 == l)
					ine = true;
			}
		}
		if (stdout_ready) {
			if (outl) {
				const ssize_t l(write(STDOUT_FILENO, outb, outl));
				if (l > 0) {
					memmove(outb, outb + l, outl - l);
					outl -= l;
				}
			}
		}
		if (masterin_ready) {
			if (!outl) {
				const ssize_t l(read(PTY_MASTER_FILENO, outb, sizeof outb));
				if (l > 0) 
					outl = l;
				else if (0 == l)
					oute = true;
			}
		}
		if (master_hangup)
			oute = true;
		if (masterout_ready) {
			if (inl) {
				const ssize_t l(write(PTY_MASTER_FILENO, inb, inl));
				if (l > 0) {
					memmove(inb, inb + l, inl - l);
					inl -= l;
				}
			}
		}
	}

	if (pass_through)
		tcsetattr_nointr(STDIN_FILENO, TCSADRAIN, original_attr);
	if (WAIT_STATUS_RUNNING == status || WAIT_STATUS_PAUSED == status)
		wait_blocking_for_exit_of(child, status, code);
	throw WAIT_STATUS_EXITED == status ? code : static_cast<int>(EXIT_TEMPORARY_FAILURE);
}
