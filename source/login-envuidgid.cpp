/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define _BSD_SOURCE 1
#include <vector>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <cerrno>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <unistd.h>
#include "config/haspam.h"
#if defined(HAS_PAM)
#	include <security/pam_appl.h>
#	include <security/pam_modules.h>
#endif
#include "config/hasutmpx.h"
#include "config/hasutmpxexit.h"
#include "config/hasutmpxsession.h"
#if defined(HAS_UTMPX)
#include <sys/time.h>
#include <utmpx.h>
#endif
#include "config/hasupdwtmpx.h"
#include "config/hasshadow.h"
#if defined(HAS_SHADOW)
#	include <shadow.h>
#endif
#include <pwd.h>
#include <grp.h>
#include "kqueue_common.h"
#include "popt.h"
#include "utils.h"
#include "fdutils.h"
#include "ttyutils.h"
#include "ttyname.h"
#include "u32string.h"
#include "FileDescriptorOwner.h"
#include "FileStar.h"
#include "UnicodeClassification.h"
#include "CharacterCell.h"
#include "ControlCharacters.h"
#include "InputMessage.h"
#include "TerminalCapabilities.h"
#include "TUIDisplayCompositor.h"
#include "TUIOutputBase.h"
#include "TUIInputBase.h"
#include "TUIVIO.h"
#include "TUIVIOWidgets.h"
#include "SignalManagement.h"
#include "ProcessEnvironment.h"
#include "LoginBannerInformation.h"
#include "LoginClassRecordOwner.h"

/* Full-screen TUI **********************************************************
// **************************************************************************
*/

namespace {

template<typename T>
void
secure_wipe(
	T & s
) {
	for (typename T::iterator p(s.begin()), e(s.end()); p != e; ++p)
		*p = static_cast<typename T::value_type>(0);
	s.clear();
	s.reserve(0);
}

const CharacterCell::colour_type common_background(ALPHA_FOR_TRUE_COLOURED,0U,0U,0U);
const ColourPair info_colour_pair(ColourPair(CharacterCell::colour_type(ALPHA_FOR_TRUE_COLOURED,0xC0,0xC0,0xC0), common_background));
const ColourPair editable_colour_pair(ColourPair(CharacterCell::colour_type(ALPHA_FOR_TRUE_COLOURED,0xA0,0xA0,0xA0), common_background));
const ColourPair label_colour_pair(ColourPair(CharacterCell::colour_type(ALPHA_FOR_TRUE_COLOURED,0x60,0x90,0x90), common_background));
const ColourPair error_colour_pair(ColourPair(CharacterCell::colour_type(ALPHA_FOR_TRUE_COLOURED,0xF0,0xF0,0x00), common_background));

const uint_fast32_t mask_pass_choices[TUIOutputBase::Options::TUI_LEVELS] = { 0x0025A9, 0x25A9, '*' };

const char help_message_unicode[] = "edit:[\u2326][\u232b] nav:[\u21f1][\u21f2][\u21e4][\u21e5][\u2b63][\u2b61][\u2b60][\u2b62] OK:[\u2ba0]/Enter cancel:[\u2388]+[D] status:[F1]";
const char help_message_ascii[] = "edit:Delete/Backspace nav:Home/End/BackTab/Tab/Down/Up/Left/Right OK:Return/Enter cancel:Ctrl+D status:F1";
const char * help_messages[TUIOutputBase::Options::TUI_LEVELS] = { help_message_unicode, help_message_unicode, help_message_ascii };

/// \brief an individual item, label and entry field, underpinning the dialogue that is shown on screen
/// These are not TUIDrawables, because we special-case their re-painting.
struct EntryField {
	enum Type { ERROR, INFO, PASSWORD, NORMAL };
	EntryField(const std::string & p, const std::string & v, bool error) : prompt(ConvertFromUTF8(p)), value(ConvertFromUTF8(v)), type(error ? ERROR : INFO), pos(value.length()) {}
	EntryField(const std::string & p, bool password) : prompt(ConvertFromUTF8(p)), value(), type(password ? PASSWORD : NORMAL), pos(value.length())
	{
		// Try to avoid having lots of reallocated partial password strings in memory.
		value.reserve(64U);
	}
	EntryField(const EntryField & o) : prompt(o.prompt), value(o.value), type(o.type), pos(o.pos) {}
	~EntryField() { secure_wipe(value); secure_wipe(utf8value); }
	const u32string prompt;
	u32string value;
	Type type;
	std::size_t pos;
	const std::string & query_value() {
		secure_wipe(utf8value);
		// Explicitly measure the length so that we don't get lots of reallocated partial password strings in memory.
		const std::string::size_type length(LengthInUTF8(value));
		utf8value.reserve(length + 1U);
		ConvertToUTF8(utf8value, value);
		return utf8value;
	}
private:
	std::string utf8value;
};

/// \brief the full-screen TUI
struct TUI :
	public TerminalCapabilities,
	public TUIOutputBase,
	public TUIInputBase
{
	TUI(const char *, ProcessEnvironment & e, TUIDisplayCompositor & comp, std::iostream & tty, int tty_fd, const FileDescriptorOwner & queue, unsigned long c, const TUIOutputBase::Options &, const LoginBannerInformation &);
	~TUI();

#if defined(HAS_PAM)
	static
	int
	pam_conv (
		int count,
		const struct pam_message * messages[],
		struct pam_response ** return_responses,
		void * data
	);
	int
	pam_conv (
		std::size_t count,
		const struct pam_message * messages[],
		struct pam_response responses[]
	);
#endif

	bool show();
	int query_dismiss_code() const { return dismiss_code; }
	int query_show_clock() const { return show_clock; }
	void add_press_return_field();

	std::vector<EntryField> fields;
	std::vector<struct kevent> ip;

protected:
	/// \name Common event handlers providing conventional keyboard semantics.
	/// @{
	TUIInputBase::WheelToKeyboard handler0;
	TUIInputBase::CalculatorKeypadToPrintables handler1;
	TUIInputBase::CUAToExtendedKeys handler2;
	TUIInputBase::LineDisciplineToExtendedKeys handler3;
	TUIInputBase::ConsumerKeysToExtendedKeys handler4;
	TUIInputBase::CalculatorKeypadToPrintables handler5;
	/// @}
	/// \brief a custom event handler for things that are particular to this UI
	class EventHandler : public TUIInputBase::EventHandler {
	public:
		EventHandler(TUI & i) : TUIInputBase::EventHandler(i), ui(i) {}
		~EventHandler();
	protected:
		virtual bool ExtendedKey(uint_fast16_t k, uint_fast8_t m);
		virtual bool UCS3(char32_t character);
		TUI & ui;
	} handler6;
	friend class EventHandler;
	const char * const prog;
	const ProcessEnvironment & envs;
	const char * const type;
	const FileDescriptorOwner & queue;
	sig_atomic_t terminate_signalled, interrupt_signalled, hangup_signalled, usr1_signalled, usr2_signalled;
	/// \name The constant TUI elememts, other than the EntryFields.
	/// @{
	TUIVIO vio;
	TUIBackdrop * backdrop;
	TUIFrame * frame;
	TUIStatusBar * statusbar;
	std::vector<TUIDrawable *> drawables;
	/// @}
	int dismiss_code;
	bool immediate_update_needed;
	std::time_t now;
	std::size_t current, maxprompt;
	bool show_help;
	bool show_clock;
	std::string status_text;

	bool dismissed() const { return 0 <= dismiss_code; }
	bool query_immediate_update() const { return immediate_update_needed; }
	bool exit_signalled() const { return terminate_signalled||interrupt_signalled||hangup_signalled; }
	void handle_signal (int);
	void handle_input (int, int);
	void update_clock() { if (!show_clock) return; const std::time_t n(std::time(nullptr)); if (n != now) { now = n; statusbar->set_time(now); set_refresh_and_immediate_update_needed(); } }
	void handle_update_event () { immediate_update_needed = false; TUIOutputBase::handle_update_event(); }
	void update_status_text () { statusbar->text = show_help ? help_messages[options.tui_level] : status_text; }

	virtual void redraw_new();

	/// \name Actions bound to input events by the event handler
	/// @{
	void start_of_field();
	void end_of_field();
	void delete_to_beginning();
	void delete_to_end();
	void delete_forwards();
	void delete_backwards();
	void delete_all();
	void character_left();
	void character_right();
	void tab_forward();
	void tab_backward();
	void dismiss(int c) { dismiss_code = c; }
	void enter_character(char32_t character);
	void toggle_help();
	void toggle_mute();
	/// @}

	void set_refresh_and_immediate_update_needed () { immediate_update_needed = true; TUIOutputBase::set_refresh_needed(); }
};

}

