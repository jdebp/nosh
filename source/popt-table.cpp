/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <iostream>
#include <iomanip>
#include <cstring>
#include <cstdlib>
#include <cctype>

#include "popt.h"

using namespace popt;

table_definition::~table_definition() {}
bool table_definition::execute(processor & proc, char c)
{
	for (unsigned i(0); i < count; ++i)
		if (array[i]->execute(proc, c))
			return true;
	return false;
}
bool table_definition::execute(processor & proc, char c, const char * s)
{
	for (unsigned i(0); i < count; ++i)
		if (array[i]->execute(proc, c, s))
			return true;
	return false;
}
bool table_definition::execute(processor & proc, const char * s)
{
	for (unsigned i(0); i < count; ++i)
		if (array[i]->execute(proc, s))
			return true;
	return false;
}
void table_definition::help()
{
	for (unsigned i(0); i < count; ++i)
		if (dynamic_cast<named_definition *>(array[i])) {
			std::cout << description << ":\n";
			break;
		}
	std::size_t w = 0;
	for (unsigned i(0); i < count; ++i)
		if (named_definition * n = dynamic_cast<named_definition *>(array[i])) {
			std::size_t l = 0;
			if (n->query_short_name()) l += 2;
			if (const char * long_name = n->query_long_name()) {
				if (n->query_short_name()) l += 2;
				l += 2 + std::strlen(long_name);
			}
			if (const char * args_description = n->query_args_description())
				l += 1 + std::strlen(args_description);
			if (l > w) w = l;
		}
	for (unsigned i(0); i < count; ++i)
		if (named_definition * n = dynamic_cast<named_definition *>(array[i])) {
			std::string t;
			if (char short_name = n->query_short_name())
				t += "-" + std::string(1, short_name);
			if (const char * long_name = n->query_long_name()) {
				if (n->query_short_name()) t += ", ";
				t += "--" + std::string(long_name);
			}
			if (const char * args_description = n->query_args_description())
				t += " " + std::string(args_description);
			std::cout.put('\t') << std::setiosflags(std::iostream::left) << std::setw(static_cast<int>(w))
#if defined(__WATCOMC__) // Watcom C++ library bug: std::strings are not output as one lump
			<< t.c_str()
#else
			<< t
#endif
			;
			if (const char * entry_description = n->query_description())
				std::cout.put(' ') << entry_description;
			std::cout.put('\n');
		}
	for (unsigned i(0); i < count; ++i)
		if (table_definition * n = dynamic_cast<table_definition *>(array[i]))
			n->help();
}
void table_definition::long_usage()
{
	for (unsigned i(0); i < count; ++i)
		if (named_definition * n = dynamic_cast<named_definition *>(array[i])) {
			const char * long_name = n->query_long_name();
			const char * args_description = n->query_args_description();
			if (long_name || args_description) {
				char short_name = n->query_short_name();
				std::cout << "[";
				if (args_description && short_name) 
					std::cout.put('-').put(short_name);
				if (long_name) {
					if (args_description && short_name) 
						std::cout.put('|');
					std::cout << "--" << long_name;
				}
				if (args_description)
					std::cout << " " << args_description;
				std::cout << "] ";
			}
		}
	for (unsigned i(0); i < count; ++i)
		if (table_definition * n = dynamic_cast<table_definition *>(array[i]))
			n->long_usage();
}
void table_definition::gather_combining_shorts(std::string & shorts)
{
	for (unsigned i(0); i < count; ++i)
		if (named_definition * n = dynamic_cast<named_definition *>(array[i])) {
			if (!n->query_args_description()) {
				if (char short_name = n->query_short_name())
					shorts += short_name;
			}
		}
	for (unsigned i(0); i < count; ++i)
		if (table_definition * n = dynamic_cast<table_definition *>(array[i]))
			n->gather_combining_shorts(shorts);
}
