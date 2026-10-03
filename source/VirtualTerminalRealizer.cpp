/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define _BSD_SOURCE 1
#include <algorithm>
#include <vector>
#include <iostream>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <csignal>
#include <limits>
#include <unistd.h>
#include "utils.h"
#include "ttyutils.h"
#include "fdutils.h"
#include "FileDescriptorOwner.h"
#include "InputMessage.h"
#include "CharacterCell.h"
#include "GraphicsInterface.h"
#include "Monospace16x16Font.h"
#include "FontLoader.h"
#include "UnicodeClassification.h"
#include "UnicodeKeyboard.h"
#include "VirtualTerminalRealizer.h"
#include "ControlCharacters.h"
#include "SignalManagement.h"
#include "kbdmap_utils.h"
#include "kbdmap_default.h"
#include "config/haswscons.h"
#include <sys/ioctl.h>
#if defined(HAS_WSCONS)
#	include <dev/wscons/wsconsio.h>
#	include <dev/wscons/wsdisplay_usl_io.h>	// VT/CONSIO ioctls
#elif defined(__LINUX__) || defined(__linux__)
#	include <linux/fb.h>
#	include <linux/kd.h>
#	include <linux/vt.h>
#else
#	include <sys/mouse.h>
#	include <sys/kbio.h>
#	include <sys/consio.h>
#endif

using namespace VirtualTerminalRealizer;

namespace {

template<typename I>
inline
void
press (
	I & counter,
	bool value
) {
	if (value) {
		if (counter < std::numeric_limits<I>::max()) ++counter;
	} else {
		if (counter > std::numeric_limits<I>::min()) --counter;
	}
}

template<typename I>
inline
void
add (
	I & counter,
	signed long delta
) {
	if (0 > delta) {
		counter = counter < std::numeric_limits<I>::min() - delta ? std::numeric_limits<I>::min() : counter + delta;
	} else
	if (0 < delta) {
		counter = counter > std::numeric_limits<I>::max() - delta ? std::numeric_limits<I>::max() : counter + delta;
	}
}

};

/* Keyboard shift state *****************************************************
// **************************************************************************
*/

inline
void
KeyboardModifierState::lheader::clear()
{
	for (std::size_t k(0); k < NUM_MODIFIERS; ++k)
		pressed[k] = 0U;
}

inline
void
KeyboardModifierState::header::clear()
{
	lheader::clear();
	for (std::size_t k(0); k < NUM_MODIFIERS; ++k)
		latched[k] = locked[k] = false;
}

KeyboardModifierState::KeyboardModifierState()
{
	local.clear();
}

inline
off_t
KeyboardModifierState::find_append_point_while_locked(
) const {
	off_t last(sizeof(header));
	event f;
	for (off_t o(last); ; o += sizeof f) {
		if (0 >= pread(f, o)) break;
		if (f.NOWT != f.command) last = o + sizeof f;
	}
	return last;
}

inline
void
KeyboardModifierState::take_snapshot_while_locked(
	header & h
) const {
	h.clear();
	pread(h, 0U);
	event e;
	for (off_t o(sizeof h); ; o += sizeof e) {
		if (0 >= pread(e, o)) break;
		if (e.NOWT != e.command) {
			switch (e.command) {
				// Range checked here just in case someone is manually writing to the file.
				case e.PRESS:	if (e.modifier < NUM_MODIFIERS) press(h.pressed[e.modifier], e.value); break;
				case e.LATCH:	if (e.modifier < NUM_MODIFIERS) h.latched[e.modifier] = e.value; break;
				case e.LOCK:	if (e.modifier < NUM_MODIFIERS) h.locked [e.modifier] = e.value; break;
				case e.NOWT:	break;
			}
			e.command = e.NOWT;
			pwrite(h, 0U);
			pwrite(e, o);
		}
	}
}

inline
void
KeyboardModifierState::un_press(
) {
	if (0 > file.get()) return;
	if (0 >	lock_exclusive_or_wait(file.get())) return;
	event e;
	e.command = e.PRESS;
	e.value = false;
	off_t o(find_append_point_while_locked());
	for (std::size_t k(0); k < NUM_MODIFIERS; ++k) {
		e.modifier = k;
		for (unsigned p(local.pressed[k]) ; p > 0U ; --p) {
			pwrite(e, o);
			o += sizeof e;
		}
	}
	unlock_file(file.get());
}

inline
void
KeyboardModifierState::re_press(
) {
	if (0 > file.get()) return;
	if (0 >	lock_exclusive_or_wait(file.get())) return;
	event e;
	e.command = e.PRESS;
	e.value = true;
	off_t o(find_append_point_while_locked());
	for (std::size_t k(0); k < NUM_MODIFIERS; ++k) {
		e.modifier = k;
		for (unsigned p(local.pressed[k]) ; p > 0U ; --p) {
			pwrite(e, o);
			o += sizeof e;
		}
	}
	ftruncate(file.get(), o);
	unlock_file(file.get());
}

KeyboardModifierState::~KeyboardModifierState()
{
	un_press();
	local.clear();
}

extern
void
KeyboardModifierState::set_file_fd(
	int fd
) {
	un_press();
	file.reset(fd);
	re_press();
}

inline
void
KeyboardModifierState::append(
	const event & e
) const {
	if (0 > file.get()) return;
	if (0 >	lock_exclusive_or_wait(file.get())) return;
	off_t o(find_append_point_while_locked());
	pwrite(e, o);
	unlock_file(file.get());
}

extern
KeyboardLEDs
KeyboardModifierState::query_LEDs() const
{
	if (0 > file.get()) return KeyboardLEDs();
	if (0 >	lock_exclusive_or_wait(file.get())) return KeyboardLEDs();
	Snapshot snapshot;
	take_snapshot_while_locked(snapshot);
	unlock_file(file.get());
	return snapshot.operator KeyboardLEDs();
}

inline
uint8_t
KeyboardModifierState::modifiers() const
{
	if (0 > file.get()) return 0U;
	if (0 >	lock_exclusive_or_wait(file.get())) return 0U;
	Snapshot snapshot;
	take_snapshot_while_locked(snapshot);
	unlock_file(file.get());
	return
		(snapshot.control() ? INPUT_MODIFIER_CONTROL : 0) |
		(snapshot.level2_lock()^snapshot.level2() ? INPUT_MODIFIER_LEVEL2 : 0) |
		((snapshot.level3_lock()^snapshot.level3())||snapshot.alt() ? INPUT_MODIFIER_LEVEL3 : 0) |
		(snapshot.group2() ? INPUT_MODIFIER_GROUP2 : 0) |
		(snapshot.super() ? INPUT_MODIFIER_SUPER : 0) ;
}

inline
uint8_t
KeyboardModifierState::nolevel2_modifiers() const
{
	if (0 > file.get()) return 0U;
	if (0 >	lock_exclusive_or_wait(file.get())) return 0U;
	Snapshot snapshot;
	take_snapshot_while_locked(snapshot);
	unlock_file(file.get());
	return
		(snapshot.control() ? INPUT_MODIFIER_CONTROL : 0) |
		((snapshot.level3_lock()^snapshot.level3())||snapshot.alt() ? INPUT_MODIFIER_LEVEL3 : 0) |
		(snapshot.group2() ? INPUT_MODIFIER_GROUP2 : 0) |
		(snapshot.super() ? INPUT_MODIFIER_SUPER : 0) ;
}

inline
uint8_t
KeyboardModifierState::nolevel_nogroup_noctrl_modifiers() const
{
	if (0 > file.get()) return 0U;
	if (0 >	lock_exclusive_or_wait(file.get())) return 0U;
	Snapshot snapshot;
	take_snapshot_while_locked(snapshot);
	unlock_file(file.get());
	return
		(snapshot.super() ? INPUT_MODIFIER_SUPER : 0) ;
}

inline
bool
KeyboardModifierState::accelerator() const
{
	if (0 > file.get()) return false;
	if (0 >	lock_exclusive_or_wait(file.get())) return false;
	Snapshot snapshot;
	take_snapshot_while_locked(snapshot);
	unlock_file(file.get());
	return snapshot.alt();
}

inline
std::size_t
KeyboardModifierState::Snapshot::semi_shiftable_index() const
{
	return (level2() ? 1U : 0U) + (control() ? 2U : 0) + (level3() ? 4U : 0) + (group2() ? 8U : 0) ;
}

