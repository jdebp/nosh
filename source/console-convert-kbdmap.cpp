/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define __STDC_FORMAT_MACROS
#include <vector>
#include <iostream>
#include <fstream>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdint.h>
#include <cerrno>
#include <unistd.h>
#if defined(__LINUX__) || defined(__linux__)
#include <endian.h>
#else
#include <sys/endian.h>
#endif
#include "utils.h"
#include "fdutils.h"
#include "popt.h"
#include "kbdmap.h"
#include "kbdmap_utils.h"
#include "InputMessage.h"

/* Keyboard map entry shorthands ********************************************
// **************************************************************************
*/

#define SIMPLE(x) { 'p', { (x),(x),(x),(x),(x),(x),(x),(x),(x),(x),(x),(x),(x),(x),(x),(x) } }
#define SHIFTABLE(m,n,o,p,q,r,s,t) { 's', { (m),(n),NOOP(0),NOOP(0),(o),(p),NOOP(0),NOOP(0),(q),(r),NOOP(0),NOOP(0),(s),(t),NOOP(0),NOOP(0) } }
#define SHIFTABLE_CONTROL(m,n,o,p,c,q,r,s,t) { 's', { (m),(n),(c),(c),(o),(p),(c),(c),(q),(r),(c),(c),(s),(t),(c),(c) } }
#define CAPSABLE_CONTROL(m,n,o,p,c,q,r,s,t) { 'c', { (m),(n),(c),(c),(o),(p),(c),(c),(q),(r),(c),(c),(s),(t),(c),(c) } }
#define NUMABLE(m,n) { 'n', { (m),(n),NOOP(0),NOOP(0),NOOP(0),NOOP(0),NOOP(0),NOOP(0),(m),(n),NOOP(0),NOOP(0),NOOP(0),NOOP(0),NOOP(0),NOOP(0) } }
#if !defined(__LINUX__) && !defined(__linux__)
#define FUNCABLE1(m) { 'f', { FUNC(m),FUNC(m),FUNC(m),FUNC(m),SCRN((m)-1U),SCRN((m)-1U),SCRN((m)-1U),SCRN((m)-1U),FUNC(m),FUNC(m),FUNC(m),FUNC(m),SCRN((m)-1U),SCRN((m)-1U),SCRN((m)-1U),SCRN((m)-1U) } }
#define FUNCABLE2(m,e) { 'f', { EXTE(e),FUNC(m),FUNC(m),FUNC(m),SCRN((m)-1U),SCRN((m)-1U),SCRN((m)-1U),SCRN((m)-1U),EXTE(e),FUNC(m),FUNC(m),FUNC(m),SCRN((m)-1U),SCRN((m)-1U),SCRN((m)-1U),SCRN((m)-1U) } }
#endif
#if defined(__LINUX__) || defined(__linux__)
#define FUNKABLE1(m) { 'f', { FUNK(m),FUNK((m)+12U),FUNK((m)+24U),FUNK((m)+36U),SCRN((m)-1U),SCRN((m)+12U-1U),SCRN((m)+24U-1U),SCRN((m)+36U-1U),FUNK(m),FUNK((m)+12U),FUNK((m)+24U),FUNK((m)+36U),SCRN((m)-1U),SCRN((m)+12U-1U),SCRN((m)+24U-1U),SCRN((m)+36U-1U) } }
#define FUNKABLE2(m,e) { 'f', { EXTE(e),FUNK((m)+12U),FUNK((m)+24U),FUNK((m)+36U),SCRN((m)-1U),SCRN((m)+12U-1U),SCRN((m)+24U-1U),SCRN((m)+36U-1U),EXTE(e),FUNK((m)+12U),FUNK((m)+24U),FUNK((m)+36U),SCRN((m)-1U),SCRN((m)+12U-1U),SCRN((m)+24U-1U),SCRN((m)+36U-1U) } }
#endif

#define	NOOP(x)	 (((x) & 0x00FFFFFF) << 0U)
#define UCSA(x) ((((x) & 0x00FFFFFF) << 0U) | KBDMAP_ACTION_UCS3)
#define MMNT(x) ((((x) & 0x0000FFFF) << 8U) | KBDMAP_ACTION_MODIFIER | KBDMAP_MODIFIER_CMD_MOMENTARY)
#define LTCH(x) ((((x) & 0x0000FFFF) << 8U) | KBDMAP_ACTION_MODIFIER | KBDMAP_MODIFIER_CMD_LATCH)
#define LOCK(x) ((((x) & 0x0000FFFF) << 8U) | KBDMAP_ACTION_MODIFIER | KBDMAP_MODIFIER_CMD_LOCK)
#define IMTH(x) ((((x) & 0x0000FFFF) << 8U) | 0x06000000)
#define SCRN(x) ((((x) & 0x0000FFFF) << 8U) | KBDMAP_ACTION_SCREEN)
#define SYST(x) ((((x) & 0x0000FFFF) << 8U) | KBDMAP_ACTION_SYSTEM)
#define CONS(x) ((((x) & 0x0000FFFF) << 8U) | KBDMAP_ACTION_CONSUMER)
#define EXTE(x) ((((x) & 0x0000FFFF) << 8U) | KBDMAP_ACTION_EXTENDED)
#if !defined(__LINUX__) && !defined(__linux__)
#define FUNC(x) ((((x) & 0x0000FFFF) << 8U) | KBDMAP_ACTION_FUNCTION)
#endif
#define FUNK(x) ((((x) & 0x0000FFFF) << 8U) | KBDMAP_ACTION_FUNCTION1)

/* Base keyboard ************************************************************
// **************************************************************************
*/

