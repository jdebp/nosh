/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <map>
#include <set>
#include <iostream>
#include <sstream>
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
#include "common-manager.h"	// Because we need to know about API mounts.
#include "popt.h"
#include "FileDescriptorOwner.h"
#include "bundle_creation.h"

/* Utilities ****************************************************************
// **************************************************************************
*/

static inline std::string quote ( const std::string & s ) { return quote_for_nosh(s); }

static inline
std::string
join (
	const std::list<std::string> & a,
	const std::string & s
) {
	std::string r;
	for (std::list<std::string>::const_iterator b(a.begin()), e(a.end()), i(b); e != i; ++i) {
		if (b != i) r += s;
		r += *i;
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

#if defined(__LINUX__) || defined(__linux__)
static inline
bool 
is_swap_type (
	const char * fstype
) {
	return 
		0 == std::strcmp(fstype, "swap")
	;
}
#endif

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

/// Wrapper function because we never generate per-user stuff and anything fstab-related in /etc/service-bundles only ever links to services and targets that are also etc bundles.
static inline
void
create_links (
	const char * prog,
	const ProcessEnvironment & envs,
	const std::string & bund,
	const bool is_target,
	const bool etc_bundle,
	const FileDescriptorOwner & bundle_dir_fd,
	const std::string & names,
	const std::string & subdir
) {
	create_links(prog, envs, bund, false, is_target, etc_bundle, etc_bundle, bundle_dir_fd, names, subdir);
}

static 
void
make_default_dependencies (
	const char * prog,
	const ProcessEnvironment & envs,
	const std::string & name,
	const bool is_target,
	const bool etc_bundle,
	const bool root,
	const FileDescriptorOwner & bundle_dir_fd
) {
	create_links(prog, envs, name, is_target, etc_bundle, bundle_dir_fd, "shutdown.target", "before/");
	if (!root)
		create_links(prog, envs, name, is_target, etc_bundle, bundle_dir_fd, "unmount.target", "stopped-by/");
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
	std::ofstream restart, run;
	open(prog, restart, service_dirname + "/restart");
	open(prog, run, service_dirname + "/run");
	run << "#!/bin/nosh\n" << multi_line_comment("Run file for a mount service.") << "true";
	restart << "#!/bin/sh\n" << multi_line_comment("Restart file for a mount service.") << "exec " << condition << "\t#Ignore script arguments.";
}

static
void
make_remain(
	const char * prog,
	const std::string & service_dirname,
	const FileDescriptorOwner & service_dir_fd,
	bool run_on_empty
) {
	flag_file(prog, service_dirname, service_dir_fd, "remain", run_on_empty);
}

static inline
void
create_gbde_bundle (
	const char * prog,
	const ProcessEnvironment & envs,
	const char * what,
	const bool local,
	const bool overwrite,
	const bool etc_bundle,
	const FileDescriptorOwner & bundle_root_fd,
	const char * bundle_root,
	const std::string & gbde,
	const std::string * fsck_bundle_dirname,
	const std::string & mount_bundle_dirname
) {
	const bool is_target(false);
	const bool root(false);
	const std::string gbde_bundle_dirname("gbde@" + systemd_name_escape(false, false, gbde));

	if (0 > mkdirat(bundle_root_fd.get(), gbde_bundle_dirname.c_str(), 0755)) {
		const int error(errno);
		if (EEXIST != error || !overwrite) {
			std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, bundle_root, gbde_bundle_dirname.c_str(), std::strerror(error));
			throw EXIT_FAILURE;
		}
	}

	const FileDescriptorOwner gbde_bundle_dir_fd(open_dir_at(bundle_root_fd.get(), gbde_bundle_dirname.c_str()));
	if (0 > gbde_bundle_dir_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, bundle_root, gbde_bundle_dirname.c_str(), std::strerror(error));
		throw EXIT_FAILURE;
	}

	const std::string bundle_fullname(bundle_root + ("/" + gbde_bundle_dirname));

	make_service(gbde_bundle_dir_fd.get());
	const FileDescriptorOwner gbde_service_dir_fd(open_service_dir(gbde_bundle_dir_fd.get()));
	make_orderings_and_relations(gbde_bundle_dir_fd.get());
	make_default_dependencies(prog, envs, gbde_bundle_dirname, is_target, etc_bundle, root, gbde_bundle_dir_fd);
	make_run_and_restart(prog, bundle_fullname + "/service", "false");
	make_remain(prog, bundle_fullname + "/service", gbde_service_dir_fd, true);

	const char * const pre_target(local ? "local-fs-pre.target" : "remote-fs-pre.target");
	create_links(prog, envs, gbde_bundle_dirname, is_target, etc_bundle, gbde_bundle_dir_fd, pre_target, "after/");
	if (fsck_bundle_dirname) {
		create_link(prog, gbde_bundle_dirname, gbde_bundle_dir_fd, "../../" + *fsck_bundle_dirname, "before/" + *fsck_bundle_dirname);
		create_link(prog, gbde_bundle_dirname, gbde_bundle_dir_fd, "../../" + *fsck_bundle_dirname, "wanted-by/" + *fsck_bundle_dirname);
	}
	create_link(prog, gbde_bundle_dirname, gbde_bundle_dir_fd, "../../" + mount_bundle_dirname, "before/" + mount_bundle_dirname);
	create_link(prog, gbde_bundle_dirname, gbde_bundle_dir_fd, "../../" + mount_bundle_dirname, "wanted-by/" + mount_bundle_dirname);

	create_link(prog, gbde_bundle_dirname, gbde_bundle_dir_fd, "/run/service-bundles/early-supervise/" + gbde_bundle_dirname, "supervise");

	std::ofstream gbde_start, gbde_stop;
	open(prog, gbde_start, bundle_fullname + "/service/start");
	open(prog, gbde_stop, bundle_fullname + "/service/stop");

	gbde_start 
		<< "#!/bin/nosh\n" 
		<< multi_line_comment("Start gbde " + quote(what) + ".\nAuto-generated by " + std::string(prog) + ".")
		<< "sh -c 'exec gbde attach ${flags} " << quote(gbde) << " ${lock:+-l \"${lock}\"}'\n";

	gbde_stop 
		<< "#!/bin/nosh\n" 
		<< multi_line_comment("Stop gbde " + quote(what) + ".\nAuto-generated by " + std::string(prog) + ".")
		<< "gbde detach\n" << quote(gbde) << "\n";
}

