/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <stdint.h>

struct timespec;

#define EV_SET(e, i, f, l, g, d, u) 		\
do {						\
	struct kevent & ke(*(e));		\
	ke.ident	= (i);			\
	ke.filter	= (f);			\
	ke.flags	= (l);			\
	ke.fflags	= (g);			\
	ke.data		= (d);			\
	ke.udata	= (u);			\
} while(0)

struct kevent {
	uintptr_t	ident;
	short		filter;
	unsigned short	flags;
	unsigned int	fflags;
	intptr_t	data;
	void		*udata;
};

enum {	// Filter types
	EVFILT_READ	= -1,
	EVFILT_WRITE	= -2,
	EVFILT_VNODE	= -4,
#if 0 // Not implemented.  Yet.
	EVFILT_PROC	= -5,
#endif
	EVFILT_SIGNAL	= -6,
};

enum {	// Flags
	EV_ADD		= 0x0001,
	EV_DELETE	= 0x0002,
	EV_ENABLE	= 0x0004,
	EV_DISABLE	= 0x0008,
	EV_ONESHOT	= 0x0010,
	EV_CLEAR	= 0x0020,
#if 0 // Not implemented.  Yet.
	EV_RECEIPT	= 0x0040,
#endif
	EV_DISPATCH	= 0x0080,
	EV_ERROR	= 0x4000,
	EV_EOF		= 0x8000,
};

enum { // Notes for VNODE filters
	NOTE_DELETE	= 0x0001,
	NOTE_WRITE	= 0x0002,
	NOTE_EXTEND	= 0x0004,
	NOTE_ATTRIB	= 0x0008,
	NOTE_LINK	= 0x0010,
	NOTE_RENAME	= 0x0020,
	NOTE_REVOKE	= 0x0040
};
	
extern "C" int kqueue_linux();
extern "C" int kevent_linux(int, const struct kevent *, int, struct kevent *, int, const struct timespec*);

#define kqueue() kqueue_linux()
#define kevent(f,c,nc,e,ne,t) kevent_linux((f),(c),(nc),(e),(ne),(t))
