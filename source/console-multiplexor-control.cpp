/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define _BSD_SOURCE 1
#include <vector>
#include <memory>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cctype>
#include <stdint.h>
#include <sys/ioctl.h>
#include "config/haswscons.h"
#if defined(HAS_WSCONS)
#	include <dev/wscons/wsdisplay_usl_io.h>	// VT/CONSIO ioctls
#elif defined(__LINUX__) || defined(__linux__)
#	include <linux/vt.h>
#else
#	include <sys/consio.h>
#endif
#include <unistd.h>
#include "utils.h"
#include "fdutils.h"
#include "popt.h"
#include "InputMessage.h"
#include "FileDescriptorOwner.h"

namespace {

inline
void
WriteInputMessage(
	const FileDescriptorOwner & input_fd,
	uint32_t m
) {
	write(input_fd.get(), &m, sizeof m);
}

inline
bool
parse_number(
	const std::string & s,
	uint32_t & n
) {
	n = 0;
	for (std::string::const_iterator p(s.begin()); s.end() != p; ++p) {
		const char c(*p);
		if (!std::isdigit(c)) return false;
		n = n * 10U + static_cast<unsigned char>(c - '0');
	}
	return true;
}

}

/* Main function ************************************************************
// **************************************************************************
*/

void
console_multiplexor_control [[gnu::noreturn]] (
	const char * & /*next_prog*/,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));

	try {
		popt::definition * top_table[] = {
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{S|N|P|number...}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}

	if (args.empty()) {
		die_missing_argument(prog, envs, "command");
	}

	FileDescriptorOwner run_dev_fd(open_dir_at(AT_FDCWD, "/run/dev"));
	if (0 > run_dev_fd.get()) {
		die_errno(prog, envs, "/run/dev");
	}

	FileDescriptorOwner dev_fd(open_dir_at(AT_FDCWD, "/dev"));
	if (0 > dev_fd.get()) {
		die_errno(prog, envs, "/dev");
	}

	for (;;) {
		if (args.empty()) throw EXIT_SUCCESS;
		const char * dirname(args.front());
		args.erase(args.begin());

		std::string vcname, command;
		if (const char * at = std::strchr(dirname, '@')) {
			vcname = at + 1;
			command = tolower(std::string(dirname, at));
		} else {
			vcname = "head0";
			command = tolower(std::string(dirname));
		}

		FileDescriptorOwner vt_fd(open_dir_at(run_dev_fd.get(), vcname.c_str()));
		uint32_t s;
		if (0 <= vt_fd.get()) {
			const FileDescriptorOwner input_fd(open_writeexisting_at(vt_fd.get(), "input"));
			if (0 > input_fd.get()) {
				die_errno(prog, envs, vcname.c_str(), "input");
			}
			if ("n" == command || "next" == command)
				WriteInputMessage(input_fd, MessageForConsumerKey(CONSUMER_KEY_NEXT_TASK, 0));
			else
			if ("p" == command || "prev" == command)
				WriteInputMessage(input_fd, MessageForConsumerKey(CONSUMER_KEY_PREVIOUS_TASK, 0));
			else
			if ("s" == command || "sel" == command)
				WriteInputMessage(input_fd, MessageForConsumerKey(CONSUMER_KEY_SELECT_TASK, 0));
			else
			if (parse_number(command, s))
				WriteInputMessage(input_fd, MessageForSession(s, 0));
			else
				die_invalid(prog, envs, command.c_str(), "Unrecognized command");
		} else {
			if (ENOTDIR == errno) {
				vt_fd.reset(open_readwriteexisting_at(run_dev_fd.get(), vcname.c_str()));
			} else
			if (ENOENT == errno) {
				vt_fd.reset(open_readwriteexisting_at(dev_fd.get(), vcname.c_str()));
			} else
			{
				die_errno(prog, envs, vcname.c_str());
			}
			if (0 > vt_fd.get()) {
				die_errno(prog, envs, vcname.c_str());
			}
			if ("n" == command || "next" == command) {
#if defined(VT_ACTIVATE) && defined(VT_GETSTATE)
				struct vt_stat s;
				if (0 <= ioctl(vt_fd.get(), VT_GETSTATE, &s))
					ioctl(vt_fd.get(), VT_ACTIVATE, s.v_active + 1);
#elif defined(VT_ACTIVATE) && defined(VT_GETACTIVE)
				int index;
				if (0 <= ioctl(vt_fd.get(), VT_GETACTIVE, &index))
					ioctl(vt_fd.get(), VT_ACTIVATE, index + 1);
#else
				;	/// \bug FIXME: Implement next action
#endif
			} else
			if ("p" == command || "prev" == command) {
#if defined(VT_ACTIVATE) && defined(VT_GETSTATE)
				struct vt_stat s;
				if (0 <= ioctl(vt_fd.get(), VT_GETSTATE, &s))
					ioctl(vt_fd.get(), VT_ACTIVATE, s.v_active - 1);
#elif defined(VT_ACTIVATE) && defined(VT_GETACTIVE)
				int index;
				if (0 <= ioctl(vt_fd.get(), VT_GETACTIVE, &index))
					ioctl(vt_fd.get(), VT_ACTIVATE, index - 1);
#else
				;	/// \bug FIXME: Implement previous action
#endif
			} else
			if ("s" == command || "sel" == command)
#if defined(VT_ACTIVATE)
				ioctl(vt_fd.get(), VT_ACTIVATE, 1);
#else
				;	/// \bug FIXME: Implement sel action
#endif
			else
			if (parse_number(command, s))
#if defined(VT_ACTIVATE)
				ioctl(vt_fd.get(), VT_ACTIVATE, s + 1);
#else
				;	/// \bug FIXME: Implement number action
#endif
			else
				die_invalid(prog, envs, command.c_str(), "Unrecognized command");
		}
	}
}
