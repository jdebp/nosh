/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define __STDC_FORMAT_MACROS
#define _XOPEN_SOURCE_EXTENDED
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
#include <sys/event.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#if defined(__LINUX__) || defined(__linux__)
#include <linux/vt.h>
#include <linux/kd.h>
#include <linux/input.h>
#include <endian.h>
#else
#include <sys/consio.h>
#include <sys/endian.h>
#endif
#include <unistd.h>
#include <fcntl.h>
#include "utils.h"
#include "fdutils.h"
#include "popt.h"
#include "CharacterCell.h"
#include "InputMessage.h"
#include "FileDescriptorOwner.h"
#include "FramebufferIO.h"
#include "KeyboardIO.h"
#include "GraphicsInterface.h"
#include "vtfont.h"
#include "kbdmap.h"
#include "kbdmap_utils.h"
#include "Monospace16x16Font.h"
#include "CompositeFont.h"
#include "UnicodeClassification.h"
#include <term.h>

/* Signal handling **********************************************************
// **************************************************************************
*/

static sig_atomic_t window_resized(false), terminate_signalled(false), interrupt_signalled(false), hangup_signalled(false), usr1_signalled(false), usr2_signalled(false), update_needed(false);

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
int
pread(int fd, bsd_vtfont_header & header, off_t o)
{
	const int rc(pread(fd, &header, sizeof header, o));
	if (0 <= rc) {
		header.glyphs = be32toh(header.glyphs);
		for (unsigned i(0U); i < 4U; ++i) header.map_lengths[i] = be32toh(header.map_lengths[i]);
	}
	return rc;
}

static inline
int
pread(int fd, bsd_vtfont_map_entry & me, off_t o)
{
	const int rc(pread(fd, &me, sizeof me, o));
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
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, name, std::strerror(error));
		throw EXIT_FAILURE;
	}
	struct stat t;
	if (0 > fstat(font_fd.get(), &t)) {
bad_file:
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, name, std::strerror(error));
		throw EXIT_FAILURE;
	}

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
	} else
	if (CombinedFont::Font::UPRIGHT != slant) 
		return;

	unsigned vtfont_index; 
	switch (weight) {
		case CombinedFont::Font::MEDIUM:	vtfont_index = 0U; break;
		case CombinedFont::Font::BOLD:		vtfont_index = 2U; break;
		case CombinedFont::Font::LIGHT:
		case CombinedFont::Font::DEMIBOLD:
		case CombinedFont::Font::BLACK:
		default:
			return;
	}

	if (header.height <= 8U && header.width <= 8U) {
		if (CombinedFont::SmallFileFont * f = font.AddSmallFileFont(font_fd.release(), weight, slant, header.height, header.width)) {
			bsd_vtfont_map_entry me;
			const off_t start(f->GlyphOffset(header.glyphs) + sizeof me * skipped_entries(header, vtfont_index));
			for (unsigned c(0U); c < entries(header, vtfont_index); ++c) {
				if (0 > pread(f->get(), me, start + c * sizeof me)) goto bad_file;
				f->AddMapping(me.character, me.glyph, me.count + 1U);
			}
		}
	} else
	if (header.width > 8U || !has_right(header, vtfont_index)) {
		if (CombinedFont::LeftFileFont * f = font.AddLeftFileFont(font_fd.release(), weight, slant, header.height, header.width)) {
			bsd_vtfont_map_entry me;
			const off_t start(f->GlyphOffset(header.glyphs) + sizeof me * skipped_entries(header, vtfont_index));
			for (unsigned c(0U); c < entries(header, vtfont_index); ++c) {
				if (0 > pread(f->get(), me, start + c * sizeof me)) goto bad_file;
				f->AddMapping(me.character, me.glyph, me.count + 1U);
			}
		}
	} else
	{
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
	}
}

namespace {
struct FontSpec {
	std::string name;
	int weight, slant;
};

typedef std::list<FontSpec> FontSpecList;

struct fontspec_definition : public popt::compound_named_definition {
public:
	fontspec_definition(char s, const char * l, const char * a, const char * d, FontSpecList & f, int w, int i) : compound_named_definition(s, l, a, d), specs(f), weight(w), slant(i) {}
	virtual void action(popt::processor &, const char *);
	virtual ~fontspec_definition();
protected:
	FontSpecList & specs;
	int weight;
	int slant;
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

typedef kbdmap_entry KeyboardMap[KBDMAP_ROWS][KBDMAP_COLS];

static inline
void
LoadKeyMap (
	const char * prog,
	KeyboardMap & map,
	const char * name
) {
	FILE * const f(std::fopen(name, "r"));
	if (!f) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, name, std::strerror(error));
		throw EXIT_FAILURE;
	}
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
	std::fclose(f);
}

/* Character cell comparison ************************************************
// **************************************************************************
*/

static inline
bool
operator != (
	const CharacterCell::colour_type & a,
	const CharacterCell::colour_type & b
) {
	return a.alpha != b.alpha || a.red != b.red || a.green != b.green || a.blue != b.blue;
}

static inline
bool
operator != (
	const CharacterCell & a,
	const CharacterCell & b
) {
	return a.character != b.character || a.attributes != b.attributes || a.foreground != b.foreground || a.background != b.background;
}

/* Keyboard shift state *****************************************************
// **************************************************************************
*/

namespace {
class KeyboardModifierState
{
public:
	KeyboardModifierState();

#if !defined(__LINUX__) && !defined(__linux__)
	bool is_pressed(uint8_t code) const { return pressed[code]; }
#endif
	uint8_t modifiers() const;
	uint8_t nolevel_nogroup_noctrl_modifiers() const;
	std::size_t shiftable_index() const;
	std::size_t capsable_index() const;
	std::size_t numable_index() const;
	std::size_t funcable_index() const;
	void event(uint16_t, uint8_t, uint32_t);
	void unlatch() { latch_state = 0U; }
	bool num_LED() const { return num_lock(); }
	bool caps_LED() const { return caps_lock()||level2_lock(); }
	bool scroll_LED() const { return group2(); }

#if !defined(__LINUX__) && !defined(__linux__)
	void set_pressed(uint8_t code, bool v) { pressed[code] = v; }
#endif

protected:
	static uint32_t bit(unsigned i) { return 1U << i; }
#if !defined(__LINUX__) && !defined(__linux__)
	bool pressed[256];	///< used for determining auto-repeat keypresses
#endif
	uint32_t shift_state, latch_state, lock_state;

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
};
}

