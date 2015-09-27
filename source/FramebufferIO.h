/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_FRAMEBUFFERIO_H)
#define INCLUDE_FRAMEBUFFERIO_H

#include <cstddef>
#include "FileDescriptorOwner.h"
#if defined(__LINUX__) || defined(__linux__)
#include <linux/fb.h>
#elif defined(__FreeBSD__) || defined(__DragonFly__)
#include <sys/fbio.h>
#else
#	error "Don't know how to query your framebuffer device."
#endif

/// A wrapper class for devices that provide the fbio protocol.
class FramebufferIO :
	public FileDescriptorOwner
{
public:
	FramebufferIO(int pfd) : FileDescriptorOwner(pfd) {}
	~FramebufferIO();
	void save_and_set_graphics_mode(const char *, const char *);
	void restore();
#if defined(__LINUX__) || defined(__linux__)
	std::size_t query_size() const { return fixed_info.smem_len; }
	unsigned short query_stride() const { return fixed_info.line_length; }
	unsigned short query_yres() const { return variable_info.yres; }
	unsigned short query_xres() const { return variable_info.xres; }
	unsigned short query_depth() const { return variable_info.bits_per_pixel; }
#else
	std::size_t query_size() const { return variable_info.vi_buffer_size; }
	unsigned short query_stride() const { return adapter_info.va_line_width; }
	unsigned short query_xres() const { return variable_info.vi_width; }
	unsigned short query_yres() const { return variable_info.vi_height; }
	unsigned short query_depth() const { return variable_info.vi_depth; }
#endif
protected:
#if defined(__LINUX__) || defined(__linux__)
	struct fb_fix_screeninfo fixed_info;
	struct fb_var_screeninfo old_variable_info, variable_info;
#else
	video_adapter_info_t adapter_info;
	int old_video_mode, video_mode;
	video_info_t variable_info;
#endif
};

#endif
