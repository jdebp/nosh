/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/ioctl.h>
#include <unistd.h>
#include "FramebufferIO.h"

FramebufferIO::FramebufferIO(int pfd, bool l80) : 
	FileDescriptorOwner(pfd),
	limit_80_columns(l80)
{
}

FramebufferIO::~FramebufferIO()
{
	restore();
}

void
FramebufferIO::save(
	const char * prog, 
	const char * fb_filename
) {
#if defined(__LINUX__) || defined(__linux__)
	if (0 > ioctl(fd, FBIOGET_VSCREENINFO, &old_variable_info)) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, fb_filename, std::strerror(error));
		throw EXIT_FAILURE;
	}
#elif defined(__FreeBSD__) || defined(__DragonFly__)
	if (0 > ioctl(fd, FBIO_GETMODE, &old_video_mode)) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, fb_filename, std::strerror(error));
		throw EXIT_FAILURE;
	}
#elif defined(__OpenBSD__)
	if (0 > ioctl(fd, WSDISPLAYIO_GMODE, &old_video_mode)) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, fb_filename, std::strerror(error));
		throw EXIT_FAILURE;
	}
#else
#	error "Don't know how to control your framebuffer device."
#endif
}

void
FramebufferIO::set_graphics_mode(
	const char * prog, 
	const char * fb_filename
) {
#if defined(__LINUX__) || defined(__linux__)
	if (0 > ioctl(fd, FBIOGET_FSCREENINFO, &fixed_info)) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, fb_filename, std::strerror(error));
		throw EXIT_FAILURE;
	}
	// To change mode, use video= on the kernel command line or on the command line for the relevant fb module.
	variable_info = old_variable_info;
	if (fixed_info.type != FB_TYPE_PACKED_PIXELS) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, fb_filename, "Not a packed pixel device.");
		throw EXIT_FAILURE;
	}
	if (fixed_info.visual != FB_VISUAL_TRUECOLOR && fixed_info.visual != FB_VISUAL_DIRECTCOLOR) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, fb_filename, "Not a true/direct colour device.");
		throw EXIT_FAILURE;
	}
#elif defined(__FreeBSD__) || defined(__DragonFly__)
    	int max_depth(0);
	for (int mode(0); mode <= M_VESA_MODE_MAX; ++mode) {
		mode_info.vi_mode = mode;
		if (0 > ioctl(fd, FBIO_MODEINFO, &mode_info)) continue;
		if (!(mode_info.vi_flags & V_INFO_GRAPHICS)) continue;
		if (mode_info.vi_mem_model != V_INFO_MM_DIRECT) continue;
		if (max_depth < mode_info.vi_depth)
			max_depth = mode_info.vi_depth;
	}
	video_mode = -1;
    	int max_width(0), max_height(0);
	for (int mode(0); mode <= M_VESA_MODE_MAX; ++mode) {
		mode_info.vi_mode = mode;
		if (0 > ioctl(fd, FBIO_MODEINFO, &mode_info)) continue;
		if (!(mode_info.vi_flags & V_INFO_GRAPHICS)) continue;
		if (mode_info.vi_mem_model != V_INFO_MM_DIRECT) continue;
		if (max_depth > mode_info.vi_depth) continue;
		if (limit_80_columns && mode_info.vi_width > 1280) continue;
		if ((max_width * max_height) > (mode_info.vi_width * mode_info.vi_height)) continue;
		video_mode = mode;
		max_width = mode_info.vi_width;
		max_height = mode_info.vi_height;
	}
	if (0 > video_mode) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, fb_filename, "No good graphics video mode found.");
		throw EXIT_FAILURE;
	}
	mode_info.vi_mode = video_mode;
	if ((0 > ioctl(fd, FBIO_SETMODE, &video_mode))
	||  (0 > ioctl(fd, FBIO_GETMODE, &video_mode))
	||  (0 > ioctl(fd, FBIO_ADPINFO, &adapter_info)) 
	||  (0 > ioctl(fd, FBIO_MODEINFO, &mode_info))
	) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, fb_filename, std::strerror(error));
		throw EXIT_FAILURE;
	}
	if (!(mode_info.vi_flags & V_INFO_GRAPHICS)) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, fb_filename, "Did not change to graphics mode.");
		throw EXIT_FAILURE;
	}
	if (mode_info.vi_mem_model != V_INFO_MM_PACKED && mode_info.vi_mem_model != V_INFO_MM_DIRECT) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, fb_filename, "Not a packed/direct colour device.");
		throw EXIT_FAILURE;
	}
#elif defined(__OpenBSD__)
	video_mode = WSDISPLAYIO_MODE_DUMBFB;
#if defined(WSDISPLAYIO_SGFXMODE)
	static const wsdisplayio_gfx_mode modes[] = {
		// 160 columns
		{	2560,	1600,	32	},
		{	2560,	1600,	24	},
		// 120 columns
		{	1920,	1200,	32	},
		{	1920,	1200,	24	},
		{	1920,	1080,	24	},
		{	1920,	1080,	24	},
		// 100 columns
		{	1600,	1200,	32	},
		{	1600,	1200,	24	},
		{	1600,	1024,	32	},
		{	1600,	1024,	24	},
		// 80 columns
		{	1280,	1024,	32	},
		{	1280,	1024,	24	},
	};
	int mode(-1);
	for (int n(0); n < sizeof modes/sizeof *modes; ++n) {
		const wsdisplayio-gfx_mode & m(modes[i]);
		if (limit_80_columns && m.width > 1280) continue;
		if (0 > ioctl(fd, WSDISPLAYIO_SETGFXMODE, &m)) continue;
		mode = n;
		break;
	}
	if (0 > mode) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, fb_filename, "No good graphics video mode found.");
		throw EXIT_FAILURE;
	}
#endif
	if ((0 > ioctl(fd, WSDISPLAYIO_SMODE, &video_mode))
	||  (0 > ioctl(fd, WSDISPLAYIO_GINFO, &mode_info))
	||  (0 > ioctl(fd, WSDISPLAYIO_LINEBYTES, &stride))
	) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, fb_filename, std::strerror(error));
		throw EXIT_FAILURE;
	}
#else
#	error "Don't know how to control your framebuffer device."
#endif
}

void
FramebufferIO::restore()
{
#if defined(__LINUX__) || defined(__linux__)
	ioctl(fd, FBIOPUT_VSCREENINFO, &old_variable_info);
#elif defined(__FreeBSD__) || defined(__DragonFly__)
	ioctl(fd, FBIO_SETMODE, &old_video_mode);
#elif defined(__OpenBSD__)
#if defined(WSDISPLAYIO_SGFXMODE)
	const wsdisplayio_gfx_mode mode = { 640, 480, 8 };
	ioctl(fd, WSDISPLAYIO_SETGFXMODE, &m);
#endif
	ioctl(fd, WSDISPLAYIO_SMODE, &old_video_mode);
#else
#	error "Don't know how to control your framebuffer device."
#endif
}

#if defined(__OpenBSD__)
std::size_t 
FramebufferIO::query_size() const
{
	return stride * mode_info.height;
}
#endif
