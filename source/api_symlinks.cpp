/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include "common-manager.h"

const static struct api_symlink data[] = 
{
#if defined(__LINUX__) || defined(__linux__)
	{	"/dev/ptmx",	"pts/ptmx"		},
	{	"/dev/fd",	"/proc/self/fd"		},
	{	"/dev/core",	"/proc/kcore"		},
	{	"/dev/stdin",	"/proc/self/fd/0"	},
	{	"/dev/stdout",	"/proc/self/fd/1"	},
	{	"/dev/stderr",	"/proc/self/fd/2"	},
	{	"/dev/shm",	"/run/shm"		},
#else
	{	"/dev/shm",	"/run/shm"		},
#endif
};

const std::vector<api_symlink> api_symlinks(data, data + sizeof data/sizeof *data);
