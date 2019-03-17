/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define _XOPEN_SOURCE_EXTENDED
#include <map>
#include <set>
#include <stack>
#include <deque>
#include <vector>
#include <algorithm>
#include <memory>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <clocale>
#include <cerrno>
#include <stdint.h>
#include <sys/stat.h>
#include "kqueue_common.h"
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#if defined(__LINUX__) || defined(__linux__)
#include <linux/vt.h>
#include <linux/kd.h>
#include <linux/input.h>
#include <endian.h>
#include "ttyutils.h"
#elif defined(__OpenBSD__)
#include <sys/uio.h>
#include <sys/endian.h>
#include <termios.h>
#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplay_usl_io.h>
#include <dev/usb/usb.h>
#include <dev/usb/usbhid.h>
#include "ttyutils.h"
#else
#include <sys/uio.h>
#include <sys/mouse.h>
#include <sys/kbio.h>
#include <sys/consio.h>
#include <sys/endian.h>
#include <termios.h>
#define HID_USAGE(u) ((u) & 0xffff)
#include <dev/usb/usbhid.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usb_ioctl.h>
#include "ttyutils.h"
#endif
#include <unistd.h>
#include <fcntl.h>
#include "utils.h"
#include "fdutils.h"
#include "pack.h"
#include "popt.h"
#include "ProcessEnvironment.h"
#include "CharacterCell.h"
#include "InputMessage.h"
#include "FileDescriptorOwner.h"
#include "FileStar.h"
#include "FramebufferIO.h"
#include "GraphicsInterface.h"
#include "vtfont.h"
#include "kbdmap.h"
#include "kbdmap_utils.h"
#include "kbdmap_default.h"
#include "Monospace16x16Font.h"
#include "CompositeFont.h"
#include "TUIDisplayCompositor.h"
#include "VirtualTerminalBackEnd.h"
#include "UnicodeClassification.h"
#include "SignalManagement.h"
#include <term.h>

/* Signal handling **********************************************************
// **************************************************************************
*/

static sig_atomic_t window_resized(false), terminate_signalled(false), interrupt_signalled(false), hangup_signalled(false), usr1_signalled(false), usr2_signalled(false);

static
void
handle_signal (
	int signo
) {
	switch (signo) {
		case SIGWINCH:	window_resized = true; break;
		case SIGTERM:	terminate_signalled = true; break;
		case SIGINT:	interrupt_signalled = true; break;
		case SIGHUP:	hangup_signalled = true; break;
		case SIGUSR1:	usr1_signalled = true; break;
		case SIGUSR2:	usr2_signalled = true; break;
	}
}

/* Loadable fonts ***********************************************************
// **************************************************************************
*/

static inline
ssize_t
pread(int fd, bsd_vtfont_header & header, off_t o)
{
	const ssize_t rc(pread(fd, &header, sizeof header, o));
	if (0 <= rc) {
		header.glyphs = be32toh(header.glyphs);
		for (unsigned i(0U); i < 4U; ++i) header.map_lengths[i] = be32toh(header.map_lengths[i]);
	}
	return rc;
}

static inline
ssize_t
pread(int fd, bsd_vtfont_map_entry & me, off_t o)
{
	const ssize_t rc(pread(fd, &me, sizeof me, o));
	if (0 <= rc) {
		me.character = be32toh(me.character);
		me.glyph = be16toh(me.glyph);
		me.count = be16toh(me.count);
	}
	return rc;
}

static inline
bool
good ( const bsd_vtfont_header & header )
{
	return 0 == std::memcmp(header.magic, "VFNT0002", sizeof header.magic);
}

static inline
bool
appropriate ( const bsd_vtfont_header & header )
{
	return 8U <= header.width && 16U >= header.width && 8U <= header.height && 16U >= header.height;
}

static inline
uint32_t
skipped_entries ( const bsd_vtfont_header & header, unsigned int n )
{
	uint32_t r(0U);
	for (unsigned int i(0U); i < n; ++i) r += header.map_lengths[i];
	return r;
}

static inline
uint32_t
entries ( const bsd_vtfont_header & header, unsigned int n )
{
	return header.map_lengths[n];
}

static inline
bool
has_right ( const bsd_vtfont_header & header, unsigned int n )
{
	return entries(header, n + 1U) > 0U;
}

static inline
void
LoadFont (
	const char * prog,
	CombinedFont & font,
	CombinedFont::Font::Weight weight,
	CombinedFont::Font::Slant slant,
	const char * name
) {
	FileDescriptorOwner font_fd(open_read_at(AT_FDCWD, name));
	if (0 > font_fd.get()) {
bad_file:
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, name, std::strerror(error));
		throw EXIT_FAILURE;
	}
	struct stat t;
	if (0 > fstat(font_fd.get(), &t)) 
		goto bad_file;

	bsd_vtfont_header header;
	if (0 > pread(font_fd.get(), header, 0U)) goto bad_file;
	if (!good(header)) {
		void * const base(mmap(0, t.st_size, PROT_READ, MAP_SHARED, font_fd.get(), 0));
		if (MAP_FAILED == base) goto bad_file;
		if (CombinedFont::MemoryMappedFont * f = font.AddMemoryMappedFont(weight, slant, 8U, 8U, base, t.st_size, 0U))
			f->AddMapping(0x00000000, 0U, t.st_size / 8U);
		return;
	} else 
	if (!appropriate(header)) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, name, "Not an appropriately sized font");
		throw EXIT_FAILURE;
	}

	unsigned vtfont_index; 
	switch (weight) {
		case CombinedFont::Font::LIGHT:
		case CombinedFont::Font::MEDIUM:	
			vtfont_index = 0U; 
			break;
		case CombinedFont::Font::DEMIBOLD:
		case CombinedFont::Font::BOLD:		
			vtfont_index = 2U; 
			break;
		default:
			return;
	}

	if (header.width <= 8U && has_right(header, vtfont_index)) {
		if (CombinedFont::LeftRightFileFont * f = font.AddLeftRightFileFont(font_fd.release(), weight, slant, header.height, header.width)) {
			bsd_vtfont_map_entry me;
			const off_t left_start(f->GlyphOffset(header.glyphs) + sizeof me * skipped_entries(header, vtfont_index));
			for (unsigned c(0U); c < entries(header, vtfont_index); ++c) {
				if (0 > pread(f->get(), me, left_start + c * sizeof me)) goto bad_file;
				f->AddLeftMapping(me.character, me.glyph, me.count + 1U);
			}
			const off_t right_start(f->GlyphOffset(header.glyphs) + sizeof me * skipped_entries(header, vtfont_index + 1U));
			for (unsigned c(0U); c < entries(header, vtfont_index + 1U); ++c) {
				if (0 > pread(f->get(), me, right_start + c * sizeof me)) goto bad_file;
				f->AddRightMapping(me.character, me.glyph, me.count + 1U);
			}
		}
	} else
	{
		if (CombinedFont::LeftFileFont * f = font.AddLeftFileFont(font_fd.release(), weight, slant, header.height, header.width)) {
			bsd_vtfont_map_entry me;
			const off_t start(f->GlyphOffset(header.glyphs) + sizeof me * skipped_entries(header, vtfont_index));
			for (unsigned c(0U); c < entries(header, vtfont_index); ++c) {
				if (0 > pread(f->get(), me, start + c * sizeof me)) goto bad_file;
				f->AddMapping(me.character, me.glyph, me.count + 1U);
			}
		}
	}
}

namespace {
struct FontSpec {
	std::string name;
	int weight;	///< -1 means match MEDIUM+BOLD, -2 means match LIGHT+DEMIBOLD
	CombinedFont::Font::Slant slant;
};

typedef std::list<FontSpec> FontSpecList;

struct fontspec_definition : public popt::compound_named_definition {
public:
	fontspec_definition(char s, const char * l, const char * a, const char * d, FontSpecList & f, int w, CombinedFont::Font::Slant i) : compound_named_definition(s, l, a, d), specs(f), weight(w), slant(i) {}
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

/* Keyboard layouts *********************************************************
// **************************************************************************
*/

namespace {
}

static inline
void
LoadKeyMap (
	const char * prog,
	KeyboardMap & map,
	const char * name
) {
	const FileStar f(std::fopen(name, "r"));
	if (!f) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, name, std::strerror(error));
		throw EXIT_FAILURE;
	}
	wipe(map);
	kbdmap_entry * p(&map[0][0]), * const e(p + sizeof map/sizeof *p);
	while (!std::feof(f)) {
		if (p >= e) break;
		uint32_t v[24] = { 0 };
		std::fread(v, sizeof v/sizeof *v, sizeof *v, f);
		const kbdmap_entry o = { 
			be32toh(v[0]), 
			{ 
				be32toh(v[ 8]), 
				be32toh(v[ 9]), 
				be32toh(v[10]), 
				be32toh(v[11]), 
				be32toh(v[12]), 
				be32toh(v[13]), 
				be32toh(v[14]), 
				be32toh(v[15]),
				be32toh(v[16]),
				be32toh(v[17]),
				be32toh(v[18]),
				be32toh(v[19]),
				be32toh(v[20]),
				be32toh(v[21]),
				be32toh(v[22]),
				be32toh(v[23]) 
			} 
		};
		*p++ = o;
	}
}

/* Keyboard shift state *****************************************************
// **************************************************************************
*/

namespace {

class KeyboardModifierState
{
public:
	KeyboardModifierState(bool);

	uint8_t modifiers() const;
	uint8_t nolevel_modifiers() const;
	uint8_t nolevel_nogroup_noctrl_modifiers() const;
	void event(uint16_t, uint8_t, uint32_t);
	void unlatch();
	bool num_LED() const { return num_lock(); }
	bool caps_LED() const { return caps_lock()||level2_lock(); }
	bool scroll_LED() const { return group2(); }
#if !defined(__LINUX__) && !defined(__linux__)
	bool shift_LED() const { return level2_lock()||level2(); }
#endif
	bool kana_LED() const { return false; }
	bool compose_LED() const { return false; }
	bool query_dirty_LEDs() const { return dirty_LEDs; }
	void clean_LEDs() { dirty_LEDs = false; }
	std::size_t query_kbdmap_parameter (const uint32_t cmd) const;

protected:
	static uint32_t bit(unsigned i) { return 1U << i; }
	uint32_t shift_state, latch_state, lock_state;
	bool dirty_LEDs;

	bool any(uint32_t) const;
	bool locked_or_latched(uint32_t) const;
	bool level2_lock() const;
	bool level3_lock() const;
	bool caps_lock() const;
	bool num_lock() const;
	bool level2() const;
	bool level3() const;
	bool group2() const;
	bool control() const;
	bool alt() const;
	bool super() const;
	std::size_t shiftable_index() const;
	std::size_t capsable_index() const;
	std::size_t numable_index() const;
	std::size_t funcable_index() const;
};

}

inline
KeyboardModifierState::KeyboardModifierState(
	bool initial_numlock
) : 
	shift_state(0U), 
	latch_state(0U), 
	lock_state(initial_numlock ? bit(KBDMAP_MODIFIER_NUM) : 0U),
	dirty_LEDs(true)
{
}

inline
bool 
KeyboardModifierState::any(uint32_t bits) const
{
	return bits & (shift_state|latch_state|lock_state);
}

inline
bool 
KeyboardModifierState::locked_or_latched(uint32_t bits) const
{
	return bits & (latch_state|lock_state);
}

inline
bool 
KeyboardModifierState::level2() const
{
	return any(bit(KBDMAP_MODIFIER_1ST_LEVEL2)|bit(KBDMAP_MODIFIER_2ND_LEVEL2));
}

inline
bool 
KeyboardModifierState::level3() const
{
	return any(bit(KBDMAP_MODIFIER_1ST_LEVEL3)|bit(KBDMAP_MODIFIER_2ND_LEVEL3));
}

inline
bool 
KeyboardModifierState::group2() const
{
	return any(bit(KBDMAP_MODIFIER_1ST_GROUP2)|bit(KBDMAP_MODIFIER_2ND_GROUP2));
}

inline
bool 
KeyboardModifierState::control() const
{
	return any(bit(KBDMAP_MODIFIER_1ST_CONTROL)|bit(KBDMAP_MODIFIER_2ND_CONTROL));
}

inline
bool 
KeyboardModifierState::alt() const
{
	return any(bit(KBDMAP_MODIFIER_1ST_ALT)|bit(KBDMAP_MODIFIER_2ND_ALT));
}

inline
bool 
KeyboardModifierState::super() const
{
	return any(bit(KBDMAP_MODIFIER_1ST_SUPER)|bit(KBDMAP_MODIFIER_2ND_SUPER));
}

inline
bool 
KeyboardModifierState::level2_lock() const
{
	return locked_or_latched(bit(KBDMAP_MODIFIER_LEVEL2));
}

inline
bool 
KeyboardModifierState::level3_lock() const
{
	return locked_or_latched(bit(KBDMAP_MODIFIER_LEVEL3));
}

inline
bool 
KeyboardModifierState::num_lock() const
{
	return locked_or_latched(bit(KBDMAP_MODIFIER_NUM));
}

inline
bool 
KeyboardModifierState::caps_lock() const
{
	return locked_or_latched(bit(KBDMAP_MODIFIER_CAPS));
}

uint8_t 
KeyboardModifierState::modifiers() const
{
	return 
		(control() ? INPUT_MODIFIER_CONTROL : 0) |
		(level2_lock()||level2() ? INPUT_MODIFIER_LEVEL2 : 0) |
		(alt()||level3_lock()||level3() ? INPUT_MODIFIER_LEVEL3 : 0) |
		(group2() ? INPUT_MODIFIER_GROUP2 : 0) |
		(super() ? INPUT_MODIFIER_SUPER : 0) ;
}

uint8_t 
KeyboardModifierState::nolevel_modifiers() const
{
	return 
		(control() ? INPUT_MODIFIER_CONTROL : 0) |
		(alt()||level3_lock()||level3() ? INPUT_MODIFIER_LEVEL3 : 0) |
		(group2() ? INPUT_MODIFIER_GROUP2 : 0) |
		(super() ? INPUT_MODIFIER_SUPER : 0) ;
}

uint8_t 
KeyboardModifierState::nolevel_nogroup_noctrl_modifiers() const
{
	return 
		(super() ? INPUT_MODIFIER_SUPER : 0) ;
}

inline
std::size_t 
KeyboardModifierState::shiftable_index() const
{
	return (level2_lock() || level2() ? 1U : 0U) + (control() ? 2U : 0) + (level3_lock() || level3() ? 4U : 0) + (group2() ? 8U : 0);
}

inline
std::size_t 
KeyboardModifierState::capsable_index() const
{
	const bool lock(caps_lock() || level2_lock());
	return (lock ^ level2() ? 1U : 0U) + (control() ? 2U : 0) + (level3_lock() || level3() ? 4U : 0) + (group2() ? 8U : 0);
}

inline
std::size_t 
KeyboardModifierState::numable_index() const
{
	const bool lock(num_lock() || level2_lock());
	return (lock ^ level2() ? 1U : 0U) + (control() ? 2U : 0) + (level3_lock() || level3() ? 4U : 0) + (group2() ? 8U : 0);
}

inline
std::size_t 
KeyboardModifierState::funcable_index() const
{
	return (level2_lock() || level2() ? 1U : 0U) + (control() ? 2U : 0) + (alt() ? 4U : 0) + (group2() ? 8U : 0);
}

inline
void
KeyboardModifierState::event(
	uint16_t k,
	uint8_t cmd,
	uint32_t v
) {
	const uint32_t bits(bit(k));
	switch (cmd) {
		case KBDMAP_MODIFIER_CMD_MOMENTARY:
			if (v > 0U)
				shift_state |= bits;
			else
				shift_state &= ~bits;
			break;
		case KBDMAP_MODIFIER_CMD_LATCH:
			if (v > 0U) {
				latch_state |= bits;
				dirty_LEDs = true;
			}
			break;
		case KBDMAP_MODIFIER_CMD_LOCK:
			if (1U == v) {
				lock_state ^= bits;
				dirty_LEDs = true;
			}
			break;
	}
	if (1U == v) {
		if (bits & (bit(KBDMAP_MODIFIER_1ST_LEVEL2)|bit(KBDMAP_MODIFIER_2ND_LEVEL2)))
			lock_state &= ~bit(KBDMAP_MODIFIER_LEVEL2);
	}
}

inline
void 
KeyboardModifierState::unlatch() 
{ 
	if (0U != latch_state)
		dirty_LEDs = true;
	latch_state = 0U; 
}

inline
std::size_t 
KeyboardModifierState::query_kbdmap_parameter (
	const uint32_t cmd
) const {
	switch (cmd) {
		default:	return -1U;
		case 'p':	return 0U;
		case 'n':	return numable_index();
		case 'c':	return capsable_index();
		case 's':	return shiftable_index();
		case 'f':	return funcable_index();
	}
}

/* Colour manipulation ******************************************************
// **************************************************************************
*/

static inline
void
invert (
	CharacterCell::colour_type & c
) {
	c.red = ~c.red;
	c.green = ~c.green;
	c.blue = ~c.blue;
}

static inline
void
dim (
	uint8_t & c
) {
	c = c > 0x40 ? c - 0x40 : 0x00;
}

static inline
void
dim (
	CharacterCell::colour_type & c
) {
	dim(c.red);
	dim(c.green);
	dim(c.blue);
}

static inline
void
bright (
	uint8_t & c
) {
	c = c < 0xC0 ? c + 0x40 : 0xff;
}

static inline
void
bright (
	CharacterCell::colour_type & c
) {
	bright(c.red);
	bright(c.green);
	bright(c.blue);
}

/* Realizing a virtual terminal onto a set of physical devices **************
// **************************************************************************
*/

namespace {
class Realizer
{
public:
	typedef unsigned short coordinate;
	Realizer(FramebufferIO & f, unsigned, bool wrong_way_up, bool, bool, bool, GraphicsInterface & g, Monospace16x16Font & mf, VirtualTerminalBackEnd & vt, TUIDisplayCompositor & c);
	~Realizer();

	enum { AXIS_W, AXIS_X, AXIS_Y, AXIS_Z, H_SCROLL, V_SCROLL };

	void set_refresh_needed() { refresh_needed = true; }
	void handle_update_event();
	void handle_refresh_event();
	void invalidate_all() { c.touch_all(); }

	static coordinate pixel_to_column(unsigned long x) { return x / CHARACTER_PIXEL_WIDTH; }
	static coordinate pixel_to_row(unsigned long y) { return y / CHARACTER_PIXEL_HEIGHT; }

	void handle_character_input(uint32_t, bool);
	void handle_mouse_abspos(uint8_t, const uint16_t axis, const unsigned long position, const unsigned long maximum);
	void handle_mouse_relpos(uint8_t, const uint16_t axis, int32_t amount);
	void handle_mouse_button(uint8_t, const uint16_t button, const bool value);
	void handle_screen_key (const uint32_t s, const uint8_t modifiers) ;
	void handle_system_key (const uint32_t k, const uint8_t modifiers) ;
	void handle_consumer_key (const uint32_t k, const uint8_t modifiers) ;
	void handle_extended_key (const uint32_t k, const uint8_t modifiers) ;
	void handle_function_key (const uint32_t k, const uint8_t modifiers) ;

protected:
	typedef GraphicsInterface::GlyphBitmapHandle GlyphBitmapHandle;
	struct GlyphCacheEntry {
		GlyphCacheEntry(GlyphBitmapHandle ha, uint32_t ch, CharacterCell::attribute_type a) : handle(ha), character(ch), attributes(a) {}
		GraphicsInterface::GlyphBitmapHandle handle;
		uint32_t character;
		CharacterCell::attribute_type attributes;
	};

	typedef std::list<GlyphCacheEntry> GlyphCache;
	enum { MAX_CACHED_GLYPHS = 256U };
	enum { CHARACTER_PIXEL_WIDTH = 16LU, CHARACTER_PIXEL_HEIGHT = 16LU };

	typedef std::vector<uint32_t> DeadKeysList;
	DeadKeysList dead_keys;

	unsigned int quadrant;	///< 0, 1, 2, or 3
	const bool wrong_way_up;	///< causes coordinate transforms between c and vt
	const bool faint_as_colour;
	const bool bold_as_colour;
	const bool has_pointer;
	FramebufferIO & fb;
	GraphicsInterface & gdi;
	Monospace16x16Font & font;
	GlyphCache glyph_cache;		///< a recently-used cache of handles to 2-colour bitmaps
	const GlyphBitmapHandle mouse_glyph_handle;
	const GlyphBitmapHandle underline_glyph_handle;
	const GlyphBitmapHandle bar_glyph_handle;
	const GlyphBitmapHandle box_glyph_handle;
	const GlyphBitmapHandle block_glyph_handle;
	const GlyphBitmapHandle star_glyph_handle;
	const CharacterCell::colour_type mouse_fg;

	bool refresh_needed, update_needed;
	void erase_new_to_backdrop ();
	void position_vt_visible_area ();
	void compose_new_from_vt ();
	void paint_changed_cells_onto_framebuffer();

	GlyphBitmapHandle GetCursorGlyphBitmap() const;
	GlyphBitmapHandle GetCachedGlyphBitmap(uint32_t character, CharacterCell::attribute_type attributes);
	void ApplyAttributesToGlyphBitmap(GlyphBitmapHandle handle , CharacterCell::attribute_type attributes);
	void PlotGreek(GlyphBitmapHandle handle, uint32_t character);

	unsigned long pointer_xpixel, pointer_ypixel;
	void set_pointer_x(uint8_t);
	void set_pointer_y(uint8_t);
	void adjust_mickeys(unsigned long & pos, int d, unsigned long max);

	void ClearDeadKeys() { dead_keys.clear(); }

	coordinate screen_y, screen_x;
	VirtualTerminalBackEnd & vt;
	TUIDisplayCompositor & c;
};

static const CharacterCell::colour_type overscan_fg(0,255,255,255), overscan_bg(0,0,0,0);
static const CharacterCell overscan_blank(' ', 0U, overscan_fg, overscan_bg);

}

Realizer::Realizer(
	FramebufferIO & f,
	unsigned q,
	bool wwu,
	bool fc,
	bool bc,
	bool hp,
	GraphicsInterface & g,
	Monospace16x16Font & mf,
	VirtualTerminalBackEnd & t,
	TUIDisplayCompositor & comp
) : 
	quadrant(q),
	wrong_way_up(wwu),
	faint_as_colour(fc),
	bold_as_colour(bc),
	has_pointer(hp),
	fb(f),
	gdi(g),
	font(mf),
	mouse_glyph_handle(gdi.MakeGlyphBitmap()),
	underline_glyph_handle(gdi.MakeGlyphBitmap()),
	bar_glyph_handle(gdi.MakeGlyphBitmap()),
	box_glyph_handle(gdi.MakeGlyphBitmap()),
	block_glyph_handle(gdi.MakeGlyphBitmap()),
	star_glyph_handle(gdi.MakeGlyphBitmap()),
	mouse_fg(31,0xFF,0xFF,0xFF),
	refresh_needed(true),
	update_needed(true),
	pointer_xpixel(0),
	pointer_ypixel(0),
	screen_y(0U),
	screen_x(0U),
	vt(t),
	c(comp)
{
	mouse_glyph_handle->Plot( 0, 0xFFFF);
	mouse_glyph_handle->Plot( 1, 0xC003);
	mouse_glyph_handle->Plot( 2, 0xA005);
	mouse_glyph_handle->Plot( 3, 0x9009);
	mouse_glyph_handle->Plot( 4, 0x8811);
	mouse_glyph_handle->Plot( 5, 0x8421);
	mouse_glyph_handle->Plot( 6, 0x8241);
	mouse_glyph_handle->Plot( 7, 0x8181);
	mouse_glyph_handle->Plot( 8, 0x8181);
	mouse_glyph_handle->Plot( 9, 0x8241);
	mouse_glyph_handle->Plot(10, 0x8421);
	mouse_glyph_handle->Plot(11, 0x8811);
	mouse_glyph_handle->Plot(12, 0x9009);
	mouse_glyph_handle->Plot(13, 0xA005);
	mouse_glyph_handle->Plot(14, 0xC003);
	mouse_glyph_handle->Plot(15, 0xFFFF);

	star_glyph_handle->Plot( 0, 0x8001);
	star_glyph_handle->Plot( 1, 0x4182);
	star_glyph_handle->Plot( 2, 0x2184);
	star_glyph_handle->Plot( 3, 0x1188);
	star_glyph_handle->Plot( 4, 0x0990);
	star_glyph_handle->Plot( 5, 0x05A0);
	star_glyph_handle->Plot( 6, 0x03C0);
	star_glyph_handle->Plot( 7, 0x7FFE);
	star_glyph_handle->Plot( 8, 0x7FFE);
	star_glyph_handle->Plot( 9, 0x03C0);
	star_glyph_handle->Plot(10, 0x05A0);
	star_glyph_handle->Plot(11, 0x0990);
	star_glyph_handle->Plot(12, 0x1188);
	star_glyph_handle->Plot(13, 0x2184);
	star_glyph_handle->Plot(14, 0x4182);
	star_glyph_handle->Plot(15, 0x8001);

	for (unsigned row(0U); row < 16U; ++row) {
		underline_glyph_handle->Plot(row, row < 14U ? 0x0000 : 0xFFFF);
		box_glyph_handle->Plot(row, row < 2U || row > 13U ? 0xFFFF : 0xC003);
		block_glyph_handle->Plot(row, 0xFFFF);
		bar_glyph_handle->Plot(row, 0xC000);
	}
}

