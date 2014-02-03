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
#include <sys/event.h>
#include <dirent.h>
#include <unistd.h>
#include "utils.h"
#include "fdutils.h"
#include "service-manager.h"
#include "service-manager-client.h"
#include "common-manager.h"

/* Scanning *****************************************************************
// **************************************************************************
*/

static
void
make_supervise (
	int supervise_dir_fd
) {
	mkfifoat(supervise_dir_fd, "ok", 0666);
	mkfifoat(supervise_dir_fd, "control", 0600);
}

static 
void
rescan (
	const char * prog,
	const char * name,
	int socket_fd,
	int retained_scan_dir_fd
) {
	const int scan_dir_fd(dup(retained_scan_dir_fd));
	if (0 > scan_dir_fd) {
exit_scan:
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, name, std::strerror(error));
	}
	DIR * scan_dir(fdopendir(scan_dir_fd));
	if (!scan_dir) goto exit_scan;
	rewinddir(scan_dir);	// because the last pass left it at EOF.
	for (;;) {
		errno = 0;
		const dirent * entry(readdir(scan_dir));
		if (!entry) {
			if (errno) goto exit_scan;
			break;
		}
#if defined(_DIRENT_HAVE_D_TYPE)
		if (DT_DIR != entry->d_type && DT_LNK != entry->d_type) continue;
#endif
#if defined(_DIRENT_HAVE_D_NAMLEN)
		if (1 > entry->d_namlen) continue;
		if (sizeof(service_manager_rpc_message::name) > entry->d_namlen) continue;
#endif
		if ('.' == entry->d_name[0]) continue;

		const int bundle_dir_fd(open_dir_at(scan_dir_fd, entry->d_name));
		if (0 <= bundle_dir_fd) {
			int service_dir_fd(open_dir_at(bundle_dir_fd, "service/"));
			if (0 > service_dir_fd) service_dir_fd = dup(bundle_dir_fd);
			mkdirat(bundle_dir_fd, "supervise", 0700);
			const int supervise_dir_fd(open_dir_at(bundle_dir_fd, "supervise/"));
			if (0 <= supervise_dir_fd) {
				const bool was_already_loaded(is_ok(supervise_dir_fd));
				if (!was_already_loaded) {
					make_supervise(supervise_dir_fd);
					load(prog, socket_fd, entry->d_name, supervise_dir_fd, service_dir_fd, false, false);
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
					int log_service_dir_fd(open_dir_at(log_bundle_dir_fd, "service/"));
					if (0 > log_service_dir_fd) log_service_dir_fd = dup(log_bundle_dir_fd);
					mkdirat(log_bundle_dir_fd, "supervise", 0700);
					const int log_supervise_dir_fd(open_dir_at(log_bundle_dir_fd, "supervise/"));
					if (0 <= log_supervise_dir_fd) {
						const bool log_was_already_loaded(is_ok(log_supervise_dir_fd));
						if (!log_was_already_loaded) {
							make_supervise(log_supervise_dir_fd);
							load(prog, socket_fd, log_name, log_supervise_dir_fd, log_service_dir_fd, true, false);
						}
						plumb(prog, socket_fd, supervise_dir_fd, log_supervise_dir_fd);
						if (!log_was_already_loaded)
							make_input_activated(prog, socket_fd, log_supervise_dir_fd);
						close(log_supervise_dir_fd);
					} else
						std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, entry->d_name, "log/supervise", std::strerror(errno));
					close(log_service_dir_fd);
					close(log_bundle_dir_fd);
				} else
					std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, entry->d_name, "log", std::strerror(errno));
				if (!was_already_loaded) {
					struct stat down_file_s;
					if (0 > fstatat(service_dir_fd, "down", &down_file_s, 0) && ENOENT == errno)
						start(prog, supervise_dir_fd);
				}
				close(supervise_dir_fd);
			} else
				std::fprintf(stderr, "%s: ERROR: %s/%s: %s\n", prog, entry->d_name, "supervise", std::strerror(errno));
			close(service_dir_fd);
			close(bundle_dir_fd);
		} else
			std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, entry->d_name, std::strerror(errno));
	}
	closedir(scan_dir);
}

/* Main function ************************************************************
// **************************************************************************
*/

int
main (
	int argc, 
	const char * argv[] 
) {
	const bool is_system(true);
	if (argc < 1) return EXIT_FAILURE;
	const char * prog(basename_of(argv[0]));
	if (argc != 2) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Incorrect number of arguments.");
		return EXIT_FAILURE;
	}

	const int queue(kqueue());
	if (0 > queue) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kqueue", std::strerror(error));
		return EXIT_FAILURE;
	}

	const int scan_dir_fd(open_dir_at(AT_FDCWD, argv[1]));
	if (0 > scan_dir_fd) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, argv[1], std::strerror(error));
		return EXIT_FAILURE;
	}

	{
		struct kevent e[1];
		EV_SET(&e[0], scan_dir_fd, EVFILT_VNODE, EV_ADD|EV_CLEAR, NOTE_WRITE|NOTE_EXTEND, 0, 0);
		if (0 > kevent(queue, e, sizeof e/sizeof *e, 0, 0, 0)) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kevent", std::strerror(error));
			return EXIT_FAILURE;
		}
	}

	umask(0);

	const int socket_fd(connect_service_manager_socket(is_system, prog));
	if (0 > socket_fd) return EXIT_FAILURE;
	rescan(prog, argv[1], socket_fd, scan_dir_fd);

	for (;;) {
		try {
			struct kevent e;
			if (0 > kevent(queue, 0, 0, &e, 1, 0)) {
				const int error(errno);
				if (EINTR == error) continue;
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kevent", std::strerror(error));
				return EXIT_FAILURE;
			}
			switch (e.filter) {
				case EVFILT_VNODE:
					if (e.ident == static_cast<uintptr_t>(scan_dir_fd))
						rescan(prog, argv[1], socket_fd, scan_dir_fd);
					else
						std::fprintf(stderr, "%s: DEBUG: vnode event ident %lu fflags %x\n", prog, e.ident, e.fflags);
					break;
				default:
					std::fprintf(stderr, "%s: DEBUG: event filter %hd ident %lu fflags %x\n", prog, e.filter, e.ident, e.fflags);
					break;
			}
		} catch (const std::exception & e) {
			std::fprintf(stderr, "%s: ERROR: exception: %s\n", prog, e.what());
		}
	}
}
