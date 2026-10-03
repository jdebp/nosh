/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstring>
#include <iostream>
#include <sstream>
#include <unistd.h>
#include "utils.h"
#include "CharacterCell.h"
#include "ECMA48Output.h"
#include "TerminalCapabilities.h"
#include "ProcessEnvironment.h"
#include "popt.h"

/* Colour utilities *********************************************************
// **************************************************************************
*/

namespace {

inline
bool
use_colours (
	int fd
) {
	return isatty(fd);
}

const CharacterCell::colour_type light_red	(ALPHA_FOR_TRUE_COLOURED,0xFF,0x19,0x00);
const CharacterCell::colour_type light_orange	(ALPHA_FOR_TRUE_COLOURED,0xFF,0xAF,0x00);
const CharacterCell::colour_type dark_orange	(ALPHA_FOR_TRUE_COLOURED,0x87,0x5F,0x00);
const CharacterCell::colour_type dark_orange3	(ALPHA_FOR_TRUE_COLOURED,0xAF,0x5F,0x00);
const CharacterCell::colour_type dark_white	(ALPHA_FOR_TRUE_COLOURED,0x7F,0x7F,0x7F);
const CharacterCell::colour_type dark_violet	(ALPHA_FOR_TRUE_COLOURED,0x87,0x00,0xD7);
const CharacterCell::colour_type light_blue	(ALPHA_FOR_TRUE_COLOURED,0x00,0x5F,0xFF);
const CharacterCell::colour_type light_cyan	(ALPHA_FOR_TRUE_COLOURED,0x00,0xFF,0xD7);

inline
void
print (
	const char * prog,
	const ProcessEnvironment & envs,
	const CharacterCell::colour_type & colour,
	const char * level,
	const char * how
) {
	TerminalCapabilities caps(envs);
	ECMA48Output o(caps, std::clog, true /* C1 is 7-bit aliased */, false /* C1 is not raw 8-bit */);
	const bool colours(use_colours(STDERR_FILENO));
	if (colours) o.set_boldface(true);
	std::clog << prog;
	if (colours) o.set_boldface(false);
	std::clog << ": ";
	if (colours) o.SGRColour(true /* foreground */, colour);
	std::clog << level;
	if (colours) o.SGRColour(true /* foreground */);
	std::clog << ": ";
	if (colours) o.set_italics(true);
	std::clog << how;
	if (colours) o.set_italics(false);
	std::clog.put('\n');
}

inline
void
print (
	const char * prog,
	const ProcessEnvironment & envs,
	const CharacterCell::colour_type & colour0,
	const CharacterCell::colour_type & colour1,
	const char * level,
	const char * what,
	const char * how
) {
	TerminalCapabilities caps(envs);
	ECMA48Output o(caps, std::clog, true /* C1 is 7-bit aliased */, false /* C1 is not raw 8-bit */);
	const bool colours(use_colours(STDERR_FILENO));
	if (colours) o.set_boldface(true);
	std::clog << prog;
	if (colours) o.set_boldface(false);
	std::clog << ": ";
	if (colours) o.SGRColour(true /* foreground */, colour0);
	std::clog << level;
	if (colours) o.SGRColour(true /* foreground */);
	std::clog << ": ";
	if (colours) o.SGRColour(true /* foreground */, colour1);
	std::clog << what;
	if (colours) o.SGRColour(true /* foreground */);
	std::clog << ": ";
	if (colours) o.set_italics(true);
	std::clog << how;
	if (colours) o.set_italics(false);
	std::clog.put('\n');
}

inline
void
print (
	const char * prog,
	const ProcessEnvironment & envs,
	const CharacterCell::colour_type & colour0,
	const CharacterCell::colour_type & colour1,
	const CharacterCell::colour_type & colour2,
	const char * level,
	const char * what0,
	const char * what1,
	const char * how
) {
	TerminalCapabilities caps(envs);
	ECMA48Output o(caps, std::clog, true /* C1 is 7-bit aliased */, false /* C1 is not raw 8-bit */);
	const bool colours(use_colours(STDERR_FILENO));
	if (colours) o.set_boldface(true);
	std::clog << prog;
	if (colours) o.set_boldface(false);
	std::clog << ": ";
	if (colours) o.SGRColour(true /* foreground */, colour0);
	std::clog << level;
	if (colours) o.SGRColour(true /* foreground */);
	std::clog << ": ";
	if (colours) o.SGRColour(true /* foreground */, colour1);
	std::clog << what0;
	if (colours) o.SGRColour(true /* foreground */);
	std::clog << ": ";
	if (colours) o.SGRColour(true /* foreground */, colour2);
	std::clog << what1;
	if (colours) o.SGRColour(true /* foreground */);
	std::clog << ": ";
	if (colours) o.set_italics(true);
	std::clog << how;
	if (colours) o.set_italics(false);
	std::clog.put('\n');
}

inline
void
print (
	const char * prog,
	const ProcessEnvironment & envs,
	const CharacterCell::colour_type & colour0,
	const CharacterCell::colour_type & colour1,
	const CharacterCell::colour_type & colour2,
	const char * level,
	const char * file,
	unsigned long line,
	const char * what,
	const char * how
) {
	std::ostringstream s;
	s << file;
	s.put('(') << line;
	s.put(')');
	print(prog, envs, colour0, colour1, colour2, level, s.str().c_str(), what, how);
}

void
print (
	const char * prog,
	const ProcessEnvironment & envs,
	const CharacterCell::colour_type & colour0,
	const CharacterCell::colour_type & colour1,
	const CharacterCell::colour_type & colour2,
	const CharacterCell::colour_type & colour3,
	const char * level,
	const char * what0,
	const char * what1,
	const char * what2,
	const char * how
) {
	TerminalCapabilities caps(envs);
	ECMA48Output o(caps, std::clog, true /* C1 is 7-bit aliased */, false /* C1 is not raw 8-bit */);
	const bool colours(use_colours(STDERR_FILENO));
	if (colours) o.set_boldface(true);
	std::clog << prog;
	if (colours) o.set_boldface(false);
	std::clog << ": ";
	if (colours) o.SGRColour(true /* foreground */, colour0);
	std::clog << level;
	if (colours) o.SGRColour(true /* foreground */);
	std::clog << ": ";
	if (colours) o.SGRColour(true /* foreground */, colour1);
	std::clog << what0;
	if (colours) o.SGRColour(true /* foreground */);
	std::clog << ": ";
	if (colours) o.SGRColour(true /* foreground */, colour2);
	std::clog << what1;
	if (colours) o.SGRColour(true /* foreground */);
	std::clog << ": ";
	if (colours) o.SGRColour(true /* foreground */, colour3);
	std::clog << what2;
	if (colours) o.SGRColour(true /* foreground */);
	std::clog << ": ";
	if (colours) o.set_italics(true);
	std::clog << how;
	if (colours) o.set_italics(false);
	std::clog.put('\n');
}

void
print (
	const char * prog,
	const ProcessEnvironment & envs,
	const CharacterCell::colour_type & colour0,
	const CharacterCell::colour_type & colour1,
	const CharacterCell::colour_type & colour2,
	const CharacterCell::colour_type & colour3,
	const CharacterCell::colour_type & colour4,
	const char * level,
	const char * what0,
	const char * what1,
	const char * what2,
	const char * what3,
	const char * how
) {
	TerminalCapabilities caps(envs);
	ECMA48Output o(caps, std::clog, true /* C1 is 7-bit aliased */, false /* C1 is not raw 8-bit */);
	const bool colours(use_colours(STDERR_FILENO));
	if (colours) o.set_boldface(true);
	std::clog << prog;
	if (colours) o.set_boldface(false);
	std::clog << ": ";
	if (colours) o.SGRColour(true /* foreground */, colour0);
	std::clog << level;
	if (colours) o.SGRColour(true /* foreground */);
	std::clog << ": ";
	if (colours) o.SGRColour(true /* foreground */, colour1);
	std::clog << what0;
	if (colours) o.SGRColour(true /* foreground */);
	std::clog << ": ";
	if (colours) o.SGRColour(true /* foreground */, colour2);
	std::clog << what1;
	if (colours) o.SGRColour(true /* foreground */);
	std::clog << ": ";
	if (colours) o.SGRColour(true /* foreground */, colour3);
	std::clog << what2;
	if (colours) o.SGRColour(true /* foreground */);
	std::clog << ": ";
	if (colours) o.SGRColour(true /* foreground */, colour4);
	std::clog << what3;
	if (colours) o.SGRColour(true /* foreground */);
	std::clog << ": ";
	if (colours) o.set_italics(true);
	std::clog << how;
	if (colours) o.set_italics(false);
	std::clog.put('\n');
}

inline
void
print (
	const char * prog,
	const ProcessEnvironment & envs,
	const CharacterCell::colour_type & colour0,
	const CharacterCell::colour_type & colour1,
	const char * level,
	const char * file,
	unsigned long line,
	const char * how
) {
	std::ostringstream s;
	s << file;
	s.put('(') << line;
	s.put(')');
	print(prog, envs, colour0, colour1, level, s.str().c_str(), how);
}

}