Realizer::~Realizer()
{
	gdi.DeleteGlyphBitmap(star_glyph_handle);
	gdi.DeleteGlyphBitmap(block_glyph_handle);
	gdi.DeleteGlyphBitmap(box_glyph_handle);
	gdi.DeleteGlyphBitmap(bar_glyph_handle);
	gdi.DeleteGlyphBitmap(underline_glyph_handle);
	gdi.DeleteGlyphBitmap(mouse_glyph_handle);
}

inline
void
Realizer::ApplyAttributesToGlyphBitmap(GlyphBitmapHandle handle, CharacterCell::attribute_type attributes)
{
	switch (attributes & CharacterCell::UNDERLINES) {
		case 0U:		 break;
		case CharacterCell::SIMPLE_UNDERLINE:	handle->Plot(15, 0xFFFF); break;
		case CharacterCell::DOUBLE_UNDERLINE:	handle->Plot(14, 0xFFFF); handle->Plot(15, 0xFFFF); break;
		default:
		case CharacterCell::CURLY_UNDERLINE:	handle->Plot(14, 0x6666); handle->Plot(15, 0x9999); break;
		case CharacterCell::DOTTED_UNDERLINE:	handle->Plot(15, 0xAAAA); break;
		case CharacterCell::DASHED_UNDERLINE:	handle->Plot(15, 0xCCCC); break;
	}
	if (attributes & CharacterCell::STRIKETHROUGH) {
		handle->Plot(7, 0xFFFF);
		handle->Plot(8, 0xFFFF);
	}
	if (attributes & CharacterCell::INVERSE) {
		for (unsigned row(0U); row < 16U; ++row) handle->Plot(row, ~handle->Row(row));
	}
}

inline
void
Realizer::PlotGreek(GlyphBitmapHandle handle, uint32_t character)
{
	if (0x20 > character || (0xA0 > character && 0x80 <= character)) {
		// C0 and C1 control characters are boxes.
		handle->Plot(0, 0xFFFF);
		handle->Plot(1, 0xFFFF);
		for (unsigned row(2U); row < 14U; ++row) handle->Plot(row, 0xC003);
		handle->Plot(14, 0xFFFF);
		handle->Plot(15, 0xFFFF);
	} else
	if (0x20 == character || 0xA0 == character || 0x200B == character) {
		// Whitespace is blank.
		for (unsigned row(0U); row < 16U; ++row) handle->Plot(row, 0x0000);
	} else
	{
		// Everything else is greeked.
		handle->Plot(0, 0);
		handle->Plot(1, 0);
		for (unsigned row(2U); row < 14U; ++row) handle->Plot(row, 0x3FFC);
		handle->Plot(14, 0);
		handle->Plot(15, 0);
	}
}

Realizer::GlyphBitmapHandle 
Realizer::GetCursorGlyphBitmap() const
{
	switch (c.query_cursor_glyph()) {
		case CursorSprite::UNDERLINE:	return underline_glyph_handle;
		case CursorSprite::BAR:		return bar_glyph_handle;
		case CursorSprite::BOX:		return box_glyph_handle;
		case CursorSprite::BLOCK:	return block_glyph_handle;
		case CursorSprite::STAR:	return star_glyph_handle;
		default:			return star_glyph_handle;
	}
}

Realizer::GlyphBitmapHandle 
Realizer::GetCachedGlyphBitmap(uint32_t character, CharacterCell::attribute_type attributes)
{
	// A linear search isn't particularly nice, but we maintain the cache in recently-used order.
	for (GlyphCache::iterator i(glyph_cache.begin()); glyph_cache.end() != i; ++i) {
		if (i->character != character || i->attributes != attributes) continue;
		GlyphBitmapHandle handle(i->handle);
		glyph_cache.erase(i);
		glyph_cache.push_front(GlyphCacheEntry(handle, character, attributes));
		return handle;
	}
	GlyphBitmapHandle handle(gdi.MakeGlyphBitmap());
	if (const uint16_t * const s = font.ReadGlyph(character, CharacterCell::BOLD & attributes, CharacterCell::FAINT & attributes, CharacterCell::ITALIC & attributes))
		for (unsigned row(0U); row < 16U; ++row) handle->Plot(row, s[row]);
	else
		PlotGreek(handle, character);
	ApplyAttributesToGlyphBitmap(handle, attributes);
	while (glyph_cache.size() > MAX_CACHED_GLYPHS) {
		const GlyphCacheEntry e(glyph_cache.back());
		glyph_cache.pop_back();
		gdi.DeleteGlyphBitmap(e.handle);
	}
	glyph_cache.push_back(GlyphCacheEntry(handle, character, attributes));
	return handle;
}

inline
void
Realizer::paint_changed_cells_onto_framebuffer()
{
	const GraphicsInterface::ScreenBitmapHandle screen(gdi.GetScreenBitmap());

	for (unsigned row(0); row < c.query_h(); ++row) {
		for (unsigned col(0); col < c.query_w(); ++col) {
			TUIDisplayCompositor::DirtiableCell & cell(c.cur_at(row, col));
			if (!cell.touched()) continue;
			CharacterCell::attribute_type font_attributes(cell.attributes);
			CharacterCell::colour_type fg(cell.foreground), bg(cell.background);
			if (faint_as_colour) {
				if (CharacterCell::FAINT & font_attributes) {
					dim(fg);
//					dim(bg);
					font_attributes &= ~CharacterCell::FAINT;
				}
			}
			if (bold_as_colour) {
				if (CharacterCell::BOLD & font_attributes) {
					bright(fg);
//					bright(bg);
					font_attributes &= ~CharacterCell::BOLD;
				}
			}
			if (c.is_marked(true, row, col) && (CursorSprite::VISIBLE & c.query_cursor_attributes())) {
				const GlyphBitmapHandle cursor_handle(GetCursorGlyphBitmap());
				if (CharacterCell::INVISIBLE & font_attributes) {
					// Being invisible leaves just the cursor glyph.
					invert(fg);
					gdi.BitBLT(screen, cursor_handle, row * CHARACTER_PIXEL_HEIGHT, col * CHARACTER_PIXEL_WIDTH, fg, bg);
				} else {
					CharacterCell::colour_type fgs[2] = { fg, fg };
					CharacterCell::colour_type bgs[2] = { bg, bg };
					invert(fgs[1]);
					invert(bgs[1]);
					const GlyphBitmapHandle glyph_handle(GetCachedGlyphBitmap(cell.character, font_attributes));
					gdi.BitBLTMask(screen, glyph_handle, cursor_handle, row * CHARACTER_PIXEL_HEIGHT, col * CHARACTER_PIXEL_WIDTH, fgs, bgs);
				}
			} else {
				if (CharacterCell::INVISIBLE & font_attributes) {
					// Being invisible is always the same glyph, so we don't cache it.
					const GlyphBitmapHandle glyph_handle(gdi.MakeGlyphBitmap());
					PlotGreek(glyph_handle, 0x20);
					ApplyAttributesToGlyphBitmap(glyph_handle, font_attributes);
					gdi.BitBLT(screen, glyph_handle, row * CHARACTER_PIXEL_HEIGHT, col * CHARACTER_PIXEL_WIDTH, fg, bg);
					gdi.DeleteGlyphBitmap(glyph_handle);
				} else {
					const GlyphBitmapHandle glyph_handle(GetCachedGlyphBitmap(cell.character, font_attributes));
					gdi.BitBLT(screen, glyph_handle, row * CHARACTER_PIXEL_HEIGHT, col * CHARACTER_PIXEL_WIDTH, fg, bg);
				}
			}
			if (c.is_pointer(row, col) && (PointerSprite::VISIBLE & c.query_pointer_attributes())) {
				gdi.BitBLTAlpha(screen, mouse_glyph_handle, row * CHARACTER_PIXEL_HEIGHT, col * CHARACTER_PIXEL_WIDTH, mouse_fg);
			}
			cell.untouch();
		}
	}
}

inline
void
Realizer::erase_new_to_backdrop () 
{
	for (unsigned short row(0U); row < c.query_h(); ++row)
		for (unsigned short col(0U); col < c.query_w(); ++col)
			c.poke(row, col, overscan_blank);
}

/// \brief Clip and position the visible portion of the terminal's display buffer.
inline
void
Realizer::position_vt_visible_area (
) {
	// Glue the terminal window to the edges of the display screen buffer.
	screen_y = !(quadrant & 0x02) && vt.query_h() < c.query_h() ? c.query_h() - vt.query_h() : 0;
	screen_x = (1U == quadrant || 2U == quadrant) && vt.query_w() < c.query_w() ? c.query_w() - vt.query_w() : 0;
	// Tell the VirtualTerminal the size of the visible window; it will position it itself according to cursor placement.
	vt.set_visible_area(c.query_h() - screen_y, c.query_w() - screen_x);
}

/// \brief Render the terminal's display buffer and cursor/pointer states onto the TUI compositor.
inline
void
Realizer::compose_new_from_vt () 
{
	for (unsigned short row(0U); row < vt.query_visible_h(); ++row) {
		const unsigned short source_row(vt.query_visible_y() + row);
		const unsigned short dest_row(screen_y + (wrong_way_up ? vt.query_visible_h() - row - 1U : row));
		for (unsigned short col(0U); col < vt.query_visible_w(); ++col)
			c.poke(dest_row, col + screen_x, vt.at(source_row, vt.query_visible_x() + col));
	}
	const CursorSprite::attribute_type a(vt.query_cursor_attributes());
	// If the cursor is invisible, we are not guaranteed that the VirtualTerminal has kept the visible area around it.
	if (CursorSprite::VISIBLE & a) {
		const unsigned short cursor_y(screen_y + (wrong_way_up ? vt.query_visible_h() - vt.query_cursor_y() - 1U : vt.query_cursor_y()) - vt.query_visible_y());
		c.move_cursor(cursor_y, vt.query_cursor_x() - vt.query_visible_x() + screen_x);
	}
	c.set_cursor_state(a, vt.query_cursor_glyph());
	if (has_pointer)
		c.set_pointer_attributes(vt.query_pointer_attributes());
}

inline
void
Realizer::handle_refresh_event (
) {
	if (refresh_needed) {
		refresh_needed = false;
		erase_new_to_backdrop();
		position_vt_visible_area();
		compose_new_from_vt();
		update_needed = true;
	}
}

inline
void
Realizer::handle_update_event (
) {
	if (update_needed) {
		update_needed = false;
		c.repaint_new_to_cur();
		paint_changed_cells_onto_framebuffer();
	}
}

inline
void 
Realizer::set_pointer_x(
	uint8_t modifiers
) {
	const coordinate col(pixel_to_column(pointer_xpixel));
	if (c.change_pointer_col(col)) {
		vt.WriteInputMessage(MessageForMouseColumn(col, modifiers));
		update_needed = true;
	}
}

inline
void 
Realizer::set_pointer_y(
	uint8_t modifiers
) {
	coordinate row(pixel_to_row(pointer_ypixel));
	if (c.change_pointer_row(row)) {
		if (wrong_way_up)
			row = c.query_h() - row - 1U;
		vt.WriteInputMessage(MessageForMouseRow(row, modifiers));
		update_needed = true;
	}
}

inline
void
Realizer::handle_mouse_abspos(
	uint8_t modifiers,
	const uint16_t axis,
	const unsigned long abspos,
	const unsigned long maximum
) {
	switch (axis) {
		case AXIS_W:
			break;
		case AXIS_X:
			pointer_xpixel = (abspos * fb.query_xres()) / maximum;
			set_pointer_x(modifiers);
			break;
		case AXIS_Y:
			pointer_ypixel = (abspos * fb.query_yres()) / maximum;
			set_pointer_y(modifiers);
			break;
		case AXIS_Z:
			break;
		default:
			std::clog << "DEBUG: Unknown axis " << axis << " position " << abspos << "/" << maximum << " absolute\n";
			break;
	}
}

inline
void
Realizer::adjust_mickeys(
	unsigned long & pos, 
	int d, 
	unsigned long max
) {
	if (d < 0) {
		if (static_cast<unsigned int>(-d) > pos)
			pos = 0;
		else
			pos += d;
	} else
	if (d > 0) {
		if (max - d < pos)
			pos = max;
		else
			pos += d;
	}
}

inline
void
Realizer::handle_mouse_relpos(
	uint8_t modifiers,
	const uint16_t axis,
	int32_t amount
) {
	switch (axis) {
		case V_SCROLL:
			if (amount < -128) amount = -128;
			if (amount > 127) amount = 127;
			vt.WriteInputMessage(MessageForMouseWheel(0U, amount, modifiers));
			break;
		case H_SCROLL:
			if (amount < -128) amount = -128;
			if (amount > 127) amount = 127;
			vt.WriteInputMessage(MessageForMouseWheel(1U, amount, modifiers));
			break;
		case AXIS_W:
			break;
		case AXIS_X:
			adjust_mickeys(pointer_xpixel, amount, fb.query_xres());
			set_pointer_x(modifiers);
			break;
		case AXIS_Y:
			adjust_mickeys(pointer_ypixel, amount, fb.query_yres());
			set_pointer_y(modifiers);
			break;
		case AXIS_Z:
			break;
		default:
			std::clog << "DEBUG: Unknown axis " << axis << " position " << amount << " relative\n";
			break;
	}
}

inline
void
Realizer::handle_mouse_button(
	uint8_t modifiers,
	const uint16_t button,
	const bool value
) {
	vt.WriteInputMessage(MessageForMouseButton(button, value, modifiers));
}

namespace {

struct Combination {
	uint32_t c1, c2, r;
	// We will be using a binary search, so we need this operator.
	bool operator < ( const Combination & b ) const { return c1 < b.c1 || ( c1 == b.c1 && c2 < b.c2 ); }
};

}

