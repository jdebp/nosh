/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define _BSD_SOURCE 1
#include <map>
#include <vector>
#include <cstring>
#include <cerrno>
#include <csignal>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include "config/haswscons.h"
#if defined(HAS_WSCONS)
#	include <dev/wscons/wsconsio.h>
#	include <dev/wscons/wsdisplay_usl_io.h>	// VT/CONSIO ioctls
#elif defined(__LINUX__) || defined(__linux__)
#	include <linux/fb.h>
#	include <linux/kd.h>
#	include <linux/vt.h>
#	if !defined(VT_TRUE)
#	define VT_TRUE 1
#	endif
#else
#	include <sys/mouse.h>
#	include <sys/fbio.h>
#	include <sys/consio.h>
#endif
#include "packed.h"
#include <unistd.h>
#include <fcntl.h>
#include "utils.h"
#include "fdutils.h"
#include "pack.h"
#include "popt.h"
#include "ProcessEnvironment.h"
#include "FileDescriptorOwner.h"
#include "kbdmap_utils.h"
#include "VirtualTerminalRealizer.h"

using namespace VirtualTerminalRealizer;

/* Font command-line option handling ****************************************
// **************************************************************************
*/

namespace {

struct fontspec_definition : public popt::compound_1arg_named_definition {
public:
	fontspec_definition(char s, const char * l, const char * a, const char * d, FontSpecList & f, int w, CombinedFont::Font::Slant i) : popt::compound_1arg_named_definition(s, l, a, d), specs(f), weight(w), slant(i) {}
	virtual void action(popt::processor &, const char *);
	virtual ~fontspec_definition();
protected:
	FontSpecList & specs;
	int weight;
	CombinedFont::Font::Slant slant;
};

}

fontspec_definition::~fontspec_definition() {}
void fontspec_definition::action(popt::processor &, const char * text)
{
	FontSpec v = { text, weight, slant };
	specs.push_back(v);
}

/* Framebuffers *************************************************************
// **************************************************************************
*/

namespace {

/// \brief HODs that use a memory-mapped character device framebuffer and that speak the fbio or wsdisplay protocol
class HODFromFramebuffer :
	public HOD
{
public:
	HODFromFramebuffer(SharedHODResources & r, const HOD::Options & o, FileDescriptorOwner & fd);
	~HODFromFramebuffer();

	void save_and_set_graphics_mode(const char *, const ProcessEnvironment &, const char *, bool);
	void restore();
	void map();
	void unmap();

	virtual GraphicsInterface::PixelCoordinate query_yres() const { return yres; }
	virtual GraphicsInterface::PixelCoordinate query_xres() const { return xres; }

	static bool mode_selection;

protected:
	const std::size_t pagesize;
	FileDescriptorOwner device;
	void * map_base;	///< Memory mapping start in process memory
#if defined(FBIOGET_VSCREENINFO) && defined(FBIOGET_FSCREENINFO)
#elif defined(FBIOGTYPE)
	void * map_extrabase;	///< Memory mapping start in process memory
	int old_video_mode;
#elif defined(WSDISPLAYIO_MODE_DUMBFB)
	int old_video_mode;
#else
#	error "Don't know how to control your framebuffer device."
#endif
	std::size_t map_offset;	///< Offset of the aperture start within the mapped memory
	std::size_t map_size;	///< Memory mapping size in process memory
	std::size_t map_stride;	///< line width in the memory mapping
	GraphicsInterface::PixelCoordinate yres;
	GraphicsInterface::PixelCoordinate xres;
	unsigned short depth;
	SharedHODResources::ScreenBitmapHandle screen;

	virtual SharedHODResources::ScreenBitmapHandle GetScreenBitmap() const { return screen; }

	/// \brief a screen bitmap using the memory mapping either as an aperture or as the full buffer
	struct ScreenBitmap :
		public GraphicsInterface::MemoryMappedScreenBitmap
	{
		ScreenBitmap(HODFromFramebuffer & f, GraphicsInterface::PixelCoordinate y, GraphicsInterface::PixelCoordinate x, unsigned short d) : MemoryMappedScreenBitmap(y, x, d), framebuffer(f) {}
		virtual ~ScreenBitmap() {}
	protected:
		HODFromFramebuffer & framebuffer;
		virtual void * GetStartOfLine(GraphicsInterface::PixelCoordinate y);
	};
	friend struct ScreenBitmap;

};

#if defined(__LINUX__) || defined(__linux__)
inline
bool
is_character_device (
	int fd
) {
	struct stat s;
	return 0 <= fstat(fd, &s) && S_ISCHR(s.st_mode);
}
#endif

}

inline
HODFromFramebuffer::HODFromFramebuffer(SharedHODResources & r, const HOD::Options & o, FileDescriptorOwner & fd) :
	HOD(r, o),
	pagesize(static_cast<std::size_t>(sysconf(_SC_PAGESIZE))),
	device(fd.release()),
	map_base(MAP_FAILED),
#if defined(FBIOGET_VSCREENINFO) && defined(FBIOGET_FSCREENINFO)
#elif defined(FBIOGTYPE)
	map_extrabase(MAP_FAILED),
	old_video_mode(),