/* Common usage messages throwing EXIT_USAGE ********************************
// **************************************************************************
*/

extern
void
die [[gnu::noreturn]] (
	const char * prog,
	const ProcessEnvironment & envs,
	const popt::error & e
) {
	print(prog, envs, light_red, dark_orange3, "fatal", e.arg, e.msg);
	throw static_cast<int>(EXIT_USAGE);
}

void
die_usage [[gnu::noreturn]] (
	const char * prog,
	const ProcessEnvironment & envs,
	const char * how
) {
	print(prog, envs, light_red, "fatal", how);
	throw static_cast<int>(EXIT_USAGE);
}

void
die_usage [[gnu::noreturn]] (
	const char * prog,
	const ProcessEnvironment & envs,
	const char * what,
	const char * how
) {
	print(prog, envs, light_red, dark_orange3, "fatal", what, how);
	throw static_cast<int>(EXIT_USAGE);
}

void
die_usage [[gnu::noreturn]] (
	const char * prog,
	const ProcessEnvironment & envs,
	const char * what0,
	const char * what1,
	const char * how
) {
	print(prog, envs, light_red, dark_orange3, dark_white, "fatal", what0, what1, how);
	throw static_cast<int>(EXIT_USAGE);
}

void
die_missing_argument [[gnu::noreturn]] (
	const char * prog,
	const ProcessEnvironment & envs,
	const char * what
) {
	print(prog, envs, light_red, "fatal", (std::string("Missing ") + what + ".").c_str());
	throw static_cast<int>(EXIT_USAGE);
}

