/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define _BSD_SOURCE 1
#include <map>
#include <string>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "config/haslogincap.h"
#if defined(HAS_LOGINCAP)
#include <sys/types.h>
#include <login_cap.h>
#include <pwd.h>
#endif
#include <unistd.h>
#include "ControlCharacters.h"
#include "ProcessEnvironment.h"
#include "LoginClassRecordOwner.h"
#include "FileDescriptorOwner.h"
#include "FileStar.h"
#include "fdutils.h"

#if !defined(LOGIN_DEFROOTCLASS)
#define LOGIN_DEFROOTCLASS "root"
#endif
#if !defined(LOGIN_DEFCLASS)
#define LOGIN_DEFCLASS "default"
#endif
#if !defined(LOGIN_MECLASS)
#define LOGIN_MECLASS "me"
#endif

#if defined(HAS_LOGINCAP)

// OpenBSD and NetBSD require const incorrectness bodges.
namespace {
#if defined(__OpenBSD__)
inline login_cap_t * login_getclass(const char * c) { return login_getclass(const_cast<char *>(c)); }
#endif
#if defined(__OpenBSD__) || defined(__NetBSD__)
inline const char * login_getcapstr(login_cap_t * d, const char * cap, const char * def, const char * err) { return login_getcapstr(d, const_cast<char *>(cap), const_cast<char *>(def), const_cast<char *>(err)); }
#endif
}

namespace {

// FreeBSD's login_getpwclass() has a security hole when the class is "me".
// And OpenBSD does not have the function at all.
inline
login_cap_t *
login_getsystemclass(
	const passwd * pwd
) {
	const char * c(nullptr);
	if (!pwd)
		c = LOGIN_DEFCLASS;
	else if (pwd->pw_class && *pwd->pw_class)
		c = pwd->pw_class;
	else
		c = 0 == pwd->pw_uid ? LOGIN_DEFROOTCLASS : LOGIN_DEFCLASS;
	return login_getclass(c);
}

#if defined(__OpenBSD__) || defined(__NetBSD__)
// OpenBSD and NetBSD lack this, too.
inline
login_cap_t *
login_getuserclass(
	const passwd * pwd
) {
	if (!pwd || !pwd->pw_dir || !*pwd->pw_dir) return nullptr;
	login_cap_t * r(static_cast<login_cap_t *>(malloc(sizeof(login_cap_t))));
	r->lc_class = r->lc_cap = r->lc_style = nullptr;
	if (!r) {
error:
		if (r) login_close(r);
		return nullptr;
	}
	r->lc_class = strdup(LOGIN_MECLASS);
	if (nullptr == r->lc_class) goto error;
	static const std::string base("/.login_conf");
	const std::string filename(pwd->pw_dir + base);
	const char *classfiles[2] = { filename.c_str(), nullptr };
	const int res(cgetent(&r->lc_cap, classfiles, r->lc_class));
	if (0 != res) goto error;
	return r;
}
#endif

}

#else

/// \name C++ data type for non-BSD systems
/// @{

struct login_cap_t {
	login_cap_t() : caps() {}
	~login_cap_t() {}
	int getbool(const char * cap, int def) const;
	const char * getstr(const char * cap, const char * def, const char * err);
	bool lookup(const char * filename, const char * c);
private:
	bool lookup(FILE * f, const std::string & name);
	bool matches(const std::string & line, const std::string & name);
	typedef std::vector<std::string> stringlist;
	stringlist caps;
};

inline
int
login_cap_t::getbool(
	const char * cap,
	int def
) const {
	if (!cap) return def;
	const std::string c(cap), nc(c + '@');
	for (stringlist::const_iterator p(caps.begin()), e(caps.end()); e != p; ++p) {
		if (c == *p) return 1;
		if (nc == *p) return 0;
	}
	return def;
}

inline
const char *
login_cap_t::getstr(
	const char * cap,
	const char * def,
	const char * err
) {
	if (!cap) return err;
	const std::string c(cap), ce(c + '='), cen(ce + '@');
	for (stringlist::const_iterator p(caps.begin()), e(caps.end()); e != p; ++p) {
		if (cen == *p) return def;
		if (p->length() >= ce.length() && 0 == std::memcmp(p->data(), ce.data(), ce.length())) {
			const std::size_t len(p->length() - ce.length());
			char * r = static_cast<char *>(std::malloc(len + 1));
			if (!r) return err;
			std::memmove(r, p->data() + ce.length(), len);
			r[len] = '\0';
			for (char * q(r); *q; ++q) {
				if ('\\' == *q && q[1]) {
					switch (q[1]) {
						case '0': 		q[1] = NUL; break;
						case 'b': case 'B':	q[1] = '\b'; break;
						case 'r': case 'R':	q[1] = '\r'; break;
						case 'c': case 'C':	q[1] = ':'; break;
						case 'e': case 'E':	q[1] = ESC; break;
						case 't': case 'T':	q[1] = '\t'; break;
						case 'n': case 'N':	q[1] = '\n'; break;
						case 'f': case 'F':	q[1] = '\f'; break;
						default:		break;
					}
					std::memmove(q, q + 1, len - (q - r));	// includes the terminating NUL
				} else
				if ('^' == *q && q[1]) {
					q[1] ^= 0x40;
					std::memmove(q, q + 1, len - (q - r));	// includes the terminating NUL
				}
			}
			return r;
		}
	}
	return def;
}