static 
const kbdmap_entry
English_international_keyboard[KBDMAP_ROWS][KBDMAP_COLS] =
{
	// 00: ISO 9995 "E" row
	{
		SIMPLE(NOOP(0xFEFF)),
		SHIFTABLE(UCSA('1'),UCSA('!'),UCSA(L'¡'),UCSA(L'¹'),UCSA(0x00B9),UCSA(0x00A1),UCSA(0x02B9),NOOP(9995)),
		SHIFTABLE_CONTROL(UCSA('2'),UCSA('@'),UCSA(L'²'),UCSA(L'⅔'),UCSA(0x00),UCSA(0x00B2),UCSA(0x00A4),UCSA(0x02BA),NOOP(9995)),
		SHIFTABLE(UCSA('3'),UCSA('#'),UCSA(L'³'),UCSA(L'⅜'),UCSA(0x00B3),UCSA(0x00A3),UCSA(0x02BF),NOOP(9995)),
		SHIFTABLE(UCSA('4'),UCSA('$'),UCSA(L'¤'),UCSA(L'£'),UCSA(0x00BC),UCSA(0x20AC),UCSA(0x02BE),NOOP(9995)),
		SHIFTABLE(UCSA('5'),UCSA('%'),UCSA(L'€'),UCSA(L'‰'),UCSA(0x00BD),UCSA(0x2191),UCSA(0x02C1),NOOP(9995)),
		SHIFTABLE_CONTROL(UCSA('6'),UCSA('^'),UCSA(L'¼'),UCSA(L'⅙'),UCSA(0x1E),UCSA(0x00BE),UCSA(0x2193),UCSA(0x02C0),NOOP(9995)),
		SHIFTABLE(UCSA('7'),UCSA('&'),UCSA(L'½'),UCSA(L'⅞'),UCSA(0x215B),UCSA(0x2190),UCSA(0x007B),NOOP(9995)),
		SHIFTABLE(UCSA('8'),UCSA('*'),UCSA(L'¾'),UCSA(L'⅛'),UCSA(0x215C),UCSA(0x2192),UCSA(0x007D),NOOP(9995)),
		SHIFTABLE(UCSA('9'),UCSA('('),UCSA(L'‘'),UCSA(L'⅟'),UCSA(0x215D),UCSA(0x00B1),UCSA(0x005B),NOOP(9995)),
		SHIFTABLE(UCSA('0'),UCSA(')'),UCSA(L'’'),UCSA(L'⁄'),UCSA(0x215E),UCSA(0x2122),UCSA(0x005D),NOOP(9995)),
		SHIFTABLE_CONTROL(UCSA('-'),UCSA('_'),UCSA(0x2013),UCSA(0x2014),UCSA(0x1F),UCSA(0x005C),UCSA(0x00BF),UCSA(0x02BB),NOOP(9995)),
		SHIFTABLE(UCSA('='),UCSA('+'),UCSA(L'×'),UCSA(L'÷'),UCSA(0x0327),UCSA(0x0328),UCSA(0x00AC),NOOP(9995)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(EXTE(EXTENDED_KEY_BACKSPACE)),
	},
	// 01: ISO 9995 "D" row
	{
		SHIFTABLE(UCSA(0x09),EXTE(EXTENDED_KEY_BACKTAB),UCSA(0x09),EXTE(EXTENDED_KEY_BACKTAB),UCSA(0x09),EXTE(EXTENDED_KEY_BACKTAB),UCSA(0x09),EXTE(EXTENDED_KEY_BACKTAB)),
		CAPSABLE_CONTROL(UCSA('q'),UCSA('Q'),UCSA(L'ä'),UCSA(L'Ä'),UCSA('q'-0x60),UCSA(0x0242),UCSA(0x0241),UCSA(0x030D),NOOP(9995)),
		CAPSABLE_CONTROL(UCSA('w'),UCSA('W'),UCSA(L'å'),UCSA(L'Å'),UCSA('w'-0x60),UCSA(0x02B7),UCSA(0x2126),UCSA(0x0307),NOOP(9995)),
		CAPSABLE_CONTROL(UCSA('e'),UCSA('E'),UCSA(L'é'),UCSA(L'É'),UCSA('e'-0x60),UCSA(0x0153),UCSA(0x0152),UCSA(0x0306),NOOP(9995)),
		CAPSABLE_CONTROL(UCSA('r'),UCSA('R'),UCSA(L'™'),UCSA(L'®'),UCSA('r'-0x60),UCSA(0x00B6),UCSA(0x00AE),UCSA(0x0302),NOOP(9995)),
		CAPSABLE_CONTROL(UCSA('t'),UCSA('T'),UCSA(L'þ'),UCSA(L'Þ'),UCSA('t'-0x60),UCSA(0xA78C),UCSA(0xA78B),UCSA(0x0308),NOOP(9995)),
		CAPSABLE_CONTROL(UCSA('y'),UCSA('Y'),UCSA(L'ü'),UCSA(L'Ü'),UCSA('y'-0x60),UCSA(0x027C),UCSA(0x00A5),UCSA(0x0311),NOOP(9995)),
		CAPSABLE_CONTROL(UCSA('u'),UCSA('U'),UCSA(L'ú'),UCSA(L'Ú'),UCSA('u'-0x60),UCSA(0x0223),UCSA(0x0222),UCSA(0x030C),NOOP(9995)),
		CAPSABLE_CONTROL(UCSA('i'),UCSA('I'),UCSA(L'í'),UCSA(L'Í'),UCSA('i'-0x60),UCSA(0x0131),UCSA(0x214D),UCSA(0x0313),NOOP(9995)),
		CAPSABLE_CONTROL(UCSA('o'),UCSA('O'),UCSA(L'ó'),UCSA(L'Ó'),UCSA('o'-0x60),UCSA(0x00F8),UCSA(0x00D8),UCSA(0x031B),NOOP(9995)),
		CAPSABLE_CONTROL(UCSA('p'),UCSA('P'),UCSA(L'ö'),UCSA(L'Ö'),UCSA('p'-0x60),UCSA(0x00FE),UCSA(0x00DE),UCSA(0x0309),NOOP(9995)),
		SHIFTABLE_CONTROL(UCSA('['),UCSA('{'),UCSA(L'«'),UCSA(L'“'),UCSA(0x1B),UCSA(0x017F),UCSA(0x030A),UCSA(0x0300),NOOP(9995)),
		SHIFTABLE_CONTROL(UCSA(']'),UCSA('}'),UCSA(L'»'),UCSA(L'”'),UCSA(0x1D),UCSA(0x0303),UCSA(0x0304),UCSA(0x0040),NOOP(9995)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(EXTE(EXTENDED_KEY_RETURN_OR_ENTER)),
	},
	// 02: ISO 9995 "C" row
	{
		SIMPLE(NOOP(0)),
		CAPSABLE_CONTROL(UCSA('a'),UCSA('A'),UCSA(L'á'),UCSA(L'Á'),UCSA('a'-0x60),UCSA(0x00E6),UCSA(0x00C6),UCSA(0x0329),NOOP(9995)),
		CAPSABLE_CONTROL(UCSA('s'),UCSA('S'),UCSA(L'ß'),UCSA(L'§'),UCSA('s'-0x60),UCSA(0x00DF),UCSA(0x00A7),UCSA(0x0323),NOOP(9995)),
		CAPSABLE_CONTROL(UCSA('d'),UCSA('D'),UCSA(L'ð'),UCSA(L'Ð'),UCSA('d'-0x60),UCSA(0x00F0),UCSA(0x00D0),UCSA(0x032E),NOOP(9995)),
		CAPSABLE_CONTROL(UCSA('f'),UCSA('F'),NOOP(0),NOOP(0),UCSA('f'-0x60),UCSA(0x0294),UCSA(0x00AA),UCSA(0x032D),NOOP(9995)),
		CAPSABLE_CONTROL(UCSA('g'),UCSA('G'),NOOP(0),NOOP(0),UCSA('g'-0x60),UCSA(0x014B),UCSA(0x014A),UCSA(0x0331),NOOP(9995)),
		CAPSABLE_CONTROL(UCSA('h'),UCSA('H'),UCSA(0x2020),UCSA(0x2021),UCSA('h'-0x60),UCSA(0x0272),UCSA(0x019D),UCSA(0x0332),NOOP(9995)),
		CAPSABLE_CONTROL(UCSA('j'),UCSA('J'),NOOP(0),NOOP(0),UCSA('j'-0x60),UCSA(0x0133),UCSA(0x0132),UCSA(0x0325),NOOP(9995)),
		CAPSABLE_CONTROL(UCSA('k'),UCSA('K'),UCSA(L'œ'),UCSA(L'Œ'),UCSA('k'-0x60),UCSA(0x0138),UCSA(0x0325),UCSA(0x0335),NOOP(9995)),
		CAPSABLE_CONTROL(UCSA('l'),UCSA('L'),UCSA(L'ø'),UCSA(L'Ø'),UCSA('l'-0x60),UCSA(0x0142),UCSA(0x0141),UCSA(0x0338),NOOP(9995)),
		SHIFTABLE(UCSA(';'),UCSA(':'),UCSA(L'¶'),UCSA(L'°'),UCSA(0x0301),UCSA(0x030B),UCSA(0x00B0),NOOP(9995)),
		SHIFTABLE(UCSA('\''),UCSA('\"'),UCSA(L'´'),UCSA(L'¨'),UCSA(0x019B),UCSA(0x1E9E),UCSA(0x2032),NOOP(9995)),
		SHIFTABLE(UCSA('`'),UCSA('~'),UCSA(L'±'),UCSA(L'¥'),UCSA(0x204A),UCSA(0x00AD),UCSA(0x007C),NOOP(9995)),
		SHIFTABLE_CONTROL(UCSA('\\'),UCSA('|'),UCSA(L'¬'),UCSA(L'¦'),UCSA(0x1C),UCSA(0x0259),UCSA(0x018F),UCSA(0x2033),NOOP(9995)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
	},
	// 03: ISO 9995 "B" row
	{
		SIMPLE(NOOP(0)),
		SHIFTABLE_CONTROL(UCSA('\\'),UCSA('|'),UCSA(L'¬'),UCSA(L'¦'),UCSA(0x1C),UCSA(0x0259),UCSA(0x018F),UCSA(0x2033),NOOP(9995)),
		CAPSABLE_CONTROL(UCSA('z'),UCSA('Z'),UCSA(L'æ'),UCSA(L'Æ'),UCSA('z'-0x60),UCSA(0x0292),UCSA(0x01B7),UCSA(0x00AB),NOOP(9995)),
		CAPSABLE_CONTROL(UCSA('x'),UCSA('X'),NOOP(0),NOOP(0),UCSA('x'-0x60),UCSA(0x201E),UCSA(0x201A),UCSA(0x00BB),NOOP(9995)),
		CAPSABLE_CONTROL(UCSA('c'),UCSA('C'),UCSA(L'¢'),UCSA(L'©'),UCSA('c'-0x60),UCSA(0x00A2),UCSA(0x00A9),UCSA(0x2015),NOOP(9995)),
		CAPSABLE_CONTROL(UCSA('v'),UCSA('V'),UCSA(L'ł'),UCSA(L'Ł'),UCSA('v'-0x60),UCSA(0x201C),UCSA(0x2018),UCSA(0x2039),NOOP(9995)),
		CAPSABLE_CONTROL(UCSA('b'),UCSA('B'),NOOP(0),NOOP(0),UCSA('b'-0x60),UCSA(0x201D),UCSA(0x2019),UCSA(0x203A),NOOP(9995)),
		CAPSABLE_CONTROL(UCSA('n'),UCSA('N'),UCSA(L'ñ'),UCSA(L'Ñ'),UCSA('n'-0x60),UCSA(0x019E),UCSA(0x0220),UCSA(0x2013),NOOP(9995)),
		CAPSABLE_CONTROL(UCSA('m'),UCSA('M'),UCSA(L'µ'),UCSA(L'Μ'),UCSA('m'-0x60),UCSA(0x00B5),UCSA(0x00BA),UCSA(0x2014),NOOP(9995)),
		SHIFTABLE(UCSA(','),UCSA('<'),UCSA(L'ç'),UCSA(L'Ç'),UCSA(0x2026),UCSA(0x00D7),UCSA(0x0024),NOOP(9995)),
		SHIFTABLE(UCSA('.'),UCSA('>'),UCSA(L'·'),UCSA(L'…'),UCSA(0x00B7),UCSA(0x00F7),UCSA(0x0023),NOOP(9995)),
		SHIFTABLE(UCSA('/'),UCSA('?'),UCSA(L'¿'),UCSA(L'‽'),UCSA(0x0140),UCSA(0x013F),UCSA(0x2011),NOOP(9995)),
		SHIFTABLE(UCSA(L'₨'),UCSA(L'₩'),UCSA(L'‱'),UCSA(L'※'),UCSA(0x0149),UCSA(0x00A6),UCSA(0x0266A),NOOP(9995)),
		SHIFTABLE(UCSA(L'¥'),UCSA(L'‾'),UCSA(L'‐'),UCSA(L'‑'),NOOP(9995),NOOP(9995),NOOP(9995),NOOP(9995)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
	},
	// 04: Modifier row
	{
		SIMPLE(MMNT(KBDMAP_MODIFIER_1ST_LEVEL2)),
		SIMPLE(MMNT(KBDMAP_MODIFIER_2ND_LEVEL2)),
		SHIFTABLE(MMNT(KBDMAP_MODIFIER_1ST_LEVEL3),LOCK(KBDMAP_MODIFIER_1ST_GROUP2),MMNT(KBDMAP_MODIFIER_1ST_LEVEL3),LOCK(KBDMAP_MODIFIER_1ST_GROUP2),MMNT(KBDMAP_MODIFIER_1ST_LEVEL3),LOCK(KBDMAP_MODIFIER_1ST_GROUP2),MMNT(KBDMAP_MODIFIER_1ST_LEVEL3),LOCK(KBDMAP_MODIFIER_1ST_GROUP2)),
		SIMPLE(NOOP(0)),
		SIMPLE(MMNT(KBDMAP_MODIFIER_1ST_CONTROL)),
		SIMPLE(MMNT(KBDMAP_MODIFIER_2ND_CONTROL)),
		SIMPLE(MMNT(KBDMAP_MODIFIER_1ST_SUPER)),
		SIMPLE(MMNT(KBDMAP_MODIFIER_2ND_SUPER)),
		SIMPLE(MMNT(KBDMAP_MODIFIER_1ST_ALT)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SHIFTABLE(LOCK(KBDMAP_MODIFIER_CAPS),LOCK(KBDMAP_MODIFIER_LEVEL2),LOCK(KBDMAP_MODIFIER_CAPS),LOCK(KBDMAP_MODIFIER_LEVEL2),LOCK(KBDMAP_MODIFIER_CAPS),LOCK(KBDMAP_MODIFIER_LEVEL2),LOCK(KBDMAP_MODIFIER_CAPS),LOCK(KBDMAP_MODIFIER_LEVEL2)),
		SIMPLE(LOCK(KBDMAP_MODIFIER_SCROLL)),
		SIMPLE(LOCK(KBDMAP_MODIFIER_NUM)),
		SIMPLE(NOOP(0)),
 	},
	// 05: ISO 9995 "A" row
	{
		SIMPLE(NOOP(0)),
		SIMPLE(IMTH(EXTENDED_KEY_KATAHIRA)),
		SIMPLE(IMTH(EXTENDED_KEY_HALF_FULL_WIDTH)),
		SIMPLE(IMTH(EXTENDED_KEY_HIRAGANA)),
		SIMPLE(IMTH(EXTENDED_KEY_KATAKANA)),
		SIMPLE(IMTH(EXTENDED_KEY_HENKAN)),
		SIMPLE(IMTH(EXTENDED_KEY_MUHENKAN)),
		SIMPLE(NOOP(0)),
		SIMPLE(IMTH(EXTENDED_KEY_HANGUL_ENGLISH)),
		SIMPLE(IMTH(EXTENDED_KEY_HANJA)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(IMTH(EXTENDED_KEY_ALTERNATE_ERASE)),
		SIMPLE(NOOP(0)),
		SHIFTABLE(UCSA(' '),UCSA(' '),UCSA(' '),UCSA(' '),UCSA(0x202F),UCSA(0x200C),UCSA(0x00A0),NOOP(9995)),
	},
	// 06: Cursor/editing "row"
	{
		SIMPLE(EXTE(EXTENDED_KEY_HOME)),
		SIMPLE(EXTE(EXTENDED_KEY_UP_ARROW)),
		SIMPLE(EXTE(EXTENDED_KEY_PAGE_UP)),
		SIMPLE(EXTE(EXTENDED_KEY_LEFT_ARROW)),
		SIMPLE(EXTE(EXTENDED_KEY_RIGHT_ARROW)),
		SIMPLE(EXTE(EXTENDED_KEY_END)),
		SIMPLE(EXTE(EXTENDED_KEY_DOWN_ARROW)),
		SIMPLE(EXTE(EXTENDED_KEY_PAGE_DOWN)),
		SIMPLE(EXTE(EXTENDED_KEY_INSERT)),
		SIMPLE(EXTE(EXTENDED_KEY_DELETE)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
	},
	// 07: Calculator keypad "row" 1
	{
		NUMABLE(UCSA('*'),EXTE(EXTENDED_KEY_PAD_ASTERISK)),
		NUMABLE(UCSA('7'),EXTE(EXTENDED_KEY_PAD_HOME)),
		NUMABLE(UCSA('8'),EXTE(EXTENDED_KEY_PAD_UP)),
		NUMABLE(UCSA('9'),EXTE(EXTENDED_KEY_PAD_PAGE_UP)),
		NUMABLE(UCSA('-'),EXTE(EXTENDED_KEY_PAD_MINUS)),
		NUMABLE(UCSA('4'),EXTE(EXTENDED_KEY_PAD_LEFT)),
		NUMABLE(UCSA('5'),EXTE(EXTENDED_KEY_PAD_CENTRE)),
		NUMABLE(UCSA('6'),EXTE(EXTENDED_KEY_PAD_RIGHT)),
		NUMABLE(UCSA('+'),EXTE(EXTENDED_KEY_PAD_PLUS)),
		NUMABLE(UCSA('1'),EXTE(EXTENDED_KEY_PAD_END)),
		NUMABLE(UCSA('2'),EXTE(EXTENDED_KEY_PAD_DOWN)),
		NUMABLE(UCSA('3'),EXTE(EXTENDED_KEY_PAD_PAGE_DOWN)),
		NUMABLE(UCSA('0'),EXTE(EXTENDED_KEY_PAD_INSERT)),
		NUMABLE(UCSA('.'),EXTE(EXTENDED_KEY_PAD_DELETE)),
		NUMABLE(UCSA(0x0A),EXTE(EXTENDED_KEY_PAD_ENTER)),
		NUMABLE(UCSA('/'),EXTE(EXTENDED_KEY_PAD_SLASH)),
	},
	// 08: Calculator keypad "row" 2
	{
		NUMABLE(UCSA(','),EXTE(EXTENDED_KEY_PAD_COMMA)),
		NUMABLE(UCSA(','),EXTE(EXTENDED_KEY_PAD_COMMA)),
		NUMABLE(UCSA('='),EXTE(EXTENDED_KEY_PAD_EQUALS)),
		NUMABLE(UCSA('='),EXTE(EXTENDED_KEY_PAD_EQUALS)),
		NUMABLE(UCSA(0xB1),EXTE(EXTENDED_KEY_PAD_SIGN)),
		NUMABLE(UCSA('('),EXTE(EXTENDED_KEY_PAD_OPEN_BRACKET)),
		NUMABLE(UCSA(')'),EXTE(EXTENDED_KEY_PAD_CLOSE_BRACKET)),
		NUMABLE(UCSA('{'),EXTE(EXTENDED_KEY_PAD_OPEN_BRACE)),
		NUMABLE(UCSA('}'),EXTE(EXTENDED_KEY_PAD_CLOSE_BRACE)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
	},
	// 09: Function row 1
	{
		SIMPLE(UCSA(0x1b)),
#if defined(__LINUX__) || defined(__linux__)
		FUNKABLE2(1,EXTENDED_KEY_PAD_F1),
		FUNKABLE2(2,EXTENDED_KEY_PAD_F2),
		FUNKABLE2(3,EXTENDED_KEY_PAD_F3),
		FUNKABLE2(4,EXTENDED_KEY_PAD_F4),
		FUNKABLE2(5,EXTENDED_KEY_PAD_F5),
		FUNKABLE1(6),
		FUNKABLE1(7),
		FUNKABLE1(8),
		FUNKABLE1(9),
		FUNKABLE1(10),
		FUNKABLE1(11),
		FUNKABLE1(12),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
#else
		FUNCABLE2(1,EXTENDED_KEY_PAD_F1),
		FUNCABLE2(2,EXTENDED_KEY_PAD_F2),
		FUNCABLE2(3,EXTENDED_KEY_PAD_F3),
		FUNCABLE2(4,EXTENDED_KEY_PAD_F4),
		FUNCABLE2(5,EXTENDED_KEY_PAD_F5),
		FUNCABLE1(6),
		FUNCABLE1(7),
		FUNCABLE1(8),
		FUNCABLE1(9),
		FUNCABLE1(10),
		FUNCABLE1(11),
		FUNCABLE1(12),
		FUNCABLE1(13),
		FUNCABLE1(14),
		FUNCABLE1(15),
#endif
	},
	// 0A: Function row 2
	{
#if defined(__LINUX__) || defined(__linux__)
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
#else
		FUNCABLE1(16),
		FUNCABLE1(17),
		FUNCABLE1(18),
		FUNCABLE1(19),
		FUNCABLE1(20),
		FUNCABLE1(21),
		FUNCABLE1(22),
		FUNCABLE1(23),
		FUNCABLE1(24),
		FUNCABLE1(25),
		FUNCABLE1(26),
		FUNCABLE1(27),
		FUNCABLE1(28),
		FUNCABLE1(29),
		FUNCABLE1(30),
		FUNCABLE1(31),
#endif
	},
	// 0B: Function row 3
	{
#if defined(__LINUX__) || defined(__linux__)
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
#else
		FUNCABLE1(32),
		FUNCABLE1(33),
		FUNCABLE1(34),
		FUNCABLE1(35),
		FUNCABLE1(36),
		FUNCABLE1(37),
		FUNCABLE1(38),
		FUNCABLE1(39),
		FUNCABLE1(40),
		FUNCABLE1(41),
		FUNCABLE1(42),
		FUNCABLE1(43),
		FUNCABLE1(44),
		FUNCABLE1(45),
		FUNCABLE1(46),
		FUNCABLE1(47),
#endif
	},
	// 0C: Function row 4
	{
#if defined(__LINUX__) || defined(__linux__)
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
#else
		FUNCABLE1(48),
		FUNCABLE1(49),
		FUNCABLE1(50),
		FUNCABLE1(51),
		FUNCABLE1(52),
		FUNCABLE1(53),
		FUNCABLE1(54),
		FUNCABLE1(55),
		FUNCABLE1(56),
		FUNCABLE1(57),
		FUNCABLE1(58),
		FUNCABLE1(59),
		FUNCABLE1(60),
		FUNCABLE1(61),
		FUNCABLE1(62),
		FUNCABLE1(63),
#endif
	},
	// 0D: System Commands keypad "row"
	{
		SIMPLE(NOOP(0)),
		SIMPLE(SYST(SYSTEM_KEY_POWER)),
		SIMPLE(SYST(SYSTEM_KEY_SLEEP)),
		SIMPLE(SYST(SYSTEM_KEY_WAKE)),
		SIMPLE(SYST(SYSTEM_KEY_DEBUG)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
	},
	// 0E: Application Commands keypad "row" 1
	{
		SIMPLE(EXTE(EXTENDED_KEY_PAUSE)),
		SHIFTABLE(CONS(CONSUMER_KEY_NEXT_TASK),CONS(CONSUMER_KEY_PREVIOUS_TASK),NOOP(0),NOOP(0),CONS(CONSUMER_KEY_NEXT_TASK),CONS(CONSUMER_KEY_PREVIOUS_TASK),NOOP(0),NOOP(0)),
		SIMPLE(EXTE(EXTENDED_KEY_ATTENTION)),
		SIMPLE(EXTE(EXTENDED_KEY_APPLICATION)),
		SIMPLE(EXTE(EXTENDED_KEY_BREAK)),
		SIMPLE(EXTE(EXTENDED_KEY_PRINT_SCREEN)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(EXTE(EXTENDED_KEY_MUTE)),
		SIMPLE(EXTE(EXTENDED_KEY_VOLUME_DOWN)),
		SIMPLE(EXTE(EXTENDED_KEY_VOLUME_UP)),
	},
	// 0F: Application Commands keypad "row" 1
	{
		SIMPLE(NOOP(0)),
		SIMPLE(EXTE(EXTENDED_KEY_EXECUTE)),
		SIMPLE(EXTE(EXTENDED_KEY_HELP)),
		SIMPLE(EXTE(EXTENDED_KEY_MENU)),
		SIMPLE(EXTE(EXTENDED_KEY_SELECT)),
		SIMPLE(EXTE(EXTENDED_KEY_CANCEL)),
		SIMPLE(EXTE(EXTENDED_KEY_CLEAR)),
		SIMPLE(EXTE(EXTENDED_KEY_PRIOR)),
		SIMPLE(EXTE(EXTENDED_KEY_RETURN)),
		SIMPLE(EXTE(EXTENDED_KEY_SEPARATOR)),
		SIMPLE(EXTE(EXTENDED_KEY_OUT)),
		SIMPLE(EXTE(EXTENDED_KEY_OPER)),
		SIMPLE(EXTE(EXTENDED_KEY_CLEAR_OR_AGAIN)),
		SIMPLE(EXTE(EXTENDED_KEY_EX_SEL)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
	},
	// 10: Application Commands keypad "row" 2
	{
		SIMPLE(NOOP(0)),
		SIMPLE(EXTE(EXTENDED_KEY_STOP)),
		SIMPLE(EXTE(EXTENDED_KEY_AGAIN)),
		SIMPLE(EXTE(EXTENDED_KEY_PROPERTIES)),
		SIMPLE(EXTE(EXTENDED_KEY_UNDO)),
		SIMPLE(EXTE(EXTENDED_KEY_REDO)),
		SIMPLE(EXTE(EXTENDED_KEY_COPY)),
		SIMPLE(EXTE(EXTENDED_KEY_OPEN)),
		SIMPLE(EXTE(EXTENDED_KEY_PASTE)),
		SIMPLE(EXTE(EXTENDED_KEY_FIND)),
		SIMPLE(EXTE(EXTENDED_KEY_CUT)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
	},
	// 11: Consumer keypad "row" 1
	{
		SIMPLE(CONS(CONSUMER_KEY_CALCULATOR)),
		SIMPLE(CONS(CONSUMER_KEY_FILE_MANAGER)),
		SIMPLE(CONS(CONSUMER_KEY_WWW)),
		SIMPLE(CONS(CONSUMER_KEY_HOME)),
		SIMPLE(CONS(CONSUMER_KEY_REFRESH)),
		SIMPLE(CONS(CONSUMER_KEY_MAIL)),
		SIMPLE(CONS(CONSUMER_KEY_BOOKMARKS)),
		SIMPLE(CONS(CONSUMER_KEY_COMPUTER)),
		SIMPLE(CONS(CONSUMER_KEY_BACK)),
		SIMPLE(CONS(CONSUMER_KEY_FORWARD)),
		SIMPLE(CONS(CONSUMER_KEY_LOCK)),
		SIMPLE(CONS(CONSUMER_KEY_CLI)),
		SIMPLE(CONS(CONSUMER_KEY_NEXT_TRACK)),
		SIMPLE(CONS(CONSUMER_KEY_PREV_TRACK)),
		SIMPLE(CONS(CONSUMER_KEY_PLAY_PAUSE)),
		SIMPLE(CONS(CONSUMER_KEY_STOP_PLAYING)),
	},
	// 12: Consumer keypad "row" 2
	{
		SIMPLE(CONS(CONSUMER_KEY_RECORD)),
		SIMPLE(CONS(CONSUMER_KEY_REWIND)),
		SIMPLE(CONS(CONSUMER_KEY_FAST_FORWARD)),
		SIMPLE(CONS(CONSUMER_KEY_EJECT)),
		SIMPLE(CONS(CONSUMER_KEY_NEW)),
		SIMPLE(CONS(CONSUMER_KEY_EXIT)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
		SIMPLE(NOOP(0)),
	},
};

/* Parsing keyboard map files ***********************************************
// **************************************************************************
*/

static
const struct bsd_kbdmap_item {
	uint32_t action;
	const char * name;
} 
bsd_actions[]= {
	{	NOOP(1),				"nop"		},	// This is not actually in the BSD doco; but it is in lots of keymap files.
	{	UCSA(0x00),				"nul"		},
	{	UCSA(0x01),				"soh"		},
	{	UCSA(0x02),				"stx"		},
	{	UCSA(0x03),				"etx"		},
	{	UCSA(0x04),				"eot"		},
	{	UCSA(0x05),				"enq"		},
	{	UCSA(0x06),				"ack"		},
	{	UCSA(0x07),				"bel"		},
	{	UCSA(0x08),				"bs"		},
	{	UCSA(0x09),				"ht"		},
	{	UCSA(0x0A),				"nl"		},
	{	UCSA(0x0B),				"vt"		},
//	{	UCSA(0x0C),				"np"		},	// A BSDism that isn't used in any FreeBSD 10 keymap.
	{	UCSA(0x0C),				"ff"		},
	{	UCSA(0x0D),				"cr"		},
	{	UCSA(0x0E),				"so"		},
	{	UCSA(0x0F),				"si"		},
	{	UCSA(0x10),				"dle"		},
	{	UCSA(0x11),				"dc1"		},
	{	UCSA(0x12),				"dc2"		},
	{	UCSA(0x13),				"dc3"		},
	{	UCSA(0x14),				"dc4"		},
	{	UCSA(0x15),				"nak"		},
	{	UCSA(0x16),				"syn"		},
	{	UCSA(0x17),				"etb"		},
	{	UCSA(0x18),				"can"		},
	{	UCSA(0x19),				"em"		},
	{	UCSA(0x1A),				"sub"		},
	{	UCSA(0x1B),				"esc"		},
	{	UCSA(0x1C),				"fs"		},
	{	UCSA(0x1D),				"gs"		},
	{	UCSA(0x1E),				"rs"		},
	{	UCSA(0x1F),				"us"		},
	{	UCSA(0x20),				"sp"		},
	{	UCSA(0x7F),				"del"		},
	{	UCSA(0x0300),				"dgra"		},
	{	UCSA(0x0301),				"dacu"		},
	{	UCSA(0x0302),				"dcir"		},
	{	UCSA(0x0303),				"dtil"		},
	{	UCSA(0x0304),				"dmac"		},
	{	UCSA(0x0306),				"dbre"		},
	{	UCSA(0x0307),				"ddot"		},
	{	UCSA(0x0308),				"ddia"		},
	{	UCSA(0x030A),				"drin"		},
	{	UCSA(0x030B),				"ddac"		},
	{	UCSA(0x030C),				"dcar"		},
	{	UCSA(0x0327),				"dced"		},
	{	UCSA(0x0328),				"dogo"		},
	/// FIXME \todo This is supposed to be a combining key.
	/// It isn't used in any FreeBSD 10 keymaps; but it is used informally in third party keymaps to make currency characters (dapo+"S" => "$", dapo+"L" => "£" etc.)
	/// There isn't a Unicode combining character that does this; nor does ISO 9995-3 define any such combining character.
//	{	UCSA(0x),				"dapo"		},
	{	UCSA(0x0338),				"dsla"		},	// See ISO 9995-3 appendix F for why this particular combining character is the "slash" that combines with "o" and "O".
	{	UCSA(0x0308),				"duml"		},	// This really should be CGJ + 0x0308, but a lot of BSD keymaps rely upon "ddia" and "duml" being interchangeable.
	{	SYST(SYSTEM_KEY_POWER),			"pdwn"		},
	{	SYST(SYSTEM_KEY_DEBUG),			"debug"		},
	{	SYST(SYSTEM_KEY_SLEEP),			"susp"		},
	{	SYST(SYSTEM_KEY_WARM_RESTART),		"boot"		},
	{	SYST(SYSTEM_KEY_HALT),			"halt"		},
	{	SYST(SYSTEM_KEY_ABEND),			"panic"		},
	{	NOOP(0xFB5D01),				"paste"		},	/// FIXME \todo This needs improvement.
	{	MMNT(KBDMAP_MODIFIER_1ST_LEVEL2),	"shift"		},
	{	MMNT(KBDMAP_MODIFIER_1ST_LEVEL2),	"lshift"	},
	{	MMNT(KBDMAP_MODIFIER_2ND_LEVEL2),	"rshift"	},
	{	MMNT(KBDMAP_MODIFIER_1ST_CONTROL),	"ctrl"		},
	{	MMNT(KBDMAP_MODIFIER_1ST_CONTROL),	"lctrl"		},
	{	MMNT(KBDMAP_MODIFIER_2ND_CONTROL),	"rctrl"		},
	{	MMNT(KBDMAP_MODIFIER_1ST_ALT),		"alt"		},
	{	MMNT(KBDMAP_MODIFIER_1ST_ALT),		"lalt"		},
	{	MMNT(KBDMAP_MODIFIER_1ST_LEVEL3),	"altgr"		},
	{	MMNT(KBDMAP_MODIFIER_1ST_LEVEL3),	"ralt"		},
	{	MMNT(KBDMAP_MODIFIER_2ND_LEVEL3),	"ashift"	},
	{	MMNT(KBDMAP_MODIFIER_1ST_META),		"meta"		},
	{	MMNT(KBDMAP_MODIFIER_1ST_META),		"lmeta"		},
	{	MMNT(KBDMAP_MODIFIER_2ND_META),		"rmeta"		},
	{	LOCK(KBDMAP_MODIFIER_LEVEL3),		"lshifta"	},
	{	LOCK(KBDMAP_MODIFIER_LEVEL3),		"rshifta"	},
	{	LOCK(KBDMAP_MODIFIER_LEVEL3),		"shifta"	},
	{	LOCK(KBDMAP_MODIFIER_LEVEL3),		"lctrla"	},
	{	LOCK(KBDMAP_MODIFIER_LEVEL3),		"rctrla"	},
	{	LOCK(KBDMAP_MODIFIER_LEVEL3),		"ctrla"		},
	{	LOCK(KBDMAP_MODIFIER_LEVEL3),		"lalta"		},
	{	LOCK(KBDMAP_MODIFIER_LEVEL3),		"ralta"		},
	{	LOCK(KBDMAP_MODIFIER_LEVEL3),		"alta"		},
	{	LOCK(KBDMAP_MODIFIER_LEVEL3),		"alock"		},
	{	LOCK(KBDMAP_MODIFIER_CAPS),		"clock"		},
	{	LOCK(KBDMAP_MODIFIER_NUM),		"nlock"		},
	{	LOCK(KBDMAP_MODIFIER_SCROLL),		"slock"		},
	{	EXTE(EXTENDED_KEY_BACKTAB),		"btab"		},
	{	EXTE(EXTENDED_KEY_BACKSPACE),		"bspace"	},	// This is an extension to the BSD format that allows use of the DEC VT switchable backspace mechanism.
	{	MMNT(KBDMAP_MODIFIER_1ST_GROUP2),	"g2shift"	},	// This is an extension to the BSD format that allows group 2 shift.
	{	LTCH(KBDMAP_MODIFIER_1ST_GROUP2),	"g2latch"	},	// This is an extension to the BSD format that allows group 2 latch.
	{	LOCK(KBDMAP_MODIFIER_1ST_GROUP2),	"g2lock"	},	// This is an extension to the BSD format that allows group 2 lock.
	{	CONS(CONSUMER_KEY_NEXT_TASK),		"nscr"		},
	{	CONS(CONSUMER_KEY_PREVIOUS_TASK),	"pscr"		},
	{	CONS(CONSUMER_KEY_LOCK),		"saver"		},
	{	SCRN( 0),				"scr01"		},
	{	SCRN( 1),				"scr02"		},
	{	SCRN( 2),				"scr03"		},
	{	SCRN( 3),				"scr04"		},
	{	SCRN( 4),				"scr05"		},
	{	SCRN( 5),				"scr06"		},
	{	SCRN( 6),				"scr07"		},
	{	SCRN( 7),				"scr08"		},
	{	SCRN( 8),				"scr09"		},
	{	SCRN( 9),				"scr10"		},
	{	SCRN(10),				"scr11"		},
	{	SCRN(11),				"scr12"		},
	{	SCRN(12),				"scr13"		},
	{	SCRN(13),				"scr14"		},
	{	SCRN(14),				"scr15"		},
	{	SCRN(15),				"scr16"		},
	// In a BSD keymap, these always imply the DEC VT numeric keypad Fn keys.
	{	EXTE(EXTENDED_KEY_PAD_F1),		"fkey01"	},
	{	EXTE(EXTENDED_KEY_PAD_F2),		"fkey02"	},
	{	EXTE(EXTENDED_KEY_PAD_F3),		"fkey03"	},
	{	EXTE(EXTENDED_KEY_PAD_F4),		"fkey04"	},
	{	EXTE(EXTENDED_KEY_PAD_F5),		"fkey05"	},
	// In a BSD keymap, these always imply the no modifiers behaviour.
	{	FUNK( 6),				"fkey06"	},
	{	FUNK( 7),				"fkey07"	},
	{	FUNK( 8),				"fkey08"	},
	{	FUNK( 9),				"fkey09"	},
	{	FUNK(10),				"fkey10"	},
	{	FUNK(11),				"fkey11"	},
	{	FUNK(12),				"fkey12"	},
	{	FUNK(13),				"fkey13"	},
	{	FUNK(14),				"fkey14"	},
	{	FUNK(15),				"fkey15"	},
	{	FUNK(16),				"fkey16"	},
	{	FUNK(17),				"fkey17"	},
	{	FUNK(18),				"fkey18"	},
	{	FUNK(19),				"fkey19"	},
	{	FUNK(20),				"fkey20"	},
	{	FUNK(21),				"fkey21"	},
	{	FUNK(22),				"fkey22"	},
	{	FUNK(23),				"fkey23"	},
	{	FUNK(24),				"fkey24"	},
	{	FUNK(25),				"fkey25"	},
	{	FUNK(26),				"fkey26"	},
	{	FUNK(27),				"fkey27"	},
	{	FUNK(28),				"fkey28"	},
	{	FUNK(29),				"fkey29"	},
	{	FUNK(30),				"fkey30"	},
	{	FUNK(31),				"fkey31"	},
	{	FUNK(32),				"fkey32"	},
	{	FUNK(33),				"fkey33"	},
	{	FUNK(34),				"fkey34"	},
	{	FUNK(35),				"fkey35"	},
	{	FUNK(36),				"fkey36"	},
	{	FUNK(37),				"fkey37"	},
	{	FUNK(38),				"fkey38"	},
	{	FUNK(39),				"fkey39"	},
	{	FUNK(40),				"fkey40"	},
	{	FUNK(41),				"fkey41"	},
	{	FUNK(42),				"fkey42"	},
	{	FUNK(43),				"fkey43"	},
	{	FUNK(44),				"fkey44"	},
	{	FUNK(45),				"fkey45"	},
	{	FUNK(46),				"fkey46"	},
	{	FUNK(47),				"fkey47"	},
	{	FUNK(48),				"fkey48"	},
	{	EXTE(EXTENDED_KEY_HOME),		"fkey49"	},
	{	EXTE(EXTENDED_KEY_UP_ARROW),		"fkey50"	},
	{	EXTE(EXTENDED_KEY_PAGE_UP),		"fkey51"	},
	{	EXTE(EXTENDED_KEY_PAD_MINUS),		"fkey52"	},
	{	EXTE(EXTENDED_KEY_LEFT_ARROW),		"fkey53"	},
	{	EXTE(EXTENDED_KEY_PAD_CENTRE),		"fkey54"	},
	{	EXTE(EXTENDED_KEY_RIGHT_ARROW),		"fkey55"	},
	{	EXTE(EXTENDED_KEY_PAD_PLUS),		"fkey56"	},
	{	EXTE(EXTENDED_KEY_END),			"fkey57"	},
	{	EXTE(EXTENDED_KEY_DOWN_ARROW),		"fkey58"	},
	{	EXTE(EXTENDED_KEY_PAGE_DOWN),		"fkey59"	},
	{	EXTE(EXTENDED_KEY_INSERT),		"fkey60"	},
	{	EXTE(EXTENDED_KEY_DELETE),		"fkey61"	},
	{	MMNT(KBDMAP_MODIFIER_1ST_SUPER),	"fkey62"	},
	{	MMNT(KBDMAP_MODIFIER_2ND_SUPER),	"fkey63"	},
	{	EXTE(EXTENDED_KEY_APPLICATION),		"fkey64"	},
	// In a BSD kbdmap "fkey", as can be seen above, does not necessarily mean function key.
	// So what "fkey65" et seq. mean is undefined and unguessable.
#if 0
	{	NOOP(0xFB5D41),				"fkey65"	},
	{	NOOP(0xFB5D42),				"fkey66"	},
	{	NOOP(0xFB5D43),				"fkey67"	},
	{	NOOP(0xFB5D44),				"fkey68"	},
	{	NOOP(0xFB5D45),				"fkey69"	},
	{	NOOP(0xFB5D46),				"fkey70"	},
	{	NOOP(0xFB5D47),				"fkey71"	},
	{	NOOP(0xFB5D48),				"fkey72"	},
	{	NOOP(0xFB5D49),				"fkey73"	},
	{	NOOP(0xFB5D4A),				"fkey74"	},
	{	NOOP(0xFB5D4B),				"fkey75"	},
	{	NOOP(0xFB5D4C),				"fkey76"	},
	{	NOOP(0xFB5D4D),				"fkey77"	},
	{	NOOP(0xFB5D4E),				"fkey78"	},
	{	NOOP(0xFB5D4F),				"fkey79"	},
	{	NOOP(0xFB5D50),				"fkey80"	},
	{	NOOP(0xFB5D51),				"fkey81"	},
	{	NOOP(0xFB5D52),				"fkey82"	},
	{	NOOP(0xFB5D53),				"fkey83"	},
	{	NOOP(0xFB5D54),				"fkey84"	},
	{	NOOP(0xFB5D55),				"fkey85"	},
	{	NOOP(0xFB5D56),				"fkey86"	},
	{	NOOP(0xFB5D57),				"fkey87"	},
	{	NOOP(0xFB5D58),				"fkey88"	},
	{	NOOP(0xFB5D59),				"fkey89"	},
	{	NOOP(0xFB5D5A),				"fkey90"	},
	{	NOOP(0xFB5D5B),				"fkey91"	},
	{	NOOP(0xFB5D5C),				"fkey92"	},
	{	NOOP(0xFB5D5D),				"fkey93"	},
	{	NOOP(0xFB5D5E),				"fkey94"	},
	{	NOOP(0xFB5D5F),				"fkey95"	},
	{	NOOP(0xFB5D60),				"fkey96"	},
	{	NOOP(0xFB5D61),				"fkey97"	},
	{	NOOP(0xFB5D62),				"fkey98"	},
	{	NOOP(0xFB5D63),				"fkey99"	},
#endif
};

static inline
std::string
strip_field (
	std::string & line
) {
	if (line.empty()) return line;
	bool was_space(false), quoted(false);
	std::string::iterator p(line.begin());
	while (p != line.end()) {
		const bool is_space(!quoted && std::isspace(*p));
		if ('\'' == *p) quoted = was_space || p == line.begin();
		if (was_space && !is_space) break;
		was_space = is_space;
		++p;
	}
	std::string r(line.substr(0, p - line.begin()));
	line = line.substr(p - line.begin(), line.npos);
	return rtrim(r);
}

static inline
bool 
is_decimal ( 
	const std::string & s 
) {
	for (std::string::const_iterator p(s.begin()); s.end() != p; ++p)
		if (!std::isdigit(*p)) 
			return false;
	return true;
}

static inline
bool
hexprefix (
	const std::string & s
) {
	return s == "0x" || s == "0X";
}

static inline
unsigned 
hexval ( 
	const std::string & s 
) {
	unsigned v = 0;
	for (std::string::const_iterator p(s.begin()); s.end() != p; ++p) {
		const char c(*p);
		if (std::isdigit(c)) 
			v = v * 0x10 + static_cast<unsigned>(c - '0');
		else
		if (std::isxdigit(c)) 
			v = v * 0x10 + static_cast<unsigned>(std::tolower(c) - 'a' + 0x0A);
		else
			break;
	}
	return v;
}

static inline
unsigned 
octval ( 
	const std::string & s 
) {
	unsigned v = 0;
	for (std::string::const_iterator p(s.begin()); s.end() != p; ++p) {
		const char c(*p);
		if (std::isdigit(c) && c < '8') 
			v = v * 0x10 + static_cast<unsigned>(c - '0');
		else
			break;
	}
	return v;
}

static inline
uint32_t
action (
	const char * prog,
	const char * name,
	unsigned long line,
	const std::string & s
) {
	if (s.length() == 3 && '\'' == s[0] && '\'' == s[2]) return UCSA(s[1]);
	if (s.length() > 2 && hexprefix(s.substr(0, 2))) return UCSA(hexval(s.substr(2, s.npos)));
	if (s.length() > 1 && s[0] == '0') return UCSA(octval(s.substr(1, s.npos)));
	if (s.length() > 0 && is_decimal(s)) return UCSA(val(s));
	const bsd_kbdmap_item * e(bsd_actions + sizeof bsd_actions/sizeof *bsd_actions);
	for (const bsd_kbdmap_item * p(bsd_actions); p <  e; ++p) {
		if (p->name == s) 
			return p->action;
	}
	if ("ns" == s)
		std::fprintf(stderr, "%s: WARNING: %s(%lu): \"%s\": %s\n", prog, name, line, s.c_str(), "This is a fictional ASCII character with no defined value; please replace it with a real one.");
	else
		std::fprintf(stderr, "%s: WARNING: %s(%lu): \"%s\": %s\n", prog, name, line, s.c_str(), "Didn't understand action.");
	return NOOP(0x777777);
}

static 
bool 
process (
	kbdmap_entry keyboard[17][16],
	const char * prog,
	const char * name,
	std::istream & i
) {
	bool seen_impossible(false);
	for (unsigned long ln(1UL); ; ++ln) {
		std::string line;
		std::getline(i, line, '\n');
		if (i.eof()) break;
		if (i.fail()) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, name, std::strerror(error));
			return false;
		}
		line = ltrim(line);
		if (line.length() > 0U && '#' == line[0]) continue;

		const std::string kc(strip_field(line));
		if (kc.empty()) continue;
		if (!is_decimal(kc)) continue;
		bool seen_scrn(false), is_all_nop(true);
		uint32_t cmds[8] = { 0 };
		for (unsigned j(0U); j < sizeof cmds/sizeof *cmds; ++j) {
			const std::string as(strip_field(line));
			cmds[j] = action(prog, name, ln, as);
			seen_scrn |= KBDMAP_ACTION_SCREEN == (cmds[j] & KBDMAP_ACTION_MASK);
			is_all_nop &= 0x00 == (cmds[j] & KBDMAP_ACTION_MASK);
		}
		const std::string l(strip_field(line));

		unsigned keycode(val(kc));
		if (keycode >= 0x100) {
			if (!seen_impossible)
				std::fprintf(stderr, "%s: WARNING: %s(%lu): %u: %s\n", prog, name, ln, keycode, "Impossible 8-bit keycode.");
			seen_impossible = true;
			continue;
		}
		const bool level3(keycode & 0x80);
		keycode &= 0x7f;
		// Silently ignore attempts to map a keycode that does not occur so that our own NOP mapping for the first entry remains untouched.
		if (0x00 == keycode) continue;	
		const uint16_t index(bsd_keycode_to_keymap_index(keycode));
		if (0xFFFF == index) {
			if (!is_all_nop)
				std::fprintf(stderr, "%s: WARNING: %s(%lu): %u: %s\n", prog, name, ln, keycode, "Un-convertable undefined keycode.");
			continue;
		}
		kbdmap_entry & map(keyboard[index >> 8U][index & 0x0F]);

		if (!level3) {
			for (unsigned j(0U); j < sizeof cmds/sizeof *cmds; ++j) 
				map.p[j] = cmds[j];
		} else
		if (!seen_scrn) {
			for (unsigned j(0U); j < 4U; ++j) 
				map.p[j + 4U] = cmds[j];
		}

		if ("B" == l || "N" == l)
			map.cmd = 'n';
		else
		if ("C" == l)
			map.cmd = 'c';
		else if (seen_scrn)
			map.cmd = 'f';
		else
			map.cmd = 's';

	}
	return true;
}

static 
void 
overlay_group2_latch (
	kbdmap_entry keyboard[17][16]
) {
	kbdmap_entry & e(keyboard[KBDMAP_INDEX_OPTION >> 8][KBDMAP_INDEX_OPTION & 0xFF]);
	e.p[1] = e.p[5] = e.p[9] = e.p[13] = LTCH(KBDMAP_MODIFIER_1ST_GROUP2);
}

/* Main function ************************************************************
// **************************************************************************
*/

void
console_convert_kbdmap [[gnu::noreturn]] ( 
	const char * & /*next_prog*/,
	std::vector<const char *> & args,
	ProcessEnvironment & /*envs*/
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::top_table_definition main_option(0, 0, "Main options", "file(s)");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw static_cast<int>(EXIT_USAGE);
	}
	kbdmap_entry keyboard[17][16];
	for (unsigned i(0U); i < 17U; ++i) {
		for (unsigned j(0U); j < 16U; ++j)
			keyboard[i][j] = English_international_keyboard[i][j];
	}
	if (args.empty()) {
		if (!process(keyboard, prog, "<stdin>", std::cin))
			throw static_cast<int>(EXIT_TEMPORARY_FAILURE);	// Bernstein daemontools compatibility
	} else {
		for (std::vector<const char *>::const_iterator i(args.begin()); i != args.end(); ++i) {
			const char * name(*i);
			std::ifstream ifs(name);
			if (ifs.fail()) {
				const int error(errno);
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, name, std::strerror(error));
				throw static_cast<int>(EXIT_PERMANENT_FAILURE);	// Bernstein daemontools compatibility
			}
			if (!process(keyboard, prog, name, ifs))
				throw static_cast<int>(EXIT_TEMPORARY_FAILURE);	// Bernstein daemontools compatibility
		}
	}
	overlay_group2_latch(keyboard);
	for (unsigned i(0U); i < 17U; ++i)
		for (unsigned j(0U); j < 16U; ++j) {
			const kbdmap_entry & e(keyboard[i][j]);
			const uint32_t v[] = {
				htobe32(e.cmd),
				0,
				0,
				0,
				0,
				0,
				0,
				0,
				htobe32(e.p[ 0]),
				htobe32(e.p[ 1]),
				htobe32(e.p[ 2]),
				htobe32(e.p[ 3]),
				htobe32(e.p[ 4]),
				htobe32(e.p[ 5]),
				htobe32(e.p[ 6]),
				htobe32(e.p[ 7]),
				htobe32(e.p[ 8]),
				htobe32(e.p[ 9]),
				htobe32(e.p[10]),
				htobe32(e.p[11]),
				htobe32(e.p[12]),
				htobe32(e.p[13]),
				htobe32(e.p[14]),
				htobe32(e.p[15]),
			};
			std::fwrite(v, sizeof v/sizeof *v, sizeof *v, stdout);
		}
	throw EXIT_SUCCESS;
}