static const
Combination
/// Rules for combining two dead keys into one, per ISO 9995-3.
peculiar_combinations1[] = {
	// Combiner + Combiner => Result
	{	0x00000300, 0x00000300, 0x0000030F	},
	{	0x00000302, 0x00000302, 0x00001DCD	},
	{	0x00000303, 0x00000303, 0x00000360	},
	{	0x00000304, 0x00000304, 0x0000035E	},
	{	0x00000306, 0x00000306, 0x0000035D	},
	{	0x00000307, 0x00000306, 0x00000310	},
	{	0x0000030D, 0x0000030D, 0x0000030E	},
	{	0x00000311, 0x00000311, 0x00000361	},
	{	0x00000313, 0x00000313, 0x00000315	},
	{	0x00000323, 0x00000323, 0x00000324	},
	{	0x00000329, 0x00000329, 0x00000348	},
	{	0x0000032E, 0x0000032E, 0x0000035C	},
	{	0x00000331, 0x00000331, 0x00000347	},
	{	0x00000332, 0x00000332, 0x0000035F	}, 
}, 
/// Rules for special dead key combinations, which apply in addition to the Unicode composition rules.
peculiar_combinations2[] = {
	// Combiner + Base => Result
	// The ISO 9995 sample code uses NUL (U+0000) for the result of some combinations.
	{	0x00000300, ' ', 0x00000060	},
	{	0x00000301, ' ', 0x000000B4	},
	{	0x00000302, ' ', 0x0000005E	},
	{	0x00000303, ' ', 0x0000007E	},
	{	0x00000304, ' ', 0x000000AF	},
	{	0x00000306, ' ', 0x000002D8	},
	{	0x00000307, ' ', 0x000002D9	},
	{	0x00000308, ' ', 0x000000A8	},
	{	0x00000309, ' ', 0x00000000	},
	{	0x0000030A, ' ', 0x000002DA	},
	{	0x0000030B, ' ', 0x000002DD	},
	{	0x0000030C, ' ', 0x000002C7	},
	{	0x0000030D, ' ', 0x000002C8	},
	{	0x0000030E, ' ', 0x00000000	},
	{	0x0000030F, ' ', 0x00000000	},
	{	0x00000310, ' ', 0x00000000	},
	{	0x00000311, ' ', 0x00000000	},
	{	0x00000313, ' ', 0x000002BC	},
	{	0x00000315, ' ', 0x00000000	},
	{	0x0000031B, ' ', 0x00000000	},
	{	0x00000323, ' ', 0x00000000	},
	{	0x00000324, ' ', 0x00000000	},
	{	0x00000325, ' ', 0x00000000	},
	{	0x00000326, ' ', 0x00000000	},
	{	0x00000327, ' ', 0x000000B8	},
	{	0x00000328, ' ', 0x000002DB	},
	{	0x00000329, ' ', 0x000002CC	},
	{	0x0000032D, ' ', 0x0000A788	},
	{	0x0000032E, ' ', 0x00000000	},
	{	0x00000331, ' ', 0x000002CD	},
	{	0x00000331, '<', 0x00002264	},
	{	0x00000331, '>', 0x00002265	},
	{	0x00000332, ' ', 0x00000000	},
	{	0x00000335, ' ', 0x00002212	},
	{	0x00000335, 'B', 0x00000243	},
	{	0x00000335, 'D', 0x00000110	},
	{	0x00000335, 'G', 0x000001E4	},
	{	0x00000335, 'H', 0x00000126	},
	{	0x00000335, 'I', 0x00000197	},
	{	0x00000335, 'J', 0x00000248	},
	{	0x00000335, 'L', 0x0000023D	},
	{	0x00000335, 'O', 0x0000019F	},
	{	0x00000335, 'P', 0x00002C63	},
	{	0x00000335, 'R', 0x0000024C	},
	{	0x00000335, 'T', 0x00000166	},
	{	0x00000335, 'U', 0x00000244	},
	{	0x00000335, 'Y', 0x0000024E	},
	{	0x00000335, 'Z', 0x000001B5	},
	{	0x00000335, 'b', 0x00000180	},
	{	0x00000335, 'd', 0x00000111	},
	{	0x00000335, 'g', 0x000001E5	},
	{	0x00000335, 'h', 0x00000127	},
	{	0x00000335, 'i', 0x00000268	},
	{	0x00000335, 'j', 0x00000249	},
	{	0x00000335, 'l', 0x0000019A	},
	{	0x00000335, 'o', 0x00000275	},
	{	0x00000335, 'p', 0x00001D7D	},
	{	0x00000335, 'r', 0x0000024D	},
	{	0x00000335, 't', 0x00000167	},
	{	0x00000335, 'u', 0x00000289	},
	{	0x00000335, 'y', 0x0000024F	},
	{	0x00000335, 'z', 0x000001B6	},
	{	0x00000338, ' ', 0x00002215	},
//	{	0x00000338, '=', 0x00002260	},	// This ISO 9995-3 rule duplicates Unicode.
	{	0x00000338, 'A', 0x0000023A	},
	{	0x00000338, 'C', 0x0000023B	},
	{	0x00000338, 'E', 0x00000246	},
	{	0x00000338, 'L', 0x00000141	},
	{	0x00000338, 'O', 0x000000D8	},
	{	0x00000338, 'T', 0x0000023E	},
	{	0x00000338, 'a', 0x00002C65	},
	{	0x00000338, 'c', 0x0000023C	},
	{	0x00000338, 'e', 0x00000247	},
	{	0x00000338, 'l', 0x00000142	},
	{	0x00000338, 'm', 0x000020A5	},
	{	0x00000338, 'o', 0x000000F8	},
	{	0x00000338, 't', 0x00002C66	},
	{	0x00000347, ' ', 0x00000000	},
	{	0x00000348, ' ', 0x00000000	},
	{	0x0000035C, ' ', 0x00000000	},
	{	0x0000035D, ' ', 0x00000000	},
	{	0x0000035E, ' ', 0x00000000	},
	{	0x0000035F, ' ', 0x00000000	},
	{	0x00000360, ' ', 0x00000000	},
	{	0x00000361, ' ', 0x00000000	},
	{	0x00000338, 0x000000B0, 0x00002300	},
},
/// Unicode composition rules.
unicode_combinations[] = {
	// Combiner + Base => Result
	{	0x00000300, 0x00000041, 0x000000C0	},
	{	0x00000300, 0x00000045, 0x000000C8	},
	{	0x00000300, 0x00000049, 0x000000CC	},
	{	0x00000300, 0x0000004E, 0x000001F8	},
	{	0x00000300, 0x0000004F, 0x000000D2	},
	{	0x00000300, 0x00000055, 0x000000D9	},
	{	0x00000300, 0x00000057, 0x00001E80	},
	{	0x00000300, 0x00000059, 0x00001EF2	},
	{	0x00000300, 0x00000061, 0x000000E0	},
	{	0x00000300, 0x00000065, 0x000000E8	},
	{	0x00000300, 0x00000069, 0x000000EC	},
	{	0x00000300, 0x0000006E, 0x000001F9	},
	{	0x00000300, 0x0000006F, 0x000000F2	},
	{	0x00000300, 0x00000075, 0x000000F9	},
	{	0x00000300, 0x00000077, 0x00001E81	},
	{	0x00000300, 0x00000079, 0x00001EF3	},
	{	0x00000300, 0x000000A8, 0x00001FED	},
	{	0x00000300, 0x000000C2, 0x00001EA6	},
	{	0x00000300, 0x000000CA, 0x00001EC0	},
	{	0x00000300, 0x000000D4, 0x00001ED2	},
	{	0x00000300, 0x000000DC, 0x000001DB	},
	{	0x00000300, 0x000000E2, 0x00001EA7	},
	{	0x00000300, 0x000000EA, 0x00001EC1	},
	{	0x00000300, 0x000000F4, 0x00001ED3	},
	{	0x00000300, 0x000000FC, 0x000001DC	},
	{	0x00000300, 0x00000102, 0x00001EB0	},
	{	0x00000300, 0x00000103, 0x00001EB1	},
	{	0x00000300, 0x00000112, 0x00001E14	},
	{	0x00000300, 0x00000113, 0x00001E15	},
	{	0x00000300, 0x0000014C, 0x00001E50	},
	{	0x00000300, 0x0000014D, 0x00001E51	},
	{	0x00000300, 0x000001A0, 0x00001EDC	},
	{	0x00000300, 0x000001A1, 0x00001EDD	},
	{	0x00000300, 0x000001AF, 0x00001EEA	},
	{	0x00000300, 0x000001B0, 0x00001EEB	},
	{	0x00000300, 0x00000391, 0x00001FBA	},
	{	0x00000300, 0x00000395, 0x00001FC8	},
	{	0x00000300, 0x00000397, 0x00001FCA	},
	{	0x00000300, 0x00000399, 0x00001FDA	},
	{	0x00000300, 0x0000039F, 0x00001FF8	},
	{	0x00000300, 0x000003A5, 0x00001FEA	},
	{	0x00000300, 0x000003A9, 0x00001FFA	},
	{	0x00000300, 0x000003B1, 0x00001F70	},
	{	0x00000300, 0x000003B5, 0x00001F72	},
	{	0x00000300, 0x000003B7, 0x00001F74	},
	{	0x00000300, 0x000003B9, 0x00001F76	},
	{	0x00000300, 0x000003BF, 0x00001F78	},
	{	0x00000300, 0x000003C5, 0x00001F7A	},
	{	0x00000300, 0x000003C9, 0x00001F7C	},
	{	0x00000300, 0x000003CA, 0x00001FD2	},
	{	0x00000300, 0x000003CB, 0x00001FE2	},
	{	0x00000300, 0x00000415, 0x00000400	},
	{	0x00000300, 0x00000418, 0x0000040D	},
	{	0x00000300, 0x00000435, 0x00000450	},
	{	0x00000300, 0x00000438, 0x0000045D	},
	{	0x00000300, 0x00001F00, 0x00001F02	},
	{	0x00000300, 0x00001F01, 0x00001F03	},
	{	0x00000300, 0x00001F08, 0x00001F0A	},
	{	0x00000300, 0x00001F09, 0x00001F0B	},
	{	0x00000300, 0x00001F10, 0x00001F12	},
	{	0x00000300, 0x00001F11, 0x00001F13	},
	{	0x00000300, 0x00001F18, 0x00001F1A	},
	{	0x00000300, 0x00001F19, 0x00001F1B	},
	{	0x00000300, 0x00001F20, 0x00001F22	},
	{	0x00000300, 0x00001F21, 0x00001F23	},
	{	0x00000300, 0x00001F28, 0x00001F2A	},
	{	0x00000300, 0x00001F29, 0x00001F2B	},
	{	0x00000300, 0x00001F30, 0x00001F32	},
	{	0x00000300, 0x00001F31, 0x00001F33	},
	{	0x00000300, 0x00001F38, 0x00001F3A	},
	{	0x00000300, 0x00001F39, 0x00001F3B	},
	{	0x00000300, 0x00001F40, 0x00001F42	},
	{	0x00000300, 0x00001F41, 0x00001F43	},
	{	0x00000300, 0x00001F48, 0x00001F4A	},
	{	0x00000300, 0x00001F49, 0x00001F4B	},
	{	0x00000300, 0x00001F50, 0x00001F52	},
	{	0x00000300, 0x00001F51, 0x00001F53	},
	{	0x00000300, 0x00001F59, 0x00001F5B	},
	{	0x00000300, 0x00001F60, 0x00001F62	},
	{	0x00000300, 0x00001F61, 0x00001F63	},
	{	0x00000300, 0x00001F68, 0x00001F6A	},
	{	0x00000300, 0x00001F69, 0x00001F6B	},
	{	0x00000300, 0x00001FBF, 0x00001FCD	},
	{	0x00000300, 0x00001FFE, 0x00001FDD	},
	{	0x00000301, 0x00000041, 0x000000C1	},
	{	0x00000301, 0x00000043, 0x00000106	},
	{	0x00000301, 0x00000045, 0x000000C9	},
	{	0x00000301, 0x00000047, 0x000001F4	},
	{	0x00000301, 0x00000049, 0x000000CD	},
	{	0x00000301, 0x0000004B, 0x00001E30	},
	{	0x00000301, 0x0000004C, 0x00000139	},
	{	0x00000301, 0x0000004D, 0x00001E3E	},
	{	0x00000301, 0x0000004E, 0x00000143	},
	{	0x00000301, 0x0000004F, 0x000000D3	},
	{	0x00000301, 0x00000050, 0x00001E54	},
	{	0x00000301, 0x00000052, 0x00000154	},
	{	0x00000301, 0x00000053, 0x0000015A	},
	{	0x00000301, 0x00000055, 0x000000DA	},
	{	0x00000301, 0x00000057, 0x00001E82	},
	{	0x00000301, 0x00000059, 0x000000DD	},
	{	0x00000301, 0x0000005A, 0x00000179	},
	{	0x00000301, 0x00000061, 0x000000E1	},
	{	0x00000301, 0x00000063, 0x00000107	},
	{	0x00000301, 0x00000065, 0x000000E9	},
	{	0x00000301, 0x00000067, 0x000001F5	},
	{	0x00000301, 0x00000069, 0x000000ED	},
	{	0x00000301, 0x0000006B, 0x00001E31	},
	{	0x00000301, 0x0000006C, 0x0000013A	},
	{	0x00000301, 0x0000006D, 0x00001E3F	},
	{	0x00000301, 0x0000006E, 0x00000144	},
	{	0x00000301, 0x0000006F, 0x000000F3	},
	{	0x00000301, 0x00000070, 0x00001E55	},
	{	0x00000301, 0x00000072, 0x00000155	},
	{	0x00000301, 0x00000073, 0x0000015B	},
	{	0x00000301, 0x00000075, 0x000000FA	},
	{	0x00000301, 0x00000077, 0x00001E83	},
	{	0x00000301, 0x00000079, 0x000000FD	},
	{	0x00000301, 0x0000007A, 0x0000017A	},
	{	0x00000301, 0x000000A8, 0x00000385	},
	{	0x00000301, 0x000000C2, 0x00001EA4	},
	{	0x00000301, 0x000000C5, 0x000001FA	},
	{	0x00000301, 0x000000C6, 0x000001FC	},
	{	0x00000301, 0x000000C7, 0x00001E08	},
	{	0x00000301, 0x000000CA, 0x00001EBE	},
	{	0x00000301, 0x000000CF, 0x00001E2E	},
	{	0x00000301, 0x000000D4, 0x00001ED0	},
	{	0x00000301, 0x000000D5, 0x00001E4C	},
	{	0x00000301, 0x000000D8, 0x000001FE	},
	{	0x00000301, 0x000000DC, 0x000001D7	},
	{	0x00000301, 0x000000E2, 0x00001EA5	},
	{	0x00000301, 0x000000E5, 0x000001FB	},
	{	0x00000301, 0x000000E6, 0x000001FD	},
	{	0x00000301, 0x000000E7, 0x00001E09	},
	{	0x00000301, 0x000000EA, 0x00001EBF	},
	{	0x00000301, 0x000000EF, 0x00001E2F	},
	{	0x00000301, 0x000000F4, 0x00001ED1	},
	{	0x00000301, 0x000000F5, 0x00001E4D	},
	{	0x00000301, 0x000000F8, 0x000001FF	},
	{	0x00000301, 0x000000FC, 0x000001D8	},
	{	0x00000301, 0x00000102, 0x00001EAE	},
	{	0x00000301, 0x00000103, 0x00001EAF	},
	{	0x00000301, 0x00000112, 0x00001E16	},
	{	0x00000301, 0x00000113, 0x00001E17	},
	{	0x00000301, 0x0000014C, 0x00001E52	},
	{	0x00000301, 0x0000014D, 0x00001E53	},
	{	0x00000301, 0x00000168, 0x00001E78	},
	{	0x00000301, 0x00000169, 0x00001E79	},
	{	0x00000301, 0x000001A0, 0x00001EDA	},
	{	0x00000301, 0x000001A1, 0x00001EDB	},
	{	0x00000301, 0x000001AF, 0x00001EE8	},
	{	0x00000301, 0x000001B0, 0x00001EE9	},
	{	0x00000301, 0x00000391, 0x00000386	},
	{	0x00000301, 0x00000395, 0x00000388	},
	{	0x00000301, 0x00000397, 0x00000389	},
	{	0x00000301, 0x00000399, 0x0000038A	},
	{	0x00000301, 0x0000039F, 0x0000038C	},
	{	0x00000301, 0x000003A5, 0x0000038E	},
	{	0x00000301, 0x000003A9, 0x0000038F	},
	{	0x00000301, 0x000003B1, 0x000003AC	},
	{	0x00000301, 0x000003B5, 0x000003AD	},
	{	0x00000301, 0x000003B7, 0x000003AE	},
	{	0x00000301, 0x000003B9, 0x000003AF	},
	{	0x00000301, 0x000003BF, 0x000003CC	},
	{	0x00000301, 0x000003C5, 0x000003CD	},
	{	0x00000301, 0x000003C9, 0x000003CE	},
	{	0x00000301, 0x000003CA, 0x00000390	},
	{	0x00000301, 0x000003CB, 0x000003B0	},
	{	0x00000301, 0x000003D2, 0x000003D3	},
	{	0x00000301, 0x00000413, 0x00000403	},
	{	0x00000301, 0x0000041A, 0x0000040C	},
	{	0x00000301, 0x00000433, 0x00000453	},
	{	0x00000301, 0x0000043A, 0x0000045C	},
	{	0x00000301, 0x00001F00, 0x00001F04	},
	{	0x00000301, 0x00001F01, 0x00001F05	},
	{	0x00000301, 0x00001F08, 0x00001F0C	},
	{	0x00000301, 0x00001F09, 0x00001F0D	},
	{	0x00000301, 0x00001F10, 0x00001F14	},
	{	0x00000301, 0x00001F11, 0x00001F15	},
	{	0x00000301, 0x00001F18, 0x00001F1C	},
	{	0x00000301, 0x00001F19, 0x00001F1D	},
	{	0x00000301, 0x00001F20, 0x00001F24	},
	{	0x00000301, 0x00001F21, 0x00001F25	},
	{	0x00000301, 0x00001F28, 0x00001F2C	},
	{	0x00000301, 0x00001F29, 0x00001F2D	},
	{	0x00000301, 0x00001F30, 0x00001F34	},
	{	0x00000301, 0x00001F31, 0x00001F35	},
	{	0x00000301, 0x00001F38, 0x00001F3C	},
	{	0x00000301, 0x00001F39, 0x00001F3D	},
	{	0x00000301, 0x00001F40, 0x00001F44	},
	{	0x00000301, 0x00001F41, 0x00001F45	},
	{	0x00000301, 0x00001F48, 0x00001F4C	},
	{	0x00000301, 0x00001F49, 0x00001F4D	},
	{	0x00000301, 0x00001F50, 0x00001F54	},
	{	0x00000301, 0x00001F51, 0x00001F55	},
	{	0x00000301, 0x00001F59, 0x00001F5D	},
	{	0x00000301, 0x00001F60, 0x00001F64	},
	{	0x00000301, 0x00001F61, 0x00001F65	},
	{	0x00000301, 0x00001F68, 0x00001F6C	},
	{	0x00000301, 0x00001F69, 0x00001F6D	},
	{	0x00000301, 0x00001FBF, 0x00001FCE	},
	{	0x00000301, 0x00001FFE, 0x00001FDE	},
	{	0x00000302, 0x00000041, 0x000000C2	},
	{	0x00000302, 0x00000043, 0x00000108	},
	{	0x00000302, 0x00000045, 0x000000CA	},
	{	0x00000302, 0x00000047, 0x0000011C	},
	{	0x00000302, 0x00000048, 0x00000124	},
	{	0x00000302, 0x00000049, 0x000000CE	},
	{	0x00000302, 0x0000004A, 0x00000134	},
	{	0x00000302, 0x0000004F, 0x000000D4	},
	{	0x00000302, 0x00000053, 0x0000015C	},
	{	0x00000302, 0x00000055, 0x000000DB	},
	{	0x00000302, 0x00000057, 0x00000174	},
	{	0x00000302, 0x00000059, 0x00000176	},
	{	0x00000302, 0x0000005A, 0x00001E90	},
	{	0x00000302, 0x00000061, 0x000000E2	},
	{	0x00000302, 0x00000063, 0x00000109	},
	{	0x00000302, 0x00000065, 0x000000EA	},
	{	0x00000302, 0x00000067, 0x0000011D	},
	{	0x00000302, 0x00000068, 0x00000125	},
	{	0x00000302, 0x00000069, 0x000000EE	},
	{	0x00000302, 0x0000006A, 0x00000135	},
	{	0x00000302, 0x0000006F, 0x000000F4	},
	{	0x00000302, 0x00000073, 0x0000015D	},
	{	0x00000302, 0x00000075, 0x000000FB	},
	{	0x00000302, 0x00000077, 0x00000175	},
	{	0x00000302, 0x00000079, 0x00000177	},
	{	0x00000302, 0x0000007A, 0x00001E91	},
	{	0x00000302, 0x00001EA0, 0x00001EAC	},
	{	0x00000302, 0x00001EA1, 0x00001EAD	},
	{	0x00000302, 0x00001EB8, 0x00001EC6	},
	{	0x00000302, 0x00001EB9, 0x00001EC7	},
	{	0x00000302, 0x00001ECC, 0x00001ED8	},
	{	0x00000302, 0x00001ECD, 0x00001ED9	},
	{	0x00000303, 0x00000041, 0x000000C3	},
	{	0x00000303, 0x00000045, 0x00001EBC	},
	{	0x00000303, 0x00000049, 0x00000128	},
	{	0x00000303, 0x0000004E, 0x000000D1	},
	{	0x00000303, 0x0000004F, 0x000000D5	},
	{	0x00000303, 0x00000055, 0x00000168	},
	{	0x00000303, 0x00000056, 0x00001E7C	},
	{	0x00000303, 0x00000059, 0x00001EF8	},
	{	0x00000303, 0x00000061, 0x000000E3	},
	{	0x00000303, 0x00000065, 0x00001EBD	},
	{	0x00000303, 0x00000069, 0x00000129	},
	{	0x00000303, 0x0000006E, 0x000000F1	},
	{	0x00000303, 0x0000006F, 0x000000F5	},
	{	0x00000303, 0x00000075, 0x00000169	},
	{	0x00000303, 0x00000076, 0x00001E7D	},
	{	0x00000303, 0x00000079, 0x00001EF9	},
	{	0x00000303, 0x000000C2, 0x00001EAA	},
	{	0x00000303, 0x000000CA, 0x00001EC4	},
	{	0x00000303, 0x000000D4, 0x00001ED6	},
	{	0x00000303, 0x000000E2, 0x00001EAB	},
	{	0x00000303, 0x000000EA, 0x00001EC5	},
	{	0x00000303, 0x000000F4, 0x00001ED7	},
	{	0x00000303, 0x00000102, 0x00001EB4	},
	{	0x00000303, 0x00000103, 0x00001EB5	},
	{	0x00000303, 0x000001A0, 0x00001EE0	},
	{	0x00000303, 0x000001A1, 0x00001EE1	},
	{	0x00000303, 0x000001AF, 0x00001EEE	},
	{	0x00000303, 0x000001B0, 0x00001EEF	},
	{	0x00000304, 0x00000041, 0x00000100	},
	{	0x00000304, 0x00000045, 0x00000112	},
	{	0x00000304, 0x00000047, 0x00001E20	},
	{	0x00000304, 0x00000049, 0x0000012A	},
	{	0x00000304, 0x0000004F, 0x0000014C	},
	{	0x00000304, 0x00000055, 0x0000016A	},
	{	0x00000304, 0x00000059, 0x00000232	},
	{	0x00000304, 0x00000061, 0x00000101	},
	{	0x00000304, 0x00000065, 0x00000113	},
	{	0x00000304, 0x00000067, 0x00001E21	},
	{	0x00000304, 0x00000069, 0x0000012B	},
	{	0x00000304, 0x0000006F, 0x0000014D	},
	{	0x00000304, 0x00000075, 0x0000016B	},
	{	0x00000304, 0x00000079, 0x00000233	},
	{	0x00000304, 0x000000C4, 0x000001DE	},
	{	0x00000304, 0x000000C6, 0x000001E2	},
	{	0x00000304, 0x000000D5, 0x0000022C	},
	{	0x00000304, 0x000000D6, 0x0000022A	},
	{	0x00000304, 0x000000DC, 0x000001D5	},
	{	0x00000304, 0x000000E4, 0x000001DF	},
	{	0x00000304, 0x000000E6, 0x000001E3	},
	{	0x00000304, 0x000000F5, 0x0000022D	},
	{	0x00000304, 0x000000F6, 0x0000022B	},
	{	0x00000304, 0x000000FC, 0x000001D6	},
	{	0x00000304, 0x000001EA, 0x000001EC	},
	{	0x00000304, 0x000001EB, 0x000001ED	},
	{	0x00000304, 0x00000226, 0x000001E0	},
	{	0x00000304, 0x00000227, 0x000001E1	},
	{	0x00000304, 0x0000022E, 0x00000230	},
	{	0x00000304, 0x0000022F, 0x00000231	},
	{	0x00000304, 0x00000391, 0x00001FB9	},
	{	0x00000304, 0x00000399, 0x00001FD9	},
	{	0x00000304, 0x000003A5, 0x00001FE9	},
	{	0x00000304, 0x000003B1, 0x00001FB1	},
	{	0x00000304, 0x000003B9, 0x00001FD1	},
	{	0x00000304, 0x000003C5, 0x00001FE1	},
	{	0x00000304, 0x00000418, 0x000004E2	},
	{	0x00000304, 0x00000423, 0x000004EE	},
	{	0x00000304, 0x00000438, 0x000004E3	},
	{	0x00000304, 0x00000443, 0x000004EF	},
	{	0x00000304, 0x00001E36, 0x00001E38	},
	{	0x00000304, 0x00001E37, 0x00001E39	},
	{	0x00000304, 0x00001E5A, 0x00001E5C	},
	{	0x00000304, 0x00001E5B, 0x00001E5D	},
	{	0x00000306, 0x00000041, 0x00000102	},
	{	0x00000306, 0x00000045, 0x00000114	},
	{	0x00000306, 0x00000047, 0x0000011E	},
	{	0x00000306, 0x00000049, 0x0000012C	},
	{	0x00000306, 0x0000004F, 0x0000014E	},
	{	0x00000306, 0x00000055, 0x0000016C	},
	{	0x00000306, 0x00000061, 0x00000103	},
	{	0x00000306, 0x00000065, 0x00000115	},
	{	0x00000306, 0x00000067, 0x0000011F	},
	{	0x00000306, 0x00000069, 0x0000012D	},
	{	0x00000306, 0x0000006F, 0x0000014F	},
	{	0x00000306, 0x00000075, 0x0000016D	},
	{	0x00000306, 0x00000228, 0x00001E1C	},
	{	0x00000306, 0x00000229, 0x00001E1D	},
	{	0x00000306, 0x00000391, 0x00001FB8	},
	{	0x00000306, 0x00000399, 0x00001FD8	},
	{	0x00000306, 0x000003A5, 0x00001FE8	},
	{	0x00000306, 0x000003B1, 0x00001FB0	},
	{	0x00000306, 0x000003B9, 0x00001FD0	},
	{	0x00000306, 0x000003C5, 0x00001FE0	},
	{	0x00000306, 0x00000410, 0x000004D0	},
	{	0x00000306, 0x00000415, 0x000004D6	},
	{	0x00000306, 0x00000416, 0x000004C1	},
	{	0x00000306, 0x00000418, 0x00000419	},
	{	0x00000306, 0x00000423, 0x0000040E	},
	{	0x00000306, 0x00000430, 0x000004D1	},
	{	0x00000306, 0x00000435, 0x000004D7	},
	{	0x00000306, 0x00000436, 0x000004C2	},
	{	0x00000306, 0x00000438, 0x00000439	},
	{	0x00000306, 0x00000443, 0x0000045E	},
	{	0x00000306, 0x00001EA0, 0x00001EB6	},
	{	0x00000306, 0x00001EA1, 0x00001EB7	},
	{	0x00000307, 0x00000041, 0x00000226	},
	{	0x00000307, 0x00000042, 0x00001E02	},
	{	0x00000307, 0x00000043, 0x0000010A	},
	{	0x00000307, 0x00000044, 0x00001E0A	},
	{	0x00000307, 0x00000045, 0x00000116	},
	{	0x00000307, 0x00000046, 0x00001E1E	},
	{	0x00000307, 0x00000047, 0x00000120	},
	{	0x00000307, 0x00000048, 0x00001E22	},
	{	0x00000307, 0x00000049, 0x00000130	},
	{	0x00000307, 0x0000004D, 0x00001E40	},
	{	0x00000307, 0x0000004E, 0x00001E44	},
	{	0x00000307, 0x0000004F, 0x0000022E	},
	{	0x00000307, 0x00000050, 0x00001E56	},
	{	0x00000307, 0x00000052, 0x00001E58	},
	{	0x00000307, 0x00000053, 0x00001E60	},
	{	0x00000307, 0x00000054, 0x00001E6A	},
	{	0x00000307, 0x00000057, 0x00001E86	},
	{	0x00000307, 0x00000058, 0x00001E8A	},
	{	0x00000307, 0x00000059, 0x00001E8E	},
	{	0x00000307, 0x0000005A, 0x0000017B	},
	{	0x00000307, 0x00000061, 0x00000227	},
	{	0x00000307, 0x00000062, 0x00001E03	},
	{	0x00000307, 0x00000063, 0x0000010B	},
	{	0x00000307, 0x00000064, 0x00001E0B	},
	{	0x00000307, 0x00000065, 0x00000117	},
	{	0x00000307, 0x00000066, 0x00001E1F	},
	{	0x00000307, 0x00000067, 0x00000121	},
	{	0x00000307, 0x00000068, 0x00001E23	},
	{	0x00000307, 0x0000006D, 0x00001E41	},
	{	0x00000307, 0x0000006E, 0x00001E45	},
	{	0x00000307, 0x0000006F, 0x0000022F	},
	{	0x00000307, 0x00000070, 0x00001E57	},
	{	0x00000307, 0x00000072, 0x00001E59	},
	{	0x00000307, 0x00000073, 0x00001E61	},
	{	0x00000307, 0x00000074, 0x00001E6B	},
	{	0x00000307, 0x00000077, 0x00001E87	},
	{	0x00000307, 0x00000078, 0x00001E8B	},
	{	0x00000307, 0x00000079, 0x00001E8F	},
	{	0x00000307, 0x0000007A, 0x0000017C	},
	{	0x00000307, 0x0000015A, 0x00001E64	},
	{	0x00000307, 0x0000015B, 0x00001E65	},
	{	0x00000307, 0x00000160, 0x00001E66	},
	{	0x00000307, 0x00000161, 0x00001E67	},
	{	0x00000307, 0x0000017F, 0x00001E9B	},
	{	0x00000307, 0x00001E62, 0x00001E68	},
	{	0x00000307, 0x00001E63, 0x00001E69	},
	{	0x00000308, 0x00000041, 0x000000C4	},
	{	0x00000308, 0x00000045, 0x000000CB	},
	{	0x00000308, 0x00000048, 0x00001E26	},
	{	0x00000308, 0x00000049, 0x000000CF	},
	{	0x00000308, 0x0000004F, 0x000000D6	},
	{	0x00000308, 0x00000055, 0x000000DC	},
	{	0x00000308, 0x00000057, 0x00001E84	},
	{	0x00000308, 0x00000058, 0x00001E8C	},
	{	0x00000308, 0x00000059, 0x00000178	},
	{	0x00000308, 0x00000061, 0x000000E4	},
	{	0x00000308, 0x00000065, 0x000000EB	},
	{	0x00000308, 0x00000068, 0x00001E27	},
	{	0x00000308, 0x00000069, 0x000000EF	},
	{	0x00000308, 0x0000006F, 0x000000F6	},
	{	0x00000308, 0x00000074, 0x00001E97	},
	{	0x00000308, 0x00000075, 0x000000FC	},
	{	0x00000308, 0x00000077, 0x00001E85	},
	{	0x00000308, 0x00000078, 0x00001E8D	},
	{	0x00000308, 0x00000079, 0x000000FF	},
	{	0x00000308, 0x000000D5, 0x00001E4E	},
	{	0x00000308, 0x000000F5, 0x00001E4F	},
	{	0x00000308, 0x0000016A, 0x00001E7A	},
	{	0x00000308, 0x0000016B, 0x00001E7B	},
	{	0x00000308, 0x00000399, 0x000003AA	},
	{	0x00000308, 0x000003A5, 0x000003AB	},
	{	0x00000308, 0x000003B9, 0x000003CA	},
	{	0x00000308, 0x000003C5, 0x000003CB	},
	{	0x00000308, 0x000003D2, 0x000003D4	},
	{	0x00000308, 0x00000406, 0x00000407	},
	{	0x00000308, 0x00000410, 0x000004D2	},
	{	0x00000308, 0x00000415, 0x00000401	},
	{	0x00000308, 0x00000416, 0x000004DC	},
	{	0x00000308, 0x00000417, 0x000004DE	},
	{	0x00000308, 0x00000418, 0x000004E4	},
	{	0x00000308, 0x0000041E, 0x000004E6	},
	{	0x00000308, 0x00000423, 0x000004F0	},
	{	0x00000308, 0x00000427, 0x000004F4	},
	{	0x00000308, 0x0000042B, 0x000004F8	},
	{	0x00000308, 0x0000042D, 0x000004EC	},
	{	0x00000308, 0x00000430, 0x000004D3	},
	{	0x00000308, 0x00000435, 0x00000451	},
	{	0x00000308, 0x00000436, 0x000004DD	},
	{	0x00000308, 0x00000437, 0x000004DF	},
	{	0x00000308, 0x00000438, 0x000004E5	},
	{	0x00000308, 0x0000043E, 0x000004E7	},
	{	0x00000308, 0x00000443, 0x000004F1	},
	{	0x00000308, 0x00000447, 0x000004F5	},
	{	0x00000308, 0x0000044B, 0x000004F9	},
	{	0x00000308, 0x0000044D, 0x000004ED	},
	{	0x00000308, 0x00000456, 0x00000457	},
	{	0x00000308, 0x000004D8, 0x000004DA	},
	{	0x00000308, 0x000004D9, 0x000004DB	},
	{	0x00000308, 0x000004E8, 0x000004EA	},
	{	0x00000308, 0x000004E9, 0x000004EB	},
	{	0x00000309, 0x00000041, 0x00001EA2	},
	{	0x00000309, 0x00000045, 0x00001EBA	},
	{	0x00000309, 0x00000049, 0x00001EC8	},
	{	0x00000309, 0x0000004F, 0x00001ECE	},
	{	0x00000309, 0x00000055, 0x00001EE6	},
	{	0x00000309, 0x00000059, 0x00001EF6	},
	{	0x00000309, 0x00000061, 0x00001EA3	},
	{	0x00000309, 0x00000065, 0x00001EBB	},
	{	0x00000309, 0x00000069, 0x00001EC9	},
	{	0x00000309, 0x0000006F, 0x00001ECF	},
	{	0x00000309, 0x00000075, 0x00001EE7	},
	{	0x00000309, 0x00000079, 0x00001EF7	},
	{	0x00000309, 0x000000C2, 0x00001EA8	},
	{	0x00000309, 0x000000CA, 0x00001EC2	},
	{	0x00000309, 0x000000D4, 0x00001ED4	},
	{	0x00000309, 0x000000E2, 0x00001EA9	},
	{	0x00000309, 0x000000EA, 0x00001EC3	},
	{	0x00000309, 0x000000F4, 0x00001ED5	},
	{	0x00000309, 0x00000102, 0x00001EB2	},
	{	0x00000309, 0x00000103, 0x00001EB3	},
	{	0x00000309, 0x000001A0, 0x00001EDE	},
	{	0x00000309, 0x000001A1, 0x00001EDF	},
	{	0x00000309, 0x000001AF, 0x00001EEC	},
	{	0x00000309, 0x000001B0, 0x00001EED	},
	{	0x0000030A, 0x00000041, 0x000000C5	},
	{	0x0000030A, 0x00000055, 0x0000016E	},
	{	0x0000030A, 0x00000061, 0x000000E5	},
	{	0x0000030A, 0x00000075, 0x0000016F	},
	{	0x0000030A, 0x00000077, 0x00001E98	},
	{	0x0000030A, 0x00000079, 0x00001E99	},
	{	0x0000030B, 0x0000004F, 0x00000150	},
	{	0x0000030B, 0x00000055, 0x00000170	},
	{	0x0000030B, 0x0000006F, 0x00000151	},
	{	0x0000030B, 0x00000075, 0x00000171	},
	{	0x0000030B, 0x00000423, 0x000004F2	},
	{	0x0000030B, 0x00000443, 0x000004F3	},
	{	0x0000030C, 0x00000041, 0x000001CD	},
	{	0x0000030C, 0x00000043, 0x0000010C	},
	{	0x0000030C, 0x00000044, 0x0000010E	},
	{	0x0000030C, 0x00000045, 0x0000011A	},
	{	0x0000030C, 0x00000047, 0x000001E6	},
	{	0x0000030C, 0x00000048, 0x0000021E	},
	{	0x0000030C, 0x00000049, 0x000001CF	},
	{	0x0000030C, 0x0000004B, 0x000001E8	},
	{	0x0000030C, 0x0000004C, 0x0000013D	},
	{	0x0000030C, 0x0000004E, 0x00000147	},
	{	0x0000030C, 0x0000004F, 0x000001D1	},
	{	0x0000030C, 0x00000052, 0x00000158	},
	{	0x0000030C, 0x00000053, 0x00000160	},
	{	0x0000030C, 0x00000054, 0x00000164	},
	{	0x0000030C, 0x00000055, 0x000001D3	},
	{	0x0000030C, 0x0000005A, 0x0000017D	},
	{	0x0000030C, 0x00000061, 0x000001CE	},
	{	0x0000030C, 0x00000063, 0x0000010D	},
	{	0x0000030C, 0x00000064, 0x0000010F	},
	{	0x0000030C, 0x00000065, 0x0000011B	},
	{	0x0000030C, 0x00000067, 0x000001E7	},
	{	0x0000030C, 0x00000068, 0x0000021F	},
	{	0x0000030C, 0x00000069, 0x000001D0	},
	{	0x0000030C, 0x0000006A, 0x000001F0	},
	{	0x0000030C, 0x0000006B, 0x000001E9	},
	{	0x0000030C, 0x0000006C, 0x0000013E	},
	{	0x0000030C, 0x0000006E, 0x00000148	},
	{	0x0000030C, 0x0000006F, 0x000001D2	},
	{	0x0000030C, 0x00000072, 0x00000159	},
	{	0x0000030C, 0x00000073, 0x00000161	},
	{	0x0000030C, 0x00000074, 0x00000165	},
	{	0x0000030C, 0x00000075, 0x000001D4	},
	{	0x0000030C, 0x0000007A, 0x0000017E	},
	{	0x0000030C, 0x000000DC, 0x000001D9	},
	{	0x0000030C, 0x000000FC, 0x000001DA	},
	{	0x0000030C, 0x000001B7, 0x000001EE	},
	{	0x0000030C, 0x00000292, 0x000001EF	},
	{	0x0000030F, 0x00000041, 0x00000200	},
	{	0x0000030F, 0x00000045, 0x00000204	},
	{	0x0000030F, 0x00000049, 0x00000208	},
	{	0x0000030F, 0x0000004F, 0x0000020C	},
	{	0x0000030F, 0x00000052, 0x00000210	},
	{	0x0000030F, 0x00000055, 0x00000214	},
	{	0x0000030F, 0x00000061, 0x00000201	},
	{	0x0000030F, 0x00000065, 0x00000205	},
	{	0x0000030F, 0x00000069, 0x00000209	},
	{	0x0000030F, 0x0000006F, 0x0000020D	},
	{	0x0000030F, 0x00000072, 0x00000211	},
	{	0x0000030F, 0x00000075, 0x00000215	},
	{	0x0000030F, 0x00000474, 0x00000476	},
	{	0x0000030F, 0x00000475, 0x00000477	},
	{	0x00000311, 0x00000041, 0x00000202	},
	{	0x00000311, 0x00000045, 0x00000206	},
	{	0x00000311, 0x00000049, 0x0000020A	},
	{	0x00000311, 0x0000004F, 0x0000020E	},
	{	0x00000311, 0x00000052, 0x00000212	},
	{	0x00000311, 0x00000055, 0x00000216	},
	{	0x00000311, 0x00000061, 0x00000203	},
	{	0x00000311, 0x00000065, 0x00000207	},
	{	0x00000311, 0x00000069, 0x0000020B	},
	{	0x00000311, 0x0000006F, 0x0000020F	},
	{	0x00000311, 0x00000072, 0x00000213	},
	{	0x00000311, 0x00000075, 0x00000217	},
	{	0x00000313, 0x00000391, 0x00001F08	},
	{	0x00000313, 0x00000395, 0x00001F18	},
	{	0x00000313, 0x00000397, 0x00001F28	},
	{	0x00000313, 0x00000399, 0x00001F38	},
	{	0x00000313, 0x0000039F, 0x00001F48	},
	{	0x00000313, 0x000003A9, 0x00001F68	},
	{	0x00000313, 0x000003B1, 0x00001F00	},
	{	0x00000313, 0x000003B5, 0x00001F10	},
	{	0x00000313, 0x000003B7, 0x00001F20	},
	{	0x00000313, 0x000003B9, 0x00001F30	},
	{	0x00000313, 0x000003BF, 0x00001F40	},
	{	0x00000313, 0x000003C1, 0x00001FE4	},
	{	0x00000313, 0x000003C5, 0x00001F50	},
	{	0x00000313, 0x000003C9, 0x00001F60	},
	{	0x00000314, 0x00000391, 0x00001F09	},
	{	0x00000314, 0x00000395, 0x00001F19	},
	{	0x00000314, 0x00000397, 0x00001F29	},
	{	0x00000314, 0x00000399, 0x00001F39	},
	{	0x00000314, 0x0000039F, 0x00001F49	},
	{	0x00000314, 0x000003A1, 0x00001FEC	},
	{	0x00000314, 0x000003A5, 0x00001F59	},
	{	0x00000314, 0x000003A9, 0x00001F69	},
	{	0x00000314, 0x000003B1, 0x00001F01	},
	{	0x00000314, 0x000003B5, 0x00001F11	},
	{	0x00000314, 0x000003B7, 0x00001F21	},
	{	0x00000314, 0x000003B9, 0x00001F31	},
	{	0x00000314, 0x000003BF, 0x00001F41	},
	{	0x00000314, 0x000003C1, 0x00001FE5	},
	{	0x00000314, 0x000003C5, 0x00001F51	},
	{	0x00000314, 0x000003C9, 0x00001F61	},
	{	0x0000031B, 0x0000004F, 0x000001A0	},
	{	0x0000031B, 0x00000055, 0x000001AF	},
	{	0x0000031B, 0x0000006F, 0x000001A1	},
	{	0x0000031B, 0x00000075, 0x000001B0	},
	{	0x00000323, 0x00000041, 0x00001EA0	},
	{	0x00000323, 0x00000042, 0x00001E04	},
	{	0x00000323, 0x00000044, 0x00001E0C	},
	{	0x00000323, 0x00000045, 0x00001EB8	},
	{	0x00000323, 0x00000048, 0x00001E24	},
	{	0x00000323, 0x00000049, 0x00001ECA	},
	{	0x00000323, 0x0000004B, 0x00001E32	},
	{	0x00000323, 0x0000004C, 0x00001E36	},
	{	0x00000323, 0x0000004D, 0x00001E42	},
	{	0x00000323, 0x0000004E, 0x00001E46	},
	{	0x00000323, 0x0000004F, 0x00001ECC	},
	{	0x00000323, 0x00000052, 0x00001E5A	},
	{	0x00000323, 0x00000053, 0x00001E62	},
	{	0x00000323, 0x00000054, 0x00001E6C	},
	{	0x00000323, 0x00000055, 0x00001EE4	},
	{	0x00000323, 0x00000056, 0x00001E7E	},
	{	0x00000323, 0x00000057, 0x00001E88	},
	{	0x00000323, 0x00000059, 0x00001EF4	},
	{	0x00000323, 0x0000005A, 0x00001E92	},
	{	0x00000323, 0x00000061, 0x00001EA1	},
	{	0x00000323, 0x00000062, 0x00001E05	},
	{	0x00000323, 0x00000064, 0x00001E0D	},
	{	0x00000323, 0x00000065, 0x00001EB9	},
	{	0x00000323, 0x00000068, 0x00001E25	},
	{	0x00000323, 0x00000069, 0x00001ECB	},
	{	0x00000323, 0x0000006B, 0x00001E33	},
	{	0x00000323, 0x0000006C, 0x00001E37	},
	{	0x00000323, 0x0000006D, 0x00001E43	},
	{	0x00000323, 0x0000006E, 0x00001E47	},
	{	0x00000323, 0x0000006F, 0x00001ECD	},
	{	0x00000323, 0x00000072, 0x00001E5B	},
	{	0x00000323, 0x00000073, 0x00001E63	},
	{	0x00000323, 0x00000074, 0x00001E6D	},
	{	0x00000323, 0x00000075, 0x00001EE5	},
	{	0x00000323, 0x00000076, 0x00001E7F	},
	{	0x00000323, 0x00000077, 0x00001E89	},
	{	0x00000323, 0x00000079, 0x00001EF5	},
	{	0x00000323, 0x0000007A, 0x00001E93	},
	{	0x00000323, 0x000001A0, 0x00001EE2	},
	{	0x00000323, 0x000001A1, 0x00001EE3	},
	{	0x00000323, 0x000001AF, 0x00001EF0	},
	{	0x00000323, 0x000001B0, 0x00001EF1	},
	{	0x00000324, 0x00000055, 0x00001E72	},
	{	0x00000324, 0x00000075, 0x00001E73	},
	{	0x00000325, 0x00000041, 0x00001E00	},
	{	0x00000325, 0x00000061, 0x00001E01	},
	{	0x00000326, 0x00000053, 0x00000218	},
	{	0x00000326, 0x00000054, 0x0000021A	},
	{	0x00000326, 0x00000073, 0x00000219	},
	{	0x00000326, 0x00000074, 0x0000021B	},
	{	0x00000327, 0x00000043, 0x000000C7	},
	{	0x00000327, 0x00000044, 0x00001E10	},
	{	0x00000327, 0x00000045, 0x00000228	},
	{	0x00000327, 0x00000047, 0x00000122	},
	{	0x00000327, 0x00000048, 0x00001E28	},
	{	0x00000327, 0x0000004B, 0x00000136	},
	{	0x00000327, 0x0000004C, 0x0000013B	},
	{	0x00000327, 0x0000004E, 0x00000145	},
	{	0x00000327, 0x00000052, 0x00000156	},
	{	0x00000327, 0x00000053, 0x0000015E	},
	{	0x00000327, 0x00000054, 0x00000162	},
	{	0x00000327, 0x00000063, 0x000000E7	},
	{	0x00000327, 0x00000064, 0x00001E11	},
	{	0x00000327, 0x00000065, 0x00000229	},
	{	0x00000327, 0x00000067, 0x00000123	},
	{	0x00000327, 0x00000068, 0x00001E29	},
	{	0x00000327, 0x0000006B, 0x00000137	},
	{	0x00000327, 0x0000006C, 0x0000013C	},
	{	0x00000327, 0x0000006E, 0x00000146	},
	{	0x00000327, 0x00000072, 0x00000157	},
	{	0x00000327, 0x00000073, 0x0000015F	},
	{	0x00000327, 0x00000074, 0x00000163	},
	{	0x00000328, 0x00000041, 0x00000104	},
	{	0x00000328, 0x00000045, 0x00000118	},
	{	0x00000328, 0x00000049, 0x0000012E	},
	{	0x00000328, 0x0000004F, 0x000001EA	},
	{	0x00000328, 0x00000055, 0x00000172	},
	{	0x00000328, 0x00000061, 0x00000105	},
	{	0x00000328, 0x00000065, 0x00000119	},
	{	0x00000328, 0x00000069, 0x0000012F	},
	{	0x00000328, 0x0000006F, 0x000001EB	},
	{	0x00000328, 0x00000075, 0x00000173	},
	{	0x0000032D, 0x00000044, 0x00001E12	},
	{	0x0000032D, 0x00000045, 0x00001E18	},
	{	0x0000032D, 0x0000004C, 0x00001E3C	},
	{	0x0000032D, 0x0000004E, 0x00001E4A	},
	{	0x0000032D, 0x00000054, 0x00001E70	},
	{	0x0000032D, 0x00000055, 0x00001E76	},
	{	0x0000032D, 0x00000064, 0x00001E13	},
	{	0x0000032D, 0x00000065, 0x00001E19	},
	{	0x0000032D, 0x0000006C, 0x00001E3D	},
	{	0x0000032D, 0x0000006E, 0x00001E4B	},
	{	0x0000032D, 0x00000074, 0x00001E71	},
	{	0x0000032D, 0x00000075, 0x00001E77	},
	{	0x0000032E, 0x00000048, 0x00001E2A	},
	{	0x0000032E, 0x00000068, 0x00001E2B	},
	{	0x00000330, 0x00000045, 0x00001E1A	},
	{	0x00000330, 0x00000049, 0x00001E2C	},
	{	0x00000330, 0x00000055, 0x00001E74	},
	{	0x00000330, 0x00000065, 0x00001E1B	},
	{	0x00000330, 0x00000069, 0x00001E2D	},
	{	0x00000330, 0x00000075, 0x00001E75	},
	{	0x00000331, 0x00000042, 0x00001E06	},
	{	0x00000331, 0x00000044, 0x00001E0E	},
	{	0x00000331, 0x0000004B, 0x00001E34	},
	{	0x00000331, 0x0000004C, 0x00001E3A	},
	{	0x00000331, 0x0000004E, 0x00001E48	},
	{	0x00000331, 0x00000052, 0x00001E5E	},
	{	0x00000331, 0x00000054, 0x00001E6E	},
	{	0x00000331, 0x0000005A, 0x00001E94	},
	{	0x00000331, 0x00000062, 0x00001E07	},
	{	0x00000331, 0x00000064, 0x00001E0F	},
	{	0x00000331, 0x00000068, 0x00001E96	},
	{	0x00000331, 0x0000006B, 0x00001E35	},
	{	0x00000331, 0x0000006C, 0x00001E3B	},
	{	0x00000331, 0x0000006E, 0x00001E49	},
	{	0x00000331, 0x00000072, 0x00001E5F	},
	{	0x00000331, 0x00000074, 0x00001E6F	},
	{	0x00000331, 0x0000007A, 0x00001E95	},
	{	0x00000338, 0x0000003C, 0x0000226E	},
	{	0x00000338, 0x0000003D, 0x00002260	},
	{	0x00000338, 0x0000003E, 0x0000226F	},
	{	0x00000338, 0x00002190, 0x0000219A	},
	{	0x00000338, 0x00002192, 0x0000219B	},
	{	0x00000338, 0x00002194, 0x000021AE	},
	{	0x00000338, 0x000021D0, 0x000021CD	},
	{	0x00000338, 0x000021D2, 0x000021CF	},
	{	0x00000338, 0x000021D4, 0x000021CE	},
	{	0x00000338, 0x00002203, 0x00002204	},
	{	0x00000338, 0x00002208, 0x00002209	},
	{	0x00000338, 0x0000220B, 0x0000220C	},
	{	0x00000338, 0x00002223, 0x00002224	},
	{	0x00000338, 0x00002225, 0x00002226	},
	{	0x00000338, 0x0000223C, 0x00002241	},
	{	0x00000338, 0x00002243, 0x00002244	},
	{	0x00000338, 0x00002245, 0x00002247	},
	{	0x00000338, 0x00002248, 0x00002249	},
	{	0x00000338, 0x0000224D, 0x0000226D	},
	{	0x00000338, 0x00002261, 0x00002262	},
	{	0x00000338, 0x00002264, 0x00002270	},
	{	0x00000338, 0x00002265, 0x00002271	},
	{	0x00000338, 0x00002272, 0x00002274	},
	{	0x00000338, 0x00002273, 0x00002275	},
	{	0x00000338, 0x00002276, 0x00002278	},
	{	0x00000338, 0x00002277, 0x00002279	},
	{	0x00000338, 0x0000227A, 0x00002280	},
	{	0x00000338, 0x0000227B, 0x00002281	},
	{	0x00000338, 0x0000227C, 0x000022E0	},
	{	0x00000338, 0x0000227D, 0x000022E1	},
	{	0x00000338, 0x00002282, 0x00002284	},
	{	0x00000338, 0x00002283, 0x00002285	},
	{	0x00000338, 0x00002286, 0x00002288	},
	{	0x00000338, 0x00002287, 0x00002289	},
	{	0x00000338, 0x00002291, 0x000022E2	},
	{	0x00000338, 0x00002292, 0x000022E3	},
	{	0x00000338, 0x000022A2, 0x000022AC	},
	{	0x00000338, 0x000022A8, 0x000022AD	},
	{	0x00000338, 0x000022A9, 0x000022AE	},
	{	0x00000338, 0x000022AB, 0x000022AF	},
	{	0x00000338, 0x000022B2, 0x000022EA	},
	{	0x00000338, 0x000022B3, 0x000022EB	},
	{	0x00000338, 0x000022B4, 0x000022EC	},
	{	0x00000338, 0x000022B5, 0x000022ED	},
	{	0x00000342, 0x000000A8, 0x00001FC1	},
	{	0x00000342, 0x000003B1, 0x00001FB6	},
	{	0x00000342, 0x000003B7, 0x00001FC6	},
	{	0x00000342, 0x000003B9, 0x00001FD6	},
	{	0x00000342, 0x000003C5, 0x00001FE6	},
	{	0x00000342, 0x000003C9, 0x00001FF6	},
	{	0x00000342, 0x000003CA, 0x00001FD7	},
	{	0x00000342, 0x000003CB, 0x00001FE7	},
	{	0x00000342, 0x00001F00, 0x00001F06	},
	{	0x00000342, 0x00001F01, 0x00001F07	},
	{	0x00000342, 0x00001F08, 0x00001F0E	},
	{	0x00000342, 0x00001F09, 0x00001F0F	},
	{	0x00000342, 0x00001F20, 0x00001F26	},
	{	0x00000342, 0x00001F21, 0x00001F27	},
	{	0x00000342, 0x00001F28, 0x00001F2E	},
	{	0x00000342, 0x00001F29, 0x00001F2F	},
	{	0x00000342, 0x00001F30, 0x00001F36	},
	{	0x00000342, 0x00001F31, 0x00001F37	},
	{	0x00000342, 0x00001F38, 0x00001F3E	},
	{	0x00000342, 0x00001F39, 0x00001F3F	},
	{	0x00000342, 0x00001F50, 0x00001F56	},
	{	0x00000342, 0x00001F51, 0x00001F57	},
	{	0x00000342, 0x00001F59, 0x00001F5F	},
	{	0x00000342, 0x00001F60, 0x00001F66	},
	{	0x00000342, 0x00001F61, 0x00001F67	},
	{	0x00000342, 0x00001F68, 0x00001F6E	},
	{	0x00000342, 0x00001F69, 0x00001F6F	},
	{	0x00000342, 0x00001FBF, 0x00001FCF	},
	{	0x00000342, 0x00001FFE, 0x00001FDF	},
	{	0x00000345, 0x00000391, 0x00001FBC	},
	{	0x00000345, 0x00000397, 0x00001FCC	},
	{	0x00000345, 0x000003A9, 0x00001FFC	},
	{	0x00000345, 0x000003AC, 0x00001FB4	},
	{	0x00000345, 0x000003AE, 0x00001FC4	},
	{	0x00000345, 0x000003B1, 0x00001FB3	},
	{	0x00000345, 0x000003B7, 0x00001FC3	},
	{	0x00000345, 0x000003C9, 0x00001FF3	},
	{	0x00000345, 0x000003CE, 0x00001FF4	},
	{	0x00000345, 0x00001F00, 0x00001F80	},
	{	0x00000345, 0x00001F01, 0x00001F81	},
	{	0x00000345, 0x00001F02, 0x00001F82	},
	{	0x00000345, 0x00001F03, 0x00001F83	},
	{	0x00000345, 0x00001F04, 0x00001F84	},
	{	0x00000345, 0x00001F05, 0x00001F85	},
	{	0x00000345, 0x00001F06, 0x00001F86	},
	{	0x00000345, 0x00001F07, 0x00001F87	},
	{	0x00000345, 0x00001F08, 0x00001F88	},
	{	0x00000345, 0x00001F09, 0x00001F89	},
	{	0x00000345, 0x00001F0A, 0x00001F8A	},
	{	0x00000345, 0x00001F0B, 0x00001F8B	},
	{	0x00000345, 0x00001F0C, 0x00001F8C	},
	{	0x00000345, 0x00001F0D, 0x00001F8D	},
	{	0x00000345, 0x00001F0E, 0x00001F8E	},
	{	0x00000345, 0x00001F0F, 0x00001F8F	},
	{	0x00000345, 0x00001F20, 0x00001F90	},
	{	0x00000345, 0x00001F21, 0x00001F91	},
	{	0x00000345, 0x00001F22, 0x00001F92	},
	{	0x00000345, 0x00001F23, 0x00001F93	},
	{	0x00000345, 0x00001F24, 0x00001F94	},
	{	0x00000345, 0x00001F25, 0x00001F95	},
	{	0x00000345, 0x00001F26, 0x00001F96	},
	{	0x00000345, 0x00001F27, 0x00001F97	},
	{	0x00000345, 0x00001F28, 0x00001F98	},
	{	0x00000345, 0x00001F29, 0x00001F99	},
	{	0x00000345, 0x00001F2A, 0x00001F9A	},
	{	0x00000345, 0x00001F2B, 0x00001F9B	},
	{	0x00000345, 0x00001F2C, 0x00001F9C	},
	{	0x00000345, 0x00001F2D, 0x00001F9D	},
	{	0x00000345, 0x00001F2E, 0x00001F9E	},
	{	0x00000345, 0x00001F2F, 0x00001F9F	},
	{	0x00000345, 0x00001F60, 0x00001FA0	},
	{	0x00000345, 0x00001F61, 0x00001FA1	},
	{	0x00000345, 0x00001F62, 0x00001FA2	},
	{	0x00000345, 0x00001F63, 0x00001FA3	},
	{	0x00000345, 0x00001F64, 0x00001FA4	},
	{	0x00000345, 0x00001F65, 0x00001FA5	},
	{	0x00000345, 0x00001F66, 0x00001FA6	},
	{	0x00000345, 0x00001F67, 0x00001FA7	},
	{	0x00000345, 0x00001F68, 0x00001FA8	},
	{	0x00000345, 0x00001F69, 0x00001FA9	},
	{	0x00000345, 0x00001F6A, 0x00001FAA	},
	{	0x00000345, 0x00001F6B, 0x00001FAB	},
	{	0x00000345, 0x00001F6C, 0x00001FAC	},
	{	0x00000345, 0x00001F6D, 0x00001FAD	},
	{	0x00000345, 0x00001F6E, 0x00001FAE	},
	{	0x00000345, 0x00001F6F, 0x00001FAF	},
	{	0x00000345, 0x00001F70, 0x00001FB2	},
	{	0x00000345, 0x00001F74, 0x00001FC2	},
	{	0x00000345, 0x00001F7C, 0x00001FF2	},
	{	0x00000345, 0x00001FB6, 0x00001FB7	},
	{	0x00000345, 0x00001FC6, 0x00001FC7	},
	{	0x00000345, 0x00001FF6, 0x00001FF7	},
	{	0x00000653, 0x00000627, 0x00000622	},
	{	0x00000654, 0x00000627, 0x00000623	},
	{	0x00000654, 0x00000648, 0x00000624	},
	{	0x00000654, 0x0000064A, 0x00000626	},
	{	0x00000654, 0x000006C1, 0x000006C2	},
	{	0x00000654, 0x000006D2, 0x000006D3	},
	{	0x00000654, 0x000006D5, 0x000006C0	},
	{	0x00000655, 0x00000627, 0x00000625	},
	{	0x0000093C, 0x00000928, 0x00000929	},
	{	0x0000093C, 0x00000930, 0x00000931	},
	{	0x0000093C, 0x00000933, 0x00000934	},
	{	0x000009BE, 0x000009C7, 0x000009CB	},
	{	0x000009D7, 0x000009C7, 0x000009CC	},
	{	0x00000B3E, 0x00000B47, 0x00000B4B	},
	{	0x00000B56, 0x00000B47, 0x00000B48	},
	{	0x00000B57, 0x00000B47, 0x00000B4C	},
	{	0x00000BBE, 0x00000BC6, 0x00000BCA	},
	{	0x00000BBE, 0x00000BC7, 0x00000BCB	},
	{	0x00000BD7, 0x00000B92, 0x00000B94	},
	{	0x00000BD7, 0x00000BC6, 0x00000BCC	},
	{	0x00000C56, 0x00000C46, 0x00000C48	},
	{	0x00000CC2, 0x00000CC6, 0x00000CCA	},
	{	0x00000CD5, 0x00000CBF, 0x00000CC0	},
	{	0x00000CD5, 0x00000CC6, 0x00000CC7	},
	{	0x00000CD5, 0x00000CCA, 0x00000CCB	},
	{	0x00000CD6, 0x00000CC6, 0x00000CC8	},
	{	0x00000D3E, 0x00000D46, 0x00000D4A	},
	{	0x00000D3E, 0x00000D47, 0x00000D4B	},
	{	0x00000D57, 0x00000D46, 0x00000D4C	},
	{	0x00000DCA, 0x00000DD9, 0x00000DDA	},
	{	0x00000DCA, 0x00000DDC, 0x00000DDD	},
	{	0x00000DCF, 0x00000DD9, 0x00000DDC	},
	{	0x00000DDF, 0x00000DD9, 0x00000DDE	},
	{	0x0000102E, 0x00001025, 0x00001026	},
	{	0x00001B35, 0x00001B05, 0x00001B06	},
	{	0x00001B35, 0x00001B07, 0x00001B08	},
	{	0x00001B35, 0x00001B09, 0x00001B0A	},
	{	0x00001B35, 0x00001B0B, 0x00001B0C	},
	{	0x00001B35, 0x00001B0D, 0x00001B0E	},
	{	0x00001B35, 0x00001B11, 0x00001B12	},
	{	0x00001B35, 0x00001B3A, 0x00001B3B	},
	{	0x00001B35, 0x00001B3C, 0x00001B3D	},
	{	0x00001B35, 0x00001B3E, 0x00001B40	},
	{	0x00001B35, 0x00001B3F, 0x00001B41	},
	{	0x00001B35, 0x00001B42, 0x00001B43	},
	{	0x00003099, 0x00003046, 0x00003094	},
	{	0x00003099, 0x0000304B, 0x0000304C	},
	{	0x00003099, 0x0000304D, 0x0000304E	},
	{	0x00003099, 0x0000304F, 0x00003050	},
	{	0x00003099, 0x00003051, 0x00003052	},
	{	0x00003099, 0x00003053, 0x00003054	},
	{	0x00003099, 0x00003055, 0x00003056	},
	{	0x00003099, 0x00003057, 0x00003058	},
	{	0x00003099, 0x00003059, 0x0000305A	},
	{	0x00003099, 0x0000305B, 0x0000305C	},
	{	0x00003099, 0x0000305D, 0x0000305E	},
	{	0x00003099, 0x0000305F, 0x00003060	},
	{	0x00003099, 0x00003061, 0x00003062	},
	{	0x00003099, 0x00003064, 0x00003065	},
	{	0x00003099, 0x00003066, 0x00003067	},
	{	0x00003099, 0x00003068, 0x00003069	},
	{	0x00003099, 0x0000306F, 0x00003070	},
	{	0x00003099, 0x00003072, 0x00003073	},
	{	0x00003099, 0x00003075, 0x00003076	},
	{	0x00003099, 0x00003078, 0x00003079	},
	{	0x00003099, 0x0000307B, 0x0000307C	},
	{	0x00003099, 0x0000309D, 0x0000309E	},
	{	0x00003099, 0x000030A6, 0x000030F4	},
	{	0x00003099, 0x000030AB, 0x000030AC	},
	{	0x00003099, 0x000030AD, 0x000030AE	},
	{	0x00003099, 0x000030AF, 0x000030B0	},
	{	0x00003099, 0x000030B1, 0x000030B2	},
	{	0x00003099, 0x000030B3, 0x000030B4	},
	{	0x00003099, 0x000030B5, 0x000030B6	},
	{	0x00003099, 0x000030B7, 0x000030B8	},
	{	0x00003099, 0x000030B9, 0x000030BA	},
	{	0x00003099, 0x000030BB, 0x000030BC	},
	{	0x00003099, 0x000030BD, 0x000030BE	},
	{	0x00003099, 0x000030BF, 0x000030C0	},
	{	0x00003099, 0x000030C1, 0x000030C2	},
	{	0x00003099, 0x000030C4, 0x000030C5	},
	{	0x00003099, 0x000030C6, 0x000030C7	},
	{	0x00003099, 0x000030C8, 0x000030C9	},
	{	0x00003099, 0x000030CF, 0x000030D0	},
	{	0x00003099, 0x000030D2, 0x000030D3	},
	{	0x00003099, 0x000030D5, 0x000030D6	},
	{	0x00003099, 0x000030D8, 0x000030D9	},
	{	0x00003099, 0x000030DB, 0x000030DC	},
	{	0x00003099, 0x000030EF, 0x000030F7	},
	{	0x00003099, 0x000030F0, 0x000030F8	},
	{	0x00003099, 0x000030F1, 0x000030F9	},
	{	0x00003099, 0x000030F2, 0x000030FA	},
	{	0x00003099, 0x000030FD, 0x000030FE	},
	{	0x0000309A, 0x0000306F, 0x00003071	},
	{	0x0000309A, 0x00003072, 0x00003074	},
	{	0x0000309A, 0x00003075, 0x00003077	},
	{	0x0000309A, 0x00003078, 0x0000307A	},
	{	0x0000309A, 0x0000307B, 0x0000307D	},
	{	0x0000309A, 0x000030CF, 0x000030D1	},
	{	0x0000309A, 0x000030D2, 0x000030D4	},
	{	0x0000309A, 0x000030D5, 0x000030D7	},
	{	0x0000309A, 0x000030D8, 0x000030DA	},
	{	0x0000309A, 0x000030DB, 0x000030DD	},
	{	0x000110BA, 0x00011099, 0x0001109A	},
	{	0x000110BA, 0x0001109B, 0x0001109C	},
	{	0x000110BA, 0x000110A5, 0x000110AB	},
	{	0x00011127, 0x00011131, 0x0001112E	},
	{	0x00011127, 0x00011132, 0x0001112F	},
	{	0x0001133E, 0x00011347, 0x0001134B	},
	{	0x00011357, 0x00011347, 0x0001134C	},
	{	0x000114B0, 0x000114B9, 0x000114BC	},
	{	0x000114BA, 0x000114B9, 0x000114BB	},
	{	0x000114BD, 0x000114B9, 0x000114BE	},
	{	0x000115AF, 0x000115B8, 0x000115BA	},
	{	0x000115AF, 0x000115B9, 0x000115BB	},
};
//static const Combination * const peculiar_combinations1_end(peculiar_combinations1 + sizeof peculiar_combinations1/sizeof *peculiar_combinations1);
static const Combination * const peculiar_combinations2_end(peculiar_combinations2 + sizeof peculiar_combinations2/sizeof *peculiar_combinations2);
static const Combination * const unicode_combinations_end(unicode_combinations + sizeof unicode_combinations/sizeof *unicode_combinations);

