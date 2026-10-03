#include <sys/types.h>
#include <login_cap.h>
#include <pwd.h>
int main()
{
	login_cap_t cap;
	(void)login_getclass;
#if !defined(__OpenBSD__) && !defined(__NetBSD__)
	(void)login_getuserclass;
#endif
	(void)login_getcapstr;
	(void)login_getcapbool;
	(void)login_close;
	(void)cap;
	return 0;
}
