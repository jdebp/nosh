/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <map>
#include <set>
#include <iostream>
#include <fstream>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <new>
#include <unistd.h>
#include <fstab.h>
#include "utils.h"
#include "fdutils.h"
#include "service-manager-client.h"
#include "service-manager.h"
#include "common-manager.h"
#include "popt.h"
#include "FileDescriptorOwner.h"
#include "bundle_creation.h"

/* Utilities ****************************************************************
// **************************************************************************
*/

static
std::string
quote (
	const std::string & s
) {
	std::string r;
	bool quote(s.empty());
	for (std::string::const_iterator p(s.begin()); s.end() != p; ++p) {
		const char c(*p);
		if (!std::isalnum(c) && '/' != c && '-' != c && '_' != c && '.' != c) {
			quote = true;
			if ('\"' == c || '\\' == c)
				r += '\\';
		}
		r += c;
	}
	if (quote) r = "\"" + r + "\"";
	return r;
}

static
std::string 
escape ( 
	const std::string & s
) {
	std::string r;
	for (std::string::const_iterator e(s.end()), q(s.begin()); e != q; ) {
		char c(*q++);
		if ('/' == c) {
			r += '-';
		} else
		if ('-' != c) {
			r += c;
		} else
		{
			char buf[6];
			snprintf(buf, sizeof buf, "\\x%02x", c);
			r += std::string(buf);
		}
	}
	return r;
}

static inline
const char *
strip_fuse (
	const char * s
) {
	if (0 == strncmp(s, "fuse.", 5))
		return s + 5;
	return s;
}

static inline
bool 
is_local_type (
	const char * fstype
) {
	fstype = strip_fuse(fstype);
	return 
		// This list is from Debian's old /etc/init.d/mountall.sh .
		0 != std::strcmp(fstype, "nfs") &&
		0 != std::strcmp(fstype, "nfs4") &&
		0 != std::strcmp(fstype, "smbfs") &&
		0 != std::strcmp(fstype, "cifs") &&
		0 != std::strcmp(fstype, "ncp") &&
		0 != std::strcmp(fstype, "ncpfs") &&
		0 != std::strcmp(fstype, "coda") &&
		0 != std::strcmp(fstype, "ocfs2") &&
		0 != std::strcmp(fstype, "gfs") &&
		0 != std::strcmp(fstype, "gfs2") &&
		0 != std::strcmp(fstype, "ceph") &&
		// some additions
		0 != std::strcmp(fstype, "afs") &&
		0 != std::strcmp(fstype, "sshfs") &&
		0 != std::strcmp(fstype, "glusterfs")
	;
}

static inline
bool 
is_preenable_type (
	const char * fstype
) {
	fstype = strip_fuse(fstype);
	return 
		0 == std::strcmp(fstype, "ext2") ||
		0 == std::strcmp(fstype, "ext3") ||
		0 == std::strcmp(fstype, "ext4") ||
		0 == std::strcmp(fstype, "ext")
	;
}

static inline
bool
is_root(
	const char * p
) {
	return '/' == p[0] && '\0' == p[1];
}

static inline
bool
is_api_mountpoint(
	const std::string & p
) {
	for (std::vector<api_mount>::const_iterator i(api_mounts.begin()); api_mounts.end() != i; ++i) {
		const std::string fspath(fspath_from_mount(i->iov, i->ioc));
		if (!fspath.empty() && p == fspath) 
			return true;
	}
	return false;
}

static 
void
make_default_dependencies (
	const char * prog,
	const char * name,
	const bool is_target,
	const bool etc_bundle,
	const bool root,
	const int bundle_dir_fd
) {
	create_links(prog, name, is_target, etc_bundle, bundle_dir_fd, "shutdown.target", "before/");
	if (!root)
		create_links(prog, name, is_target, etc_bundle, bundle_dir_fd, "unmount.target", "stopped-by/");
}

static
void
open (
	const char * prog,
	std::ofstream & file,
	const std::string & name
) {
	file.open(name.c_str(), std::ios::trunc|std::ios::out);
	if (!file) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, name.c_str(), std::strerror(error));
		throw EXIT_FAILURE;
	}
	chmod(name.c_str(), 0755);
}

static
void
make_run_and_restart(
	const char * prog,
	const std::string & service_dirname,
	const std::string & condition
) {
	std::ofstream restart, run, remain;
	open(prog, restart, service_dirname + "/restart");
	open(prog, run, service_dirname + "/run");
	open(prog, remain, service_dirname + "/remain");
	run << "#!/bin/nosh\n" << multi_line_comment("Run file for a mount service.") << "true";
	restart << "#!/bin/sh\n" << multi_line_comment("Restart file for a mount service.") << "exec " << condition << "\t#Ignore script arguments.";
}

