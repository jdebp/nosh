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
#include <sys/event.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include "popt.h"
#include "utils.h"

/* Main function ************************************************************
// **************************************************************************
*/

enum { PTY_MASTER_FILENO = 4 };

static bool pass_through(false);
static termios original_attr;

static sig_atomic_t child_signalled = false;

static
void
sig_pipe (
	int signo
) {
	if (SIGPIPE != signo) return;
}

static
void
sig_child (
	int signo
) {
	if (SIGCHLD != signo) return;
	child_signalled = true;
}

static
void
sig_winch (
	int signo
) {
	if (SIGWINCH != signo) return;
	if (pass_through) {
		static winsize size;
		if (0 <= tcgetwinsz_nointr(STDIN_FILENO, size))
			tcsetwinsz_nointr(PTY_MASTER_FILENO, size);
	}
}

static
void
sig_cont (
	int signo
) {
	if (SIGCONT != signo) return;
	if (pass_through) {
		static winsize size;
		if (0 <= tcgetattr_nointr(STDIN_FILENO, original_attr))
			tcsetattr_nointr(STDIN_FILENO, TCSADRAIN, make_raw(original_attr));
		if (0 <= tcgetwinsz_nointr(STDIN_FILENO, size))
			tcsetwinsz_nointr(PTY_MASTER_FILENO, size);
	}
}

void
pty_run ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));

	try {
		popt::bool_definition pass_through_option('t', "mirror", "Operate in pass-through TTY mode.", pass_through);
		popt::definition * top_table[] = {
			&pass_through_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "prog");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw EXIT_FAILURE;
	}

	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing next program.");
		throw EXIT_FAILURE;
	}

	int child = fork();

	if (0 > child) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "fork", std::strerror(error));
		throw EXIT_FAILURE;
	}

	if (0 == child) {
		close(PTY_MASTER_FILENO);
		return;
	}

	char inb[1024], outb[1024];
	size_t inl(0), outl(0);
	bool ine(false), oute(false);

	struct sigaction sa;
	sa.sa_flags=0;
	sigemptyset(&sa.sa_mask);
	sa.sa_handler=sig_child;
	sigaction(SIGCHLD,&sa,NULL);
	sa.sa_handler=sig_pipe;
	sigaction(SIGPIPE,&sa,NULL);
	if (pass_through) {
		sa.sa_handler=sig_cont;
		sigaction(SIGCONT,&sa,NULL);
		sa.sa_handler=sig_winch;
		sigaction(SIGWINCH,&sa,NULL);
	}

	sig_cont (SIGCONT);

	int status(0);

#if defined(__LINUX__) || defined(__linux__)
	const int PTY_MASTER_OUT_FILENO(PTY_MASTER_FILENO);
	const int PTY_MASTER_IN_FILENO(dup(PTY_MASTER_FILENO));
	if (0 > PTY_MASTER_IN_FILENO) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "dup", std::strerror(error));
		throw EXIT_FAILURE;
	}
#else
	const int PTY_MASTER_OUT_FILENO(PTY_MASTER_FILENO);
	const int PTY_MASTER_IN_FILENO(PTY_MASTER_FILENO);