#elif defined(WSDISPLAYIO_MODE_DUMBFB)
	old_video_mode(WSDISPLAYIO_MODE_EMUL),
#endif
	map_offset(0),
	map_size(0),
	map_stride(0),
	yres(0),
	xres(0),
	depth(0),
	screen(nullptr)
{
}

HODFromFramebuffer::~HODFromFramebuffer(
) {
	unmap();
	restore();
}

void
HODFromFramebuffer::unmap(
) {
	delete screen; screen = nullptr;
	if (MAP_FAILED != map_base) {
		munmap(map_base, map_size);
		map_base = MAP_FAILED;
		map_size = 0;
		map_stride = 0;
	}
#if defined(FBIOGET_VSCREENINFO) && defined(FBIOGET_FSCREENINFO)
#elif defined(FBIOGTYPE)
	if (MAP_FAILED != map_extrabase) {
		munmap(map_extrabase, pagesize);
		map_extrabase = MAP_FAILED;
	}
#elif defined(WSDISPLAYIO_MODE_DUMBFB)
	ioctl(device.get(), WSDISPLAYIO_SMODE, &old_video_mode);
#else
#	error "Don't know how to query your framebuffer device."
#endif
}

void
HODFromFramebuffer::map(
) {
	unmap();
	if (device.get() < 0) return;
#if defined(FBIOGET_VSCREENINFO) && defined(FBIOGET_FSCREENINFO)
	fb_fix_screeninfo fixed_info;
	fb_var_screeninfo variable_info;
	if (0 > ioctl(device.get(), FBIOGET_FSCREENINFO, &fixed_info)) return;
	// To change mode, use video= on the kernel command line or on the command line for the relevant fb module.
	if (0 > ioctl(device.get(), FBIOGET_VSCREENINFO, &variable_info)) return;
	yres = variable_info.yres;
	xres = variable_info.xres;
	depth = variable_info.bits_per_pixel;
	map_offset = 0U;
	map_size = (fixed_info.smem_len + pagesize - 1U) & ~(pagesize - 1U);
	map_stride = fixed_info.line_length;
#elif defined(FBIOGTYPE)
	struct fbtype t;
	if (0 > ioctl(device.get(), FBIOGTYPE, &t)) return;
	map_offset = 0U;
	// Yes, FreeBSD always rounds down to a multiple of the page size, not up.
	// Try to include any last partial page and you get EINVAL.
	// https://bugs.freebsd.org/bugzilla/show_bug.cgi?id=162373
	map_size = (map_offset + t.fb_size) & ~(pagesize - 1U);
	map_stride = FBTYPE_GET_STRIDE(&t);
	yres = t.fb_height;
	xres = t.fb_width;
	depth = t.fb_depth;
#elif defined(WSDISPLAYIO_MODE_DUMBFB)
	// This is not setting the mode, it is changing the memory map layout.
	// In dumb framebuffer mode, mapping the device only gets its framebuffer.
	// In graphics mode, mapping the device gets all of the (memory-mapped) registers as well.
	// In emulation mode, memory mapping the device is disallowed.
	int video_mode = WSDISPLAYIO_MODE_DUMBFB;
	if (0 > ioctl(device.get(), WSDISPLAYIO_GMODE, &old_video_mode)) return;
	if (0 > ioctl(device.get(), WSDISPLAYIO_SMODE, &video_mode)) return;
#if defined(WSDISPLAYIO_GET_FBINFO)
	wsdisplayio_fbinfo framebuffer_info;
	if (0 > ioctl(device.get(), WSDISPLAYIO_GET_FBINFO, &framebuffer_info)) return;
	map_offset = framebuffer_info.fbi_fboffset;
	map_size = framebuffer_info.fbi_fbsize;
	map_stride = framebuffer_info.fbi_stride;
	yres = framebuffer_info.fbi_height;
	xres = framebuffer_info.fbi_width;
	depth = framebuffer_info.fbi_bitsperpixel;
#elif defined(WSDISPLAYIO_LINEBYTES)
	wsdisplay_fbinfo mode_info;
	int linebytes;
	if (0 > ioctl(device.get(), WSDISPLAYIO_GINFO, &mode_info)) return;
	if (0 > ioctl(device.get(), WSDISPLAYIO_LINEBYTES, &linebytes)) return;
	map_offset = 0U;
	const std::size_t unrounded_size(static_cast<std::size_t>(linebytes) * yres);
	map_size = (unrounded_size + pagesize - 1U) & ~(pagesize - 1U);
	map_stride = static_cast<std::size_t>(linebytes);
	yres = mode_info.height;
	xres = mode_info.width;
	depth = mode_info.depth;
#else
#	error "Don't know how to query your wsdisplay device."
#endif
#else
#	error "Don't know how to query your framebuffer device."
#endif
	map_base = mmap(nullptr /* no address hint */, map_size, PROT_READ|PROT_WRITE, MAP_SHARED, device.get(), 0UL);
	if (MAP_FAILED == map_base) return;
#if defined(FBIOGET_VSCREENINFO) && defined(FBIOGET_FSCREENINFO)
#elif defined(FBIOGTYPE)
	map_extrabase = mmap(static_cast<char *>(map_base) + map_size, pagesize, PROT_READ|PROT_WRITE, MAP_ANON, -1, 0UL);
	if (MAP_FAILED == map_extrabase) return;
#endif
	screen = new ScreenBitmap(*this, yres, xres, depth);
	// The compositor size is rounded down to integer rows and columns from the framebuffer size.
	c.resize(shared.pixel_to_row(yres), shared.pixel_to_column(xres));
}

