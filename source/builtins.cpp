/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <new>
#include <unistd.h>
#include "utils.h"
#include "fdutils.h"
#include "environ.h"

/* Table of commands ********************************************************
// **************************************************************************
*/

const command *
find (
	const char * prog,
	bool allow_personalities
) {
	if (allow_personalities) {
		for ( const command * c(personalities); c != personalities + num_personalities; ++c )
			if (0 == strcmp(c->name, prog)) 
				return c;
	}
	for ( const command * c(commands); c != commands + num_commands; ++c )
		if (0 == strcmp(c->name, prog)) 
			return c;
	return 0;
}

static inline
bool
default_path(
	std::string & r
) {
	const size_t n(confstr(_CS_PATH, NULL, 0));
	if (!n) return false;
	std::vector<char> p(n, char());
	confstr(_CS_PATH, p.data(), n);
	r = std::string(p.data(), n);
	return true;
}

/// A safer form of execvp() that doesn't include the current directory in the default search path.
static inline
void
safe_execvp (
	const char * prog,
	const char ** args
) {
	const char * path = std::getenv("PATH");
	std::string d;
	if (!path) {
		if (!default_path(d)) return;
		path = d.c_str();
	}

	const size_t plen = std::strlen(prog);
	std::vector<char> buf(std::strlen(path) + 2 + plen + 1, char());

	int error(ENOENT);	// the most interesting error encountered
	for (;;) {
		const char * colon = std::strchr(path, ':');
		if (!colon) colon = std::strchr(path, '\0');
		size_t l(colon - path);
		if (!l) 
			buf[l++] = '.';
		else
			memcpy(buf.data(), path, l);
		buf[l++] = '/';
		memcpy(buf.data() + l, prog, plen + 1);

#if defined(__OpenBSD__)
		const int rc(execve(buf.data(), const_cast<char **>(args), environ));
#else
		const int fd(open_exec_at(AT_FDCWD, buf.data()));
		if (0 <= fd) {
			fexecve(fd, const_cast<char **>(args), environ);
			const int saved_error(errno);
			close(fd);
			errno = saved_error;
		}
#endif
		if (ENOENT == errno) 
			errno = error;	// Restore a more interesting error.
		else if ((EACCES != errno) && (EPERM != errno) && (EISDIR != errno)) 
			return;		// Everything else is fatal.
		else
			error = errno;	// Save this interesting error.
		if (!*colon) 
			return;
		path = colon + 1;
	}
}

void
exec_terminal (
	const char * & prog,
	const char * & next_program,
	std::vector<const char *> & args
) {
	for (;;) {
		if (args.empty()) return;

		if (std::strchr(next_program, '/')) {
			args.push_back(0);
			execv(next_program, const_cast<char **>(args.data()));
			return;
		} else if (const command * c = find(next_program, false)) {
			prog = basename_of(next_program);
			setprocname(prog);
			setprocargv(args.size(), args.data());
			c->func(next_program, args);
		} else {
			args.push_back(0);
			safe_execvp(next_program, args.data());
			return;
		}
	}
}
