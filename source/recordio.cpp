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
#include <sys/poll.h>
#include <unistd.h>
#include "popt.h"
#include "utils.h"
#include "fdutils.h"

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
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));

	try {
		popt::top_table_definition main_option(0, 0, "Main options", "prog");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		if (p.stopped()) throw EXIT_SUCCESS;
		args = new_args;
		next_prog = arg0_of(args);
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw EXIT_FAILURE;
	}

	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing next program.");
		throw EXIT_FAILURE;
	}

	int input_fds[2];
	if (0 > pipe(input_fds)) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "pipe", std::strerror(error));
		throw EXIT_FAILURE;
	}
	int output_fds[2];
	if (0 > pipe(output_fds)) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "pipe", std::strerror(error));
		throw EXIT_FAILURE;
	}

	const int child(fork());

	if (0 > child) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "fork", std::strerror(error));
		throw EXIT_FAILURE;
	}

	if (0 != child) {
		close(input_fds[1]);
		close(output_fds[0]);
		if (STDIN_FILENO != input_fds[0]) {
			if (0 > dup2(input_fds[0], STDIN_FILENO)) {
				const int error(errno);
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "dup2", std::strerror(error));
				throw EXIT_FAILURE;
			}
			close(input_fds[0]);
		}
		if (STDOUT_FILENO != output_fds[1]) {
			if (0 > dup2(output_fds[1], STDOUT_FILENO)) {
				const int error(errno);
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "dup2", std::strerror(error));
				throw EXIT_FAILURE;
			}
			close(output_fds[1]);
		}
		return;
	}

	close(input_fds[0]); input_fds[0] = -1;
	close(output_fds[1]); output_fds[1] = -1;

	pollfd p[2];
	p[0].fd = STDIN_FILENO;
	p[0].events = POLLIN;
	p[1].fd = output_fds[0];
	p[1].events = POLLIN;

	sigset_t masked_signals;
	sigprocmask(SIG_SETMASK, 0, &masked_signals);
	sigaddset(&masked_signals, SIGPIPE);
	sigprocmask(SIG_SETMASK, &masked_signals, 0);

	for (;;) {
		const int rc(poll(p, sizeof p/sizeof *p, -1));
		if (0 > rc) {
			const int error(errno);
			if (EINTR == error) continue;
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "poll", std::strerror(error));
			throw EXIT_FAILURE;
		}
		char buf [4096];
		if (p[1].revents & (POLLIN|POLLHUP)) {
			const int n(read(p[1].fd, buf, sizeof buf));
			if (0 > n) {
				const int error(errno);
				if (EINTR == error) continue;
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "read-pipe", std::strerror(error));
				throw EXIT_FAILURE;
			}
			log('>', buf, n);
			if (0 != n)
				writeall(STDOUT_FILENO, buf, n);
			else {
				close(STDOUT_FILENO);
				p[1].fd = -1;
				break;
			}
		}
		if (p[0].revents & (POLLIN|POLLHUP)) {
			const int n(read(p[0].fd, buf, sizeof buf));
			if (0 > n) {
				const int error(errno);
				if (EINTR == error) continue;
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "read-stdin", std::strerror(error));
				throw EXIT_FAILURE;
			}
			log('<', buf, n);
			if (0 != n)
				writeall(input_fds[1], buf, n);
			else {
				close(input_fds[1]); input_fds[1] = -1;
				p[0].fd = -1;
			}
		}
	}
	throw EXIT_SUCCESS;
}
