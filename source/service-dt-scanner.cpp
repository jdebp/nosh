/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstring>
#include <csignal>
#include <cerrno>
#include <stdint.h>
#include <sys/types.h>
#if defined(__LINUX__) || defined(__linux__)
#include "kqueue_linux.h"
#else
#include <sys/event.h>
#endif
#include <dirent.h>
#include <unistd.h>
#include "popt.h"
#include "utils.h"
#include "fdutils.h"
#include "service-manager-client.h"
#include "FileDescriptorOwner.h"
#include "DirStar.h"

/* Scanning *****************************************************************
// **************************************************************************
*/

static 
void
rescan (
	const char * prog,
	const char * name,
	const int socket_fd,
	const int retained_scan_dir_fd,
	const bool input_activation
) {
	FileDescriptorOwner scan_dir_fd(dup(retained_scan_dir_fd));
	if (0 > scan_dir_fd.get()) {
exit_scan:
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, name, std::strerror(error));
		return;
	}
	const DirStar scan_dir(scan_dir_fd);
	if (!scan_dir) goto exit_scan;
	rewinddir(scan_dir);	// because the last pass left it at EOF.
	for (;;) {
		errno = 0;
		const dirent * entry(readdir(scan_dir));
		if (!entry) {
			if (errno) goto exit_scan;
			break;
		}
#if defined(_DIRENT_HAVE_D_NAMLEN)
		if (1 > entry->d_namlen) continue;
		if (sizeof(service_manager_rpc_message::name) > entry->d_namlen) continue;
#endif
		if ('.' == entry->d_name[0]) continue;
#if defined(_DIRENT_HAVE_D_TYPE)
		if (DT_DIR != entry->d_type && DT_LNK != entry->d_type) continue;
#endif

		const int bundle_dir_fd(open_dir_at(scan_dir.fd(), entry->d_name));
		if (0 <= bundle_dir_fd) {
			int service_dir_fd(open_service_dir(bundle_dir_fd));
			if (0 <= service_dir_fd) {
				make_supervise(bundle_dir_fd);
				const int supervise_dir_fd(open_supervise_dir(bundle_dir_fd));
				if (0 <= supervise_dir_fd) {
					const bool was_already_loaded(is_ok(supervise_dir_fd));
					if (!was_already_loaded) {
						make_supervise_fifos(supervise_dir_fd);
						load(prog, socket_fd, entry->d_name, supervise_dir_fd, service_dir_fd);
					}
					const int log_bundle_dir_fd(open_dir_at(bundle_dir_fd, "log/"));
					if (0 <= log_bundle_dir_fd) {
						char log_name[NAME_MAX + sizeof "/log"];
						std::strncpy(log_name, entry->d_name, sizeof log_name);
#if defined(_DIRENT_HAVE_D_NAMLEN)
						std::strncat(log_name, "/log", sizeof log_name - entry->d_namelen);
#else
						std::strncat(log_name, "/log", sizeof log_name - std::strlen(entry->d_name));
#endif
						int log_service_dir_fd(open_service_dir(log_bundle_dir_fd));
						if (0 <= log_service_dir_fd) {
							make_supervise(log_bundle_dir_fd);
							const int log_supervise_dir_fd(open_supervise_dir(log_bundle_dir_fd));
							if (0 <= log_supervise_dir_fd) {
								const bool log_was_already_loaded(is_ok(log_supervise_dir_fd));
								if (!log_was_already_loaded) {
									make_supervise_fifos(log_supervise_dir_fd);
									load(prog, socket_fd, log_name, log_supervise_dir_fd, log_service_dir_fd);
									make_pipe_connectable(prog, socket_fd, log_supervise_dir_fd);
								}
								plumb(prog, socket_fd, supervise_dir_fd, log_supervise_dir_fd);
								if (!log_was_already_loaded) {
									if (input_activation) 
										make_input_activated(prog, socket_fd, log_supervise_dir_fd);
									else {
										if (is_initially_up(log_service_dir_fd)) {
											if (!wait_ok(log_supervise_dir_fd, 5000))
												std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, entry->d_name, "log/supervise/ok", "Unable to load service bundle.");
											else
												start(log_supervise_dir_fd);
										} else
											std::fprintf(stderr, "%s: INFO: %s/%s: %s\n", prog, entry->d_name, "log", "Service is initially down.");
									}
								}
								close(log_supervise_dir_fd);
							} else
								std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, entry->d_name, "log/supervise", std::strerror(errno));
							close(log_service_dir_fd);
						} else
							std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, entry->d_name, "log/service", std::strerror(errno));
						close(log_bundle_dir_fd);
					} else
						std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, entry->d_name, "log", std::strerror(errno));
					if (!was_already_loaded) {
						if (is_initially_up(service_dir_fd)) {
							if (!wait_ok(supervise_dir_fd, 5000))
								std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, entry->d_name, "supervise/ok", "Unable to load service bundle.");
							else
								start(supervise_dir_fd);
						} else
							std::fprintf(stderr, "%s: INFO: %s: %s\n", prog, entry->d_name, "Service is initially down.");
					}
					close(supervise_dir_fd);
				} else
					std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, entry->d_name, "supervise", std::strerror(errno));
				close(service_dir_fd);
			} else
				std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, entry->d_name, "service", std::strerror(errno));
			close(bundle_dir_fd);
		} else
			std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, entry->d_name, std::strerror(errno));
	}
}

