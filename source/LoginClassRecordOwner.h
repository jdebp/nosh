/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_LOGIN_CLASS_RECORD_OWNER_H)
#define INCLUDE_LOGIN_CLASS_RECORD_OWNER_H

#include <string>
#include "config/haslogincap.h"
#if defined(HAS_LOGINCAP)
#include <sys/types.h>
#include <login_cap.h>
#else
struct login_cap_t;
#endif
#include <pwd.h>
#include <unistd.h>

/// \brief A wrapper for login_cap_t that automatically closes the record in its destructor.
struct LoginClassRecordOwner {
	static LoginClassRecordOwner GetSystem(const passwd &);
	static LoginClassRecordOwner GetUser(const passwd &);
	LoginClassRecordOwner(LoginClassRecordOwner && o) : d(o.release()) {}
	~LoginClassRecordOwner() { assign(nullptr); }
	const char * getcapstr(const char *, const char *, const char *);
	bool getcapbool(const char *, bool) const;
protected:
	login_cap_t * d;
	void assign(login_cap_t * n);
	login_cap_t * release() { login_cap_t *dp(d); d = nullptr; return dp; }
	std::string res;
private:
#if 0 // These are unused at present.
	LoginClassRecordOwner & operator= ( login_cap_t * n ) { assign(n); return *this; }
#endif
	LoginClassRecordOwner(login_cap_t * dp = nullptr) : d(dp) {}
	LoginClassRecordOwner & operator= (const LoginClassRecordOwner &);
	LoginClassRecordOwner(const LoginClassRecordOwner &);
};

#endif
