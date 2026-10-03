#include <utmpx.h>
int main()
{
	struct utmpx u = {};
	(void)u.ut_session;
	return 0;
}
