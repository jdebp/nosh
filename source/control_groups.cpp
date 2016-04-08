/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <cstdio>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include "FileDescriptorOwner.h"
#include "FileStar.h"
#include "fdutils.h"
#include "control_groups.h"

FILE *
open_my_control_group_info(
	const char * const name
) {
	FileDescriptorOwner self_cgroup_fd(open_read_at(AT_FDCWD, name));
	if (0 > self_cgroup_fd.get()) return 0;
	FileStar self_cgroup(fdopen(self_cgroup_fd.get(), "r"));
	if (self_cgroup) self_cgroup_fd.release();
	return self_cgroup.release();
}

bool
read_my_control_group (
	FILE * f,
	const char * const name,
	std::string & g
) {
	std::rewind(f);
	g.clear();
	for (;;) {
		const int c(std::fgetc(f));
		if (EOF == c) return false;
		if ('\n' != c) {
			g += char(c);
			continue;
		}
		std::string::size_type colon1(g.find(':'));
		if (std::string::npos == colon1) {
			g.clear();
			continue;
		}
		g = g.substr(colon1 + 1, std::string::npos);
		std::string::size_type colon2(g.find(':'));
		if (std::string::npos == colon2) {
			g.clear();
			continue;
		}
		const std::string n(g.substr(0, colon2));
		if (name != n) {
			g.clear();
			continue;
		}
		g = g.substr(colon2 + 1, std::string::npos);
		return true;
	}
}