inline
std::size_t
KeyboardModifierState::Snapshot::shiftable_index() const
{
	return (level2_lock() ^ level2() ? 1U : 0U) + (control() ? 2U : 0) + (level3_lock() ^ level3() ? 4U : 0) + (group2() ? 8U : 0) ;
}

inline
std::size_t
KeyboardModifierState::Snapshot::capsable_index() const
{
	const bool lock(caps_lock() || level2_lock());
	return (lock ^ level2() ? 1U : 0U) + (control() ? 2U : 0) + (level3_lock() ^ level3() ? 4U : 0) + (group2() ? 8U : 0);
}

inline
std::size_t
KeyboardModifierState::Snapshot::numable_index() const
{
	const bool lock(num_lock() || level2_lock());
	return (lock ^ level2() ? 1U : 0U) + (control() ? 2U : 0) + (level3_lock() ^ level3() ? 4U : 0) + (group2() ? 8U : 0);
}

inline
std::size_t
KeyboardModifierState::Snapshot::funcable_index() const
{
	return (level2_lock() || level2() ? 1U : 0U) + (control() ? 2U : 0) + (alt() ? 4U : 0) + (group2() ? 8U : 0);
}

inline
std::size_t
KeyboardModifierState::query_kbdmap_parameter (
	const uint32_t cmd
) const {
	if (0 > file.get()) return -1U;
	if (0 >	lock_exclusive_or_wait(file.get())) return -1U;
	Snapshot snapshot;
	take_snapshot_while_locked(snapshot);
	unlock_file(file.get());
	switch (cmd) {
		default:	return -1U;
		case 'p':	return 0U;
		case 'n':	return snapshot.numable_index();
		case 'c':	return snapshot.capsable_index();
		case 'l':	return snapshot.semi_shiftable_index();
		case 's':	return snapshot.shiftable_index();
		case 'f':	return snapshot.funcable_index();
	}
}

extern
void
KeyboardModifierState::set_pressed(
	std::size_t k,
	bool v
) {
	if (k >= NUM_MODIFIERS) return;
	if (!!local.pressed[k] == v) return;	// Avoid auto-repeat building up presses.
	press(local.pressed[k], v);
	event e = {};
	e.command = e.PRESS;
	e.value = v;
	e.modifier = k;
	append(e);
}

inline
void
KeyboardModifierState::set_locked(
	std::size_t k,
	bool v
) {
	if (k >= NUM_MODIFIERS) return;
	if (0 > file.get()) return;
	if (0 >	lock_exclusive_or_wait(file.get())) return;
	Snapshot snapshot;
	take_snapshot_while_locked(snapshot);
	if (snapshot.is_locked(k) != v) {
		event e = {};
		e.command = e.LOCK;
		e.value = v;
		e.modifier = k;
		off_t o(find_append_point_while_locked());
		pwrite(e, o);
	}
	unlock_file(file.get());
}

inline
void
KeyboardModifierState::invert_lock(
	std::size_t k
) {
	if (k >= NUM_MODIFIERS) return;
	if (0 > file.get()) return;
	if (0 >	lock_exclusive_or_wait(file.get())) return;
	Snapshot snapshot;
	take_snapshot_while_locked(snapshot);
	event e = {};
	e.command = e.LOCK;
	e.value = !snapshot.is_latched_or_locked(k);
	e.modifier = k;
	off_t o(find_append_point_while_locked());
	pwrite(e, o);
	unlock_file(file.get());
}

inline
void
KeyboardModifierState::latch(
	std::size_t k
) {
	if (k >= NUM_MODIFIERS) return;
	event e = {};
	e.command = e.LATCH;
	e.value = true;
	e.modifier = k;
	append(e);
}

inline
void
KeyboardModifierState::unlatch_all()
{
	if (0 > file.get()) return;
	if (0 >	lock_exclusive_or_wait(file.get())) return;
	Snapshot snapshot;
	take_snapshot_while_locked(snapshot);
	event e;
	e.command = e.LATCH;
	e.value = false;
	off_t o(find_append_point_while_locked());
	for (std::size_t k(0); k < NUM_MODIFIERS; ++k) {
		if (snapshot.is_latched(k)) {
			e.modifier = k;
			pwrite(e, o);
			o += sizeof e;
		}
	}
	unlock_file(file.get());
}

/* Mouse and touchpad state *************************************************
// **************************************************************************
*/

#if !defined(__LINUX__) && !defined(__linux__) && !defined(__NetBSD__)
static_assert(MouseState::NUM_BUTTONS >= MOUSE_MAXBUTTON, "The mouse state must have at least as many buttons as the mouse(4) protocol.");
#endif

inline
void
MouseState::lheader::clear()
{
	for (std::size_t button(0); button < NUM_BUTTONS; ++button)
		pressed[button] = 0U;
}

inline
void
MouseState::header::clear()
{
	lheader::clear();
	for (std::size_t axis(0); axis < NUM_AXES; ++axis)
		positions[axis] = 0U;
	for (std::size_t wheel(0); wheel < NUM_WHEELS; ++wheel)
		offsets[wheel] = 0;
}

MouseState::MouseState()
{
	local.clear();
}

inline
off_t
MouseState::find_append_point_while_locked(
) const {
	off_t last(sizeof(header));
	event f;
	for (off_t o(last); ; o += sizeof f) {
		if (0 >= pread(f, o)) break;
		if (f.NOWT != f.command) last = o + sizeof f;
	}
	return last;
}

inline
void
MouseState::take_snapshot_while_locked(
	header & h
) const {
	h.clear();
	pread(h, 0U);
	event e;
	for (off_t o(sizeof h); ; o += sizeof e) {
		if (0 >= pread(e, o)) break;
		if (e.NOWT != e.command) {
			switch (e.command) {
				// Range checked here just in case someone is manually writing to the file.
				case e.BUTTON:	if (e.index < NUM_BUTTONS) press(h.pressed[e.index], e.value); break;
				case e.WHEEL:	if (e.index < NUM_WHEELS) add(h.offsets[e.index], e.delta); break;
				case e.ABSPOS:	if (e.index < NUM_AXES) h.positions[e.index] = e.position; break;
				case e.RELPOS:	if (e.index < NUM_AXES) add(h.positions[e.index], e.delta); break;
				case e.NOWT:	break;
			}
			e.command = e.NOWT;
			pwrite(h, 0U);
			pwrite(e, o);
		}
	}
}

inline
void
MouseState::un_press(
) {
	if (0 > file.get()) return;
	if (0 >	lock_exclusive_or_wait(file.get())) return;
	event e;
	e.command = e.BUTTON;
	e.value = false;
	off_t o(find_append_point_while_locked());
	for (std::size_t b(0); b < NUM_BUTTONS; ++b) {
		e.index = b;
		for (unsigned p(local.pressed[b]) ; p > 0U ; --p) {
			pwrite(e, o);
			o += sizeof e;
		}
	}
	unlock_file(file.get());
}

inline
void
MouseState::re_press(
) {
	if (0 > file.get()) return;
	if (0 >	lock_exclusive_or_wait(file.get())) return;
	event e;
	e.command = e.BUTTON;
	e.value = true;
	off_t o(find_append_point_while_locked());
	for (std::size_t b(0); b < NUM_BUTTONS; ++b) {
		e.index = b;
		for (unsigned p(local.pressed[b]) ; p > 0U ; --p) {
			pwrite(e, o);
			o += sizeof e;
		}
	}
	ftruncate(file.get(), o);
	unlock_file(file.get());
}

MouseState::~MouseState()
{
	un_press();
	local.clear();
}

void
MouseState::set_file_fd(
	int fd
) {
	un_press();
	file.reset(fd);
	re_press();
}

inline
void
MouseState::append(
	const event & e
) const {
	if (0 > file.get()) return;
	if (0 >	lock_exclusive_or_wait(file.get())) return;
	off_t o(find_append_point_while_locked());
	pwrite(e, o);
	unlock_file(file.get());
}

void
MouseState::relpos(unsigned axis, signed long delta)
{
	if (axis >= NUM_AXES) return;
	if (!delta) return;
	event e = {};
	e.command = e.RELPOS;
	e.index = axis;
	e.delta = delta;
	append(e);
}

void
MouseState::abspos(unsigned axis, unsigned long pos)
{
	if (axis >= NUM_AXES) return;
	event e = {};
	e.command = e.ABSPOS;
	e.index = axis;
	e.position = pos;
	append(e);
}

void
MouseState::button(unsigned button, bool value)
{
	if (button >= NUM_BUTTONS) return;
	if (!!local.pressed[button] == value) return;	// Avoid auto-repeat building up presses.
	press(local.pressed[button], value);
	event e = {};
	e.command = e.BUTTON;
	e.index = button;
	e.value = value;
	append(e);
}