void *
HODFromFramebuffer::ScreenBitmap::GetStartOfLine(unsigned short y)
{
	return static_cast<uint8_t *>(framebuffer.map_base) + (framebuffer.map_offset + framebuffer.map_stride * y) % framebuffer.map_size;
}

bool HODFromFramebuffer::mode_selection(false);

void
HODFromFramebuffer::save_and_set_graphics_mode(
	const char * prog,
	const ProcessEnvironment & envs,
	const char * fb_filename,
	bool limit_80_columns
) {
	if (0 > device.get()) return;
	if (false) {
	fbio_error:
		die_errno(prog, envs, fb_filename);
	}

#if defined(FBIOGET_VSCREENINFO) && defined(FBIOGET_FSCREENINFO)
	// To change mode, use video= on the kernel command line or on the command line for the relevant fb module.
	fb_fix_screeninfo fixed_info;
	if (0 > ioctl(device.get(), FBIOGET_FSCREENINFO, &fixed_info)) goto fbio_error;
	if (fixed_info.type != FB_TYPE_PACKED_PIXELS)
		die_invalid(prog, envs, fb_filename, "Not a packed pixel device.");
	if (fixed_info.visual != FB_VISUAL_TRUECOLOR && fixed_info.visual != FB_VISUAL_DIRECTCOLOR)
		die_invalid(prog, envs, fb_filename, "Not a true/direct colour device.");
#elif defined(FBIOGTYPE)
	if (mode_selection) {
		if (0 > ioctl(device.get(), FBIO_GETMODE, &old_video_mode)) goto fbio_error;
		std::vector<video_info_t> mode_infos;
		int max_depth(0);
		for (int mode(0); mode <= M_VESA_MODE_MAX; ++mode) {
			video_info_t mi;
			mi.vi_mode = mode;
			if (0 > ioctl(device.get(), FBIO_MODEINFO, &mi)) continue;
			if ((V_INFO_GRAPHICS|V_INFO_LINEAR) != (mi.vi_flags & (V_INFO_GRAPHICS|V_INFO_LINEAR))) continue;
			if (mi.vi_mem_model != V_INFO_MM_DIRECT) continue;
			mode_infos.push_back(mi);
			if (max_depth < mi.vi_depth)
				max_depth = mi.vi_depth;
		}
		int video_mode = -1;
		video_info_t mode_info;
		int max_area(0);
		for (std::vector<video_info_t>::const_iterator mi(mode_infos.begin()), me(mode_infos.end()); mi != me; ++mi) {
			if (max_depth > mi->vi_depth) continue;
			if (limit_80_columns && mi->vi_width > 1280) continue;
			int area = (mi->vi_width * mi->vi_height);
			if (max_area > area) continue;
			video_mode = mi->vi_mode;
			max_area = area;
			mode_info = *mi;
			std::fprintf(stderr, "%s: INFO: %s: Graphics video mode candidate %d (%dW %dH %dD) found.\n", prog, fb_filename, video_mode, mi->vi_width, mi->vi_height, mi->vi_depth);
		}
		if (0 > video_mode)
			die_invalid(prog, envs, fb_filename, "No good graphics video mode found.");
		std::fprintf(stderr,
			"%s: DEBUG: mode %d: window %lx window_size %lx window_gran %lx buffer %lx buffer_size %lx planes %u\n", prog,
			mode_info.vi_mode, mode_info.vi_window, mode_info.vi_window_size, mode_info.vi_window_gran, mode_info.vi_buffer, mode_info.vi_buffer_size, mode_info.vi_planes
		);
		if (0 > ioctl(device.get(), FBIO_SETMODE, &video_mode)) goto fbio_error;
		video_adapter_info_t adapter_info;
		if (0 > ioctl(device.get(), FBIO_ADPINFO, &adapter_info)) goto fbio_error;
		std::fprintf(stderr,
			"%s: DEBUG: adapter: iobase %lx io_size %x mem_base %lx nem_size %x window %lx window_size %lx window_gran %lx buffer %lx buffer_size %lx\n", prog,
			adapter_info.va_io_base, adapter_info.va_io_size, adapter_info.va_mem_base, adapter_info.va_mem_size, adapter_info.va_window, adapter_info.va_window_size, adapter_info.va_window_gran, adapter_info.va_unused0, adapter_info.va_buffer_size
		);
	}
#elif defined(WSDISPLAYIO_SGFXMODE)
	// This is the mode selection logic.
	if (mode_selection) {
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
			const wsdisplayio_gfx_mode & m(modes[i]);
			if (limit_80_columns && m.width > 1280) continue;
			if (0 > ioctl(device.get(), WSDISPLAYIO_SETGFXMODE, &m)) continue;
			mode = n;
			break;
		}
		if (0 > mode)
			die_invalid(prog, envs, fb_filename, "No good graphics video mode found.");
	}