static inline
void
create_geli_bundle (
	const char * prog,
	const ProcessEnvironment & envs,
	const char * what,
	const bool local,
	const bool overwrite,
	const bool etc_bundle,
	const FileDescriptorOwner & bundle_root_fd,
	const char * bundle_root,
	const std::string & geli,
	const std::string * fsck_bundle_dirname,
	const std::string & mount_bundle_dirname,
	const bool swap,
	const std::list<std::string> & options_list
) {
	const bool is_target(false);
	const bool root(false);
	const std::string geli_bundle_dirname("geli@" + systemd_name_escape(false, false, geli));

	if (0 > mkdirat(bundle_root_fd.get(), geli_bundle_dirname.c_str(), 0755)) {
		const int error(errno);
		if (EEXIST != error || !overwrite) {
			std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, bundle_root, geli_bundle_dirname.c_str(), std::strerror(error));
			throw EXIT_FAILURE;
		}
	}

	const FileDescriptorOwner geli_bundle_dir_fd(open_dir_at(bundle_root_fd.get(), geli_bundle_dirname.c_str()));
	if (0 > geli_bundle_dir_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, bundle_root, geli_bundle_dirname.c_str(), std::strerror(error));
		throw EXIT_FAILURE;
	}

	const std::string bundle_fullname(bundle_root + ("/" + geli_bundle_dirname));

	make_service(geli_bundle_dir_fd.get());
	const FileDescriptorOwner geli_service_dir_fd(open_service_dir(geli_bundle_dir_fd.get()));
	make_orderings_and_relations(geli_bundle_dir_fd.get());
	make_default_dependencies(prog, envs, geli_bundle_dirname, is_target, etc_bundle, root, geli_bundle_dir_fd);
	make_run_and_restart(prog, bundle_fullname + "/service", "false");
	make_remain(prog, bundle_fullname + "/service", geli_service_dir_fd, true);

	const char * const pre_target(local ? "local-fs-pre.target" : "remote-fs-pre.target");
	create_links(prog, envs, geli_bundle_dirname, is_target, etc_bundle, geli_bundle_dir_fd, pre_target, "after/");
	if (fsck_bundle_dirname) {
		create_link(prog, geli_bundle_dirname, geli_bundle_dir_fd, "../../" + *fsck_bundle_dirname, "before/" + *fsck_bundle_dirname);
		create_link(prog, geli_bundle_dirname, geli_bundle_dir_fd, "../../" + *fsck_bundle_dirname, "wanted-by/" + *fsck_bundle_dirname);
	}
	create_link(prog, geli_bundle_dirname, geli_bundle_dir_fd, "../../" + mount_bundle_dirname, "before/" + mount_bundle_dirname);
	create_link(prog, geli_bundle_dirname, geli_bundle_dir_fd, "../../" + mount_bundle_dirname, "wanted-by/" + mount_bundle_dirname);

	create_link(prog, geli_bundle_dirname, geli_bundle_dir_fd, "/run/service-bundles/early-supervise/" + geli_bundle_dirname, "supervise");

	std::ofstream geli_start, geli_stop;
	open(prog, geli_start, bundle_fullname + "/service/start");
	open(prog, geli_stop, bundle_fullname + "/service/stop");

	const char * verb(swap ? "onetime" : "attach");
	std::stringstream start_opts;
	std::string val;
	if (has_option(options_list, "aalgo", val)) start_opts << " -a " << val;
	if (has_option(options_list, "ealgo", val)) start_opts << " -a " << val;
	if (has_option(options_list, "keylen", val)) start_opts << " -l " << val;
	if (has_option(options_list, "sectorsize", val)) start_opts << " -s " << val; /* FIXME: \bug else << " -s " << pagesize()@; */

	geli_start 
		<< "#!/bin/nosh\n" 
		<< multi_line_comment("Start geli " + quote(what) + ".\nAuto-generated by " + std::string(prog) + ".")
		<< "foreground sh -c 'exec geli " << verb << start_opts.str() << " ${flags} " << quote(geli) << "' ;\n"
		<< "sh -c 'test -n \"${autodetach}\" && exec geli detach -l " << quote(geli) << "'\n";

	geli_stop 
		<< "#!/bin/nosh\n" 
		<< multi_line_comment("Stop geli " + quote(what) + ".\nAuto-generated by " + std::string(prog) + ".")
		<< "geli detach\n" << quote(geli) << "\n";
}

