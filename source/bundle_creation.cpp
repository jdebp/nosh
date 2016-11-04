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
	const bool services_are_relative,
	const bool targets_are_relative,
	const FileDescriptorOwner & bundle_dir_fd,
	const std::string & names,
	const std::string & subdir
) {
	const std::list<std::string> list(split_list(names));
	for (std::list<std::string>::const_iterator i(list.begin()); list.end() != i; ++i) {
		const std::string & name(*i);
		std::string base, link, target;
		if (ends_in(name, ".target", base)) {
			if (targets_are_relative) {
				if (is_target)
					target = "../../" + base;
				else
					target = "../../../targets/" + base;
			} else
				target = "/etc/service-bundles/targets/" + base;
			link = subdir + base;
		} else
		if (ends_in(name, ".service", base)
		||  ends_in(name, ".socket", base)
		) {
			if (services_are_relative)
				target = "../../" + base;
			else
				target = "/var/sv/" + base;
			link = subdir + base;
		} else 
		{
			target = "../../" + name;
			link = subdir + name;
		}
		create_link(prog, bund, bundle_dir_fd, target, link);
	}
}

static inline
bool
remove_last_component (
	std::string & dirname
) {
	const std::string::size_type slash(dirname.rfind('/'));
	if (std::string::npos != slash) {
		dirname = std::string(dirname, 0, 0 == slash ? 1 : slash);
		return true;
	} else {
		dirname = std::string();
		return false;
	}
}

void
make_mount_interdependencies (
	const char * prog,
	const std::string & name,
	const bool etc_bundle,
	const bool prevent_root_link,
	const FileDescriptorOwner & bundle_dir_fd,
	std::string where
) {
	const char * const etc_mount(etc_bundle ? "../../mount@" : "/etc/service-bundles/services/mount@");
	while (remove_last_component(where)) {
		const bool to_root(is_root(where.c_str()));
		if (prevent_root_link && to_root) break;
		const std::string param(systemd_name_escape(false, where));
		const std::string link("after/mount@" + param);
		const std::string target(etc_mount + param);
		create_link(prog, name, bundle_dir_fd, target, link);
		if (to_root) break;
	}
}

void
flag_file (
	const char * prog,
	const std::string & service_dirname,
	const FileDescriptorOwner & service_dir_fd,
	const char * name,
	bool make
) {
	if (make) {
		const FileDescriptorOwner fd(open_writecreate_at(service_dir_fd.get(), name, 0400));
		if (0 > fd.get()) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, service_dirname.c_str(), name, std::strerror(error));
			throw EXIT_FAILURE;
		}
	} else {
		const int rc(unlinkat(service_dir_fd.get(), name, 0));
		if (0 > rc && ENOENT != errno) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, service_dirname.c_str(), name, std::strerror(error));
			throw EXIT_FAILURE;
		}
	}
}
