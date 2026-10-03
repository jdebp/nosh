/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_VIRTUALTERMINALREALIZER_H)
#define INCLUDE_VIRTUALTERMINALREALIZER_H

#include <list>
#include <set>
#include <string>
#include <memory>
#include <termios.h>
#include "kbdmap.h"
#include "kqueue_common.h"
#include "GraphicsInterface.h"
#include "CompositeFont.h"
#include "TUIVIO.h"
#include "TUIDisplayCompositor.h"
#include "VirtualTerminalBackEnd.h"

class Monospace16x16Font;
struct ProcessEnvironment;

namespace VirtualTerminalRealizer {

/// \brief An abstraction for a set of keyboard LEDs based upon what modifiers are on
struct KeyboardLEDs {
	KeyboardLEDs(
		bool num_lock,
		bool caps_lock,
		bool shift2_lock,
		bool shift3_lock,
		bool group2,
		bool control,
		bool level2,
		bool level3,
		bool super,
		bool alt
	) :
		bits(0U
		| (num_lock ? NUM_LOCK : 0U)
		| (caps_lock ? CAPS_LOCK : 0U)
		| (shift2_lock ? SHIFT2_LOCK : 0U)
		| (shift3_lock ? SHIFT3_LOCK : 0U)
		| (group2 ? GROUP2 : 0U)
		| (control ? CONTROL : 0U)
		| (level2 ? SHIFT2 : 0U)
		| (level3 ? SHIFT3 : 0U)
		| (super ? SUPER : 0U)
		| (alt ? ALT : 0U)
		)
	{
	}
	KeyboardLEDs() : bits(-1U) {}
	bool num_lock() const { return bits & NUM_LOCK; }
	bool caps_lock() const { return bits & CAPS_LOCK; }
	bool shift2_lock() const { return bits & SHIFT2_LOCK; }
	bool shift3_lock() const { return bits & SHIFT3_LOCK; }
	bool group2() const { return bits & GROUP2; }
	bool control() const { return bits & CONTROL; }
	bool shift2() const { return bits & SHIFT2; }
	bool shift3() const { return bits & SHIFT3; }
	bool super() const { return bits & SUPER; }
	bool alt() const { return bits & ALT; }
	bool operator!=(const KeyboardLEDs & o) const { return o.bits != bits; }
protected:
	enum {
		NUM_LOCK	= 0x0001,
		CAPS_LOCK	= 0x0002,
		SHIFT2_LOCK	= 0x0004,
		SHIFT3_LOCK	= 0x0008,
		GROUP2		= 0x0010,
		CONTROL		= 0x0020,
		SHIFT2		= 0x0040,
		SHIFT3		= 0x0080,
		SUPER		= 0x0100,
		ALT		= 0x0200,
	};
	uint_fast8_t bits;
} ;

class SharedStateBase {
public:
	SharedStateBase() : file(-1) {}
	~SharedStateBase() {}
	/// \name Polling for state changes
	/// @{
	int query_file_fd() const { return file.get(); }
	/// @}

protected:
	FileDescriptorOwner file;
	ssize_t pread(void * d, size_t s, off_t o) const { return ::pread(file.get(), d, s, o); }
	ssize_t pwrite(const void * d, size_t s, off_t o) const { return ::pwrite(file.get(), d, s, o); }
};

/// \brief keyboard modifier state persisted to a (possibly shared) file
class KeyboardModifierState :
	public SharedStateBase
{
public:
	KeyboardModifierState();
	~KeyboardModifierState();
	/// \name Connecting to state files.
	/// @{
	void set_file_fd(int fd);
	/// @}

	/// \name Querying the (shared) state information
	/// @{
	KeyboardLEDs query_LEDs() const;
	uint8_t modifiers() const;
	uint8_t nolevel2_modifiers() const;
	uint8_t nolevel_nogroup_noctrl_modifiers() const;
	bool accelerator() const;
	std::size_t query_kbdmap_parameter (const uint32_t cmd) const;
	/// @}

	/// \name State change events
	/// @{
	void set_pressed(std::size_t k, bool v);
	void set_locked(std::size_t k, bool v);
	void invert_lock(std::size_t k);
	void latch(std::size_t k);
	void unlatch_all();
	/// @}

protected:
	enum { NUM_MODIFIERS = 32U };
	static_assert(NUM_MODIFIERS >= KBDMAP_TOTAL_MODIFIERS, "The keyboard state must have at least as many modifiers as the keyboard maps.");
	static_assert(NUM_MODIFIERS <= 256U, "The keyboard state must be able to store the modifier number in a byte.");
	using SharedStateBase::pread;
	using SharedStateBase::pwrite;

	/// \brief This is local state private to each HID; and also the base of the shared global state.
	/// The global state is a persistent accumulated count of all of the local states.
	/// The local state is transient for the lifetime of the (pluggable) device.
	/// This places an upper limit of 256 modifier keys of a single type in any single shared system.
	struct lheader {
		uint8_t pressed[NUM_MODIFIERS];
		void clear();
	} local;
	/// \brief The header of the persistent global state file.
	struct header : public lheader {
		uint8_t latched[NUM_MODIFIERS];
		uint8_t locked [NUM_MODIFIERS];
		void clear();
	};
	ssize_t pread(header & h, off_t o) const { return pread(&h, sizeof h, o); }
	ssize_t pwrite(const header & h, off_t o) const { return pwrite(&h, sizeof h, o); }
	/// \brief The remainder of the global state file is an accumulation of state changes.
	/// These are rolled up and nulled every time that a snapshot is taken.
	/// When we attach to a fresh file, we truncate trailing nulled state changes.
	struct event {
		enum { NOWT = 0U, PRESS = 'p', LATCH = 'l', LOCK = 'k' };
		uint8_t command;
		uint8_t value;	// A bool is wasteful of file bytes here, but this is really a boolean.
		uint8_t modifier;
	};
	ssize_t pread(event & e, off_t o) const { return pread(&e, sizeof e, o); }
	ssize_t pwrite(const event & e, off_t o) const { return pwrite(&e, sizeof e, o); }

	/// \brief A lot of convenience functions that wrap the global state once it has been snapshotted.
	struct Snapshot : public header {
		operator KeyboardLEDs() const { return KeyboardLEDs(num_lock(), caps_lock(), level2_lock(), level3_lock(), group2(), control(), level2(), level3(), super(), alt()); }

		std::size_t semi_shiftable_index() const;
		std::size_t shiftable_index() const;
		std::size_t capsable_index() const;
		std::size_t numable_index() const;
		std::size_t funcable_index() const;

		bool is_latched(std::size_t k) const { return !!latched[k]; }
		bool is_locked(std::size_t k) const { return !!locked[k]; }
		bool is_latched_or_locked(std::size_t k) const { return !!locked[k] || !!latched[k]; }
		bool is_any(std::size_t k) const { return !!pressed[k] || !!locked[k] || !!latched[k]; }

		bool level2_lock() const { return is_latched_or_locked(KBDMAP_MODIFIER_LEVEL2); }
		bool level3_lock() const { return is_latched_or_locked(KBDMAP_MODIFIER_LEVEL3); }
		bool num_lock() const { return is_latched_or_locked(KBDMAP_MODIFIER_NUM); }
		bool caps_lock() const { return is_latched_or_locked(KBDMAP_MODIFIER_CAPS); }

		bool level2() const { return is_any(KBDMAP_MODIFIER_1ST_LEVEL2)||is_any(KBDMAP_MODIFIER_2ND_LEVEL2); }
		bool level3() const { return is_any(KBDMAP_MODIFIER_1ST_LEVEL3)||is_any(KBDMAP_MODIFIER_2ND_LEVEL3); }
		bool group2() const { return is_any(KBDMAP_MODIFIER_1ST_GROUP2); }
		bool control() const { return is_any(KBDMAP_MODIFIER_1ST_CONTROL)||is_any(KBDMAP_MODIFIER_2ND_CONTROL); }
		bool super() const { return is_any(KBDMAP_MODIFIER_1ST_SUPER)||is_any(KBDMAP_MODIFIER_2ND_SUPER); }
		bool alt() const { return is_any(KBDMAP_MODIFIER_1ST_ALT); }
	};

	off_t find_append_point_while_locked() const;
	void take_snapshot_while_locked(header &) const;
	void append(const event &) const;
	void un_press();
	void re_press();
};

enum MouseAxis { AXIS_W, AXIS_X, AXIS_Y, AXIS_Z, H_SCROLL, V_SCROLL, AXIS_INVALID = -1 };

/// \brief mouse location and button state persisted to a (possibly shared) file
class MouseState :
	public SharedStateBase
{
public:
	enum { NUM_AXES = 4U, NUM_BUTTONS = 32U, NUM_WHEELS = 2U };

	MouseState();
	~MouseState();
	/// \name Connecting to state files and polling for state changes
	/// @{
	void set_file_fd(int fd);
	/// @}

	/// \name Querying the (shared) state information
	/// @{
	unsigned long query_pos(unsigned axis) const;
	void query_buttons(bool pressed[NUM_BUTTONS]) const;
	int32_t get_and_reset_wheel(unsigned wheel);
	/// @}

	/// \name State change events
	/// @{
	void relpos(unsigned axis, signed long delta);
	void abspos(unsigned axis, unsigned long amount);
	void button(unsigned button, bool value);
	void wheel(unsigned wheel, int32_t delta);
	/// @}

protected:
	static_assert(NUM_BUTTONS <= 256U, "The mouse state must be able to store the button number in a byte.");
	static_assert(NUM_AXES <= 256U, "The mouse state must be able to store the axis number in a byte.");
	static_assert(NUM_WHEELS <= 256U, "The mouse state must be able to store the wheel number in a byte.");
	using SharedStateBase::pread;
	using SharedStateBase::pwrite;

	/// \brief This is local state private to each HID; and also the base of the shared global state.
	/// The global state is a persistent accumulated count of all of the local states.
	/// The local state is transient for the lifetime of the (pluggable) device.
	/// This places an upper limit of 256 buttons of a single type in any single shared system.
	struct lheader {
		uint8_t		pressed[NUM_BUTTONS];
		void clear();
	} local;
	/// \brief The header of the persistent global state file.
	struct header : public lheader {
		unsigned long	positions[NUM_AXES];
		signed long	offsets[NUM_WHEELS];
		void clear();
	};
	ssize_t pread(header & h, off_t o) const { return pread(&h, sizeof h, o); }
	ssize_t pwrite(const header & h, off_t o) const { return pwrite(&h, sizeof h, o); }
	/// \brief The remainder of the global state file is an accumulation of state changes.
	struct event {
		enum { NOWT = 0U, BUTTON = 'b', ABSPOS = 'a', RELPOS = 'r', WHEEL = 'w' };
		uint8_t command;
		uint8_t index;
		union {
			bool value;
			signed long delta;
			unsigned long position;
		};
	};
	ssize_t pread(event & e, off_t o) const { return pread(&e, sizeof e, o); }
	ssize_t pwrite(const event & e, off_t o) const { return pwrite(&e, sizeof e, o); }

	/// \brief Any convenience functions that wrap the global state once it has been snapshotted.
	struct Snapshot : public header {
	};

	off_t find_append_point_while_locked() const;
	void take_snapshot_while_locked(header &) const;
	void append(const event &) const;
	void un_press();
	void re_press();
};

/// \brief common shared resources for HODs
class SharedHODResources
{
public:
	typedef unsigned short coordinate;

