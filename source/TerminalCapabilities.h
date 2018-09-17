/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_TERMINALCAPABILITIES_H)
#define INCLUDE_TERMINALCAPABILITIES_H

struct ProcessEnvironment;

class TerminalCapabilities {
public:
	TerminalCapabilities(const ProcessEnvironment &);
	static bool permit_fake_truecolour;
	enum { ECMA_8_COLOURS, ECMA_16_COLOURS, INDEXED_COLOUR_FAULTY, ISO_INDEXED_COLOUR, DIRECT_COLOUR_FAULTY, ISO_DIRECT_COLOUR } colour_level;
	enum { NO_SCUSR, ORIGINAL_DECSUSR, EXTENDED_DECSUSR, LINUX_SCUSR } cursor_shape_command;
	bool use_DECPrivateMode, use_DECLocator, has_XTermSGRMouse, use_NEL, use_RI, use_IND, pending_wrap, linux_editing_keypad, interix_function_keys, sco_function_keys, rxvt_function_keys, has_invisible, has_reverse_off;
};

#endif