static inline
void
create_fsck_bundle (
	const char * prog,
	const char * what,
	const bool preenable,
	const bool local,
	const bool overwrite,
	const bool fuse,
	const std::string * gbde,
	const std::string * geli,
	const bool etc_bundle,
	int bundle_root_fd,
	const char * bundle_root,
	const std::string & fsck_bundle_dirname,
	const std::string & mount_bundle_dirname
) {
	const bool is_target(false);
	const bool root(false);

	if (0 > mkdirat(bundle_root_fd, fsck_bundle_dirname.c_str(), 0755)) {
		const int error(errno);
		if (EEXIST != error || !overwrite) {
			std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, bundle_root, fsck_bundle_dirname.c_str(), std::strerror(error));
			throw EXIT_FAILURE;
		}
	}

	const FileDescriptorOwner fsck_bundle_dir_fd(open_dir_at(bundle_root_fd, fsck_bundle_dirname.c_str()));
	if (0 > fsck_bundle_dir_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, bundle_root, fsck_bundle_dirname.c_str(), std::strerror(error));
		throw EXIT_FAILURE;
	}

	const std::string bundle_fullname(bundle_root + ("/" + fsck_bundle_dirname));

	make_service(fsck_bundle_dir_fd.get());
	make_orderings_and_relations(fsck_bundle_dir_fd.get());
	make_default_dependencies(prog, fsck_bundle_dirname.c_str(), is_target, etc_bundle, root, fsck_bundle_dir_fd.get());
	make_run_and_restart(prog, bundle_fullname + "/service", "false");

	const char * const pre_target(local ? "local-fs-pre.target" : "remote-fs-pre.target");
	create_links(prog, fsck_bundle_dirname.c_str(), is_target, etc_bundle, fsck_bundle_dir_fd.get(), pre_target, "after/");
	create_link(prog, fsck_bundle_dirname.c_str(), fsck_bundle_dir_fd.get(), "../../" + mount_bundle_dirname, "before/" + mount_bundle_dirname);
	create_link(prog, fsck_bundle_dirname.c_str(), fsck_bundle_dir_fd.get(), "../../" + mount_bundle_dirname, "wanted-by/" + mount_bundle_dirname);

	if (fuse) {
		create_link(prog, fsck_bundle_dirname.c_str(), fsck_bundle_dir_fd.get(), "../../kmod@fuse", "after/kmod@fuse");
		create_link(prog, fsck_bundle_dirname.c_str(), fsck_bundle_dir_fd.get(), "../../kmod@fuse", "wants/kmod@fuse");
	}
	if (gbde) {
		const std::string geom_service("gbde@" + escape(*gbde));
		create_link(prog, fsck_bundle_dirname.c_str(), fsck_bundle_dir_fd.get(), "../../" + geom_service, "after/" + geom_service);
		create_link(prog, fsck_bundle_dirname.c_str(), fsck_bundle_dir_fd.get(), "../../" + geom_service, "wants/" + geom_service);
	}
	if (geli) {
		const std::string geom_service("geli@" + escape(*geli));
		create_link(prog, fsck_bundle_dirname.c_str(), fsck_bundle_dir_fd.get(), "../../" + geom_service, "after/" + geom_service);
		create_link(prog, fsck_bundle_dirname.c_str(), fsck_bundle_dir_fd.get(), "../../" + geom_service, "wants/" + geom_service);
	}

	create_link(prog, fsck_bundle_dirname.c_str(), fsck_bundle_dir_fd.get(), "/run/service-bundles/early-supervise/" + fsck_bundle_dirname, "supervise");

	std::ofstream fsck_start, fsck_stop;
	open(prog, fsck_start, bundle_fullname + "/service/start");
	open(prog, fsck_stop, bundle_fullname + "/service/stop");

	fsck_start 
		<< "#!/bin/nosh\n" 
		<< multi_line_comment("Start fsck " + quote(what) + ".\nAuto-generated by convert-fstab-services.")
		<< "monitored-fsck\n"
#if defined(__LINUX__) || defined(__linux__)
		<< (preenable ? "-p # preen mode\n" : "-a # unattended mode\n")
#else
		"-C # Skip if clean.\n-p # preen mode\n"
#endif
		<< quote(what) << "\n";

	fsck_stop 
		<< "#!/bin/nosh\n" 
		<< multi_line_comment("Stop fsck " + quote(what) + ".\nAuto-generated by convert-fstab-services.")
		<< "true\n";
}