static inline
void
create_fsck_bundle (
	const char * prog,
	const ProcessEnvironment & envs,
	const char * what,
#if defined(__LINUX__) || defined(__linux__)
	const bool preenable,
#else
	const bool /*preenable*/,
#endif
	const bool local,
	const bool overwrite,
	const bool fuse,
	const std::string * gbde,
	const std::string * geli,
	const bool etc_bundle,
	const FileDescriptorOwner & bundle_root_fd,
	const char * bundle_root,
	const std::string & fsck_bundle_dirname,
	const std::string & mount_bundle_dirname
) {
	const bool is_target(false);
	const bool root(false);

	if (0 > mkdirat(bundle_root_fd.get(), fsck_bundle_dirname.c_str(), 0755)) {
		const int error(errno);
		if (EEXIST != error || !overwrite) {
			std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, bundle_root, fsck_bundle_dirname.c_str(), std::strerror(error));
			throw EXIT_FAILURE;
		}
	}

	const FileDescriptorOwner fsck_bundle_dir_fd(open_dir_at(bundle_root_fd.get(), fsck_bundle_dirname.c_str()));
	if (0 > fsck_bundle_dir_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, bundle_root, fsck_bundle_dirname.c_str(), std::strerror(error));
		throw EXIT_FAILURE;
	}

	const std::string bundle_fullname(bundle_root + ("/" + fsck_bundle_dirname));

	make_service(fsck_bundle_dir_fd.get());
	const FileDescriptorOwner fsck_service_dir_fd(open_service_dir(fsck_bundle_dir_fd.get()));
	make_orderings_and_relations(fsck_bundle_dir_fd.get());
	make_default_dependencies(prog, envs, fsck_bundle_dirname, is_target, etc_bundle, root, fsck_bundle_dir_fd);
	make_run_and_restart(prog, bundle_fullname + "/service", "false");
	/// \bug FIXME: Should be false when we get system-control capable of dealing with this.
	make_remain(prog, bundle_fullname + "/service", fsck_service_dir_fd, true);

	const char * const pre_target(local ? "local-fs-pre.target" : "remote-fs-pre.target");
	create_links(prog, envs, fsck_bundle_dirname, is_target, etc_bundle, fsck_bundle_dir_fd, pre_target, "after/");
	// fsck is fairly memory hungry, so we want (non-late) swap available when it is running.
	create_links(prog, envs, fsck_bundle_dirname, is_target, etc_bundle, fsck_bundle_dir_fd, "swapauto.target", "after/");
	create_link(prog, fsck_bundle_dirname, fsck_bundle_dir_fd, "../../" + mount_bundle_dirname, "before/" + mount_bundle_dirname);
	create_link(prog, fsck_bundle_dirname, fsck_bundle_dir_fd, "../../" + mount_bundle_dirname, "wanted-by/" + mount_bundle_dirname);

	if (fuse) {
		create_link(prog, fsck_bundle_dirname, fsck_bundle_dir_fd, "../../kmod@fuse", "after/kmod@fuse");
		create_link(prog, fsck_bundle_dirname, fsck_bundle_dir_fd, "../../kmod@fuse", "wants/kmod@fuse");
	}
	if (gbde) {
		const std::string geom_service("gbde@" + systemd_name_escape(false, false, *gbde));
		create_link(prog, fsck_bundle_dirname, fsck_bundle_dir_fd, "../../" + geom_service, "after/" + geom_service);
		create_link(prog, fsck_bundle_dirname, fsck_bundle_dir_fd, "../../" + geom_service, "wants/" + geom_service);
	}
	if (geli) {
		const std::string geom_service("geli@" + systemd_name_escape(false, false, *geli));
		create_link(prog, fsck_bundle_dirname, fsck_bundle_dir_fd, "../../" + geom_service, "after/" + geom_service);
		create_link(prog, fsck_bundle_dirname, fsck_bundle_dir_fd, "../../" + geom_service, "wants/" + geom_service);
	}

	create_link(prog, fsck_bundle_dirname, fsck_bundle_dir_fd, "/run/service-bundles/early-supervise/" + fsck_bundle_dirname, "supervise");

	std::ofstream fsck_start, fsck_stop;
	open(prog, fsck_start, bundle_fullname + "/service/start");
	open(prog, fsck_stop, bundle_fullname + "/service/stop");

	fsck_start 
		<< "#!/bin/nosh\n" 
		<< multi_line_comment("Start fsck " + quote(what) + ".\nAuto-generated by " + std::string(prog) + ".")
		<< "monitored-fsck\n"