inline
KeyboardModifierState::KeyboardModifierState() : 
	shift_state(0U), 
	latch_state(0U), 
	lock_state(0U)
{
#if !defined(__LINUX__) && !defined(__linux__)
	for (unsigned i(0U); i < sizeof pressed/sizeof *pressed; ++i)
		pressed[i] = false;
#endif
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
	return (num_lock() ^ level2() ? 1U : 0U) + (control() ? 2U : 0) + (level3_lock() || level3() ? 4U : 0) + (group2() ? 8U : 0);
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
			if (v > 0U)
				latch_state |= bits;
			break;
		case KBDMAP_MODIFIER_CMD_LOCK:
			if (1U == v)
				lock_state ^= bits;
			break;
	}
	if (1U == v) {
		if (bits & (bit(KBDMAP_MODIFIER_1ST_LEVEL2)|bit(KBDMAP_MODIFIER_2ND_LEVEL2)))
			lock_state &= ~bit(KBDMAP_MODIFIER_LEVEL2);
	}
}

/* Window compositing and change buffering **********************************
// **************************************************************************
*/

namespace {
class Compositor
{
public:
	typedef unsigned short coordinate;

	Compositor(coordinate h, coordinate w);
	~Compositor();
	coordinate query_h() const { return h; }
	coordinate query_w() const { return w; }
	void repaint_new_to_cur();
	void poke(coordinate y, coordinate x, const CharacterCell & c);
	void move(coordinate y, coordinate x);
	bool is_marked(coordinate y, coordinate x);
	void set_cursor_state(int v);

protected:
	class DirtiableCell : public CharacterCell {
	public:
		DirtiableCell() : CharacterCell(), t(true) {}
		bool touched() const { return t; }
		void untouch() { t = false; }
		void touch() { t = true; }
		DirtiableCell & operator = ( const CharacterCell & c );
	protected:
		bool t;
	};

	coordinate cursor_y, cursor_x;
	int cursor_state;
	const coordinate h, w;
	std::vector<DirtiableCell> cur_cells;
	std::vector<CharacterCell> new_cells;

	DirtiableCell & cur_at(coordinate y, coordinate x) { return cur_cells[static_cast<std::size_t>(y) * w + x]; }
	CharacterCell & new_at(coordinate y, coordinate x) { return new_cells[static_cast<std::size_t>(y) * w + x]; }
};
}

Compositor::Compositor(coordinate init_h, coordinate init_w) :
	cursor_y(0U),
	cursor_x(0U),
	cursor_state(-1),
	h(init_h),
	w(init_w),
	cur_cells(static_cast<std::size_t>(h) * w),
	new_cells(static_cast<std::size_t>(h) * w)
{
}

Compositor::DirtiableCell & 
Compositor::DirtiableCell::operator = ( const CharacterCell & c ) 
{ 
	if (c != *this) {
		CharacterCell::operator =(c); 
		t = true; 
	}
	return *this; 
}

Compositor::~Compositor()
{
}

inline
void
Compositor::repaint_new_to_cur()
{
	for (unsigned row(0); row < h; ++row)
		for (unsigned col(0); col < w; ++col)
			cur_at(row, col) = new_at(row, col);
}

inline
void
Compositor::poke(coordinate y, coordinate x, const CharacterCell & c)
{
	if (y < h && x < w) new_at(y, x) = c;
}

void 
Compositor::move(coordinate y, coordinate x) 
{
	if (cursor_y != y || cursor_x != x) {
		cur_at(cursor_y, cursor_x).touch();
		cursor_y = y;
		cursor_x = x;
		cur_at(cursor_y, cursor_x).touch();
	}
}

void 
Compositor::set_cursor_state(int v) 
{ 
	if (cursor_state != v) {
		cursor_state = v; 
		cur_at(cursor_y, cursor_x).touch();
	}
}

