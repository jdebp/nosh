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

FramebufferIO::~FramebufferIO()
{
	restore();
}

void
FramebufferIO::save_and_set_graphics_mode(
	const char * prog, 
	const char * fb_filename
) {
#if defined(__LINUX__) || defined(__linux__)
	if ((0 > ioctl(fd, FBIOGET_FSCREENINFO, &fixed_info))
	|| (0 > ioctl(fd, FBIOGET_VSCREENINFO, &old_variable_info))
	) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, fb_filename, std::strerror(error));
		throw EXIT_FAILURE;
	}
	variable_info = old_variable_info;	/// TODO: \todo change mode
	if (fixed_info.type != FB_TYPE_PACKED_PIXELS) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, fb_filename, "Not a packed pixel device.");
		throw EXIT_FAILURE;
	}
	if (fixed_info.visual != FB_VISUAL_TRUECOLOR && fixed_info.visual != FB_VISUAL_DIRECTCOLOR) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, fb_filename, "Not a true/direct colour device.");
		throw EXIT_FAILURE;
	}
#elif defined(__FreeBSD__) || defined(__DragonFly__)
	if (0 > ioctl(fd, FBIO_GETMODE, &old_video_mode)) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, fb_filename, std::strerror(error));
		throw EXIT_FAILURE;
	}
	video_mode = variable_info.vi_mode = M_VESA_FULL_1280;
	if ((0 > ioctl(fd, FBIO_SETMODE, &video_mode))
	|| (0 > ioctl(fd, FBIO_ADPINFO, &adapter_info)) 
	|| (0 > ioctl(fd, FBIO_MODEINFO, &variable_info))
	) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, fb_filename, std::strerror(error));
		throw EXIT_FAILURE;
	}
	if (!(variable_info.vi_flags & V_INFO_GRAPHICS)) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, fb_filename, "Did not change to graphics mode.");
		throw EXIT_FAILURE;
	}
	if (variable_info.vi_mem_model != V_INFO_MM_PACKED && variable_info.vi_mem_model != V_INFO_MM_DIRECT) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, fb_filename, "Not a packed/direct colour device.");
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
#else
#	error "Don't know how to control your framebuffer device."
#endif
}