#if defined(__LINUX__) || defined(__linux__)
		<< (preenable ? "-p # preen mode\n" : "-a # unattended mode\n")
#else
		"-C # Skip if clean.\n-p # preen mode\n"
#endif
		<< quote(what) << "\n";

	fsck_stop 
		<< "#!/bin/nosh\n" 
		<< multi_line_comment("Stop fsck " + quote(what) + ".\nAuto-generated by " + std::string(prog) + ".")
		<< "true\n";
}

static inline
void
create_mount_bundle (
	const char * prog,
	const ProcessEnvironment & envs,
	const char * what,
	const char * where,
	const char * fstype,
	const std::list<std::string> & options,
	const bool local,
	const bool overwrite,
	const std::list<std::string> & modules,
	const std::string * gbde,
	const std::string * geli,
	const bool etc_bundle,
	const FileDescriptorOwner & bundle_root_fd,
	const char * bundle_root,
	const std::string & mount_bundle_dirname
) {
	const bool is_target(false);
	const bool root(is_root(where));
	const bool api(is_api_mountpoint(where));

	if (0 > mkdirat(bundle_root_fd.get(), mount_bundle_dirname.c_str(), 0755)) {
		const int error(errno);
		if (EEXIST != error || !overwrite) {
			std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, bundle_root, mount_bundle_dirname.c_str(), std::strerror(error));
			throw EXIT_FAILURE;
		}
	}

	const FileDescriptorOwner mount_bundle_dir_fd(open_dir_at(bundle_root_fd.get(), mount_bundle_dirname.c_str()));
	if (0 > mount_bundle_dir_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, bundle_root, mount_bundle_dirname.c_str(), std::strerror(error));
		throw EXIT_FAILURE;
	}

	const std::string bundle_fullname(bundle_root + ("/" + mount_bundle_dirname));

	make_service(mount_bundle_dir_fd.get());
	const FileDescriptorOwner mount_service_dir_fd(open_service_dir(mount_bundle_dir_fd.get()));
	make_orderings_and_relations(mount_bundle_dir_fd.get());
	make_default_dependencies(prog, envs, mount_bundle_dirname, is_target, etc_bundle, root, mount_bundle_dir_fd);
	make_run_and_restart(prog, bundle_fullname + "/service", "true");
	make_remain(prog, bundle_fullname + "/service", mount_service_dir_fd, true);

	const char * const target(local ? "local-fs.target" : "remote-fs.target");
	const char * const pre_target(local ? "local-fs-pre.target" : "remote-fs-pre.target");
	if (!local)
		create_links(prog, envs, mount_bundle_dirname, is_target, etc_bundle, mount_bundle_dir_fd, "fs-servers.target", "after/");
	create_links(prog, envs, mount_bundle_dirname, is_target, etc_bundle, mount_bundle_dir_fd, target, "wanted-by/");
	create_links(prog, envs, mount_bundle_dirname, is_target, etc_bundle, mount_bundle_dir_fd, target, "before/");
	create_links(prog, envs, mount_bundle_dirname, is_target, etc_bundle, mount_bundle_dir_fd, pre_target, "after/");

	for (std::list<std::string>::const_iterator p(modules.begin()); modules.end() != p; ++p) {
		const std::string & modname(*p);
		create_link(prog, mount_bundle_dirname, mount_bundle_dir_fd, "../../kmod@" + modname, "after/kmod@" + modname);
		create_link(prog, mount_bundle_dirname, mount_bundle_dir_fd, "../../kmod@" + modname, "wants/kmod@" + modname);
	}
	if (gbde) {
		const std::string geom_service("gbde@" + systemd_name_escape(false, false, *gbde));
		create_link(prog, mount_bundle_dirname, mount_bundle_dir_fd, "../../" + geom_service, "after/" + geom_service);
		create_link(prog, mount_bundle_dirname, mount_bundle_dir_fd, "../../" + geom_service, "wants/" + geom_service);
	}
	if (geli) {
		const std::string geom_service("geli@" + systemd_name_escape(false, false, *geli));
		create_link(prog, mount_bundle_dirname, mount_bundle_dir_fd, "../../" + geom_service, "after/" + geom_service);
		create_link(prog, mount_bundle_dirname, mount_bundle_dir_fd, "../../" + geom_service, "wants/" + geom_service);
	}

	make_mount_interdependencies(prog, mount_bundle_dirname, etc_bundle, root, mount_bundle_dir_fd, where);

	create_link(prog, mount_bundle_dirname, mount_bundle_dir_fd, "/run/service-bundles/early-supervise/" + mount_bundle_dirname, "supervise");

	std::ofstream mount_start, mount_stop;
	open(prog, mount_start, bundle_fullname + "/service/start");
	open(prog, mount_stop, bundle_fullname + "/service/stop");

	mount_start 
		<< "#!/bin/nosh\n" 
		<< multi_line_comment("Start mount " + quote(what) + " " + quote(where) + ".\nAuto-generated by " + std::string(prog) + ".")
		<< "mount\n";
	if (fstype) mount_start << "-t " << quote(fstype) << "\n";
	if (!options.empty()) mount_start << "-o " << quote(join(options, std::string(","))) << "\n";
	// We just remount the pre-mounted filesystems.
