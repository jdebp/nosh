/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_TTYUTILS_H)
#define INCLUDE_TTYUTILS_H

struct termios;
struct winsize;

extern
struct termios
make_default_local_virtual (
	bool no_tostop,
	bool no_utf_8
) ;
extern
struct termios
enable_canonical_software_processing (
	const struct termios & t
) ;
extern
struct termios
disable_canonical_software_processing (
	const struct termios & t
) ;
extern
struct termios
disable_tostop (
	const struct termios & t
) ;
extern
int
tcsetattr_nointr (
	int fd,
	int mode,
	const struct termios & t
) ;
extern
int
tcgetattr_nointr (
	int fd,
	struct termios & t
) ;
extern
void
sane (
	struct winsize & w
) ;
extern
int
tcsetwinsz_nointr (
	int fd,
	const struct winsize & w
) ;
extern
int
tcgetwinsz_nointr (
	int fd,
	struct winsize & w
) ;

#endif
