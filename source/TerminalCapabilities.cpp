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
	use_DECSTR(false),
	use_DECST8C(false),
	use_DECLocator(false),
	has_XTerm1006Mouse(true),
	use_NEL(true),
	use_RI(true),
	use_IND(true),
	use_CTC(false),
	use_HPA(false),
	use_DECSNLS(false),
	use_DECSCPP(true),
	use_DECSLRM(true),
	has_DTTerm_DECSLPP_extensions(false),
	pending_wrap(true),
	linux_editing_keypad(false),
	interix_function_keys(false),
	sco_function_keys(false),
	rxvt_function_keys(false),
	reset_sets_tabs(false),
	has_invisible(true),
	has_reverse_off(true),
	has_square_mode(false)
{
	const char * term(envs.query("TERM"));
	const char * colorterm(envs.query("COLORTERM"));
	const char * term_program(envs.query("TERM_PROGRAM"));
	const char * vte_version_env(envs.query("VTE_VERSION"));
	const char * xterm_version(envs.query("XTERM_VERSION"));
	const long vte_version = vte_version_env ? std::strtol(vte_version_env, NULL, 10) : 0;
	const bool iterm_env = term_program && std::strstr(term_program, "iTerm.app");
	const bool konsole = !!envs.query("KONSOLE_PROFILE_NAME") || !!envs.query("KONSOLE_DBUS_SESSION");
	const bool roxterm = !!envs.query("ROXTERM_ID") || !!envs.query("ROXTERM_NUM") || !!envs.query("ROXTERM_PID");
	const bool tmux_env = !!envs.query("TMUX");
	const bool true_kvt = !(xterm_version || (vte_version > 0) || colorterm);

	// Our default is to assume terminals are at least ECMA-48 from 1976, rather than TTY-37 from 1968.  Progress!
	if (!term) term = "ansi";

#define is_term_family(term, family) (0 == std::strncmp((term), (family), sizeof(family) - 1))

	const bool dumb = is_term_family(term, "dumb");
	const bool dtterm = is_term_family(term, "dtterm");
	const bool xterm = is_term_family(term, "xterm");
	const bool linuxvt = is_term_family(term, "linux");	// "linux" is a macro.
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
	const bool tmux_pretending_screen = screen && tmux_env;

	if (dumb)
	{
		colour_level = NO_COLOURS;
	} else
  	if (vte_version >= 3600			// per GNOME bug #685759
	||  iterm || iterm_pretending_xterm	// per analysis of VT100Terminal.m
	||  (permit_fake_truecolour && true_xterm)
	) {
		colour_level = ISO_DIRECT_COLOUR;
	} else
	if ((!true_kvt && (teken || linuxvt))
	// Linux 4.8+ supports true-colour SGR, which we use in preference to 256-colour SGR.
	||  (permit_fake_truecolour && linuxvt)
	// per http://lists.schmorp.de/pipermail/rxvt-unicode/2016q2/002261.html
	||  (permit_fake_truecolour && rxvt)
	||  konsole	// per commentary in VT102Emulation.cpp
	||  st		// per experimentation
	||  gnome	// per experimentation
	||  roxterm	// per experimentation
  	||  (colorterm && (0 == std::strcmp(colorterm, "truecolor") || 0 == std::strcmp(colorterm, "24bit")))
	) {
		colour_level = DIRECT_COLOUR_FAULTY;
	} else
	if (xterm	// Forged XTerms or real XTerm without fake truecolour
	||  putty
	||  rxvt	// Fallback when fakery is disallowed
	||  linuxvt	// Fallback when fakery is disallowed
	||  teken
	||  tmux || tmux_pretending_screen
	||  (colorterm && std::strstr(colorterm, "256"))
	||  (term && std::strstr(term, "256"))
	) {
		colour_level = INDEXED_COLOUR_FAULTY;
	} else
	if (colorterm
	) {
		colour_level = ECMA_16_COLOURS;
	} else
	if (interix
	||  pcvt
	||  cons
	) {
		colour_level = ECMA_8_COLOURS;
	}

	if (false
        // Allows forcing the use of DECSCUSR on KVT-compatible terminals that do indeed implement the xterm extension:
	||  (!true_kvt && (teken || linuxvt))
	) {
		// These have an extended version that has a vertical bar, a box, and a star.
		cursor_shape_command = EXTENDED_DECSCUSR;
	} else
	if (true_xterm	// per xterm ctlseqs doco (since version 282)
        ||  rxvt	// per command.C
        ||  iterm || iterm_pretending_xterm	// per analysis of VT100Terminal.m
        ||  teraterm	// per TeraTerm "Supported Control Functions" doco
	) {
		// xterm has an extended version that has a vertical bar.
		cursor_shape_command = XTERM_DECSCUSR;
	} else
	if (putty		// per MinTTY 0.4.3-1 release notes from 2009
	||  vte_version >= 3900	// per https://bugzilla.gnome.org/show_bug.cgi?id=720821
        ||  tmux		// per tmux manual page and per https://lists.gnu.org/archive/html/screen-devel/2013-03/msg00000.html
	||  screen
	||  roxterm
	) {
		cursor_shape_command = ORIGINAL_DECSCUSR;
	} else
	if (linuxvt) {
		// Linux uses an idiosyncratic escape code to set the cursor shape and does not support DECSCUSR.
		cursor_shape_command = LINUX_SCUSR;
	}

	if (teken		// The teken library does not do anything, but does handle the control sequence properly.
        // Allows forcing the use of DECSTR on KVT-compatible terminals that do indeed implement DEC soft reset:
	||  (!true_kvt && (teken || linuxvt))
	) {
		use_DECSTR = true;
	}
	
	if (true_xterm
        // Allows forcing the use of DECELR on KVT-compatible terminals that do indeed implement location reports:
	||  (!true_kvt && (teken || linuxvt))
	) {
		use_DECLocator = true;
	}
	
	if (false		// XTerm does not handle the control sequence properly.
        // Allows forcing the use of DECELR on KVT-compatible terminals that do indeed implement location reports:
	||  (!true_kvt && (teken || linuxvt))
	) {
		use_DECST8C = true;
	}

	if (dumb
	||  rxvt
	||  (xterm && !true_xterm && vte_version <= 0 && !konsole)
	) {
		has_XTerm1006Mouse = false;
	}

	if (true_xterm
        // Allows forcing the use of HPA on KVT-compatible terminals that do indeed implement the control sequence:
	||  (!true_kvt && (teken || linuxvt))
	) {
		use_HPA = true;
	}

	if (true_xterm
        // Allows forcing the use of CTC on KVT-compatible terminals that do indeed implement the control sequence:
	||  (!true_kvt && (teken || linuxvt))
	) {
		use_CTC = true;
	}

	if (true_xterm
        // Allows forcing the use of DECSNLS on KVT-compatible terminals that do indeed implement the control sequence:
	||  (!true_kvt && (teken || linuxvt))
	) {
		use_DECSNLS = true;
	}

	// Actively inhibt these only on those terminals known to actually mishandle them rather than just ignore them.
	if (dumb
	||  interix
	||  roxterm
	||  vte_version > 0
	) {
		use_DECSCPP = false;
		use_DECSLRM = false;
	}

	if (dtterm		// originated this extension
	||  vte_version >= 3900	//
	||  xterm		// per xterm ctlseqs doco
	||  konsole		// per commentary in VT102Emulation.cpp
	||  teraterm		// per TeraTerm "Supported Control Functions" doco
	||  rxvt		// per command.C
	) {
		has_DTTerm_DECSLPP_extensions = true;
	}

	if (dumb
	||  (true_kvt && (teken || linuxvt))
	) {
		has_invisible = false;
	}

	if (dumb
	||  interix
	||  (xterm && !true_xterm)
	) {
		use_NEL = false;
	}

	if (dumb
	||  interix
	) {
		use_DECPrivateMode = false;
		use_RI = false;
		use_IND = false;
		has_reverse_off = false;
	}

	if (dumb
	||  interix
	||  cygwin
	) {
		pending_wrap = false;
	}

	if (linuxvt)
		linux_editing_keypad = true;

	if (rxvt)
		rxvt_function_keys = true;

	if (interix)
		interix_function_keys = true;

	if (pcvt
	||  cons
	||  teken
	) {
		sco_function_keys = true;
	}

	if (teken
	||  linuxvt
	||  xterm
	||  rxvt
	) {
		reset_sets_tabs = true;
	}

	if (!true_kvt && (teken || linuxvt))
		has_square_mode = true;
}