	SharedHODResources(VirtualTerminalBackEnd & t, Monospace16x16Font & f);
	~SharedHODResources();

	VirtualTerminalBackEnd & vt;
	GraphicsInterface gdi;

	typedef GraphicsInterface::GlyphBitmapHandle GlyphBitmapHandle;
	typedef GraphicsInterface::ScreenBitmapHandle ScreenBitmapHandle;

	GlyphBitmapHandle GetPointerGlyphBitmap() const { return mouse_glyph_handle; }
	GlyphBitmapHandle GetCursorGlyphBitmap(CursorSprite::glyph_type t) const;
	GlyphBitmapHandle GetCachedGlyphBitmap(uint32_t character, CharacterCell::attribute_type attributes);

	static coordinate pixel_to_column(unsigned long x) { return x / CHARACTER_PIXEL_WIDTH; }
	static coordinate pixel_to_row(unsigned long y) { return y / CHARACTER_PIXEL_HEIGHT; }
	static coordinate pixel_to_depth(unsigned long z) { return z / CHARACTER_PIXEL_DEPTH; }
	static unsigned long column_to_pixel(coordinate x) { return x * CHARACTER_PIXEL_WIDTH; }
	static unsigned long row_to_pixel(coordinate y) { return y * CHARACTER_PIXEL_HEIGHT; }
	static unsigned long depth_to_pixel(coordinate z) { return z * CHARACTER_PIXEL_DEPTH; }