#if defined(__LINUX__) || defined(__linux__)
	if (api || root) mount_start << "-o remount\n";
#else
	if (api || root) mount_start << "-o update\n";
	if (root)
		mount_start << "-o rw\n";
#endif
	mount_start << quote(what) << "\n" << quote(where) << "\n";

	mount_stop 
		<< "#!/bin/nosh\n" 
		<< multi_line_comment("Stop mount " + quote(what) + " " + quote(where) + ".\nAuto-generated by " + std::string(prog) + ".");
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
	const ProcessEnvironment & envs,
	const char * what,
	const bool overwrite,
	const bool etc_bundle,
	const FileDescriptorOwner & bundle_root_fd,
	const char * bundle_root,
	const std::string & swap_bundle_dirname,
	const std::list<std::string> & options_list
) {
	const bool is_target(false);
	const bool root(false);

	if (0 > mkdirat(bundle_root_fd.get(), swap_bundle_dirname.c_str(), 0755)) {
		const int error(errno);
		if (EEXIST != error || !overwrite) {
			std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, bundle_root, swap_bundle_dirname.c_str(), std::strerror(error));
			throw EXIT_FAILURE;
		}
	}

	const FileDescriptorOwner swap_bundle_dir_fd(open_dir_at(bundle_root_fd.get(), swap_bundle_dirname.c_str()));
	if (0 > swap_bundle_dir_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, bundle_root, swap_bundle_dirname.c_str(), std::strerror(error));
		throw EXIT_FAILURE;
	}

	const std::string bundle_fullname(bundle_root + ("/" + swap_bundle_dirname));

	make_service(swap_bundle_dir_fd.get());
	const FileDescriptorOwner swap_service_dir_fd(open_service_dir(swap_bundle_dir_fd.get()));
	make_orderings_and_relations(swap_bundle_dir_fd.get());
	make_default_dependencies(prog, envs, swap_bundle_dirname, is_target, etc_bundle, root, swap_bundle_dir_fd);
	make_run_and_restart(prog, bundle_fullname + "/service", "true");
	make_remain(prog, bundle_fullname + "/service", swap_service_dir_fd, true);

	if (!has_option(options_list, "late"))
		create_links(prog, envs, swap_bundle_dirname, is_target, etc_bundle, swap_bundle_dir_fd, "swapauto.target", "wanted-by/");
	else
		create_links(prog, envs, swap_bundle_dirname, is_target, etc_bundle, swap_bundle_dir_fd, "swaplate.target", "wanted-by/");

	create_link(prog, swap_bundle_dirname, swap_bundle_dir_fd, "/run/service-bundles/early-supervise/" + swap_bundle_dirname, "supervise");

	std::ofstream swap_start, swap_stop;
	open(prog, swap_start, bundle_fullname + "/service/start");
	open(prog, swap_stop, bundle_fullname + "/service/stop");

	std::string val;
	swap_start 
		<< "#!/bin/nosh\n" 
		<< multi_line_comment("Start swap " + quote(what) + ".\nAuto-generated by " + std::string(prog) + ".")
		<< "swapon\n";
	if (has_option(options_list, "discard")) swap_start << "--discard\n";
	if (has_option(options_list, "pri", val)) swap_start << "--priority " << val << "\n";
	swap_start << quote(what) << "\n";

	swap_stop 
		<< "#!/bin/nosh\n" 
		<< multi_line_comment("Stop swap " + quote(what) + ".\nAuto-generated by " + std::string(prog) + ".")
		<< "swapoff\n";
	swap_stop << quote(what) << "\n";
}

