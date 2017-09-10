/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include "common-manager.h"

const static struct api_symlink data[] = 
{
#if defined(__LINUX__) || defined(__linux__)
	{	1,	"/dev/ptmx",	"pts/ptmx"		},
	{	0,	"/dev/fd",	"/proc/self/fd"		},
	{	0,	"/dev/core",	"/proc/kcore"		},
	{	0,	"/dev/stdin",	"/proc/self/fd/0"	},
	{	0,	"/dev/stdout",	"/proc/self/fd/1"	},
	{	0,	"/dev/stderr",	"/proc/self/fd/2"	},
	{	0,	"/dev/shm",	"/run/shm"		},
#else
	{	0,	"/dev/shm",	"/run/shm"		},
#endif
};

const std::vector<api_symlink> api_symlinks(data, data + sizeof data/sizeof *data);
