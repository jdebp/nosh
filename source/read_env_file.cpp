/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include "utils.h"
#include "popt.h"
#include "fdutils.h"

std::string
read_env_file (
	const char * prog,
	const char * dir,
	const char * basename,
	int fd
) {
	std::string r;
	FILE * f(fdopen(fd, "r"));
	if (!f) {
exit_error:
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dir, basename, std::strerror(error));
		throw EXIT_FAILURE;
	}
	for (;;) {
		int c(std::fgetc(f));
		if (std::feof(f)) break;
		if ('\n' == c) break;
		if ('\0' == c) c = '\n';
		r += char(c);
	}
	if (std::ferror(f)) goto exit_error;
	std::fclose(f);
	return r;
}
