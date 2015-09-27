/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_UTILS_H)
#define INCLUDE_UTILS_H

#include <vector>
#include <list>
#include <string>
#include <cstddef>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <stdint.h>

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
bool
process_env_dir (
	const char * prog,
	const char * dir,
	int scan_dir_fd,
	bool ignore_errors,
	bool full,
	bool chomp
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
std::list<std::string>
split_fstab_options (
	const char * o
) ;
extern
bool
has_option (
	const std::list<std::string> & options,
	std::string prefix,
	std::string & remainder
) ;
extern
bool
has_option (
	const std::list<std::string> & options,
	const std::string & opt
) ;
extern
bool
read_line (
	FILE * f,
	std::string & l
) ;
extern
std::string
read_env_file (
	const char * prog,
	const char * dir,
	const char * basename,
	int fd,
	bool full,
	bool chomp
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
extern
std::string
tolower (
	const std::string & s
);
extern
unsigned 
val ( 
	const std::string & s 
) ;
extern
std::list<std::string>
split_list (
	const std::string & s
) ;
extern
std::string
multi_line_comment (
	const std::string & s
) ;
time_t
tai64_to_time (
	const uint64_t s,
	bool & leap
) ;
uint64_t
time_to_tai64 (
	const std::time_t s,
	bool leap
) ;
extern
void
subreaper (
	bool on
) ;

#endif
