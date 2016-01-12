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
	const std::string & bund,
	const bool is_target,
	const bool etc_bundle,
	const FileDescriptorOwner & bundle_dir_fd,
	const std::string & names,
	const std::string & subdir
) ;

#endif
