/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <string>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#if defined(__FreeBSD__) || defined(__DragonFly__)
#include <dev/usb/usb_ioctl.h>
#endif
#include <unistd.h>
#include <fcntl.h>
#include "utils.h"
#include "fdutils.h"
#include "popt.h"
#include "FileDescriptorOwner.h"

#if defined(__FreeBSD__) || defined(__DragonFly__)
bool 
is_kernel_driver_attached(
	const FileDescriptorOwner & fd,
	int index
) {
	return 0 <= ioctl(fd.get(), USB_IFACE_DRIVER_ACTIVE, &index);
}

bool 
detach_kernel_driver(
	const FileDescriptorOwner & fd,
	int index
) {
	return 0 <= ioctl(fd.get(), USB_IFACE_DRIVER_DETACH, &index);
}
#else
bool 
is_kernel_driver_attached(
	const FileDescriptorOwner &,
	int 
) {
	return false;
}

bool 
detach_kernel_driver(
	const FileDescriptorOwner &,
	int 
) {
	return true;
}
#endif

/* Main function ************************************************************
// **************************************************************************
*/

void
detach_kernel_usb_driver [[gnu::noreturn]] (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::definition * top_table[] = {};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{device(s)...}");

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

	while (!args.empty()) {
		const char * file(args.front());
		args.erase(args.begin());
		next_prog = arg0_of(args);

		FileDescriptorOwner fd(open_read_at(AT_FDCWD, file));
		if (0 > fd.get()) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, file, std::strerror(error));
			throw EXIT_FAILURE;
		}
		if (is_kernel_driver_attached(fd, 0)) {
			if (!detach_kernel_driver(fd, 0)) {
				const int error(errno);
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, file, std::strerror(error));
				throw EXIT_FAILURE;
			}
		}
	}

	throw EXIT_SUCCESS;
}
