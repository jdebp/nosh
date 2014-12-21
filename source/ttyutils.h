/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_TTYUTILS_H)
#define INCLUDE_TTYUTILS_H

extern
struct termios
sane (
	bool no_tostop,
	bool no_utf_8
) ;
extern
struct termios
make_raw (
	const struct termios & t
) ;
extern
int
tcsetattr_nointr (
	int fd,
	int mode,
	const struct termios & t
) ;
int
tcgetattr_nointr (
	int fd,
	struct termios & t
) ;
int
tcsetwinsz_nointr (
	int fd,
	const struct winsize & w
) ;
int
tcgetwinsz_nointr (
	int fd,
	struct winsize & w
) ;

#endif
