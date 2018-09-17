/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_BUNDLE_CREATON_H)
#define INCLUDE_BUNDLE_CREATON_H

#include <string>

class FileDescriptorOwner;

void
create_link (
	const char * prog,
	const std::string & name,
	const FileDescriptorOwner & bundle_dir_fd,
	const std::string & target,
	const std::string & link
) ;
void
create_links (
	const char * prog,
	const ProcessEnvironment & envs,
	const std::string & bund,
	const bool is_user,
	const bool is_target,
	const bool services_are_relative,
	const bool targets_are_relative,
	const FileDescriptorOwner & bundle_dir_fd,
	const std::string & names,
	const std::string & subdir
) ;
void
create_mount_links (
	const char * prog,
	const std::string & bund,
	const bool mounts_are_relative,
	const bool prevent_root_link,
	const FileDescriptorOwner & bundle_dir_fd,
	const std::string & names,
	const std::string & subdir
) ;
void
make_mount_interdependencies (
	const char * prog,
	const std::string & name,
	const bool mounts_are_relative,
	const bool prevent_root_link,
	const FileDescriptorOwner & bundle_dir_fd,
	std::string where
) ;
void
flag_file (
	const char * prog,
	const std::string & service_dirname,
	const FileDescriptorOwner & service_dir_fd,
	const char * name,
	bool make
) ;
static inline
bool
is_root(
	const char * p
) {
	return '/' == p[0] && '\0' == p[1];
}

#endif
