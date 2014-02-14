/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_FILESTAR_H)
#define INCLUDE_FILESTAR_H

#include <cstdio>

/// \brief A wrapper for FILE * that automatically closes the file in its destructor.
struct FileStar {
	operator FILE * () const { return f ; }
	FileStar(FILE * fp = 0) : f(fp) {}
	FileStar & operator= ( FILE * n ) { assign(n); return *this; }
	~FileStar() { assign(0); }
protected:
	FILE * f;
	void assign(FILE * n) { if (f) std::fclose(f); f = n; }
};

#endif
