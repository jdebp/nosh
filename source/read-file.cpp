/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <string>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include "utils.h"

std::vector<std::string>
read_file (
	FILE * f
) {
	std::vector<std::string> a;
	std::string * current = 0;
	enum { UNQUOTED, DOUBLE, SINGLE } quote(UNQUOTED);
	bool slash(false), comment(false);
	for (;;) {
		const int c(std::fgetc(f));
		if (std::feof(f)) break;
		if (UNQUOTED == quote && !slash) {
			if (comment) {
				if ('\n' == c)
					comment = false;
				continue;
			} else if ('#' == c && !current) {
				comment = true;
				continue;
			} else if (std::isspace(c)) {
				current = 0;
				continue;
			}
		}
		if (slash && '\n' == c) {
			slash = false;
			continue;
		}
		if (!current) {
			a.push_back(std::string());
			current = &a.back();
		}
		if (slash) {
			*current += char(c);
			slash = false;
		} else switch (quote) {
			case SINGLE:
				if ('\'' == c)
					quote = UNQUOTED;
				else
					*current += char(c);
				break;
			case DOUBLE:
				if ('\\' == c)
					slash = true;
				else if ('\"' == c)
					quote = UNQUOTED;
				else
					*current += char(c);
				break;
			case UNQUOTED:
				if ('\\' == c)
					slash = true;
				else if ('\"' == c)
					quote = DOUBLE;
				else if ('\'' == c)
					quote = SINGLE;
				else
					*current += char(c);
				break;
		}
	}
	if (slash)
		throw "Slash before end of file.";
	if (UNQUOTED != quote)
		throw "Unterminated quote at end of file.";
	return a;
}

std::vector<std::string>
read_file (
	const char * prog,
	const char * filename,
	FILE * f
) {
	try {
		std::vector<std::string> v(read_file(f));
		if (std::ferror(f)) {
			const int error(errno);
			std::fclose(f);
			std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, filename, std::strerror(error));
			throw EXIT_FAILURE;
		}
		std::fclose(f);
		return v;
	} catch (const char * r) {
		std::fclose(f);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, filename, r);
		throw EXIT_FAILURE;
	} catch (...) {
		std::fclose(f);
		throw;
	}
}

std::vector<std::string>
read_file (
	const char * prog,
	const char * filename
) {
	FILE * f(std::fopen(filename, "r"));
	if (!f) {
		const int error(errno);
		std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, filename, std::strerror(error));
		throw EXIT_FAILURE;
	}
	return read_file(prog, filename, f);
}
