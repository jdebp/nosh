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
#if defined(__LINUX__) || defined(__linux__)
#include "kqueue_linux.h"
#else
#include <sys/types.h>
#include <sys/event.h>
#endif
#include "kqueue_common.h"
#include <unistd.h>
#include "popt.h"
#include "utils.h"
#include "fdutils.h"
#include "SignalManagement.h"

static const int pid(getpid());

static
void
writeall (
	int fd,
	const char * ptr,
	std::size_t len
) {
	while (len) {
		const int n(write(fd, ptr, len));
		if (0 >= n) continue;
		ptr += n;
		len -= n;
	}
}

static
void
log (
	char dir,
	const char * ptr,
	std::size_t len
) {
	if (!len)
		std::fprintf(stderr, "%u: %c [EOF]\n", pid, dir);
	else
	while (len) {
		std::fprintf(stderr, "%u: %c ", pid, dir);
		if (const char * nl = static_cast<const char *>(std::memchr(ptr, '\n', len))) {
			const std::size_t l(nl - ptr);
			std::fwrite(ptr, l, 1, stderr);
			std::fputc(' ', stderr);
			ptr += l + 1;
			len -= l + 1;
		} else {
			std::fwrite(ptr, len, 1, stderr);
			std::fputc('+', stderr);
			ptr += len;
			len = 0;
		}
		std::fputc('\n', stderr);
	}
}

/* Main function ************************************************************
// **************************************************************************
*/

void
recordio (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));

	try {
		popt::top_table_definition main_option(0, nullptr, "Main options", "{prog}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		if (p.stopped()) throw EXIT_SUCCESS;
		args = new_args;
		next_prog = arg0_of(args);
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}

	if (args.empty()) die_missing_next_program(prog, envs);

	int input_fds[2];
	if (0 > pipe(input_fds)) {
		die_errno(prog, envs, "pipe");
	}
	int output_fds[2];
	if (0 > pipe(output_fds)) {
		die_errno(prog, envs, "pipe");
	}

	const pid_t child(fork());

	if (0 > child) {
		die_errno(prog, envs, "fork");
	}

	if (0 != child) {
		close(input_fds[1]);
		close(output_fds[0]);
		if (STDIN_FILENO != input_fds[0]) {
			if (0 > dup2(input_fds[0], STDIN_FILENO)) {
				die_errno(prog, envs, "dup2");
			}
			close(input_fds[0]);
		}
		if (STDOUT_FILENO != output_fds[1]) {
			if (0 > dup2(output_fds[1], STDOUT_FILENO)) {
				die_errno(prog, envs, "dup2");
			}
			close(output_fds[1]);
		}
		return;
	}

	close(input_fds[0]); input_fds[0] = -1;
	close(output_fds[1]); output_fds[1] = -1;

	const int queue(kqueue());
	if (0 > queue) {
		die_errno(prog, envs, "kqueue");
	}

	PreventDefaultForFatalSignals ignored_signals(SIGPIPE, 0);

	std::vector<struct kevent> ip;
	append_event(ip, STDIN_FILENO, EVFILT_READ, EV_ADD, 0, 0, nullptr);
	append_event(ip, output_fds[0], EVFILT_READ, EV_ADD, 0, 0, nullptr);

	for (;;) {
		struct kevent p[2];
		const int rc(kevent(queue, ip.data(), ip.size(), p, sizeof p/sizeof *p, nullptr));
		ip.clear();
		if (0 > rc) {
			if (EINTR == errno) continue;
			die_errno(prog, envs, prog);
		}
		for (size_t i(0); i < static_cast<std::size_t>(rc); ++i) {
			const struct kevent & e(p[i]);
			if (EVFILT_READ != e.filter)
				continue;
			const int fd(static_cast<int>(e.ident));
			char buf [4096];
			const ssize_t n(read(fd, buf, sizeof buf));
			if (output_fds[0] == fd) {
				if (0 > n) {
					if (EINTR == errno) continue;
					die_errno(prog, envs, "read-pipe");
				}
				log('>', buf, n);
				if (0 != n)
					writeall(STDOUT_FILENO, buf, n);
				else {
					close(STDOUT_FILENO);
					append_event(ip, fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
					break;
				}
			}
			if (STDIN_FILENO == fd) {
				if (0 > n) {
					if (EINTR == errno) continue;
					die_errno(prog, envs, "read-stdin");
				}
				log('<', buf, n);
				if (0 != n)
					writeall(input_fds[1], buf, n);
				else {
					close(input_fds[1]); input_fds[1] = -1;
					append_event(ip, fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
				}
			}
		}
	}
	throw EXIT_SUCCESS;
}
