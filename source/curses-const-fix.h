/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

// The XPG definition for the curses API was not const-correct.
// A const-correct API is a good thing.
//
// Some curses implementations, like pdcurses, simply choose const correctness outright.
// Dickey ncurses has, since 1997, come with a configuration option where one can choose for it to be const-correct or XPG compatible.
// (FreeBSD chooses the const-correct option; OpenBSD chooses the XPG compatible option.)
// This option can be detected by the existence of the NCURSES_CONST preprocessor macro, which expands either to nothing or to the keyword const.
//
// Fortunately other curses implementations do not use this macro; so we can nail it to one value to enforce const-correctness on non-ncurses systems, and then use it across all curses implementations to match whatever the const-correctness setting is.
// This does mean that the presence of this macro does not mean the presence of ncurses.
// Use the NCURSES_VERSION preprocessor macro to test for the presence of ncurses.

#if !defined(NCURSES_CONST)
#define	NCURSES_CONST	const
#endif