TUI::TUI(
	const char * p,
	ProcessEnvironment & e,
	TUIDisplayCompositor & comp,
	std::iostream & tty,
	int tty_fd,
	const FileDescriptorOwner & q,
	unsigned long /*columns*/,
	const TUIOutputBase::Options & options,
	const LoginBannerInformation & info
) :
	TerminalCapabilities(e),
	TUIOutputBase(*this, tty, tty_fd, options, comp),
	TUIInputBase(static_cast<const TerminalCapabilities &>(*this), tty, tty_fd),
	handler0(*this),
	handler1(*this),
	handler2(*this),
	handler3(*this),
	handler4(*this),
	handler5(*this),
	handler6(*this),
	prog(p),
	envs(e),
	type(e.query("TERM")),
	queue(q),
	terminate_signalled(false),
	interrupt_signalled(false),
	hangup_signalled(false),
	usr1_signalled(false),
	usr2_signalled(false),
	vio(comp),
	backdrop(new TUIBackdrop(options, vio)),
	frame(new TUIFrame(options, vio, 0, 2, 1L, 1L)),
	statusbar(new TUIStatusBar(options, vio, 0, 0, 0L, 1L)),
	drawables(),
	dismiss_code(-1),
	immediate_update_needed(true),
	now(std::time(nullptr)),
	current(0U),
	maxprompt(0U),
	show_help(false),
	show_clock(true),
	status_text("F1=help")
{
	comp.set_screen_flags(options.scnm ? ScreenFlags::INVERTED : 0U);
	suspended();
	drawables.push_back(backdrop);
	drawables.push_back(frame);
	drawables.push_back(statusbar);
	frame->title = info.query_pretty_nodename();
	if (const char * host = info.query_hostname())
		frame->title += " (" + std::string(host) + ')';
	if (type)
		status_text += " (" + std::string(type) + ')';
	if (const char * line = info.query_line())
		status_text += ' ' + std::string(line);
	update_status_text();
	statusbar->set_time(now);
	statusbar->show_time(show_clock);
}

TUI::~TUI(
) {
	continued();
	delete frame; frame = nullptr;
	delete backdrop; backdrop = nullptr;
	delete statusbar; statusbar = nullptr;
}

