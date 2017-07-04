/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_PROCESS_ENVIRONMENT_H)
#define INCLUDE_PROCESS_ENVIRONMENT_H

#include <map>
#include <vector>
#include <string>

struct ProcessEnvironment {
	ProcessEnvironment(const char * const *);
	const char * const * data() { if (!cached_data) make_data(); return cached_data; }
	bool clear();
	bool set(const char *, const char *);
	bool set(const char *, const std::string &);
	bool set(const std::string &, const std::string &);
	bool unset(const char * var) { return set(var, 0); }
	bool unset(const std::string & var) { return set(var, 0); }
	const char * query(const char *) const;
protected:
	const char * const * global_environ;
	const char * const * cached_data;
	typedef std::map<std::string, std::string> map;
	map m;
	typedef std::vector<std::string> vec;
	vec s;
	std::vector<const char *> d;
	void make_copy();
	void make_data();
};

#endif