/// Combine multiple dead keys into different dead keys.
/// This is not Unicode composition; it is a means of typing dead key R (that is not in the common group 2) by typing two dead keys C1 and C2.
/// It is therefore commutative; whereas Unicode composition rules are not.
static inline
bool
combine_dead_keys (
	uint32_t & c1,
	const uint32_t c2
) {
	for (const Combination * p(peculiar_combinations1), *e(peculiar_combinations1 + sizeof peculiar_combinations1/sizeof *peculiar_combinations1); p < e; ++p)  {
		if ((p->c1 == c1 && p->c2 == c2)
		|| (p->c1 == c2 && p->c2 == c1)
	     	) {
			c1 = p->r;
			return true;
		}
	}
	return false;
}

static inline
bool
Combine (
	const Combination * begin,
	const Combination * end,
	const uint32_t c,
	uint32_t & b
) {
	const Combination one = { c, b, -1U };
	const Combination * p(std::lower_bound(begin, end, one));
	if (p >= end || p->c1 != c || p->c2 != b) return false;
	b = p->r;
	return true;
}

static inline
bool
combine_peculiar_non_combiners (
	const uint32_t c,
	uint32_t & b
) {
	return Combine(peculiar_combinations2, peculiar_combinations2_end, c, b);
}

static inline
bool
combine_unicode (
	const uint32_t c,
	uint32_t & b
) {
	return Combine(unicode_combinations, unicode_combinations_end, c, b);
}