static inline
void
create_mount_bundle (
	const char * prog,
	const char * what,
	const char * where,
	const char * fstype,
	const char * options,
	const bool local,
	const bool overwrite,
	const bool fuse,
	const std::string * gbde,
	const std::string * geli,
	const bool etc_bundle,
	int bundle_root_fd,
	const char * bundle_root,
	const std::string & mount_bundle_dirname
) {
	const bool is_target(false);
	const bool root(is_root(where));
	const bool api(is_api_mountpoint(where));

	if (0 > mkdirat(bundle_root_fd, mount_bundle_dirname.c_str(), 0755)) {
		const int error(errno);
		if (EEXIST != error || !overwrite) {
			std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, bundle_root, mount_bundle_dirname.c_str(), std::strerror(error));
			throw EXIT_FAILURE;
		}
	}

	const FileDescriptorOwner mount_bundle_dir_fd(open_dir_at(bundle_root_fd, mount_bundle_dirname.c_str()));
	if (0 > mount_bundle_dir_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, bundle_root, mount_bundle_dirname.c_str(), std::strerror(error));
		throw EXIT_FAILURE;
	}

	const std::string bundle_fullname(bundle_root + ("/" + mount_bundle_dirname));

	make_service(mount_bundle_dir_fd.get());
	make_orderings_and_relations(mount_bundle_dir_fd.get());
	make_default_dependencies(prog, mount_bundle_dirname.c_str(), is_target, etc_bundle, root, mount_bundle_dir_fd.get());
	make_run_and_restart(prog, bundle_fullname + "/service", "true");

	const char * const target(local ? "local-fs.target" : "remote-fs.target");
	const char * const pre_target(local ? "local-fs-pre.target" : "remote-fs-pre.target");
	create_links(prog, mount_bundle_dirname.c_str(), is_target, etc_bundle, mount_bundle_dir_fd.get(), target, "wanted-by/");
	create_links(prog, mount_bundle_dirname.c_str(), is_target, etc_bundle, mount_bundle_dir_fd.get(), pre_target, "after/");

	if (fuse) {
		create_link(prog, mount_bundle_dirname.c_str(), mount_bundle_dir_fd.get(), "../../kmod@fuse", "after/kmod@fuse");
		create_link(prog, mount_bundle_dirname.c_str(), mount_bundle_dir_fd.get(), "../../kmod@fuse", "wants/kmod@fuse");
	}
	if (gbde) {
		const std::string geom_service("gbde@" + escape(*gbde));
		create_link(prog, mount_bundle_dirname.c_str(), mount_bundle_dir_fd.get(), "../../" + geom_service, "after/" + geom_service);
		create_link(prog, mount_bundle_dirname.c_str(), mount_bundle_dir_fd.get(), "../../" + geom_service, "wants/" + geom_service);
	}
	if (geli) {
		const std::string geom_service("geli@" + escape(*geli));
		create_link(prog, mount_bundle_dirname.c_str(), mount_bundle_dir_fd.get(), "../../" + geom_service, "after/" + geom_service);
		create_link(prog, mount_bundle_dirname.c_str(), mount_bundle_dir_fd.get(), "../../" + geom_service, "wants/" + geom_service);
	}

	create_link(prog, mount_bundle_dirname.c_str(), mount_bundle_dir_fd.get(), "/run/service-bundles/early-supervise/" + mount_bundle_dirname, "supervise");

	std::ofstream mount_start, mount_stop;
	open(prog, mount_start, bundle_fullname + "/service/start");
	open(prog, mount_stop, bundle_fullname + "/service/stop");

	mount_start 
		<< "#!/bin/nosh\n" 
		<< multi_line_comment("Start mount " + quote(what) + " " + quote(where) + ".\nAuto-generated by convert-fstab-services.")
		<< "mount\n";
	if (fstype) mount_start << "-t " << quote(fstype) << "\n";
	if (options) mount_start << "-o " << quote(options) << "\n";
	// We just remount the pre-mounted filesystems.
#if defined(__LINUX__) || defined(__linux__)
	if (api || root) mount_start << "-o remount\n";
#else
	if (api || root) mount_start << "-o update\n";
#endif
	mount_start << quote(what) << "\n" << quote(where) << "\n";

	mount_stop 
		<< "#!/bin/nosh\n" 
		<< multi_line_comment("Stop mount " + quote(what) + " " + quote(where) + ".\nAuto-generated by convert-fstab-services.");
	if (api || root) {
		mount_stop << "mount\n";
#if defined(__LINUX__) || defined(__linux__)
		mount_stop << "-o remount\n";
#else
		mount_stop << "-o update\n";
#endif
		if (root)
			mount_stop << "-o ro\n";
	} else
		mount_stop << "umount\n";
	mount_stop << quote(where) << "\n";
}