void
TUI::handle_input (
	int fd,
	int n		///< number of characters available; can be <= 0 erroneously
) {
	for (;;) {
		char control_buffer[128U];
		int l(read(fd, control_buffer, sizeof control_buffer));
		if (0 >= l) break;
		HandleInput(control_buffer, l);
		if (l >= n) break;
		n -= l;
	}
	BreakInput();
}

void
TUI::handle_signal (
	int signo
) {
	switch (signo) {
		case SIGWINCH:	set_resized(); break;
		case SIGTERM:	terminate_signalled = true; break;
		case SIGINT:	interrupt_signalled = true; break;
		case SIGHUP:	hangup_signalled = true; break;
		case SIGUSR1:	usr1_signalled = true; break;
		case SIGUSR2:	usr2_signalled = true; break;
		case SIGTSTP:	tstp_signal(); break;
		case SIGCONT:	continued(); break;
	}
}

void
TUI::redraw_new (
) {
	frame->w = c.query_w() - frame->x;
	frame->h = fields.size() + 5U;
	statusbar->w = c.query_w() - statusbar->x;
	for (std::vector<TUIDrawable*>::const_iterator p(drawables.begin()), e(drawables.end()); p != e; ++p)
		(*p)->Paint();

	std::size_t n(0);
	for (std::vector<EntryField>::const_iterator p(fields.begin()), e(fields.end()); p != e; ++p, ++n) {
		const EntryField & field(*p);
		const CharacterCell::attribute_type prompt_attr(current == n ? ColourPairAndAttributes::BOLD : 0);
		const CharacterCell::attribute_type value_attr(current == n ? ColourPairAndAttributes::BOLD : 0);
		const ColourPair & value_colour_pair(field.ERROR == field.type ? error_colour_pair : field.INFO == field.type ? info_colour_pair : editable_colour_pair);
		const ColourPair & prompt_colour_pair(label_colour_pair);
		long x(frame->x + 2L);
		vio.PrintCharStrAttr(frame->y + 2 + n, x, prompt_attr, prompt_colour_pair, field.prompt.data(), field.prompt.length());
		if (x < 2 + static_cast<long>(maxprompt))
			vio.PrintNCharsAttr(frame->y + 2 + n, x, prompt_attr, prompt_colour_pair, SPC, 2 + static_cast<long>(maxprompt) - x);
		++x;
		if (field.PASSWORD == field.type)
			vio.PrintNCharsAttr(frame->y + 2 + n, x, value_attr, value_colour_pair, mask_pass_choices[options.tui_level], field.value.length());
		else
			vio.PrintCharStrAttr(frame->y + 2 + n, x, value_attr, value_colour_pair, field.value.data(), field.value.length());
	}
	if (current < fields.size()) {
		const EntryField & field(fields[current]);
		c.move_cursor(frame->y + 2 + current, 2 + maxprompt + 1 + field.pos);
		c.set_cursor_state(CursorSprite::BLINK|CursorSprite::VISIBLE, CursorSprite::BOX);
	} else
	{
		c.move_cursor(frame->y + 2 + fields.size(), 0);
		c.set_cursor_state(0U, CursorSprite::BOX);
	}
}

/// \name Actions bound to input events by the event handler
/// @{

void
TUI::start_of_field(
) {
	if (current < fields.size()) {
		EntryField & field(fields[current]);
		if (field.pos != field.value.length() - 1U) {
			field.pos = field.value.length() - 1U;
			set_refresh_and_immediate_update_needed();
		}
	}
}

void
TUI::end_of_field(
) {
	if (current < fields.size()) {
		EntryField & field(fields[current]);
		if (0 != field.pos) {
			field.pos = 0U;
			set_refresh_and_immediate_update_needed();
		}
	}
}

void
TUI::delete_to_beginning()
{
	if (current < fields.size()) {
		EntryField & field(fields[current]);
		if (0 != field.pos) {
			field.value.erase(0U, field.pos);
			field.pos = 0U;
			set_refresh_and_immediate_update_needed();
		}
	}
}

void
TUI::delete_to_end()
{
	if (current < fields.size()) {
		EntryField & field(fields[current]);
		if (field.pos < field.value.length()) {
			field.value.erase(field.pos);
			set_refresh_and_immediate_update_needed();
		}
	}
}

void
TUI::delete_forwards()
{
	if (current < fields.size()) {
		EntryField & field(fields[current]);
		if (field.pos < field.value.length()) {
			field.value.erase(field.pos, 1U);
			set_refresh_and_immediate_update_needed();
		}
	}
}

void
TUI::delete_backwards()
{
	if (current < fields.size()) {
		EntryField & field(fields[current]);
		if (0 != field.pos) {
			--field.pos;
			field.value.erase(field.pos, 1U);
			set_refresh_and_immediate_update_needed();
		}
	}
}

void
TUI::delete_all()
{
	if (current < fields.size()) {
		EntryField & field(fields[current]);
		if (field.value.length() > 0U) {
			field.value.clear();
			field.pos = 0;
			set_refresh_and_immediate_update_needed();
		}
	}
}

void
TUI::character_left()
{
	if (current < fields.size()) {
		EntryField & field(fields[current]);
		if (0 < field.pos) {
			--field.pos;
			set_refresh_and_immediate_update_needed();
		}
	}
}

void
TUI::character_right()
{
	if (current < fields.size()) {
		EntryField & field(fields[current]);
		if (field.pos < field.value.length()) {
			++field.pos;
			set_refresh_and_immediate_update_needed();
		}
	}
}