	void BitBLT(ScreenBitmapHandle s, GlyphBitmapHandle g, coordinate row, coordinate col, const ColourPair & colour) { gdi.BitBLT(s, g, row * CHARACTER_PIXEL_WIDTH, col * CHARACTER_PIXEL_HEIGHT, colour); }
	void BitBLTMask(ScreenBitmapHandle s, GlyphBitmapHandle g, GlyphBitmapHandle m, coordinate row, coordinate col, const ColourPair colours[2]) { gdi.BitBLTMask(s, g, m, row * CHARACTER_PIXEL_WIDTH, col * CHARACTER_PIXEL_HEIGHT, colours); }
	void BitBLTAlpha(ScreenBitmapHandle s, GlyphBitmapHandle g, coordinate row, coordinate col, const CharacterCell::colour_type & c) { gdi.BitBLTAlpha(s, g, row * CHARACTER_PIXEL_WIDTH, col * CHARACTER_PIXEL_HEIGHT, c); }
protected:
	enum { CHARACTER_PIXEL_WIDTH = 16LU, CHARACTER_PIXEL_HEIGHT = 16LU, CHARACTER_PIXEL_DEPTH = 1LU };
	Monospace16x16Font & font;

	struct GlyphCacheEntry {
		GlyphCacheEntry(GlyphBitmapHandle ha, uint32_t ch, CharacterCell::attribute_type a) : handle(ha), character(ch), attributes(a) {}
		GlyphBitmapHandle handle;
		uint32_t character;
		CharacterCell::attribute_type attributes;
	};
	typedef std::list<GlyphCacheEntry> GlyphCache;