static inline
bool
lower_combining_class (
	uint32_t c1,
	uint32_t c2
) {
	return UnicodeCategorization::CombiningClass(c1) < UnicodeCategorization::CombiningClass(c2);
}

void 
Realizer::handle_character_input(
	uint32_t ch,
	bool accelerator
) {
	if (UnicodeCategorization::IsMarkEnclosing(ch)
	||  UnicodeCategorization::IsMarkNonSpacing(ch)
	) {
		// Per ISO 9995-3 there are certain pairs of dead keys that make other dead keys.
		// Per DIN 2137, this happens on the fly as the keys are typed, so we only need to test combining against the preceding key.
		if (dead_keys.empty() || !combine_dead_keys(dead_keys.back(), ch))
			dead_keys.push_back(ch);
	} else
	if (0x200C == ch) {
		// Per DIN 2137, Zero-Width Non-Joiner means emit the dead-key sequence as-is.
		// We must not sort into Unicode combination class order.
		// ZWNJ is essentially a pass-through mechanism for combiners.
		for (DeadKeysList::iterator i(dead_keys.begin()); i != dead_keys.end(); i = dead_keys.erase(i))
			vt.WriteInputMessage((accelerator ? MessageForAcceleratorKey : MessageForUCS3)(*i));
	} else
	if (!dead_keys.empty()) {
		// Per ISO 9995-3 there are certain C+B=R combinations that apply in addition to the Unicode composition rules.
		// These can be done first, because they never start with a precomposed character.
		for (DeadKeysList::iterator i(dead_keys.begin()); i != dead_keys.end(); )
			if (combine_peculiar_non_combiners(*i, ch))
				i = dead_keys.erase(i);
			else
				++i;
		// The standard thing to do at this point is to renormalize into Unicode Normalization Form C (NFC).
		// As explained in the manual at length (q.v.), we don't do full Unicode Normalization.
		std::stable_sort(dead_keys.begin(), dead_keys.end(), lower_combining_class);
		for (DeadKeysList::iterator i(dead_keys.begin()); i != dead_keys.end(); )
			if (combine_unicode(*i, ch))
				i = dead_keys.erase(i);
			else
				++i;
		// If we have any dead keys left, we emit them standalone, before the composed key (i.e. in original typing order, as best we can).
		// We don't send the raw combiners, as in the ZWNJ case.
		// Instead, we make use of the fact that ISO 9995-3 defines rules for combining with the space character to produce a composed spacing character.
		for (DeadKeysList::iterator i(dead_keys.begin()); i != dead_keys.end(); i = dead_keys.erase(i)) {
			uint32_t s(' ');
			if (combine_peculiar_non_combiners(*i, s))
				vt.WriteInputMessage((accelerator ? MessageForAcceleratorKey : MessageForUCS3)(s));
			else
				vt.WriteInputMessage((accelerator ? MessageForAcceleratorKey : MessageForUCS3)(*i));
		}
		// This is the final composed key.
		vt.WriteInputMessage((accelerator ? MessageForAcceleratorKey : MessageForUCS3)(ch));
	} else
	{
		vt.WriteInputMessage((accelerator ? MessageForAcceleratorKey : MessageForUCS3)(ch));
	}
}

inline
void
Realizer::handle_screen_key (
	const uint32_t s,
	const uint8_t modifiers
) {
	ClearDeadKeys();
	vt.WriteInputMessage(MessageForSession(s, modifiers));
}

inline
void
Realizer::handle_system_key (
	const uint32_t k,
	const uint8_t modifiers
) {
	ClearDeadKeys();
	vt.WriteInputMessage(MessageForSystemKey(k, modifiers));
}

inline
void
Realizer::handle_consumer_key (
	const uint32_t k,
	const uint8_t modifiers
) {
	ClearDeadKeys();
	vt.WriteInputMessage(MessageForConsumerKey(k, modifiers));
}

inline
void
Realizer::handle_extended_key (
	const uint32_t k,
	const uint8_t modifiers
) {
	ClearDeadKeys();
	vt.WriteInputMessage(MessageForExtendedKey(k, modifiers));
}

inline
void
Realizer::handle_function_key (
	const uint32_t k,
	const uint8_t modifiers
) {
	ClearDeadKeys();
	vt.WriteInputMessage(MessageForFunctionKey(k, modifiers));
}

/* event HIDs ***************************************************************
// **************************************************************************
*/

#if defined(HUG_APPLE_EJECT)
#define HUG_LAST_SYSTEM_KEY	HUG_APPLE_EJECT
#else
#define HUG_LAST_SYSTEM_KEY	HUG_SYSTEM_MENU_DOWN
#endif

namespace {

class HIDBase :
	public KeyboardModifierState
{
public:
	HIDBase(const KeyboardMap & m, bool n) : KeyboardModifierState(n), map(m) {}

	void handle_keyboard (Realizer &, const uint8_t row, const uint8_t col, uint32_t v);
	void handle_keyboard (Realizer &, const uint16_t index, uint32_t v);

protected:
	const KeyboardMap & map;

 };

}

/// Actions can be simple transmissions of an input message, or complex procedures with the input method.
inline
void
HIDBase::handle_keyboard (
	Realizer & r,
	const uint8_t row,
	const uint8_t col,
	uint32_t v
) {
	if (row >= KBDMAP_ROWS) return;
	if (col >= KBDMAP_COLS) return;
	const kbdmap_entry & action(map[row][col]);
	const std::size_t o(query_kbdmap_parameter(action.cmd));
	if (o >= sizeof action.p/sizeof *action.p) return;
	const uint32_t act(action.p[o] & KBDMAP_ACTION_MASK);
	const uint32_t cmd(action.p[o] & ~KBDMAP_ACTION_MASK);

	switch (act) {
		default:	break;
		case KBDMAP_ACTION_UCS3:
		{
			if (v < 1U) break;
			r.handle_character_input(cmd, alt());
			unlatch();
			break;
		}
		case KBDMAP_ACTION_MODIFIER:
		{
			const uint32_t k((cmd & 0x00FFFF00) >> 8U);
			const uint32_t c(cmd & 0x000000FF);
			event(k, c, v);
			break;
		}
		case KBDMAP_ACTION_SCREEN:
		{
			if (v < 1U) break;
			const uint32_t s((cmd & 0x00FFFF00) >> 8U);
			r.handle_screen_key(s, modifiers());
			unlatch();
			break;
		}
		case KBDMAP_ACTION_SYSTEM:
		{
			if (v < 1U) break;
			const uint32_t k((cmd & 0x00FFFF00) >> 8U);
			r.handle_system_key(k, modifiers());
			unlatch();
			break;
		}
		case KBDMAP_ACTION_CONSUMER:
		{
			if (v < 1U) break;
			const uint32_t k((cmd & 0x00FFFF00) >> 8U);
			r.handle_consumer_key(k, modifiers());
			unlatch();
			break;
		}
		case KBDMAP_ACTION_EXTENDED:
		{
			if (v < 1U) break;
			const uint32_t k((cmd & 0x00FFFF00) >> 8U);
			r.handle_extended_key(k, modifiers());
			unlatch();
			break;
		}
		case KBDMAP_ACTION_EXTENDED1:
		{
			if (v < 1U) break;
			const uint32_t k((cmd & 0x00FFFF00) >> 8U);
			r.handle_extended_key(k, nolevel_modifiers());
			unlatch();
			break;
		}
		case KBDMAP_ACTION_FUNCTION:
		{
			if (v < 1U) break;
			const uint32_t k((cmd & 0x00FFFF00) >> 8U);
			r.handle_function_key(k, modifiers());
			unlatch();
			break;
		}
		case KBDMAP_ACTION_FUNCTION1:
		{
			if (v < 1U) break;
			const uint32_t k((cmd & 0x00FFFF00) >> 8U);
			r.handle_function_key(k, nolevel_nogroup_noctrl_modifiers());
			unlatch();
			break;
		}
	}
}

inline
void
HIDBase::handle_keyboard (
	Realizer & r,
	const uint16_t index,	///< by common convention, 0xFFFF is bypass
	uint32_t v
) {
	if (0xFFFF == index) return;
	const uint8_t row(index >> 8U);
	const uint8_t col(index & 0xFF);
	handle_keyboard(r, row, col, v);
}

#if !defined(__LINUX__) && !defined(__linux__)
namespace {

class USBHIDBase :
	public HIDBase
{
public:
	virtual ~USBHIDBase() {}
	void set_input_fd(int fd) { input.reset(fd); }
	int query_input_fd() const { return input.get(); }
	void handle_input_events(Realizer &);
	void set_LEDs();

protected:
	typedef uint32_t UsageID;
	enum {
		USAGE_CONSUMER_AC_PAN	= 0x000C0238,
	};
#if defined(__OpenBSD__)
	enum {
		HUP_LEDS		= HUP_LED,
		USB_SHORT_XFER_OK	= USBD_SHORT_XFER_OK,
	};
	static uint16_t HID_USAGE(UsageID d) { return d & 0xFFFF; }
#endif
	enum {
#if !defined(__OpenBSD__)
		HUL_NUM_LOCK	= 0x0001,
		HUL_CAPS_LOCK	= 0x0002,
		HUL_SCROLL_LOCK	= 0x0003,
		HUL_COMPOSE	= 0x0004,
		HUL_KANA	= 0x0005,
#endif
		HUL_SHIFT	= 0x0007,
	};

	struct ParserContext {
		struct Item { 
			Item(uint32_t pd, std::size_t ps): d(pd), s(ps) {}
			Item() : d(), s() {}
			uint32_t d; 
			std::size_t s; 
		};
		Item globals[16], locals[16];
		bool has_min, has_max;
		ParserContext() : has_min(false), has_max(false) { wipe_globals(); wipe_locals(); }
		void clear_locals() { wipe_locals(); idents.clear(); has_min = has_max = false; }
		const Item & logical_minimum() const { return globals[1]; }
		const Item & logical_maximum() const { return globals[2]; }
		const Item & report_size() const { return globals[7]; }
		const Item & report_id() const { return globals[8]; }
		const Item & report_count() const { return globals[9]; }
		UsageID ident() const { return ident(usage(), page()); }
		UsageID ident_minimum() const { return ident(usage_minimum(), page()); }
		UsageID ident_maximum() const { return ident(usage_maximum(), page()); }
		void add_ident() { idents.push_back(ident()); }
		UsageID take_variable_ident() { 
			if (!idents.empty()) {
				const UsageID v(idents.front());
				if (idents.size() > 1) idents.pop_front();
				return v;
			} else
			if (has_min) {
				const UsageID v(ident_minimum());
				if (has_max && v < ident_maximum()) {
					// Reset to full size because pages are not min/max boundaries and we don't want page wraparound.
					locals[1].d = v + 1;
					locals[1].s = 4;
				}
				return v;
			} else
				return ident();
		}
	protected:
		std::deque<UsageID> idents;
		static UsageID ident(const Item & u, const Item & p) { return u.s < 3 ? HID_USAGE2(p.d, u.d) : u.d; }
		const Item & page() const { return globals[0]; }
		const Item & usage() const { return locals[0]; }
		const Item & usage_minimum() const { return locals[1]; }
		const Item & usage_maximum() const { return locals[2]; }
		void wipe_locals() { for (std::size_t i(0); i < sizeof locals/sizeof *locals; ++i) locals[i].d = locals[i].s = 0U; }
		void wipe_globals() { for (std::size_t i(0); i < sizeof globals/sizeof *globals; ++i) globals[i].d = globals[i].s = 0U; }
	};
	struct ReportField {
		static int64_t mm(const ParserContext::Item & control, const ParserContext::Item & value) 
		{ 
			const uint32_t control_signbit(1U << (control.s * 8U - 1U));
			const uint32_t value_signbit(1U << (value.s * 8U - 1U));
			if ((control.d & control_signbit) && (value.d & value_signbit)) {
				const uint32_t v(value.d | ~(value_signbit - 1U));
				return static_cast<int32_t>(v);
			} else
				return static_cast<int64_t>(value.d); 
		}
		ReportField(std::size_t p, std::size_t l): pos(p), len(l) {}
		std::size_t pos, len;
	};
	struct InputVariable : public ReportField {
		InputVariable(UsageID i, std::size_t p, std::size_t l, bool r, const ParserContext::Item & mi, const ParserContext::Item & ma): ReportField(p, l), relative(r), ident(i), min(mm(mi, mi)), max(mm(mi, ma)) {}
		bool relative;
		UsageID ident;
		int64_t min, max;
	};
	struct InputIndex : public ReportField {
		InputIndex(std::size_t p, std::size_t l, UsageID umi, UsageID uma, const ParserContext::Item & imi, const ParserContext::Item & ima): ReportField(p, l), min_ident(umi), max_ident(uma), min_index(mm(imi, imi)), max_index(mm(imi, ima)) {}
		UsageID min_ident, max_ident;
		int64_t min_index, max_index;
	};
	struct InputReportDescription {
		InputReportDescription() : bits(0U), bytes(0U) {}
		std::size_t bits, bytes;
		typedef std::list<InputVariable> Variables;
		Variables variables;
		typedef std::list<InputIndex> Indices;
		Indices array_indices;
	};
	struct OutputVariable : public ReportField {
		OutputVariable(UsageID i, std::size_t p, std::size_t l): ReportField(p, l), ident(i) {}
		UsageID ident;
	};
	struct OutputIndex : public ReportField {
		OutputIndex(std::size_t p, std::size_t l, UsageID umi, UsageID uma): ReportField(p, l), min_ident(umi), max_ident(uma) {}
		UsageID min_ident, max_ident;
	};
	struct OutputReportDescription {
		OutputReportDescription() : bits(0U), bytes(0U) {}
		std::size_t bits, bytes;
		typedef std::list<OutputVariable> Variables;
		Variables variables;
		typedef std::list<OutputIndex> Indices;
		Indices array_indices;
	};
	typedef std::map<uint8_t, InputReportDescription> InputReports;
	typedef std::map<uint8_t, OutputReportDescription> OutputReports;
	typedef std::set<UsageID> KeysPressed;

	USBHIDBase(const KeyboardMap &, bool);

	FileDescriptorOwner input;
	char output_buffer[4096];
	char input_buffer[4096];
	std::size_t input_offset;
	bool has_report_ids;
	InputReports input_reports;
	OutputReports output_reports;

	bool read_description(unsigned char * const b, const std::size_t maxlen, const std::size_t actlen);
	bool IsInRange(const InputVariable & f, uint32_t);
	uint32_t GetUnsignedField(const ReportField & f, std::size_t);
	bool IsInRange(const InputVariable & f, int32_t);
	int32_t GetSignedField(const ReportField & f, std::size_t);

	virtual void write_output_report(const uint8_t, std::size_t) = 0;
	void SetUnsignedField(const ReportField & f, std::size_t, uint32_t);

	uint16_t stdbuttons;
	static unsigned short TranslateStdButton(const unsigned short button);
	void handle_mouse_movement(Realizer & r, const InputReportDescription & report, const InputVariable & f, const uint16_t axis);
	void handle_mouse_button(Realizer & r, UsageID ident, bool down);

	KeysPressed keys;
	static void handle_key(bool & seen_keyboard, KeysPressed & keys, UsageID ident, bool down);
};

}

inline
USBHIDBase::USBHIDBase(const KeyboardMap & km, bool n) : 
	HIDBase(km, n),
	input(-1),
	input_offset(0U),
	has_report_ids(false),
	stdbuttons(0U)
{
}

inline
unsigned short
USBHIDBase::TranslateStdButton(
	const unsigned short button
) {
	switch (button) {
		case 1U:	return 2;
		case 2U:	return 1;
		default:	return button;
	}
}

inline
uint32_t 
USBHIDBase::GetUnsignedField(
	const ReportField & f,
	std::size_t report_size
) {
	if (0U == f.len) return 0U;
	// Byte-aligned fields can be optimized.
	if (0U == (f.pos & 7U)) {
		const std::size_t bytepos(f.pos >> 3U);
		if (bytepos >= report_size) return 0U;
		if (f.len <= 8U) {
			uint8_t v(*reinterpret_cast<const uint8_t *>(input_buffer + bytepos));
			if (f.len < 8U) v &= 0xFF >> (8U - f.len);
			return v;
		}
		if (f.len <= 16U) {
			uint16_t v(le16toh(*reinterpret_cast<const uint16_t *>(input_buffer + bytepos)));
			if (f.len < 16U) v &= 0xFFFF >> (16U - f.len);
			return v;
		}
		if (f.len <= 32U) {
			uint32_t v(le32toh(*reinterpret_cast<const uint32_t *>(input_buffer + bytepos)));
			if (f.len < 32U) v &= 0xFFFFFFFF >> (32U - f.len);
			return v;
		}
	}
	// 1-bit fields can be optimized.
	if (1U == f.len) {
		const std::size_t bytepos(f.pos >> 3U);
		if (bytepos >= report_size) return 0U;
		const uint8_t v(*reinterpret_cast<const uint8_t *>(input_buffer + bytepos));
		return (v >> (f.pos & 7U)) & 1U;
	}
	/// \bug FIXME: This doesn't handle unaligned >1-bit fields.
	return 0U;
}

inline
bool 
USBHIDBase::IsInRange(
	const InputVariable & f,
	uint32_t v
) {
	return v <= f.max && v >= f.min;
}

inline
int32_t 
USBHIDBase::GetSignedField(
	const ReportField & f,
	std::size_t report_size
) {
	if (0U == f.len) return 0U;
	// Byte-aligned fields can be optimized.
	if (0U == (f.pos & 7U)) {
		const std::size_t bytepos(f.pos >> 3U);
		if (bytepos >= report_size) return 0U;
		if (f.len <= 8U) {
			uint8_t v(*reinterpret_cast<const uint8_t *>(input_buffer + bytepos));
			if (f.len < 8U) v &= 0xFF >> (8U - f.len);
			const uint8_t signbit(1U << (f.len - 1U));
			if (v & signbit) v |= ~(signbit - 1U);
			return static_cast<int8_t>(v);
		}
		if (f.len <= 16U) {
			uint16_t v(le16toh(*reinterpret_cast<const uint16_t *>(input_buffer + bytepos)));
			if (f.len < 16U) v &= 0xFFFF >> (16U - f.len);
			const uint16_t signbit(1U << (f.len - 1U));
			if (v & signbit) v |= ~(signbit - 1U);
			return static_cast<int16_t>(v);
		}
		if (f.len <= 32U) {
			uint32_t v(le32toh(*reinterpret_cast<const uint32_t *>(input_buffer + bytepos)));
			if (f.len < 32U) v &= 0xFFFFFFFF >> (32U - f.len);
			const uint32_t signbit(1U << (f.len - 1U));
			if (v & signbit) v |= ~(signbit - 1U);
			return static_cast<int32_t>(v);
		}
	}
	// 1-bit fields can be optimized.
	if (1U == f.len) {
		const std::size_t bytepos(f.pos >> 3U);
		if (bytepos >= report_size) return 0U;
		const uint8_t v(*reinterpret_cast<const uint8_t *>(input_buffer + bytepos));
		// The 1 bit is the sign bit, so sign extension gets us this.
		return (v >> (f.pos & 7U)) & 1U ? -1 : 0;
	}
	/// \bug FIXME: This doesn't handle unaligned >1-bit fields.
	return 0U;
}

