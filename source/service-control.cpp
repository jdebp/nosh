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
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include "utils.h"
#include "fdutils.h"
#include "FileDescriptorOwner.h"
#include "popt.h"

/* Control character options ************************************************
// **************************************************************************
*/

struct control_character_definition : public popt::simple_named_definition {
public:
	control_character_definition(char s, const char * l, const char * d, char p_c, std::string & p_v) : simple_named_definition(s, l, d), c(p_c), v(p_v) {}
	virtual void action(popt::processor &);
	virtual ~control_character_definition();
protected:
	char c;
	std::string & v;
};
control_character_definition::~control_character_definition() {}
void control_character_definition::action(popt::processor &)
{
	v += c;
}

/* Main function ************************************************************
// **************************************************************************
*/

void
service_control [[gnu::noreturn]] (
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));

	std::string controls;
	try {
		control_character_definition up_option('u', "up", "Bring the service up, if it is not up already.", 'u', controls);
		control_character_definition down_option('d', "down", "Bring the service down, if it is not down already, unpausing it if necessary.", 'd', controls);
		control_character_definition once_option('o', "once", "Run the service once, bringing it up if it is down.", 'o', controls);
		control_character_definition at_most_once_option('O', "at-most-once", "Run the service at most once, leaving it down if it is down.", 'O', controls);
		control_character_definition unload_option('x', "exit", "Unload the service when it reaches the stopped state.", 'x', controls);
		control_character_definition pause_option('p', "pause", "Send a SIGSTOP signal to the service, pausing it.", 'p', controls);
		control_character_definition continue_option('c', "continue", "Send a SIGCONT signal to the service, continuing it.", 'c', controls);
		control_character_definition hangup_option('h', "hangup", "Send a SIGHUP signal to the service.", 'h', controls);
		control_character_definition hangup_main_option('H', "hangup-main", "Send a SIGHUP signal to only the main process of the service.", 'H', controls);
		control_character_definition alarm_option('a', "alarm", "Send a SIGALRM signal to the service.", 'a', controls);
		control_character_definition interrupt_option('i', "interrupt", "Send a SIGINT signal to the service.", 'i', controls);
		control_character_definition terminate_option('t', "terminate", "Send a SIGTERM signal to the service.", 't', controls);
		control_character_definition terminate_main_option('T', "terminate-main", "Send a SIGTERM signal to only the main process of the service.", 'T', controls);
		control_character_definition kill_option('k', "kill", "Send a SIGKILL signal to the service.", 'k', controls);
		control_character_definition kill_main_option('K', "kill-main", "Send a SIGKILL signal to only the main process of the service.", 'K', controls);
		control_character_definition quit_option('q', "quit", "Send a SIGQUIT signal to the service.", 'q', controls);
		control_character_definition usr1_option('1', "usr1", "Send a SIGUSR1 signal to the service.", '1', controls);
		control_character_definition usr2_option('2', "usr2", "Send a SIGUSR2 signal to the service.", '2', controls);
		control_character_definition window_option('w', "window", "Send a SIGWINCH signal to the service.", 'w', controls);
		control_character_definition suspend_option('z', "suspend", "Send a SIGTSTP signal to the service.", 'z', controls);
		popt::definition * top_table[] = {
			&up_option,
			&down_option,
			&once_option,
			&at_most_once_option,
			&unload_option,
			&pause_option,
			&continue_option,
			&hangup_option,
			&hangup_main_option,
			&alarm_option,
			&interrupt_option,
			&terminate_option,
			&terminate_main_option,
			&kill_option,
			&kill_main_option,
			&quit_option,
			&usr1_option,
			&usr2_option,
			&window_option,
			&suspend_option
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{file(s)...}");

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
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing directory name(s).");
		throw static_cast<int>(EXIT_USAGE);
	}

	sigset_t original_signals;
	sigprocmask(SIG_SETMASK, 0, &original_signals);
	sigset_t masked_signals(original_signals);
	sigaddset(&masked_signals, SIGPIPE);
	sigprocmask(SIG_SETMASK, &masked_signals, 0);

	for (std::vector<const char *>::const_iterator i(args.begin()); i != args.end(); ++i) {
		const char * name(*i);
		FileDescriptorOwner dir_fd(open_dir_at(AT_FDCWD, name));
		if (0 > dir_fd.get()) {
			const int error(errno);
			std::fprintf(stdout, "%s: %s\n", name, std::strerror(error));
			continue;
		}
		{
			FileDescriptorOwner ok_fd(open_writeexisting_at(dir_fd.get(), "ok"));
			if (0 > ok_fd.get()) {
				const FileDescriptorOwner old_dir_fd(dir_fd.release());
				dir_fd.reset(open_dir_at(old_dir_fd.get(), "supervise"));
				if (0 > dir_fd.get()) {
					const int error(errno);
					std::fprintf(stdout, "%s: %s: %s\n", name, "supervise", std::strerror(error));
					continue;
				}
				ok_fd.reset(open_writeexisting_at(dir_fd.get(), "ok"));
				if (0 > ok_fd.get()) {
					const int error(errno);
					if (ENXIO == error)
						std::fprintf(stdout, "%s: No supervisor is running\n", name);
					else
						std::fprintf(stdout, "%s: %s: %s\n", name, "supervise/ok", std::strerror(error));
					continue;
				}
			}
		}
		const FileDescriptorOwner control_fd(open_writeexisting_at(dir_fd.get(), "control"));
		if (0 > control_fd.get()) {
			const int error(errno);
			std::fprintf(stdout, "%s: %s: %s\n", name, "control", std::strerror(error));
			continue;
		}
		dir_fd.reset(-1);
		const char * p(controls.c_str());
		std::size_t l(controls.length());
		while (l) {
			const ssize_t n(write(control_fd.get(), p, l));
			if (0 > n) {
				const int error(errno);
				if (EINTR == error) continue;
				std::fprintf(stdout, "%s: %s: %s\n", name, "control", std::strerror(error));
				break;
			} else if (0 == n)
				break;
			p += n;
			l -= n;
		}
	}

	throw EXIT_SUCCESS;
}
