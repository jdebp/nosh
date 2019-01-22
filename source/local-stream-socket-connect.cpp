/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <cerrno>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netdb.h>
#include <unistd.h>
#include "popt.h"
#include "utils.h"
#include "fdutils.h"
#include "getaddrinfo_unix.h"
#include "ProcessEnvironment.h"
#include "FileDescriptorOwner.h"

/* Helper functions *********************************************************
// **************************************************************************
*/

static inline
const char *
q (
	const ProcessEnvironment & envs,
	const char * name
) {
	const char * value(envs.query(name));
	return value ? value : "";
}

/* Main function ************************************************************
// **************************************************************************
*/

void
local_stream_socket_connect ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	const char * localpath(0);
	bool verbose(false);
	try {
		popt::bool_definition verbose_option('v', "verbose", "Print status information.", verbose);
		popt::string_definition localpath_option('l', "local-path", "name", "Override the local path.", localpath);
		popt::definition * top_table[] = {
			&verbose_option,
			&localpath_option,
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{path} {prog}");

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

	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing connect path name.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * connectpath(args.front());
	args.erase(args.begin());
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing next program.");
		throw static_cast<int>(EXIT_USAGE);
	}
	next_prog = arg0_of(args);

	addrinfo * remote_info_list(0), remote_hints = {0,0,0,0,0,0,0,0};
	remote_hints.ai_family = AF_UNIX;
	remote_hints.ai_socktype = SOCK_STREAM;
	remote_hints.ai_protocol = 0;
	remote_hints.ai_flags = 0;
	const int rrc(getaddrinfo(connectpath, &remote_hints, &remote_info_list));
	if (0 != rrc) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, connectpath, EAI_SYSTEM == rrc ? std::strerror(error) : gai_strerror(rrc));
		throw EXIT_FAILURE;
	}

	for (addrinfo * remote_info(remote_info_list); ; remote_info = remote_info->ai_next) {
		if (!remote_info) {
			std::fprintf(stderr, "%s: FATAL: %s\n", prog, "No addresses (remaining) to connect to.");
		free_fail:
			if (remote_info_list) freeaddrinfo(remote_info_list);
			throw EXIT_FAILURE;
		}

		FileDescriptorOwner s(socket(remote_info->ai_family, remote_info->ai_socktype, remote_info->ai_protocol));
		if (0 > s.get()) {
		exit_error:
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s\n", prog, std::strerror(error));
			goto free_fail;
		}

		if (localpath) {
			addrinfo * local_info(0), local_hints = {0,0,0,0,0,0,0,0};
			local_hints.ai_family = remote_info->ai_family;
			local_hints.ai_socktype = remote_info->ai_socktype;
			local_hints.ai_protocol = remote_info->ai_protocol;
			local_hints.ai_flags = AI_NUMERICHOST|AI_NUMERICSERV|AI_PASSIVE;
			const int lrc(getaddrinfo(localpath, &local_hints, &local_info));
			if (0 != lrc) {
				const int error(errno);
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, localpath, EAI_SYSTEM == lrc ? std::strerror(error) : gai_strerror(lrc));
				goto free_fail;
			}
			if (0 > bind(s.get(), local_info->ai_addr, local_info->ai_addrlen)) goto exit_error;
			freeaddrinfo(local_info);
		}

		if (0 > socket_connect(s.get(), remote_info->ai_addr, remote_info->ai_addrlen)) continue;

		sockaddr_storage localaddr;
		socklen_t localaddrsz = sizeof localaddr;
		if (0 > getsockname(s.get(), reinterpret_cast<sockaddr *>(&localaddr), &localaddrsz)) goto exit_error;

		envs.set("PROTO", "UNIX");
		switch (localaddr.ss_family) {
			case AF_UNIX:
			{
				const struct sockaddr_un & localaddru(*reinterpret_cast<const struct sockaddr_un *>(&localaddr));
				if (localaddrsz > offsetof(sockaddr_un, sun_path) && localaddru.sun_path[0])
					envs.set("UNIXLOCALPATH", localaddru.sun_path);
				else
					envs.set("UNIXLOCALPATH", 0);
				break;
			}
			default:
				envs.set("UNIXLOCALPATH", 0);
				break;
		}
		envs.set("UNIXLOCALUID", 0);
		envs.set("UNIXLOCALGID", 0);
		envs.set("UNIXLOCALPID", 0);
		switch (remote_info->ai_family) {
			case AF_UNIX:
			{
				const struct sockaddr_un & remoteaddr(*reinterpret_cast<const struct sockaddr_un *>(remote_info->ai_addr));
				if (remote_info->ai_addrlen > offsetof(sockaddr_un, sun_path) && remoteaddr.sun_path[0])
					envs.set("UNIXREMOTEPATH", remoteaddr.sun_path);
				else
					envs.set("UNIXREMOTEPATH", 0);
#if defined(SO_PEERCRED)
#if defined(__LINUX__) || defined(__linux__)
				struct ucred u;
#elif defined(__OpenBSD__)
				struct sockpeercred u;
#else
#error "Don't know how to do SO_PEERCRED on your platform."
#endif
				socklen_t ul = sizeof u;
				if (0 > getsockopt(s.get(), SOL_SOCKET, SO_PEERCRED, &u, &ul)) goto exit_error;
				char buf[64];
				snprintf(buf, sizeof buf, "%u", u.pid);
				envs.set("UNIXREMOTEPID", buf);
				snprintf(buf, sizeof buf, "%u", u.gid);
				envs.set("UNIXREMOTEEGID", buf);
				snprintf(buf, sizeof buf, "%u", u.uid);
				envs.set("UNIXREMOTEEUID", buf);
#else
				envs.set("UNIXREMOTEPID", 0);
				envs.set("UNIXREMOTEEGID", 0);
				envs.set("UNIXREMOTEEUID", 0);
#endif
				break;
			}
			default:
				envs.set("UNIXREMOTEPATH", 0);
				envs.set("UNIXREMOTEPID", 0);
				envs.set("UNIXREMOTEEGID", 0);
				envs.set("UNIXREMOTEEUID", 0);
				break;
		}

		if (0 > dup2(s.get(), 6)) goto exit_error;
		if (0 > dup2(s.get(), 7)) goto exit_error;
		if (6 == s.get() || 7 == s.get())
			s.release();

		if (verbose)
			std::fprintf(stderr, "%s: %u %s %s %s %s %s\n", prog, getpid(), q(envs, "UNIXLOCALPATH"), q(envs, "UNIXREMOTEPATH"), q(envs, "UNIXREMOTEPID"), q(envs, "UNIXREMOTEEUID"), q(envs, "UNIXREMOTEEGID"));

		return;
	}
}
