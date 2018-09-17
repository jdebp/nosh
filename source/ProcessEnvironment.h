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
private:
	typedef std::map<std::string, std::string> map;
public:
	ProcessEnvironment(const char * const *);
	const char * const * data() { if (!cached_data) make_data(); return cached_data; }
	std::size_t size() { make_copy(); return m.size(); }
	bool clear();
	bool set(const char *, const char *);
	bool set(const char *, const std::string &);
	bool set(const std::string &, const std::string &);
	bool unset(const char * var) { return set(var, 0); }
	bool unset(const std::string & var) { return set(var, 0); }
	const char * query(const char *) const;
	typedef map::const_iterator const_iterator;
	const_iterator find(const std::string &);
	const_iterator begin();
	const_iterator end();
protected:
	const char * const * global_environ;	///< non-NULL if we are simply passing through to std::getenv()
	const char * const * cached_data;	///< non-NULL if the cache is valid
	map m;	///< the actual environment dictionary
	typedef std::vector<std::string> vec;
	vec s;	///< cached var=value strings
	std::vector<const char *> d;	///< cached vector of const char pointers
	void make_copy();
	void make_data();
};

#endif