inline
bool
USBHIDBase::IsInRange(
	const InputVariable & f,
	int32_t v
) {
	return v <= f.max && v >= f.min;
}

inline
void 
USBHIDBase::SetUnsignedField(
	const ReportField & f,
	std::size_t report_size,
	uint32_t value
) {
	if (0U == f.len) return;
	// Byte-aligned fields can be optimized.
	if (0U == (f.pos & 7U)) {
		const std::size_t bytepos(f.pos >> 3U);
		if (bytepos >= report_size) return;
		if (f.len <= 8U) {
			uint8_t v(*reinterpret_cast<const uint8_t *>(output_buffer + bytepos));
			v &= 0xFF << f.len;
			v |= value & (0xFF >> (8U - f.len));
			*reinterpret_cast<uint8_t *>(output_buffer + bytepos) = v;
		}
		if (f.len <= 16U) {
			uint16_t v(le16toh(*reinterpret_cast<const uint16_t *>(output_buffer + bytepos)));
			v &= 0xFFFF << f.len;
			v |= value & (0xFFFF >> (16U - f.len));
			*reinterpret_cast<uint16_t *>(output_buffer + bytepos) = htole16(v);
		}
		if (f.len <= 32U) {
			uint32_t v(le32toh(*reinterpret_cast<const uint32_t *>(output_buffer + bytepos)));
			v &= 0xFFFFFFFF << f.len;
			v |= value & (0xFFFFFFFF >> (32U - f.len));
			*reinterpret_cast<uint32_t *>(output_buffer + bytepos) = htole32(v);
		}
	}
	// 1-bit fields can be optimized.
	if (1U == f.len) {
		const std::size_t bytepos(f.pos >> 3U);
		if (bytepos >= report_size) return;
		uint8_t & v(*reinterpret_cast<uint8_t *>(output_buffer + bytepos));
		const uint8_t mask(1U << (f.pos & 7U));
		if (value) 
			v |= mask;
		else
			v &= ~mask;
	}
	/// \bug FIXME: This doesn't handle unaligned >1-bit fields.
}

inline
bool 
USBHIDBase::read_description (
	unsigned char * const b,
	const std::size_t maxlen,
	const std::size_t actlen
) {
	input_reports.clear();
	has_report_ids = false;

	enum { MAIN = 0, GLOBAL, LOCAL, LONG };
	enum /*local tags*/ { USAGE = 0, MIN, MAX };
	enum /*global tags*/ { PAGE = 0, REPORT_ID = 8, PUSH = 10, POP = 11 };
	enum /*main tags*/ { INPUT = 8, OUTPUT = 9, COLLECT = 10, END = 12};
	enum /*collection types*/ { APPLICATION = 1 };

	std::stack<ParserContext> context_stack;
	context_stack.push(ParserContext());
	unsigned collection_level(0U);
	bool enable_fields(false);

	for ( std::size_t i(0U); i < actlen; ) {
		if (i >= maxlen) break;
		unsigned size((b[i] & 3U) + ((b[i] & 3U) > 2U));
		unsigned type((b[i] >> 2) & 3U);
		unsigned tag((b[i] >> 4) & 15U);
		++i;
		uint32_t datum(0U);
		switch (size) {
			case 4: 
				if (i + 4 > actlen) break;
				datum = b[i+0] | (uint32_t(b[i+1]) << 8) | (uint32_t(b[i+2]) << 16) | (uint32_t(b[i+3]) << 24);
				i += 4;
				break;
			case 2:
				if (i + 2 > actlen) break;
				datum = b[i+0] | (uint32_t(b[i+1]) << 8);
				i += 2;
				break;
			case 1:
				if (i + 1 > actlen) break;
				datum = b[i++];
				break;
			case 0:	
				break;
		}
		ParserContext & context(context_stack.top());
		switch (type) {
			case LONG:
				size = datum & 0xFF;
				tag = (datum >> 8) & 0xFF;
				if (i + size > actlen) break;
				i += size;
				break;
			case LOCAL:
				context.locals[tag] = ParserContext::Item(datum, size);
				if (USAGE == tag) context.add_ident();
				if (MIN == tag) context.has_min = true;
				if (MAX == tag) context.has_max = true;
				break;
			case GLOBAL:
				if (PUSH == tag)
					context_stack.push(context);
				else
				if (POP == tag) {
					if (context_stack.size() > 1)
						context_stack.pop();
				} else
				if (PUSH > tag) {
					context.globals[tag] = ParserContext::Item(datum, size);
					if (REPORT_ID == tag) has_report_ids = true;
				}
				break;
			case MAIN:
				switch (tag) {
					case INPUT:
					{
						if (!enable_fields) break;
						InputReportDescription & r(input_reports[context.report_id().d]);
						if (datum & HIO_CONST) {
							r.bits += context.report_size().d * context.report_count().d;
						} else
						if (datum & HIO_VARIABLE) {
							for ( std::size_t count(0U); count < context.report_count().d; ++count ) {
								const UsageID ident(context.take_variable_ident());
								const bool relative(HIO_RELATIVE & datum);
								r.variables.push_back(InputVariable(ident, r.bits, context.report_size().d, relative, context.logical_minimum(), context.logical_maximum()));
								r.bits += context.report_size().d;
							}
						} else
						{
							for ( std::size_t count(0U); count < context.report_count().d; ++count ) {
								r.array_indices.push_back(InputIndex(r.bits, context.report_size().d, context.ident_minimum(), context.ident_maximum(), context.logical_minimum(), context.logical_maximum()));
								r.bits += context.report_size().d;
							}
						}
						r.bytes = (r.bits + 7U) >> 3U;
						break;
					}
					case OUTPUT:
					{
						if (!enable_fields) break;
						OutputReportDescription & r(output_reports[context.report_id().d]);
						if (datum & HIO_CONST) {
							r.bits += context.report_size().d * context.report_count().d;
						} else
						if (datum & HIO_VARIABLE) {
						for ( std::size_t count(0U); count < context.report_count().d; ++count ) {
							const UsageID ident(context.take_variable_ident());
								r.variables.push_back(OutputVariable(ident, r.bits, context.report_size().d));
								r.bits += context.report_size().d;
							}
						} else
						{
							for ( std::size_t count(0U); count < context.report_count().d; ++count ) {
								r.array_indices.push_back(OutputIndex(r.bits, context.report_size().d, context.ident_minimum(), context.ident_maximum()));
								r.bits += context.report_size().d;
							}
						}
						r.bytes = (r.bits + 7U) >> 3U;
						break;
					}
					case COLLECT:
						if (0 == collection_level && APPLICATION == datum) {
							enable_fields = 
								HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_MOUSE) == context.ident() || 
								HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_KEYBOARD) == context.ident() ||
								HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_KEYPAD) == context.ident();
						}
						++collection_level;
						break;
					case END:
						if (collection_level > 0) {
							--collection_level;
							if (0 == collection_level)
								enable_fields = false;
						}
						break;
				}
				context.clear_locals();
				break;
		}
	}

	return true;
}

inline
void
USBHIDBase::handle_mouse_movement(
	Realizer & r,
	const InputReportDescription & report,
	const InputVariable & f,
	const uint16_t axis
) {
	if (f.relative) {
		const int32_t off(GetSignedField(f, report.bytes));
		if (IsInRange(f, off))
			r.handle_mouse_relpos(modifiers(), axis, off);
	} else {
		const uint32_t pos(GetUnsignedField(f, report.bytes));
		if (IsInRange(f, pos))
			r.handle_mouse_abspos(modifiers(), axis, pos, f.max - f.min + 1U);
	}
}

inline
void 
USBHIDBase::handle_mouse_button(
	Realizer & r,
	UsageID ident, 
	bool down
) {
	if (HID_USAGE2(HUP_BUTTON, 0) < ident && HID_USAGE2(HUP_BUTTON, 32U) >= ident) {
		const unsigned short button(ident - HID_USAGE2(HUP_BUTTON, 1));
		const unsigned long mask(1UL << button);
		if (!!(stdbuttons & mask) != down) {
			if (down)
				stdbuttons |= mask;
			else
				stdbuttons &= ~mask;
			r.handle_mouse_button(modifiers(), TranslateStdButton(button), down);
		}
	} else
		/* Ignore out of range button. */ ;
}

inline
void 
USBHIDBase::handle_key(
	bool & seen_keyboard, 
	KeysPressed & newkeys, 
	UsageID ident, 
	bool down
) {
	if ((HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_SYSTEM_CONTROL) <= ident && HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_LAST_SYSTEM_KEY) >= ident)
	||  (HID_USAGE2(HUP_KEYBOARD, 0) <= ident && HID_USAGE2(HUP_KEYBOARD, 0xFFFF) >= ident)
	||  (HID_USAGE2(HUP_CONSUMER, 0) <= ident && HID_USAGE2(HUP_CONSUMER, 0xFFFF) >= ident)
	) {
		seen_keyboard = true;
		if (down)
			newkeys.insert(ident);
		else
			newkeys.erase(ident);
	} else
		/* Ignore out of range key. */ ;
}

inline
void
USBHIDBase::handle_input_events(
	Realizer & r
) {
	const int n(read(input.get(), reinterpret_cast<char *>(input_buffer) + input_offset, sizeof input_buffer - input_offset));
	if (0 > n) return;

	KeysPressed newkeys;
	bool seen_keyboard(false);

	for (
		input_offset += n;
		input_offset > 0;
	    ) {
		uint8_t report_id(0);
		if (has_report_ids) {
			report_id = input_buffer[0];
			std::memmove(input_buffer, input_buffer + 1, sizeof input_buffer - 1),
			--input_offset;
		}
		InputReports::const_iterator rp(input_reports.find(report_id));
		if (input_reports.end() == rp) break;
		const InputReportDescription & report(rp->second);
		if (input_offset < report.bytes) break;

		for (InputReportDescription::Variables::const_iterator i(report.variables.begin()); i != report.variables.end(); ++i) {
			const InputVariable & f(*i);
			if (HID_USAGE2(HUP_BUTTON, 0) <= f.ident && HID_USAGE2(HUP_BUTTON, 0xFFFF) >= f.ident) {
				const uint32_t down(GetUnsignedField(f, report.bytes));
				if (IsInRange(f, down))
					handle_mouse_button(r, f.ident, down);
			} else
			if ((HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_SYSTEM_CONTROL) <= f.ident && HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_LAST_SYSTEM_KEY) >= f.ident)
			||  (HID_USAGE2(HUP_KEYBOARD, 0) <= f.ident && HID_USAGE2(HUP_KEYBOARD, 0xFFFF) >= f.ident)
			||  (HID_USAGE2(HUP_CONSUMER, 0) <= f.ident && HID_USAGE2(HUP_CONSUMER, 0xFFFF) >= f.ident)
			) {
				const uint32_t down(GetUnsignedField(f, report.bytes));
				if (IsInRange(f, down))
					handle_key(seen_keyboard, newkeys, f.ident, down);
			} else
			switch (f.ident) {
				case HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_X):
					handle_mouse_movement(r, report, f, r.AXIS_X);
					break;
				case HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_Y):
					handle_mouse_movement(r, report, f, r.AXIS_Y);
					break;
				case HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_WHEEL):
					handle_mouse_movement(r, report, f, r.V_SCROLL);
					break;
				case HID_USAGE2(HUP_CONSUMER, HUC_AC_PAN):
					handle_mouse_movement(r, report, f, r.H_SCROLL);
					break;
			}
		}
		for (InputReportDescription::Indices::const_iterator i(report.array_indices.begin()); i != report.array_indices.end(); ++i) {
			const InputIndex & f(*i);
			const uint32_t v(GetUnsignedField(f, report.bytes));
			if (v < f.min_index || v > f.max_index) continue;
			uint32_t ident(v + f.min_ident - f.min_index);
			if (ident > f.max_ident) ident = f.max_ident;

			const bool down(true);	// Implied by the existence of the index value.
			if (HID_USAGE2(HUP_BUTTON, 0) < ident && HID_USAGE2(HUP_BUTTON, 0xFFFF) >= ident) {
				handle_mouse_button(r, ident, down);
			} else
			if ((HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_SYSTEM_CONTROL) <= ident && HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_LAST_SYSTEM_KEY) >= ident)
			||  (HID_USAGE2(HUP_KEYBOARD, 0) <= ident && HID_USAGE2(HUP_KEYBOARD, 0xFFFF) >= ident)
			||  (HID_USAGE2(HUP_CONSUMER, 0) <= ident && HID_USAGE2(HUP_CONSUMER, 0xFFFF) >= ident)
			) {
				handle_key(seen_keyboard, newkeys, ident, down);
			} else
				/* Ignore out of range button/key. */ ;
		}

		std::memmove(input_buffer, input_buffer + report.bytes, sizeof input_buffer - report.bytes),
		input_offset -= report.bytes;
	}

	if (seen_keyboard) {
		for (KeysPressed::iterator was(keys.begin()); was != keys.end(); ) {
			KeysPressed::iterator now(newkeys.find(*was));
			const uint16_t index(usb_ident_to_keymap_index(*was));
			if (newkeys.end() == now) {
				handle_keyboard(r, index, 0);
				was = keys.erase(was);
			} else {
				handle_keyboard(r, index, 2);
				newkeys.erase(now);
				++was;
			}
		}
		for (KeysPressed::iterator now(newkeys.begin()); now != newkeys.end(); now = newkeys.erase(now)) {
			const uint16_t index(usb_ident_to_keymap_index(*now));
			handle_keyboard(r, index, 1);
			keys.insert(*now);
		}
	}
}

inline
void 
USBHIDBase::set_LEDs(
) {
	for (OutputReports::const_iterator rp(output_reports.begin()); output_reports.end() != rp; ++rp) {
		const uint8_t report_id(rp->first);
		const OutputReportDescription & report(rp->second);
		if (sizeof output_buffer < report.bytes) continue;

		memset(output_buffer, 0, report.bytes);

		bool seen_LED(false);
		for (OutputReportDescription::Variables::const_iterator i(report.variables.begin()); i != report.variables.end(); ++i) {
			const OutputVariable & f(*i);
			if (HID_USAGE2(HUP_LEDS, 0) <= f.ident && HID_USAGE2(HUP_LEDS, 0xFFFF) >= f.ident) {
				switch (HID_USAGE(f.ident)) {
					case HUL_NUM_LOCK:
						SetUnsignedField(f, report.bytes, num_LED());
						break;
					case HUL_CAPS_LOCK:
						SetUnsignedField(f, report.bytes, caps_LED());
						break;
					case HUL_SCROLL_LOCK:
						SetUnsignedField(f, report.bytes, scroll_LED());
						break;
					case HUL_COMPOSE:
						SetUnsignedField(f, report.bytes, compose_LED());
						break;
					case HUL_KANA:
						SetUnsignedField(f, report.bytes, kana_LED());
						break;
					case HUL_SHIFT:
						SetUnsignedField(f, report.bytes, shift_LED());
						break;
				}
				seen_LED = true;
			}
		}

		if (seen_LED)
			write_output_report(report_id, report.bytes);
	}
}
#endif

#if defined(__LINUX__) || defined(__linux__)

/* Linux event HIDs *********************************************************
// **************************************************************************
*/

namespace {

class HID :
	public HIDBase
{
public:
	HID(const KeyboardMap &, bool);
	~HID() {}

	void set_input_fd(int fd) { input.reset(fd); }
	int query_input_fd() const { return input.get(); }
	bool set_exclusive(bool);
	void set_LEDs();
	void handle_input_events(Realizer &);
protected:
	FileDescriptorOwner input;
	input_event buffer[16];
	std::size_t offset;

	static int TranslateAbsAxis(const uint16_t code);
	static int TranslateRelAxis(const uint16_t code);
};

/// On Linux, these devices are character devices with a terminal line discipline that speak the kbio protocol.
class KernelVT :
	public HIDBase
{
public:
	KernelVT(const KeyboardMap &, bool, int, int);
	~KernelVT();

	void reset(const char *, unsigned long);
	int query_input_fd() const { return device.get(); }
	void acknowledge_switch_to();
	void permit_switch_from();
	void release();
	bool is_active();
	bool activate();
	bool wait_until_active();
	void set_LEDs();
	void handle_input_events(Realizer &);
protected:
	FileDescriptorOwner device;
	unsigned long vtnr;

	char buffer[16];
	std::size_t offset;
	unsigned state;
	bool up;
	uint16_t mediumraw;

	termios original_attr;
	struct vt_mode vtmode;
	const int release_signal, acquire_signal;
#if defined(KDGKBMUTE) && defined(KDSKBMUTE)
	int kbmute;
#endif
	long kdmode;
	long kbmode;
	void save_and_set_mode();
	void restore();

	bool pressed[16384];	///< used for determining auto-repeat keypresses
	bool is_pressed(uint8_t code) const { return pressed[code]; }
	void set_pressed(uint8_t code, bool v) { pressed[code] = v; }
};

/// On Linux, ps2mouse devices are character devices that speak the PS/2 mouse protocol.
class PS2Mouse :
	public HIDBase
{
public:
	PS2Mouse(const KeyboardMap &);
	~PS2Mouse();

	void set_input_fd(int);
	int query_input_fd() const { return device.get(); }
	void handle_input_events(Realizer & r);
protected:
	enum { MOUSE_PS2_PACKETSIZE = 3 };
	enum { MOUSE_PS2_STDBUTTONS = 3 };
	enum { MOUSE_PS2_MAXBUTTON = 3 };
	FileDescriptorOwner device;
	unsigned char buffer[MOUSE_PS2_PACKETSIZE * 16];
	std::size_t offset;

	void save_and_set_mode() {}
	void restore() {}

	uint16_t stdbuttons;
	static int TranslateStdButton(const unsigned short button);
	static short int GetOffset9 (uint8_t one, uint8_t two);
}; 

}

inline
HID::HID(
	const KeyboardMap & km,
	bool n
) : 
	HIDBase(km, n),
	input(-1),
	offset(0U)
{
}

inline
bool 
HID::set_exclusive(
	bool v
) {
	return 0 <= ioctl(input.get(), EVIOCGRAB, v);
}

inline
void 
HID::set_LEDs(
) {
	if (-1 != input.get()) {
		input_event e[5];
		e[0].type = e[1].type = e[2].type = e[3].type = e[4].type = EV_LED;
		e[0].code = LED_CAPSL; 
		e[0].value = caps_LED();
		e[1].code = LED_NUML; 
		e[1].value = num_LED();
		e[2].code = LED_SCROLLL; 
		e[2].value = scroll_LED();
		e[3].code = LED_KANA; 
		e[3].value = kana_LED();
		e[4].code = LED_COMPOSE; 
		e[4].value = compose_LED();
		write(input.get(), e, sizeof e);
	}
}

inline
int
HID::TranslateAbsAxis(
	const uint16_t code
) {
	switch (code) {
		default:		return -1;
		case ABS_X:		return Realizer::AXIS_X;
		case ABS_Y:		return Realizer::AXIS_Y;
		case ABS_Z:		return Realizer::AXIS_Z;
	}
}

inline
int
HID::TranslateRelAxis(
	const uint16_t code
) {
	switch (code) {
		default:		return -1;
		case REL_WHEEL:		return Realizer::V_SCROLL;
		case REL_HWHEEL:	return Realizer::H_SCROLL;
		case REL_X:		return Realizer::AXIS_X;
		case REL_Y:		return Realizer::AXIS_Y;
		case REL_Z:		return Realizer::AXIS_Z;
	}
}

inline
void
HID::handle_input_events(
	Realizer & r
) {
	const int n(read(input.get(), reinterpret_cast<char *>(buffer) + offset, sizeof buffer - offset));
	if (0 > n) return;
	for (
		offset += n;
		offset >= sizeof *buffer;
		memmove(buffer, buffer + 1U, sizeof buffer - sizeof *buffer),
		offset -= sizeof *buffer
	    ) {
		const input_event & e(buffer[0]);
		switch (e.type) {
			case EV_ABS:
				r.handle_mouse_abspos(modifiers(), TranslateAbsAxis(e.code), e.value, 32767U);
				break;
			case EV_REL:
				r.handle_mouse_relpos(modifiers(), TranslateRelAxis(e.code), e.value);
				break;
			case EV_KEY:
			{
				switch (e.code) {
					default:		
					{
						const uint16_t index(linux_evdev_keycode_to_keymap_index(e.code));
						handle_keyboard(r, index, e.value);
						break;
					}
					case BTN_LEFT:		r.handle_mouse_button(modifiers(), 0x00, e.value); break;
					case BTN_MIDDLE:	r.handle_mouse_button(modifiers(), 0x01, e.value); break;
					case BTN_RIGHT:		r.handle_mouse_button(modifiers(), 0x02, e.value); break;
					case BTN_SIDE:		r.handle_mouse_button(modifiers(), 0x03, e.value); break;
					case BTN_EXTRA:		r.handle_mouse_button(modifiers(), 0x04, e.value); break;
					case BTN_FORWARD:	r.handle_mouse_button(modifiers(), 0x05, e.value); break;
					case BTN_BACK:		r.handle_mouse_button(modifiers(), 0x06, e.value); break;
					case BTN_TASK:		r.handle_mouse_button(modifiers(), 0x07, e.value); break;
				}
				break;
			}
		}
	}
}

inline
KernelVT::KernelVT(
	const KeyboardMap & km,
	bool n,
	int rs,
	int as
) : 
	HIDBase(km, n),
	device(-1), 
	vtnr(0UL),
	offset(0U),
	state(0U),
	original_attr(),
	vtmode(),
	release_signal(rs),
	acquire_signal(as),
#if defined(KDGKBMUTE) && defined(KDSKBMUTE)
	kbmute(),
#endif
	kdmode(),
	kbmode()
{
	for (unsigned i(0U); i < sizeof pressed/sizeof *pressed; ++i)
		pressed[i] = false;
}

KernelVT::~KernelVT()
{
	restore();
}

inline
void 
KernelVT::reset(
	const char * prog, 
	unsigned long n 
) { 
	if (vtnr == n) return;
	char name[64];
	std::snprintf(name, sizeof name, "/dev/tty%lu", n);
	FileDescriptorOwner fd(open_readwriteexisting_at(AT_FDCWD, name));
	if (0 > fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, name, std::strerror(error));
		throw EXIT_FAILURE;
	}
	restore();
	device.reset(fd.release()); 
	vtnr = n;
	save_and_set_mode();
}

inline
void 
KernelVT::release()
{
	restore();
	device.release(); 
	vtnr = 0UL;
}

inline
void 
KernelVT::set_LEDs(
) {
	if (-1 != device.get()) {
		ioctl(device.get(), KDSETLED, (caps_LED() ? LED_CAP : 0)|(num_LED() ? LED_NUM : 0)|(scroll_LED() ? LED_SCR : 0));
	}
}

inline
void
KernelVT::handle_input_events(
	Realizer & r
) {
	const ssize_t n(read(device.get(), reinterpret_cast<char *>(buffer) + offset, sizeof buffer - offset));
	if (0 > n) return;
	for (
		offset += n;
		offset > 0 && offset >= sizeof *buffer;
		std::memmove(buffer, buffer + 1U, sizeof buffer - sizeof *buffer),
		offset -= sizeof *buffer
	) {
		switch (state) {
			case 0U:
				mediumraw = buffer[0] & 0x7F;
				up = buffer[0] & 0x80;
				if (0x00 == mediumraw) {
					mediumraw = 0x0000;
					++state;
				}
				break;
			case 1U:
				mediumraw = buffer[0] & 0x7F;
				++state;
				break;
			default:
				mediumraw = (mediumraw << 7) | (buffer[0] & 0x7F);
				state = 0U;
				break;
		}
		if (0U == state) {
			const unsigned value(up ? 0U : is_pressed(mediumraw) ? 2U : 1U);
			set_pressed(mediumraw, !up);

			const uint16_t index(linux_evdev_keycode_to_keymap_index(mediumraw));
			handle_keyboard(r, index, value);
		}
	}
}

void 
KernelVT::save_and_set_mode()
{
	if (-1 != device.get()) {
		// This is the BSD way of acquiring the controlling TTY.
		// On Linux, the 1 flag causes the forcible removal of this controlling TTY from existing processes, if we have the privileges.
		ioctl(device.get(), TIOCSCTTY, 1);
		// Set the VT switching to send the given release and acquire signals to this process.
		ioctl(device.get(), VT_GETMODE, &vtmode);
		struct vt_mode m = { VT_PROCESS, 0, static_cast<short>(release_signal), static_cast<char>(acquire_signal), 0 };
		ioctl(device.get(), VT_SETMODE, &m);
		// Tell the terminal emulator not to draw its character buffer, with KD_GRAPHICS or equivalent.
		ioctl(device.get(), KDGETMODE, &kdmode);
		ioctl(device.get(), KDSETMODE, KD_GRAPHICS);
#if defined(KDGKBMUTE) && defined(KDSKBMUTE)
		ioctl(device.get(), KDGKBMUTE, &kbmute);
		ioctl(device.get(), KDSKBMUTE, 0);
#endif
		// The line discipline needs to be set to raw mode for the duration.
		if (0 <= tcgetattr_nointr(device.get(), original_attr))
			tcsetattr_nointr(device.get(), TCSADRAIN, make_raw(original_attr));
		// And beneath that the keyboard needs to be set to keycode mode.
		ioctl(device.get(), KDGKBMODE, &kbmode);
		ioctl(device.get(), KDSKBMODE, K_MEDIUMRAW);
		// Set the LED operation to manual mode for the duration.
		ioctl(device.get(), KDSETLED, 0U);
	}
}

