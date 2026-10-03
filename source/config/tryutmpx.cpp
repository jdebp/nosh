#include <utmpx.h>
int main()
{
	struct utmpx u = {};
	(void)pututxline;
	(void)setutxent;
	(void)getutxent;
	(void)endutxent;
	(void)u;
	return 0;
}
