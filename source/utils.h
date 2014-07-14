/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_UTILS_H)
#define INCLUDE_UTILS_H

#include <vector>
#include <string>
#include <cstddef>
#include <cstdlib>
#include <cstdio>

enum {
	EXIT_USAGE = EXIT_FAILURE,
	EXIT_TEMPORARY_FAILURE = 111,
	EXIT_PERMANENT_FAILURE = 100
};

struct command {
	const char * name;
	void (*func) ( const char * &, std::vector<const char *> & );
} ;
extern const command commands[], personalities[];
extern const std::size_t num_commands, num_personalities;

extern 
const char *
basename_of (
	const char * s
) ;
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
extern
void
exec_terminal (
	const char * & prog,
	const char * & next_prog,
	std::vector<const char *> & args
) ;
extern
const command *
find (
	const char * prog,
	bool
) ;
extern inline
const char *
arg0_of (
	std::vector<const char *> & args
) {
	return args.empty() ? 0 : args[0];
}
extern
std::vector<std::string>
read_file (
	const char * prog,
	const char * filename
) ;
extern
std::vector<std::string>
read_file (
	const char * prog,
	const char * filename,
	FILE *
) ;
extern
std::vector<std::string>
read_file (
	FILE *
) ;
extern
std::string
convert (
	const struct iovec & v
) ;
extern
std::string
fspath_from_mount (
	struct iovec * iov,
	unsigned int ioc
) ;
extern
std::string
read_env_file (
	const char * prog,
	const char * dir,
	const char * basename,
	int fd
) ;
extern
bool
ends_in (
	const std::string & s,
	const std::string & p,
	std::string & r
) ;
extern
bool
begins_with (
	const std::string & s,
	const std::string & p,
	std::string & r
) ;
extern
std::string
ltrim (
	const std::string & s
);
extern
std::string
rtrim (
	const std::string & s
);


#endif