/// This is a fairly minimal function for testing whether a particular cell position is within the current cursor, so that it can be displayed marked.
/// \todo TODO: When we gain mark/copy functionality, the cursor will be the entire marked region rather than just one cell.
inline
bool
Compositor::is_marked(coordinate y, coordinate x)
{
	return cursor_y == y && cursor_x == x;
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

/* Realizing onto real devices **********************************************
// **************************************************************************
*/

namespace {
class Realizer :
	public Compositor
{
public:
	~Realizer();
	Realizer(coordinate y, coordinate x, bool, GraphicsInterface & g, Monospace16x16Font & mf);

	void paint_changed_cells_onto_framebuffer() { do_paint(false); }
	void paint_all_cells_onto_framebuffer() { do_paint(true); }

protected:
	typedef GraphicsInterface::GlyphBitmapHandle GlyphBitmapHandle;
	struct GlyphCacheEntry {
		GlyphCacheEntry(GlyphBitmapHandle ha, uint32_t c, CharacterCell::attribute_type a) : handle(ha), character(c), attributes(a) {}
		GraphicsInterface::GlyphBitmapHandle handle;
		uint32_t character;
		CharacterCell::attribute_type attributes;
	};

	typedef std::list<GlyphCacheEntry> GlyphCache;
	enum { MAX_CACHED_GLYPHS = 256U };
	enum { CHARACTER_PIXEL_WIDTH = 16LU, CHARACTER_PIXEL_HEIGHT = 16LU };

	const bool faint_as_colour;
	GraphicsInterface & gdi;
	Monospace16x16Font & font;
	GlyphCache glyph_cache;		///< a recently-used cache of handles to 2-colour bitmaps

	void do_paint(bool changed);

	GlyphBitmapHandle GetGlyphBitmap(uint32_t character, CharacterCell::attribute_type attributes);
	GlyphBitmapHandle GetCachedGlyphBitmap(uint32_t character, CharacterCell::attribute_type attributes);
	GlyphBitmapHandle MakeFontGlyphBitmap(uint32_t character, CharacterCell::attribute_type attributes);
	void AddCachedGlyphBitmap(uint32_t character, CharacterCell::attribute_type attributes, GlyphBitmapHandle handle);
};
}

Realizer::Realizer(
	coordinate y, 
	coordinate x, 
	bool fc,
	GraphicsInterface & g,
	Monospace16x16Font & mf
) : 
	Compositor(y / CHARACTER_PIXEL_HEIGHT, x / CHARACTER_PIXEL_WIDTH),
	faint_as_colour(fc),
	gdi(g),
	font(mf)
{
}

Realizer::~Realizer()
{
}

inline
Realizer::GlyphBitmapHandle 
Realizer::MakeFontGlyphBitmap(uint32_t character, CharacterCell::attribute_type attributes)
{
	GlyphBitmapHandle handle(gdi.MakeGlyphBitmap());
	
	if (const uint16_t * const s = font.ReadGlyph(character, CharacterCell::BOLD & attributes, CharacterCell::FAINT & attributes, CharacterCell::ITALIC & attributes)) {
		for (unsigned row(0U); row < 16U; ++row) handle->Plot(row, s[row]);
	} else
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

	if (attributes & CharacterCell::UNDERLINE) {
		handle->Plot(15, 0xFFFF);
	}
	if (attributes & CharacterCell::STRIKETHROUGH) {
		handle->Plot(7, 0xFFFF);
		handle->Plot(8, 0xFFFF);
	}
	if (attributes & CharacterCell::INVERSE) {
		for (unsigned row(0U); row < 16U; ++row) handle->Plot(row, ~handle->Row(row));
	}

	return handle;
}

inline
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
	return 0;
}

inline
void 
Realizer::AddCachedGlyphBitmap(uint32_t character, CharacterCell::attribute_type attributes, GlyphBitmapHandle handle)
{
	while (glyph_cache.size() > MAX_CACHED_GLYPHS) {
		const GlyphCacheEntry e(glyph_cache.back());
		glyph_cache.pop_back();
		gdi.DeleteGlyphBitmap(e.handle);
	}
	glyph_cache.push_back(GlyphCacheEntry(handle, character, attributes));
}

Realizer::GlyphBitmapHandle 
Realizer::GetGlyphBitmap(uint32_t character, CharacterCell::attribute_type attributes)
{
	GlyphBitmapHandle handle(GetCachedGlyphBitmap(character, attributes));
	if (!handle) {
		handle = MakeFontGlyphBitmap(character, attributes);
		AddCachedGlyphBitmap(character, attributes, handle);
	}
	return handle;
}

inline
void
Realizer::do_paint(bool force)
{
	const GraphicsInterface::ScreenBitmapHandle screen(gdi.GetScreenBitmap());

	for (unsigned row(0); row < h; ++row) {
		for (unsigned col(0); col < w; ++col) {
			DirtiableCell & c(cur_at(row, col));
			if (!force && !c.touched()) continue;
			// Being invisible is not an alteration to the glyph (which would waste a lot of glyph cache entries) but an alteration to the foreground colour.
			CharacterCell::attribute_type font_attributes(c.attributes & ~CharacterCell::INVISIBLE);
			if (faint_as_colour)
				font_attributes &= ~CharacterCell::FAINT;
			const GlyphBitmapHandle handle(GetGlyphBitmap(c.character, font_attributes));
			CharacterCell::colour_type fg(c.foreground), bg(c.background);
			if (faint_as_colour && (CharacterCell::FAINT & c.attributes)) {
				dim(fg);
				dim(bg);
			}
			if (CharacterCell::INVISIBLE & c.attributes)
				fg = bg;	// Must be done after dimming and brightening.
			if (is_marked(row, col) && cursor_state > 0) {
				invert(fg);
				invert(bg);
			}
			gdi.BitBLT(screen, handle, row * CHARACTER_PIXEL_HEIGHT, col * CHARACTER_PIXEL_WIDTH, fg, bg);
			c.untouch();
		}
	}
}

/* A virtual terminal back-end **********************************************
// **************************************************************************
*/

namespace {
class VirtualTerminal 
{
public:
	typedef unsigned short coordinate;
	VirtualTerminal(bool display_only, const char * prog, const char * dirname, int dir_fd);
	~VirtualTerminal();

	int query_buffer_fd() const { return buffer_fd; }
	coordinate query_h() const { return h; }
	coordinate query_w() const { return w; }
	coordinate query_screen_y() const { return screen_y; }
	coordinate query_screen_x() const { return screen_x; }
	coordinate query_visible_y() const { return visible_y; }
	coordinate query_visible_x() const { return visible_x; }
	coordinate query_visible_h() const { return visible_h; }
	coordinate query_visible_w() const { return visible_w; }
	coordinate query_cursor_y() const { return cursor_y; }
	coordinate query_cursor_x() const { return cursor_x; }
	int query_cursor_state() const { return cursor_state; }
	void reload();
	void set_position(coordinate y, coordinate x);
	void set_visible_area(coordinate h, coordinate w);
	CharacterCell & at(coordinate y, coordinate x) { return cells[static_cast<std::size_t>(y) * w + x]; }
	void WriteInputMessage(uint32_t);
	void WriteInputUCS24(uint32_t);
	void ClearDeadKeys() { dead_keys.clear(); }

protected:
	typedef std::vector<uint32_t> DeadKeysList;
	VirtualTerminal(const VirtualTerminal & c);
	enum { CELL_LENGTH = 16U, HEADER_LENGTH = 16U };

