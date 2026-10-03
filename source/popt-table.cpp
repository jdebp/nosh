/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <iostream>
#include <iomanip>
#include <cstring>

#include "popt.h"
#include "ECMA48Output.h"

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
void table_definition::help(std::ostream & out, ECMA48Output & ecma48, bool do_colour)
{
	for (unsigned i(0); i < count; ++i)
		if (dynamic_cast<named_definition *>(array[i])) {
			if (do_colour) ecma48.set_underline(true);
			out << description;
			if (do_colour) ecma48.set_underline(false);
			out << ":\n";
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
				l += 2 + std::strlen(args_description);
			if (l > w) w = l;
		}
	for (unsigned i(0); i < count; ++i)
		if (named_definition * n = dynamic_cast<named_definition *>(array[i])) {
			std::size_t l = 0;
			out.put('\t');
			if (char short_name = n->query_short_name()) {
				if (do_colour) ecma48.set_boldface(true);
				out.put('-') << std::string(1, short_name);
				l += 2;
				if (do_colour) ecma48.set_boldface(false);
			}
			if (const char * long_name = n->query_long_name()) {
				if (n->query_short_name()) {
					out << ", " << std::flush;
					l += 2;
				}
				if (do_colour) ecma48.set_boldface(true);
				out << "--" << std::string(long_name);
				if (do_colour) ecma48.set_boldface(false);
				l += 1 + std::strlen(long_name);
			}
			if (const char * args_description = n->query_args_description()) {
				if (do_colour) ecma48.set_italics(true);
				out.put(' ') << std::string(args_description);
				if (do_colour) ecma48.set_italics(false);
				l += 1 + std::strlen(args_description);
			}
			while (l < w) { out.put(' '); ++l; }
			if (const char * entry_description = n->query_description())
				out.put(' ').put(' ') << entry_description;
			out.put('\n');
		}
	for (unsigned i(0); i < count; ++i)
		if (table_definition * n = dynamic_cast<table_definition *>(array[i]))
			n->help(out, ecma48, do_colour);
}
void table_definition::long_usage(std::ostream & out, ECMA48Output & ecma48, bool do_colour)
{
	for (unsigned i(0); i < count; ++i)
		if (named_definition * n = dynamic_cast<named_definition *>(array[i])) {
			const char * long_name = n->query_long_name();
			const char * args_description = n->query_args_description();
			if (long_name || args_description) {
				char short_name = n->query_short_name();
				out.put('[');
				if (do_colour) ecma48.set_boldface(true);
				if (args_description && short_name)
					out.put('-').put(short_name);
				if (long_name) {
					if (args_description && short_name) {
						if (do_colour) ecma48.set_boldface(false);
						out.put('|');
						if (do_colour) ecma48.set_boldface(true);
					}
					out << "--" << long_name;
				}
				if (do_colour) ecma48.set_boldface(false);
				if (args_description) {
					if (do_colour) ecma48.set_italics(true);
					out.put(' ') << args_description;
					if (do_colour) ecma48.set_italics(false);
				}
				out << "] ";
			}
		}
	for (unsigned i(0); i < count; ++i)
		if (table_definition * n = dynamic_cast<table_definition *>(array[i]))
			n->long_usage(out, ecma48, do_colour);
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