void
KernelVT::restore()
{
	if (-1 != device.get()) {
		ioctl(device.get(), KDSETLED, -1U);
		ioctl(device.get(), KDSKBMODE, kbmode);
		tcsetattr_nointr(device.get(), TCSADRAIN, original_attr);
#if defined(KDGKBMUTE) && defined(KDSKBMUTE)
		ioctl(device.get(), KDSKBMUTE, kbmute);
#endif
		ioctl(device.get(), KDSETMODE, kdmode);
		ioctl(device.get(), VT_SETMODE, &vtmode);
		ioctl(device.get(), TIOCNOTTY, 0);
	}
}

bool
KernelVT::is_active()
{
	if (-1 == device.get()) return true;
	struct vt_stat s;
	if (0 > ioctl(device.get(), VT_GETSTATE, &s)) return false;
	return s.v_active == vtnr;
}

bool
KernelVT::activate()
{
	if (-1 == device.get()) return true;
	while (0 > ioctl(device.get(), VT_ACTIVATE, vtnr)) {
		if (EINTR != errno) return false;
	}
	return true;
}

bool
KernelVT::wait_until_active()
{
	if (-1 == device.get()) return true;
	while (0 > ioctl(device.get(), VT_WAITACTIVE, vtnr)) {
		if (EINTR != errno) return false;
	}
	return true;
}
void 
KernelVT::acknowledge_switch_to()
{
	if (-1 != device.get())
		ioctl(device.get(), VT_RELDISP, VT_ACKACQ);
}

void 
KernelVT::permit_switch_from()
{
	if (-1 != device.get())
		ioctl(device.get(), VT_RELDISP, 1);
}

inline
PS2Mouse::PS2Mouse(
	const KeyboardMap & km
) : 
	HIDBase(km, false),
	device(-1), 
	offset(0U),
	stdbuttons(0U)
{
}

PS2Mouse::~PS2Mouse()
{
	restore();
}

inline
void 
PS2Mouse::set_input_fd(
	int n
) { 
	restore();
	device.reset(n); 
	save_and_set_mode();
}

inline
int
PS2Mouse::TranslateStdButton(
	const unsigned short button
) {
	switch (button) {
		case 0U:	return 0;
		case 1U:	return 2;
		case 2U:	return 1;
		default:	return -1;
	}
}

inline
short int
PS2Mouse::GetOffset9 ( uint8_t hi, uint8_t lo )
{
	return hi & 0x01 ? static_cast<int16_t>(lo & 0xFF) - 256 : static_cast<int16_t>(lo & 0xFF);
}

inline
void
PS2Mouse::handle_input_events(
	Realizer & r
) {
	const int n(read(device.get(), reinterpret_cast<char *>(buffer) + offset, sizeof buffer - offset));
	if (0 > n) return;
	offset += n;
	for (;;) {
		if (offset < MOUSE_PS2_PACKETSIZE) break;
 		if (short int dx = GetOffset9((buffer[0] & 0x10) >> 4, buffer[1]))
			r.handle_mouse_relpos(modifiers(), r.AXIS_X, dx);
		if (short int dy = GetOffset9((buffer[0] & 0x20) >> 5, buffer[2]))
			// Vertical movement is negated for some undocumented reason.
			r.handle_mouse_relpos(modifiers(), r.AXIS_Y, -dy);
		const unsigned newstdbuttons(buffer[0] & MOUSE_PS2_STDBUTTONS);
		for (unsigned short button(0U); button < MOUSE_PS2_MAXBUTTON; ++button) {
			const unsigned long mask(1UL << button);
			if ((stdbuttons & mask) != (newstdbuttons & mask)) {
				const bool down(newstdbuttons & mask);
				r.handle_mouse_button(modifiers(), TranslateStdButton(button), down);
			}
		}
		stdbuttons = newstdbuttons;
		std::memmove(buffer, buffer + MOUSE_PS2_PACKETSIZE, sizeof buffer - MOUSE_PS2_PACKETSIZE * sizeof *buffer);
		offset -= MOUSE_PS2_PACKETSIZE * sizeof *buffer;
	}
}

#elif defined(__FreeBSD__) || defined (__DragonFly__) || defined(__OpenBSD__)

/* FreeBSD/TrueOS/DragonFly BSD HIDs ****************************************
// **************************************************************************
*/

namespace {

typedef USBHIDBase HID;

class USBHID :
	public USBHIDBase
{
public:
	USBHID(const KeyboardMap &, bool);
	~USBHID() {}

	bool read_description();
protected:
	void write_output_report(const uint8_t, std::size_t);
	using USBHIDBase::read_description;
};

class GenericUSB :
	public USBHIDBase
{
public:
	GenericUSB(const KeyboardMap &, bool);
	~GenericUSB() {}

	void set_control_fd(int fd) { control.reset(fd); }
	bool read_description();
#if defined(__FreeBSD__) || defined(__DragonFly__)
	bool is_kernel_driver_attached(int) const;
	bool detach_kernel_driver(int);
#endif
protected:
	FileDescriptorOwner control;
	using USBHIDBase::read_description;

	void write_output_report(const uint8_t, std::size_t);
};

/// On the BSDs, atkbd devices are character devices with a terminal line discipline that speak the kbio protocol.
class ATKeyboardBase :
	public HIDBase
{
public:
	ATKeyboardBase(const KeyboardMap &, bool);
	~ATKeyboardBase() {}

	int query_input_fd() const { return device.get(); }
	void set_LEDs();
	void handle_input_events(Realizer &);
protected:
	FileDescriptorOwner device;
	char buffer[16];
	std::size_t offset;

	bool pressed[256];	///< used for determining auto-repeat keypresses
	bool is_pressed(uint8_t code) const { return pressed[code]; }
	void set_pressed(uint8_t code, bool v) { pressed[code] = v; }
};

#if defined(__FreeBSD__) || defined(__DragonFly__)
class ATKeyboard :
	public ATKeyboardBase
{
public:
	ATKeyboard(const KeyboardMap &, bool);
	~ATKeyboard();

	void reset(int);
protected:
	termios original_attr;
	long kbmode;
	void save_and_set_mode();
	void restore();
};
#endif

class KernelVT :
	public ATKeyboardBase
{
public:
	KernelVT(const KeyboardMap &, bool, int, int);
	~KernelVT();

	void reset(int);
	bool is_active();
	bool activate();
	bool wait_until_active();
	void acknowledge_switch_to();
	void permit_switch_from();
protected:
	struct vt_mode vtmode;
	const int release_signal, acquire_signal;
	termios original_attr;
	long kdmode;
	long kbmode;
	void save_and_set_mode();
	void restore();
};

#if defined(__FreeBSD__) || defined(__DragonFly__)
/// On the BSDs, sysmouse devices are character devices with a terminal line discipline that speak the sysmouse protocol.
class SysMouse :
	public HIDBase
{
public:
	SysMouse(const KeyboardMap &);
	~SysMouse();

	void set_input_fd(int);
	int query_input_fd() const { return device.get(); }
	void handle_input_events(Realizer & r);
protected:
	FileDescriptorOwner device;
	unsigned char buffer[MOUSE_SYS_PACKETSIZE * 16];
	std::size_t offset;

	termios original_attr;
	int level;
	void save_and_set_sysmouse_level();
	void restore();

	uint16_t stdbuttons, extbuttons;
	static int TranslateStdButton(const unsigned short button);

private:
	static bool IsSync (char b);
	static uint8_t Extend7to8 (uint8_t v);
	static short int GetOffset8 (uint8_t one, uint8_t two);
	static short int GetOffset7 (uint8_t one, uint8_t two);
}; 
#endif

}

inline
USBHID::USBHID(
	const KeyboardMap & km,
	bool n
) : 
	USBHIDBase(km, n)
{
}

#if defined(__FreeBSD__) || defined (__DragonFly__)
bool 
USBHID::read_description()
{
	usb_gen_descriptor d = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0 } };

	d.ugd_maxlen = 65535U;
	if (0 > ioctl(input.get(), USB_GET_REPORT_DESC, &d)) return false;
	std::vector<unsigned char> b(d.ugd_actlen);
	const std::size_t maxlen(d.ugd_actlen);
	d.ugd_data = b.data();
	d.ugd_maxlen = maxlen;
	if (0 > ioctl(input.get(), USB_GET_REPORT_DESC, &d)) return false;

	return read_description(b.data(), maxlen, d.ugd_actlen);
}
#endif

#if defined(__OpenBSD__)
bool 
USBHID::read_description()
{
	usb_ctl_report_desc d = { 0, { 0 } };
	const std::size_t maxlen(sizeof d.ucrd_data);

	d.ucrd_size = maxlen;
	if (0 > ioctl(input.get(), USB_GET_REPORT_DESC, &d)) return false;

	return read_description(d.ucrd_data, maxlen, d.ucrd_size);
}
#endif

void 
USBHID::write_output_report(
	const uint8_t report_id,
	std::size_t report_size
) {
	struct iovec v[] = {
		{ const_cast<uint8_t *>(&report_id), has_report_ids ? 1U : 0U },
		{ output_buffer, report_size },
	};
	writev(input.get(), v, sizeof v/sizeof *v);
}

inline
GenericUSB::GenericUSB(
	const KeyboardMap & km,
	bool n
) : 
	USBHIDBase(km, n),
	control(-1)
{
}

bool 
GenericUSB::read_description()
{
	std::vector<unsigned char> b(0x8000);
	usb_ctl_request d;
	d.ucr_addr = 0;
	d.ucr_actlen = 0;
	d.ucr_data = b.data();
	d.ucr_flags = USB_SHORT_XFER_OK;
	d.ucr_request.bmRequestType = 0x80 | 0x01;			// device to host, interface
	d.ucr_request.bRequest = 0x06;					// GET_DESCRIPTOR
	pack_littleendian(d.ucr_request.wValue, 0x2200, 2U);		// UHID report descriptor
	pack_littleendian(d.ucr_request.wIndex, 0x0000, 2U);		// interface zero
	pack_littleendian(d.ucr_request.wLength, b.size(), 2U);

	if (0 > ioctl(control.get(), USB_DO_REQUEST, &d)) return false;

	return read_description(b.data(), b.size(), d.ucr_actlen);
}

void 
GenericUSB::write_output_report(
	const uint8_t report_id,
	std::size_t report_size
) {
	usb_ctl_request d;
	d.ucr_addr = 0;
	d.ucr_actlen = 0;
	d.ucr_data = output_buffer;
	d.ucr_flags = USB_SHORT_XFER_OK;
	d.ucr_request.bmRequestType = 0x00 | 0x20 | 0x01;			// host to device, class, interface
	d.ucr_request.bRequest = 0x09;						// SET_REPORT
	pack_littleendian(d.ucr_request.wValue, 0x0200 | report_id, 2U);	// output report
	pack_littleendian(d.ucr_request.wIndex, 0x0000, 2U);			// interface zero
	pack_littleendian(d.ucr_request.wLength, report_size, 2U);

	ioctl(control.get(), USB_DO_REQUEST, &d);
}

#if defined(__FreeBSD__) || defined(__DragonFly__)
inline
bool 
GenericUSB::is_kernel_driver_attached(
	int index
) const {
	return 0 <= ioctl(control.get(), USB_IFACE_DRIVER_ACTIVE, &index);
}

inline
bool 
GenericUSB::detach_kernel_driver(
	int index
) {
	return 0 <= ioctl(control.get(), USB_IFACE_DRIVER_DETACH, &index);
}
#endif

inline
ATKeyboardBase::ATKeyboardBase(
	const KeyboardMap & km,
	bool n
) : 
	HIDBase(km, n),
	device(-1), 
	offset(0U)
{
	for (unsigned i(0U); i < sizeof pressed/sizeof *pressed; ++i)
		pressed[i] = false;
}

inline
void 
ATKeyboardBase::set_LEDs(
) {
	if (-1 != device.get()) {
		const int newled((caps_LED() ? LED_CAP : 0)|(num_LED() ? LED_NUM : 0)|(scroll_LED() ? LED_SCR : 0));
		ioctl(device.get(), KDSETLED, newled);
	}
}

inline
void
ATKeyboardBase::handle_input_events(
	Realizer & r
) {
	const ssize_t n(read(device.get(), reinterpret_cast<char *>(buffer) + offset, sizeof buffer - offset));
	if (0 > n) return;
	for (
		offset += n;
		offset > 0 && offset >= sizeof *buffer;
		std::memmove(buffer, buffer + 1U, sizeof buffer - sizeof *buffer),
		offset -= sizeof *buffer
	) {
		uint16_t code(buffer[0]);
		const bool bit7(code & 0x80);
		code &= 0x7F;
		const unsigned value(bit7 ? 0U : is_pressed(code) ? 2U : 1U);
		set_pressed(code, !bit7);

		const uint16_t index(bsd_keycode_to_keymap_index(code));
		handle_keyboard(r, index, value);
	}
}

#if defined(__FreeBSD__) || defined(__DragonFly__)
ATKeyboard::ATKeyboard(
	const KeyboardMap & km,
	bool n
) :
	ATKeyboardBase(km, n),
	original_attr(),
	kbmode()
{
	save_and_set_mode();
}

ATKeyboard::~ATKeyboard()
{
	restore();
}

inline
void 
ATKeyboard::reset(
	int n
) { 
	restore();
	device.reset(n); 
	save_and_set_mode();
}

void 
ATKeyboard::save_and_set_mode()
{
	if (-1 != device.get()) {
		// The line discipline needs to be set to raw mode for the duration.
		if (0 <= tcgetattr_nointr(device.get(), original_attr))
			tcsetattr_nointr(device.get(), TCSADRAIN, make_raw(original_attr));
		// And beneath that the keyboard needs to be set to keycode mode.
		ioctl(device.get(), KDGKBMODE, &kbmode);
		ioctl(device.get(), KDSKBMODE, K_CODE);
 	}
}

void
ATKeyboard::restore()
{
	if (-1 != device.get()) {
		ioctl(device.get(), KDSKBMODE, kbmode);
		tcsetattr_nointr(device.get(), TCSADRAIN, original_attr);
	}
}
#endif

KernelVT::KernelVT(
	const KeyboardMap & km, 
	bool n,
	int rs, 
	int as
) :
	ATKeyboardBase(km, n),
	vtmode(),
	release_signal(rs),
	acquire_signal(as),
	original_attr(),
	kdmode(),
	kbmode()
{
	save_and_set_mode();
}

KernelVT::~KernelVT()
{
	restore();
}

void
KernelVT::save_and_set_mode()
{
	if (-1 != device.get()) {
		ioctl(device.get(), VT_GETMODE, &vtmode);
		// Set the VT switching to send the given release and acquire signals to this process.
		struct vt_mode m = { VT_PROCESS, 0, static_cast<short>(release_signal), static_cast<char>(acquire_signal), static_cast<char>(acquire_signal) };
		ioctl(device.get(), VT_SETMODE, &m);
		// Tell the terminal emulator not to draw its character buffer, with KD_GRAPHICS or equivalent.
#if defined(__OpenBSD__)
		// OpenBSD bug: There's no API for finding out the prior drawing setting, so we always restore to KD_TEXT.
		kdmode = KD_TEXT;
#else
		ioctl(device.get(), KDGETMODE, &kdmode);
#endif
		ioctl(device.get(), KDSETMODE, KD_GRAPHICS);
		// The line discipline needs to be set to raw mode for the duration.
		if (0 <= tcgetattr_nointr(device.get(), original_attr))
			tcsetattr_nointr(device.get(), TCSADRAIN, make_raw(original_attr));
		// And beneath that the keyboard needs to be set to keycode mode.
		ioctl(device.get(), KDGKBMODE, &kbmode);
#if defined(__OpenBSD__)
		ioctl(device.get(), KDSKBMODE, K_RAW);
#else
		ioctl(device.get(), KDSKBMODE, K_CODE);
#endif
 	}
 }

void
KernelVT::restore()
{
	if (-1 != device.get()) {
		ioctl(device.get(), KDSKBMODE, kbmode);
		tcsetattr_nointr(device.get(), TCSADRAIN, original_attr);
		ioctl(device.get(), KDSETMODE, kdmode);
		ioctl(device.get(), VT_SETMODE, &vtmode);
	}
}

void 
KernelVT::reset(int nfd) 
{ 
	restore(); 
	device.reset(nfd);
	save_and_set_mode();
}

#if defined(__FreeBSD__) || defined(__DragonFly__)
bool
KernelVT::is_active()
{
	if (-1 == device.get()) return true;
	int active;
	if (0 > ioctl(device.get(), VT_GETACTIVE, &active)) return false;
	int index;
	if (0 > ioctl(device.get(), VT_GETINDEX, &index)) return false;
	return active == index;
}

bool
KernelVT::activate()
{
	if (-1 == device.get()) return true;
	int index;
	if (0 > ioctl(device.get(), VT_GETINDEX, &index)) return false;
	while (0 > ioctl(device.get(), VT_ACTIVATE, index)) {
		if (EINTR != errno) return false;
	}
	return true;
}

bool
KernelVT::wait_until_active()
{
	if (-1 == device.get()) return true;
	int index;
	if (0 > ioctl(device.get(), VT_GETINDEX, &index)) return false;
	while (0 > ioctl(device.get(), VT_WAITACTIVE, index)) {
		if (EINTR != errno) return false;
	}
	return true;
}
#endif

void 
KernelVT::acknowledge_switch_to()
{
	if (-1 != device.get())
		ioctl(device.get(), VT_RELDISP, VT_ACKACQ);
}

void 
KernelVT::permit_switch_from()
{
	if (-1 != device.get())
		ioctl(device.get(), VT_RELDISP, 1);
}

#if defined(__FreeBSD__) || defined(__DragonFly__)
inline
SysMouse::SysMouse(
	const KeyboardMap & km
) : 
	HIDBase(km, false),
	device(-1), 
	offset(0U),
	original_attr(),
	level(),
	stdbuttons(0U),
	extbuttons(0U)
{
}

SysMouse::~SysMouse()
{
	restore();
}

inline
void 
SysMouse::set_input_fd(
	int n
) { 
	restore();
	device.reset(n); 
	save_and_set_sysmouse_level();
}

inline
bool
SysMouse::IsSync ( char b ) 
{ 
	return (b & MOUSE_SYS_SYNCMASK) == MOUSE_SYS_SYNC; 
}

inline
uint8_t
SysMouse::Extend7to8 ( uint8_t v ) 
{
	return (v & 0x7F) | ((v & 0x40) << 1);
}

inline
short int
SysMouse::GetOffset8 ( uint8_t one, uint8_t two )
{
	return static_cast<int8_t>(one) + static_cast<int8_t>(two);
}

inline
short int
SysMouse::GetOffset7 ( uint8_t one, uint8_t two )
{
	return GetOffset8(Extend7to8(one), Extend7to8(two));
}

inline
int
SysMouse::TranslateStdButton(
	const unsigned short button
) {
	switch (button) {
		case 0U:	return 2;
		case 1U:	return 1;
		case 2U:	return 0;
		default:	return -1;
	}
}

inline
void
SysMouse::handle_input_events(
	Realizer & r
) {
	const int n(read(device.get(), reinterpret_cast<char *>(buffer) + offset, sizeof buffer - offset));
	if (0 > n) return;
	// Because of an unavoidable race caused by the way that the FreeBSD mouse driver works, we have to cope with both the level 0 and the level 1 protocols.
	// Fortunately, the Mouse Systems protocol (according to MS own doco) allows for extensibility, and is synchronized with an initial flag byte.
	offset += n;
	for (;;) {
		while (offset > 0U && !IsSync(buffer[0])) {
			std::memmove(buffer, buffer + 1, sizeof buffer - sizeof *buffer),
			--offset;
		}
		if (offset < MOUSE_MSC_PACKETSIZE) break;
 		if (short int dx = GetOffset8(buffer[1], buffer[3]))
			r.handle_mouse_relpos(modifiers(), r.AXIS_X, dx);
		if (short int dy = GetOffset8(buffer[2], buffer[4]))
			// Vertical movement is negated in the original MouseSystems device protocol.
			r.handle_mouse_relpos(modifiers(), r.AXIS_Y, -dy);
		const bool ext(offset >= MOUSE_SYS_PACKETSIZE && !IsSync(buffer[5]) && !IsSync(buffer[6]) && !IsSync(buffer[7]));
	       	if (ext) {
			if (short int dz = GetOffset7(buffer[5], buffer[6]))
				r.handle_mouse_relpos(modifiers(), r.V_SCROLL, dz);
		}
		const unsigned newstdbuttons(buffer[0] & MOUSE_SYS_STDBUTTONS);
		for (unsigned short button(0U); button < MOUSE_MSC_MAXBUTTON; ++button) {
			const unsigned long mask(1UL << button);
			if ((stdbuttons & mask) != (newstdbuttons & mask)) {
				const bool up(newstdbuttons & mask);
				r.handle_mouse_button(modifiers(), TranslateStdButton(button), !up);
			}
		}
		stdbuttons = newstdbuttons;
	       	if (ext) {
			const unsigned newextbuttons(buffer[7] & MOUSE_SYS_EXTBUTTONS);
			for (unsigned short button(0U); button < (MOUSE_SYS_MAXBUTTON - MOUSE_MSC_MAXBUTTON); ++button) {
				const unsigned long mask(1UL << button);
				if ((extbuttons & mask) != (newextbuttons & mask)) {
					const bool up(newextbuttons & mask);
					r.handle_mouse_button(modifiers(), button + MOUSE_MSC_MAXBUTTON, !up);
				}
			}
			extbuttons = newextbuttons;
			std::memmove(buffer, buffer + MOUSE_SYS_PACKETSIZE, sizeof buffer - MOUSE_SYS_PACKETSIZE * sizeof *buffer),
			offset -= MOUSE_SYS_PACKETSIZE * sizeof *buffer;
		} else {
			std::memmove(buffer, buffer + MOUSE_MSC_PACKETSIZE, sizeof buffer - MOUSE_MSC_PACKETSIZE * sizeof *buffer),
			offset -= MOUSE_MSC_PACKETSIZE * sizeof *buffer;
		}
	}
}

// Like the BSD cfmakeraw() and our make_raw(), but even harder still than both.
static inline
termios
make_mouse (
	const termios & ti
) {
	termios t(ti);
	t.c_iflag = IGNPAR|IGNBRK;
	t.c_oflag = 0;
	t.c_cflag = CS8|CSTOPB|CREAD|CLOCAL|HUPCL;
	t.c_lflag = 0;
	t.c_cc[VTIME] = 0;
	t.c_cc[VMIN] = 1;
	return t;
}

/// The line discipline needs to be set to "mouse" mode for the duration.
/// And beneath that the mouse protocol level needs to be set to the common sysmouse protocol level.
void 
SysMouse::save_and_set_sysmouse_level()
{
	if (-1 != device.get()) {
		ioctl(device.get(), MOUSE_GETLEVEL, &level);
		int one(1);
		ioctl(device.get(), MOUSE_SETLEVEL, &one);
		if (0 <= tcgetattr_nointr(device.get(), original_attr))
			tcsetattr_nointr(device.get(), TCSADRAIN, make_mouse(original_attr));
	}
}

void
SysMouse::restore()
{
	if (-1 != device.get()) {
		tcsetattr_nointr(device.get(), TCSADRAIN, original_attr);
		ioctl(device.get(), MOUSE_SETLEVEL, &level);
	}
}
#endif

#else

#error "Don't know how to handle HIDs for your operating system."

#endif

/* The HID list *************************************************************
// **************************************************************************
*/

namespace {

class HIDList : public std::list<HID *> {
public:
	~HIDList();
#if defined(__LINUX__) || defined(__linux__)
	HID & add(const KeyboardMap & km, bool n) { HID * p(new HID(km, n)); push_back(p); return *p; }
#elif defined(__FreeBSD__) || defined (__DragonFly__) || defined(__OpenBSD__)
	GenericUSB & add_ugen(const KeyboardMap & km, bool n) { GenericUSB * p(new GenericUSB(km, n)); push_back(p); return *p; }
	USBHID & add_uhid(const KeyboardMap & km, bool n) { USBHID * p(new USBHID(km, n)); push_back(p); return *p; }
#endif
};

}

HIDList::~HIDList()
{
	for (iterator p(begin()); end() != p; p = erase(p))
		delete *p;
}

/* Main function ************************************************************
// **************************************************************************
*/