	void move(coordinate y, coordinate x) { cursor_y = y; cursor_x = x; }
	void resize(coordinate, coordinate);
	void keep_visible_area_in_buffer();
	void keep_visible_area_around_cursor();

	FileDescriptorOwner dir_fd;
	const int buffer_fd, input_fd;
	FILE * const buffer_file;
	unsigned short cursor_y, cursor_x, visible_y, visible_x, visible_h, visible_w, h, w, screen_y, screen_x;
	int cursor_state;
	std::vector<CharacterCell> cells;
	DeadKeysList dead_keys;
};
}

VirtualTerminal::VirtualTerminal(
	bool display_only,
	const char * prog, 
	const char * dirname, 
	int d
) :
	dir_fd(d),
	buffer_fd(open_read_at(dir_fd.get(), "display")),
	input_fd(display_only ? -1 : open_writeexisting_at(dir_fd.get(), "input")),
	buffer_file(buffer_fd < 0 ? 0 : fdopen(buffer_fd, "r")),
	cursor_y(0U),
	cursor_x(0U),
	visible_y(0U),
	visible_x(0U),
	visible_h(0U),
	visible_w(0U),
	h(0U),
	w(0U),
	screen_y(0U),
	screen_x(0U),
	cursor_state(-1),
	cells()
{
	if (buffer_fd < 0) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "display", std::strerror(error));
		throw EXIT_FAILURE;
	}
	if (!display_only && input_fd < 0) {
		const int error(errno);
		close(buffer_fd);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "input", std::strerror(error));
		throw EXIT_FAILURE;
	}
	if (!buffer_file) {
		const int error(errno);
		close(buffer_fd);
		if (-1 != input_fd) close(input_fd);
		std::fprintf(stderr, "%s: FATAL: %s/%s: %s\n", prog, dirname, "display", std::strerror(error));
		throw EXIT_FAILURE;
	}
}

VirtualTerminal::~VirtualTerminal()
{
	std::fclose(buffer_file);
	if (-1 != input_fd) close(input_fd);
}

void 
VirtualTerminal::resize(
	coordinate new_h, 
	coordinate new_w
) {
	if (h == new_h && w == new_w) return;
	h = new_h;
	w = new_w;
	const std::size_t s(static_cast<std::size_t>(h) * w);
	if (cells.size() == s) return;
	cells.resize(s);
}

inline
void 
VirtualTerminal::set_position(
	coordinate new_y, 
	coordinate new_x
) {
	screen_y = new_y;
	screen_x = new_x;
}

inline
void 
VirtualTerminal::keep_visible_area_in_buffer()
{
	if (visible_y + visible_h > h) visible_y = h - visible_h;
	if (visible_x + visible_w > w) visible_x = w - visible_w;
}

inline
void 
VirtualTerminal::keep_visible_area_around_cursor()
{
	// When programs repaint the screen the cursor is instantaneously all over the place, leading to the window scrolling all over the shop.
	// But some programs, like vim, make the cursor invisible during the repaint in order to reduce cursor flicker.
	// We take advantage of this by only scrolling the screen to include the cursor position if the cursor is actually visible.
	if (cursor_state > 0) {
		// The window includes the cursor position.
		if (visible_y > cursor_y) visible_y = cursor_y; else if (visible_y + visible_h <= cursor_y) visible_y = cursor_y - visible_h + 1;
		if (visible_x > cursor_x) visible_x = cursor_x; else if (visible_x + visible_w <= cursor_x) visible_x = cursor_x - visible_w + 1;
	}
}

void 
VirtualTerminal::set_visible_area(
	coordinate new_h, 
	coordinate new_w
) {
	if (new_h > h) { visible_h = h; } else { visible_h = new_h; }
	if (new_w > w) { visible_w = w; } else { visible_w = new_w; }
	keep_visible_area_in_buffer();
	keep_visible_area_around_cursor();
}

/// \brief Pull the display buffer from file into the memory buffer, but don't output anything.
inline
void
VirtualTerminal::reload () 
{
	std::fseek(buffer_file, 4, SEEK_SET);
	uint16_t header1[4] = { 0, 0, 0, 0 };
	std::fread(header1, sizeof header1, 1U, buffer_file);
	uint8_t header2[4] = { 0, 0, 0, 0 };
	std::fread(header2, sizeof header2, 1U, buffer_file);
	std::fseek(buffer_file, HEADER_LENGTH, SEEK_SET);
	resize(header1[1], header1[0]);
	move(header1[3], header1[2]);
	const unsigned ctype(header2[0]), cattr(header2[1]);
	if (CursorSprite::VISIBLE != (cattr & CursorSprite::VISIBLE))
		cursor_state = 0;
	else if (CursorSprite::UNDERLINE == ctype)
		cursor_state = 1;
	else
		cursor_state = 2;

	for (unsigned row(0); row < h; ++row) {
		for (unsigned col(0); col < w; ++col) {
			unsigned char b[CELL_LENGTH] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
			std::fread(b, sizeof b, 1U, buffer_file);
			uint32_t wc;
			std::memcpy(&wc, &b[8], 4);
			const CharacterCell::attribute_type a(b[12]);
			const CharacterCell::colour_type fg(b[0], b[1], b[2], b[3]);
			const CharacterCell::colour_type bg(b[4], b[5], b[6], b[7]);
			CharacterCell cc(wc, a, fg, bg);
			at(row, col) = cc;
		}
	}
}

void 
VirtualTerminal::WriteInputMessage(uint32_t m)
{
	write(input_fd, &m, sizeof m);
}

