/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_CONTROLCHARACTERS_H)
#define INCLUDE_CONTROLCHARACTERS_H

	enum {
		NUL = '\0',
		SOH = 0x01,
		STX = 0x02,
		ETX = 0x03,
		EOT = 0x04,
		ENQ = 0x05,
		ACK = 0x06,
		BEL = 0x07,
		BS = 0x08,
		TAB = 0x09,
		LF = 0x0a,
		VT = 0x0b,
		FF = 0x0c,
		CR = 0x0d,
		SO = 0x0e,
		CAN = 0x18,
		EM = 0x19,
		SUB = 0x1a,
		ESC = 0x1b,
		FS = 0x1c,
		GS = 0x1d,
		RS = 0x1e,
		US = 0x1f,
		SPC = 0x20,
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
