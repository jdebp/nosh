#include <utmpx.h>
int main()
{
	struct utmpx u = {};
	(void)u.ut_ss;
	return 0;
}