static const
struct Combination {
	uint32_t c1, c2, r;
	// We will be using a binary search, so we need this operator.
	bool operator < ( const Combination & b ) const { return c1 < b.c1 || ( c1 == b.c1 && c2 < b.c2 ); }
} 
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
VirtualTerminal::WriteInputUCS24(uint32_t c)
{
	if (UnicodeCategorization::IsMarkEnclosing(c)
	||  UnicodeCategorization::IsMarkNonSpacing(c)
	) {
		// Per ISO 9995-3 there are certain pairs of dead keys that make other dead keys.
		// Per DIN 2137, this happens on the fly as the keys are typed, so we only need to test combining against the preceding key.
		if (dead_keys.empty() || !combine_dead_keys(dead_keys.back(), c))
			dead_keys.push_back(c);
	} else
	if (0x200C == c) {
		// Per DIN 2137, Zero-Width Non-Joiner means emit the dead-key sequence as-is.
		// We must not sort into Unicode combination class order.
		// ZWNJ is essentially a pass-through mechanism for combiners.
		for (DeadKeysList::iterator i(dead_keys.begin()); i != dead_keys.end(); i = dead_keys.erase(i))
			WriteInputMessage(MessageForUCS24(*i));
	} else
	if (!dead_keys.empty()) {
		// Per ISO 9995-3 there are certain C+B=R combinations that apply in addition to the Unicode composition rules.
		// These can be done first, because they never start with a precomposed character.
		for (DeadKeysList::iterator i(dead_keys.begin()); i != dead_keys.end(); )
			if (combine_peculiar_non_combiners(*i, c))
				i = dead_keys.erase(i);
			else
				++i;
		// The standard thing to do at this point is to renormalize into Unicode Normalization Form C (NFC).
		// As explained in the manual at length (q.v.), we don't do full Unicode Normalization.
		std::stable_sort(dead_keys.begin(), dead_keys.end(), lower_combining_class);
		for (DeadKeysList::iterator i(dead_keys.begin()); i != dead_keys.end(); )
			if (combine_unicode(*i, c))
				i = dead_keys.erase(i);
			else
				++i;
		// If we have any dead keys left, we emit them standalone, before the composed key (i.e. in original typing order, as best we can).
		// We don't send the raw combiners, as in the ZWNJ case.
		// Instead, we make use of the fact that ISO 9995-3 defines rules for combining with the space character to produce a composed spacing character.
		for (DeadKeysList::iterator i(dead_keys.begin()); i != dead_keys.end(); i = dead_keys.erase(i)) {
			uint32_t s(' ');
			if (combine_peculiar_non_combiners(*i, s))
				WriteInputMessage(MessageForUCS24(s));
			else
				WriteInputMessage(MessageForUCS24(*i));
		}
		// This is the final composed key.
		WriteInputMessage(MessageForUCS24(c));
	} else
	{
		WriteInputMessage(MessageForUCS24(c));
	}
}

/* Sharing the framebuffer with kernel virtual terminals ********************
// **************************************************************************
*/

namespace {
class KernelVTSubsystemLockout :
	public FileDescriptorOwner
{
public:
	KernelVTSubsystemLockout(int d, int rs, int as);
	~KernelVTSubsystemLockout();
	static int open(const char *);
protected:
	struct vt_mode vtmode;
	long kdmode;
#if defined(__LINUX__) || defined(__linux__)
#if defined(KDGKBMUTE) && defined(KDSKBMUTE)
	// This is the newest mechanism, that doesn't react badly with other things that independently change the KVT mode.
	int kbmute;
#elif defined(K_OFF)
	// This is an older mechanism, that is at least better than setting raw mode.
	long kbmode;
#else
	// Fall back to just setting raw mode when turning the KVT's keyboard input queue off.
	long kbmode;
#endif
#endif
};
}

KernelVTSubsystemLockout::KernelVTSubsystemLockout(int d, int release, int acquire) : 
	FileDescriptorOwner(d),
	vtmode(),
	kdmode(KD_TEXT)
#if defined(__LINUX__) || defined(__linux__)
	,
#if defined(KDGKBMUTE) && defined(KDSKBMUTE)
	kbmute(1)
#elif defined(K_OFF)
	kbmode(K_XLATE)
#else
	kbmode(K_XLATE)
#endif
#endif
{
	if (-1 != fd) {
		ioctl(fd, VT_GETMODE, &vtmode);
		struct vt_mode m = { VT_PROCESS, 0, static_cast<short>(release), static_cast<char>(acquire), 0 };
		ioctl(fd, VT_SETMODE, &m);
		ioctl(fd, KDGETMODE, &kdmode);
		ioctl(fd, KDSETMODE, KD_GRAPHICS);
#if defined(__LINUX__) || defined(__linux__)
#if defined(KDGKBMUTE) && defined(KDSKBMUTE)
		ioctl(fd, KDGKBMUTE, &kbmute);
		ioctl(fd, KDSKBMUTE, 0);
#elif defined(K_OFF)
		ioctl(fd, KDGKBMODE, &kbmode);
		ioctl(fd, KDSKBMODE, K_OFF);
#else
		ioctl(fd, KDGKBMODE, &kbmode);
		ioctl(fd, KDSKBMODE, K_RAW);
#endif
#endif
	}
}

KernelVTSubsystemLockout::~KernelVTSubsystemLockout()
{
	if (-1 != fd) {
		ioctl(fd, VT_SETMODE, &vtmode);
		ioctl(fd, KDSETMODE, kdmode);
#if defined(__LINUX__) || defined(__linux__)
#if defined(KDGKBMUTE) && defined(KDSKBMUTE)
		ioctl(fd, KDSKBMUTE, kbmute);
#elif defined(K_OFF)
		ioctl(fd, KDSKBMODE, kbmode);
#else
		ioctl(fd, KDSKBMODE, kbmode);
#endif
#endif
	}
}

int 
KernelVTSubsystemLockout::open(const char * dev)
{
	return open_readwriteexisting_at(AT_FDCWD, dev);
}

/* Support routines  ********************************************************
// **************************************************************************
*/

static const CharacterCell::colour_type overscan_fg(0,255,255,255), overscan_bg(0,0,0,0);
static const CharacterCell overscan_blank(' ', 0U, overscan_fg, overscan_bg);

static inline
void
paint_backdrop (
	Compositor & r
) {
	for (unsigned short row(0U); row < r.query_h(); ++row)
		for (unsigned short col(0U); col < r.query_w(); ++col)
			r.poke(row, col, overscan_blank);
}