static inline
void
create_dump_bundle (
	const char * prog,
	const ProcessEnvironment & envs,
	const char * what,
	const bool overwrite,
	const bool etc_bundle,
	const FileDescriptorOwner & bundle_root_fd,
	const char * bundle_root,
	const std::string & swap_bundle_dirname
) {
	const bool is_target(false);
	const bool root(false);
	const std::string dump_bundle_dirname("dump@" + systemd_name_escape(false, false, what));

	if (0 > mkdirat(bundle_root_fd.get(), dump_bundle_dirname.c_str(), 0755)) {
		const int error(errno);
		if (EEXIST != error || !overwrite) {
			std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, bundle_root, dump_bundle_dirname.c_str(), std::strerror(error));
			throw EXIT_FAILURE;
		}
	}

	const FileDescriptorOwner dump_bundle_dir_fd(open_dir_at(bundle_root_fd.get(), dump_bundle_dirname.c_str()));
	if (0 > dump_bundle_dir_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, bundle_root, dump_bundle_dirname.c_str(), std::strerror(error));
		throw EXIT_FAILURE;
	}

	const std::string bundle_fullname(bundle_root + ("/" + dump_bundle_dirname));

	make_service(dump_bundle_dir_fd.get());
	const FileDescriptorOwner dump_service_dir_fd(open_service_dir(dump_bundle_dir_fd.get()));
	make_orderings_and_relations(dump_bundle_dir_fd.get());
	make_default_dependencies(prog, envs, dump_bundle_dirname, is_target, etc_bundle, root, dump_bundle_dir_fd);
	make_run_and_restart(prog, bundle_fullname + "/service", "true");
	make_remain(prog, bundle_fullname + "/service", dump_service_dir_fd, true);

	create_links(prog, envs, dump_bundle_dirname, is_target, etc_bundle, dump_bundle_dir_fd, "dumpauto.target", "wanted-by/");

	create_link(prog, dump_bundle_dirname, dump_bundle_dir_fd, "../../" + swap_bundle_dirname, "after/" + swap_bundle_dirname);

	create_link(prog, dump_bundle_dirname, dump_bundle_dir_fd, "/run/service-bundles/early-supervise/" + dump_bundle_dirname, "supervise");

	std::ofstream dump_start, dump_stop;
	open(prog, dump_start, bundle_fullname + "/service/start");
	open(prog, dump_stop, bundle_fullname + "/service/stop");

	dump_start 
		<< "#!/bin/nosh\n" 
		<< multi_line_comment("Start dump " + quote(what) + ".\nAuto-generated by " + std::string(prog) + ".")
		<< "dumpon\n-v\n";
	dump_start << quote(what) << "\n";

	dump_stop 
		<< "#!/bin/nosh\n" 
		<< multi_line_comment("Stop dump " + quote(what) + ".\nAuto-generated by " + std::string(prog) + ".")
		<< "dumpon\noff\n";
}

