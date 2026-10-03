/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <cstring>
#include <cstdlib>
#include "ProcessEnvironment.h"
#include "DefaultEnvironment.h"
#include "TerminalCapabilities.h"

bool TerminalCapabilities::permit_fake_truecolour(false);

namespace {

	inline
	bool
	is_prefix (
		const char * term,
		const char * prefix,
		std::size_t len
	) {
		return	len <= std::strlen(term) &&
			0 == std::memcmp(term, prefix, len) &&
			('\0' == term[len] || '-' == term[len])
		;
	}

	inline
	bool
	has_suffix (
		const char * term,
		const char * suffix,
		std::size_t len
	) {
		for (;;) {
			const char * minus(std::strchr(term, '-'));
			if (!minus) return false;
			term = minus + 1;
			if (is_prefix(term, suffix, len)) return true;
		}
	}

}

TerminalCapabilities::TerminalCapabilities(
	const ProcessEnvironment & envs
) :
	colour_level(ECMA_8_COLOURS),
	cursor_shape_command(NO_SCUSR),
	lacks_pending_wrap(false),
	lacks_NEL(false),
	lacks_RI(false),
	lacks_IND(false),
	lacks_CTC(true),
	lacks_HPA(true),
	lacks_REP(true),
	lacks_invisible(false),
	lacks_strikethrough(false),
	lacks_reverse_off(false),
	faulty_reverse_video(false),
	faulty_inverse_erase(true),
	faulty_SP_REP(false),
	linux_editing_keypad(false),
	interix_function_keys(false),
	teken_function_keys(false),
	sco_function_keys(false),
	rxvt_function_keys(false),
	linux_function_keys(false),
	use_DECPrivateMode(true),
	use_SCOPrivateMode(true),
	use_DECSTR(false),
	use_DECST8C(false),
	use_DECLocator(false),
	use_DECSNLS(false),
	use_DECSCPP(true),
	use_DECSLRM(true),
	use_DECNKM(true),
	has_DECECM(false),
	initial_DECECM(false),
	has_DTTerm_DECSLPP_extensions(false),
	has_XTerm1006Mouse(true),
	has_square_mode(false),
	reset_sets_tabs(false),
	has_extended_underline(false)
{
	const char * term(envs.query("TERM"));
	const char * colorterm(envs.query("COLORTERM"));
	const char * term_program(envs.query("TERM_PROGRAM"));
	const char * vte_version_env(envs.query("VTE_VERSION"));
	const char * xterm_version(envs.query("XTERM_VERSION"));
	const long vte_version = vte_version_env ? std::strtol(vte_version_env, nullptr, 10) : 0;
	const bool iterm_env = term_program && std::strstr(term_program, "iTerm.app");
	const bool konsole = !!envs.query("KONSOLE_PROFILE_NAME") || !!envs.query("KONSOLE_DBUS_SESSION");
	const bool roxterm = !!envs.query("ROXTERM_ID") || !!envs.query("ROXTERM_NUM") || !!envs.query("ROXTERM_PID");
	const bool tmux_env = !!envs.query("TMUX");
	const bool true_kvt = !(xterm_version || (vte_version > 0) || colorterm);

	if (!term) term = DefaultEnvironment::TERM;

#define is_term_family(term, family) is_prefix((term),(family),sizeof(family) - 1)
#define has_term_feature(term, feature) has_suffix((term),(feature),sizeof(feature) - 1)

	const bool dumb = is_term_family(term, "dumb");
	const bool dtterm = is_term_family(term, "dtterm");
	const bool xterm = is_term_family(term, "xterm");
	const bool aixterm = is_term_family(term, "aixterm");
	const bool linuxvt = is_term_family(term, "linux");	// "linux" is a macro.
	const bool cons = is_term_family(term, "cons25") || is_term_family(term, "cons50");
	const bool teken = is_term_family(term, "teken");
	const bool pcvt = is_term_family(term, "pcvtXX") || is_term_family(term, "pcvt25") || is_term_family(term, "pcvt50");
	const bool wsvt = is_term_family(term, "wsvt25");
	const bool netbsd6 = is_term_family(term, "netbsd6");
#if 0 // Not yet needed.
	const bool pccon = is_term_family(term, "pccon");
#endif
	const bool interix = is_term_family(term, "interix");
	const bool cygwin = is_term_family(term, "cygwin");
	const bool rxvt = is_term_family(term, "rxvt");
	const bool teraterm = is_term_family(term, "teraterm");
	const bool putty = is_term_family(term, "putty");
	const bool msterminal = is_term_family(term, "ms-terminal");
	const bool kitty = is_term_family(term, "kitty");
	const bool screen = is_term_family(term, "screen");
	const bool tmux = is_term_family(term, "tmux");
	const bool st = is_term_family(term, "st");
	const bool gnome = is_term_family(term, "gnome") || is_term_family(term, "vte");
	const bool iterm = is_term_family(term, "iterm") || is_term_family(term, "iTerm.app");
	const bool iterm_pretending_xterm = xterm && iterm_env;
	const bool true_xterm = xterm && !!xterm_version;
	const bool tmux_pretending_screen = screen && tmux_env;
	const bool user_space_vt = !true_kvt && (teken || linuxvt || netbsd6);

	// *********************************************************************
	// colour abilities

	{

		if (dumb)
		{
			colour_level = NO_COLOURS;
		} else
		if (interix
		||  pcvt
		||  wsvt
		||  (true_kvt && netbsd6)
		||  cons
		) {
			colour_level = ECMA_8_COLOURS;
		} else
		if (vte_version >= 3600			// per GNOME bug #685759
		||  iterm || iterm_pretending_xterm	// per analysis of VT100Terminal.m
		||  (permit_fake_truecolour && true_xterm)
		||  user_space_vt
		) {
			colour_level = ISO_DIRECT_COLOUR;
		} else
		if (false
		// Linux 4.8+ supports true-colour SGR, which we use in preference to 256-colour SGR.
		||  (permit_fake_truecolour && linuxvt)
		// per http://lists.schmorp.de/pipermail/rxvt-unicode/2016q2/002261.html
		||  (permit_fake_truecolour && rxvt)
		||  konsole	// per commentary in VT102Emulation.cpp
		||  st		// per experimentation
		||  gnome	// per experimentation
		||  roxterm	// per experimentation
		||  msterminal	// per experimentation
		||  (colorterm && (has_term_feature(colorterm, "truecolor") || has_term_feature(colorterm, "24bit")))
		||  (term && (has_term_feature(term, "24bit") || has_term_feature(term, "truecolor")))
		) {
			colour_level = DIRECT_COLOUR_FAULTY;
		} else
		if (xterm	// Forged XTerms or real XTerm without fake truecolour
		||  putty
		||  rxvt	// Fallback when fakery is disallowed
		||  linuxvt	// Fallback when fakery is disallowed
		||  teken
		||  tmux || tmux_pretending_screen
		||  (colorterm && has_term_feature(colorterm, "256color"))
		||  (term && has_term_feature(term, "256color"))
		) {
			colour_level = INDEXED_COLOUR_FAULTY;
		} else
		if (colorterm
		||  aixterm
		) {
			colour_level = ECMA_16_COLOURS;
		}

	}

	// *********************************************************************
	// cursor glyph abilities and mechanisms

	{

		if (false
		// Allows forcing the use of DECSCUSR on KVT-compatible terminals that do indeed implement the xterm extension:
		||  user_space_vt
		) {
			// These have an extended version that has a vertical bar, a box, a star, under+over line, and a mirror L-shape.
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
		||  msterminal
		) {
			cursor_shape_command = ORIGINAL_DECSCUSR;
		} else
		if (linuxvt) {
			// Linux uses an idiosyncratic escape code to set the cursor shape and does not support DECSCUSR.
			cursor_shape_command = LINUX_SCUSR;
		}

	}

	// *********************************************************************
	// standards non-conformance, deficiencies, and bugs

	{

		if (dumb
		||  interix
		||  cygwin
		) {
			lacks_pending_wrap = true;
		}

		if (true_xterm
		// Allows forcing the use of HPA on KVT-compatible terminals that do indeed implement the control sequence:
		||  user_space_vt
		) {
			lacks_HPA = false;
		}

		if (true_xterm
		// Allows forcing the use of CTC on KVT-compatible terminals that do indeed implement the control sequence:
		||  user_space_vt
		) {
			lacks_CTC = false;
		}

		if (dumb
		||  user_space_vt
		) {
			lacks_invisible = true;
		}

		if (dumb
		||  konsole
		) {
			lacks_strikethrough = true;
		}

		if (dumb
		||  interix
		||  (xterm && !true_xterm)
		) {
			lacks_NEL = true;
		}

		if (dumb
		||  interix
		) {
			lacks_RI = true;
			lacks_IND = true;
			lacks_reverse_off = true;
		}

		if (true_xterm
		||  putty
		||  msterminal
		||  user_space_vt
		)
			lacks_REP = false;

		if (rxvt) {
			// rxvt erroneously uses the default colour if background and foreground colours are the same and reverse video is turned on.
			faulty_reverse_video = true;
		}

		if (putty
		||  konsole
		||  msterminal
		||  user_space_vt
		)
			faulty_inverse_erase = false;

		if (msterminal)
			faulty_SP_REP = true;

		if (linuxvt) {
			linux_editing_keypad = true;
			linux_function_keys = true;
		}

		if (rxvt)
			rxvt_function_keys = true;

		if (interix)
			interix_function_keys = true;

		if (pcvt
		||  wsvt
		||  netbsd6
		||  cons
		||  teken
		) {
			teken_function_keys = true;
		}

	}

	// *********************************************************************
	// DEC and other private modes and control sequences

	{

		if (dumb
		||  interix
		) {
			use_DECPrivateMode = false;
			use_SCOPrivateMode = false;
		}

		if (teken		// The teken library does not do anything, but does handle the control sequence properly.
		// Allows forcing the use of DECSTR on KVT-compatible terminals that do indeed implement DEC soft reset:
		||  user_space_vt
		) {
			use_DECSTR = true;
		}

		if (true_xterm
		// Allows forcing the use of DECELR on KVT-compatible terminals that do indeed implement location reports:
		||  user_space_vt
		) {
			use_DECLocator = true;
		}

		if (false		// XTerm does not handle the control sequence properly.
		// Allows forcing the use of DECELR on KVT-compatible terminals that do indeed implement location reports:
		||  user_space_vt
		) {
			use_DECST8C = true;
		}

		if (true_xterm
		// Allows forcing the use of DECSNLS on KVT-compatible terminals that do indeed implement the control sequence:
		||  user_space_vt
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

		// Actively inhibt this only on those terminals known to not support DECCKM.
		if (putty
		||  msterminal
		) {
			use_DECNKM = false;
		}

		if (user_space_vt) {
			has_DECECM = true;
		}
		if (vte_version > 0) {
			initial_DECECM = true;
		}

		if (dtterm		// originated this extension
		||  vte_version >= 3900	//
		||  xterm		// per xterm ctlseqs doco
		||  konsole		// per commentary in VT102Emulation.cpp
		||  teraterm		// per TeraTerm "Supported Control Functions" doco
		||  rxvt		// per command.C
		// Allows forcing the use of DTTerm DECSLPP extensions on KVT-compatible terminals that do indeed implement the control sequence:
		||  user_space_vt
		) {
			has_DTTerm_DECSLPP_extensions = true;
		}

		if (dumb
		||  rxvt
		||  (xterm && !true_xterm && vte_version <= 0 && !konsole)
		) {
			has_XTerm1006Mouse = false;
		}

		if (user_space_vt
		||  (term && has_term_feature(term, "square"))
		) {
			has_square_mode = true;
		}

	}

	// *********************************************************************
	// other augmentations

	{

		if (teken
		||  linuxvt
		||  xterm
		||  rxvt
		) {
			reset_sets_tabs = true;
		}

		// See https://github.com/kovidgoyal/kitty/issues/226
		// rxvt copes very badly with extended underline attributes, as it does not handle subparameters properly.
		if (kitty
		||  gnome
		||  user_space_vt
		) {
			has_extended_underline = true;
		}
	}
}
