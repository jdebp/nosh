/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <unistd.h>
#include "utils.h"
#include "popt.h"

/* Main function ************************************************************
// **************************************************************************
*/

void
userenv ( 
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment &
) {
	args[0] = "userenv-fromenv";
	args.insert(args.begin(), "getuidgid");
	next_prog = args[0];
}