	enum { MAX_CACHED_GLYPHS = 16384U };
	GlyphCache glyph_cache;		///< a recently-used cache of handles to 2-colour bitmaps
	const GlyphBitmapHandle mouse_glyph_handle;
	const GlyphBitmapHandle underline_glyph_handle;
	const GlyphBitmapHandle underover_glyph_handle;
	const GlyphBitmapHandle bar_glyph_handle;
	const GlyphBitmapHandle box_glyph_handle;
	const GlyphBitmapHandle block_glyph_handle;
	const GlyphBitmapHandle star_glyph_handle;
	const GlyphBitmapHandle mirrorl_glyph_handle;

	void ReduceCacheSizeTo(std::size_t size);
};

/// \brief common shared resources for HIDs
class SharedHIDResources
{
public:
	SharedHIDResources() {}
	~SharedHIDResources() {}

	/// \name API for derived classes to flesh out
	/// @{
	virtual void handle_unicode (const char32_t c) = 0;
	virtual void handle_keyboard (const uint16_t index, uint8_t v) = 0;
	virtual void mouse_button (const uint16_t index, bool v) = 0;
	virtual void mouse_relpos (const MouseAxis, int) = 0;
	virtual void mouse_abspos (const MouseAxis, unsigned long, unsigned long) = 0;
	/// @}
};

/// \brief the base abstract class for HODs, used as an interface to concrete HOD derived classes by the main realizer
class HOD
{
public:
	/// \brief common options for HODs
	struct Options
	{
		Options();

		unsigned long quadrant;	///< 0, 1, 2, or 3
		bool wrong_way_up;	///< causes coordinate transforms between c and vt
		bool faint_as_colour;
		bool bold_as_colour;
		bool has_pointer;
	};

	HOD(SharedHODResources &, const Options & opts);
	virtual ~HOD();

