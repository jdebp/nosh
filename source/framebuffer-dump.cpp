/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <vector>
#include <iostream>
#include <iomanip>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "config/haswscons.h"
#if defined(HAS_WSCONS)
#	include <dev/wscons/wsconsio.h>
#elif defined(__LINUX__) || defined(__linux__)
#	include <linux/fb.h>
#else
#	include <sys/fbio.h>
#endif
#include <unistd.h>
#include "utils.h"
#include "fdutils.h"
#include "popt.h"
#include "FileDescriptorOwner.h"
#include "CharacterCell.h"

/* Framebuffer access *******************************************************
// **************************************************************************
*/

namespace {

class FrameBufferMapper {
public:
	FrameBufferMapper(
		const char * p,
		const ProcessEnvironment & e,
		const char * f,
		FileDescriptorOwner & d
	) :
		prog(p),
		envs(e),
		framebuffer_filename(f),
		device(d),
		pagesize(static_cast<std::size_t>(sysconf(_SC_PAGESIZE))),
		base(MAP_FAILED),
#if defined(__FreeBSD__) || defined(__DragonFly__)
		extrabase(MAP_FAILED),
#endif
#if defined(__NetBSD__)
		old_mode(WSDISPLAYIO_MODE_EMUL),
#endif
		size(0U)
	{
	}
	~FrameBufferMapper() 
	{
		unmap();
	}
	void
	unmap ()
	{
#if defined(__FreeBSD__) || defined(__DragonFly__)
		if (MAP_FAILED !=extrabase) {
			munmap(const_cast<void *>(extrabase), pagesize);
			extrabase = MAP_FAILED;
		}
#endif
		if (MAP_FAILED != base) {
			munmap(const_cast<void *>(base), size);
			base = MAP_FAILED;
			size = 0;
		}
#if defined(__NetBSD__)
		ioctl(device.get(), WSDISPLAYIO_SMODE, &old_mode);
#endif
	}
	void map ();
	const void *
	query_line_start (
		unsigned short y
	) const {
		const std::size_t window_offset(offset + stride * y);
		return static_cast<const uint8_t *>(base) + window_offset % size;
	}
	unsigned short query_depth() const { return depth; }
	std::size_t query_yres() const { return yres; }
	std::size_t query_xres() const { return xres; }
protected:
	const char * prog;
	const ProcessEnvironment & envs;
	const char * framebuffer_filename;
	FileDescriptorOwner & device;
	const std::size_t pagesize;
	const void * base;
#if defined(__FreeBSD__) || defined(__DragonFly__)
	const void * extrabase;
#endif
#if defined(__NetBSD__)
	u_int old_mode;
#endif
	std::size_t size, offset, stride;
	unsigned short yres, xres, depth;
};

void
FrameBufferMapper::map (
) {
	if (false) {
	exit_error:
		die_errno(prog, envs, framebuffer_filename);
	}
#if defined(__LINUX__) || defined(__linux__)
	fb_fix_screeninfo fixed_info;
	fb_var_screeninfo variable_info;
	if (0 > ioctl(device.get(), FBIOGET_VSCREENINFO, &variable_info)) goto exit_error;
	if (0 > ioctl(device.get(), FBIOGET_FSCREENINFO, &fixed_info)) goto exit_error;
	if (fixed_info.type != FB_TYPE_PACKED_PIXELS)
		die_invalid(prog, envs, framebuffer_filename, "Not a packed pixel device.");
	if (fixed_info.visual != FB_VISUAL_TRUECOLOR && fixed_info.visual != FB_VISUAL_DIRECTCOLOR)
		die_invalid(prog, envs, framebuffer_filename, "Not a true/direct colour device.");
	const std::size_t unrounded_size(fixed_info.smem_len);
	size = (unrounded_size + pagesize - 1U) & ~(pagesize - 1U);
	yres = variable_info.yres;
	xres = variable_info.xres;
	depth = variable_info.bits_per_pixel;
	stride = fixed_info.line_length;
	offset = 0U;
#elif defined(__FreeBSD__) || defined(__DragonFly__)
	struct fbtype t;
	if (0 > ioctl(device.get(), FBIOGTYPE, &t)) goto exit_error;
	offset = 0U;
	// Yes, FreeBSD always rounds down to a multiple of the page size, not up.
	// Try to include any last partial page and you get EINVAL.
	// https://bugs.freebsd.org/bugzilla/show_bug.cgi?id=162373
	size = (offset + t.fb_size) & ~(pagesize - 1U);
	yres = t.fb_height;
	xres = t.fb_width;
	depth = t.fb_depth;
	stride = FBTYPE_GET_STRIDE(&t);
#elif defined(__NetBSD__)
	wsdisplayio_fbinfo framebuffer_info;
	u_int mode = WSDISPLAYIO_MODE_DUMBFB;
	if (0 > ioctl(device.get(), WSDISPLAYIO_GET_FBINFO, &framebuffer_info)) goto exit_error;
	if (0 > ioctl(device.get(), WSDISPLAYIO_GMODE, &old_mode)) goto exit_error;
	if (0 > ioctl(device.get(), WSDISPLAYIO_SMODE, &mode)) goto exit_error;
	yres = framebuffer_info.fbi_height;
	xres = framebuffer_info.fbi_width;
	depth = framebuffer_info.fbi_bitsperpixel;
	stride = framebuffer_info.fbi_stride;
	offset = framebuffer_info.fbi_fboffset;
	size = framebuffer_info.fbi_fbsize;
#elif defined(__OpenBSD__)
	int linebytes;
	wsdisplay_fbinfo mode_info;
	if (0 > ioctl(device.get(), WSDISPLAYIO_GINFO, &mode_info)) goto exit_error;
	if (0 > ioctl(device.get(), WSDISPLAYIO_LINEBYTES, &linebytes)) goto exit_error;
	yres = mode_info.height;
	xres = mode_info.width;
	depth = mode_info.depth;
	stride = linebytes;
	offset = 0U;
	const std::size_t unrounded_size(stride * mode_info.height);
	size = (unrounded_size + pagesize - 1U) & ~(pagesize - 1U);
#else
#	error "Don't know how to query your framebuffer device."
#endif
	base = mmap(nullptr, size, PROT_READ, MAP_SHARED, device.get(), 0UL);
	if (MAP_FAILED == base) goto exit_error;
#if defined(__FreeBSD__) || defined(__DragonFly__)
	extrabase = mmap(static_cast<char *>(const_cast<void *>(base)) + size, pagesize, PROT_READ, MAP_ANON, -1, 0UL);
	if (MAP_FAILED == extrabase) {
		munmap(const_cast<void *>(base), size);
		base = MAP_FAILED;
		size = 0;
		goto exit_error;
	}
#endif
}

}