void
TUI::tab_forward()
{
	for (std::size_t i(current + 1); i != current; ++i) {
		if (i >= fields.size()) i = 0;
		const EntryField & f(fields[i]);
		if ((EntryField::PASSWORD == f.type || EntryField::NORMAL == f.type)) {
			current = i;
			set_refresh_and_immediate_update_needed();
			break;
		}
	}
}

void
TUI::tab_backward()
{
	for (std::size_t i(current > 0U ? current - 1 : fields.size() - 1); i != current; --i) {
		const EntryField & f(fields[i]);
		if ((EntryField::PASSWORD == f.type || EntryField::NORMAL == f.type)) {
			current = i;
			set_refresh_and_immediate_update_needed();
			break;
		}
		if (i <= 0) i = fields.size();
	}
}

void
TUI::enter_character(
	char32_t character
) {
	if (current < fields.size()) {
		EntryField & field(fields[current]);
		if (!UnicodeCategorization::IsMarkEnclosing(character)
		&&  !UnicodeCategorization::IsMarkNonSpacing(character)
		&&  !UnicodeCategorization::IsOtherFormat(character)
		&&  !UnicodeCategorization::IsOtherSurrogate(character)
		&&  !UnicodeCategorization::IsOtherControl(character)
		) {
			if (field.value.length() < 128U) {
				field.value.insert(field.pos++, &character, 1U);
				set_refresh_and_immediate_update_needed();
			}
		}
	}
}

void
TUI::toggle_help()
{
	show_help = !show_help;
	update_status_text();
	set_refresh_and_immediate_update_needed();
}

void
TUI::toggle_mute()
{
	show_clock = !show_clock;
	statusbar->show_time(show_clock);
	update_status_text();
	set_refresh_and_immediate_update_needed();
}

/// @}

/// \brief The inner show dialogue function.
/// This contains an event loop that terminates when the dialogue is dismissed.
bool
TUI::show(
) {
	continued();

	// How long to wait with updates pending.
	const struct timespec normal_timeout = { 1, 0 };		// Wake up every second to tick the clock.
	const struct timespec short_timeout = { 0, 100000000L };	// Wait for 0.1s before doing an update.
	const struct timespec immediate_timeout = { 0, 0 };

	dismiss(-1);
	for (current = 0; current < fields.size(); ++current) {
		const EntryField & f(fields[current]);
		if ((EntryField::PASSWORD == f.type || EntryField::NORMAL == f.type))
			break;
	}
	maxprompt = 0;
	for (std::vector<EntryField>::const_iterator p(fields.begin()), e(fields.end()); p != e; ++p)
		if (p->prompt.length() > maxprompt)
			maxprompt = p->prompt.length();

	set_refresh_and_immediate_update_needed();
	std::vector<struct kevent> p(4);
	while (true) {
		if (dismissed())
			break;
		if (exit_signalled()) {
			suspended();
			vio.CLSToSpace(ColourPair::def);
			return false;
		}
		handle_resize_event();
		handle_refresh_event();

		const struct timespec * timeout(query_immediate_update() ? &immediate_timeout : has_update_pending() ? &short_timeout : query_show_clock() ? &normal_timeout : nullptr);
		const int rc(kevent(queue.get(), ip.data(), ip.size(), p.data(), p.size(), timeout));
		ip.clear();

		if (0 > rc) {
			if (EINTR == errno) continue;
#if defined(__LINUX__) || defined(__linux__)
			if (EINVAL == errno) continue;	// This works around a Linux bug when an inotify queue overflows.
			if (0 == errno) continue;	// This works around another Linux bug.
#endif
			die_errno(prog, envs, "kevent");
		}

		if (0 == rc) {
			update_clock();
			handle_update_event();
			continue;
		}

		for (std::size_t i(0); i < static_cast<std::size_t>(rc); ++i) {
			const struct kevent & e(p[i]);
			switch (e.filter) {
				case EVFILT_SIGNAL:
					handle_signal(e.ident);
					break;
				case EVFILT_READ:
				{
					const int fd(static_cast<int>(e.ident));
					if (QueryInputFD() == fd) {
						handle_input(fd, e.data);
					}
					break;
				}
			}
		}
	}

	suspended();
	vio.CLSToSpace(ColourPair::def);

	return true;
}

