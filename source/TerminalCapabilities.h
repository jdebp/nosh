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
	enum { NO_COLOURS, ECMA_8_COLOURS, ECMA_16_COLOURS, INDEXED_COLOUR_FAULTY, ISO_INDEXED_COLOUR, DIRECT_COLOUR_FAULTY, ISO_DIRECT_COLOUR } colour_level;
	enum { NO_SCUSR, ORIGINAL_DECSCUSR, XTERM_DECSCUSR, EXTENDED_DECSCUSR, LINUX_SCUSR } cursor_shape_command;
	bool use_DECPrivateMode, use_DECSTR, use_DECST8C, use_DECLocator, has_XTerm1006Mouse, use_NEL, use_RI, use_IND, use_CTC, use_HPA, use_DECSNLS, use_DECSCPP, use_DECSLRM, has_DTTerm_DECSLPP_extensions, pending_wrap, linux_editing_keypad, interix_function_keys, sco_function_keys, rxvt_function_keys, reset_sets_tabs, has_invisible, has_reverse_off, has_square_mode;
};

#endif