#endif
}

void
HODFromFramebuffer::restore()
{
	if (0 > device.get()) return;

#if defined(FBIOGET_VSCREENINFO) && defined(FBIOGET_FSCREENINFO)
	// No mode selection happened in this case.
#elif defined(FBIOGTYPE)
	if (mode_selection) {
		ioctl(device.get(), FBIO_SETMODE, &old_video_mode);
	}
#elif defined(WSDISPLAYIO_SGFXMODE)
	if (mode_selection) {
		const wsdisplayio_gfx_mode mode = { 640, 480, 8 };
		ioctl(device.get(), WSDISPLAYIO_SETGFXMODE, &m);
	}
#endif
}

void
console_fb_realizer [[gnu::noreturn]] (
	const char * & /*next_prog*/,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	HOD::Options hod_options;
	bool limit_80_columns(false);
	FontSpecList fonts;

	try {
		fontspec_definition vtfont_option('\0', "vtfont", "filename", "Use this font as a medium+bold upright vt font.", fonts, -1, CombinedFont::Font::UPRIGHT);
		fontspec_definition vtfont_faint_r_option('\0', "vtfont-faint-r", "filename", "Use this font as a light+demibold upright vt font.", fonts, -2, CombinedFont::Font::UPRIGHT);
		fontspec_definition vtfont_faint_o_option('\0', "vtfont-faint-o", "filename", "Use this font as a light+demibold oblique vt font.", fonts, -2, CombinedFont::Font::OBLIQUE);
		fontspec_definition vtfont_faint_i_option('\0', "vtfont-faint-i", "filename", "Use this font as a light+demibold italic vt font.", fonts, -2, CombinedFont::Font::ITALIC);
		fontspec_definition vtfont_normal_r_option('\0', "vtfont-normal-r", "filename", "Use this font as a medium+bold upright vt font.", fonts, -1, CombinedFont::Font::UPRIGHT);
		fontspec_definition vtfont_normal_o_option('\0', "vtfont-normal-o", "filename", "Use this font as a medium+bold oblique vt font.", fonts, -1, CombinedFont::Font::OBLIQUE);
		fontspec_definition vtfont_normal_i_option('\0', "vtfont-normal-i", "filename", "Use this font as a medium+bold italic vt font.", fonts, -1, CombinedFont::Font::ITALIC);
		popt::definition * vtfont_table[] = {
			&vtfont_option,
			&vtfont_faint_r_option,
			&vtfont_faint_o_option,
			&vtfont_faint_i_option,
			&vtfont_normal_r_option,
			&vtfont_normal_o_option,
			&vtfont_normal_i_option,
		};
		popt::table_definition vtfont_table_option(sizeof vtfont_table/sizeof *vtfont_table, vtfont_table, "VT font options");
		fontspec_definition font_light_r_option('\0', "font-light-r", "filename", "Use this font as a light-upright font.", fonts, CombinedFont::Font::LIGHT, CombinedFont::Font::UPRIGHT);
		fontspec_definition font_light_o_option('\0', "font-light-o", "filename", "Use this font as a light-oblique font.", fonts, CombinedFont::Font::LIGHT, CombinedFont::Font::OBLIQUE);
		fontspec_definition font_light_i_option('\0', "font-light-i", "filename", "Use this font as a light-italic font.", fonts, CombinedFont::Font::LIGHT, CombinedFont::Font::ITALIC);
		fontspec_definition font_medium_r_option('\0', "font-medium-r", "filename", "Use this font as a medium-upright font.", fonts, CombinedFont::Font::MEDIUM, CombinedFont::Font::UPRIGHT);
		fontspec_definition font_medium_o_option('\0', "font-medium-o", "filename", "Use this font as a medium-oblique font.", fonts, CombinedFont::Font::MEDIUM, CombinedFont::Font::OBLIQUE);
		fontspec_definition font_medium_i_option('\0', "font-medium-i", "filename", "Use this font as a medium-italic font.", fonts, CombinedFont::Font::MEDIUM, CombinedFont::Font::ITALIC);
		fontspec_definition font_demibold_r_option('\0', "font-demibold-r", "filename", "Use this font as a demibold-upright font.", fonts, CombinedFont::Font::DEMIBOLD, CombinedFont::Font::UPRIGHT);
		fontspec_definition font_demibold_o_option('\0', "font-demibold-o", "filename", "Use this font as a demibold-oblique font.", fonts, CombinedFont::Font::DEMIBOLD, CombinedFont::Font::OBLIQUE);
		fontspec_definition font_demibold_i_option('\0', "font-demibold-i", "filename", "Use this font as a demibold-italic font.", fonts, CombinedFont::Font::DEMIBOLD, CombinedFont::Font::ITALIC);
		fontspec_definition font_bold_r_option('\0', "font-bold-r", "filename", "Use this font as a bold-upright font.", fonts, CombinedFont::Font::BOLD, CombinedFont::Font::UPRIGHT);
		fontspec_definition font_bold_o_option('\0', "font-bold-o", "filename", "Use this font as a bold-oblique font.", fonts, CombinedFont::Font::BOLD, CombinedFont::Font::OBLIQUE);
		fontspec_definition font_bold_i_option('\0', "font-bold-i", "filename", "Use this font as a bold-italic font.", fonts, CombinedFont::Font::BOLD, CombinedFont::Font::ITALIC);
		popt::definition * font_table[] = {
			&font_light_r_option,
			&font_light_o_option,
			&font_light_i_option,
			&font_medium_r_option,
			&font_medium_o_option,
			&font_medium_i_option,
			&font_demibold_r_option,
			&font_demibold_o_option,
			&font_demibold_i_option,
			&font_bold_r_option,
			&font_bold_o_option,
			&font_bold_i_option,
		};
		popt::table_definition font_table_option(sizeof font_table/sizeof *font_table, font_table, "Raw font options");
		popt::bool_definition bold_as_colour_option('\0', "bold-as-colour", "Forcibly render boldface as a colour brightness change.", hod_options.bold_as_colour);
		popt::bool_definition limit_80_columns_option('\0', "80-columns", "Limit to no wider than 80 columns.", limit_80_columns);
		popt::unsigned_number_definition quadrant_option('\0', "quadrant", "number", "Position the terminal in quadrant 0, 1, 2, or 3.", hod_options.quadrant, 0);
		popt::bool_definition wrong_way_up_option('\0', "wrong-way-up", "Display from bottom to top.", hod_options.wrong_way_up);
		popt::bool_definition has_pointer_option('\0', "has-pointer", "Display the pointer.", hod_options.has_pointer);
		popt::definition * display_table[] = {
			&quadrant_option,
			&wrong_way_up_option,
			&bold_as_colour_option,
//			&faint_as_colour_option,
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__) || defined(__NetBSD__)
			&limit_80_columns_option,
#endif
			&has_pointer_option,
		};
		popt::table_definition display_table_option(sizeof display_table/sizeof *display_table, display_table, "Display options");
		popt::bool_definition auto_mode_selection_option('\0', "auto-mode-selection", "Pick a graphics mode automatically.", HODFromFramebuffer::mode_selection);
		popt::definition * io_table[] = {
			&auto_mode_selection_option,
		};
		popt::table_definition io_table_option(sizeof io_table/sizeof *io_table, io_table, "I/O options");
		popt::definition * top_table[] = {
			&display_table_option,
			&io_table_option,
			&vtfont_table_option,
			&font_table_option,
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

	Main main(prog, envs, false /* not mouse primary */);

	main.load_fonts(fonts);

	// Now open devices.

	{
		FileDescriptorOwner fd1(open_readwriteexisting_at(AT_FDCWD, framebuffer_filename));
		if (0 > fd1.get()) {
		hoderror:
			die_errno(prog, envs, framebuffer_filename);
		}
		HODFromFramebuffer * output = new HODFromFramebuffer(main, hod_options, fd1);
		if (!output) goto hoderror;
		main.add_device(output);
		output->save_and_set_graphics_mode(prog, envs, framebuffer_filename, limit_80_columns);
	}

	{
		std::list<std::string> device_paths;
		device_paths.push_back("eisa.pnpFB00." + std::string(basename_of(framebuffer_filename)));
		device_paths.push_back("eisa.pnpFB00");
		main.autoconfigure(device_paths, true /* has display device */, false /* no mouse */, false /* no keyboard */, false /* no NumLock key*/, false /* no LEDs */);
	}

	main.raise_acquire_signal();

	main.loop();

	throw EXIT_SUCCESS;
}

/* Kernel Virtual Terminal sharing ******************************************
// **************************************************************************
*/

namespace {

/// HIDs that are character devices with a terminal line discipline that speak the kbio protocol, that are kernel virtual terminals
typedef HIDSpeakingKBIO HIDFromKernelVT;

/// \brief a concrete switching controller that is a kernel virtual terminal character device
class KernelVTSwitchingController :
	public SwitchingController
{
public:
	KernelVTSwitchingController(FileDescriptorOwner &, int, int, unsigned long);
	~KernelVTSwitchingController();

	bool is_active();
	/// \name concrete implementation of the base class API
	/// @{
	virtual void save();
	virtual void set_mode();
	/// @}
protected:
	const int release_signal, acquire_signal;
	FileDescriptorOwner device;
#if defined(VT_GETSTATE) || defined(VT_GETACTIVE)
	unsigned long vtnr;
#endif
#if defined(WSDISPLAYIO_SMODE) && defined (WSDISPLAYIO_GMODE)
	// wscons does not have this mechanism.
#elif defined(KDSETMODE) && defined(KDGETMODE)
	long fbmode;	///< the saved do/do not draw flag for the kernel virtual terminal emulator's display
#endif
	struct vt_mode vtmode;

	void acknowledge_switch_to();
	void permit_switch_from();

	/// \name concrete implementation of the base class API
	/// @{
	virtual void restore();
	/// @}
};

}

KernelVTSwitchingController::KernelVTSwitchingController(
	FileDescriptorOwner & fd,
	int rs,
	int as,
	unsigned long kvtn
) :
	release_signal(rs),
	acquire_signal(as),
	device(fd.release()),
#if defined(VT_GETSTATE) || defined(VT_GETACTIVE)
	vtnr(kvtn),
#endif
#if defined(WSDISPLAYIO_SMODE) && defined (WSDISPLAYIO_GMODE)
	// wscons does not have this mechanism.
#elif defined(KDSETMODE) && defined(KDGETMODE)
	fbmode(),
#endif
	vtmode()
{
}

KernelVTSwitchingController::~KernelVTSwitchingController()
{
	restore();
}

void
KernelVTSwitchingController::save()
{
	if (0 <= device.get()) {
		ioctl(device.get(), VT_GETMODE, &vtmode);
#if defined(WSDISPLAYIO_SMODE) && defined (WSDISPLAYIO_GMODE)
		// Do nothing; this is all handled by the HODFromFramebuffer class.
#elif defined(KDSETMODE) && defined(KDGETMODE)
		// Save the prior do/do not draw mode of the kernel virtual terminal emulator.
		ioctl(device.get(), KDGETMODE, &fbmode);
#endif
	}
}

void
KernelVTSwitchingController::set_mode()
{
	if (0 <= device.get()) {
		// Set the VT switching to send the given release and acquire signals to this process.
		struct vt_mode m = {
			VT_PROCESS,
			0,
			static_cast<short>(release_signal),
			static_cast<short>(acquire_signal),
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__) || defined(__NetBSD__)
			// If you read doco somewhere that says "unused (set to 0)" then it is Linux doco.
			// FreeBSD checks that this field is a valid signal, even though it never uses it.
			// 0 is not a valid signal number, and causes an EINVAL return.
			SIGCONT
#else
			0
#endif
		};
		ioctl(device.get(), VT_SETMODE, &m);
#if defined(WSDISPLAYIO_SMODE) && defined (WSDISPLAYIO_GMODE)
		// Do nothing; this is all handled by the HODFromFramebuffer class.
#elif defined(KDSETMODE) && defined(KDGETMODE)
		// Tell the kernel virtual terminal emulator to not draw its character buffer, with KD_GRAPHICS or equivalent.
		ioctl(device.get(), KDSETMODE, KD_GRAPHICS);
#endif
	}
}

