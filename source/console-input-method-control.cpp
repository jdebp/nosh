/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define _XOPEN_SOURCE_EXTENDED
#include <vector>
#include <memory>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cctype>
#include <stdint.h>
#include <unistd.h>
#include "utils.h"
#include "fdutils.h"
#include "popt.h"
#include "InputMessage.h"
#include "FileDescriptorOwner.h"

static inline
void 
WriteInputMessage(
	const FileDescriptorOwner & input_fd,
	uint32_t m
) {
	write(input_fd.get(), &m, sizeof m);
}

/* Main function ************************************************************
// **************************************************************************
*/

void
console_input_method_control [[gnu::noreturn]] ( 
	const char * & /*next_prog*/,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));

	try {
		popt::definition * top_table[] = {
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{.|K|L|R|G|Y|Z|N|C...}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw static_cast<int>(EXIT_USAGE);
	}

	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing command.");
		throw static_cast<int>(EXIT_USAGE);
	}

	FileDescriptorOwner run_dev_fd(open_dir_at(AT_FDCWD, "/run/dev"));
	if (0 > run_dev_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "/run/dev", std::strerror(error));
		throw EXIT_FAILURE;
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
		if (0 > vt_fd.get()) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, vcname.c_str(), std::strerror(error));
			throw EXIT_FAILURE;
		}
		const FileDescriptorOwner input_fd(open_writeexisting_at(vt_fd.get(), "input"));
		if (0 > input_fd.get()) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, vcname.c_str(), "input", std::strerror(error));
			throw EXIT_FAILURE;
		}
		if ("." == command)
			WriteInputMessage(input_fd, MessageForExtendedKey(EXTENDED_KEY_IM_TOGGLE, 0));
		else
		if ("k" == command || "katakana" == command)
			WriteInputMessage(input_fd, MessageForExtendedKey(EXTENDED_KEY_KATAKANA, 0));
		else
		if ("l" == command || "hiragana" == command)
			WriteInputMessage(input_fd, MessageForExtendedKey(EXTENDED_KEY_HIRAGANA, 0));
		else
		if ("r" == command || "romaji" == command)
			WriteInputMessage(input_fd, MessageForExtendedKey(EXTENDED_KEY_ROMAJI, 0));
		else
		if ("g" == command || "hangeul" == command)
			WriteInputMessage(input_fd, MessageForExtendedKey(EXTENDED_KEY_HANGEUL, 0));
		else
		if ("y" == command || "hanyeong" == command || "yeong" == command)
			WriteInputMessage(input_fd, MessageForExtendedKey(EXTENDED_KEY_HAN_YEONG, 0));
		else
		if ("z" == command || "hanja" == command || "kanji" == command || "hanzi" == command)
			WriteInputMessage(input_fd, MessageForExtendedKey(EXTENDED_KEY_HANJA, 0));
		else
		if ("n" == command || "muhenkan" == command)
			WriteInputMessage(input_fd, MessageForExtendedKey(EXTENDED_KEY_MUHENKAN, 0));
		else
		if ("c" == command || "henkan" == command)
			WriteInputMessage(input_fd, MessageForExtendedKey(EXTENDED_KEY_HENKAN, 0));
		else {
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, command.c_str(), "Unrecognized command");
			throw EXIT_FAILURE;
		}
	}
}
