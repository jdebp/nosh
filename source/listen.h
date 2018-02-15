/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_LISTEN_H)
#define INCLUDE_LISTEN_H

enum { LISTEN_SOCKET_FILENO = 3 };

struct ProcessEnvironment;

extern unsigned query_listen_fds(ProcessEnvironment &);
extern unsigned query_listen_fds_or_daemontools(ProcessEnvironment &);
extern int query_listen_fds_passthrough(ProcessEnvironment &);

#endif
