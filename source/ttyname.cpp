/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <paths.h>
#include <sys/ioctl.h>	// for struct winsize
#include "ttyname.h"
#include "ttyutils.h"
#include "fdutils.h"
#include "ProcessEnvironment.h"
#include "FileDescriptorOwner.h"

namespace {

	static const char redirector[] = "/dev/tty";

	const char *
	get_controlling_tty_filename_not_generic (const ProcessEnvironment & envs)
	{
		const char * tty(envs.query("TTY"));
		if (!tty) tty = ttyname(STDIN_FILENO);	// Fall back to the way that /bin/tty does this.
		if (!tty) {
			FileDescriptorOwner fd(open_readwriteexisting_at(AT_FDCWD, redirector));
			if (0 <= fd.get()) tty = ttyname(fd.get());
		}
		return tty;
	}

	inline
	const char *
	strip_dev (
		const char * s
	) {
		if (0 == strncmp(s, _PATH_DEV, sizeof _PATH_DEV - 1))
			return s + sizeof _PATH_DEV - 1;
		if (0 == strncmp(s, "/dev/", 5))
			return s + 5;
		if (0 == strncmp(s, "/run/dev/", 9))
			return s + 9;
		if (0 == strncmp(s, "/var/dev/", 9))
			return s + 9;
		return s;
	}

}

const char *
get_line_name (const ProcessEnvironment & envs)
{
	const char * tty(get_controlling_tty_filename_not_generic(envs));
	if (tty) tty = strip_dev(tty);
	return tty;
}

const char *
get_controlling_tty_filename (const ProcessEnvironment & envs)
{
	const char * tty(get_controlling_tty_filename_not_generic(envs));
	if (!tty) tty = redirector;
	return tty;
}

std::string
id_field_from (
	const char * s
) {
	if (0 == strncmp(s, "tty", 3)
	||  0 == strncmp(s, "tts", 3)
	||  0 == strncmp(s, "pty", 3)
	||  0 == strncmp(s, "pts", 3)
	) {
		return s;
	}
	// This is a nosh convention for user-space virtual terminals.
	if (0 == strncmp(s, "vc", 2)) {
		std::string id(s);
		const std::string::size_type l(id.length());
		if (l > 4 && "/tty" == id.substr(l - 4))
			id = id.substr(0, l-4);
		return id;
	}
	while (*s && !std::isdigit(*s)) ++s;
	return s;
}

unsigned long
get_columns (
	const ProcessEnvironment & envs,
	int fd
) {
	unsigned long columns(80UL);
	// The COLUMNS environment variable takes precedence, per the Single UNIX Specification ("Environment variables").
	const char * c = envs.query("COLUMNS"), * s(c);
	if (c)
		columns = std::strtoul(c, const_cast<char **>(&c), 0);
	if (c == s || *c) {
		winsize size = {};
		tcgetwinsz_nointr(fd, size);	// It does not matter if this fails because it is not a terminal.
		// We would do these in both code paths, anyway.
		sane(size);
		columns = size.ws_col;
	}
	return columns;
}
