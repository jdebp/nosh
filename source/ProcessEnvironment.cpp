/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <map>
#include <string>
#include <cstring>
#include <cstdlib>
#include "ProcessEnvironment.h"

#include <cstdio>

ProcessEnvironment::ProcessEnvironment(const char * const * envp) :
	global_environ(envp), 
	cached_data(envp)
{
}

void
ProcessEnvironment::make_data()
{
	if (!cached_data) {
		s.clear();
		s.reserve(m.size());
		for (map::const_iterator i(m.begin()), e(m.end()); i != e; ++i)
			s.push_back(i->first + "=" + i->second);
		d.clear();
		d.reserve(s.size() + 1);
		for (vec::const_iterator i(s.begin()), e(s.end()); i != e; ++i)
			d.push_back(i->c_str());
		d.push_back(0);
		cached_data = d.data();
	}
}

bool 
ProcessEnvironment::clear()
{
	m.clear();
	global_environ = 0;
	cached_data = 0;
	return true;
}

bool 
ProcessEnvironment::set(const std::string & var, const std::string & val)
{
	make_copy();
	typedef std::pair<map::iterator, bool> ret;
	ret r(m.insert(map::value_type(var, val)));
	if (!r.second)
		r.first->second = val;
	return true;
}

bool 
ProcessEnvironment::set(const char * cvar, const std::string & val)
{
	const std::string var(cvar);
	return set(var, val);
}

bool 
ProcessEnvironment::set(const char * cvar, const char * cval)
{
	const std::string var(cvar);
	if (cval) {
		const std::string val(cval);
		return set(var, val);
	}
	make_copy();
	map::iterator i(m.find(var));
	if (m.end() != i) m.erase(i);
	return true;
}

const char * 
ProcessEnvironment::query(const char * var) const
{
	if (global_environ) return std::getenv(var);
	map::const_iterator i(m.find(var));
	if (m.end() == i) return 0;
	return i->second.c_str();
}

ProcessEnvironment::const_iterator
ProcessEnvironment::find(const std::string & var)
{
	make_copy();
	return m.find(var);
}

ProcessEnvironment::const_iterator
ProcessEnvironment::begin()
{
	make_copy();
	return m.begin();
}

ProcessEnvironment::const_iterator
ProcessEnvironment::end()
{
	make_copy();
	return m.end();
}

void 
ProcessEnvironment::make_copy()
{
	if (global_environ) {
		while (const char * e = *global_environ++) {
			if (const char * q = std::strchr(e, '=')) {
				const std::string var(e, q);
				m.insert(map::value_type(var, q + 1));
			} else
				m.insert(map::value_type(e, ""));
		}
		global_environ = 0;
	}
	cached_data = 0;
}
