/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(__LINUX__) && !defined(__linux__)

#if defined(__OpenBSD__)
#include <dev/usb/usb.h>
#endif
#include <dev/usb/usbhid.h>
#include "kbdmap.h"
#include "kbdmap_utils.h"

#if defined(HUG_APPLE_EJECT)
#define HUG_LAST_SYSTEM_KEY	HUG_APPLE_EJECT
#else
#define HUG_LAST_SYSTEM_KEY	HUG_SYSTEM_MENU_DOWN
#endif

/// The USB HID keycode maps to a row+column in the current keyboard map, which contains an action for that row+column.
uint16_t
usb_ident_to_keymap_index (
	const uint32_t ident
) {
	if ((HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_SYSTEM_CONTROL) <= ident && HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_LAST_SYSTEM_KEY) >= ident)) 
		switch (ident - HID_USAGE2(HUP_GENERIC_DESKTOP, 0)) { 
			default:	break;
			case 0x81:	return KBDMAP_INDEX_POWER;
			case 0x82:	return KBDMAP_INDEX_SLEEP;
			case 0x83:	return KBDMAP_INDEX_WAKE;
			case 0xA4:	return KBDMAP_INDEX_DEBUG;
			case 0xA8:	break;				// return KBDMAP_INDEX_HIBERNATE;
		} 
	else
	if ((HID_USAGE2(HUP_KEYBOARD, 0) <= ident && HID_USAGE2(HUP_KEYBOARD, 0xFFFF) >= ident))
		switch (ident - HID_USAGE2(HUP_KEYBOARD, 0)) {
			default:	break;
			case 0x04:	return KBDMAP_INDEX_A;		// C01
			case 0x05:	return KBDMAP_INDEX_B;		// B05
			case 0x06:	return KBDMAP_INDEX_C;		// B03
			case 0x07:	return KBDMAP_INDEX_D;		// C03
			case 0x08:	return KBDMAP_INDEX_E;		// D03
			case 0x09:	return KBDMAP_INDEX_F;		// C04
			case 0x0A:	return KBDMAP_INDEX_G;		// C05
			case 0x0B:	return KBDMAP_INDEX_H;		// C06
			case 0x0C:	return KBDMAP_INDEX_I;		// D08
			case 0x0D:	return KBDMAP_INDEX_J;		// C07
			case 0x0E:	return KBDMAP_INDEX_K;		// C08
			case 0x0F:	return KBDMAP_INDEX_L;		// C09
			case 0x10:	return KBDMAP_INDEX_M;		// B07
			case 0x11:	return KBDMAP_INDEX_N;		// B06
			case 0x12:	return KBDMAP_INDEX_O;		// D09
			case 0x13:	return KBDMAP_INDEX_P;		// D10
			case 0x14:	return KBDMAP_INDEX_Q;		// D01
			case 0x15:	return KBDMAP_INDEX_R;		// D04
			case 0x16:	return KBDMAP_INDEX_S;		// C02
			case 0x17:	return KBDMAP_INDEX_T;		// D05
			case 0x18:	return KBDMAP_INDEX_U;		// D07
			case 0x19:	return KBDMAP_INDEX_V;		// B04
			case 0x1A:	return KBDMAP_INDEX_W;		// D02
			case 0x1B:	return KBDMAP_INDEX_X;		// B02
			case 0x1C:	return KBDMAP_INDEX_Y;		// D06
			case 0x1D:	return KBDMAP_INDEX_Z;		// B01
			case 0x1E:	return KBDMAP_INDEX_1;		// E01
			case 0x1F:	return KBDMAP_INDEX_2;		// E02
			case 0x20:	return KBDMAP_INDEX_3;		// E03
			case 0x21:	return KBDMAP_INDEX_4;		// E04
			case 0x22:	return KBDMAP_INDEX_5;		// E05
			case 0x23:	return KBDMAP_INDEX_6;		// E06
			case 0x24:	return KBDMAP_INDEX_7;		// E07
			case 0x25:	return KBDMAP_INDEX_8;		// E08
			case 0x26:	return KBDMAP_INDEX_9;		// E09
			case 0x27:	return KBDMAP_INDEX_0;		// E10
			case 0x28:	return KBDMAP_INDEX_RETURN;	// C13
			case 0x29:	return KBDMAP_INDEX_ESC;
			case 0x2A:	return KBDMAP_INDEX_BACKSPACE;	// E13
			case 0x2B:	return KBDMAP_INDEX_TAB;	// D00
			case 0x2C:	return KBDMAP_INDEX_SPACE;	// A03
			case 0x2D:	return KBDMAP_INDEX_MINUS;	// E11
			case 0x2E:	return KBDMAP_INDEX_EQUALS;	// E12
			case 0x2F:	return KBDMAP_INDEX_LEFTBRACE;	// D11
			case 0x30:	return KBDMAP_INDEX_RIGHTBRACE;	// D12
			case 0x31:	return KBDMAP_INDEX_EUROPE1;	// The idea that D13 with "\|" is a distinct key is a USB HID specification bug; on real keyboards C12/D13 is a single key per ISO/IEC 9995-3:2010.
			case 0x32:	return KBDMAP_INDEX_EUROPE1;	// C12 (105/107/109) or D13 (104) or E13 (106), EUROPE1 (despite being present on U.S. keyboards too)
			case 0x33:	return KBDMAP_INDEX_SEMICOLON;	// C10
			case 0x34:	return KBDMAP_INDEX_APOSTROPHE;	// C11
			case 0x35:	return KBDMAP_INDEX_GRAVE;	// E00
			case 0x36:	return KBDMAP_INDEX_COMMA;	// B08
			case 0x37:	return KBDMAP_INDEX_DOT;	// B09
			case 0x38:	return KBDMAP_INDEX_SLASH1;	// B10
			case 0x39:	return KBDMAP_INDEX_CAPSLOCK;	// C00
			case 0x3A:	return KBDMAP_INDEX_F1;
			case 0x3B:	return KBDMAP_INDEX_F2;
			case 0x3C:	return KBDMAP_INDEX_F3;
			case 0x3D:	return KBDMAP_INDEX_F4;
			case 0x3E:	return KBDMAP_INDEX_F5;
			case 0x3F:	return KBDMAP_INDEX_F6;
			case 0x40:	return KBDMAP_INDEX_F7;
			case 0x41:	return KBDMAP_INDEX_F8;
			case 0x42:	return KBDMAP_INDEX_F9;
			case 0x43:	return KBDMAP_INDEX_F10;
			case 0x44:	return KBDMAP_INDEX_F11;
			case 0x45:	return KBDMAP_INDEX_F12;
			case 0x46:	return KBDMAP_INDEX_PRINT_SCREEN;
			case 0x47:	return KBDMAP_INDEX_SCROLLLOCK;
			case 0x48:	return KBDMAP_INDEX_PAUSE;
			case 0x49:	return KBDMAP_INDEX_INSERT;	// E30
			case 0x4A:	return KBDMAP_INDEX_HOME;	// E31
			case 0x4B:	return KBDMAP_INDEX_PAGE_UP;	// E32
			case 0x4C:	return KBDMAP_INDEX_DELETE;	// D30
			case 0x4D:	return KBDMAP_INDEX_END;	// D31
			case 0x4E:	return KBDMAP_INDEX_PAGE_DOWN;	// D32
			case 0x4F:	return KBDMAP_INDEX_RIGHT_ARROW;// A30
			case 0x50:	return KBDMAP_INDEX_LEFT_ARROW;	// A32
			case 0x51:	return KBDMAP_INDEX_DOWN_ARROW;	// A31
			case 0x52:	return KBDMAP_INDEX_UP_ARROW;	// B31
			case 0x53:	return KBDMAP_INDEX_NUMLOCK;	// E51
			case 0x54:	return KBDMAP_INDEX_KP_SLASH;	// E52
			case 0x55:	return KBDMAP_INDEX_KP_ASTERISK;// E53
			case 0x56:	return KBDMAP_INDEX_KP_MINUS;	// E54
			case 0x57:	return KBDMAP_INDEX_KP_PLUS;	// D54
			case 0x58:	return KBDMAP_INDEX_KP_ENTER;	// A54
			case 0x59:	return KBDMAP_INDEX_KP_1;	// B51
			case 0x5A:	return KBDMAP_INDEX_KP_2;	// B52
			case 0x5B:	return KBDMAP_INDEX_KP_3;	// B53
			case 0x5C:	return KBDMAP_INDEX_KP_4;	// C51
			case 0x5D:	return KBDMAP_INDEX_KP_5;	// C52
			case 0x5E:	return KBDMAP_INDEX_KP_6;	// C53
			case 0x5F:	return KBDMAP_INDEX_KP_7;	// D51
			case 0x60:	return KBDMAP_INDEX_KP_8;	// D52
			case 0x61:	return KBDMAP_INDEX_KP_9;	// D53
			case 0x62:	return KBDMAP_INDEX_KP_0;	// A51
			case 0x63:	return KBDMAP_INDEX_KP_DECIMAL;	// A53
			case 0x64:	return KBDMAP_INDEX_EUROPE2;	// B00
			case 0x65:	return KBDMAP_INDEX_APPLICATION;// A11
			case 0x66:	return KBDMAP_INDEX_POWER;
			case 0x67:	return KBDMAP_INDEX_KP_EQUALS;	// E52 (Apple)
			case 0x68:	return KBDMAP_INDEX_F13;
			case 0x69:	return KBDMAP_INDEX_F14;
			case 0x6A:	return KBDMAP_INDEX_F15;
			case 0x6B:	return KBDMAP_INDEX_F16;
			case 0x6C:	return KBDMAP_INDEX_F17;
			case 0x6D:	return KBDMAP_INDEX_F18;
			case 0x6E:	return KBDMAP_INDEX_F19;
			case 0x6F:	return KBDMAP_INDEX_F20;
			case 0x70:	return KBDMAP_INDEX_F21;
			case 0x71:	return KBDMAP_INDEX_F22;
			case 0x72:	return KBDMAP_INDEX_F23;
			case 0x73:	return KBDMAP_INDEX_F24;
			case 0x74:	return KBDMAP_INDEX_EXECUTE;
			case 0x75:	return KBDMAP_INDEX_HELP;
			case 0x76:	return KBDMAP_INDEX_MENU;
			case 0x77:	return KBDMAP_INDEX_SELECT;
			case 0x78:	return KBDMAP_INDEX_STOP;
			case 0x79:	return KBDMAP_INDEX_AGAIN;
			case 0x7A:	return KBDMAP_INDEX_UNDO;
			case 0x7B:	return KBDMAP_INDEX_CUT;
			case 0x7C:	return KBDMAP_INDEX_COPY;
			case 0x7D:	return KBDMAP_INDEX_PASTE;
			case 0x7E:	return KBDMAP_INDEX_FIND;
			case 0x7F:	return KBDMAP_INDEX_MUTE;
			case 0x80:	return KBDMAP_INDEX_VOLUME_UP;
			case 0x81:	return KBDMAP_INDEX_VOLUME_DOWN;
			case 0x85:	return KBDMAP_INDEX_KP_THOUSANDS;// C54 (107) or A52 (Apple)
			case 0x86:	return KBDMAP_INDEX_KP_AS400_EQUALS;	// Keypad Equal Sign for AS/400
			case 0x87:	return KBDMAP_INDEX_SLASH2;	// B11, International1
			case 0x88:	return KBDMAP_INDEX_KATAKANA_HIRAGANA;	// A07, International2
			case 0x89:	return KBDMAP_INDEX_YEN;	// E13, International3
			case 0x8A:	return KBDMAP_INDEX_HENKAN;	// A03, International4
			case 0x8B:	return KBDMAP_INDEX_MUHENKAN;	// A06, International5
			case 0x8C:	break;				// return KBDMAP_INDEX_INTERNATIONAL6;
			case 0x8D:	break;				// return KBDMAP_INDEX_INTERNATIONAL7;
			case 0x8E:	break;				// return KBDMAP_INDEX_INTERNATIONAL8;
			case 0x8F:	break;				// return KBDMAP_INDEX_INTERNATIONAL9;
			case 0x90:	return KBDMAP_INDEX_HAN_YEONG;	// A02, Lang1
			case 0x91:	return KBDMAP_INDEX_HANJA;	// A07, Lang2
			case 0x92:	return KBDMAP_INDEX_KATAKANA;	// LANG3, not a PC keyboard key
			case 0x93:	return KBDMAP_INDEX_HIRAGANA;	// LANG4, not a PC keyboard key
			case 0x94:	return KBDMAP_INDEX_ZENKAKU_HANKAKU;	// LANG5, not a PC keyboard key
			case 0x95:	break;				// return KBDMAP_INDEX_LANG6;
			case 0x96:	break;				// return KBDMAP_INDEX_LANG7;
			case 0x97:	break;				// return KBDMAP_INDEX_LANG8;
			case 0x98:	break;				// return KBDMAP_INDEX_LANG9;
			case 0x99:	return KBDMAP_INDEX_ALTERNATE_ERASE;
			case 0x9A:	return KBDMAP_INDEX_ATTENTION;
			case 0x9B:	return KBDMAP_INDEX_CANCEL;
			case 0x9C:	return KBDMAP_INDEX_CLEAR;
			case 0x9D:	return KBDMAP_INDEX_PRIOR;
			case 0x9E:	return KBDMAP_INDEX_APP_RETURN;
			case 0x9F:	return KBDMAP_INDEX_SEPARATOR;
			case 0xA0:	return KBDMAP_INDEX_OUT;
			case 0xA1:	return KBDMAP_INDEX_OPER;
			case 0xA2:	return KBDMAP_INDEX_CLEAR_OR_AGAIN;	// Clear/Again
			case 0xA3:	return KBDMAP_INDEX_PROPERTIES;	// CrSel/Props
			case 0xA4:	return KBDMAP_INDEX_EXSEL;
			case 0xE0:	return KBDMAP_INDEX_CONTROL1;	// A99
			case 0xE1:	return KBDMAP_INDEX_SHIFT1;	// B99
			case 0xE2:	return KBDMAP_INDEX_ALT;	// A02
			case 0xE3:	return KBDMAP_INDEX_SUPER1;	// A01
			case 0xE4:	return KBDMAP_INDEX_CONTROL2;	// A12
			case 0xE5:	return KBDMAP_INDEX_SHIFT2;	// B12
			case 0xE6:	return KBDMAP_INDEX_OPTION;	// A08
			case 0xE7:	return KBDMAP_INDEX_SUPER2;	// A10
		} 
	else
	if ((HID_USAGE2(HUP_CONSUMER, 0) <= ident && HID_USAGE2(HUP_CONSUMER, 0xFFFF) >= ident))
		switch (ident - HID_USAGE2(HUP_CONSUMER, 0)) {
			default:	break;
			case 0x00B0:	break;	// return KBDMAP_INDEX_PLAY;
			case 0x00B1:	return KBDMAP_INDEX_PAUSE;
			case 0x00B2:	return KBDMAP_INDEX_RECORD;
			case 0x00B3:	return KBDMAP_INDEX_FAST_FORWARD;
			case 0x00B4:	return KBDMAP_INDEX_REWIND;
			case 0x00B5:	return KBDMAP_INDEX_NEXT_TRACK;
			case 0x00B6:	return KBDMAP_INDEX_PREV_TRACK;
			case 0x00B7:	return KBDMAP_INDEX_STOP_PLAYING;
			case 0x00B8:	return KBDMAP_INDEX_EJECT;
			case 0x00B9:	break;	// return KBDMAP_INDEX_RANDOM_PLAY;
			case 0x00BC:	break;	// return KBDMAP_INDEX_REPEAT;
			case 0x00BE:	break;	// return KBDMAP_INDEX_TRACK_NORMAL;
			case 0x00C0:	break;	// return KBDMAP_INDEX_FRAME_FORWARD;
			case 0x00C1:	break;	// return KBDMAP_INDEX_FRAME_BACK;
			case 0x00CC:	break;	// return KBDMAP_INDEX_STOP_EJECT;
			case 0x00CD:	return KBDMAP_INDEX_PLAY_PAUSE;
			case 0x00CE:	break;	// return KBDMAP_INDEX_PLAY_SKIP;
			case 0x00E2:	break;	// return KBDMAP_INDEX_MUTE_PLAYER;
			case 0x00E5:	break;	// return KBDMAP_INDEX_BASS_BOOST;
			case 0x00E7:	break;	// return KBDMAP_INDEX_LOUDNESS;
			case 0x00E9:	return KBDMAP_INDEX_VOLUME_UP;
			case 0x00EA:	return KBDMAP_INDEX_VOLUME_DOWN;
			case 0x0150:	break;	// return KBDMAP_INDEX_BALANCE_RIGHT;
			case 0x0151:	break;	// return KBDMAP_INDEX_BALANCE_LEFT;
			case 0x0152:	break;	// return KBDMAP_INDEX_BASS_UP;
			case 0x0153:	break;	// return KBDMAP_INDEX_BASS_DOWN;
			case 0x0154:	break;	// return KBDMAP_INDEX_TREBLE_UP;
			case 0x0155:	break;	// return KBDMAP_INDEX_TREBLE_DOWN;
			case 0x0184:	break;	// return KBDMAP_INDEX_WORDPROCESSOR;
			case 0x0185:	break;	// return KBDMAP_INDEX_TEXT_EDITOR;
			case 0x0186:	break;	// return KBDMAP_INDEX_SPREADSHEET;
			case 0x0187:	break;	// return KBDMAP_INDEX_GRAPHICS_EDITOR;
			case 0x0188:	break;	// return KBDMAP_INDEX_PRESENTATION_APP;
			case 0x0189:	break;	// return KBDMAP_INDEX_DATABASE;
			case 0x018A:	return KBDMAP_INDEX_MAIL;
			case 0x018B:	break;	// return KBDMAP_INDEX_NEWS;
			case 0x018C:	break;	// return KBDMAP_INDEX_VOICEMAIL;
			case 0x018D:	break;	// return KBDMAP_INDEX_ADDRESS_BOOK;
			case 0x018E:	break;	// return KBDMAP_INDEX_CALENDAR;
			case 0x018F:	break;	// return KBDMAP_INDEX_PROJECT_MANAGER;
			case 0x0190:	break;	// return KBDMAP_INDEX_TIMECARD;
			case 0x0191:	break;	// return KBDMAP_INDEX_CHECKBOOK;
			case 0x0192:	return KBDMAP_INDEX_CALCULATOR;
			case 0x0194:	return KBDMAP_INDEX_COMPUTER;
			case 0x0195:	break;	// return KBDMAP_INDEX_NETWORK;
			case 0x0196:	return KBDMAP_INDEX_WWW;
			case 0x0198:	break;	// return KBDMAP_INDEX_CONFERENCE;
			case 0x0199:	break;	// return KBDMAP_INDEX_CHAT;
			case 0x019A:	break;	// return KBDMAP_INDEX_DIALLER;
			case 0x019B:	break;	// return KBDMAP_INDEX_LOGON;
			case 0x019C:	break;	// return KBDMAP_INDEX_LOGOFF;
			case 0x019E:	return KBDMAP_INDEX_LOCK;
			case 0x019F:	break;	// return KBDMAP_INDEX_CONTROL_PANEL;
			case 0x01A0:	return KBDMAP_INDEX_CLI;
			case 0x01A1:	break;	// return KBDMAP_INDEX_TASK_MANAGER;
			case 0x01A2:	break;	// return KBDMAP_INDEX_SELECT_TASK;
			case 0x01A3:	break;	// return KBDMAP_INDEX_NEXT_TASK;
			case 0x01A4:	break;	// return KBDMAP_INDEX_PREVIOUS_TASK;
			case 0x01A5:	break;	// return KBDMAP_INDEX_HALT_TASK;
			case 0x01A6:	break;	// return KBDMAP_INDEX_HELP_CENTRE;
			case 0x01A7:	break;	// return KBDMAP_INDEX_DOCUMENTS;
			case 0x01AA:	break;	// return KBDMAP_INDEX_DESKTOP;
			case 0x01B3:	break;	// return KBDMAP_INDEX_CLOCK;
			case 0x01B4:	return KBDMAP_INDEX_FILE_MANAGER;
			case 0x01BC:	break;	// return KBDMAP_INDEX_INSTANT_MESSAGING;
			case 0x01C1:	break;	// return KBDMAP_INDEX_WWW_SHOPPING;
			case 0x0201:	return KBDMAP_INDEX_NEW;
			case 0x0202:	return KBDMAP_INDEX_OPEN;
			case 0x0203:	break;	// return KBDMAP_INDEX_CLOSE;
			case 0x0204:	return KBDMAP_INDEX_EXIT;
			case 0x0205:	break;	// return KBDMAP_INDEX_MAXIMIZE;
			case 0x0206:	break;	// return KBDMAP_INDEX_MINIMIZE;
			case 0x0207:	break;	// return KBDMAP_INDEX_SAVE;
			case 0x0208:	break;	// return KBDMAP_INDEX_PRINT;
			case 0x0209:	return KBDMAP_INDEX_PROPERTIES;
			case 0x021A:	return KBDMAP_INDEX_UNDO;
			case 0x021B:	return KBDMAP_INDEX_COPY;
			case 0x021C:	return KBDMAP_INDEX_CUT;
			case 0x021D:	return KBDMAP_INDEX_PASTE;
			case 0x021E:	break;	// return KBDMAP_INDEX_SELECT_ALL;
			case 0x021F:	return KBDMAP_INDEX_FIND;
			case 0x0220:	break;	// return KBDMAP_INDEX_FIND_AND_REPLACE;
			case 0x0221:	break;	// return KBDMAP_INDEX_SEARCH;
			case 0x0223:	return KBDMAP_INDEX_HOME;
			case 0x0224:	return KBDMAP_INDEX_BACK;
			case 0x0225:	return KBDMAP_INDEX_FORWARD;
			case 0x0226:	break;	// return KBDMAP_INDEX_STOP_LOADING;
			case 0x0227:	return KBDMAP_INDEX_REFRESH;
			case 0x0228:	break;	// return KBDMAP_INDEX_PREVIOUS_LINK;
			case 0x0229:	break;	// return KBDMAP_INDEX_NEXT_LINK;
			case 0x022A:	return KBDMAP_INDEX_BOOKMARKS;
			case 0x022B:	break;	// return KBDMAP_INDEX_HISTORY;
		} 
	else
		/* This is not a system, keyboard, or consumer key. */ ;

	return 0xFFFF;
}

#endif