/// \brief Clip and position the visible portion of the terminal's display buffer.
static inline
void
position (
	Compositor & r, 
	VirtualTerminal & vt
) {
	const unsigned short screen_y(vt.query_h() < r.query_h() ? r.query_h() - vt.query_h() : 0);	// Glue the terminal window to the bottom edge of the realizer screen buffer.
	const unsigned short screen_x(/*vt.query_visible_w() < r.query_w() ? r.query_w() - vt.query_visible_w() :*/ 0);	// Glue the terminal window to the left-hand edge of the realizer screen buffer.
	vt.set_position(screen_y, screen_x);
	vt.set_visible_area(r.query_h() - screen_y, r.query_w() - screen_x);
}

/// \brief Render the terminal's display buffer onto the realizer's screen buffer.
static inline
void
compose (
	Compositor & r, 
	VirtualTerminal & vt
) {
	for (unsigned short row(0U); row < vt.query_visible_h(); ++row)
		for (unsigned short col(0U); col < vt.query_visible_w(); ++col)
			r.poke(row + vt.query_screen_y(), col + vt.query_screen_x(), vt.at(vt.query_visible_y() + row, vt.query_visible_x() + col));
}

static inline
void
set_cursor_pos (
	Compositor & r, 
	VirtualTerminal & vt
) {
	r.move(vt.query_cursor_y() - vt.query_visible_y() + vt.query_screen_y(), vt.query_cursor_x() - vt.query_visible_x() + vt.query_screen_x());
}

/// Actions can be simple transmissions of an input message, or complex procedures with the input method.
static inline
void
Enact (
	const uint32_t cmd,
#if !defined(__LINUX__) && !defined(__linux__)
	KeyboardIO & event_fd,
#else
	FileDescriptorOwner & event_fd,
#endif
	KeyboardModifierState & r,
	VirtualTerminal & vt,
	uint32_t v
) {
	switch (cmd & KBDMAP_ACTION_MASK) {
		default:	break;
		case KBDMAP_ACTION_UCS24:
		{
			if (v < 1U) break;
			const uint32_t c(cmd & 0x00FFFFFF);
			vt.WriteInputUCS24(c);
			r.unlatch();
#if !defined(__LINUX__) && !defined(__linux__)
			event_fd.set_LEDs(r.caps_LED(), r.num_LED(), r.scroll_LED());
#endif
			break;
		}
		case KBDMAP_ACTION_MODIFIER:
		{
			const uint32_t k((cmd & 0x00FFFF00) >> 8U);
			const uint32_t c(cmd & 0x000000FF);
			r.event(k, c, v);
#if !defined(__LINUX__) && !defined(__linux__)
			event_fd.set_LEDs(r.caps_LED(), r.num_LED(), r.scroll_LED());
#endif
			break;
		}
		case 0x06000000:
		{
			break;
		}
		case KBDMAP_ACTION_SCREEN:
		{
			if (v < 1U) break;
			const uint32_t s((cmd & 0x00FFFF00) >> 8U);
			vt.ClearDeadKeys();
			vt.WriteInputMessage(MessageForSession(s, r.modifiers()));
			r.unlatch();
#if !defined(__LINUX__) && !defined(__linux__)
			event_fd.set_LEDs(r.caps_LED(), r.num_LED(), r.scroll_LED());
#endif
			break;
		}
		case KBDMAP_ACTION_SYSTEM:
		{
			if (v < 1U) break;
			const uint32_t k((cmd & 0x00FFFF00) >> 8U);
			vt.ClearDeadKeys();
			vt.WriteInputMessage(MessageForSystemKey(k, r.modifiers()));
			r.unlatch();
#if !defined(__LINUX__) && !defined(__linux__)
			event_fd.set_LEDs(r.caps_LED(), r.num_LED(), r.scroll_LED());
#endif
			break;
		}
		case KBDMAP_ACTION_CONSUMER:
		{
			if (v < 1U) break;
			const uint32_t k((cmd & 0x00FFFF00) >> 8U);
			vt.ClearDeadKeys();
			vt.WriteInputMessage(MessageForConsumerKey(k, r.modifiers()));
			r.unlatch();
#if !defined(__LINUX__) && !defined(__linux__)
			event_fd.set_LEDs(r.caps_LED(), r.num_LED(), r.scroll_LED());
#endif
			break;
		}
		case KBDMAP_ACTION_EXTENDED:
		{
			if (v < 1U) break;
			const uint32_t k((cmd & 0x00FFFF00) >> 8U);
			vt.ClearDeadKeys();
			vt.WriteInputMessage(MessageForExtendedKey(k, r.modifiers()));
			r.unlatch();
#if !defined(__LINUX__) && !defined(__linux__)
			event_fd.set_LEDs(r.caps_LED(), r.num_LED(), r.scroll_LED());
#endif
			break;
		}
		case KBDMAP_ACTION_FUNCTION:
		{
			if (v < 1U) break;
			const uint32_t k((cmd & 0x00FFFF00) >> 8U);
			vt.ClearDeadKeys();
			vt.WriteInputMessage(MessageForFunctionKey(k, r.modifiers()));
			r.unlatch();
#if !defined(__LINUX__) && !defined(__linux__)
			event_fd.set_LEDs(r.caps_LED(), r.num_LED(), r.scroll_LED());
#endif
			break;
		}
		case KBDMAP_ACTION_FUNCTION1:
		{
			if (v < 1U) break;
			const uint32_t k((cmd & 0x00FFFF00) >> 8U);
			vt.ClearDeadKeys();
			vt.WriteInputMessage(MessageForFunctionKey(k, r.nolevel_nogroup_noctrl_modifiers()));
			r.unlatch();
#if !defined(__LINUX__) && !defined(__linux__)
			event_fd.set_LEDs(r.caps_LED(), r.num_LED(), r.scroll_LED());
#endif
			break;
		}
	}
}

