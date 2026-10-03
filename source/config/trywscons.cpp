#include <sys/ioctl.h>
#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplay_usl_io.h>	// VT/CONSIO ioctls
int main()
{
	(void)WSEVENT_VERSION;
	struct wscons_event e;
	static_cast<void>(e);
	return 0;
}
