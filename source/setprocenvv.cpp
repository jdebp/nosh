/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include "utils.h"
#if defined(__OpenBSD__)
#include <unistd.h>
#include <sys/exec.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <uvm/uvmexp.h>
#elif defined(__LINUX__) || defined(__linux__)
#include <sys/prctl.h>
#endif

extern
void
setprocenvv (
	std::size_t envc,
	const char * const envv[]
) {
#if defined(__FreeBSD__) || defined(__DragonFly__)
	static_cast<void>(envc);	// Silence a compiler warning.
	static_cast<void>(envv);	// Silence a compiler warning.
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
	static std::vector<const char *> envs;
	envs.clear();
	for (std::size_t c(0); c < envc; ++c)
		envs.push_back(envv[c]);
	envs.push_back(0);
	ps->ps_nenvstr = 0;	// Prevent a window where the memory area is invalidly defined.
	ps->ps_envstr = const_cast<char **>(envs.data());
	ps->ps_nenvstr = envs.size();
#elif defined(__LINUX__) || defined(__linux__)
	std::string e;
	for (size_t c(0); c < envc; ++c) {
		if (!envv[c]) break;
		e += envv[c];
		e += '\0';
	}
#	if defined(PR_SET_MM) && defined (PR_SET_MM_ENV_START) && defined (PR_SET_MM_ENV_END)
		static std::string env_holder;
		// First make the existing environ memory area no longer live.
		// Yes, we may have to do this nuttiness twice.
		// We cannot reset the pointers to 0, and there's a start<=end check that returns EINVAL.
		// On the first pass, at least one of the pair will get set; and the other on the second pass.
		for (unsigned i(0); i < 2U; ++i) {
			bool success(true);
			success &= (0 <= prctl(PR_SET_MM, PR_SET_MM_ENV_START, env_holder.data(), 0, 0));
			success &= (0 <= prctl(PR_SET_MM, PR_SET_MM_ENV_END, env_holder.data(), 0, 0));
			if (success) break;
		}
		// Now set the data.
		// We must not update the holder before this point because its prior contents were live until now.
		// Again, we may have to do it twice.
		// Note that shortcut && or || will defeat the point of doing it twice.
		env_holder = e;
		for (unsigned i(0); i < 2U; ++i) {
			bool success(true);
			success &= (0 <= prctl(PR_SET_MM, PR_SET_MM_ENV_START, env_holder.data(), 0, 0));
			success &= (0 <= prctl(PR_SET_MM, PR_SET_MM_ENV_END, env_holder.data() + env_holder.size(), 0, 0));
			if (success) break;
		}
#	else
#		pragma warning("Missing prctl() in Linux")
#	endif
#else
#error "Don't know how to overwrite the process envv on your platform."
#endif
}