void
console_fb_realizer [[gnu::noreturn]] ( 
	const char * & /*next_prog*/,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	const char * keyboard_map_filename(0);
#if defined(__LINUX__) || defined(__linux__)
	unsigned long kernel_vt_number(0U);
#elif defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
	const char * kernel_vt_filename(0);
#endif
#if defined(__LINUX__) || defined(__linux__)
	const char * ps2mouse_filename(0);
#elif defined(__FreeBSD__) || defined(__DragonFly__)
	const char * atkeyboard_filename(0);
	const char * sysmouse_filename(0);
#endif
	bool limit_80_columns(false);
	std::list<std::string> input_filenames;
	std::list< std::pair<std::string,std::string> > ugen_input_filenames;
	bool wrong_way_up(false);
	bool bold_as_colour(false);
	bool initial_numlock(false);
	FontSpecList fonts;
	unsigned long quadrant(3U);

	try {
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
		bool has_kernel_vt(false);
#endif
		popt::bool_definition bold_as_colour_option('\0', "bold-as-colour", "Forcibly render boldface as a colour brightness change.", bold_as_colour);
#if defined(__LINUX__) || defined(__linux__)
		popt::unsigned_number_definition kernel_vt_number_option('\0', "kernel-vt-number", "number", "Use the given kernel virtual terminal.", kernel_vt_number, 10);
#elif defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
		popt::bool_definition limit_80_columns_option('\0', "80-columns", "Limit to no wider than 80 columns.", limit_80_columns);
		popt::bool_definition kernel_vt_option('\0', "kernel-vt", "Use a kernel virtual terminal device.", has_kernel_vt);
#endif
		popt::string_list_definition input_option('\0', "input", "device", "Use the this input device.", input_filenames);
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
		popt::string_pair_list_definition ugen_input_option('\0', "ugen-input", "control function", "Use the this ugen input control and function device pair.", ugen_input_filenames);
#endif
#if defined(__LINUX__) || defined(__linux__)
		popt::string_definition ps2mouse_option('\0', "ps2mouse", "device", "Use this ps2mouse input device.", ps2mouse_filename);
#elif defined(__FreeBSD__) || defined(__DragonFly__)
		popt::string_definition atkeyboard_option('\0', "atkeyboard", "device", "Use this atkbd input device.", atkeyboard_filename);
		popt::string_definition sysmouse_option('\0', "sysmouse", "device", "Use this sysmouse input device.", sysmouse_filename);
#endif
		popt::bool_definition initial_numlock_option('\0', "initial-numlock", "Turn NumLock on, initially.", initial_numlock);
		popt::string_definition keyboard_map_option('\0', "keyboard-map", "filename", "Use this keyboard map.", keyboard_map_filename);
		popt::unsigned_number_definition quadrant_option('\0', "quadrant", "number", "Position the terminal in quadrant 0, 1, 2, or 3.", quadrant, 0);
		popt::bool_definition wrong_way_up_option('\0', "wrong-way-up", "Display from bottom to top.", wrong_way_up);
		fontspec_definition vtfont_option('\0', "vtfont", "filename", "Use this font as a medium+bold upright vt font.", fonts, -1, CombinedFont::Font::UPRIGHT);
		fontspec_definition vtfont_faint_r_option('\0', "vtfont-faint-r", "filename", "Use this font as a light+demibold upright vt font.", fonts, -2, CombinedFont::Font::UPRIGHT);
		fontspec_definition vtfont_faint_o_option('\0', "vtfont-faint-o", "filename", "Use this font as a light+demibold oblique vt font.", fonts, -2, CombinedFont::Font::OBLIQUE);
		fontspec_definition vtfont_faint_i_option('\0', "vtfont-faint-i", "filename", "Use this font as a light+demibold italic vt font.", fonts, -2, CombinedFont::Font::ITALIC);
		fontspec_definition vtfont_normal_r_option('\0', "vtfont-normal-r", "filename", "Use this font as a medium+bold upright vt font.", fonts, -1, CombinedFont::Font::UPRIGHT);
		fontspec_definition vtfont_normal_o_option('\0', "vtfont-normal-o", "filename", "Use this font as a medium+bold oblique vt font.", fonts, -1, CombinedFont::Font::OBLIQUE);
		fontspec_definition vtfont_normal_i_option('\0', "vtfont-normal-i", "filename", "Use this font as a medium+bold italic vt font.", fonts, -1, CombinedFont::Font::ITALIC);
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
		popt::definition * top_table[] = {
			&bold_as_colour_option,
			&input_option,
#if defined(__LINUX__) || defined(__linux__)
			&kernel_vt_number_option,
#elif defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
			&kernel_vt_option,
			&limit_80_columns_option,
			&ugen_input_option,
#endif
#if defined(__LINUX__) || defined(__linux__)
			&ps2mouse_option,
#elif defined(__FreeBSD__) || defined(__DragonFly__)
			&atkeyboard_option,
			&sysmouse_option,
#endif
			&initial_numlock_option,
			&keyboard_map_option,
			&quadrant_option,
			&wrong_way_up_option,
			&vtfont_option,
			&vtfont_faint_r_option,
			&vtfont_faint_o_option,
			&vtfont_faint_i_option,
			&vtfont_normal_r_option,
			&vtfont_normal_o_option,
			&vtfont_normal_i_option,
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
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{virtual-terminal} {framebuffer}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		if (p.stopped()) throw EXIT_SUCCESS;
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
		if (has_kernel_vt)
			kernel_vt_filename = envs.query("TTY");
#else
		static_cast<void>(envs);	// Silence a compiler warning.
#endif
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw static_cast<int>(EXIT_USAGE);
	}

	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing virtual terminal directory name.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * vt_dirname(args.front());
	args.erase(args.begin());
#if defined(__LINUX__) || defined(__linux__)
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing framebuffer device file name.");
		throw static_cast<int>(EXIT_USAGE);
	}
	const char * fb_filename(args.front());
	args.erase(args.begin());
#elif defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
	const char * fb_filename(0);
	if (args.empty()) {
		if (kernel_vt_filename)
			fb_filename = kernel_vt_filename;
		else {
			std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing framebuffer device file name.");
			throw static_cast<int>(EXIT_USAGE);
		}
	} else {
		fb_filename = args.front();
		args.erase(args.begin());
	}
#endif
	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Unexpected argument.");
		throw static_cast<int>(EXIT_USAGE);
	}

	CombinedFont font;
	HIDList inputs;
	KeyboardMap keyboard_map;
#if defined(__LINUX__) || defined(__linux__)
	PS2Mouse ps2mou(keyboard_map);
#elif defined(__FreeBSD__) || defined(__DragonFly__)
	ATKeyboard atkbd(keyboard_map, initial_numlock);
	SysMouse sysmou(keyboard_map);
#endif

	// Open non-devices first, so that abends during setup don't leave hardware in strange states.

	for (FontSpecList::const_iterator n(fonts.begin()); fonts.end() != n; ++n) {
		for (CombinedFont::Font::Weight weight(static_cast<CombinedFont::Font::Weight>(0)); weight < CombinedFont::Font::NUM_WEIGHTS; weight = static_cast<CombinedFont::Font::Weight>(weight + 1)) {
			for (CombinedFont::Font::Slant slant(static_cast<CombinedFont::Font::Slant>(0)); slant < CombinedFont::Font::NUM_SLANTS; slant = static_cast<CombinedFont::Font::Slant>(slant + 1)) {
				if (n->slant != slant) continue;
				if (n->weight == -1) {
					if (CombinedFont::Font::MEDIUM != weight && CombinedFont::Font::BOLD != weight) continue;
				} else 
				if (n->weight == -2) {
					if (CombinedFont::Font::LIGHT != weight && CombinedFont::Font::DEMIBOLD != weight) continue;
				} else 
				if (n->weight != weight) 
					continue;
				LoadFont(prog, font, weight, slant, n->name.c_str());
			}
		}
	}

	const FileDescriptorOwner queue(kqueue());
	if (0 > queue.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kqueue", std::strerror(error));
		throw EXIT_FAILURE;
	}
	std::vector<struct kevent> ip;

	FileDescriptorOwner vt_dir_fd(open_dir_at(AT_FDCWD, vt_dirname));
	if (0 > vt_dir_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, vt_dirname, std::strerror(error));
		throw EXIT_FAILURE;
	}
	FileDescriptorOwner buffer_fd(open_read_at(vt_dir_fd.get(), "display"));
	if (0 > buffer_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, vt_dirname, "display", std::strerror(error));
		throw EXIT_FAILURE;
	}
	FileStar buffer_file(fdopen(buffer_fd.get(), "r"));
	if (!buffer_file) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, vt_dirname, "display", std::strerror(error));
		throw EXIT_FAILURE;
	}
	buffer_fd.release();
	FileDescriptorOwner input_fd(-1);
	if (!input_filenames.empty()
#if defined(__LINUX__) || defined(__linux__)
	|| ps2mouse_filename
#elif defined(__FreeBSD__) || defined(__DragonFly__)
	|| atkeyboard_filename || sysmouse_filename || !ugen_input_filenames.empty()
#endif
	) {
       		input_fd.reset(open_writeexisting_at(vt_dir_fd.get(), "input"));
		if (0 > input_fd.get()) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, vt_dirname, "input", std::strerror(error));
			throw EXIT_FAILURE;
		}
	}
	vt_dir_fd.release();

	if (keyboard_map_filename)
		LoadKeyMap(prog, keyboard_map, keyboard_map_filename);
	else {
		set_default(keyboard_map);
		overlay_group2_latch(keyboard_map);
	}

	// Now open devices.

	FramebufferIO fb(open_readwriteexisting_at(AT_FDCWD, fb_filename), limit_80_columns);
	if (fb.get() < 0) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, fb_filename, std::strerror(error));
		throw EXIT_FAILURE;
	}
	fb.save(prog, fb_filename);
	ReserveSignalsForKQueue kqueue_reservation(SIGTERM, SIGINT, SIGHUP, SIGPIPE, SIGWINCH, SIGUSR1, SIGUSR2, 0);
	PreventDefaultForFatalSignals ignored_signals(SIGTERM, SIGINT, SIGHUP, SIGPIPE, SIGUSR1, SIGUSR2, 0);
	append_event(ip, SIGWINCH, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	append_event(ip, SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	append_event(ip, SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	append_event(ip, SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	append_event(ip, SIGPIPE, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);

#if defined(__LINUX__) || defined(__linux__)
	KernelVT kvt(keyboard_map, initial_numlock, SIGUSR1, SIGUSR2);
	if (kernel_vt_number > 0UL) {
		kvt.reset(prog, kernel_vt_number);
		append_event(ip, SIGUSR1, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		append_event(ip, SIGUSR2, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		append_event(ip, kvt.query_input_fd(), EVFILT_READ, EV_ADD|EV_DISABLE, 0, 0, 0);
	}

	for (std::list<std::string>::const_iterator i(input_filenames.begin()); input_filenames.end() != i; ++i) {
		const std::string & input_filename(*i);
		HID & input(inputs.add(keyboard_map, initial_numlock));
		FileDescriptorOwner fd1(open_readwriteexisting_at(AT_FDCWD, input_filename.c_str())); 
		if (0 > fd1.get()) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, input_filename.c_str(), std::strerror(error));
			throw EXIT_FAILURE;
		}
		input.set_input_fd(fd1.release());
		if (!input.set_exclusive(true)) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, input_filename.c_str(), std::strerror(error));
			throw EXIT_FAILURE;
		}
		append_event(ip, input.query_input_fd(), EVFILT_READ, EV_ADD|EV_DISABLE, 0, 0, 0);
	}
#elif defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
	KernelVT kvt(keyboard_map, initial_numlock, SIGUSR1, SIGUSR2);
	if (kernel_vt_filename) {
		FileDescriptorOwner fd(open_readwriteexisting_at(AT_FDCWD, kernel_vt_filename));
		if (0 > fd.get()) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, kernel_vt_filename, std::strerror(error));
			throw EXIT_FAILURE;
		}
		if (!isatty(fd.get())) {
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, kernel_vt_filename, "Not a terminal device.");
			throw EXIT_FAILURE;
		}
		kvt.reset(fd.release());
		append_event(ip, SIGUSR1, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		append_event(ip, SIGUSR2, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		append_event(ip, kvt.query_input_fd(), EVFILT_READ, EV_ADD|EV_DISABLE, 0, 0, 0);
	}

	for (std::list<std::string>::const_iterator i(input_filenames.begin()); input_filenames.end() != i; ++i) {
		const std::string & input_filename(*i);
		USBHID & input(inputs.add_uhid(keyboard_map, initial_numlock));
		FileDescriptorOwner fd1(open_readwriteexisting_at(AT_FDCWD, input_filename.c_str())); 
		if (0 > fd1.get()) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, input_filename.c_str(), std::strerror(error));
			throw EXIT_FAILURE;
		}
		input.set_input_fd(fd1.release());
		if (!input.read_description()) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, input_filename.c_str(), std::strerror(error));
			throw EXIT_FAILURE;
		}
		append_event(ip, input.query_input_fd(), EVFILT_READ, EV_ADD|EV_DISABLE, 0, 0, 0);
	}
	for (std::list< std::pair<std::string,std::string> >::const_iterator i(ugen_input_filenames.begin()); ugen_input_filenames.end() != i; ++i) {
		const std::string & control_filename(i->first);
		const std::string & input_filename(i->second);
		GenericUSB & input(inputs.add_ugen(keyboard_map, initial_numlock));
		// The control device is for control requests and output/feature reports.
		FileDescriptorOwner fd1(open_readwriteexisting_at(AT_FDCWD, control_filename.c_str())); 
		if (0 > fd1.get()) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, control_filename.c_str(), std::strerror(error));
			throw EXIT_FAILURE;
		}
		// The input device is for input reports only.
		FileDescriptorOwner fd2(open_read_at(AT_FDCWD, input_filename.c_str())); 
		if (0 > fd2.get()) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, input_filename.c_str(), std::strerror(error));
			throw EXIT_FAILURE;
		}
		input.set_control_fd(fd1.release());
		input.set_input_fd(fd2.release());
		if (!input.read_description()) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, input_filename.c_str(), std::strerror(error));
			throw EXIT_FAILURE;
		}
#if defined(__FreeBSD__) || defined(__DragonFly__)
		if (input.is_kernel_driver_attached(0)) {
			std::fprintf(stderr, "%s: INFO: %s: %s\n", prog, input_filename.c_str(), "Detaching kernel device driver from ugen device.");
			if (!input.detach_kernel_driver(0)) {
				const int error(errno);
				std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, input_filename.c_str(), std::strerror(error));
				throw EXIT_FAILURE;
			}
		}
#endif
		append_event(ip, input.query_input_fd(), EVFILT_READ, EV_ADD|EV_DISABLE, 0, 0, 0);
	}
#endif

#if defined(__LINUX__) || defined(__linux__)
	if (ps2mouse_filename) {
		FileDescriptorOwner fd1(open_readwriteexisting_at(AT_FDCWD, ps2mouse_filename)); 
		if (0 > fd1.get()) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, ps2mouse_filename, std::strerror(error));
			throw EXIT_FAILURE;
		}
		ps2mou.set_input_fd(fd1.release());
		append_event(ip, ps2mou.query_input_fd(), EVFILT_READ, EV_ADD|EV_DISABLE, 0, 0, 0);
	}
#elif defined(__FreeBSD__) || defined(__DragonFly__)
	if (atkeyboard_filename) {
		FileDescriptorOwner fd1(open_readwriteexisting_at(AT_FDCWD, atkeyboard_filename)); 
		if (0 > fd1.get()) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, atkeyboard_filename, std::strerror(error));
			throw EXIT_FAILURE;
		}
		atkbd.reset(fd1.release());
		append_event(ip, atkbd.query_input_fd(), EVFILT_READ, EV_ADD|EV_DISABLE, 0, 0, 0);
	}
	if (sysmouse_filename) {
		FileDescriptorOwner fd1(open_readwriteexisting_at(AT_FDCWD, sysmouse_filename)); 
		if (0 > fd1.get()) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, sysmouse_filename, std::strerror(error));
			throw EXIT_FAILURE;
		}
		sysmou.set_input_fd(fd1.release());
		append_event(ip, sysmou.query_input_fd(), EVFILT_READ, EV_ADD|EV_DISABLE, 0, 0, 0);
	}
#endif
	const bool has_pointer(!inputs.empty()
#if defined(__LINUX__) || defined(__linux__)
	|| ps2mouse_filename
#elif defined(__FreeBSD__) || defined(__DragonFly__)
	|| sysmouse_filename || !ugen_input_filenames.empty()
#endif
	);

#if defined(__LINUX__) || defined(__linux__) || defined(__FreeBSD__) || defined(__DragonFly__)
	// FreeBSD's checks on mmap()ing the framebuffer fail unless the relevant virtual terminal is the currently active one.
	// Linux has similar problems.
	if (0 <= kvt.query_input_fd()) {
		if (!kvt.activate()
		||  !kvt.wait_until_active()
		) {
			const int error(errno);
#if defined(__LINUX__) || defined(__linux__)
			std::fprintf(stderr, "%s: FATAL: tty%lu: %s\n", prog, kernel_vt_number, std::strerror(error));
#else
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, kernel_vt_filename, std::strerror(error));
#endif
			throw EXIT_FAILURE;
		}
	}
#endif
	fb.set_graphics_mode(prog, fb_filename);

	void * const base(mmap(0, fb.query_size(), PROT_READ|PROT_WRITE, MAP_SHARED, fb.get(), 0));
	if (MAP_FAILED == base) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, fb_filename, std::strerror(error));
		throw EXIT_FAILURE;
	}

	VirtualTerminalBackEnd vt(vt_dirname, buffer_file.release(), input_fd.release());
	append_event(ip, vt.query_buffer_fd(), EVFILT_VNODE, EV_ADD|EV_ENABLE|EV_CLEAR, NOTE_WRITE, 0, 0);
	append_event(ip, vt.query_input_fd(), EVFILT_WRITE, EV_ADD|EV_DISABLE, 0, 0, 0);
	TUIDisplayCompositor c(true /* software cursor */, Realizer::pixel_to_row(fb.query_yres()), Realizer::pixel_to_column(fb.query_xres()));
	GraphicsInterface gdi(base, fb.query_size(), fb.query_yres(), fb.query_xres(), fb.query_stride(), fb.query_depth());
	Realizer realizer(fb, quadrant, wrong_way_up, !font.has_faint(), bold_as_colour, has_pointer, gdi, font, vt, c);

#if defined(__LINUX__) || defined(__linux__)
	if (0 <= kvt.query_input_fd()) {
		if (kvt.is_active())
			usr2_signalled = true;
		else
			usr1_signalled = true;
	} else
		usr2_signalled = true;
#elif defined(__FreeBSD__) || defined(__DragonFly__)
	if (0 <= kvt.query_input_fd())
		usr2_signalled = kvt.is_active();
	else
		usr2_signalled = true;
#elif defined(__OpenBSD__)
	usr2_signalled = true;
#endif

	bool active(false);

	const struct timespec immediate_timeout = { 0, 0 };

	while (true) {
		if (terminate_signalled||interrupt_signalled||hangup_signalled) 
			break;
		if (usr1_signalled)  {
			usr1_signalled = false;
			if (active) {
#if defined(__LINUX__) || defined(__linux__) || defined(__FreeBSD__) || defined(__DragonFly__)
				if (0 <= kvt.query_input_fd())
					append_event(ip, kvt.query_input_fd(), EVFILT_READ, EV_DISABLE, 0, 0, 0);
#endif
#if defined(__LINUX__) || defined(__linux__)
				if (0 <= ps2mou.query_input_fd())
					append_event(ip, ps2mou.query_input_fd(), EVFILT_READ, EV_DISABLE, 0, 0, 0);
#elif defined(__FreeBSD__) || defined(__DragonFly__)
				if (0 <= atkbd.query_input_fd())
					append_event(ip, atkbd.query_input_fd(), EVFILT_READ, EV_DISABLE, 0, 0, 0);
				if (0 <= sysmou.query_input_fd())
					append_event(ip, sysmou.query_input_fd(), EVFILT_READ, EV_DISABLE, 0, 0, 0);
#endif
				for (HIDList::iterator i(inputs.begin()); inputs.end() != i; ++i) {
					HID & input(**i);
					append_event(ip, input.query_input_fd(), EVFILT_READ, EV_DISABLE, 0, 0, 0);
#if defined(__LINUX__) || defined(__linux__)
					input.set_exclusive(false);
#endif
				}
				active = false;
			}
#if defined(__LINUX__) || defined(__linux__) || defined(__FreeBSD__) || defined(__DragonFly__)
			if (0 <= kvt.query_input_fd())
				kvt.permit_switch_from();
#endif
		}
		if (usr2_signalled) {
			usr2_signalled = false;
			if (!active) {
#if defined(__LINUX__) || defined(__linux__) || defined(__FreeBSD__) || defined(__DragonFly__)
				if (0 <= kvt.query_input_fd()) {
					if (kvt.query_dirty_LEDs()) {
						kvt.set_LEDs();
						kvt.clean_LEDs();
					}
					append_event(ip, kvt.query_input_fd(), EVFILT_READ, EV_ENABLE, 0, 0, 0);
				}
#endif
#if defined(__FreeBSD__) || defined (__DragonFly__)
				if (atkbd.query_dirty_LEDs()) {
					atkbd.set_LEDs();
					atkbd.clean_LEDs();
				}
#endif
				for (HIDList::iterator j(inputs.begin()); inputs.end() != j; ++j) {
					HID & input(**j);
					if (input.query_dirty_LEDs()) {
						input.set_LEDs();
						input.clean_LEDs();
					}
				}
#if defined(__LINUX__) || defined(__linux__)
				if (0 <= ps2mou.query_input_fd())
					append_event(ip, ps2mou.query_input_fd(), EVFILT_READ, EV_ENABLE, 0, 0, 0);
#elif defined(__FreeBSD__) || defined(__DragonFly__)
				if (0 <= atkbd.query_input_fd())
					append_event(ip, atkbd.query_input_fd(), EVFILT_READ, EV_ENABLE, 0, 0, 0);
				if (0 <= sysmou.query_input_fd())
					append_event(ip, sysmou.query_input_fd(), EVFILT_READ, EV_ENABLE, 0, 0, 0);
#endif
				for (HIDList::iterator i(inputs.begin()); inputs.end() != i; ++i) {
					HID & input(**i);
					append_event(ip, input.query_input_fd(), EVFILT_READ, EV_ENABLE, 0, 0, 0);
#if defined(__LINUX__) || defined(__linux__)
					input.set_exclusive(true);
#endif
				}
				realizer.set_refresh_needed();
				realizer.invalidate_all();
				active = true;
			}
#if defined(__LINUX__) || defined(__linux__) || defined(__FreeBSD__) || defined(__DragonFly__)
			if (0 <= kvt.query_input_fd())
				kvt.acknowledge_switch_to();
#endif
		}
		realizer.handle_refresh_event();
		if (active)
			realizer.handle_update_event();

		struct kevent p[512];
		const int rc(kevent(queue.get(), ip.data(), ip.size(), p, sizeof p/sizeof *p, vt.query_reload_needed() ? &immediate_timeout : 0));
		ip.clear();

		if (0 > rc) {
			const int error(errno);
			if (EINTR == error) continue;
			fb.restore();
#if defined(__LINUX__) || defined(__linux__)
			kvt.release();
#elif defined(__FreeBSD__) || defined (__DragonFly__)
			kvt.reset(-1);
#endif
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kevent", std::strerror(error));
			throw EXIT_FAILURE;
		}

		if (0 == rc) {
			if (vt.query_reload_needed()) {
				vt.reload();
				realizer.set_refresh_needed();
			}
			continue;
		}

		for (std::size_t i(0); i < static_cast<std::size_t>(rc); ++i) {
			const struct kevent & e(p[i]);
			switch (e.filter) {
				case EVFILT_VNODE:
					if (0 <= static_cast<int>(e.ident)) {
						if (vt.query_buffer_fd() == static_cast<int>(e.ident)) 
							vt.set_reload_needed();
					}
					break;
				case EVFILT_SIGNAL:
					handle_signal(e.ident);
					break;
				case EVFILT_READ:
					if (0 <= static_cast<int>(e.ident)) {
#if defined(__LINUX__) || defined(__linux__) || defined(__FreeBSD__) || defined(__DragonFly__)
						if (kvt.query_input_fd() == static_cast<int>(e.ident)) 
							kvt.handle_input_events(realizer);
#endif
#if defined(__LINUX__) || defined(__linux__)
						if (ps2mou.query_input_fd() == static_cast<int>(e.ident)) 
							ps2mou.handle_input_events(realizer);
#elif defined(__FreeBSD__) || defined (__DragonFly__)
						if (atkbd.query_input_fd() == static_cast<int>(e.ident)) 
							atkbd.handle_input_events(realizer);
						if (sysmou.query_input_fd() == static_cast<int>(e.ident)) 
							sysmou.handle_input_events(realizer);
#endif
						for (HIDList::iterator j(inputs.begin()); inputs.end() != j; ++j) {
							HID & input(**j);
							if (input.query_input_fd() == static_cast<int>(e.ident)) 
								input.handle_input_events(realizer);
						}
					}
					break;
				case EVFILT_WRITE:
					if (0 <= static_cast<int>(e.ident)) {
						if (vt.query_input_fd() == static_cast<int>(e.ident))
							vt.FlushMessages();
					}
					break;
			}
		}

		if (vt.MessageAvailable()) {
			if (!vt.query_polling_for_write()) {
				append_event(ip, vt.query_input_fd(), EVFILT_WRITE, EV_ENABLE, 0, 0, 0);
				vt.set_polling_for_write(true);
			}
		} else {
			if (vt.query_polling_for_write()) {
				append_event(ip, vt.query_input_fd(), EVFILT_WRITE, EV_DISABLE, 0, 0, 0);
				vt.set_polling_for_write(false);
			}
		}

#if defined(__LINUX__) || defined(__linux__)
		if (kvt.query_dirty_LEDs()) {
			kvt.set_LEDs();
			kvt.clean_LEDs();
		}
#elif defined(__FreeBSD__) || defined (__DragonFly__)
		if (kvt.query_dirty_LEDs()) {
			kvt.set_LEDs();
			kvt.clean_LEDs();
		}
		if (atkbd.query_dirty_LEDs()) {
			atkbd.set_LEDs();
			atkbd.clean_LEDs();
		}
#endif
		for (HIDList::iterator j(inputs.begin()); inputs.end() != j; ++j) {
			HID & input(**j);
			if (input.query_dirty_LEDs()) {
				input.set_LEDs();
				input.clean_LEDs();
			}
		}
	}

	throw EXIT_SUCCESS;
}
