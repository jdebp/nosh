/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <algorithm>
#include <unistd.h>
#include "FileDescriptorOwner.h"

FileDescriptorOwner::~FileDescriptorOwner()
{
	if (-1 != fd) close(fd);
}

FileDescriptorOwner::FileDescriptorOwner(
	FileDescriptorOwner && o
) : 
	fd(-1)
{
	std::swap(fd, o.fd);
}

void 
FileDescriptorOwner::reset(int f)
{
	if (-1 != fd) close(fd);
	std::swap(fd, f);
}

int 
FileDescriptorOwner::release()
{
	int f(-1);
	std::swap(fd, f);
	return f;
}
