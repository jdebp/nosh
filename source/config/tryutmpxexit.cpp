#include <utmpx.h>
int main()
{
	struct utmpx u = {};
	(void)u.ut_exit;
	return 0;
}