static inline
std::size_t 
query_parameter (
	const uint32_t cmd,
	const KeyboardModifierState & r
) {
	switch (cmd) {
		default:	return -1U;
		case 'p':	return 0U;
		case 'n':	return r.numable_index();
		case 'c':	return r.capsable_index();
		case 's':	return r.shiftable_index();
		case 'f':	return r.funcable_index();
	}
}

static inline
void
handle_input_event(
#if !defined(__LINUX__) && !defined(__linux__)
	KeyboardIO & event_fd,
#else
	FileDescriptorOwner & event_fd,
#endif
	KeyboardModifierState & r, 
	const KeyboardMap & keymap,
	VirtualTerminal & vt
) {
#if defined(__LINUX__) || defined(__linux__)
	input_event b[16];
#else
	unsigned char b[16];
#endif
	const int n(read(event_fd.get(), b, sizeof b));
	if (0 > n) return;
	for (unsigned i(0); i * sizeof *b < static_cast<unsigned>(n); ++i) {
#if defined(__LINUX__) || defined(__linux__)
		const input_event & e(b[i]);
		if (EV_KEY != e.type) continue;
		const uint16_t code(e.code);
		const uint32_t value(e.value);
#else
		uint16_t code(b[i]);
		const bool bit7(code & 0x80);
		code &= 0x7F;
		const unsigned value(bit7 ? 0U : r.is_pressed(code) ? 2U : 1U);
		r.set_pressed(code, !bit7);
#endif
		const uint16_t index(keycode_to_keymap_index(code));
		if (0xFFFF == index) continue;
		const uint8_t row(index >> 8U);
		if (row >= KBDMAP_ROWS) continue;
		const uint8_t col(index & 0xFF);
		if (col >= KBDMAP_COLS) continue;
		const kbdmap_entry & action(keymap[row][col]);
		const std::size_t o(query_parameter(action.cmd, r));
		if (o >= sizeof action.p/sizeof *action.p) continue;
		Enact(action.p[o], event_fd, r, vt, value);
	}
}

/* Main function ************************************************************
// **************************************************************************
*/

void
console_fb_realizer ( 
	const char * & /*next_prog*/,
	std::vector<const char *> & args
) {
	const char * prog(basename_of(args[0]));
	const char * kernel_vt(0);
	bool display_only(false);
	bool bold_as_colour(false);
	FontSpecList fonts;

	try {
		popt::bool_definition display_only_option('\0', "display-only", "Only render the display; do not send input.", display_only);
		popt::bool_definition bold_as_colour_option('\0', "bold-as-colour", "Forcibly render boldface as a colour brightness change.", bold_as_colour);
		popt::string_definition kernel_vt_option('\0', "kernel-vt", "device", "Use the kernel FB sharing protocol via this device.", kernel_vt);
		fontspec_definition vtfont_option('\0', "vtfont", "filename", "Use this font as a vt font.", fonts, -1, -1);
		fontspec_definition font_medium_r_option('\0', "font-medium-r", "filename", "Use this font as a medium-upright font.", fonts, CombinedFont::Font::MEDIUM, CombinedFont::Font::UPRIGHT);
		fontspec_definition font_medium_o_option('\0', "font-medium-o", "filename", "Use this font as a medium-oblique font.", fonts, CombinedFont::Font::MEDIUM, CombinedFont::Font::OBLIQUE);
		fontspec_definition font_medium_i_option('\0', "font-medium-i", "filename", "Use this font as a medium-italic font.", fonts, CombinedFont::Font::MEDIUM, CombinedFont::Font::ITALIC);
		fontspec_definition font_bold_r_option('\0', "font-bold-r", "filename", "Use this font as a bold-upright font.", fonts, CombinedFont::Font::BOLD, CombinedFont::Font::UPRIGHT);
		fontspec_definition font_bold_o_option('\0', "font-bold-o", "filename", "Use this font as a bold-oblique font.", fonts, CombinedFont::Font::BOLD, CombinedFont::Font::OBLIQUE);
		fontspec_definition font_bold_i_option('\0', "font-bold-i", "filename", "Use this font as a bold-italic font.", fonts, CombinedFont::Font::BOLD, CombinedFont::Font::ITALIC);
		popt::definition * top_table[] = {
			&display_only_option,
			&bold_as_colour_option,
			&kernel_vt_option,
			&vtfont_option,
			&font_medium_r_option,
			&font_medium_o_option,
			&font_medium_i_option,
			&font_bold_r_option,
			&font_bold_o_option,
			&font_bold_i_option,
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "framebuffer event-device kbd-map virtual-terminal");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, e.arg, e.msg);
		throw static_cast<int>(EXIT_USAGE);
	}

	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing framebuffer device file name.");
		throw EXIT_FAILURE;
	}
	const char * fb_filename(args.front());
	args.erase(args.begin());
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing event device file name.");
		throw EXIT_FAILURE;
	}
	const char * event_filename(args.front());
	args.erase(args.begin());
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing virtual terminal directory name.");
		throw EXIT_FAILURE;
	}
	const char * keymap_filename(args.front());
	args.erase(args.begin());
	if (args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Missing keyboard map file name.");
		throw EXIT_FAILURE;
	}
	const char * vt_dirname(args.front());
	args.erase(args.begin());
	if (!args.empty()) {
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, args.front(), "Unexpected argument.");
		throw static_cast<int>(EXIT_USAGE);
	}

	CombinedFont font;

	for (FontSpecList::const_iterator n(fonts.begin()); fonts.end() != n; ++n) {
		for (CombinedFont::Font::Weight weight(static_cast<CombinedFont::Font::Weight>(0)); weight < CombinedFont::Font::NUM_WEIGHTS; weight = static_cast<CombinedFont::Font::Weight>(weight + 1)) {
			for (CombinedFont::Font::Slant slant(static_cast<CombinedFont::Font::Slant>(0)); slant < CombinedFont::Font::NUM_SLANTS; slant = static_cast<CombinedFont::Font::Slant>(slant + 1)) {
				if (n->weight != -1 && n->weight != weight) continue;
				if (n->slant != -1 && n->slant != slant) continue;
				LoadFont(prog, font, weight, slant, n->name.c_str());
			}
		}
	}

	KeyboardMap keymap;

	LoadKeyMap(prog, keymap, keymap_filename);

	FileDescriptorOwner vt_dir_fd(open_dir_at(AT_FDCWD, vt_dirname));
	if (0 > vt_dir_fd.get()) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, vt_dirname, std::strerror(error));
		throw EXIT_FAILURE;
	}
	VirtualTerminal vt(display_only, prog, vt_dirname, vt_dir_fd.release());

	FramebufferIO fb(open_readwriteexisting_at(AT_FDCWD, fb_filename));
	if (fb.get() < 0) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, fb_filename, std::strerror(error));
		throw EXIT_FAILURE;
	}