	void set_refresh_needed() { refresh_needed = true; }
	void set_update_needed() { update_needed = true; }
	void invalidate_cur() { c.touch_all(); }
	void handle_update_event();
	void handle_refresh_event();
	void set_pointer_col(const SharedHODResources::coordinate col);
	void set_pointer_row(const SharedHODResources::coordinate row);
	void set_pointer_dep(const SharedHODResources::coordinate dep);

	/// \brief API for derived classes to flesh out
	/// @{
	virtual void unmap() = 0;
	virtual void map() = 0;
	virtual GraphicsInterface::PixelCoordinate query_yres() const = 0;
	virtual GraphicsInterface::PixelCoordinate query_xres() const = 0;
	/// @}

protected:
	const Options & options;
	SharedHODResources & shared;

	bool refresh_needed, update_needed;
	void position_vt_visible_area ();
	void compose_new_from_vt ();
	void paint_changed_cells_onto_framebuffer();

	SharedHODResources::GlyphBitmapHandle GetCursorGlyphBitmap() const { return shared.GetCursorGlyphBitmap(c.query_cursor_glyph()); }
	SharedHODResources::GlyphBitmapHandle GetPointerGlyphBitmap() const { return shared.GetPointerGlyphBitmap(); }
	virtual SharedHODResources::ScreenBitmapHandle GetScreenBitmap() const = 0;	///< Every derived class has its own type of screen bitmap.

	SharedHODResources::coordinate screen_y, screen_x;
	VirtualTerminalBackEnd::xy visible_origin;
	VirtualTerminalBackEnd::wh visible_size;
	TUIDisplayCompositor c;
	TUIVIO vio;
};

/// \brief the base abstract class for HIDs, used as an interface to concrete HID derived classes by the main realizer
class HID
{
public:
	virtual ~HID() { restore(); }

	int query_input_fd() const { return device.get(); }
	/// \name API for derived classes to flesh out
	/// @{
	virtual void set_LEDs(const KeyboardLEDs &) = 0;
	virtual void handle_input_events() = 0;
	virtual bool set_exclusive(bool);	// not pure virtual because it is optional for derived classes to implement this
	virtual void save();	// not pure virtual because it is optional for derived classes to implement this
	virtual void set_mode();	// not pure virtual because it is optional for derived classes to implement this
	/// @}

protected:
	HID(SharedHIDResources & r, FileDescriptorOwner & fd) : shared(r), device(fd.release()) {}
	/// \name API for derived classes to flesh out
	/// @{
	virtual void restore();	// not pure virtual because it is optional for derived classes to implement this
	/// @}
	SharedHIDResources & shared;
	const FileDescriptorOwner device;

