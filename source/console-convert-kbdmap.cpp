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
#include "kbdmap_default.h"
#include "kbdmap_utils.h"
#include "kbdmap_entries.h"
#include "InputMessage.h"

/* Parsing keyboard map files ***********************************************
// **************************************************************************
*/

static
const struct bsd_kbdmap_item {
	// Having two actions lets us map a single BSD name onto different actions for the calculator and cursor+editing keypads.
	uint32_t action[2U];	// Action indices: 0 if from original PC/XT keycode, 1 if from PC/AT or PC98 extension keycodes
	const char * name;
} 
bsd_actions[]= {
	{ {	NOOP(1),				NOOP(1),				}, "nop"		},	// This is not actually in the BSD doco; but it is in lots of keymap files.
	{ {	UCSA(0x00),				UCSA(0x00),				}, "nul"		},
	{ {	UCSA(0x01),				UCSA(0x01),				}, "soh"		},
	{ {	UCSA(0x02),				UCSA(0x02),				}, "stx"		},
	{ {	UCSA(0x03),				UCSA(0x03),				}, "etx"		},
	{ {	UCSA(0x04),				UCSA(0x04),				}, "eot"		},
	{ {	UCSA(0x05),				UCSA(0x05),				}, "enq"		},
	{ {	UCSA(0x06),				UCSA(0x06),				}, "ack"		},
	{ {	UCSA(0x07),				UCSA(0x07),				}, "bel"		},
	{ {	UCSA(0x08),				UCSA(0x08),				}, "bs"			},
	{ {	UCSA(0x09),				UCSA(0x09),				}, "ht"			},
	{ {	UCSA(0x0A),				UCSA(0x0A),				}, "nl"			},
	{ {	UCSA(0x0B),				UCSA(0x0B),				}, "vt"			},
//	{ {	UCSA(0x0C),				UCSA(0x0C),				}, "np"			},	// A BSDism that isn't used in any FreeBSD 10 keymap.
	{ {	UCSA(0x0C),				UCSA(0x0C),				}, "ff"			},
	{ {	UCSA(0x0D),				UCSA(0x0D),				}, "cr"			},
	{ {	UCSA(0x0E),				UCSA(0x0E),				}, "so"			},
	{ {	UCSA(0x0F),				UCSA(0x0F),				}, "si"			},
	{ {	UCSA(0x10),				UCSA(0x10),				}, "dle"		},
	{ {	UCSA(0x11),				UCSA(0x11),				}, "dc1"		},
	{ {	UCSA(0x12),				UCSA(0x12),				}, "dc2"		},
	{ {	UCSA(0x13),				UCSA(0x13),				}, "dc3"		},
	{ {	UCSA(0x14),				UCSA(0x14),				}, "dc4"		},
	{ {	UCSA(0x15),				UCSA(0x15),				}, "nak"		},
	{ {	UCSA(0x16),				UCSA(0x16),				}, "syn"		},
	{ {	UCSA(0x17),				UCSA(0x17),				}, "etb"		},
	{ {	UCSA(0x18),				UCSA(0x18),				}, "can"		},
	{ {	UCSA(0x19),				UCSA(0x19),				}, "em"			},
	{ {	UCSA(0x1A),				UCSA(0x1A),				}, "sub"		},
	{ {	UCSA(0x1B),				UCSA(0x1B),				}, "esc"		},
	{ {	UCSA(0x1C),				UCSA(0x1C),				}, "fs"			},
	{ {	UCSA(0x1D),				UCSA(0x1D),				}, "gs"			},
	{ {	UCSA(0x1E),				UCSA(0x1E),				}, "rs"			},
	{ {	UCSA(0x1F),				UCSA(0x1F),				}, "us"			},
	{ {	UCSA(0x20),				UCSA(0x20),				}, "sp"			},
	{ {	UCSA(0x7F),				UCSA(0x7F),				}, "del"		},
	{ {	UCSA(0x0300),				UCSA(0x0300),				}, "dgra"		},
	{ {	UCSA(0x0301),				UCSA(0x0301),				}, "dacu"		},
	{ {	UCSA(0x0302),				UCSA(0x0302),				}, "dcir"		},
	{ {	UCSA(0x0303),				UCSA(0x0303),				}, "dtil"		},
	{ {	UCSA(0x0304),				UCSA(0x0304),				}, "dmac"		},
	{ {	UCSA(0x0306),				UCSA(0x0306),				}, "dbre"		},
	{ {	UCSA(0x0307),				UCSA(0x0307),				}, "ddot"		},
	{ {	UCSA(0x0308),				UCSA(0x0308),				}, "ddia"		},
	{ {	UCSA(0x030A),				UCSA(0x030A),				}, "drin"		},
	{ {	UCSA(0x030B),				UCSA(0x030B),				}, "ddac"		},
	{ {	UCSA(0x030C),				UCSA(0x030C),				}, "dcar"		},
	{ {	UCSA(0x0327),				UCSA(0x0327),				}, "dced"		},
	{ {	UCSA(0x0328),				UCSA(0x0328),				}, "dogo"		},
	/// FIXME \todo This is supposed to be a combining key.
	/// It isn't used in any FreeBSD 10 keymaps; but it is used informally in third party keymaps to make currency characters (dapo+}, "S" => "$", dapo+"L" => "£" etc.)
	/// There isn't a Unicode combining character that does this; nor does ISO 9995-3 define any such combining character.
//	{ {	UCSA(0x),				UCSA(0x),				}, "dapo"		},
	{ {	UCSA(0x0338),				UCSA(0x0338),				}, "dsla"		},	// See ISO 9995-3 appendix F for why this particular combining character is the "slash" that combines with "o" and "O".
	{ {	UCSA(0x0308),				UCSA(0x0308),				}, "duml"		},	// This really should be CGJ + 0x0308, but a lot of BSD keymaps rely upon "ddia" and "duml" being interchangeable.
	{ {	SYST(SYSTEM_KEY_POWER),			SYST(SYSTEM_KEY_POWER),			}, "pdwn"		},
	{ {	SYST(SYSTEM_KEY_DEBUG),			SYST(SYSTEM_KEY_DEBUG),			}, "debug"		},
	{ {	SYST(SYSTEM_KEY_SLEEP),			SYST(SYSTEM_KEY_SLEEP),			}, "susp"		},
	{ {	SYST(SYSTEM_KEY_WARM_RESTART),		SYST(SYSTEM_KEY_WARM_RESTART),		}, "boot"		},
	{ {	SYST(SYSTEM_KEY_HALT),			SYST(SYSTEM_KEY_HALT),			}, "halt"		},
	{ {	SYST(SYSTEM_KEY_ABEND),			SYST(SYSTEM_KEY_ABEND),			}, "panic"		},
	{ {	NOOP(0xFB5D01),				NOOP(0xFB5D01),				}, "paste"		},	/// FIXME \todo This needs improvement.
	{ {	MMNT(KBDMAP_MODIFIER_1ST_LEVEL2),	MMNT(KBDMAP_MODIFIER_1ST_LEVEL2),	}, "shift"		},
	{ {	MMNT(KBDMAP_MODIFIER_1ST_LEVEL2),	MMNT(KBDMAP_MODIFIER_1ST_LEVEL2),	}, "lshift"		},
	{ {	MMNT(KBDMAP_MODIFIER_2ND_LEVEL2),	MMNT(KBDMAP_MODIFIER_2ND_LEVEL2),	}, "rshift"		},
	{ {	MMNT(KBDMAP_MODIFIER_1ST_CONTROL),	MMNT(KBDMAP_MODIFIER_1ST_CONTROL),	}, "ctrl"		},
	{ {	MMNT(KBDMAP_MODIFIER_1ST_CONTROL),	MMNT(KBDMAP_MODIFIER_1ST_CONTROL),	}, "lctrl"		},
	{ {	MMNT(KBDMAP_MODIFIER_2ND_CONTROL),	MMNT(KBDMAP_MODIFIER_2ND_CONTROL),	}, "rctrl"		},
	{ {	MMNT(KBDMAP_MODIFIER_1ST_ALT),		MMNT(KBDMAP_MODIFIER_1ST_ALT),		}, "alt"		},
	{ {	MMNT(KBDMAP_MODIFIER_1ST_ALT),		MMNT(KBDMAP_MODIFIER_1ST_ALT),		}, "lalt"		},
	{ {	MMNT(KBDMAP_MODIFIER_1ST_LEVEL3),	MMNT(KBDMAP_MODIFIER_1ST_LEVEL3),	}, "altgr"		},
	{ {	MMNT(KBDMAP_MODIFIER_1ST_LEVEL3),	MMNT(KBDMAP_MODIFIER_1ST_LEVEL3),	}, "ralt"		},
	{ {	MMNT(KBDMAP_MODIFIER_2ND_LEVEL3),	MMNT(KBDMAP_MODIFIER_2ND_LEVEL3),	}, "ashift"		},
	{ {	MMNT(KBDMAP_MODIFIER_1ST_META),		MMNT(KBDMAP_MODIFIER_1ST_META),		}, "meta"		},
	{ {	MMNT(KBDMAP_MODIFIER_1ST_META),		MMNT(KBDMAP_MODIFIER_1ST_META),		}, "lmeta"		},
	{ {	MMNT(KBDMAP_MODIFIER_2ND_META),		MMNT(KBDMAP_MODIFIER_2ND_META),		}, "rmeta"		},
	{ {	LOCK(KBDMAP_MODIFIER_LEVEL3),		LOCK(KBDMAP_MODIFIER_LEVEL3),		}, "lshifta"		},
	{ {	LOCK(KBDMAP_MODIFIER_LEVEL3),		LOCK(KBDMAP_MODIFIER_LEVEL3),		}, "rshifta"		},
	{ {	LOCK(KBDMAP_MODIFIER_LEVEL3),		LOCK(KBDMAP_MODIFIER_LEVEL3),		}, "shifta"		},
	{ {	LOCK(KBDMAP_MODIFIER_LEVEL3),		LOCK(KBDMAP_MODIFIER_LEVEL3),		}, "lctrla"		},
	{ {	LOCK(KBDMAP_MODIFIER_LEVEL3),		LOCK(KBDMAP_MODIFIER_LEVEL3),		}, "rctrla"		},
	{ {	LOCK(KBDMAP_MODIFIER_LEVEL3),		LOCK(KBDMAP_MODIFIER_LEVEL3),		}, "ctrla"		},
	{ {	LOCK(KBDMAP_MODIFIER_LEVEL3),		LOCK(KBDMAP_MODIFIER_LEVEL3),		}, "lalta"		},
	{ {	LOCK(KBDMAP_MODIFIER_LEVEL3),		LOCK(KBDMAP_MODIFIER_LEVEL3),		}, "ralta"		},
	{ {	LOCK(KBDMAP_MODIFIER_LEVEL3),		LOCK(KBDMAP_MODIFIER_LEVEL3),		}, "alta"		},
	{ {	LOCK(KBDMAP_MODIFIER_LEVEL3),		LOCK(KBDMAP_MODIFIER_LEVEL3),		}, "alock"		},
	{ {	LOCK(KBDMAP_MODIFIER_CAPS),		LOCK(KBDMAP_MODIFIER_CAPS),		}, "clock"		},
	{ {	LOCK(KBDMAP_MODIFIER_NUM),		LOCK(KBDMAP_MODIFIER_NUM),		}, "nlock"		},
	{ {	LOCK(KBDMAP_MODIFIER_SCROLL),		LOCK(KBDMAP_MODIFIER_SCROLL),		}, "slock"		},
	{ {	EXTE(EXTENDED_KEY_BACKTAB),		EXTE(EXTENDED_KEY_BACKTAB),		}, "btab"		},
	{ {	EXTE(EXTENDED_KEY_BACKSPACE),		EXTE(EXTENDED_KEY_BACKSPACE),		}, "bspace"		},	// This is an extension to the BSD format that allows use of the DEC VT switchable backspace mechanism.
	{ {	EXTE(EXTENDED_KEY_DELETE),		EXTE(EXTENDED_KEY_DELETE),		}, "delete"		},	// This is an extension to the BSD format that allows use of the XTerm switchable delete mechanism.
	{ {	EXTE(EXTENDED_KEY_RETURN_OR_ENTER),	EXTE(EXTENDED_KEY_RETURN_OR_ENTER),	}, "return"		},	// This is an extension to the BSD format that allows use of the DEC VT return key.
	{ {	EXTN(EXTENDED_KEY_PAD_ENTER),		EXTE(EXTENDED_KEY_PAD_ENTER),		}, "enter"		},	// This is an extension to the BSD format that allows use of the DEC VT enter key.
	{ {	EXTE(EXTENDED_KEY_IM_TOGGLE),		EXTE(EXTENDED_KEY_IM_TOGGLE),		}, "imsw"		},	// This is an extension to the BSD format that allows an IM toggle key.
	{ {	MMNT(KBDMAP_MODIFIER_1ST_GROUP2),	MMNT(KBDMAP_MODIFIER_1ST_GROUP2),	}, "g2shift"		},	// This is an extension to the BSD format that allows group 2 shift.
	{ {	LTCH(KBDMAP_MODIFIER_1ST_GROUP2),	LTCH(KBDMAP_MODIFIER_1ST_GROUP2),	}, "g2latch"		},	// This is an extension to the BSD format that allows group 2 latch.
	{ {	LOCK(KBDMAP_MODIFIER_1ST_GROUP2),	LOCK(KBDMAP_MODIFIER_1ST_GROUP2),	}, "g2lock"		},	// This is an extension to the BSD format that allows group 2 lock.
	{ {	CONS(CONSUMER_KEY_NEXT_TASK),		CONS(CONSUMER_KEY_NEXT_TASK),		}, "nscr"		},
	{ {	CONS(CONSUMER_KEY_PREVIOUS_TASK),	CONS(CONSUMER_KEY_PREVIOUS_TASK),	}, "pscr"		},
	{ {	CONS(CONSUMER_KEY_LOCK),		CONS(CONSUMER_KEY_LOCK),		}, "saver"		},
	{ {	SCRN( 0),				SCRN( 0),				}, "scr01"		},
	{ {	SCRN( 1),				SCRN( 1),				}, "scr02"		},
	{ {	SCRN( 2),				SCRN( 2),				}, "scr03"		},
	{ {	SCRN( 3),				SCRN( 3),				}, "scr04"		},
	{ {	SCRN( 4),				SCRN( 4),				}, "scr05"		},
	{ {	SCRN( 5),				SCRN( 5),				}, "scr06"		},
	{ {	SCRN( 6),				SCRN( 6),				}, "scr07"		},
	{ {	SCRN( 7),				SCRN( 7),				}, "scr08"		},
	{ {	SCRN( 8),				SCRN( 8),				}, "scr09"		},
	{ {	SCRN( 9),				SCRN( 9),				}, "scr10"		},
	{ {	SCRN(10),				SCRN(10),				}, "scr11"		},
	{ {	SCRN(11),				SCRN(11),				}, "scr12"		},
	{ {	SCRN(12),				SCRN(12),				}, "scr13"		},
	{ {	SCRN(13),				SCRN(13),				}, "scr14"		},
	{ {	SCRN(14),				SCRN(14),				}, "scr15"		},
	{ {	SCRN(15),				SCRN(15),				}, "scr16"		},
	// In a BSD keymap, these always imply the DEC VT numeric keypad Fn keys.
	{ {	EXTE(EXTENDED_KEY_PAD_F1),		EXTE(EXTENDED_KEY_PAD_F1),		}, "fkey01"		},
	{ {	EXTE(EXTENDED_KEY_PAD_F2),		EXTE(EXTENDED_KEY_PAD_F2),		}, "fkey02"		},
	{ {	EXTE(EXTENDED_KEY_PAD_F3),		EXTE(EXTENDED_KEY_PAD_F3),		}, "fkey03"		},
	{ {	EXTE(EXTENDED_KEY_PAD_F4),		EXTE(EXTENDED_KEY_PAD_F4),		}, "fkey04"		},
	{ {	EXTE(EXTENDED_KEY_PAD_F5),		EXTE(EXTENDED_KEY_PAD_F5),		}, "fkey05"		},
	// In a BSD keymap, these always imply the no modifiers behaviour.
	{ {	FUNK( 6),				FUNK( 6),				}, "fkey06"		},
	{ {	FUNK( 7),				FUNK( 7),				}, "fkey07"		},
	{ {	FUNK( 8),				FUNK( 8),				}, "fkey08"		},
	{ {	FUNK( 9),				FUNK( 9),				}, "fkey09"		},
	{ {	FUNK(10),				FUNK(10),				}, "fkey10"		},
	{ {	FUNK(11),				FUNK(11),				}, "fkey11"		},
	{ {	FUNK(12),				FUNK(12),				}, "fkey12"		},
	{ {	FUNK(13),				FUNK(13),				}, "fkey13"		},
	{ {	FUNK(14),				FUNK(14),				}, "fkey14"		},
	{ {	FUNK(15),				FUNK(15),				}, "fkey15"		},
	{ {	FUNK(16),				FUNK(16),				}, "fkey16"		},
	{ {	FUNK(17),				FUNK(17),				}, "fkey17"		},
	{ {	FUNK(18),				FUNK(18),				}, "fkey18"		},
	{ {	FUNK(19),				FUNK(19),				}, "fkey19"		},
	{ {	FUNK(20),				FUNK(20),				}, "fkey20"		},
	{ {	FUNK(21),				FUNK(21),				}, "fkey21"		},
	{ {	FUNK(22),				FUNK(22),				}, "fkey22"		},
	{ {	FUNK(23),				FUNK(23),				}, "fkey23"		},
	{ {	FUNK(24),				FUNK(24),				}, "fkey24"		},
	{ {	FUNK(25),				FUNK(25),				}, "fkey25"		},
	{ {	FUNK(26),				FUNK(26),				}, "fkey26"		},
	{ {	FUNK(27),				FUNK(27),				}, "fkey27"		},
	{ {	FUNK(28),				FUNK(28),				}, "fkey28"		},
	{ {	FUNK(29),				FUNK(29),				}, "fkey29"		},
	{ {	FUNK(30),				FUNK(30),				}, "fkey30"		},
	{ {	FUNK(31),				FUNK(31),				}, "fkey31"		},
	{ {	FUNK(32),				FUNK(32),				}, "fkey32"		},
	{ {	FUNK(33),				FUNK(33),				}, "fkey33"		},
	{ {	FUNK(34),				FUNK(34),				}, "fkey34"		},
	{ {	FUNK(35),				FUNK(35),				}, "fkey35"		},
	{ {	FUNK(36),				FUNK(36),				}, "fkey36"		},
	{ {	FUNK(37),				FUNK(37),				}, "fkey37"		},
	{ {	FUNK(38),				FUNK(38),				}, "fkey38"		},
	{ {	FUNK(39),				FUNK(39),				}, "fkey39"		},
	{ {	FUNK(40),				FUNK(40),				}, "fkey40"		},
	{ {	FUNK(41),				FUNK(41),				}, "fkey41"		},
	{ {	FUNK(42),				FUNK(42),				}, "fkey42"		},
	{ {	FUNK(43),				FUNK(43),				}, "fkey43"		},
	{ {	FUNK(44),				FUNK(44),				}, "fkey44"		},
	{ {	FUNK(45),				FUNK(45),				}, "fkey45"		},
	{ {	FUNK(46),				FUNK(46),				}, "fkey46"		},
	{ {	FUNK(47),				FUNK(47),				}, "fkey47"		},
	{ {	FUNK(48),				FUNK(48),				}, "fkey48"		},
	{ {	EXTN(EXTENDED_KEY_PAD_HOME),		EXTE(EXTENDED_KEY_HOME),		}, "fkey49"		},
	{ {	EXTN(EXTENDED_KEY_PAD_UP),		EXTE(EXTENDED_KEY_UP_ARROW),		}, "fkey50"		},
	{ {	EXTN(EXTENDED_KEY_PAD_PAGE_UP),		EXTE(EXTENDED_KEY_PAGE_UP),		}, "fkey51"		},
	{ {	EXTN(EXTENDED_KEY_PAD_MINUS),		EXTE(EXTENDED_KEY_PAD_MINUS),		}, "fkey52"		},
	{ {	EXTN(EXTENDED_KEY_PAD_LEFT),		EXTE(EXTENDED_KEY_LEFT_ARROW),		}, "fkey53"		},
	{ {	EXTN(EXTENDED_KEY_PAD_CENTRE),		EXTE(EXTENDED_KEY_CENTRE),		}, "fkey54"		},
	{ {	EXTN(EXTENDED_KEY_PAD_RIGHT),		EXTE(EXTENDED_KEY_RIGHT_ARROW),		}, "fkey55"		},
	{ {	EXTN(EXTENDED_KEY_PAD_PLUS),		EXTE(EXTENDED_KEY_PAD_PLUS),		}, "fkey56"		},
	{ {	EXTN(EXTENDED_KEY_PAD_END),		EXTE(EXTENDED_KEY_END),			}, "fkey57"		},
	{ {	EXTN(EXTENDED_KEY_PAD_DOWN),		EXTE(EXTENDED_KEY_DOWN_ARROW),		}, "fkey58"		},
	{ {	EXTN(EXTENDED_KEY_PAD_PAGE_DOWN),	EXTE(EXTENDED_KEY_PAGE_DOWN),		}, "fkey59"		},
	{ {	EXTN(EXTENDED_KEY_PAD_INSERT),		EXTE(EXTENDED_KEY_INSERT),		}, "fkey60"		},
	{ {	EXTN(EXTENDED_KEY_PAD_DELETE),		EXTE(EXTENDED_KEY_DELETE),		}, "fkey61"		},
	{ {	MMNT(KBDMAP_MODIFIER_1ST_SUPER),	MMNT(KBDMAP_MODIFIER_1ST_SUPER),	}, "fkey62"		},
	{ {	MMNT(KBDMAP_MODIFIER_2ND_SUPER),	MMNT(KBDMAP_MODIFIER_2ND_SUPER),	}, "fkey63"		},
	{ {	EXTE(EXTENDED_KEY_APPLICATION),		EXTE(EXTENDED_KEY_APPLICATION),		}, "fkey64"		},
	// In a BSD kbdmap "fkey", as can be seen above, does not necessarily mean function key.
	// So what "fkey65" et seq. mean is undefined and unguessable.
#if 0
	{ {	NOOP(0xFB5D41),				NOOP(0xFB5D41),				}, "fkey65"	},
	{ {	NOOP(0xFB5D42),				NOOP(0xFB5D42),				}, "fkey66"	},
	{ {	NOOP(0xFB5D43),				NOOP(0xFB5D43),				}, "fkey67"	},
	{ {	NOOP(0xFB5D44),				NOOP(0xFB5D44),				}, "fkey68"	},
	{ {	NOOP(0xFB5D45),				NOOP(0xFB5D45),				}, "fkey69"	},
	{ {	NOOP(0xFB5D46),				NOOP(0xFB5D46),				}, "fkey70"	},
	{ {	NOOP(0xFB5D47),				NOOP(0xFB5D47),				}, "fkey71"	},
	{ {	NOOP(0xFB5D48),				NOOP(0xFB5D48),				}, "fkey72"	},
	{ {	NOOP(0xFB5D49),				NOOP(0xFB5D49),				}, "fkey73"	},
	{ {	NOOP(0xFB5D4A),				NOOP(0xFB5D4A),				}, "fkey74"	},
	{ {	NOOP(0xFB5D4B),				NOOP(0xFB5D4B),				}, "fkey75"	},
	{ {	NOOP(0xFB5D4C),				NOOP(0xFB5D4C),				}, "fkey76"	},
	{ {	NOOP(0xFB5D4D),				NOOP(0xFB5D4D),				}, "fkey77"	},
	{ {	NOOP(0xFB5D4E),				NOOP(0xFB5D4E),				}, "fkey78"	},
	{ {	NOOP(0xFB5D4F),				NOOP(0xFB5D4F),				}, "fkey79"	},
	{ {	NOOP(0xFB5D50),				NOOP(0xFB5D50),				}, "fkey80"	},
	{ {	NOOP(0xFB5D51),				NOOP(0xFB5D51),				}, "fkey81"	},
	{ {	NOOP(0xFB5D52),				NOOP(0xFB5D52),				}, "fkey82"	},
	{ {	NOOP(0xFB5D53),				NOOP(0xFB5D53),				}, "fkey83"	},
	{ {	NOOP(0xFB5D54),				NOOP(0xFB5D54),				}, "fkey84"	},
	{ {	NOOP(0xFB5D55),				NOOP(0xFB5D55),				}, "fkey85"	},
	{ {	NOOP(0xFB5D56),				NOOP(0xFB5D56),				}, "fkey86"	},
	{ {	NOOP(0xFB5D57),				NOOP(0xFB5D57),				}, "fkey87"	},
	{ {	NOOP(0xFB5D58),				NOOP(0xFB5D58),				}, "fkey88"	},
	{ {	NOOP(0xFB5D59),				NOOP(0xFB5D59),				}, "fkey89"	},
	{ {	NOOP(0xFB5D5A),				NOOP(0xFB5D5A),				}, "fkey90"	},
	{ {	NOOP(0xFB5D5B),				NOOP(0xFB5D5B),				}, "fkey91"	},
	{ {	NOOP(0xFB5D5C),				NOOP(0xFB5D5C),				}, "fkey92"	},
	{ {	NOOP(0xFB5D5D),				NOOP(0xFB5D5D),				}, "fkey93"	},
	{ {	NOOP(0xFB5D5E),				NOOP(0xFB5D5E),				}, "fkey94"	},
	{ {	NOOP(0xFB5D5F),				NOOP(0xFB5D5F),				}, "fkey95"	},
	{ {	NOOP(0xFB5D60),				NOOP(0xFB5D60),				}, "fkey96"	},
	{ {	NOOP(0xFB5D61),				NOOP(0xFB5D61),				}, "fkey97"	},
	{ {	NOOP(0xFB5D62),				NOOP(0xFB5D62),				}, "fkey98"	},
	{ {	NOOP(0xFB5D63),				NOOP(0xFB5D63),				}, "fkey99"	},
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
	const bool pc_at_addition,
	const std::string & s
) {
	if (s.length() == 3 && '\'' == s[0] && '\'' == s[2]) return UCSA(s[1]);
	if (s.length() > 2 && hexprefix(s.substr(0, 2))) return UCSA(hexval(s.substr(2, s.npos)));
	if (s.length() > 1 && s[0] == '0') return UCSA(octval(s.substr(1, s.npos)));
	if (s.length() > 0 && is_decimal(s)) return UCSA(val(s));
	const bsd_kbdmap_item * e(bsd_actions + sizeof bsd_actions/sizeof *bsd_actions);
	for (const bsd_kbdmap_item * p(bsd_actions); p <  e; ++p) {
		if (p->name == s) 
			return p->action[pc_at_addition ? 1U : 0U];
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
	kbdmap_entry keyboard[KBDMAP_ROWS][KBDMAP_COLS],
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
		unsigned keycode(val(kc));
		const bool level3(keycode & 0x80);
		keycode &= ~0x80;
		const bool pc_at_addition(0x5E <= keycode);

		bool seen_scrn(false), is_all_nop(true);
		uint32_t cmds[8] = { 0 };
		for (unsigned j(0U); j < sizeof cmds/sizeof *cmds; ++j) {
			const std::string as(strip_field(line));
			cmds[j] = action(prog, name, ln, pc_at_addition, as);
			seen_scrn |= KBDMAP_ACTION_SCREEN == (cmds[j] & KBDMAP_ACTION_MASK);
			is_all_nop &= 0x00000000 == (cmds[j] & KBDMAP_ACTION_MASK);
		}
		const std::string l(strip_field(line));

		if (keycode >= 0x100) {
			if (!seen_impossible)
				std::fprintf(stderr, "%s: WARNING: %s(%lu): %u: %s\n", prog, name, ln, keycode, "Impossible 8-bit keycode.");
			seen_impossible = true;
			continue;
		}
		// Silently ignore attempts to map a keycode that does not occur so that our own NOP mapping for the first entry remains untouched.
		if (0x00 == keycode) continue;	
		const uint16_t index(bsd_keycode_to_keymap_index(keycode));
		if (0xFFFF == index) {
			if (!is_all_nop)
				std::fprintf(stderr, "%s: WARNING: %s(%lu): %u: %s\n", prog, name, ln, keycode, "Un-convertable undefined keycode.");
			continue;
		}
		kbdmap_entry & e(keyboard[index >> 8U][index & 0x0F]);

		if (!level3) {
			for (unsigned j(0U); j < sizeof cmds/sizeof *cmds; ++j) 
				e.p[j] = cmds[j];
		} else
		if (!seen_scrn) {
			for (unsigned j(0U); j < 4U; ++j) 
				e.p[j + 4U] = cmds[j];
		}

		if ("B" == l || "N" == l)
			e.cmd = 'n';
		else
		if ("C" == l)
			e.cmd = 'c';
		else
		if (seen_scrn)
			e.cmd = 'f';
		else
			e.cmd = 's';

	}
	return true;
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
		popt::top_table_definition main_option(0, 0, "Main options", "[file(s)...]");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw static_cast<int>(EXIT_USAGE);
	}
	KeyboardMap map;
	set_default(map);
	if (args.empty()) {
		if (!process(map, prog, "<stdin>", std::cin))
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
			if (!process(map, prog, name, ifs))
				throw static_cast<int>(EXIT_TEMPORARY_FAILURE);	// Bernstein daemontools compatibility
		}
	}
	overlay_group2_latch(map);
	for (unsigned i(0U); i < KBDMAP_ROWS; ++i)
		for (unsigned j(0U); j < KBDMAP_COLS; ++j) {
			const kbdmap_entry & e(map[i][j]);
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
