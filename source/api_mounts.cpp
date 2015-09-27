/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <unistd.h>
#include <sys/uio.h>
#include <sys/mount.h>
#include "nmount.h"
#include "common-manager.h"

#if defined(__LINUX__) || defined(__linux__)
static const struct iovec proc[] = {
	FSTYPE,				MAKE_IOVEC("proc"),
	FSPATH,				MAKE_IOVEC("/proc"),
};
static const struct iovec sys[] = {
	FSTYPE,				MAKE_IOVEC("sysfs"),
	FSPATH,				MAKE_IOVEC("/sys"),
};
static const struct iovec dev[] = {
	FSTYPE,				MAKE_IOVEC("devtmpfs"),
	FSPATH,				MAKE_IOVEC("/dev"),
	MAKE_IOVEC("mode"),		MAKE_IOVEC("0755"),
	MAKE_IOVEC("size"),		MAKE_IOVEC("10M"),
};
static const struct iovec pts[] = {
	FSTYPE,				MAKE_IOVEC("devpts"),
	FSPATH,				MAKE_IOVEC("/dev/pts"),
	MAKE_IOVEC("ptmxmode"),		MAKE_IOVEC("0666"),
	MAKE_IOVEC("mode"),		MAKE_IOVEC("0620"),
	// Yes, we're hardwiring the GID of the "tty" group to what it conventionally is in Linux distributions.
	// It's this, or have the whole nsswitch mechanism loaded into process #1 just to read /etc/groups.
	MAKE_IOVEC("gid"),		MAKE_IOVEC("5"),
	// In modern systems, this should always be used.
	// Otherwise, the instance mounted in the root would be accessible to containers.
	MAKE_IOVEC("newinstance"),	ZERO_IOVEC(),
};
static const struct iovec run[] = {
	FROM,				MAKE_IOVEC("tmpfs"),
	FSTYPE,				MAKE_IOVEC("tmpfs"),
	FSPATH,				MAKE_IOVEC("/run"),
	MAKE_IOVEC("mode"),		MAKE_IOVEC("01777"),
	MAKE_IOVEC("size"),		MAKE_IOVEC("20%"),
};
static const struct iovec shm[] = {
	FROM,				MAKE_IOVEC("tmpfs"),
	FSTYPE,				MAKE_IOVEC("tmpfs"),
	FSPATH,				MAKE_IOVEC("/run/shm"),
	MAKE_IOVEC("mode"),		MAKE_IOVEC("01777"),
	MAKE_IOVEC("size"),		MAKE_IOVEC("50%"),
};
#else
static const struct iovec proc[] = {
	FSTYPE,				MAKE_IOVEC("procfs"),
	FSPATH,				MAKE_IOVEC("/proc"),
};
static const struct iovec dev[] = {
	FSTYPE,				MAKE_IOVEC("devfs"),
	FSPATH,				MAKE_IOVEC("/dev"),
};
static const struct iovec fds[] = {
	FSTYPE,				MAKE_IOVEC("fdescfs"),
	FSPATH,				MAKE_IOVEC("/dev/fd"),
};
static const struct iovec run[] = {
	FSTYPE,				MAKE_IOVEC("tmpfs"),
	FSPATH,				MAKE_IOVEC("/run"),
	MAKE_IOVEC("size"),		MAKE_IOVEC("20%"),
};
static const struct iovec shm[] = {
	FSTYPE,				MAKE_IOVEC("tmpfs"),
	FSPATH,				MAKE_IOVEC("/run/shm"),
	MAKE_IOVEC("size"),		MAKE_IOVEC("50%"),
};
#endif

#define MAKE_DATA(x) # x, const_cast<struct iovec *>(x), sizeof x/sizeof *x

static const struct api_mount data[] = 
{
#if defined(__LINUX__) || defined(__linux__)
	{	MAKE_DATA(proc),	MS_NOSUID|MS_NODEV				},
	{	MAKE_DATA(sys),		MS_NOSUID|MS_NOEXEC|MS_NODEV			},
	{	MAKE_DATA(dev),		MS_NOSUID|MS_STRICTATIME			},
	{	MAKE_DATA(pts),		MS_NOSUID|MS_STRICTATIME|MS_NOEXEC		},
	{	MAKE_DATA(run),		MS_NOSUID|MS_STRICTATIME|MS_NODEV		},
	{	MAKE_DATA(shm),		MS_NOSUID|MS_STRICTATIME|MS_NOEXEC|MS_NODEV	},
#else
	{	MAKE_DATA(proc),	MNT_NOSUID					},
	{	MAKE_DATA(dev),		MNT_NOSUID|MNT_NOEXEC				},
	{	MAKE_DATA(fds),		MNT_NOSUID|MNT_NOEXEC				},
	{	MAKE_DATA(run),		MNT_NOSUID					},
	{	MAKE_DATA(shm),		MNT_NOSUID|MNT_NOEXEC				},
#endif
};

const std::vector<api_mount> api_mounts(data, data + sizeof data/sizeof *data);
