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
#include <memory>
#include <unistd.h>
#if defined(__LINUX__) || defined(__linux__)
#include <sys/prctl.h>
#endif
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
char *
default_path()
{
	const size_t n(confstr(_CS_PATH, NULL, 0));
	if (!n) return 0;
	char * p = new(std::nothrow) char[n];
	if (p) confstr(_CS_PATH, p, n);
	return p;
}

/// A safer form of execvp() that doesn't include the current directory in the default search path.
static inline
void
safe_execvp (
	const char * prog,
	const char ** args
) {
	const char * path = std::getenv("PATH");
	std::auto_ptr<char> d;
	if (!path) {
		d.reset(default_path());
		path = d.get();
	}
	if (!path) return;

	const size_t plen = std::strlen(prog);
	std::auto_ptr<char> buf(new(std::nothrow) char [std::strlen(path) + 1 + plen + 1]);
	if (!buf.get()) return;

	int error(ENOENT);	// the most interesting error encountered
	for (;;) {
		const char * colon = std::strchr(path, ':');
		if (!colon) colon = std::strchr(path, '\0');
		size_t l(colon - path);
		if (!l) 
			buf.get()[l++] = '.';
		else
			memcpy(buf.get(), path, l);
		buf.get()[l++] = '/';
		memcpy(buf.get() + l, prog, plen + 1);

		const int fd(open_exec_at(AT_FDCWD, buf.get()));
		if (0 <= fd) {
			fexecve(fd, const_cast<char **>(args), environ);
			const int saved_error(errno);
			close(fd);
			errno = saved_error;
		}
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
#if defined(__LINUX__) || defined(__linux__)
#	if defined(PR_SET_NAME)
			prctl(PR_SET_NAME, prog);
#	endif
#endif
			c->func(next_program, args);
		} else {
			args.push_back(0);
			safe_execvp(next_program, args.data());
			return;
		}
	}
}
