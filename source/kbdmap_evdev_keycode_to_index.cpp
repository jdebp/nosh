/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if defined(__LINUX__) || defined(__linux__)

#include <linux/input.h>
#include "kbdmap.h"
#include "kbdmap_utils.h"

/// The Linux evdev keycode maps to a row+column in the current keyboard map, which contains an action for that row+column.
uint16_t
linux_evdev_keycode_to_keymap_index (
	const uint16_t k
) {
	switch (k) {
		default:	break;
		case KEY_ESC:		return KBDMAP_INDEX_ESC;
		case KEY_1:		return KBDMAP_INDEX_1;
		case KEY_2:		return KBDMAP_INDEX_2;
		case KEY_3:		return KBDMAP_INDEX_3;
		case KEY_4:		return KBDMAP_INDEX_4;
		case KEY_5:		return KBDMAP_INDEX_5;
		case KEY_6:		return KBDMAP_INDEX_6;
		case KEY_7:		return KBDMAP_INDEX_7;
		case KEY_8:		return KBDMAP_INDEX_8;
		case KEY_9:		return KBDMAP_INDEX_9;
		case KEY_0:		return KBDMAP_INDEX_0;
		case KEY_MINUS:		return KBDMAP_INDEX_MINUS;
		case KEY_EQUAL:		return KBDMAP_INDEX_EQUALS;
		case KEY_BACKSPACE:	return KBDMAP_INDEX_BACKSPACE;
		case KEY_TAB:		return KBDMAP_INDEX_TAB;
		case KEY_Q:		return KBDMAP_INDEX_Q;
		case KEY_W:		return KBDMAP_INDEX_W;
		case KEY_E:		return KBDMAP_INDEX_E;
		case KEY_R:		return KBDMAP_INDEX_R;
		case KEY_T:		return KBDMAP_INDEX_T;
		case KEY_Y:		return KBDMAP_INDEX_Y;
		case KEY_U:		return KBDMAP_INDEX_U;
		case KEY_I:		return KBDMAP_INDEX_I;
		case KEY_O:		return KBDMAP_INDEX_O;
		case KEY_P:		return KBDMAP_INDEX_P;
		case KEY_LEFTBRACE:	return KBDMAP_INDEX_LEFTBRACE;
		case KEY_RIGHTBRACE:	return KBDMAP_INDEX_RIGHTBRACE;
		case KEY_ENTER:		return KBDMAP_INDEX_RETURN;
		case KEY_A:		return KBDMAP_INDEX_A;
		case KEY_S:		return KBDMAP_INDEX_S;
		case KEY_D:		return KBDMAP_INDEX_D;
		case KEY_F:		return KBDMAP_INDEX_F;
		case KEY_G:		return KBDMAP_INDEX_G;
		case KEY_H:		return KBDMAP_INDEX_H;
		case KEY_J:		return KBDMAP_INDEX_J;
		case KEY_K:		return KBDMAP_INDEX_K;
		case KEY_L:		return KBDMAP_INDEX_L;
		case KEY_SEMICOLON:	return KBDMAP_INDEX_SEMICOLON;
		case KEY_APOSTROPHE:	return KBDMAP_INDEX_APOSTROPHE;
		case KEY_GRAVE:		return KBDMAP_INDEX_GRAVE;
		case KEY_LINEFEED:	break;	// This is not a key in any real system.
		case KEY_BACKSLASH:	return KBDMAP_INDEX_EUROPE1;
		case KEY_Z:		return KBDMAP_INDEX_Z;
		case KEY_X:		return KBDMAP_INDEX_X;
		case KEY_C:		return KBDMAP_INDEX_C;
		case KEY_V:		return KBDMAP_INDEX_V;
		case KEY_B:		return KBDMAP_INDEX_B;
		case KEY_N:		return KBDMAP_INDEX_N;
		case KEY_M:		return KBDMAP_INDEX_M;
		case KEY_COMMA:		return KBDMAP_INDEX_COMMA;
		case KEY_DOT:		return KBDMAP_INDEX_DOT;
		case KEY_SLASH:		return KBDMAP_INDEX_SLASH1;
		case KEY_102ND:		return KBDMAP_INDEX_EUROPE2;
		case KEY_YEN:		return KBDMAP_INDEX_YEN;
		case KEY_LEFTSHIFT:	return KBDMAP_INDEX_SHIFT1;
		case KEY_RIGHTSHIFT:	return KBDMAP_INDEX_SHIFT2;
		case KEY_RIGHTALT:	return KBDMAP_INDEX_OPTION;
		case KEY_LEFTCTRL:	return KBDMAP_INDEX_CONTROL1;
		case KEY_RIGHTCTRL:	return KBDMAP_INDEX_CONTROL2;
		case KEY_LEFTMETA:	return KBDMAP_INDEX_SUPER1;
		case KEY_RIGHTMETA:	return KBDMAP_INDEX_SUPER2;
		case KEY_LEFTALT:	return KBDMAP_INDEX_ALT;
		case KEY_CAPSLOCK:	return KBDMAP_INDEX_CAPSLOCK;
		case KEY_SCROLLLOCK:	return KBDMAP_INDEX_SCROLLLOCK;
		case KEY_NUMLOCK:	return KBDMAP_INDEX_NUMLOCK;
		case KEY_RO:		return KBDMAP_INDEX_SLASH2;
		case KEY_KATAKANAHIRAGANA:	return KBDMAP_INDEX_KATAKANA_HIRAGANA;
		case KEY_ZENKAKUHANKAKU:return KBDMAP_INDEX_ZENKAKU_HANKAKU;
		case KEY_HIRAGANA:	return KBDMAP_INDEX_HIRAGANA;
		case KEY_KATAKANA:	return KBDMAP_INDEX_KATAKANA;
		case KEY_HENKAN:	return KBDMAP_INDEX_HENKAN;
		case KEY_MUHENKAN:	return KBDMAP_INDEX_MUHENKAN;
		case KEY_HANGEUL:	return KBDMAP_INDEX_HAN_YEONG;
		case KEY_HANJA:		return KBDMAP_INDEX_HANJA;
		case KEY_COMPOSE:	return KBDMAP_INDEX_COMPOSE;
		case KEY_SPACE:		return KBDMAP_INDEX_SPACE;
		case KEY_F1:		return KBDMAP_INDEX_F1;
		case KEY_F2:		return KBDMAP_INDEX_F2;
		case KEY_F3:		return KBDMAP_INDEX_F3;
		case KEY_F4:		return KBDMAP_INDEX_F4;
		case KEY_F5:		return KBDMAP_INDEX_F5;
		case KEY_F6:		return KBDMAP_INDEX_F6;
		case KEY_F7:		return KBDMAP_INDEX_F7;
		case KEY_F8:		return KBDMAP_INDEX_F8;
		case KEY_F9:		return KBDMAP_INDEX_F9;
		case KEY_F10:		return KBDMAP_INDEX_F10;
		case KEY_F11:		return KBDMAP_INDEX_F11;
		case KEY_F12:		return KBDMAP_INDEX_F12;
		case KEY_F13:		return KBDMAP_INDEX_F13;
		case KEY_F14:		return KBDMAP_INDEX_F14;
		case KEY_F15:		return KBDMAP_INDEX_F15;
		case KEY_F16:		return KBDMAP_INDEX_F16;
		case KEY_F17:		return KBDMAP_INDEX_F17;
		case KEY_F18:		return KBDMAP_INDEX_F18;
		case KEY_F19:		return KBDMAP_INDEX_F19;
		case KEY_F20:		return KBDMAP_INDEX_F20;
		case KEY_F21:		return KBDMAP_INDEX_F21;
		case KEY_F22:		return KBDMAP_INDEX_F22;
		case KEY_F23:		return KBDMAP_INDEX_F23;
		case KEY_F24:		return KBDMAP_INDEX_F24;
		case KEY_HOME:		return KBDMAP_INDEX_HOME;
		case KEY_UP:		return KBDMAP_INDEX_UP_ARROW;
		case KEY_PAGEUP:	return KBDMAP_INDEX_PAGE_UP;
		case KEY_LEFT:		return KBDMAP_INDEX_LEFT_ARROW;
		case KEY_RIGHT:		return KBDMAP_INDEX_RIGHT_ARROW;
		case KEY_END:		return KBDMAP_INDEX_END;
		case KEY_DOWN:		return KBDMAP_INDEX_DOWN_ARROW;
		case KEY_PAGEDOWN:	return KBDMAP_INDEX_PAGE_DOWN;
		case KEY_INSERT:	return KBDMAP_INDEX_INSERT;
		case KEY_DELETE:	return KBDMAP_INDEX_DELETE;
		case KEY_KPASTERISK:	return KBDMAP_INDEX_KP_ASTERISK;
		case KEY_KP7:		return KBDMAP_INDEX_KP_7;
		case KEY_KP8:		return KBDMAP_INDEX_KP_8;
		case KEY_KP9:		return KBDMAP_INDEX_KP_9;
		case KEY_KPMINUS:	return KBDMAP_INDEX_KP_MINUS;
		case KEY_KP4:		return KBDMAP_INDEX_KP_4;
		case KEY_KP5:		return KBDMAP_INDEX_KP_5;
		case KEY_KP6:		return KBDMAP_INDEX_KP_6;
		case KEY_KPPLUS:	return KBDMAP_INDEX_KP_PLUS;
		case KEY_KP1:		return KBDMAP_INDEX_KP_1;
		case KEY_KP2:		return KBDMAP_INDEX_KP_2;
		case KEY_KP3:		return KBDMAP_INDEX_KP_3;
		case KEY_KP0:		return KBDMAP_INDEX_KP_0;
		case KEY_KPDOT:		return KBDMAP_INDEX_KP_DECIMAL;
		case KEY_KPENTER:	return KBDMAP_INDEX_KP_ENTER;
		case KEY_KPSLASH:	return KBDMAP_INDEX_KP_SLASH;
		case KEY_KPJPCOMMA:	return KBDMAP_INDEX_KP_JPCOMMA;
		case KEY_KPCOMMA:	return KBDMAP_INDEX_KP_THOUSANDS;
		case KEY_KPEQUAL:	return KBDMAP_INDEX_KP_EQUALS;
		case KEY_KPPLUSMINUS:	return KBDMAP_INDEX_KP_SIGN;
		case KEY_KPLEFTPAREN:	return KBDMAP_INDEX_KP_LBRACKET;
		case KEY_KPRIGHTPAREN:	return KBDMAP_INDEX_KP_RBRACKET;
		case KEY_PAUSE:		return KBDMAP_INDEX_PAUSE;
		case KEY_SYSRQ:		return KBDMAP_INDEX_ATTENTION;
		case KEY_STOP:		return KBDMAP_INDEX_STOP;
		case KEY_AGAIN:		return KBDMAP_INDEX_AGAIN;
		case KEY_PROPS:		return KBDMAP_INDEX_PROPERTIES;
		case KEY_UNDO:		return KBDMAP_INDEX_UNDO;
		case KEY_REDO:		return KBDMAP_INDEX_REDO;
		case KEY_COPY:		return KBDMAP_INDEX_COPY;
		case KEY_OPEN:		return KBDMAP_INDEX_OPEN;
		case KEY_PASTE:		return KBDMAP_INDEX_PASTE;
		case KEY_FIND:		return KBDMAP_INDEX_FIND;
		case KEY_CUT:		return KBDMAP_INDEX_CUT;
		case KEY_HELP:		return KBDMAP_INDEX_HELP;
		case KEY_MUTE:		return KBDMAP_INDEX_MUTE;
		case KEY_VOLUMEDOWN:	return KBDMAP_INDEX_VOLUME_DOWN;
		case KEY_VOLUMEUP:	return KBDMAP_INDEX_VOLUME_UP;
		case KEY_CALC:		return KBDMAP_INDEX_CALCULATOR;
		case KEY_FILE:		return KBDMAP_INDEX_FILE_MANAGER;
		case KEY_WWW:		return KBDMAP_INDEX_WWW;
		case KEY_HOMEPAGE:	return KBDMAP_INDEX_HOME_PAGE;
		case KEY_REFRESH:	return KBDMAP_INDEX_REFRESH;
		case KEY_MAIL:		return KBDMAP_INDEX_MAIL;
		case KEY_BOOKMARKS:	return KBDMAP_INDEX_BOOKMARKS;
		case KEY_COMPUTER:	return KBDMAP_INDEX_COMPUTER;
		case KEY_BACK:		return KBDMAP_INDEX_BACK;
		case KEY_FORWARD:	return KBDMAP_INDEX_FORWARD;
		case KEY_SCREENLOCK:	return KBDMAP_INDEX_LOCK;
		case KEY_MSDOS:		return KBDMAP_INDEX_CLI;
		case KEY_NEXTSONG:	return KBDMAP_INDEX_NEXT_TRACK;
		case KEY_PREVIOUSSONG:	return KBDMAP_INDEX_PREV_TRACK;
		case KEY_PLAYPAUSE:	return KBDMAP_INDEX_PLAY_PAUSE;
		case KEY_STOPCD:	return KBDMAP_INDEX_STOP_PLAYING;
		case KEY_RECORD:	return KBDMAP_INDEX_RECORD;
		case KEY_REWIND:	return KBDMAP_INDEX_REWIND;
		case KEY_FASTFORWARD:	return KBDMAP_INDEX_FAST_FORWARD;
		case KEY_EJECTCD:	return KBDMAP_INDEX_EJECT;
		case KEY_NEW:		return KBDMAP_INDEX_NEW;
		case KEY_EXIT:		return KBDMAP_INDEX_EXIT;
		case KEY_POWER:		return KBDMAP_INDEX_POWER;
		case KEY_SLEEP:		return KBDMAP_INDEX_SLEEP;
		case KEY_WAKEUP:	return KBDMAP_INDEX_WAKE;
	}
	return 0xFFFF;
}

#endif