	void handle_mouse_abspos(const MouseAxis axis, const unsigned long abspos, const unsigned long maximum) ;
	void handle_mouse_relpos(const MouseAxis axis, int32_t amount) ;
	void handle_mouse_button(const uint16_t button, const bool value) ;

};

/// \brief HIDs that are character devices with a terminal line discipline
class HIDWithLineDiscipline :
	public HID
{
public:
	~HIDWithLineDiscipline();

protected:
	HIDWithLineDiscipline(SharedHIDResources &, FileDescriptorOwner & fd);
	termios original_attr;
	/// \name concrete implementation of the base class API
	/// @{
	virtual void save();
	virtual void set_mode();
	virtual void restore();
	/// @}
	virtual termios disable_canonical_software_processing (const termios &);
};

/// \brief HIDs that are character devices with a terminal line discipline that speak the kbio protocol
class HIDSpeakingKBIO :
	public HIDWithLineDiscipline
{
public:
	HIDSpeakingKBIO(SharedHIDResources &, FileDescriptorOwner & fd);
	~HIDSpeakingKBIO();

	/// \name concrete implementation of the base class API
	/// @{
	virtual void set_LEDs(const KeyboardLEDs &);
	virtual void save();
	virtual void set_mode();
	/// @}
	void handle_input_events();
protected:
	char buffer[16];
	std::size_t offset;
#if defined(__LINUX__) || defined(__linux__)
	unsigned state;
	bool up;
#endif
	uint16_t code;
	// A set is less efficient than a simple bitmap.
	// But with the 64Ki codespace and 10-key rollver maximum (for most humans) it is much more space efficient.
	typedef std::set<uint16_t> KeysPressed;
	KeysPressed pressed;	///< used for determining auto-repeat keypresses
	bool is_pressed(uint16_t c) const { return pressed.find(c) != pressed.end(); }
	void set_pressed(uint16_t c, bool v) { if (v) pressed.insert(c); else pressed.erase(c); }

	long kbmode;
	long kbmute;
	void restore();
};

/// \brief the base abstract class for switching controllers, used as an interface to concrete switching controller derived classes by the main realizer
class SwitchingController
{
public:
	virtual void acknowledge_switch_to() = 0;
	virtual void permit_switch_from() = 0;
protected:
	SwitchingController() {}
	virtual ~SwitchingController();
};

/// \brief a font specification from the command line
struct FontSpec {
	std::string name;
	int weight;	///< -1 means MEDIUM+BOLD, -2 means LIGHT+DEMIBOLD
	CombinedFont::Font::Slant slant;
};

/// \brief a list of fonot specifications
typedef std::list<FontSpec> FontSpecList;

/// \brief Common processing for human I/O device realizers
class Main :
	public SharedHIDResources,
	public SharedHODResources
{
public:
	const FileDescriptorOwner queue;
	std::shared_ptr<SwitchingController> switching_controller;

	Main(const char * prog, const ProcessEnvironment & envs, bool);
	void autoconfigure(const std::list<std::string> &, bool, bool, bool, bool, bool);
	void load_fonts(const FontSpecList & list);
	void update_LEDs();
	void update_locators();
	void raise_acquire_signal();
	void raise_release_signal();
	void capture_signals(int rs, int as);
	void add_device(HID * dev);
	void add_device(HOD * dev);
	void loop();
	~Main() {}

protected:
	const char * prog;
	const ProcessEnvironment & envs;
	bool active, mouse_primary;
#if defined(__FreeBSD__) || defined(__DragonFly__)
	FileDescriptorOwner passthrough_mouse, passthrough_keyboard;
#endif

	KeyboardModifierState keyboard_state;
	KeyboardLEDs leds;
	MouseState mouse_state;
	VirtualTerminalBackEnd vt;
	KeyboardMap map;
	CombinedFont font;
	std::vector<struct kevent> ip;

	bool mouse_buttons[MouseState::NUM_BUTTONS];
	SharedHODResources::coordinate mouse_col, mouse_row, mouse_dep;

	typedef std::vector<char32_t> DeadKeysList;
	DeadKeysList dead_keys;

	typedef std::list<std::shared_ptr<HID> > HIDList;
	typedef std::list<std::shared_ptr<HOD> > HODList;
	HIDList inputs;
	HODList outputs;

#if defined(__FreeBSD__) || defined(__DragonFly__)
	typedef uint8_t PassthroughKeyboardMap[KBDMAP_ROWS][KBDMAP_COLS];
	static PassthroughKeyboardMap passthrough_keymap;
#endif

	/// \name overrides for the abstract I/O APIs
	/// @{
	virtual void handle_unicode (const char32_t c);
	virtual void handle_keyboard(const uint16_t index, uint8_t value);
	virtual void mouse_button (const uint16_t, bool);
	virtual void mouse_relpos (const MouseAxis, int);
	virtual void mouse_abspos (const MouseAxis, unsigned long, unsigned long);
	/// @}

	bool has_idle_work();
	void do_idle_work();

	void clear_dead_keys() { dead_keys.clear(); }
	void handle_keyboard(const uint8_t row, const uint8_t col, uint8_t v);
	void send_character_input(char32_t c, bool accelerator);
	void send_screen_key(const uint32_t s, const uint8_t m);
	void send_system_key(const uint32_t k, const uint8_t m);
	void send_consumer_key(const uint32_t k, const uint8_t m);
	void send_extended_key(const uint32_t k, const uint8_t m);
	void send_function_key(const uint32_t k, const uint8_t m);
} ;

}

#endif
