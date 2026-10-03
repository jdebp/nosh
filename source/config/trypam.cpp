#include <security/pam_appl.h>
int main()
{
	(void)pam_start;
	(void)pam_end;
	(void)pam_open_session;
	(void)pam_close_session;
	(void)pam_authenticate;
	(void)pam_setcred;
	(void)pam_chauthtok;
	(void)pam_acct_mgmt;
	return 0;
}