/* Main function ************************************************************
// **************************************************************************
*/

void
service_dt_scanner [[gnu::noreturn]] (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));

	const bool is_system(true);
	bool input_activation(false);
	try {
		popt::bool_definition input_activation_option('\0', "input-activation", "Use input activation for log services.", input_activation);
		popt::definition * top_table[] = {
			&input_activation_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{directory}");

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

	if (1 != args.size()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "One directory name is required.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * const scan_directory(args[0]);

	const int queue(kqueue());
	if (0 > queue) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kqueue", std::strerror(error));
		throw EXIT_FAILURE;
	}

	const FileDescriptorOwner scan_dir_fd(open_dir_at(AT_FDCWD, scan_directory));
	if (0 > scan_dir_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, scan_directory, std::strerror(error));
		throw EXIT_FAILURE;
	}

	{
		struct kevent e[1];
		EV_SET(&e[0], scan_dir_fd.get(), EVFILT_VNODE, EV_ADD|EV_CLEAR, NOTE_WRITE|NOTE_EXTEND, 0, 0);
		if (0 > kevent(queue, e, sizeof e/sizeof *e, 0, 0, 0)) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kevent", std::strerror(error));
			throw EXIT_FAILURE;
		}
	}

	umask(0022);

	const int socket_fd(connect_service_manager_socket(is_system, prog));
	if (0 > socket_fd) throw EXIT_FAILURE;
	rescan(prog, scan_directory, socket_fd, scan_dir_fd.get(), input_activation);

	for (;;) {
		try {
			struct kevent p[2];
			const int rc(kevent(queue, 0, 0, p, sizeof p/sizeof *p, 0));
			if (0 > rc) {
				const int error(errno);
				if (EINTR == error) continue;
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kevent", std::strerror(error));
				throw EXIT_FAILURE;
			}
			for (size_t i(0); i < static_cast<std::size_t>(rc); ++i) {
				const struct kevent & e(p[i]);
				switch (e.filter) {
					case EVFILT_VNODE:
						if (e.ident == static_cast<uintptr_t>(scan_dir_fd.get()))
							rescan(prog, scan_directory, socket_fd, scan_dir_fd.get(), input_activation);
						else
							std::fprintf(stderr, "%s: DEBUG: vnode event ident %lu fflags %x\n", prog, e.ident, e.fflags);
						break;
					default:
						std::fprintf(stderr, "%s: DEBUG: event filter %hd ident %lu fflags %x\n", prog, e.filter, e.ident, e.fflags);
						break;
				}
			}
		} catch (const std::exception & e) {
			std::fprintf(stderr, "%s: ERROR: exception: %s\n", prog, e.what());
		}
	}
}
