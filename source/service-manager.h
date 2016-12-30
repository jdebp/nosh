/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_SERVICE_MANAGER_H)
#define INCLUDE_SERVICE_MANAGER_H

#include <stdint.h>

enum {		///< statuses from daemontools-encore
	encore_status_stopped,
	encore_status_starting,
	encore_status_started,
	encore_status_running,
	encore_status_stopping,
	encore_status_failed,
};
enum {
	// Original daemontools status
	THIS_PID_OFFSET = 12U,
	PAUSE_FLAG_OFFSET = 16U,
	WANT_FLAG_OFFSET = 17U,
	DAEMONTOOLS_STATUS_BLOCK_SIZE = 18U,
	// daemontools encore status
	ENCORE_STATUS_OFFSET = DAEMONTOOLS_STATUS_BLOCK_SIZE,
	ENCORE_STATUS_BLOCK_SIZE = 19U,
	// nosh service manager status
	EXIT_STATUSES_OFFSET = ENCORE_STATUS_BLOCK_SIZE,
		EXIT_STATUS_SIZE = 17U,	// a byte code, a 32-bit number, and a TAI64N timestamp
	STATUS_BLOCK_SIZE = ENCORE_STATUS_BLOCK_SIZE + 4U * EXIT_STATUS_SIZE,
};
struct service_manager_rpc_message {
	enum { NOOP = 0, PLUMB, LOAD, MAKE_INPUT_ACTIVATED, UNLOAD, MAKE_PIPE_CONNECTABLE, MAKE_RUN_ON_EMPTY };
	uint8_t command;
	char name[256 + sizeof "/log"];
};

#endif