#if !defined(__LINUX__) && !defined(__linux__)
	KeyboardIO event(open_read_at(AT_FDCWD, event_filename));
#else
	FileDescriptorOwner event(open_read_at(AT_FDCWD, event_filename));
#endif
	if (event.get() < 0) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, event_filename, std::strerror(error));
		throw EXIT_FAILURE;
	}

	struct sigaction sa;
	sa.sa_flags=0;
	sigemptyset(&sa.sa_mask);
	sa.sa_handler=handle_signal;
	sigaction(SIGWINCH,&sa,NULL);
#if !defined(__LINUX__) && !defined(__linux__)
	// We only need to set handlers for those signals that would otherwise directly terminate the process.
	sa.sa_handler=SIG_IGN;
	sigaction(SIGTERM,&sa,NULL);
	sigaction(SIGINT,&sa,NULL);
	sigaction(SIGHUP,&sa,NULL);
	sigaction(SIGPIPE,&sa,NULL);
#endif

	const int queue(kqueue());
	if (0 > queue) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kqueue", std::strerror(error));
		throw EXIT_FAILURE;
	}

	struct kevent p[16];
	{
		std::size_t index(0U);
		EV_SET(&p[index++], vt.query_buffer_fd(), EVFILT_VNODE, EV_ADD|EV_CLEAR, NOTE_WRITE, 0, 0);
		if (!display_only)
			EV_SET(&p[index++], event.get(), EVFILT_READ, EV_ADD, 0, 0, 0);
		EV_SET(&p[index++], SIGWINCH, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		EV_SET(&p[index++], SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		EV_SET(&p[index++], SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		EV_SET(&p[index++], SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		EV_SET(&p[index++], SIGPIPE, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		EV_SET(&p[index++], SIGUSR1, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		EV_SET(&p[index++], SIGUSR2, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
		if (0 > kevent(queue, p, index, 0, 0, 0)) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "kevent", std::strerror(error));
			throw EXIT_FAILURE;
		}
	}

	std::auto_ptr<KernelVTSubsystemLockout> kvt(0);
	if (kernel_vt) {
		FileDescriptorOwner fd(KernelVTSubsystemLockout::open(kernel_vt));
		if (0 > fd.get()) {
			const int error(errno);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, kernel_vt, std::strerror(error));
			throw EXIT_FAILURE;
		}
		kvt.reset(new KernelVTSubsystemLockout(fd.release(), SIGUSR1, SIGUSR2));
	}

	fb.save_and_set_graphics_mode(prog, fb_filename);
#if !defined(__LINUX__) && !defined(__linux__)
	event.save_and_set_code_mode();
#endif

	void * const base(mmap(0, fb.query_size(), PROT_READ|PROT_WRITE, MAP_SHARED, fb.get(), 0));
	if (MAP_FAILED == base) {
		const int error(errno);
		std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, fb_filename, std::strerror(error));
		throw EXIT_FAILURE;
	}

	KeyboardModifierState kbd;
	GraphicsInterface gdi(base, fb.query_size(), fb.query_yres(), fb.query_xres(), fb.query_stride(), fb.query_depth());
	Realizer realizer(fb.query_yres(), fb.query_xres(), !font.has_faint(), gdi, font);

	update_needed = true;
	usr2_signalled = true;

	bool active(true);

	vt.reload();
	while (true) {
		if (terminate_signalled||interrupt_signalled||hangup_signalled) 
			break;
		if (usr1_signalled) 
			active = false;
		if (usr2_signalled) {
			if (!active) 
				realizer.paint_all_cells_onto_framebuffer();
			active = true;
		}
		if (update_needed) {
			update_needed = false;
			paint_backdrop(realizer);
			position(realizer, vt);
			compose(realizer, vt);
			set_cursor_pos(realizer, vt);
			realizer.set_cursor_state(vt.query_cursor_state());
			realizer.repaint_new_to_cur();
			if (active)
				realizer.paint_changed_cells_onto_framebuffer();
		}

		const int rc(kevent(queue, p, 0, p, sizeof p/sizeof *p, 0));

		if (0 > rc) {
			const int error(errno);
			if (EINTR == error) continue;
#if defined(__LINUX__) || defined(__linux__)
			if (EINVAL == error) continue;	// This works around a Linux bug when an inotify queue overflows.
#endif
			fb.restore();
			kvt.reset(0);
			std::fprintf(stderr, "%s: FATAL: %s: %s\n", prog, "poll", std::strerror(error));
			throw EXIT_FAILURE;
		}

		for (std::size_t i(0); i < static_cast<std::size_t>(rc); ++i) {
			const struct kevent & e(p[i]);
			if (EVFILT_VNODE == e.filter) {
				if (vt.query_buffer_fd() == static_cast<int>(e.ident)) {
					vt.reload();
					update_needed = true;
				}
			}
			if (EVFILT_SIGNAL == e.filter)
				handle_signal(e.ident);
			if (EVFILT_READ == e.filter && event.get() == static_cast<int>(e.ident)) 
				handle_input_event(event, kbd, keymap, vt);
		}
	}

	throw EXIT_SUCCESS;
}
