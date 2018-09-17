/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_CONTROLCHARACTERS_H)
#define INCLUDE_CONTROLCHARACTERS_H

	enum {
		NUL = '\0',
		BEL = '\a',
		CR = '\r',
		LF = '\n',
		VT = '\v',
		TAB = '\t',
		FF = '\f',
		BS = '\b',
		CAN = 0x18,
		ESC = 0x1b,
		DEL = 0x7f,
		IND = 0x84,
		NEL = 0x85,
		SSA = 0x86,
		HTS = 0x88,
		RI = 0x8d,
		SS2 = 0x8e,
		SS3 = 0x8f,
		DCS = 0x90,
		SOS = 0x98,
		CSI = 0x9b,
		ST = 0x9c,
		OSC = 0x9d,
		PM = 0x9e,
		APC = 0x9f,
	};

#endif