void
KernelVTSwitchingController::restore()
{
	if (0 <= device.get()) {
		ioctl(device.get(), VT_SETMODE, &vtmode);
#if defined(WSDISPLAYIO_SMODE) && defined (WSDISPLAYIO_GMODE)
		// Do nothing; this is all handled by the base class.
#elif defined(KDSETMODE) && defined(KDGETMODE)
		// Restore the prior do/do not draw mode of the kernel virtual terminal emulator.
		ioctl(device.get(), KDSETMODE, fbmode);
#endif
	}
}

void
KernelVTSwitchingController::acknowledge_switch_to()
{
	if (0 <= device.get())
		ioctl(device.get(), VT_RELDISP, VT_ACKACQ);
}

void
KernelVTSwitchingController::permit_switch_from()
{
	if (0 <= device.get())
		ioctl(device.get(), VT_RELDISP, VT_TRUE);
}

#if defined(VT_GETSTATE)

bool
KernelVTSwitchingController::is_active()
{
	if (-1 == device.get()) return true;
	struct vt_stat s;
	if (0 > ioctl(device.get(), VT_GETSTATE, &s)) return false;
	return s.v_active == vtnr;
}

#elif defined(VT_GETACTIVE)

bool
KernelVTSwitchingController::is_active()
{
	if (-1 == device.get()) return true;
	int active;
	if (0 > ioctl(device.get(), VT_GETACTIVE, &active)) return false;
	return static_cast<unsigned long>(active) == vtnr;
}

