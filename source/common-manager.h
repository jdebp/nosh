/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_SYSTEM_MANAGER_H)
#define INCLUDE_SYSTEM_MANAGER_H

#include <vector>
#include <string>

struct api_symlink {
	const char * name, * target;
};

extern const std::vector<api_symlink> api_symlinks;

struct iovec;

struct api_mount {
	const char * name;
	struct iovec * iov;
	unsigned int ioc;
	int flags;
};

extern const std::vector<api_mount> api_mounts;

extern int query_manager_pid(const bool is_system);
extern int listen_service_manager_socket(const bool is_system, const char * prog);
extern int connect_service_manager_socket(const bool is_system, const char * prog);
extern std::string xdg_runtime_dir();

extern bool local_session_mode;
extern const char * const roots[4];
extern const char * const bundle_prefixes[4];

#endif
