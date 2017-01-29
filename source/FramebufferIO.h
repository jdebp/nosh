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
#elif defined(__OpenBSD__)
#include <dev/wscons/wsconsio.h>
#else
#	error "Don't know how to query your framebuffer device."
#endif

/// A wrapper class for devices that provide the fbio protocol.
class FramebufferIO :
	public FileDescriptorOwner
{
public:
	FramebufferIO(int pfd, bool l80);
	~FramebufferIO();
	void save(const char *, const char *);
	void set_graphics_mode(const char *, const char *);
	void restore();
#if defined(__LINUX__) || defined(__linux__)
	std::size_t query_size() const { return fixed_info.smem_len; }
	unsigned short query_stride() const { return fixed_info.line_length; }
	unsigned short query_yres() const { return variable_info.yres; }
	unsigned short query_xres() const { return variable_info.xres; }
	unsigned short query_depth() const { return variable_info.bits_per_pixel; }
#elif defined(__FreeBSD__) || defined(__DragonFly__)
	std::size_t query_size() const { return mode_info.vi_buffer_size; }
	unsigned short query_stride() const { return adapter_info.va_line_width; }
	unsigned short query_xres() const { return mode_info.vi_width; }
	unsigned short query_yres() const { return mode_info.vi_height; }
	unsigned short query_depth() const { return mode_info.vi_depth; }
#elif defined(__OpenBSD__)
	std::size_t query_size() const;
	unsigned short query_stride() const { return stride; }
	unsigned short query_xres() const { return mode_info.width; }
	unsigned short query_yres() const { return mode_info.height; }
	unsigned short query_depth() const { return mode_info.depth; }
#else
	std::size_t query_size() const;
	unsigned short query_stride() const;
	unsigned short query_xres() const;
	unsigned short query_yres() const;
	unsigned short query_depth() const;
#	error "Don't know how to query your framebuffer device."
#endif
protected:
#if defined(__LINUX__) || defined(__linux__)
	fb_fix_screeninfo fixed_info;
	fb_var_screeninfo old_variable_info, variable_info;
#elif defined(__FreeBSD__) || defined(__DragonFly__)
	int old_video_mode, video_mode;
	video_adapter_info_t adapter_info;
	video_info_t mode_info;
#elif defined(__OpenBSD__)
	int old_video_mode, video_mode, stride;
	wsdisplay_fbinfo mode_info;
#else
#	error "Don't know how to query your framebuffer device."
#endif
	bool limit_80_columns;
};

#endif