#else

bool
KernelVTSwitchingController::is_active()
{
	return false;
}

#endif

void
console_kvt_realizer [[gnu::noreturn]] (
	const char * & /*next_prog*/,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	HOD::Options hod_options;
	bool mouse_primary(false);
	bool limit_80_columns(false);
	FontSpecList fonts;

	try {
		fontspec_definition vtfont_option('\0', "vtfont", "filename", "Use this font as a medium+bold upright vt font.", fonts, -1, CombinedFont::Font::UPRIGHT);
		fontspec_definition vtfont_faint_r_option('\0', "vtfont-faint-r", "filename", "Use this font as a light+demibold upright vt font.", fonts, -2, CombinedFont::Font::UPRIGHT);
		fontspec_definition vtfont_faint_o_option('\0', "vtfont-faint-o", "filename", "Use this font as a light+demibold oblique vt font.", fonts, -2, CombinedFont::Font::OBLIQUE);
		fontspec_definition vtfont_faint_i_option('\0', "vtfont-faint-i", "filename", "Use this font as a light+demibold italic vt font.", fonts, -2, CombinedFont::Font::ITALIC);
		fontspec_definition vtfont_normal_r_option('\0', "vtfont-normal-r", "filename", "Use this font as a medium+bold upright vt font.", fonts, -1, CombinedFont::Font::UPRIGHT);
		fontspec_definition vtfont_normal_o_option('\0', "vtfont-normal-o", "filename", "Use this font as a medium+bold oblique vt font.", fonts, -1, CombinedFont::Font::OBLIQUE);
		fontspec_definition vtfont_normal_i_option('\0', "vtfont-normal-i", "filename", "Use this font as a medium+bold italic vt font.", fonts, -1, CombinedFont::Font::ITALIC);
		popt::definition * vtfont_table[] = {
			&vtfont_option,
			&vtfont_faint_r_option,
			&vtfont_faint_o_option,
			&vtfont_faint_i_option,
			&vtfont_normal_r_option,
			&vtfont_normal_o_option,
			&vtfont_normal_i_option,
		};
		fontspec_definition font_light_r_option('\0', "font-light-r", "filename", "Use this font as a light-upright font.", fonts, CombinedFont::Font::LIGHT, CombinedFont::Font::UPRIGHT);
		fontspec_definition font_light_o_option('\0', "font-light-o", "filename", "Use this font as a light-oblique font.", fonts, CombinedFont::Font::LIGHT, CombinedFont::Font::OBLIQUE);
		fontspec_definition font_light_i_option('\0', "font-light-i", "filename", "Use this font as a light-italic font.", fonts, CombinedFont::Font::LIGHT, CombinedFont::Font::ITALIC);
		fontspec_definition font_medium_r_option('\0', "font-medium-r", "filename", "Use this font as a medium-upright font.", fonts, CombinedFont::Font::MEDIUM, CombinedFont::Font::UPRIGHT);
		fontspec_definition font_medium_o_option('\0', "font-medium-o", "filename", "Use this font as a medium-oblique font.", fonts, CombinedFont::Font::MEDIUM, CombinedFont::Font::OBLIQUE);
		fontspec_definition font_medium_i_option('\0', "font-medium-i", "filename", "Use this font as a medium-italic font.", fonts, CombinedFont::Font::MEDIUM, CombinedFont::Font::ITALIC);
		fontspec_definition font_demibold_r_option('\0', "font-demibold-r", "filename", "Use this font as a demibold-upright font.", fonts, CombinedFont::Font::DEMIBOLD, CombinedFont::Font::UPRIGHT);
		fontspec_definition font_demibold_o_option('\0', "font-demibold-o", "filename", "Use this font as a demibold-oblique font.", fonts, CombinedFont::Font::DEMIBOLD, CombinedFont::Font::OBLIQUE);
		fontspec_definition font_demibold_i_option('\0', "font-demibold-i", "filename", "Use this font as a demibold-italic font.", fonts, CombinedFont::Font::DEMIBOLD, CombinedFont::Font::ITALIC);
		fontspec_definition font_bold_r_option('\0', "font-bold-r", "filename", "Use this font as a bold-upright font.", fonts, CombinedFont::Font::BOLD, CombinedFont::Font::UPRIGHT);
		fontspec_definition font_bold_o_option('\0', "font-bold-o", "filename", "Use this font as a bold-oblique font.", fonts, CombinedFont::Font::BOLD, CombinedFont::Font::OBLIQUE);
		fontspec_definition font_bold_i_option('\0', "font-bold-i", "filename", "Use this font as a bold-italic font.", fonts, CombinedFont::Font::BOLD, CombinedFont::Font::ITALIC);
		popt::definition * font_table[] = {
			&font_light_r_option,
			&font_light_o_option,
			&font_light_i_option,
			&font_medium_r_option,
			&font_medium_o_option,
			&font_medium_i_option,
			&font_demibold_r_option,
			&font_demibold_o_option,
			&font_demibold_i_option,
			&font_bold_r_option,
			&font_bold_o_option,
			&font_bold_i_option,
		};
		popt::table_definition vtfont_table_option(sizeof vtfont_table/sizeof *vtfont_table, vtfont_table, "VT font options");
		popt::table_definition font_table_option(sizeof font_table/sizeof *font_table, font_table, "Raw font options");
		popt::bool_definition bold_as_colour_option('\0', "bold-as-colour", "Forcibly render boldface as a colour brightness change.", hod_options.bold_as_colour);
//		popt::bool_definition faint_as_colour_option('\0', "faint-as-colour", "Forcibly render faint as a colour brightness change.", hod_options.faint_as_colour);
		popt::bool_definition limit_80_columns_option('\0', "80-columns", "Limit to no wider than 80 columns.", limit_80_columns);
		popt::unsigned_number_definition quadrant_option('\0', "quadrant", "number", "Position the terminal in quadrant 0, 1, 2, or 3.", hod_options.quadrant, 0);
		popt::bool_definition wrong_way_up_option('\0', "wrong-way-up", "Display from bottom to top.", hod_options.wrong_way_up);
		popt::definition * display_table[] = {
			&quadrant_option,
			&wrong_way_up_option,
			&bold_as_colour_option,
//			&faint_as_colour_option,
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__) || defined(__NetBSD__)
			&limit_80_columns_option,
#endif
		};
		popt::table_definition display_table_option(sizeof display_table/sizeof *display_table, display_table, "Display options");
		popt::bool_definition auto_mode_selection_option('\0', "auto-mode-selection", "Pick a graphics mode automatically.", HODFromFramebuffer::mode_selection);
		popt::bool_definition mouse_primary_option('\0', "mouse-primary", "Pass mouse position data to the terminal emulator.", mouse_primary);
		popt::definition * io_table[] = {
			&mouse_primary_option,
			&auto_mode_selection_option,
		};
		popt::table_definition io_table_option(sizeof io_table/sizeof *io_table, io_table, "I/O options");
		popt::definition * top_table[] = {
			&display_table_option,
			&io_table_option,
			&vtfont_table_option,
			&font_table_option,
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}

	if (!args.empty()) die_unexpected_argument(prog, args, envs);
	const char * kernel_vt_filename = envs.query("TTY");
	if (!kernel_vt_filename) {
		die_missing_environment_variable(prog, envs, "TTY");
	}

	Main main(prog, envs, mouse_primary);

	const bool has_input(isatty(STDIN_FILENO));
#if defined(__LINUX__) || defined(__linux__)
	const bool has_output(is_character_device(STDOUT_FILENO));
#else
	const bool has_output(isatty(STDOUT_FILENO));
#endif
	if (has_output)
		main.load_fonts(fonts);
	hod_options.has_pointer = has_input||mouse_primary;

	// Now open devices.
	const char * basename(basename_of(kernel_vt_filename));

	if (false) {
	error:
		die_errno(prog, envs, kernel_vt_filename);
	}

	{
		FileDescriptorOwner fdc(open_readwriteexisting_at(AT_FDCWD, kernel_vt_filename));
		if (0 > fdc.get()) goto error;

		// Calculate the switching index for the device in the VT/CONS API.
#if defined(VT_GETINDEX)
		int index;
		if (0 > ioctl(fdc.get(), VT_GETINDEX, &index)) goto error;
#elif defined(__LINUX__) || defined(__linux__)
		unsigned index;
		if (1 > std::sscanf(basename, "tty%u", &index))
			die_invalid(prog, envs, kernel_vt_filename, "Cannot parse KVT number from device name.");
#elif defined(__NetBSD__)
		unsigned index;
		char letter;
		if (2 > std::sscanf(basename, "tty%c%u", &letter, &index))
			die_invalid(prog, envs, kernel_vt_filename, "Cannot parse KVT number from device name.");
		++index;	// In the device names, indices are 0-based; but they are 1-based in the VT API.
#else
		int index = -1;
#endif

		// Print an informative message to the device.
		// Also, as a redundant measure, turn its cursor off with LINUXSCUSR.
		// If KD_GRAPHICS does not take effect, Debian's VT subsystem splats its character under the cursor onto the framebuffer every cursor blink.
		FileDescriptorOwner fd2(dup(fdc.get()));
		if (0 > fd2.get()) goto error;
		const FileStar vt(fdopen(fd2.get(), "w"));
		if (!vt) die_errno(prog, envs, kernel_vt_filename);
		fd2.release();
		std::fprintf(vt, "\033[H\033[2J\033[?25l\015%s: INFO: %s: %s\r\n", prog, kernel_vt_filename, "This kernel virtual terminal is in use for realizing user virtual terminals.");

		// Create and initialize the switching controller.
		KernelVTSwitchingController * dev = new KernelVTSwitchingController(fdc, SIGUSR1, SIGUSR2, index);
		if (!dev) goto error;
		main.switching_controller = std::shared_ptr<SwitchingController>(dev);
		main.capture_signals(SIGUSR1, SIGUSR2);
		dev->save();
		dev->set_mode();

		if (dev->is_active())
			main.raise_acquire_signal();
		else
			main.raise_release_signal();
	}

	if (has_input) {
		FileDescriptorOwner fd0(dup(STDIN_FILENO));
		if (0 > fd0.get()) goto error;
		HIDFromKernelVT * input = new HIDFromKernelVT(main, fd0);
		if (!input) goto error;
		main.add_device(input);
		input->save();
		input->set_mode();
	}

	if (has_output) {
		FileDescriptorOwner fd1(dup(STDOUT_FILENO));
		if (0 > fd1.get()) goto error;
		HODFromFramebuffer * output = new HODFromFramebuffer(main, hod_options, fd1);
		if (!output) goto error;
		main.add_device(output);
		output->save_and_set_graphics_mode(prog, envs, kernel_vt_filename, limit_80_columns);
	}

	{
		std::list<std::string> device_paths;
		device_paths.push_back("kvt." + std::string(basename));
		device_paths.push_back("kvt");
		main.autoconfigure(device_paths, has_output, false /* no mouse */, has_input /* implies keyboard */, has_input /* implies NumLock key */, has_input /* implies LEDs */);
	}

	main.loop();

	throw EXIT_SUCCESS;
}