inline
bool
login_cap_t::matches(
	const std::string & line,	///< must not have leading whitespace
	const std::string & name
) {
	std::string first_field;
	std::string * current(&first_field);
	for (std::string::const_iterator p(line.begin()), e(line.end()); e != p; ++p) {
		if (':' == *p) {
			current = nullptr;
		} else
		if (current || !std::isspace(*p))
		{
			if (!current) {
				caps.push_back(std::string());
				current = &caps.back();
			}
			*current += *p;
		}
	}
	if (caps.empty()) return false;
	current = nullptr;
	stringlist names;
	for (std::string::const_iterator p(first_field.begin()), e(first_field.end()); e != p; ++p) {
		if ('|' == *p) {
			current = nullptr;
		} else
		{
			if (!current) {
				names.push_back(std::string());
				current = &names.back();
			}
			*current += *p;
		}
	}
	for (stringlist::const_iterator p(names.begin()), e(names.end()); e != p; ++p) {
		if (name == *p)
			return true;
	}
	return false;
}

inline
bool
login_cap_t::lookup(
	FILE * f,
	const std::string & name
) {
	caps.clear();
	enum { BOL, COMMENT, COMMENT_ESCAPE, LEAD_SPACE, LINE, LINE_ESCAPE } state = BOL;
	std::string line;
	for (;;) {
		const int c(std::getc(f));
		if (EOF == c) return false;
		switch (state) {
			append:
				line += static_cast<char>(c);
				break;
			newline:
				line.clear();
				state = BOL;
				break;
			case BOL:
				if (LF == c) 
					goto newline;
				else
				if (std::isspace(c))
					state = LEAD_SPACE;
				else
				if ('#' == c)
					state = COMMENT;
				else {
					state = LINE;
					goto append;
				}
				break;
			case LEAD_SPACE:
				if (LF == c) 
					goto newline;
				else
				if (std::isspace(c))
					break;
				else
				if ('#' == c)
					state = COMMENT;
				else {
					state = LINE;
					goto append;
				}
				break;
			case COMMENT:
				if (LF == c) 
					goto newline;
				else
				if ('\\' == c)
					state = COMMENT_ESCAPE;
				break;
			case COMMENT_ESCAPE:
				state = COMMENT;
				break;
			case LINE:
				if (LF == c) {
					if (matches(line, name)) return true;
					caps.clear();
					goto newline;
				} else
				if ('\\' == c)
					state = LINE_ESCAPE;
				else
					goto append;
				break;
			case LINE_ESCAPE:
				state = LINE;
				if (LF != c) {
					line += static_cast<char>('\\');
					goto append;
				}
				break;
		}
	}
}

inline
bool
login_cap_t::lookup(
	const char * filename,
	const char * c
) {
	if (!filename || !c) return false;
	FileDescriptorOwner fd(open_read_at(AT_FDCWD, filename));
	if (0 > fd.get()) return false;
	FileStar f(fdopen(fd.get(), "r"));
	if (!f) return false;
	fd.release();
	return lookup(f.operator FILE *(), c);
}

/// @}

/// \name dummy functions for non-BSD systems
/// @{
namespace {

inline
login_cap_t *
login_getsystemclass(
	const passwd * pwd
) {
	const char * c(pwd && 0 == pwd->pw_uid ? LOGIN_DEFROOTCLASS : LOGIN_DEFCLASS);
	login_cap_t * r(new login_cap_t());
	if (r) {
		const char filename[] = "/etc/login.conf";
		if (!r->lookup(filename, c)) {
			const int error(errno);
			delete r;
			errno = error;
			return nullptr;
		}
	}
	return r;
}

inline
login_cap_t *
login_getuserclass(
	const passwd * pwd
) {
	if (!pwd || !pwd->pw_dir || !*pwd->pw_dir) return nullptr;
	login_cap_t * r(new login_cap_t());
	if (r) {
		static const std::string base("/.login_conf");
		const std::string filename(pwd->pw_dir + base);
		if (!r->lookup(filename.c_str(), LOGIN_MECLASS)) {
			const int error(errno);
			delete r;
			errno = error;
			return nullptr;
		}
	}
	return r;
}

inline
const char *
login_getcapstr(
	struct login_cap_t * d,
	const char *cap,
	const char *def,
	const char *err
) {
	return d ? d->getstr(cap, def, err) : err;
}

inline
int
login_getcapbool(
	struct login_cap_t * d,
	const char * cap,
	int def
) {
	return d ? d->getbool(cap, def) : def;
}

inline
void
login_close(
	struct login_cap_t * cap
) {
	delete cap;
}

}
/// @}

#endif

const char *
LoginClassRecordOwner::getcapstr(
	const char * cap,
	const char * def,
	const char * err
) {
#if defined(__OpenBSD__)
	// OpenBSD's login_getcapstr() does not have FreeBSD's NULL pointer check.
	if (!d) return err;
#endif
	const char * r(login_getcapstr(d, cap, def, err));
	// login_getcapstr() allocates memory, only sometimes.
	if (def != r && err != r && r) {
		res = r;
		free(const_cast<char *>(r));
		r = res.c_str();
	}
	return r;
}

bool
LoginClassRecordOwner::getcapbool(
	const char * cap,
	bool def
) const {
#if defined(__OpenBSD__)
	// OpenBSD's login_getcapbool() does not have FreeBSD's NULL pointer check.
	if (!d) return false;
#endif
	int r(login_getcapbool(d, cap, def ? 1 : 0));
	return !!r;
}

void
LoginClassRecordOwner::assign(
	login_cap_t * n
) {
	if (d) login_close(d);
	d = n;
}

LoginClassRecordOwner
LoginClassRecordOwner::GetSystem(
	const passwd & pw
) {
	return LoginClassRecordOwner(login_getsystemclass(&pw));
}

LoginClassRecordOwner
LoginClassRecordOwner::GetUser(
	const passwd & pw
) {
	return LoginClassRecordOwner(login_getuserclass(&pw));
}