static inline
void
create_regular_bundles (
	const char * prog,
	const ProcessEnvironment & envs,
	const char * what,
	const char * where,
	const char * vfstype,
	const bool overwrite,
	const bool etc_bundle,
	const FileDescriptorOwner & bundle_root_fd,
	const char * bundle_root,
	const bool netdev,
	const bool want_fsck,
	const std::list<std::string> & options_list
) {
	const std::string fsck_bundle_dirname("fsck@" + systemd_name_escape(false, false, where));
	const std::string mount_bundle_dirname("mount@" + systemd_name_escape(false, false, where));
	const bool local(!netdev && is_local_type(vfstype));
	const bool preenable(is_preenable_type(vfstype));
	std::string gbde, geli, fuse;
	const bool is_fuse(begins_with(basename_of(what), "fuse", fuse) && fuse.length() > 1 && std::isdigit(fuse[0]));
	const bool is_gbde(ends_in(what, ".bde", gbde));
	const bool is_geli(ends_in(what, ".eli", geli));

	std::list<std::string> modules;
	if (is_fuse)
		modules.push_back("fuse");
	if (0 == std::strcmp(vfstype, "efivarfs"))
		modules.push_back(vfstype);

	if (is_gbde)
		create_gbde_bundle(prog, envs, what, local, overwrite, etc_bundle, bundle_root_fd, bundle_root, gbde, &fsck_bundle_dirname, mount_bundle_dirname);
	if (is_geli)
		create_geli_bundle(prog, envs, what, local, overwrite, etc_bundle, bundle_root_fd, bundle_root, geli, &fsck_bundle_dirname, mount_bundle_dirname, false, options_list);
	if (want_fsck)
		create_fsck_bundle(prog, envs, what, preenable, local, overwrite, is_fuse, is_gbde ? &gbde : 0, is_geli ? &geli : 0, etc_bundle, bundle_root_fd, bundle_root, fsck_bundle_dirname, mount_bundle_dirname);
	create_mount_bundle(prog, envs, what, where, vfstype, options_list, local, overwrite, modules, is_gbde ? &gbde : 0, is_geli ? &geli : 0, etc_bundle, bundle_root_fd, bundle_root, mount_bundle_dirname);
}

/* System control subcommands ***********************************************
// **************************************************************************
*/

void
convert_fstab_services [[gnu::noreturn]] ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
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
		throw static_cast<int>(EXIT_USAGE);
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

		std::list<std::string> options_list(split_fstab_options(entry->fs_mntops));
		const bool netdev(has_option(options_list, "_netdev"));
		delete_fstab_option(options_list, "_netdev");
		delete_fstab_option(options_list, "noauto");
		delete_fstab_option(options_list, "nofail");
		delete_fstab_option(options_list, "auto");