/* Main function ************************************************************
// **************************************************************************
*/

void
framebuffer_dump [[gnu::noreturn]] (
	const char * & /*next_prog*/,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	try {
		popt::definition * top_table[] = {
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{framebuffer}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}

	if (args.empty()) {
		die_missing_argument(prog, envs, "framebuffer device name");
	}
	const char * framebuffer_filename(args.front());
	args.erase(args.begin());
	if (!args.empty()) die_unexpected_argument(prog, args, envs);

	FileDescriptorOwner device(open_read_at(AT_FDCWD, framebuffer_filename));
	if (0 > device.get()) {
		die_errno(prog, envs, framebuffer_filename);
	}

	FrameBufferMapper mapper(prog, envs, framebuffer_filename, device);
	mapper.map();

	switch (mapper.query_depth()) {
		case 1U:
			std::cout << "P1\n# https://netpbm.sourceforge.net/doc/ppm.html\n" << mapper.query_xres() << '\n' << mapper.query_yres() << '\n';
			break;
		case 8U:
#if defined(OLD_GRAYSCALE) && OLD_GRAYSCALE
			std::cout << "P2\n# https://netpbm.sourceforge.net/doc/pgm.html\n" << mapper.query_xres() << '\n' << mapper.query_yres() << "\n255\n";
			break;
#endif
		case 15U:
		case 16U:
		case 24U:
		case 32U:
			std::cout << "P3\n# https://netpbm.sourceforge.net/doc/ppm.html\n" << mapper.query_xres() << '\n' << mapper.query_yres() << "\n255\n";
			break;
		default:
			std::cout << "PN\n# https://netpbm.sourceforge.net/doc/pnm.html\n" << mapper.query_xres() << '\n' << mapper.query_yres() << "\n255\n";
			break;
	}
	for (unsigned short y(0U); y < mapper.query_yres(); ++y) {
		const void * const start(mapper.query_line_start(y));
		for (unsigned short x(0U); x < mapper.query_xres(); ++x) {
			switch (mapper.query_depth()) {
				default:	// Best of a bad job.  At least we are unlikely to go outside of the mapped memory this way.
				case 1U:
				{
					const uint8_t * const p(static_cast<const uint8_t *>(start) + x / 8U);
					// Black and white are the inverse of the usual convention.
					std::cout << (((*p >> (7U - x % 8U)) & 1U) ? '0' : '1') << '\n';
					break;
				}
				case 8U:
				{
					const uint8_t * const p(static_cast<const uint8_t *>(start) + x);
#if defined(OLD_GRAYSCALE) && OLD_GRAYSCALE
					std::cout << static_cast<unsigned int>(*p) << '\n';
#else
					const CharacterCell::colour_type c(Map256Colour(*p));
					std::cout << std::setw(3) << static_cast<unsigned int>(c.red) << ' ' << std::setw(3) << static_cast<unsigned int>(c.green) << ' ' << std::setw(3) << static_cast<unsigned int>(c.blue) << '\n';
#endif
					break;
				}
				case 15U:
				{
					const uint16_t * const p(static_cast<const uint16_t *>(start) + x);
					const unsigned int red  ((*p >> 7U) & 0xF8);
					const unsigned int green((*p >> 2U) & 0xF8);
					const unsigned int blue ((*p << 3U) & 0xF8);
					std::cout << std::setw(3) << red << ' ' << std::setw(3) << green << ' ' << std::setw(3) << blue << '\n';
					break;
				}
				case 16U:
				{
					const uint16_t * const p(static_cast<const uint16_t *>(start) + x);
					const unsigned int red  ((*p >> 8U) & 0xF8);
					const unsigned int green((*p >> 3U) & 0xFC);
					const unsigned int blue ((*p << 3U) & 0xF8);
					std::cout << std::setw(3) << red << ' ' << std::setw(3) << green << ' ' << std::setw(3) << blue << '\n';
					break;
				}
				case 24U:
				{
					const uint8_t * const p(static_cast<const uint8_t *>(start) + 3U * x);
					std::cout << std::setw(3) << static_cast<unsigned int>(p[2U]) << ' ' << std::setw(3) << static_cast<unsigned int>(p[1U]) << ' ' << std::setw(3) << static_cast<unsigned int>(p[0U]) << '\n';
					break;
				}
				case 32U:
				{
					const uint8_t * const p(reinterpret_cast<const uint8_t *>(static_cast<const uint32_t *>(start) + x));
					std::cout << std::setw(3) << static_cast<unsigned int>(p[2U]) << ' ' << std::setw(3) << static_cast<unsigned int>(p[1U]) << ' ' << std::setw(3) << static_cast<unsigned int>(p[0U]) << '\n';
					break;
				}
			}
		}
	}
	throw EXIT_SUCCESS;
}