void
MouseState::wheel(unsigned wheel, int32_t delta)
{
	if (wheel >= NUM_WHEELS) return;
	if (!delta) return;
	event e = {};
	e.command = e.WHEEL;
	e.index = wheel;
	e.delta = delta;
	append(e);
}

unsigned long
MouseState::query_pos (
	unsigned axis
) const {
	if (axis >= NUM_AXES) return 0UL;
	if (0 > file.get()) return 0;
	if (0 >	lock_exclusive_or_wait(file.get())) return 0;
	Snapshot snapshot;
	take_snapshot_while_locked(snapshot);
	unlock_file(file.get());
	return snapshot.positions[axis];
}

void
MouseState::query_buttons(
	bool buttonv[NUM_BUTTONS]	// "buttons" is a macro from term.h, unfortunately
) const {
	if (0 > file.get()) return;
	if (0 >	lock_exclusive_or_wait(file.get())) return;
	Snapshot snapshot;
	take_snapshot_while_locked(snapshot);
	unlock_file(file.get());
	for (unsigned i(0U); i < NUM_BUTTONS; ++i)
		buttonv[i] = snapshot.pressed[i] > 0U;
}

int32_t
MouseState::get_and_reset_wheel(unsigned wheel)
{
	if (wheel >= NUM_WHEELS) return 0;
	if (0 > file.get()) return 0;
	if (0 >	lock_exclusive_or_wait(file.get())) return 0;
	Snapshot snapshot;
	take_snapshot_while_locked(snapshot);
	const int32_t b(snapshot.offsets[wheel]);
	if (b != 0) {
		snapshot.offsets[wheel] = 0;
		pwrite(snapshot, 0U);
	}
	unlock_file(file.get());
	return b;
}

/* Resources shared amongst multiple HODs ***********************************
// **************************************************************************
*/

SharedHODResources::SharedHODResources(
	VirtualTerminalBackEnd & t,
	Monospace16x16Font & f
) :
	vt(t),
	gdi(),
	font(f),
	glyph_cache(),
	mouse_glyph_handle(gdi.MakeGlyphBitmap()),
	underline_glyph_handle(gdi.MakeGlyphBitmap()),
	underover_glyph_handle(gdi.MakeGlyphBitmap()),
	bar_glyph_handle(gdi.MakeGlyphBitmap()),
	box_glyph_handle(gdi.MakeGlyphBitmap()),
	block_glyph_handle(gdi.MakeGlyphBitmap()),
	star_glyph_handle(gdi.MakeGlyphBitmap()),
	mirrorl_glyph_handle(gdi.MakeGlyphBitmap())
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
		underover_glyph_handle->Plot(row, row < 2U || row > 13U ? 0xFFFF : 0x0000);
		bar_glyph_handle->Plot(row, 0xC000);
		box_glyph_handle->Plot(row, row < 2U || row > 13U ? 0xFFFF : 0xC003);
		block_glyph_handle->Plot(row, 0xFFFF);
		mirrorl_glyph_handle->Plot(row, row < 14U ? 0x0003 : 0xFFFF);
	}
}

SharedHODResources::~SharedHODResources(
) {
	ReduceCacheSizeTo(0U);
	gdi.DeleteGlyphBitmap(mirrorl_glyph_handle);
	gdi.DeleteGlyphBitmap(star_glyph_handle);
	gdi.DeleteGlyphBitmap(block_glyph_handle);
	gdi.DeleteGlyphBitmap(box_glyph_handle);
	gdi.DeleteGlyphBitmap(bar_glyph_handle);
	gdi.DeleteGlyphBitmap(underover_glyph_handle);
	gdi.DeleteGlyphBitmap(underline_glyph_handle);
	gdi.DeleteGlyphBitmap(mouse_glyph_handle);
}

SharedHODResources::GlyphBitmapHandle
SharedHODResources::GetCursorGlyphBitmap(CursorSprite::glyph_type t) const
{
	switch (t) {
		case CursorSprite::UNDERLINE:	return underline_glyph_handle;
		case CursorSprite::BAR:		return bar_glyph_handle;
		case CursorSprite::BOX:		return box_glyph_handle;
		case CursorSprite::BLOCK:	return block_glyph_handle;
		case CursorSprite::STAR:	return star_glyph_handle;
		case CursorSprite::UNDEROVER:	return underover_glyph_handle;
		case CursorSprite::MIRRORL:	return mirrorl_glyph_handle;
#if 0	// Actually unreachable, and generates a warning.
		default:			return star_glyph_handle;
#endif
	}
}

SharedHODResources::GlyphBitmapHandle
SharedHODResources::GetCachedGlyphBitmap(
	uint32_t character,
	CharacterCell::attribute_type attributes
) {
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
		gdi.PlotGreek(handle, character);
	gdi.ApplyAttributesToGlyphBitmap(handle, attributes);
	ReduceCacheSizeTo(MAX_CACHED_GLYPHS);
	glyph_cache.push_back(GlyphCacheEntry(handle, character, attributes));
	return handle;
}

void
SharedHODResources::ReduceCacheSizeTo(
	std::size_t size
) {
	while (glyph_cache.size() > size) {
		const GlyphCacheEntry e(glyph_cache.back());
		glyph_cache.pop_back();
		gdi.DeleteGlyphBitmap(e.handle);
	}
}

/* Colour manipulation used for painting onto GDI bitmaps *******************
// **************************************************************************
*/

namespace {

const CharacterCell::colour_type mouse_fg(ALPHA_FOR_MOUSE_SPRITE,0xFF,0xFF,0xFF);

}

/* Human output devices *****************************************************
// **************************************************************************
*/

HOD::Options::Options(
) :
	quadrant(3U),
	wrong_way_up(false),
	faint_as_colour(false),
	bold_as_colour(false),
	has_pointer(false)
{
}

HOD::HOD(
	SharedHODResources & r,
	const Options & o
) :
	options(o),
	shared(r),
	refresh_needed(true),
	update_needed(true),
	screen_y(0U),
	screen_x(0U),
	visible_origin(),
	visible_size(),
	c(true /* software cursor */, 24U, 80U),
	vio(c)
{
}

HOD::~HOD()
{
}

inline
void
HOD::paint_changed_cells_onto_framebuffer()
{
	const bool invert_screen(c.query_screen_flags() & ScreenFlags::INVERTED);
	const GraphicsInterface::ScreenBitmapHandle screen(GetScreenBitmap());
	if (!screen) return;

	for (unsigned row(0); row < c.query_h(); ++row) {
		for (unsigned col(0); col < c.query_w(); ++col) {
			TUIDisplayCompositor::DirtiableCell & cell(c.cur_at(row, col));
			if (!cell.touched()) continue;
			CharacterCell::attribute_type font_attributes(cell.attributes);
			ColourPair colour(cell);
			if (invert_screen)
				font_attributes ^= CharacterCell::INVERSE;
			// Bodges that do not do things properly.
			if (options.faint_as_colour || options.bold_as_colour) {
				// Properly, reverse video is a distinct glyph.
				if (CharacterCell::INVERSE & font_attributes) {
					std::swap(colour.foreground,colour.background);
					font_attributes &= ~CharacterCell::INVERSE;
				}
				if (options.faint_as_colour) {
					if (CharacterCell::FAINT & font_attributes) {
						colour.foreground.dim();
//						colour.background.dim();
						font_attributes &= ~CharacterCell::FAINT;
					}
				}
				if (options.bold_as_colour) {
					if (CharacterCell::BOLD & font_attributes) {
						colour.foreground.bright();
//						colour.background.bright();
						font_attributes &= ~CharacterCell::BOLD;
					}
				}
			}
			if (c.is_marked(true, row, col) && (CursorSprite::VISIBLE & c.query_cursor_attributes())) {
				const SharedHODResources::GlyphBitmapHandle cursor_handle(GetCursorGlyphBitmap());
				if (CharacterCell::INVISIBLE & font_attributes) {
					// Being invisible leaves just the cursor glyph.
					colour.foreground.complement();
					shared.BitBLT(screen, cursor_handle, row, col, colour);
				} else {
					ColourPair colours[2] = { colour, colour };
					colours[1].foreground.complement();
					colours[1].background.complement();
					const SharedHODResources::GlyphBitmapHandle glyph_handle(shared.GetCachedGlyphBitmap(cell.character, font_attributes));
					shared.BitBLTMask(screen, glyph_handle, cursor_handle, row, col, colours);
				}
			} else {
				if (CharacterCell::INVISIBLE & font_attributes) {
					// Being invisible is always the same glyph, which may not be in the font, so we don't cache it.
					const SharedHODResources::GlyphBitmapHandle glyph_handle(shared.gdi.MakeGlyphBitmap());
					shared.gdi.PlotGreek(glyph_handle, 0x20);
					shared.gdi.ApplyAttributesToGlyphBitmap(glyph_handle, font_attributes);
					shared.BitBLT(screen, glyph_handle, row, col, colour);
					shared.gdi.DeleteGlyphBitmap(glyph_handle);
				} else {
					const SharedHODResources::GlyphBitmapHandle glyph_handle(shared.GetCachedGlyphBitmap(cell.character, font_attributes));
					shared.BitBLT(screen, glyph_handle, row, col, colour);
				}
			}
			if (c.is_pointer(row, col) && (PointerSprite::VISIBLE & c.query_pointer_attributes())) {
				shared.BitBLTAlpha(screen, GetPointerGlyphBitmap(), row, col, mouse_fg);
			}
			cell.untouch();
		}
	}
}

