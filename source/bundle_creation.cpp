/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <map>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include "utils.h"
#include "fdutils.h"
#include "bundle_creation.h"
#include "FileDescriptorOwner.h"

void
create_link (
	const char * prog,
	const std::string & name,
	const FileDescriptorOwner & bundle_dir_fd,
	const std::string & target,
	const std::string & link
) {
	if (0 > unlinkat(bundle_dir_fd.get(), link.c_str(), 0)) {
		const int error(errno);
		if (ENOENT != error)
			std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, name.c_str(), link.c_str(), std::strerror(error));
	}
	if (0 > symlinkat(target.c_str(), bundle_dir_fd.get(), link.c_str())) {
		const int error(errno);
		std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, name.c_str(), link.c_str(), std::strerror(error));
	}
}

void
create_links (
	const char * prog,
	const std::string & bund,
	const bool is_target,
	const bool etc_bundle,
	const FileDescriptorOwner & bundle_dir_fd,
	const std::string & names,
	const std::string & subdir
) {
	const std::list<std::string> list(split_list(names));
	for (std::list<std::string>::const_iterator i(list.begin()); list.end() != i; ++i) {
		const std::string & name(*i);
		std::string base, link, target;
		if (ends_in(name, ".target", base)) {
			if (etc_bundle) {
				if (is_target)
					target = "../../" + base;
				else
					target = "../../../targets/" + base;
			} else
				target = "/etc/service-bundles/targets/" + base;
			link = subdir + base;
		} else
		if (ends_in(name, ".service", base)) {
			if (etc_bundle)
				target = "/var/sv/" + base;
			else
				target = "../../" + base;
			link = subdir + base;
		} else 
		{
			target = "../../" + name;
			link = subdir + name;
		}
		create_link(prog, bund, bundle_dir_fd, target, link);
	}
}
