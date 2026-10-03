#include <utmpx.h>
int main()
{
	struct utmpx u = {};
	(void)u.ut_addr_v6;
	return 0;
}