static inline
void
create_swap_bundle (
	const char * prog,
	const char * what,
	const bool overwrite,
	const bool etc_bundle,
	int bundle_root_fd,
	const char * bundle_root,
	const std::list<std::string> & options_list
) {
	const bool is_target(false);
	const bool root(false);
	const std::string swap_bundle_dirname("swap@" + escape(what));

	if (0 > mkdirat(bundle_root_fd, swap_bundle_dirname.c_str(), 0755)) {
		const int error(errno);
		if (EEXIST != error || !overwrite) {
			std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, bundle_root, swap_bundle_dirname.c_str(), std::strerror(error));
			throw EXIT_FAILURE;
		}
	}

	const FileDescriptorOwner swap_bundle_dir_fd(open_dir_at(bundle_root_fd, swap_bundle_dirname.c_str()));
	if (0 > swap_bundle_dir_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, bundle_root, swap_bundle_dirname.c_str(), std::strerror(error));
		throw EXIT_FAILURE;
	}

	const std::string bundle_fullname(bundle_root + ("/" + swap_bundle_dirname));

	make_service(swap_bundle_dir_fd.get());
	make_orderings_and_relations(swap_bundle_dir_fd.get());
	make_default_dependencies(prog, swap_bundle_dirname.c_str(), is_target, etc_bundle, root, swap_bundle_dir_fd.get());
	make_run_and_restart(prog, bundle_fullname + "/service", "true");

	if (!has_option(options_list, "late"))
		create_links(prog, swap_bundle_dirname.c_str(), is_target, etc_bundle, swap_bundle_dir_fd.get(), "swapauto.target", "wanted-by/");
	else
		create_links(prog, swap_bundle_dirname.c_str(), is_target, etc_bundle, swap_bundle_dir_fd.get(), "swaplate.target", "wanted-by/");

	create_link(prog, swap_bundle_dirname.c_str(), swap_bundle_dir_fd.get(), "/run/service-bundles/early-supervise/" + swap_bundle_dirname, "supervise");

	std::ofstream swap_start, swap_stop;
	open(prog, swap_start, bundle_fullname + "/service/start");
	open(prog, swap_stop, bundle_fullname + "/service/stop");

	std::string val;
	swap_start 
		<< "#!/bin/nosh\n" 
		<< multi_line_comment("Start swap " + quote(what) + ".\nAuto-generated by convert-fstab-services.")
		<< "swapon\n";
	if (has_option(options_list, "discard")) swap_start << "--discard\n";
	if (has_option(options_list, "pri", val)) swap_start << "--priority " << val << "\n";
	swap_start << quote(what) << "\n";

	swap_stop 
		<< "#!/bin/nosh\n" 
		<< multi_line_comment("Stop swap " + quote(what) + ".\nAuto-generated by convert-fstab-services.")
		<< "swapoff\n";
	swap_stop << quote(what) << "\n";
}

static inline
void
create_dump_bundle (
	const char * prog,
	const char * what,
	const bool overwrite,
	const bool etc_bundle,
	int bundle_root_fd,
	const char * bundle_root
) {
	const bool is_target(false);
	const bool root(false);
	const std::string dump_bundle_dirname("dump@" + escape(what));

	if (0 > mkdirat(bundle_root_fd, dump_bundle_dirname.c_str(), 0755)) {
		const int error(errno);
		if (EEXIST != error || !overwrite) {
			std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, bundle_root, dump_bundle_dirname.c_str(), std::strerror(error));
			throw EXIT_FAILURE;
		}
	}

	const FileDescriptorOwner dump_bundle_dir_fd(open_dir_at(bundle_root_fd, dump_bundle_dirname.c_str()));
	if (0 > dump_bundle_dir_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, bundle_root, dump_bundle_dirname.c_str(), std::strerror(error));
		throw EXIT_FAILURE;
	}

	const std::string bundle_fullname(bundle_root + ("/" + dump_bundle_dirname));

	make_service(dump_bundle_dir_fd.get());
	make_orderings_and_relations(dump_bundle_dir_fd.get());
	make_default_dependencies(prog, dump_bundle_dirname.c_str(), is_target, etc_bundle, root, dump_bundle_dir_fd.get());
	make_run_and_restart(prog, bundle_fullname + "/service", "true");

	create_links(prog, dump_bundle_dirname.c_str(), is_target, etc_bundle, dump_bundle_dir_fd.get(), "dumpauto.target", "wanted-by/");

	create_link(prog, dump_bundle_dirname.c_str(), dump_bundle_dir_fd.get(), "/run/service-bundles/early-supervise/" + dump_bundle_dirname, "supervise");

	std::ofstream dump_start, dump_stop;
	open(prog, dump_start, bundle_fullname + "/service/start");
	open(prog, dump_stop, bundle_fullname + "/service/stop");

	dump_start 
		<< "#!/bin/nosh\n" 
		<< multi_line_comment("Start dump " + quote(what) + ".\nAuto-generated by convert-fstab-services.")
		<< "dumpon\n-v\n";
	dump_start << quote(what) << "\n";

	dump_stop 
		<< "#!/bin/nosh\n" 
		<< multi_line_comment("Stop dump " + quote(what) + ".\nAuto-generated by convert-fstab-services.")
		<< "dumpon\noff\n";
}

