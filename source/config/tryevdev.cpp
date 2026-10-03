#include <linux/input.h>
int main()
{
	(void)EVIOCGRAB;
	(void)LED_CAPSL;
	(void)LED_NUML;
	(void)LED_SCROLLL;
	(void)LED_KANA;
	(void)LED_COMPOSE;
	(void)EV_ABS;
	(void)EV_REL;
	(void)EV_KEY;
	(void)BTN_LEFT;
	(void)BTN_MIDDLE;
	(void)BTN_RIGHT;
	(void)BTN_SIDE;
	(void)BTN_EXTRA;
	(void)BTN_FORWARD;
	(void)BTN_BACK;
	(void)BTN_TASK;
	return 0;
}