/// \brief Clip and position the visible portion of the terminal's display buffer.
inline
void
HOD::position_vt_visible_area (
) {
	// Glue the terminal window to the edges of the display screen buffer.
	screen_y = !(options.quadrant & 0x02) && shared.vt.query_size().h < c.query_h() ? c.query_h() - shared.vt.query_size().h : 0;
	screen_x = (1U == options.quadrant || 2U == options.quadrant) && shared.vt.query_size().w < c.query_w() ? c.query_w() - shared.vt.query_size().w : 0;
	// Ask the VirtualTerminal to position the visible window according to cursor placement.
	const VirtualTerminalBackEnd::wh area(c.query_w() - screen_x, c.query_h() - screen_y);
	shared.vt.calculate_visible_rectangle(area, visible_origin, visible_size);
}

/// \brief Render the terminal's display buffer and cursor/pointer states onto the TUI compositor.
inline
void
HOD::compose_new_from_vt ()
{
	for (unsigned short row(0U); row < visible_size.h; ++row) {
		const unsigned short source_row(visible_origin.y + row);
		const unsigned short dest_row(screen_y + (options.wrong_way_up ? visible_size.h - row - 1U : row));
		for (unsigned short col(0U); col < visible_size.w; ++col)
			c.poke(dest_row, col + screen_x, shared.vt.at(source_row, visible_origin.x + col));
	}
	const CursorSprite::attribute_type a(shared.vt.query_cursor_attributes());
	// If the cursor is invisible, we are not guaranteed that the VirtualTerminal has kept the visible area around it.
	if (CursorSprite::VISIBLE & a) {
		const unsigned short cursor_y(screen_y + (options.wrong_way_up ? visible_size.h - shared.vt.query_cursor().y - 1U : shared.vt.query_cursor().y) - visible_origin.y);
		c.move_cursor(cursor_y, shared.vt.query_cursor().x - visible_origin.x + screen_x);
	}
	c.set_cursor_state(a, shared.vt.query_cursor_glyph());
	if (c.set_screen_flags(shared.vt.query_screen_flags()))
		invalidate_cur();
	if (options.has_pointer)
		c.set_pointer_attributes(shared.vt.query_pointer_attributes());
}

inline
void
HOD::handle_refresh_event (
) {
	if (refresh_needed) {
		refresh_needed = false;
		vio.CLSToSpace(ColourPair::def);
		position_vt_visible_area();
		compose_new_from_vt();
		set_update_needed();
	}
}

inline
void
HOD::handle_update_event (
) {
	if (update_needed) {
		update_needed = false;
		c.repaint_new_to_cur();
		paint_changed_cells_onto_framebuffer();
	}
}

inline
void
HOD::set_pointer_col (
	const SharedHODResources::coordinate col
) {
	if (c.change_pointer_col(col))
		set_update_needed();
}

inline
void
HOD::set_pointer_row (
	const SharedHODResources::coordinate row
) {
	if (c.change_pointer_row(row))
		set_update_needed();
}

inline
void
HOD::set_pointer_dep (
	const SharedHODResources::coordinate dep
) {
	if (c.change_pointer_dep(dep))
		set_update_needed();
}

/* Human input devices ******************************************************
// **************************************************************************
*/

bool HID::set_exclusive(bool) { return true; }
void HID::save() {}
void HID::restore() {}
void HID::set_mode() {}

void
HID::handle_mouse_abspos(
	const MouseAxis axis,
	const unsigned long abspos,
	const unsigned long maximum
) {
	shared.mouse_abspos(axis, abspos, maximum);
}

void
HID::handle_mouse_relpos(
	const MouseAxis axis,
	int32_t amount
) {
	shared.mouse_relpos(axis, amount);
}

void
HID::handle_mouse_button(
	const uint16_t button,
	const bool value
) {
	shared.mouse_button(button, value);
}

/* Human input devices with line disciplines ********************************
// **************************************************************************
*/

HIDWithLineDiscipline::HIDWithLineDiscipline(
	SharedHIDResources & r,
	FileDescriptorOwner & fd
) :
	HID(r, fd),
	original_attr()
{
}

HIDWithLineDiscipline::~HIDWithLineDiscipline()
{
	restore();
}

void
HIDWithLineDiscipline::save()
{
	if (0 <= device.get())
		tcgetattr_nointr(device.get(), original_attr);
}

void
HIDWithLineDiscipline::set_mode()
{
	if (0 <= device.get())
		// The line discipline needs to be set to raw mode for the duration.
		tcsetattr_nointr(device.get(), TCSADRAIN, disable_canonical_software_processing(original_attr));
}

void
HIDWithLineDiscipline::restore()
{
	if (0 <= device.get())
		tcsetattr_nointr(device.get(), TCSADRAIN, original_attr);
}

termios
HIDWithLineDiscipline::disable_canonical_software_processing (
	const termios & ti
) {
	return ::disable_canonical_software_processing(ti);
}

/* Human input devices with line disciplines that speak the KBIO protocol ***
// **************************************************************************
*/

HIDSpeakingKBIO::HIDSpeakingKBIO(
	SharedHIDResources & r,
	FileDescriptorOwner & fd
) :
	HIDWithLineDiscipline(r, fd),
	offset(0U),
#if defined(__LINUX__) || defined(__linux__)
	state(0U),
	up(false),
#endif
	code(0U),
	pressed(),
	kbmode(),
	kbmute()
{
}

inline
HIDSpeakingKBIO::~HIDSpeakingKBIO()
{
	restore();
}

inline
void
HIDSpeakingKBIO::set_LEDs(
	const KeyboardLEDs & leds
) {
	if (0 <= device.get()) {
#if defined(WSKBDIO_SETLEDS)
		const int newled((leds.caps_lock()||leds.shift2_lock() ? WSKBD_LED_CAPS : 0)|(leds.num_lock() ? WSKBD_LED_NUM : 0)|(leds.group2() ? WSKBD_LED_SCROLL : 0)|(leds.shift3_lock() ? WSKBD_LED_COMPOSE : 0));
		ioctl(device.get(), WSKBDIO_SETLEDS, &newled);
#elif defined(KDSETLED)
		const int newled((leds.caps_lock()||leds.shift2_lock() ? LED_CAP : 0)|(leds.num_lock() ? LED_NUM : 0)|(leds.group2() ? LED_SCR : 0));
		ioctl(device.get(), KDSETLED, newled);
#endif
	}
}

inline
void
HIDSpeakingKBIO::handle_input_events(
) {
	const ssize_t n(read(device.get(), reinterpret_cast<char *>(buffer) + offset, sizeof buffer - offset));
	if (0 > n) return;
	for (
		offset += n;
		offset > 0 && offset >= sizeof *buffer;
		std::memmove(buffer, buffer + 1U, sizeof buffer - sizeof *buffer),
		offset -= sizeof *buffer
	) {
		const uint16_t c(static_cast<uint8_t>(buffer[0]));
		const uint8_t lowbits(c & 0x7F);
#if defined(__LINUX__) || defined(__linux__)
		// The MEDIUMRAW protocol uses unused scan code 0 as an escape mechanism to encode 14-bit keycodes as fake "release" scancodes.
		// It is only documented in a comment in the kernel source.
		code = (code << 7) | lowbits;
		switch (state) {
			case 0U:
				up = !!(c & 0x80);
				if (0x00 != lowbits) {
					break;
				}
				[[clang::fallthrough]];
			default:
				if (state > 1U) state = 0U; else ++state;
				break;
		}
		if (0U == state) {
			const unsigned value(up ? 0U : is_pressed(code) ? 2U : 1U);
			set_pressed(code, !up);
			const uint16_t index(linux_evdev_keycode_to_keymap_index(code));
			shared.handle_keyboard(index, value);
			code = 0x0000;
		}
#else
		switch (c) {
			case 0xE0:
			case 0xE1:
				code = c << 8U;
				break;
			default:
			{
				const bool up(!!(c & 0x80));
				code |= lowbits;
				const unsigned value(up ? 0U : is_pressed(code) ? 2U : 1U);
				set_pressed(code, !up);
				const uint16_t index(bsd_keycode_to_keymap_index(code));
				shared.handle_keyboard(index, value);
				code = 0x0000;
				break;
			}
		}
#endif
	}
}

