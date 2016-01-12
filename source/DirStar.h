/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_DIRSTAR_H)
#define INCLUDE_DIRSTAR_H

#include <dirent.h>
#include "FileDescriptorOwner.h"

/// \brief A wrapper for DIR * that automatically closes the directory in its destructor.
struct DirStar {
	operator DIR * () const { return d ; }
	DirStar(DIR * dp = 0) : d(dp) {}
	DirStar(FileDescriptorOwner & fd) : d(fdopendir(fd.get())) { if (d) fd.release(); }
//	DirStar(int fd) : d(fdopendir(fd)) {}
	DIR * release() { DIR *dp(d); d = 0; return dp; }
	int fd() const { return dirfd(d); }
	DirStar & operator= ( DIR * n ) { assign(n); return *this; }
	~DirStar() { assign(0); }
protected:
	DIR * d;
	void assign(DIR * n) { if (d) closedir(d); d = n; }
};

#endif
