/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <cstring>
#include <cstdlib>
#include "ProcessEnvironment.h"
#include "TerminalCapabilities.h"

bool TerminalCapabilities::permit_fake_truecolour(false);

TerminalCapabilities::TerminalCapabilities(
	const ProcessEnvironment & envs
) :
	colour_level(ECMA_8_COLOURS),
	cursor_shape_command(NO_SCUSR),
	use_DECPrivateMode(true),
	use_DECLocator(false),
	has_XTermSGRMouse(true),
	use_NEL(true),
	use_RI(true),
	use_IND(true),
	pending_wrap(true),
	linux_editing_keypad(false),
	interix_function_keys(false),
	sco_function_keys(false),
	rxvt_function_keys(false),
	has_invisible(true),
	has_reverse_off(true)
{
	const char * term(envs.query("TERM"));
	const char * colorterm(envs.query("COLORTERM"));
	const char * term_program(envs.query("TERM_PROGRAM"));
	const char * vte_version_env(envs.query("VTE_VERSION"));
	const char * xterm_version(envs.query("XTERM_VERSION"));
	const long vte_version = vte_version_env ? std::strtol(vte_version_env, NULL, 10) : 0;
	const bool iterm_env = term_program && std::strstr(term_program, "iTerm.app");
	const bool konsole = !!envs.query("KONSOLE_PROFILE_NAME") || !!envs.query("KONSOLE_DBUS_SESSION");

#define is_term_family(term, family) (0 == std::strncmp((term), (family), sizeof(family) - 1))

	const bool xterm = is_term_family(term, "xterm");
	const bool linuxvt = is_term_family(term, "linux");
	const bool cons = is_term_family(term, "cons");
	const bool teken = is_term_family(term, "teken");
	const bool pcvt = is_term_family(term, "pcvt");
#if 0 // Not yet needed.
	const bool pccon = is_term_family(term, "pccon");
#endif
	const bool interix = is_term_family(term, "interix");
	const bool cygwin = is_term_family(term, "cygwin");
	const bool rxvt = is_term_family(term, "rxvt");
	const bool teraterm = is_term_family(term, "teraterm");
	const bool putty = is_term_family(term, "putty");
	const bool screen = is_term_family(term, "screen");
	const bool tmux = is_term_family(term, "tmux");
	const bool st = is_term_family(term, "st");
	const bool gnome = is_term_family(term, "gnome") || is_term_family(term, "vte");
	const bool iterm = is_term_family(term, "iterm") || is_term_family(term, "iTerm.app");
	const bool iterm_pretending_xterm = xterm && iterm_env;
	const bool true_xterm = xterm && !!xterm_version;
	const bool tmux_pretending_screen = screen && !!envs.query("TMUX");

	if (linuxvt && !(xterm_version || (vte_version > 0) || colorterm)) {
		linux_editing_keypad = true;
		has_invisible = false;
	}
	if (teken && !(xterm_version || (vte_version > 0) || colorterm)) {
		has_invisible = false;
	}

  	if (vte_version >= 3600			// per GNOME bug #685759
	||  iterm || iterm_pretending_xterm	// per analysis of VT100Terminal.m
	||  (permit_fake_truecolour && true_xterm)
	) {
		colour_level = ISO_DIRECT_COLOUR;
	} else
	if (linuxvt	// Linux 4.8+ supports true-colour SGR, which we use in preference to 256-colour SGR.
	||  konsole	// per commentary in VT102Emulation.cpp
	// per http://lists.schmorp.de/pipermail/rxvt-unicode/2016q2/002261.html
	||  (permit_fake_truecolour && rxvt)
	||  st		// per experimentation
	||  gnome	// per experimentation
  	||  (colorterm && (0 == std::strcmp(colorterm, "truecolor") || 0 == std::strcmp(colorterm, "24bit")))
	) {
		colour_level = DIRECT_COLOUR_FAULTY;
	} else
	if (xterm	// Forged XTerms or real XTerm without fake truecolour
	||  putty
	||  rxvt	// Fallback
	||  teken
	||  tmux || tmux_pretending_screen
	||  (colorterm && std::strstr(colorterm, "256"))
	||  (term && std::strstr(term, "256"))
	) {
		colour_level = INDEXED_COLOUR_FAULTY;
	} else
	if (colorterm) {
		colour_level = ECMA_16_COLOURS;
	} else
	if (interix
	||  pcvt
	||  cons
	) {
		colour_level = ECMA_8_COLOURS;
	}

	if (true_xterm	// per xterm ctlseqs doco (since version 282)
        ||  rxvt	// per command.C
        ||  iterm || iterm_pretending_xterm	// per analysis of VT100Terminal.m
        ||  teraterm	// per TeraTerm "Supported Control Functions" doco
        // Allows forcing the use of DECSCUSR on linux type terminals that do indeed implement the xterm extension:
        ||  (linuxvt && (xterm_version || (vte_version > 0) || colorterm))
        // Allows forcing the use of DECSCUSR on teken type terminals that do indeed implement the xterm extension:
        ||  (teken && (xterm_version || (vte_version > 0) || colorterm))
	) {
		// xterm has an extended version that has a vertical bar.
		cursor_shape_command = EXTENDED_DECSUSR;
	} else
	if (putty		// per MinTTY 0.4.3-1 release notes from 2009
	||  vte_version >= 3900	// per https://bugzilla.gnome.org/show_bug.cgi?id=720821
        ||  tmux		// per tmux manual page and per https://lists.gnu.org/archive/html/screen-devel/2013-03/msg00000.html
	||  screen
	) {
		cursor_shape_command = ORIGINAL_DECSUSR;
	} else
	if (linuxvt) {
		// Linux uses an idiosyncratic escape code to set the cursor shape and does not support DECSCUSR.
		cursor_shape_command = LINUX_SCUSR;
	}

	if (true_xterm
        // Allows forcing the use of DECELR on linux type terminals that do indeed implement location reports:
        ||  (linuxvt && (xterm_version || (vte_version > 0) || colorterm))
        // Allows forcing the use of DECELR on teken type terminals that do indeed implement location reports:
        ||  (teken && (xterm_version || (vte_version > 0) || colorterm))
	) {
		use_DECLocator = true;
	}
	
	if (rxvt
	||  (xterm && !true_xterm && vte_version <= 0 && !konsole)
	) {
		has_XTermSGRMouse = false;
	}
	if (rxvt)
		rxvt_function_keys = true;

	if (interix) {
		use_DECPrivateMode = false;
		use_NEL = false;
		use_RI = false;
		use_IND = false;
		interix_function_keys = true;
		has_reverse_off = false;
	}

	if (interix || cygwin) {
		pending_wrap = false;
	}

	if (pcvt
	||  cons
	) {
		sco_function_keys = true;
	}
}
