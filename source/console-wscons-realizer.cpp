/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define _BSD_SOURCE 1
#include <map>
#include <set>
#include <stack>
#include <deque>
#include <vector>
#include <cstring>
#include <cerrno>
#include <csignal>
#include <sys/mman.h>
#include "config/haswscons.h"
#if defined(HAS_WSCONS)
#	include <dev/wscons/wsconsio.h>
#	if defined(__OpenBSD__) || defined(__NetBSD__)
#		include <dev/usb/usb.h>
#	endif
#	include <dev/usb/usbhid.h>
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

#if defined(HAS_WSCONS)

/* WSCONS  event HIDs *******************************************************
// **************************************************************************
*/

namespace {

class WSConsDevice :
	public HID
{
public:
	WSConsDevice(SharedHIDResources &, FileDescriptorOwner & fd);
	~WSConsDevice() {}

	/// \name overrides for the abstract I/O APIs
	/// @{
	virtual void set_LEDs(const KeyboardLEDs &);
	virtual void handle_input_events();
	/// @}
protected:
	wscons_event buffer[16];
	std::size_t offset;

	static VirtualTerminalRealizer::MouseAxis TranslateAxis(const unsigned int code);
	static int32_t TranslateDelta(const unsigned int code, const signed int delta);
};

/// \note In theory, we should query the keyboard type and pick amongst USB, PC/AT, and other key code translations.
/// In practice, modern versions of NetBSD tend to say that almost everything that one will realistically encounter in the wild is WSKBD_TYPE_USB.
uint16_t
wscons_keycode_to_keymap_index(
	unsigned int value
) {
       return value >= 0xFFFF ? 0xFFFF : usb_ident_to_keymap_index(HID_USAGE2(HUP_KEYBOARD, value));
}

}

inline
WSConsDevice::WSConsDevice(
	SharedHIDResources & r,
	FileDescriptorOwner & fd
) :
	HID(r, fd),
	offset(0U)
{
}

inline
void
WSConsDevice::set_LEDs(
	const KeyboardLEDs & leds
) {
	if (-1 != device.get()) {
		const int newled((leds.caps_lock()||leds.shift2_lock() ? WSKBD_LED_CAPS : 0)|(leds.num_lock() ? WSKBD_LED_NUM : 0)|(leds.group2() ? WSKBD_LED_SCROLL : 0)|(leds.shift3_lock() ? WSKBD_LED_COMPOSE : 0));
		ioctl(device.get(), WSKBDIO_SETLEDS, &newled);
	}
}

inline
VirtualTerminalRealizer::MouseAxis
WSConsDevice::TranslateAxis(
	const unsigned int code
) {
	switch (code) {
		default:					return AXIS_INVALID;
		case WSCONS_EVENT_MOUSE_ABSOLUTE_W:		return AXIS_W;
		case WSCONS_EVENT_MOUSE_ABSOLUTE_X:		return AXIS_X;
		case WSCONS_EVENT_MOUSE_ABSOLUTE_Y:		return AXIS_Y;
		case WSCONS_EVENT_MOUSE_ABSOLUTE_Z:		return AXIS_Z;
		case WSCONS_EVENT_MOUSE_DELTA_W:		return AXIS_W;
		case WSCONS_EVENT_MOUSE_DELTA_X:		return AXIS_X;
		case WSCONS_EVENT_MOUSE_DELTA_Y:		return AXIS_Y;
		case WSCONS_EVENT_MOUSE_DELTA_Z:		return AXIS_Z;
	}
}

inline
int32_t
WSConsDevice::TranslateDelta(
	const unsigned int code,
	const int32_t delta
) {
	switch (code) {
		default:					return delta;
		// Frustratingly, wscons has the Y axis the other way up to everyone else.
		case WSCONS_EVENT_MOUSE_DELTA_Y:		return -delta;
	}
}

inline
void
WSConsDevice::handle_input_events(
) {
	const int n(read(device.get(), reinterpret_cast<char *>(buffer) + offset, sizeof buffer - offset));
	if (0 > n) return;
	for (
		offset += n;
		offset >= sizeof *buffer;
		memmove(buffer, buffer + 1U, sizeof buffer - sizeof *buffer),
		offset -= sizeof *buffer
	    ) {
		const wscons_event & e(buffer[0]);
		switch (e.type) {
			case WSCONS_EVENT_MOUSE_ABSOLUTE_W:
			case WSCONS_EVENT_MOUSE_ABSOLUTE_X:
			case WSCONS_EVENT_MOUSE_ABSOLUTE_Y:
			case WSCONS_EVENT_MOUSE_ABSOLUTE_Z:
				handle_mouse_abspos(TranslateAxis(e.type), e.value, 32767U);
				break;
			case WSCONS_EVENT_MOUSE_DELTA_W:
			case WSCONS_EVENT_MOUSE_DELTA_X:
			case WSCONS_EVENT_MOUSE_DELTA_Y:
			case WSCONS_EVENT_MOUSE_DELTA_Z:
				handle_mouse_relpos(TranslateAxis(e.type), TranslateDelta(e.type, e.value));
				break;
			case WSCONS_EVENT_KEY_UP:
			case WSCONS_EVENT_KEY_DOWN:
			{
				const bool down(WSCONS_EVENT_KEY_DOWN == e.type);
				const uint16_t index(wscons_keycode_to_keymap_index(e.value));
				shared.handle_keyboard(index, down);
				break;
			}
			case WSCONS_EVENT_MOUSE_UP:
			case WSCONS_EVENT_MOUSE_DOWN:
			{
				const bool down(WSCONS_EVENT_MOUSE_DOWN == e.type);
				handle_mouse_button(e.value, down); break;
				break;
			}
		}
	}
}

void
console_wscons_realizer [[gnu::noreturn]] (
	const char * & /*next_prog*/,
	std::vector<const char *> & args,
	ProcessEnvironment & envs
) {
	const char * prog(basename_of(args[0]));
	bool mouse_primary(false);

	try {
		popt::bool_definition mouse_primary_option('\0', "mouse-primary", "Pass mouse position data to the terminal emulator.", mouse_primary);
		popt::definition * top_table[] = {
			&mouse_primary_option,
		};
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{wsconsdevice}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}

	if (args.empty()) {
		die_missing_argument(prog, envs, "evdev device name");
	}
	const char * input_filename(args.front());
	args.erase(args.begin());
	if (!args.empty()) die_unexpected_argument(prog, args, envs);

	Main main(prog, envs, mouse_primary);

	// Now open devices.

	{
		FileDescriptorOwner fdi(open_readwriteexisting_at(AT_FDCWD, input_filename));
		if (0 > fdi.get()) {
		hiderror:
			die_errno(prog, envs, input_filename);
		}
		WSConsDevice * input = new WSConsDevice(main, fdi);
		if (!input) goto hiderror;
		main.add_device(input);
		input->save();
		input->set_mode();
	}

	{
		std::list<std::string> device_paths;
		device_paths.push_back("wscons." + std::string(basename_of(input_filename)));
		device_paths.push_back("wscons");
		main.autoconfigure(device_paths, false /* no display */, true /* has mouse */, true /* has keyboard */, true /* has NumLock */, true /* has LEDs */);
	}

	main.raise_acquire_signal();

	main.loop();

	throw EXIT_SUCCESS;
}

#endif