/* System control subcommands ***********************************************
// **************************************************************************
*/

void
convert_fstab_services ( 
	const char * & next_prog,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	const char * bundle_root(0);
	bool overwrite(false), etc_bundle(false);
	try {
		popt::bool_definition overwrite_option('o', "overwrite", "Update/overwrite an existing service bundle.", overwrite);
		popt::string_definition bundle_option('\0', "bundle-root", "directory", "Root directory for bundles.", bundle_root);
		popt::bool_definition etc_bundle_option('\0', "etc-bundle", "Consider this service to live away from the normal service bundle group.", etc_bundle);
		popt::definition * main_table[] = {
			&overwrite_option,
			&bundle_option,
			&etc_bundle_option
		};
		popt::top_table_definition main_option(sizeof main_table/sizeof *main_table, main_table, "Main options", "");

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
	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Unexpected argument.");
		throw static_cast<int>(EXIT_USAGE);
	}

	if (1 > setfsent()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Unable to open fstab database.");
		throw static_cast<int>(EXIT_TEMPORARY_FAILURE);
	}
	if (bundle_root)
		mkdirat(AT_FDCWD, bundle_root, 0755);
	else
		bundle_root = ".";
	const FileDescriptorOwner bundle_root_fd(open_dir_at(AT_FDCWD, bundle_root));
	if (0 > bundle_root_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, bundle_root, std::strerror(error));
		throw EXIT_FAILURE;
	}
	while (struct fstab * entry = getfsent()) {
		const char * what(entry->fs_spec);
		const char * where(entry->fs_file);
		const char * type(entry->fs_type);

		if (!what || !where) continue;

		const std::list<std::string> options_list(split_fstab_options(entry->fs_mntops));

		if ((0 == std::strcmp(type, "rw"))
		||  (0 == std::strcmp(type, "rq"))
		||  (0 == std::strcmp(type, "ro"))
		) {
			const std::string fsck_bundle_dirname("fsck@" + escape(where));
			const std::string mount_bundle_dirname("mount@" + escape(where));
			const bool local(!has_option(options_list, "_nodev") && is_local_type(entry->fs_vfstype));
			const bool preenable(is_preenable_type(entry->fs_vfstype));
			std::string gbde, geli, fuse;
			const bool is_fuse(begins_with(basename_of(what), "fuse", fuse) && fuse.length() > 1 && std::isdigit(fuse[0]));
			const bool is_gbde(ends_in(what, ".bde", gbde));
			const bool is_geli(ends_in(what, ".eli", geli));

			if (entry->fs_passno > 0)
				create_fsck_bundle(prog, what, preenable, local, overwrite, is_fuse, is_gbde ? &gbde : 0, is_geli ? &geli : 0, etc_bundle, bundle_root_fd.get(), bundle_root, fsck_bundle_dirname, mount_bundle_dirname);
			create_mount_bundle(prog, what, where, entry->fs_vfstype, entry->fs_mntops, local, overwrite, is_fuse, is_gbde ? &gbde : 0, is_geli ? &geli : 0, etc_bundle, bundle_root_fd.get(), bundle_root, mount_bundle_dirname);
		} else
		if (0 == std::strcmp(type, "sw")) {
			create_swap_bundle(prog, what, overwrite, etc_bundle, bundle_root_fd.get(), bundle_root, options_list);
			create_dump_bundle(prog, what, overwrite, etc_bundle, bundle_root_fd.get(), bundle_root);
		}
	}
	endfsent();

	throw EXIT_SUCCESS;
}
