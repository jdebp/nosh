/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_FILEDESCRIPTOROWNER_H)
#define INCLUDE_FILEDESCRIPTOROWNER_H

/// A class for simple ownership of file descriptors, modelled on the semantics of std::auto_ptr.
class FileDescriptorOwner
{
public:
	FileDescriptorOwner(int f) : fd(f) {}
	~FileDescriptorOwner();
	FileDescriptorOwner(FileDescriptorOwner && o);
	void reset(int);
	int release();
	int get() const { return fd; }
protected:
	int fd;
private:
	FileDescriptorOwner(const FileDescriptorOwner &);
};

#endif
