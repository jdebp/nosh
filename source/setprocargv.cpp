/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include "utils.h"
#if defined(__FreeBSD__) || defined(__DragonFly__)
#include <unistd.h>
#include <sys/exec.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#elif defined(__OpenBSD__)
#include <unistd.h>
#include <sys/exec.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <uvm/uvmexp.h>
#elif defined(__LINUX__) || defined(__linux__)
#include <sys/prctl.h>
#endif

// OpenBSD requires const incorrectness bodges.
#if defined(__OpenBSD__)
namespace {

inline
int
sysctl(const int * name, size_t namelen, void * oldp, size_t * oldlenp, const void * newp, size_t newlen) {
	return ::sysctl(name, namelen, oldp, oldlenp, const_cast<void *>(newp), newlen);
}

}
#endif

extern
void
setprocargv (
	std::size_t argc,
	const char * const argv[]
) {
#if defined(__FreeBSD__) || defined(__DragonFly__)
	std::string s;
	for (std::size_t c(0); c < argc; ++c) {
		if (!argv[c]) break;
		s += argv[c];
		s += '\0';
	}
	const int oid[4] = { CTL_KERN, KERN_PROC, KERN_PROC_ARGS, getpid() };
	sysctl(oid, sizeof oid/sizeof *oid, 0, 0, s.data(), s.length());
#elif defined(__OpenBSD__)
	static volatile ps_strings *ps(0);
	if (!ps) {
		_ps_strings kernel_ps_strings;
		size_t len(sizeof kernel_ps_strings);
		const int oid[2] = { CTL_VM, VM_PSSTRINGS };
		if (0 > sysctl(oid, sizeof oid/sizeof *oid, &kernel_ps_strings, &len, 0, 0))
			return;
		ps = static_cast<ps_strings *>(kernel_ps_strings.val);
	}
	static std::vector<const char *> args;
	args.clear();
	for (std::size_t c(0); c < argc; ++c)
		args.push_back(argv[c]);
	args.push_back(0);
	ps->ps_nargvstr = 0;	// Prevent a window where the memory area is invalidly defined.
	ps->ps_argvstr = const_cast<char **>(args.data());
	ps->ps_nargvstr = args.size();
#elif defined(__LINUX__) || defined(__linux__)
	std::string a;
	for (std::size_t c(0); c < argc; ++c) {
		if (!argv[c]) break;
		a += argv[c];
		a += '\0';
	}
#	if defined(PR_SET_MM) && defined(PR_SET_MM_ARG_START) && defined (PR_SET_MM_ARG_END)
		static std::string arg_holder;
		// First make the existing cmdline memory area no longer live.
		// Yes, we may have to do this nuttiness twice.
		// We cannot reset the pointers to 0, and there's a start<=end check that returns EINVAL.
		// On the first pass, at least one of the pair will get set; and the other on the second pass.
		for (unsigned i(0); i < 2U; ++i) {
			bool success(true);
			success &= (0 <= prctl(PR_SET_MM, PR_SET_MM_ARG_START, arg_holder.data(), 0, 0));
			success &= (0 <= prctl(PR_SET_MM, PR_SET_MM_ARG_END, arg_holder.data(), 0, 0));
			if (success) break;
		}
		// Now set the data.
		// We must not update the holder before this point because its prior contents were live until now.
		// Again, we may have to do it twice.
		// Note that shortcut && or || will defeat the point of doing it twice.
		arg_holder = a;
		for (unsigned i(0); i < 2U; ++i) {
			bool success(true);
			success &= (0 <= prctl(PR_SET_MM, PR_SET_MM_ARG_START, arg_holder.data(), 0, 0));
			success &= (0 <= prctl(PR_SET_MM, PR_SET_MM_ARG_END, arg_holder.data() + arg_holder.size(), 0, 0));
			if (success) break;
		}
#	else
#		pragma warning("Missing prctl() in Linux")
#	endif
#else
#error "Don't know how to overwrite the process argv on your platform."
#endif
}