#if defined(__LINUX__) || defined(__linux__)
		const bool is_vfs_swap(!netdev && is_swap_type(entry->fs_vfstype));
#endif
		if (0 == std::strcmp(type, "xx")) {
			continue;
		} else
		if ((0 == std::strcmp(type, "rw"))
		||  (0 == std::strcmp(type, "rq"))
		||  (0 == std::strcmp(type, "ro"))
#if defined(__LINUX__) || defined(__linux__)
		||  (!is_vfs_swap && 0 == std::strcmp(type, "??"))
#endif
		) {
			const bool want_fsck(entry->fs_passno > 0);
			create_regular_bundles(prog, envs, what, where, entry->fs_vfstype, overwrite, etc_bundle, bundle_root_fd, bundle_root, netdev, want_fsck, options_list);
		} else
		if (0 == std::strcmp(type, "sw")
#if defined(__LINUX__) || defined(__linux__)
		||  (is_vfs_swap && 0 == std::strcmp(type, "??"))
#endif
		) {
			const bool local(true);
			const std::string swap_bundle_dirname("swap@" + systemd_name_escape(false, false, what));
			std::string gbde, geli;
			const bool is_gbde(ends_in(what, ".bde", gbde));
			const bool is_geli(ends_in(what, ".eli", geli));
			if (is_gbde)
				create_gbde_bundle(prog, envs, what, local, overwrite, etc_bundle, bundle_root_fd, bundle_root, gbde, 0, swap_bundle_dirname);
			if (is_geli)
				create_geli_bundle(prog, envs, what, local, overwrite, etc_bundle, bundle_root_fd, bundle_root, geli, 0, swap_bundle_dirname, true, options_list);
			create_swap_bundle(prog, envs, what, overwrite, etc_bundle, bundle_root_fd, bundle_root, swap_bundle_dirname, options_list);
			create_dump_bundle(prog, envs, what, overwrite, etc_bundle, bundle_root_fd, bundle_root, swap_bundle_dirname);
		} else
			std::fprintf(stderr, "%s: WARNING: %s: %s: %s\n", prog, where, type, "Unrecognized type.");
	}
	endfsent();

	throw EXIT_SUCCESS;
}

void
write_volume_service_bundles [[gnu::noreturn]] ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	const char * bundle_root(0);
	const char * mntops(0);
	bool overwrite(false), etc_bundle(false), want_fsck(false);
	try {
		popt::bool_definition overwrite_option('o', "overwrite", "Update/overwrite existing service bundles.", overwrite);
		popt::string_definition bundle_option('\0', "bundle-root", "directory", "Root directory for bundles.", bundle_root);
		popt::string_definition mntops_option('\0', "mount-options", "list", "Mount options.", mntops);
		popt::bool_definition etc_bundle_option('\0', "etc-bundle", "Consider this service to live away from the normal service bundle group.", etc_bundle);
		popt::bool_definition want_fsck_option('\0', "want-fsck", "Create an fsck service bundle as well.", want_fsck);
		popt::definition * main_table[] = {
			&overwrite_option,
			&bundle_option,
			&mntops_option,
			&etc_bundle_option,
			&want_fsck_option,
		};
		popt::top_table_definition main_option(sizeof main_table/sizeof *main_table, main_table, "Main options", "{fstype} {source} {directory}");

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

	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing vfs name.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * vfstype(args.front());
	args.erase(args.begin());
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing device name.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * what(args.front());
	args.erase(args.begin());
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing directory name.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * where(args.front());
	args.erase(args.begin());
	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Unexpected argument.");
		throw static_cast<int>(EXIT_USAGE);
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

	std::list<std::string> options_list(split_fstab_options(mntops));
	const bool netdev(has_option(options_list, "_netdev"));
	delete_fstab_option(options_list, "_netdev");
	delete_fstab_option(options_list, "noauto");
	delete_fstab_option(options_list, "nofail");
	delete_fstab_option(options_list, "auto");

	create_regular_bundles(prog, envs, what, where, vfstype, overwrite, etc_bundle, bundle_root_fd, bundle_root, netdev, want_fsck, options_list);

	throw EXIT_SUCCESS;
}
