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

struct ProcessEnvironment;

struct command {
	const char * name;
	void (*func) ( const char * &, std::vector<const char *> &, ProcessEnvironment & );
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
	std::vector<const char *> & args,
	ProcessEnvironment & envs
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
	ProcessEnvironment & envs,
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
void
delete_fstab_option (
	std::list<std::string> &,
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
) ;
extern
std::string
rtrim (
	const std::string & s
) ;
extern
std::string
tolower (
	const std::string & s
) ;
extern
bool
is_bool_true (
	const std::string & r
) ;
extern
bool
is_bool_false (
	const std::string & r
) ;
extern
std::string 
systemd_name_unescape ( 
	bool, 
	bool, 
	const std::string & 
) ;
extern
std::string 
systemd_name_escape ( 
	bool, 
	bool, 
	const std::string & 
) ;
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
	const ProcessEnvironment & envs,
	const uint64_t s,
	bool & leap
) ;
uint64_t
time_to_tai64 (
	const ProcessEnvironment & envs,
	const std::time_t s,
	bool leap
) ;
extern
int
subreaper (
	bool on
) ;
extern
void
setprocname (
	const char *
) ;
extern
void
setprocargv (
	size_t argc,
	const char * argv[]
) ;
extern
const char *
classify_signal (
	int signo
) ;
extern
const char *
signame (
	int signo
) ;
extern
int
wait_nonblocking_for_anychild_stopcontexit (
	pid_t & child,
	int & status
) ;
extern
int
wait_nonblocking_for_anychild_stopexit (
	pid_t & child,
	int & status
) ;
extern
int
wait_nonblocking_for_anychild_exit (
	pid_t & child,
	int & status
) ;
extern
int
wait_blocking_for_anychild_exit (
	pid_t & child,
	int & status
) ;
extern
int
wait_nonblocking_for_stopcontexit_of (
	const pid_t child,
	int & status
) ;
extern
int
wait_nonblocking_for_stopexit_of (
	const pid_t child,
	int & status
) ;
extern
int
wait_nonblocking_for_exit_of (
	const pid_t child,
	int & status
) ;
extern
int
wait_blocking_for_exit_of (
	const pid_t child,
	int & status
) ;

#endif
