/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_SERVICE_MANAGER_H)
#define INCLUDE_SERVICE_MANAGER_H

#include <stdint.h>

enum {		// statuses from daemontools-encore
	encore_status_stopped,
	encore_status_starting,
	encore_status_started,
	encore_status_running,
	encore_status_stopping,
	encore_status_failed,
};
struct service_manager_rpc_message {
	enum { NOOP = 0, PLUMB, LOAD, MAKE_INPUT_ACTIVATED };
	uint8_t command;
	uint8_t want_pipe;
	uint8_t run_on_empty;
	char name[256 + sizeof "/log"];
};

#endif