bool
TUI::EventHandler::ExtendedKey(
	uint_fast16_t k,
	uint_fast8_t m
) {
	switch (k) {
		default:	return TUIInputBase::EventHandler::ExtendedKey(k, m);
		case EXTENDED_KEY_MUTE:
			ui.toggle_mute();
			return true;
		case EXTENDED_KEY_HELP:
			ui.toggle_help();
			return true;
		case EXTENDED_KEY_END:
			if (m & INPUT_MODIFIER_CONTROL) ui.start_of_field();
			return true;
		case EXTENDED_KEY_HOME:
			if (m & INPUT_MODIFIER_CONTROL) ui.end_of_field();
			return true;
		case EXTENDED_KEY_LEFT_ARROW:
			ui.character_left();
			return true;
		case EXTENDED_KEY_RIGHT_ARROW:
			ui.character_right();
			return true;
		case EXTENDED_KEY_BACKSPACE:
		case EXTENDED_KEY_ALTERNATE_ERASE:
			if (m & INPUT_MODIFIER_CONTROL) {
				if (m & INPUT_MODIFIER_LEVEL2)
					ui.delete_to_end();
				else
					ui.delete_to_beginning();
			} else
			{
				if (m & INPUT_MODIFIER_LEVEL2)
					ui.delete_forwards();
				else
					ui.delete_backwards();
			}
			return true;
		case EXTENDED_KEY_DELETE:
		case EXTENDED_KEY_DEL_CHAR:
			if (m & INPUT_MODIFIER_CONTROL)
				ui.delete_to_end();
			else
				ui.delete_forwards();
			return true;
		case EXTENDED_KEY_CLEAR:
		case EXTENDED_KEY_DEL_LINE:
			ui.delete_all();
			return true;
		case EXTENDED_KEY_TAB:
		case EXTENDED_KEY_DOWN_ARROW:
		case EXTENDED_KEY_NEXT:
			ui.tab_forward();
			return true;
		case EXTENDED_KEY_BACKTAB:
		case EXTENDED_KEY_UP_ARROW:
		case EXTENDED_KEY_PREVIOUS:
			ui.tab_backward();
			return true;
		case EXTENDED_KEY_PAD_ENTER:
		case EXTENDED_KEY_RETURN_OR_ENTER:
		case EXTENDED_KEY_APP_RETURN:
		case EXTENDED_KEY_EXECUTE:
			ui.dismiss(1);
			return true;
		case EXTENDED_KEY_CANCEL:
		case EXTENDED_KEY_CLOSE:
		case EXTENDED_KEY_EXIT:
		case EXTENDED_KEY_STOP:
			ui.dismiss(0);
			return true;
		case EXTENDED_KEY_REFRESH:
			ui.invalidate_cur();
			ui.set_update_needed();
			return true;
	}
}

bool
TUI::EventHandler::UCS3(
	char32_t character
) {
	if (!UnicodeCategorization::IsOtherControl(character))
		ui.enter_character(character);
	return true;
}

// Seat of the class
TUI::EventHandler::~EventHandler() {}

namespace {
	const char press_return_message_unicode[] = "Press [\u2ba0] to continue.";
	const char press_return_message_ascii[] = "Press [Return] to continue.";
	const char * press_return_messages[TUIOutputBase::Options::TUI_LEVELS] = { press_return_message_unicode, press_return_message_unicode, press_return_message_ascii };
}

void
TUI::add_press_return_field(
) {
	fields.push_back(EntryField("action", press_return_messages[options.tui_level], false));
}

#if defined(HAS_PAM)

/// \brief Reflect the static member call back on to a non-static member function of the TUI object.
int
TUI::pam_conv (
	int count,
	const struct pam_message * messages[],
	struct pam_response ** return_responses,
	void * data
) {
	if (count < 0) return PAM_BUF_ERR;
	struct pam_response * responses = static_cast<struct pam_response *>(std::calloc(sizeof(struct pam_response), count));
	if (!responses) return PAM_BUF_ERR;
	*return_responses = responses;
	return static_cast<TUI *>(data)->pam_conv(count, messages, responses);
}

/// \brief This is essentially treating the TUI as a modal dialogue.
int
TUI::pam_conv (
	std::size_t count,
	const struct pam_message * messages[],
	struct pam_response responses[]
) {
	// Convert the PAM messages into a list of abstract EntryFields.
	fields.clear();
	bool seen_entryfield(false);
	for(std::size_t i(0); i < count; ++i) {
		const struct pam_message & m(*messages[i]);
		switch (m.msg_style) {
			case PAM_PROMPT_ECHO_OFF:
				fields.push_back(EntryField(m.msg, true));
				seen_entryfield = true;
				break;
			case PAM_PROMPT_ECHO_ON:
				fields.push_back(EntryField(m.msg, false));
				seen_entryfield = true;
				break;
			default:
			case PAM_ERROR_MSG:
				fields.push_back(EntryField("error", m.msg, true));
				break;
			case PAM_TEXT_INFO:
				fields.push_back(EntryField("information", m.msg, false));
				break;
		}
	}
	if (!seen_entryfield)
		add_press_return_field();
	// ShowDialog(), pretty much.
	show();
	if (1 > query_dismiss_code()) return PAM_CONV_ERR;
	// Read the abstract EntryFields back into the PAM results.
	for(std::size_t i(0); i < count; ++i) {
		const struct pam_message & m(*messages[i]);
		struct pam_response & r(responses[i]);
		switch (m.msg_style) {
			case PAM_PROMPT_ECHO_OFF:
			case PAM_PROMPT_ECHO_ON:
				r.resp = strdup(fields[i].query_value().c_str());
				break;
			default:
			case PAM_ERROR_MSG:
			case PAM_TEXT_INFO:
				r.resp = nullptr;
				break;
		}
		r.resp_retcode = 0;
	}
	return PAM_SUCCESS;
}

#endif

/* Utilities ****************************************************************
// **************************************************************************
*/

namespace {

#if defined(HAS_UTMPX)
inline
void
gettimeofday (
	struct utmpx & u
) {
#if defined(__LINUX__) || defined(__linux__)
	// gettimeofday() doesn't work directly because the member structure isn't actually a timeval.
	struct timeval tv;
	gettimeofday(&tv, nullptr);
	u.ut_tv.tv_sec = tv.tv_sec;
	u.ut_tv.tv_usec = tv.tv_usec;
#else
	gettimeofday(&u.ut_tv, nullptr);
#endif
}
#endif

}

/* Main functions ***********************************************************
// **************************************************************************
*/

