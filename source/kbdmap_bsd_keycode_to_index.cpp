/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include "kbdmap.h"
#include "kbdmap_utils.h"

/// The atkbd device keycode maps to a row+column in the current keyboard map, which contains an action for that row+column.
uint16_t
bsd_keycode_to_keymap_index (
	const uint16_t k
) {
	switch (k) {
		default:	break;
		case 0x01:	return KBDMAP_INDEX_ESC;
		case 0x02:	return KBDMAP_INDEX_1;
		case 0x03:	return KBDMAP_INDEX_2;
		case 0x04:	return KBDMAP_INDEX_3;
		case 0x05:	return KBDMAP_INDEX_4;
		case 0x06:	return KBDMAP_INDEX_5;
		case 0x07:	return KBDMAP_INDEX_6;
		case 0x08:	return KBDMAP_INDEX_7;
		case 0x09:	return KBDMAP_INDEX_8;
		case 0x0A:	return KBDMAP_INDEX_9;
		case 0x0B:	return KBDMAP_INDEX_0;
		case 0x0C:	return KBDMAP_INDEX_MINUS;
		case 0x0D:	return KBDMAP_INDEX_EQUALS;
		case 0x0E:	return KBDMAP_INDEX_BACKSPACE;
		case 0x0F:	return KBDMAP_INDEX_TAB;
		case 0x10:	return KBDMAP_INDEX_Q;
		case 0x11:	return KBDMAP_INDEX_W;
		case 0x12:	return KBDMAP_INDEX_E;
		case 0x13:	return KBDMAP_INDEX_R;
		case 0x14:	return KBDMAP_INDEX_T;
		case 0x15:	return KBDMAP_INDEX_Y;
		case 0x16:	return KBDMAP_INDEX_U;
		case 0x17:	return KBDMAP_INDEX_I;
		case 0x18:	return KBDMAP_INDEX_O;
		case 0x19:	return KBDMAP_INDEX_P;
		case 0x1a:	return KBDMAP_INDEX_LEFTBRACE;
		case 0x1b:	return KBDMAP_INDEX_RIGHTBRACE;
		case 0x1c:	return KBDMAP_INDEX_RETURN;
		case 0x1d:	return KBDMAP_INDEX_CONTROL1;
		case 0x1e:	return KBDMAP_INDEX_A;
		case 0x1f:	return KBDMAP_INDEX_S;
		case 0x20:	return KBDMAP_INDEX_D;
		case 0x21:	return KBDMAP_INDEX_F;
		case 0x22:	return KBDMAP_INDEX_G;
		case 0x23:	return KBDMAP_INDEX_H;
		case 0x24:	return KBDMAP_INDEX_J;
		case 0x25:	return KBDMAP_INDEX_K;
		case 0x26:	return KBDMAP_INDEX_L;
		case 0x27:	return KBDMAP_INDEX_SEMICOLON;
		case 0x28:	return KBDMAP_INDEX_APOSTROPHE;
		case 0x29:	return KBDMAP_INDEX_GRAVE;
		case 0x2a:	return KBDMAP_INDEX_SHIFT1;
		case 0x2b:	return KBDMAP_INDEX_EUROPE1;
		case 0x2c:	return KBDMAP_INDEX_Z;
		case 0x2d:	return KBDMAP_INDEX_X;
		case 0x2e:	return KBDMAP_INDEX_C;
		case 0x2f:	return KBDMAP_INDEX_V;
		case 0x30:	return KBDMAP_INDEX_B;
		case 0x31:	return KBDMAP_INDEX_N;
		case 0x32:	return KBDMAP_INDEX_M;
		case 0x33:	return KBDMAP_INDEX_COMMA;
		case 0x34:	return KBDMAP_INDEX_DOT;
		case 0x35:	return KBDMAP_INDEX_SLASH1;
		case 0x36:	return KBDMAP_INDEX_SHIFT2;
		case 0x37:	return KBDMAP_INDEX_KP_ASTERISK;
		case 0x38:	return KBDMAP_INDEX_ALT;
		case 0x39:	return KBDMAP_INDEX_SPACE;
		case 0x3a:	return KBDMAP_INDEX_CAPSLOCK;
		case 0x3b:	return KBDMAP_INDEX_F1;
		case 0x3c:	return KBDMAP_INDEX_F2;
		case 0x3d:	return KBDMAP_INDEX_F3;
		case 0x3e:	return KBDMAP_INDEX_F4;
		case 0x3f:	return KBDMAP_INDEX_F5;
		case 0x40:	return KBDMAP_INDEX_F6;
		case 0x41:	return KBDMAP_INDEX_F7;
		case 0x42:	return KBDMAP_INDEX_F8;
		case 0x43:	return KBDMAP_INDEX_F9;
		case 0x44:	return KBDMAP_INDEX_F10;
		case 0x45:	return KBDMAP_INDEX_NUMLOCK;
		case 0x46:	return KBDMAP_INDEX_SCROLLLOCK;
		case 0x47:	return KBDMAP_INDEX_KP_7;
		case 0x48:	return KBDMAP_INDEX_KP_8;
		case 0x49:	return KBDMAP_INDEX_KP_9;
		case 0x4a:	return KBDMAP_INDEX_KP_MINUS;
		case 0x4b:	return KBDMAP_INDEX_KP_4;
		case 0x4c:	return KBDMAP_INDEX_KP_5;
		case 0x4d:	return KBDMAP_INDEX_KP_6;
		case 0x4e:	return KBDMAP_INDEX_KP_PLUS;
		case 0x4f:	return KBDMAP_INDEX_KP_1;
		case 0x50:	return KBDMAP_INDEX_KP_2;
		case 0x51:	return KBDMAP_INDEX_KP_3;
		case 0x52:	return KBDMAP_INDEX_KP_0;
		case 0x53:	return KBDMAP_INDEX_KP_DECIMAL;
		case 0x54:	return KBDMAP_INDEX_ATTENTION;		// Alt-PrtScn
		case 0x56:	return KBDMAP_INDEX_EUROPE2;
		case 0x57:	return KBDMAP_INDEX_F11;
		case 0x58:	return KBDMAP_INDEX_F12;
		case 0x59:	return KBDMAP_INDEX_KP_ENTER;
		case 0x5A:	return KBDMAP_INDEX_CONTROL2;
		case 0x5B:	return KBDMAP_INDEX_KP_SLASH;
		case 0x5C:	return KBDMAP_INDEX_PRINT_SCREEN;	// Print Screen/Shift-Numpad-Asterisk
		case 0x5D:	return KBDMAP_INDEX_OPTION;
		case 0x5E:	return KBDMAP_INDEX_HOME;
		case 0x5F:	return KBDMAP_INDEX_UP_ARROW;
		case 0x60:	return KBDMAP_INDEX_PAGE_UP;
		case 0x61:	return KBDMAP_INDEX_LEFT_ARROW;
		case 0x62:	return KBDMAP_INDEX_RIGHT_ARROW;
		case 0x63:	return KBDMAP_INDEX_END;
		case 0x64:	return KBDMAP_INDEX_DOWN_ARROW;
		case 0x65:	return KBDMAP_INDEX_PAGE_DOWN;
		case 0x66:	return KBDMAP_INDEX_INSERT;
		case 0x67:	return KBDMAP_INDEX_DELETE;
		case 0x68:	return KBDMAP_INDEX_PAUSE;		// Pause/Ctrl-NumLock
		case 0x69:	return KBDMAP_INDEX_SUPER1;
		case 0x6A:	return KBDMAP_INDEX_SUPER2;
		case 0x6B:	return KBDMAP_INDEX_APPLICATION;
		case 0x6C:	return KBDMAP_INDEX_BREAK;		// Ctrl-Pause/Ctrl-ScrollLock
		case 0x6D:	return KBDMAP_INDEX_POWER;
		case 0x6E:	return KBDMAP_INDEX_SLEEP;
		case 0x6F:	return KBDMAP_INDEX_WAKE;
		// FreeBSD's atkbdc driver does not generate codes beyond this point.
		case 0x70:	return KBDMAP_INDEX_KATAKANA_HIRAGANA;	// Intl2
		case 0x73:	return KBDMAP_INDEX_SLASH2;		// Intl1 (per FreeBSD jp and br keymaps)
		case 0x77:	return KBDMAP_INDEX_HIRAGANA;		// Lang4
		case 0x78:	return KBDMAP_INDEX_KATAKANA;		// Lang3
		case 0x79:	return KBDMAP_INDEX_HENKAN;		// Intl4 (per FreeBSD jp keymap)
		case 0x7B:	return KBDMAP_INDEX_MUHENKAN;		// Intl5 (per FreeBSD jp keymap)
		case 0x7D:	return KBDMAP_INDEX_YEN;		// Intl3 (per FreeBSD jp keymap)
		case 0x7E:	return KBDMAP_INDEX_KP_THOUSANDS;	// Brazilian KP . (per FreeBSD br keymap)
	}
	return 0xFFFF;
}