void
HIDSpeakingKBIO::save()
{
	HIDWithLineDiscipline::save();
	if (0 <= device.get()) {
#if defined(WSKBDIO_SETMODE) && defined (WSKBDIO_GETMODE)
		ioctl(device.get(), WSKBDIO_GETMODE, &kbmode);
#else
#	if defined(KDSKBMODE) && defined(KDGKBMODE)
		// Beneath the line discipline the keyboard needs to be set to keycode mode.
		ioctl(device.get(), KDGKBMODE, &kbmode);
#	endif
#	if defined(KDGKBMUTE) && defined(KDSKBMUTE)
		ioctl(device.get(), KDGKBMUTE, &kbmute);
#	endif
#endif
#if defined(WSKBDIO_SETLEDS)
		const int newled(0);
		ioctl(device.get(), WSKBDIO_SETLEDS, &newled);
#elif defined(KDSETLED)
		// Set the LED operation to manual mode for the duration.
		ioctl(device.get(), KDSETLED, 0U);
#endif
	}
}

void
HIDSpeakingKBIO::set_mode()
{
	HIDWithLineDiscipline::set_mode();
	if (0 <= device.get()) {
#if defined(WSKBDIO_SETMODE) && defined (WSKBDIO_GETMODE)
		ioctl(device.get(), WSKBDIO_SETMODE, WSKBD_RAW);
#else
#	if defined(KDSKBMODE) && defined(KDGKBMODE)
		// Beneath the line discipline the keyboard needs to be set to keycode mode.
#		if defined(K_MEDIUMRAW)
		ioctl(device.get(), KDSKBMODE, K_MEDIUMRAW);
#		elif defined(K_RAW)
		ioctl(device.get(), KDSKBMODE, K_RAW);
#		else
		ioctl(device.get(), KDSKBMODE, K_CODE);
#		endif
#	endif
#	if defined(KDGKBMUTE) && defined(KDSKBMUTE)
		ioctl(device.get(), KDSKBMUTE, 0);
#	endif
#endif
	}
}

void
HIDSpeakingKBIO::restore()
{
	if (0 <= device.get()) {
#if defined(WSKBDIO_SETMODE) && defined (WSKBDIO_GETMODE)
		ioctl(device.get(), WSKBDIO_SETMODE, kbmode);
#else
#	if defined(KDGKBMUTE) && defined(KDSKBMUTE)
		ioctl(device.get(), KDSKBMUTE, kbmute);
#	endif
#	if defined(KDSKBMODE) && defined(KDGKBMODE)
		// Beneath the line discipline the keyboard needs to be set back out of keycode mode.
		ioctl(device.get(), KDSKBMODE, kbmode);
#	endif
#endif
#if defined(WSKBDIO_SETLEDS)
		const int newled(-1);
		ioctl(device.get(), WSKBDIO_SETLEDS, &newled);
#elif defined(KDSETLED)
		// Set the LED operation back to automatic mode managed by the kernel virtual terminal.
		ioctl(device.get(), KDSETLED, -1U);
#endif
		ioctl(device.get(), TIOCNOTTY, 0);
	}
	HIDWithLineDiscipline::restore();
}

SwitchingController::~SwitchingController() {}

/* Signal handling **********************************************************
// **************************************************************************
*/

namespace {

sig_atomic_t window_resized(false), terminate_signalled(false), interrupt_signalled(false), hangup_signalled(false), usr1_signalled(false), usr2_signalled(false);

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

}

/* Common processing ********************************************************
// **************************************************************************
*/

Main::Main(
	const char * pp,
	const ProcessEnvironment & ee,
	bool mp
) :
	SharedHIDResources(),
	SharedHODResources(vt, font),
	queue(kqueue()),
	switching_controller(),
	prog(pp),
	envs(ee),
	active(false),
	mouse_primary(mp),
#if defined(__FreeBSD__) || defined(__DragonFly__)
	passthrough_mouse(-1),
	passthrough_keyboard(-1),
#endif
	keyboard_state(),
	leds(),
	mouse_state(),
	vt(),
	map(),
	font(),
	ip(),
	mouse_col(-1U),
	mouse_row(-1U),
	mouse_dep(-1U),
	dead_keys(),
	inputs(),
	outputs()
{
	if (0 > queue.get()) {
		die_errno(prog, envs, "kqueue");
	}
}