void
die_missing_next_program [[gnu::noreturn]] (
	const char * prog,
	const ProcessEnvironment & envs
) {
	die_missing_argument(prog, envs, "next program");
}

void
die_missing_variable_name [[gnu::noreturn]] (
	const char * prog,
	const ProcessEnvironment & envs
) {
	die_missing_argument(prog, envs, "variable name");
}

void
die_missing_service_name [[gnu::noreturn]] (
	const char * prog,
	const ProcessEnvironment & envs
) {
	die_missing_argument(prog, envs, "service name");
}

void
die_missing_directory_name [[gnu::noreturn]] (
	const char * prog,
	const ProcessEnvironment & envs
) {
	die_missing_argument(prog, envs, "directory name");
}

void
die_missing_expression [[gnu::noreturn]] (
	const char * prog,
	const ProcessEnvironment & envs
) {
	die_missing_argument(prog, envs, "expression");
}

void
die_missing_environment_variable [[gnu::noreturn]] (
	const char * prog,
	const ProcessEnvironment & envs,
	const char * what
) {
	print(prog, envs, light_red, dark_orange3, "fatal", what, "Missing environment variable.");
	throw static_cast<int>(EXIT_USAGE);
}

void
die_unexpected_argument [[gnu::noreturn]] (
	const char * prog,
	std::vector<const char *> & args,
	const ProcessEnvironment & envs
) {
	die_usage(prog, envs, args.front(), "Unexpected argument");
}

void
die_invalid_argument [[gnu::noreturn]] (
	const char * prog,
	const ProcessEnvironment & envs,
	const char * what,
	const char * how
) {
	die_usage(prog, envs, what, how);
}

void
die_unsupported_command [[gnu::noreturn]] (
	const char * prog,
	const ProcessEnvironment & envs,
	const char * what
) {
	die_usage(prog, envs, what, "Unsupported command");
}

void
die_unsupported_command [[gnu::noreturn]] (
	const char * prog,
	const ProcessEnvironment & envs,
	const char * what0,
	const char * what1
) {
	die_usage(prog, envs, what0, what1, "Unsupported command");
}

void
die_unrecognized_command [[gnu::noreturn]] (
	const char * prog,
	const ProcessEnvironment & envs,
	const char * what
) {
	die_usage(prog, envs, what, "Unrecognized command");
}

/* Common error messages ****************************************************
// **************************************************************************
*/

void
message_warning (
	const char * prog,
	const ProcessEnvironment & envs,
	const char * what,
	const char * how
) {
	print(prog, envs, dark_orange, dark_orange3, "warning", what, how);
}

