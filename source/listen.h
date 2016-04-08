/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_LISTEN_H)
#define INCLUDE_LISTEN_H

enum { LISTEN_SOCKET_FILENO = 3 };

extern unsigned query_listen_fds();
extern int query_listen_fds_passthrough();

#endif