void
Main::autoconfigure(
	const std::list<std::string> & device_paths,
	bool has_display_device,
	bool has_mouse_device,
	bool has_keyboard_device,
	bool has_NumLock_key,
	bool has_LEDs_device
) {
	// Auto-configure the VT back-end.
	{
		FileDescriptorOwner vt_dir_fd(-1);
		std::string vt_dirname;
		for (std::list<std::string>::const_iterator p(device_paths.begin()), e(device_paths.end()); p != e; ++p) {
			vt_dirname = "vcs/" + *p;
			vt_dir_fd.reset(open_dir_at(AT_FDCWD, vt_dirname.c_str()));
			if (0 <= vt_dir_fd.get()) break;
		}
		if (0 > vt_dir_fd.get())
			vt_dir_fd.reset(open_dir_at(AT_FDCWD, "vcs/default"));
		if (0 > vt_dir_fd.get()) {
			std::fprintf(stderr, "%s: FATAL: %s\n", prog, "Unable to open virtual terminal back-end.");
			throw EXIT_FAILURE;
		}
		if (has_display_device) {
			FileDescriptorOwner buffer_fd(open_read_at(vt_dir_fd.get(), "display"));
			if (0 > buffer_fd.get()) {
				die_errno(prog, envs, vt_dirname.c_str(), "display");
			}
			vt.set_buffer_file(fdopen(buffer_fd.get(), "r"));
			if (!vt.query_buffer_file()) {
				die_errno(prog, envs, vt_dirname.c_str(), "display");
			}
			buffer_fd.release();
			append_event(ip, vt.query_buffer_fd(), EVFILT_VNODE, EV_ADD|EV_ENABLE|EV_CLEAR, NOTE_WRITE, 0, nullptr);
		}
		if (has_keyboard_device||mouse_primary) {
			vt.set_input_fd(open_writeexisting_at(vt_dir_fd.get(), "input"));
			if (0 > vt.query_input_fd()) {
				die_errno(prog, envs, vt_dirname.c_str(), "input");
			}
			append_event(ip, vt.query_input_fd(), EVFILT_WRITE, EV_ADD|EV_DISABLE, 0, 0, nullptr);
		}
	}

#if defined(__FreeBSD__) || defined(__DragonFly__)
	if (has_mouse_device) {
		passthrough_mouse.reset(-1);
		std::string consolectlname;
		for (std::list<std::string>::const_iterator p(device_paths.begin()), e(device_paths.end()); p != e; ++p) {
			consolectlname = "consolectl/" + *p;
			passthrough_mouse.reset(open_dir_at(AT_FDCWD, consolectlname.c_str()));
			if (0 <= passthrough_mouse.get()) break;
		}
		if (0 > passthrough_mouse.get())
			passthrough_mouse.reset(open_dir_at(AT_FDCWD, "consolectl/default"));
		if (0 > passthrough_mouse.get())
			std::fprintf(stderr, "%s: WARNING: %s\n", prog, "Unable to open consolectl device for pass-through.");
	}
	if (has_keyboard_device) {
		passthrough_keyboard.reset(-1);
		std::string kbdctlname;
		for (std::list<std::string>::const_iterator p(device_paths.begin()), e(device_paths.end()); p != e; ++p) {
			kbdctlname = "vkbdctl/" + *p;
			passthrough_keyboard.reset(open_dir_at(AT_FDCWD, kbdctlname.c_str()));
			if (0 <= passthrough_keyboard.get()) break;
		}
		if (0 > passthrough_keyboard.get())
			passthrough_keyboard.reset(open_dir_at(AT_FDCWD, "vkbdctl/default"));
		if (0 > passthrough_keyboard.get())
			std::fprintf(stderr, "%s: WARNING: %s\n", prog, "Unable to open vkbdctl device for pass-through.");
		// Although we have opened a vkbdctl device, we have been given a file descriptor for an underlying vkbd "control" device.
		if (0 <= passthrough_keyboard.get()) {
			if (const char * name = fdevname(passthrough_keyboard.get()))
				unlink(name);
		}
	}
#endif

	if (has_mouse_device||has_display_device||mouse_primary) {
		for (std::list<std::string>::const_iterator p(device_paths.begin()), e(device_paths.end()); p != e; ++p) {
			const std::string mousename("mice-aggregate/" + *p);
			mouse_state.set_file_fd(open_readwriteexisting_at(AT_FDCWD, mousename.c_str()));
			if (0 <= mouse_state.query_file_fd()) break;
		}
		if (0 > mouse_state.query_file_fd())
			mouse_state.set_file_fd(open_readwriteexisting_at(AT_FDCWD, "mice-aggregate/default"));
		if (0 > mouse_state.query_file_fd()) {
			die_errno(prog, envs, "mice-aggregate");
		}
		mouse_state.query_buttons(mouse_buttons);	// Synchronize with the initial pre-existing mouse state file.
		if (has_display_device||mouse_primary)
			append_event(ip, mouse_state.query_file_fd(), EVFILT_VNODE, EV_ADD|EV_ENABLE|EV_CLEAR, NOTE_WRITE, 0, nullptr);
	}

	if (has_mouse_device||has_keyboard_device||has_LEDs_device) {
		for (std::list<std::string>::const_iterator p(device_paths.begin()), e(device_paths.end()); p != e; ++p) {
			const std::string keyboardname("keyboards-aggregate/" + *p);
			keyboard_state.set_file_fd(open_readwriteexisting_at(AT_FDCWD, keyboardname.c_str()));
			if (0 <= keyboard_state.query_file_fd()) break;
		}
		if (0 > keyboard_state.query_file_fd())
			keyboard_state.set_file_fd(open_readwriteexisting_at(AT_FDCWD, "keyboards-aggregate/default"));
		if (0 > keyboard_state.query_file_fd()) {
			die_errno(prog, envs, "keyboards-aggregate");
		}
		append_event(ip, keyboard_state.query_file_fd(), EVFILT_VNODE, EV_ADD|EV_ENABLE|EV_CLEAR, NOTE_WRITE, 0, nullptr);
		leds = keyboard_state.query_LEDs();	// Synchronize with the initial pre-existing keyboard state file.
		// Turn numeric lock on if there is no NumLock key.
		// Some madly designed devices (e.g. some USB calculators) fail to work in hosted device mode if the NumLock LED is off; but they also lack a key for turning it on.
		if (!has_NumLock_key && has_LEDs_device)
			keyboard_state.set_locked(KBDMAP_MODIFIER_NUM, true);
		update_LEDs();
	}

	// Auto-configure the keyboard map.
	if (has_keyboard_device) {
		FileStar keymap_file(nullptr);
		for (std::list<std::string>::const_iterator p(device_paths.begin()), e(device_paths.end()); p != e; ++p) {
			const std::string filename("kbdmaps/" + *p);
			keymap_file = std::fopen(filename.c_str(), "r");
			if (keymap_file) break;
		}
		if (!keymap_file)
			keymap_file = std::fopen("kbdmaps/default", "r");
		if (keymap_file) {
			wipe(map);
			kbdmap_entry * p(&map[0][0]), * const e(p + sizeof map/sizeof *p);
			while (!std::feof(keymap_file)) {
				if (p >= e) break;
				uint32_t v[24] = { 0 };
				std::fread(v, sizeof v/sizeof *v, sizeof *v, keymap_file);
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
		} else
		{
			set_default(map);
			overlay_group2_latch(map);
		}
	}
}

void
Main::load_fonts(const FontSpecList & fonts)
{
	for (FontSpecList::const_iterator n(fonts.begin()); fonts.end() != n; ++n) {
		if (-2 == n->weight) {
			FontLoader::LoadVTFont(prog, envs, font, true, n->slant, n->name.c_str());
		} else
		if (-1 == n->weight) {
			FontLoader::LoadVTFont(prog, envs, font, false, n->slant, n->name.c_str());
		} else
		if (0 <= n->weight) {
			FontLoader::LoadRawFont(prog, envs, font, static_cast<CombinedFont::Font::Weight>(n->weight), n->slant, n->name.c_str());
		}
	}
}

inline
void
Main::send_character_input(
	char32_t ch,
	bool accelerator
) {
	if (UnicodeCategorization::IsMarkEnclosing(ch)
	||  UnicodeCategorization::IsMarkNonSpacing(ch)
	) {
		// Per ISO 9995-3 there are certain pairs of dead keys that make other dead keys.
		// Per DIN 2137, this happens on the fly as the keys are typed, so we only need to test combining against the preceding key.
		if (dead_keys.empty() || !UnicodeKeyboard::combine_dead_keys(dead_keys.back(), ch))
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
			if (UnicodeKeyboard::combine_peculiar_non_combiners(*i, ch))
				i = dead_keys.erase(i);
			else
				++i;
		// The standard thing to do at this point is to renormalize into Unicode Normalization Form C (NFC).
		// As explained in the manual at length (q.v.), we don't do full Unicode Normalization.
		std::stable_sort(dead_keys.begin(), dead_keys.end(), UnicodeKeyboard::lower_combining_class);
		for (DeadKeysList::iterator i(dead_keys.begin()); i != dead_keys.end(); )
			if (UnicodeKeyboard::combine_unicode(*i, ch))
				i = dead_keys.erase(i);
			else
				++i;
		// If we have any dead keys left, we emit them standalone, before the composed key (i.e. in original typing order, as best we can).
		// We don't send the raw combiners, as in the ZWNJ case.
		// Instead, we make use of the fact that ISO 9995-3 defines rules for combining with the space character to produce a composed spacing character.
		for (DeadKeysList::iterator i(dead_keys.begin()); i != dead_keys.end(); i = dead_keys.erase(i)) {
			char32_t s(SPC);
			if (UnicodeKeyboard::combine_peculiar_non_combiners(*i, s))
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
Main::send_screen_key (
	const uint32_t s,
	const uint8_t m
) {
	clear_dead_keys();
	vt.WriteInputMessage(MessageForSession(s, m));
}

inline
void
Main::send_system_key (
	const uint32_t k,
	const uint8_t m
) {
	clear_dead_keys();
	vt.WriteInputMessage(MessageForSystemKey(k, m));
}

inline
void
Main::send_consumer_key (
	const uint32_t k,
	const uint8_t m
) {
	clear_dead_keys();
	vt.WriteInputMessage(MessageForConsumerKey(k, m));
}

inline
void
Main::send_extended_key (
	const uint32_t k,
	const uint8_t m
) {
	clear_dead_keys();
	vt.WriteInputMessage(MessageForExtendedKey(k, m));
}

inline
void
Main::send_function_key (
	const uint32_t k,
	const uint8_t m
) {
	clear_dead_keys();
	vt.WriteInputMessage(MessageForFunctionKey(k, m));
}

void
Main::mouse_button (const uint16_t button, bool value)
{
	if (active)
		mouse_state.button(button, value);
	else {
		std::clog << "DEBUG: Passthrough mouse button " << button << " value " << value << "\n";
#if defined(__FreeBSD__) || defined(__DragonFly__)
		if (0 <= passthrough_mouse.get()) {
			mouse_info_t i = {};
			i.operation = MOUSE_BUTTON_EVENT;
			i.u.event.id = button;
			i.u.event.value = value ? 1U /*single click*/ : 0U;
			ioctl(passthrough_mouse.get(), CONS_MOUSECTL, &i);
		}
#endif
	}
}

void
Main::mouse_relpos (const MouseAxis axis, int mickeys)
{
	if (active) {
		switch (axis) {
			case V_SCROLL:	mouse_state.wheel(0U, mickeys); break;
			case H_SCROLL:	mouse_state.wheel(1U, mickeys); break;
			case AXIS_W:	mouse_state.relpos(0U, mickeys); break;
			case AXIS_X:	mouse_state.relpos(1U, mickeys); break;
			case AXIS_Y:	mouse_state.relpos(2U, mickeys); break;
			case AXIS_Z:	mouse_state.relpos(3U, mickeys); break;
			case AXIS_INVALID:	break;
		}
	} else {
#if defined(__FreeBSD__) || defined(__DragonFly__)
		if (0 <= passthrough_mouse.get()) {
			mouse_info_t i = {};
			i.operation = MOUSE_MOTION_EVENT;
			switch (axis) {
#if 0 // Not supported by FreeBSD
				case AXIS_W:	i.u.data.w = mickeys; break;
#else
				case AXIS_W:	break;
#endif
				case AXIS_X:	i.u.data.x = mickeys; break;
				case AXIS_Y:	i.u.data.y = mickeys; break;
				case AXIS_Z:	i.u.data.z = mickeys; break;
				case H_SCROLL: case V_SCROLL: case AXIS_INVALID:	break;
#if 0 // Generates a compiler warning, when we explicitly list impossible states to avoid a different compiler warning.
				default:	break;
#endif
			}
			ioctl(passthrough_mouse.get(), CONS_MOUSECTL, &i);
		}
#endif
	}
}

void
Main::mouse_abspos (const MouseAxis axis, unsigned long amount, unsigned long maximum)
{
	if (active) {
		amount = static_cast<unsigned long>(amount * (static_cast<long double>(std::numeric_limits<unsigned long>::max()) + 1U) / (static_cast<long double>(maximum) + 1U));
		switch (axis) {
			case H_SCROLL: case V_SCROLL: case AXIS_INVALID:
#if 0 // Generates a compiler warning, when we explicitly list impossible states to avoid a different compiler warning.
			default:
#endif
				std::clog << "DEBUG: Unknown touchpad axis " << axis << " position " << amount << "/" << maximum << " absolute\n";
				break;
			case AXIS_W:	mouse_state.abspos(0U, amount); break;
			case AXIS_X:	mouse_state.abspos(1U, amount); break;
			case AXIS_Y:	mouse_state.abspos(2U, amount); break;
			case AXIS_Z:	mouse_state.abspos(3U, amount); break;
		}
	} else {
#if defined(__FreeBSD__) || defined(__DragonFly__)
		if (0 <= passthrough_mouse.get()) {
			// Yes, this does not work; fundamentally because FreeBSD's common level 1 mouse protocol does not have the notion of absolute motion.
			mouse_info_t i = {};
			i.operation = MOUSE_MOVEABS;
			switch (axis) {
				case H_SCROLL: case V_SCROLL: case AXIS_INVALID:	break;
#if 0 // Generates a compiler warning, when we explicitly list impossible states to avoid a different compiler warning.
				default:	break;
#endif
#if 0 // Not supported by FreeBSD
				case AXIS_W:	i.u.data.w = amount; break;
#else
				case AXIS_W:	break;
#endif
				case AXIS_X:	i.u.data.x = amount; break;
				case AXIS_Y:	i.u.data.y = amount; break;
				case AXIS_Z:	i.u.data.z = amount; break;
			}
			ioctl(passthrough_mouse.get(), CONS_MOUSECTL, &i);
		}
#endif
	}
}

/// Actions can be simple transmissions of an input message, or complex procedures with the input method.
inline
void
Main::handle_keyboard (
	const uint8_t row,	///< caller has validated against the keyboard map array bounds
	const uint8_t col,	///< caller has validated against the keyboard map array bounds
	uint8_t v
) {
	const kbdmap_entry & action(map[row][col]);
	const std::size_t o(keyboard_state.query_kbdmap_parameter(action.cmd));
	if (o >= sizeof action.p/sizeof *action.p) return;
	const uint32_t act(action.p[o] & KBDMAP_ACTION_MASK);
	const uint32_t cmd(action.p[o] & ~KBDMAP_ACTION_MASK);

	switch (act) {
		default:	break;
		case KBDMAP_ACTION_UCS3:
		{
			if (v < 1U) break;
			send_character_input(cmd, keyboard_state.accelerator());
			keyboard_state.unlatch_all();
			break;
		}
		case KBDMAP_ACTION_MODIFIER:
		{
			const uint32_t k((cmd & 0x00FFFF00) >> 8U);
			const uint32_t c(cmd & 0x000000FF);
			switch (c) {
				case KBDMAP_MODIFIER_CMD_MOMENTARY:
					keyboard_state.set_pressed(k, 0U != v);
					break;
				case KBDMAP_MODIFIER_CMD_LATCH:
					if (0U != v) keyboard_state.latch(k);
					break;
				case KBDMAP_MODIFIER_CMD_LOCK:
					if (1U == v) keyboard_state.invert_lock(k);
					break;
			}
			break;
		}
		case KBDMAP_ACTION_SCREEN:
		{
			if (v < 1U) break;
			const uint32_t s((cmd & 0x00FFFF00) >> 8U);
			send_screen_key(s, keyboard_state.modifiers());
			keyboard_state.unlatch_all();
			break;
		}
		case KBDMAP_ACTION_SYSTEM:
		{
			if (v < 1U) break;
			const uint32_t k((cmd & 0x00FFFF00) >> 8U);
			send_system_key(k, keyboard_state.modifiers());
			keyboard_state.unlatch_all();
			break;
		}
		case KBDMAP_ACTION_CONSUMER:
		{
			if (v < 1U) break;
			const uint32_t k((cmd & 0x00FFFF00) >> 8U);
			send_consumer_key(k, keyboard_state.modifiers());
			keyboard_state.unlatch_all();
			break;
		}
		case KBDMAP_ACTION_EXTENDED:
		{
			if (v < 1U) break;
			const uint32_t k((cmd & 0x00FFFF00) >> 8U);
			send_extended_key(k, keyboard_state.modifiers());
			keyboard_state.unlatch_all();
			break;
		}
		case KBDMAP_ACTION_EXTENDED1:
		{
			if (v < 1U) break;
			const uint32_t k((cmd & 0x00FFFF00) >> 8U);
			send_extended_key(k, keyboard_state.nolevel2_modifiers());
			keyboard_state.unlatch_all();
			break;
		}
		case KBDMAP_ACTION_FUNCTION:
		{
			if (v < 1U) break;
			const uint32_t k((cmd & 0x00FFFF00) >> 8U);
			send_function_key(k, keyboard_state.modifiers());
			keyboard_state.unlatch_all();
			break;
		}
		case KBDMAP_ACTION_FUNCTION1:
		{
			if (v < 1U) break;
			const uint32_t k((cmd & 0x00FFFF00) >> 8U);
			send_function_key(k, keyboard_state.nolevel_nogroup_noctrl_modifiers());
			keyboard_state.unlatch_all();
			break;
		}
	}
}

#if defined(__FreeBSD__) || defined(__DragonFly__)
Main::PassthroughKeyboardMap
Main::passthrough_keymap =
{
	// FIXME: This is all wrong.
	// 00: ISO 9995 "E" row
	{
		0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	},
	// 01: ISO 9995 "D" row
	{
		0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	},
	// 02: ISO 9995 "C" row
	{
		0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	},
	// 03: ISO 9995 "B" row
	{
		0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	},
	// 04: Modifier row
	{
		0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
 	},
	// 05: ISO 9995 "A" row
	{
		0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	},
	// 06: Cursor/editing "row"
	{
		0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	},
	// 07: Calculator keypad "row" 1
	{
		0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	},
	// 08: Calculator keypad "row" 2
	{
		0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	},
	// 09: Function row 1
	{
		0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	},
	// 0A: Function row 2
	{
		0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	},
	// 0B: Function row 3
	{
		0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	},
	// 0C: Function row 4
	{
		0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	},
	// 0D: System Commands keypad "row"
	{
		0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	},
	// 0E: Application Commands keypad "row" 1
	{
		0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	},
	// 0F: Application Commands keypad "row" 2
	{
		0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	},
	// 10: Application Commands keypad "row" 3
	{
		0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	},
	// 11: Consumer keypad "row" 1
	{
		0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	},
	// 12: Consumer keypad "row" 2
	{
		0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
	},
};
#endif

void
Main::handle_unicode (
	const char32_t c
) {
	send_character_input(c, keyboard_state.accelerator());
	keyboard_state.unlatch_all();
}

void
Main::handle_keyboard (
	const uint16_t index,	///< by common convention, 0xFFFF is bypass
	uint8_t v
) {
	if (0xFFFF == index) return;
	const uint8_t row(index >> 8U);
	const uint8_t col(index & 0xFF);
	if (row >= KBDMAP_ROWS) return;
	if (col >= KBDMAP_COLS) return;
	if (active)
		handle_keyboard(row, col, v);
	else {
#if defined(__FreeBSD__) || defined(__DragonFly__)
		if (0 <= passthrough_keyboard.get()) {
			const unsigned int scancode(passthrough_keymap[row][col] | (v ? 0x80 : 0x00));
			write(passthrough_keyboard.get(), &scancode, sizeof scancode);
		}
#endif
	}
}

inline
void
Main::update_LEDs()
{
	const KeyboardLEDs newleds(keyboard_state.query_LEDs());
	if (leds != newleds) {
		for (HIDList::iterator j(inputs.begin()); inputs.end() != j; ++j) {
			HID & input(**j);
			input.set_LEDs(newleds);
		}
		leds = newleds;
	}
}

inline
void
Main::update_locators()
{
	unsigned long x(mouse_state.query_pos(1U));
	unsigned long y(mouse_state.query_pos(2U));
	unsigned long z(mouse_state.query_pos(3U));
	const SharedHODResources::coordinate col(SharedHODResources::pixel_to_column(x));
	const SharedHODResources::coordinate row(SharedHODResources::pixel_to_row(y));
	const SharedHODResources::coordinate dep(SharedHODResources::pixel_to_depth(z));

	// Inform output devices.

	for (HODList::iterator j(outputs.begin()); outputs.end() != j; ++j) {
		HOD & output(**j);
		if (mouse_col != col)
			output.set_pointer_col(col);
		if (mouse_row != row)
			output.set_pointer_row(row);
		if (mouse_dep != dep)
			output.set_pointer_dep(dep);
	}

	// Inform the VT back-end.

	if (mouse_primary) {
		const uint8_t modifiers(keyboard_state.modifiers());
		bool new_mouse_buttons[MouseState::NUM_BUTTONS];
		mouse_state.query_buttons(new_mouse_buttons);
		for (unsigned i(0U); i < sizeof mouse_buttons/sizeof *mouse_buttons; ++i) {
			const bool v(new_mouse_buttons[i]);
			if (v != mouse_buttons[i]) {
				vt.WriteInputMessage(MessageForMouseButton(i, v, modifiers));
				mouse_buttons[i] = v;
			}
		}
		if (mouse_col != col)
			vt.WriteInputMessage(MessageForMouseColumn(col, modifiers));
		if (mouse_row != row)
			vt.WriteInputMessage(MessageForMouseRow(row, modifiers));
		if (mouse_dep != dep)
			vt.WriteInputMessage(MessageForMouseDepth(dep, modifiers));
		for (unsigned i(0U); i < MouseState::NUM_WHEELS; ++i) {
			int32_t d(mouse_state.get_and_reset_wheel(i));
			while (d) {
				const int8_t v(d < -128 ? -128 : d > 127 ? 127 : d);
				vt.WriteInputMessage(MessageForMouseWheel(i, v, modifiers));
				d -= v;
			}
		}
	}

	mouse_col = col;
	mouse_row = row;
	mouse_dep = dep;
}

void
Main::raise_acquire_signal(
) {
	usr2_signalled = true;
}

void
Main::raise_release_signal(
) {
	usr1_signalled = true;
}

void
Main::capture_signals(
	int rs,
	int as
) {
	append_event(ip, rs, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, as, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
}

void
Main::add_device(
	HID * dev
) {
	inputs.push_back(std::shared_ptr<HID>(dev));
	append_event(ip, dev->query_input_fd(), EVFILT_READ, EV_ADD|EV_DISABLE, 0, 0, nullptr);
}

void
Main::add_device(
	HOD * dev
) {
	outputs.push_back(std::shared_ptr<HOD>(dev));
}

inline
bool
Main::has_idle_work(
) {
	return vt.query_reload_needed();
}

inline
void
Main::do_idle_work(
) {
	if (vt.query_reload_needed()) {
		vt.reload();
		for (HODList::iterator j(outputs.begin()); outputs.end() != j; ++j) {
			HOD & output(**j);
			output.set_refresh_needed();
		}
	}
}

void
Main::loop (
) {
	ReserveSignalsForKQueue kqueue_reservation(SIGTERM, SIGINT, SIGHUP, SIGPIPE, SIGWINCH, SIGUSR1, SIGUSR2, 0);
	PreventDefaultForFatalSignals ignored_signals(SIGTERM, SIGINT, SIGHUP, SIGPIPE, SIGUSR1, SIGUSR2, 0);
	append_event(ip, SIGWINCH, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ip, SIGPIPE, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);

	for (HIDList::iterator j(inputs.begin()); inputs.end() != j; ++j) {
		HID & input(**j);
		set_non_blocking(input.query_input_fd(), false);
	}

	const struct timespec immediate_timeout = { 0, 0 };
	bool eof(false);

	while (true) {
		if (terminate_signalled||interrupt_signalled||hangup_signalled)
			break;
		if (eof)
			break;
		if (usr1_signalled)  {
			// release commanded.
			usr1_signalled = false;
			if (active) {
				for (HIDList::iterator i(inputs.begin()); inputs.end() != i; ++i) {
					HID & input(**i);
					append_event(ip, input.query_input_fd(), EVFILT_READ, EV_DISABLE, 0, 0, nullptr);
					input.set_exclusive(false);
				}
				for (HODList::iterator j(outputs.begin()); outputs.end() != j; ++j) {
					HOD & output(**j);
					output.unmap();
				}
				if (switching_controller) switching_controller->permit_switch_from();
				active = false;
			}
		}
		if (usr2_signalled) {
			// acquire notified.
			usr2_signalled = false;
			if (!active) {
				if (switching_controller) switching_controller->acknowledge_switch_to();
				for (HIDList::iterator j(inputs.begin()); inputs.end() != j; ++j) {
					HID & input(**j);
					append_event(ip, input.query_input_fd(), EVFILT_READ, EV_ENABLE, 0, 0, nullptr);
					input.set_exclusive(true);
				}
				update_LEDs();
				update_locators();
				for (HODList::iterator j(outputs.begin()); outputs.end() != j; ++j) {
					HOD & output(**j);
					output.map();
					output.set_refresh_needed();
					output.invalidate_cur();
				}
				active = true;
			}
		}
		for (HODList::iterator j(outputs.begin()); outputs.end() != j; ++j) {
			HOD & output(**j);
			output.handle_refresh_event();
			if (active)
				output.handle_update_event();
		}

		struct kevent p[512];
		const int rc(kevent(queue.get(), ip.data(), ip.size(), p, sizeof p/sizeof *p, has_idle_work() ? &immediate_timeout : nullptr));
		ip.clear();

		if (0 > rc) {
			if (EINTR == errno) continue;
			// Destructors of the device objects, addressed by std::shared_ptrs, will call restore().
			die_errno(prog, envs, "kevent");
		}

		if (0 == rc) {
			do_idle_work();
			continue;
		}

		for (std::size_t i(0); i < static_cast<std::size_t>(rc); ++i) {
			const struct kevent & e(p[i]);
			switch (e.filter) {
				case EVFILT_VNODE:
					if (0 <= static_cast<int>(e.ident)) {
						if (vt.query_buffer_fd() == static_cast<int>(e.ident))
							vt.set_reload_needed();
						if (keyboard_state.query_file_fd() == static_cast<int>(e.ident))
							update_LEDs();
						if (mouse_state.query_file_fd() == static_cast<int>(e.ident))
							update_locators();
					}
					break;
				case EVFILT_SIGNAL:
					handle_signal(e.ident);
					break;
				case EVFILT_READ:
					if (0 <= static_cast<int>(e.ident)) {
						if (EV_EOF & e.flags) eof = true;
						if (EV_ERROR & e.flags)
							std::fprintf(stderr, "%s: ERROR: %s: %s\n", prog, "kevent", std::strerror(e.data));
						else
						for (HIDList::iterator j(inputs.begin()); inputs.end() != j; ++j) {
							HID & input(**j);
							if (input.query_input_fd() == static_cast<int>(e.ident))
								input.handle_input_events();
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
				append_event(ip, vt.query_input_fd(), EVFILT_WRITE, EV_ENABLE, 0, 0, nullptr);
				vt.set_polling_for_write(true);
			}
		} else {
			if (vt.query_polling_for_write()) {
				append_event(ip, vt.query_input_fd(), EVFILT_WRITE, EV_DISABLE, 0, 0, nullptr);
				vt.set_polling_for_write(false);
			}
		}
	}
}