void
message_warning (
	const char * prog,
	const ProcessEnvironment & envs,
	const char * what0,
	const char * what1,
	const char * what2,
	const char * what3,
	const char * how
) {
	print(prog, envs, dark_orange, dark_orange3, dark_white, dark_white, dark_white, "warning", what0, what1, what2, what3, how);
}

void
message_error_errno (
	const char * prog,
	const ProcessEnvironment & envs,
	const char * what,
	int error
) {
	print(prog, envs, light_orange, dark_orange3, "error", what, std::strerror(error));
}

void
message_error_errno (
	const char * prog,
	const ProcessEnvironment & envs,
	const char * what
) {
	message_error_errno(prog, envs, what, errno);
}

void
message_error (
	const char * prog,
	const ProcessEnvironment & envs,
	const char * what,
	const char * how
) {
	print(prog, envs, light_orange, dark_orange3, "error", what, how);
}

/* Common fatal messages throwing EXIT_FAILURE ******************************
// **************************************************************************
*/

void
message_fatal_errno (
	const char * prog,
	const ProcessEnvironment & envs,
	int error,
	const char * what
) {
	print(prog, envs, light_red, dark_orange3, "fatal", what, std::strerror(error));
	std::clog.put('\n');
}

void
message_fatal_errno (
	const char * prog,
	const ProcessEnvironment & envs,
	const char * what
) {
	message_fatal_errno(prog, envs, errno, what);
}

void
message_fatal_errno (
	const char * prog,
	const ProcessEnvironment & envs,
	const char * what0,
	const char * what1
) {
	const int error(errno);
	print(prog, envs, light_red, dark_orange3, dark_white, "fatal", what0, what1, std::strerror(error));
}

void
message_fatal_errno (
	const char * prog,
	const ProcessEnvironment & envs,
	const char * what0,
	const char * what1,
	const char * what2
) {
	const int error(errno);
	print(prog, envs, light_red, dark_orange3, dark_white, dark_white, "fatal", what0, what1, what2, std::strerror(error));
}

void
die_errno [[gnu::noreturn]] (
	const char * prog,
	const ProcessEnvironment & envs,
	int error,
	const char * what
) {
	message_fatal_errno(prog, envs, error, what);
	throw static_cast<int>(EXIT_FAILURE);
}

void
die_errno [[gnu::noreturn]] (
	const char * prog,
	const ProcessEnvironment & envs,
	const char * what
) {
	message_fatal_errno(prog, envs, what);
	throw static_cast<int>(EXIT_FAILURE);
}

void
die_errno [[gnu::noreturn]] (
	const char * prog,
	const ProcessEnvironment & envs,
	const char * what0,
	const char * what1
) {
	message_fatal_errno(prog, envs, what0, what1);
	throw static_cast<int>(EXIT_FAILURE);
}

void
die_errno [[gnu::noreturn]] (
	const char * prog,
	const ProcessEnvironment & envs,
	const char * what0,
	const char * what1,
	const char * what2
) {
	message_fatal_errno(prog, envs, what0, what1, what2);
	throw static_cast<int>(EXIT_FAILURE);
}

void
die_invalid [[gnu::noreturn]] (
	const char * prog,
	const ProcessEnvironment & envs,
	const char * what,
	const char * how
) {
	print(prog, envs, light_red, dark_orange3, "fatal", what, how);
	throw static_cast<int>(EXIT_FAILURE);
}

void
die_invalid [[gnu::noreturn]] (
	const char * prog,
	const ProcessEnvironment & envs,
	const char * what0,
	const char * what1,
	const char * how
) {
	print(prog, envs, light_red, dark_orange3, dark_white, "fatal", what0, what1, how);
	throw static_cast<int>(EXIT_FAILURE);
}

void
die_parser_error [[gnu::noreturn]] (
	const char * prog,
	const ProcessEnvironment & envs,
	const char * file,
	unsigned long line,
	const char * what,
	const char * how
) {
	print(prog, envs, light_red, dark_orange3, dark_white, "fatal", file, line, what, how);
	throw static_cast<int>(EXIT_FAILURE);
}

void
die_parser_error [[gnu::noreturn]] (
	const char * prog,
	const ProcessEnvironment & envs,
	const char * file,
	unsigned long line,
	const char * how
) {
	print(prog, envs, light_red, dark_orange3, "fatal", file, line, how);
	throw static_cast<int>(EXIT_FAILURE);
}