#if defined(HAS_PAM)
namespace {
#endif

void
login_envuidgid
(
#if defined(HAS_PAM)
	bool do_pam,
#endif
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	TUIOutputBase::Options options;
	bool verbose(false);
#if defined(HAS_UTMPX)
	bool do_utmpx(false);
#if defined(HAS_PAM)
	bool utmpx_even_if_pam(false);
#endif
#endif
	try {
		popt::bool_definition cursor_application_mode_option('\0', "cursor-keypad-application-mode", "Set the cursor keypad to application mode instead of normal mode.", options.cursor_application_mode);
		popt::bool_definition calculator_application_mode_option('\0', "calculator-keypad-application-mode", "Set the calculator keypad to application mode instead of normal mode.", options.calculator_application_mode);
		popt::bool_definition no_alternate_screen_buffer_option('\0', "no-alternate-screen-buffer", "Prevent switching to the XTerm alternate screen buffer.", options.no_alternate_screen_buffer);
		popt::bool_string_definition scnm_option('\0', "inversescreen", "Switch inverse screen mode on/off.", options.scnm);
		popt::tui_level_definition tui_level_option('T', "tui-level", "Specify the level of TUI character set.");
		popt::definition * tui_table[] = {
			&cursor_application_mode_option,
			&calculator_application_mode_option,
			&no_alternate_screen_buffer_option,
			&scnm_option,
			&tui_level_option,
		};
		popt::table_definition tui_table_option(sizeof tui_table/sizeof *tui_table, tui_table, "Terminal quirks options");
		popt::bool_definition verbose_option('v', "verbose", "Log verbose information.", verbose);
#if defined(HAS_UTMPX)
		popt::bool_definition utmpx_option('\0', "utmpx", "Update the utmpx login database.", do_utmpx);
#if defined(HAS_PAM)
		popt::bool_definition utmpx_even_if_pam_option('\0', "utmpx-even-if-pam", "Update the utmpx login database even when PAM is being used.", utmpx_even_if_pam);
#else
		bool utmpx_even_if_pam(false);
		popt::bool_definition utmpx_even_if_pam_option('\0', "utmpx-even-if-pam", "Compatibility option; ignored.", utmpx_even_if_pam);
#endif
#else
		bool do_utmpx(false);
		bool utmpx_even_if_pam(false);
		popt::bool_definition utmpx_option('\0', "utmpx", "Compatibility option; ignored.", do_utmpx);
		popt::bool_definition utmpx_even_if_pam_option('\0', "utmpx-even-if-pam", "Compatibility option; ignored.", utmpx_even_if_pam);
#endif
		popt::definition * top_table[] = {
			&utmpx_option,
			&utmpx_even_if_pam_option,
			&verbose_option,
			&tui_table_option,
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{prog}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		next_prog = arg0_of(args);
		if (p.stopped()) throw EXIT_SUCCESS;
		options.tui_level = tui_level_option.value() < options.TUI_LEVELS ? tui_level_option.value() : options.TUI_LEVELS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}

	const char * tty(get_controlling_tty_filename(envs));
	std::fstream control(tty);
	if (!control.is_open()) {
		die_errno(prog, envs, tty);
	}
#if __cpp_lib_fstream_native_handle
	const int control_fd(control.rdbuf()->native_handle());
#else
	const int control_fd(open_readwriteexisting_at(AT_FDCWD, tty));
	if (0 > control_fd) {
		die_errno(prog, envs, tty);
	}
#endif

	const unsigned long columns(get_columns(envs, control_fd));

	LoginBannerInformation info(prog, envs);

	const FileDescriptorOwner queue(kqueue());
	if (0 > queue.get()) {
		die_errno(prog, envs, "kqueue");
	}

	TUIDisplayCompositor compositor(false /* no software cursor */, 24, 80);
	TUI ui(prog, envs, compositor, control, control_fd, queue, columns, options, info);

	append_event(ui.ip, ui.QueryInputFD(), EVFILT_READ, EV_ADD, 0, 0, nullptr);
	ReserveSignalsForKQueue kqueue_reservation(SIGTERM, SIGINT, SIGHUP, SIGPIPE, SIGUSR1, SIGUSR2, SIGWINCH, SIGTSTP, SIGCONT, 0);
	PreventDefaultForFatalSignals ignored_signals(SIGTERM, SIGINT, SIGHUP, SIGPIPE, SIGUSR1, SIGUSR2, 0);
	append_event(ui.ip, SIGWINCH, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ui.ip, SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ui.ip, SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ui.ip, SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ui.ip, SIGPIPE, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ui.ip, SIGTSTP, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
	append_event(ui.ip, SIGCONT, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);

	errno = 0;
	if (false) {
exit_error:
		const int error(errno);
		ui.fields.clear();
		ui.fields.push_back(EntryField("error", std::strerror(error), true));
		ui.add_press_return_field();
		ui.show();
		if (verbose)
			std::fprintf(stderr, "%s: ERROR: %s.\n", prog, std::strerror(error));
		throw static_cast<int>(EXIT_TEMPORARY_FAILURE);	// Bernstein daemontools compatibility
	}

	const char *user(nullptr);
	std::string account;
#if defined(HAS_PAM)
	int pam_errno(0);
	pam_handle_t *pamh(nullptr);
	bool has_cred(false), has_session(false);
	if (false) {
exit_pamerror:
		const int saved_pam_errno(pam_errno);
		if (has_session) pam_errno = pam_close_session(pamh, 0);
		if (has_cred) pam_errno = pam_setcred(pamh, PAM_DELETE_CRED);
		pam_end(pamh, pam_errno);
		ui.fields.clear();
		const char * msg(pam_strerror(pamh, saved_pam_errno));
		if (!msg) msg = "error message not supplied";
		ui.fields.push_back(EntryField("PAM error", msg, true));
		ui.add_press_return_field();
		ui.show();
		if (verbose)
			std::fprintf(stderr, "%s: ERROR: PAM: %s.\n", prog, msg);
		throw static_cast<int>(EXIT_TEMPORARY_FAILURE);	// Bernstein daemontools compatibility
	}
	if (do_pam) {
		struct pam_conv pamc = { TUI::pam_conv, &ui };
		pam_errno = pam_start("login", nullptr, &pamc, &pamh);
		if (PAM_SUCCESS != pam_errno) goto exit_pamerror;
		if (const char * line = info.query_line()) {
			pam_errno = pam_set_item(pamh, PAM_TTY, line);
			if (PAM_SUCCESS != pam_errno) goto exit_pamerror;
		}
		if (const char * hostname = info.query_hostname()) {
			pam_errno = pam_set_item(pamh, PAM_RHOST, hostname);
			if (PAM_SUCCESS != pam_errno) goto exit_pamerror;
		}
		if (const char * logname = getlogin()) {
			pam_errno = pam_set_item(pamh, PAM_RUSER, logname);
			if (PAM_SUCCESS != pam_errno) goto exit_pamerror;
		}
#if !defined(__LINUX__) && !defined(__linux__)	// Linux does not let applications call pam_set_data().
		if (const char * nisdomainname = info.query_nisdomainname()) {
			pam_errno = pam_set_data(pamh, "yp_domain", const_cast<char *>(nisdomainname), nullptr);
			if (PAM_SUCCESS != pam_errno) goto exit_pamerror;
		}
#endif
		pam_errno = pam_authenticate(pamh, 0);
		if (PAM_SUCCESS != pam_errno) goto exit_pamerror;
		pam_errno = pam_acct_mgmt(pamh, 0);
		if (PAM_NEW_AUTHTOK_REQD == pam_errno)
			pam_errno = pam_chauthtok(pamh, PAM_CHANGE_EXPIRED_AUTHTOK);
		if (PAM_SUCCESS != pam_errno) goto exit_pamerror;
		pam_errno = pam_get_user(pamh, &user, nullptr);
		if (PAM_SUCCESS != pam_errno) goto exit_pamerror;
	} else
#endif
	{
		const passwd * p(getpwuid(0));
		LoginClassRecordOwner lc_system(LoginClassRecordOwner::GetSystem(*p));
		ui.fields.clear();
		const char * login_prompt(lc_system.getcapstr("login_prompt", "account:", "account:"));
		ui.fields.push_back(EntryField(login_prompt, false));
		const char * passwd_prompt(lc_system.getcapstr("passwd_prompt", "password:", "password:"));
		ui.fields.push_back(EntryField(passwd_prompt, true));
#	if !defined(__OpenBSD__)
		ui.fields.push_back(EntryField("warning", "PAM is not available", true));
#	endif
		endpwent();
		ui.show();
		if (1 > ui.query_dismiss_code()) throw EXIT_FAILURE;
		account = ui.fields[0].query_value();		// We need to have a copy of this.
		user = account.c_str();
	}
	errno = 0;
	passwd * const p(getpwnam(user));
	if (!p) goto exit_error;
#if defined(HAS_PAM)
	if (do_pam)
	{
		if (char * const * pam_env = pam_getenvlist(pamh)) {
			for (char * const * ep = pam_env; *ep != nullptr; ++ep) {
				const char * const e(*ep);
				if (const char * q = std::strchr(e, '=')) {
					const std::string var(e, q);
					if (!envs.set(var, q + 1)) goto exit_error;
				}
				free(*ep);
			}
		}
	}
	else
#endif
	{
#	if defined(HAS_SHADOW)
		errno = 0;
		struct spwd * const s(getspnam(p->pw_name));
		if (!s) goto exit_error;
		const char * const passwd(s->sp_pwdp);
#	else
		const char * const passwd(p->pw_passwd);
#	endif
		if (passwd && *passwd) {
			const std::string & password(ui.fields[1].query_value());	// Do not copy this.
			const char *encrypted(crypt(password.c_str(), passwd));
			if (0 != std::strcmp(encrypted, passwd)) {
				ui.fields.clear();
				ui.fields.push_back(EntryField("error", "Incorrect account name or password.", true));
				ui.add_press_return_field();
				ui.show();
				if (verbose)
					// The user name is attacker-supplied and has not been sanitized in this path by the authentication system validating it.
					std::fprintf(stderr, "%s: ERROR: Account failed to authenticate.\n", prog);
				throw static_cast<int>(EXIT_TEMPORARY_FAILURE);	// Bernstein daemontools compatibility
			}
		}
	}
#if 0 // not yet, and we may break processing "welcome", "hushlogin", and "nocheckmail" out into a separate tool.
	LoginClassRecordOwner lc_system(LoginClassRecordOwner::GetSystem(*p));
	LoginClassRecordOwner lc_user(LoginClassRecordOwner::GetUser(*p));
#endif
	int n(0);	// Zero initialization is important!
	getgrouplist(user, p->pw_gid, nullptr, &n);
	std::vector<gid_t> groups(n);
	n = groups.size();
	if (0 > getgrouplist(user, p->pw_gid, groups.data(), &n)) goto exit_error;
	char uid_value[64], gid_value[64];
	std::string gidlist;
	for (std::vector<gid_t>::const_iterator i(groups.begin()), e(groups.end()); e != i; ++i) {
		snprintf(gid_value, sizeof gid_value, "%u", *i);
		if (!gidlist.empty()) gidlist += ",";
		gidlist += gid_value;
	}
	if (!envs.set("LOGNAME", user)) goto exit_error;
	if (!envs.set("GIDLIST", gidlist)) goto exit_error;
	snprintf(uid_value, sizeof uid_value, "%u", p->pw_uid);
	snprintf(gid_value, sizeof gid_value, "%u", p->pw_gid);
	if (!envs.set("UID", uid_value)) goto exit_error;
	if (!envs.set("GID", gid_value)) goto exit_error;
#if defined(HAS_PAM)
	if (!do_pam)
#endif
	{
#	if defined(HAS_SHADOW)
		endspent();
#	endif
	}
	endpwent();
#if defined(HAS_PAM)
	if (do_pam) {
		pam_errno = pam_setcred(pamh, PAM_ESTABLISH_CRED);
		if (PAM_SUCCESS != pam_errno) goto exit_pamerror;
		has_cred = true;
		pam_errno = pam_open_session(pamh, 0);
		if (PAM_SUCCESS != pam_errno) goto exit_pamerror;
		has_session = true;
	}
#endif
	pid_t child(fork());
	if (0 > child) {
exit_error_pam_cleanup:
#if defined(HAS_PAM)
		if (do_pam) {
			const int error(errno);
			pam_errno = pam_close_session(pamh, 0);
			pam_errno = pam_setcred(pamh, PAM_DELETE_CRED);
			pam_end(pamh, pam_errno);
			errno = error;
		}
#endif
		goto exit_error;
	} else
	if (0 != child) {
		if (verbose)
			std::fprintf(stderr, "%s: INFO: Logged in \"%s\" and spawned process %u.\n", prog, user, child);
#if defined(HAS_UTMPX)
		struct utmpx u = {};
		if (do_utmpx
#if defined(HAS_PAM)
		&& (!do_pam || utmpx_even_if_pam)
#endif
		) {
			// The entire decades-since obsolete scheme with inittab IDs and INIT_PROCESS+LOGIN_PROCESS states depends from matching the process ID of this process.
			// But the modern reality is that only USER_PROCESS and DEAD_PROCESS are meaningful at all.
			// So we can mark the actual user process as the process ID, instead of our own process ID.
			u.ut_pid = child;
#if defined(HAS_UTMPX_SESSION)
			u.ut_session = getsid(0);
#endif
			std::strncpy(u.ut_user, user, sizeof u.ut_user);
			const char * line(get_line_name(envs));
			std::strncpy(u.ut_line, line, sizeof u.ut_line);
			arc4random_buf(u.ut_id, sizeof u.ut_id);
			if (const char * host = info.query_hostname())
				std::strncpy(u.ut_host, host, sizeof u.ut_host);
		}
#endif

#if defined(HAS_UTMPX)
		if (do_utmpx
#if defined(HAS_PAM)
		&& (!do_pam || utmpx_even_if_pam)
#endif
		) {
			u.ut_type = USER_PROCESS;
			gettimeofday(u);
			setutxent();
			pututxline(&u);
#if defined(HAS_UPDWTMPX)
			updwtmpx(_PATH_WTMP, &u);
#endif
			endutxent();
		}
#endif
		int status(0), code(0);
		const int r(wait_blocking_for_exit_of(child, status, code));
		const int error(errno);
		if (verbose)
			std::fprintf(stderr, "%s: INFO: Process %u terminated with status %d code %d.\n", prog, child, status, code);
#if defined(HAS_UTMPX)
		if (do_utmpx
#if defined(HAS_PAM)
		&& (!do_pam || utmpx_even_if_pam)
#endif
		) {
			u.ut_type = DEAD_PROCESS;
			gettimeofday(u);
			setutxent();
#if defined(HAS_UTMPX_EXIT)
			u.ut_exit.e_termination = status;
			u.ut_exit.e_exit = code;
#endif
			pututxline(&u);
#if defined(HAS_UPDWTMPX)
			updwtmpx(_PATH_WTMP, &u);
#endif
			endutxent();
		}
#endif
		errno = error;
		if (0 > r) goto exit_error_pam_cleanup;

#if defined(HAS_PAM)
		if (do_pam) {
			pam_errno = pam_close_session(pamh, 0);
			if (PAM_SUCCESS != pam_errno) goto exit_pamerror;
			has_session = false;
			pam_errno = pam_setcred(pamh, PAM_DELETE_CRED);
			if (PAM_SUCCESS != pam_errno) goto exit_pamerror;
			has_cred = false;
			pam_end(pamh, pam_errno);
		}
#endif
		if (WAIT_STATUS_SIGNALLED == status || WAIT_STATUS_SIGNALLED_CORE == status) {
			TemporarilyUnblockSignals unblock(code, 0);
			raise(code);
			throw 128 + code;
		} else
			throw code;
	}
}

#if defined(HAS_PAM)
}
#endif

#if defined(HAS_PAM)
void
login_envuidgid
(
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	login_envuidgid(true /* do PAM if available */, next_prog, args, envs);
}
#endif

void
login_envuidgid_nopam
(
	const char * & next_prog,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
#if defined(HAS_PAM)
	login_envuidgid(false /* do not do PAM even if available */, next_prog, args, envs);
#else
	login_envuidgid(next_prog, args, envs);
#endif
}