#endif

	const int queue(kqueue());
	if (0 > queue) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kqueue", std::strerror(error));
		throw EXIT_FAILURE;
	}

	struct kevent p[4];
	EV_SET(&p[0], STDIN_FILENO, EVFILT_READ, EV_ADD, 0, 0, 0);
	EV_SET(&p[1], STDOUT_FILENO, EVFILT_WRITE, EV_ADD, 0, 0, 0);
	EV_SET(&p[2], PTY_MASTER_IN_FILENO, EVFILT_READ, EV_ADD, 0, 0, 0);
	EV_SET(&p[3], PTY_MASTER_OUT_FILENO, EVFILT_WRITE, EV_ADD, 0, 0, 0);
	if (0 > kevent(queue, p, sizeof p/sizeof *p, 0, 0, 0)) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kevent", std::strerror(error));
		throw EXIT_FAILURE;
	}

	while ((!ine || inl) && (!oute || outl)) {
		if (child_signalled) {
			for (;;) {
				pid_t c = waitpid(child, &status, WNOHANG|WUNTRACED);
				if (c <= 0) break;
				if (WIFSTOPPED(status)) {
					if (!pass_through) {
						kill(c, SIGCONT);
					}
				}
			}
			child_signalled = false;
		}

		// Read from stdin if we have emptied the in buffer and haven't hit EOF.
		EV_SET(&p[0], STDIN_FILENO, EVFILT_READ, !ine && !inl ? EV_ENABLE : EV_DISABLE, 0, 0, 0);
		// Read from master if we have emptied the out buffer and haven't hit EOF.
		EV_SET(&p[2], PTY_MASTER_IN_FILENO, EVFILT_READ, !oute && !outl ? EV_ENABLE : EV_DISABLE, 0, 0, 0);
		// Write to stdout if we have things in the out buffer.
		EV_SET(&p[1], STDOUT_FILENO, EVFILT_WRITE, outl ? EV_ENABLE : EV_DISABLE, 0, 0, 0);
		// Write to master if we have things in the in buffer.
		EV_SET(&p[3], PTY_MASTER_OUT_FILENO, EVFILT_WRITE, inl ? EV_ENABLE : EV_DISABLE, 0, 0, 0);

		const int rc(kevent(queue, p, 4, p, sizeof p/sizeof *p, 0));

		if (0 > rc) {
			if (EINTR == errno) continue;
			if (pass_through)
				tcsetattr_nointr(STDIN_FILENO, TCSADRAIN, original_attr);
			if (!WIFEXITED(status))
				waitpid(child, &status, 0);
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "poll", std::strerror(error));
			throw EXIT_FAILURE;
		}

#if defined(DEBUG)
		if (0 != rc)
			std::fprintf(stderr, "%s: DEBUG: ine %i inl %lu oute %i outl %lu, rc = %i\n", prog, ine, inl, oute, outl, rc);
#endif

		for (size_t i(0); i < static_cast<size_t>(rc); ++i) {
#if defined(DEBUG)
			std::fprintf(stderr, "%s: DEBUG: i %lu: filter %i ident %lu flags %i data %i\n", prog, i, p[i].filter, p[i].ident, p[i].flags, p[i].data);
#endif
			if (EVFILT_READ == p[i].filter && STDIN_FILENO == p[i].ident) {
				const int l(read(STDIN_FILENO, inb, sizeof inb));
				if (l > 0) 
					inl = l;
				else if (0 == l)
					ine = true;
			}
			if (EVFILT_WRITE == p[i].filter && STDOUT_FILENO == p[i].ident) {
				const int l(write(STDOUT_FILENO, outb, outl));
				if (l > 0) {
					memmove(outb, outb + l, outl - l);
					outl -= l;
				}
			}
			if (EVFILT_READ == p[i].filter && PTY_MASTER_IN_FILENO == p[i].ident && EV_EOF != p[i].flags) {
				const int l(read(PTY_MASTER_IN_FILENO, outb, sizeof outb));
				if (l > 0) 
					outl = l;
				else if (0 == l)
					oute = true;
			}
			if (EVFILT_READ == p[i].filter && PTY_MASTER_IN_FILENO == p[i].ident && EV_EOF == p[i].flags) {
				if (!outl) oute = true;
			}
			if (EVFILT_WRITE == p[i].filter && PTY_MASTER_OUT_FILENO == p[i].ident) {
				const int l(write(PTY_MASTER_OUT_FILENO, inb, inl));
				if (l > 0) {
					memmove(inb, inb + l, inl - l);
					inl -= l;
				}
			}
#if defined(DEBUG)
			std::fprintf(stderr, "%s: DEBUG: ine %i inl %lu oute %i outl %lu\n", prog, ine, inl, oute, outl);
#endif
		}
	}

	if (pass_through)
		tcsetattr_nointr(STDIN_FILENO, TCSADRAIN, original_attr);
	if (!WIFEXITED(status))
		waitpid(child, &status, 0);
	throw WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_TEMPORARY_FAILURE;
}
