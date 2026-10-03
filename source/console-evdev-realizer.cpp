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
#include "config/hasevdev.h"
#if defined(HAS_EVDEV)
#	include <linux/input.h>
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

#if defined(HAS_EVDEV)

/* Linux event HIDs *********************************************************
// **************************************************************************
*/

namespace {

class EvDev :
	public HID
{
public:
	EvDev(SharedHIDResources &, FileDescriptorOwner & fd);
	~EvDev() {}

	/// \name overrides for the abstract I/O APIs
	/// @{
	virtual void set_LEDs(const KeyboardLEDs &);
	virtual void handle_input_events();
	virtual bool set_exclusive(bool);
	/// @}
protected:
	input_event buffer[16];
	std::size_t offset;

	static VirtualTerminalRealizer::MouseAxis TranslateAbsAxis(const uint16_t code);
	static VirtualTerminalRealizer::MouseAxis TranslateRelAxis(const uint16_t code);
};

}

inline
EvDev::EvDev(
	SharedHIDResources & r,
	FileDescriptorOwner & fd
) :
	HID(r, fd),
	offset(0U)
{
}

inline
bool
EvDev::set_exclusive(
	bool v
) {
	return 0 <= ioctl(device.get(), EVIOCGRAB, v);
}

inline
void
EvDev::set_LEDs(
	const KeyboardLEDs & leds
) {
	if (-1 != device.get()) {
		input_event e[5];
		e[0].type = e[1].type = e[2].type = e[3].type = e[4].type = EV_LED;
		e[0].code = LED_CAPSL;
		e[0].value = leds.caps_lock();
		e[1].code = LED_NUML;
		e[1].value = leds.num_lock();
		e[2].code = LED_SCROLLL;
		e[2].value = leds.group2();
		e[3].code = LED_KANA;
		e[3].value = leds.shift2_lock();
		e[4].code = LED_COMPOSE;
		e[4].value = leds.shift3_lock();
		write(device.get(), e, sizeof e);
	}
}

inline
VirtualTerminalRealizer::MouseAxis
EvDev::TranslateAbsAxis(
	const uint16_t code
) {
	switch (code) {
		default:		return AXIS_INVALID;
		case ABS_X:		return AXIS_X;
		case ABS_Y:		return AXIS_Y;
		case ABS_Z:		return AXIS_Z;
	}
}

inline
VirtualTerminalRealizer::MouseAxis
EvDev::TranslateRelAxis(
	const uint16_t code
) {
	switch (code) {
		default:		return AXIS_INVALID;
		case REL_WHEEL:		return V_SCROLL;
		case REL_HWHEEL:	return H_SCROLL;
		case REL_X:		return AXIS_X;
		case REL_Y:		return AXIS_Y;
		case REL_Z:		return AXIS_Z;
	}
}

inline
void
EvDev::handle_input_events(
) {
	const int n(read(device.get(), reinterpret_cast<char *>(buffer) + offset, sizeof buffer - offset));
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
				handle_mouse_abspos(TranslateAbsAxis(e.code), e.value, 32767U);
				break;
			case EV_REL:
				handle_mouse_relpos(TranslateRelAxis(e.code), e.value);
				break;
			case EV_KEY:
			{
				switch (e.code) {
					default:
					{
						const uint16_t index(linux_evdev_keycode_to_keymap_index(e.code));
						shared.handle_keyboard(index, e.value);
						break;
					}
					case BTN_LEFT:		handle_mouse_button(0x00, e.value); break;
					case BTN_MIDDLE:	handle_mouse_button(0x01, e.value); break;
					case BTN_RIGHT:		handle_mouse_button(0x02, e.value); break;
					case BTN_SIDE:		handle_mouse_button(0x03, e.value); break;
					case BTN_EXTRA:		handle_mouse_button(0x04, e.value); break;
					case BTN_FORWARD:	handle_mouse_button(0x05, e.value); break;
					case BTN_BACK:		handle_mouse_button(0x06, e.value); break;
					case BTN_TASK:		handle_mouse_button(0x07, e.value); break;
				}
				break;
			}
		}
	}
}

void
console_evdev_realizer [[gnu::noreturn]] (
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
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{inputdevice}");

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
		EvDev * input = new EvDev(main, fdi);
		if (!input) goto hiderror;
		main.add_device(input);
		input->save();
		input->set_mode();
	}

	{
		std::list<std::string> device_paths;
		device_paths.push_back("evdev." + std::string(basename_of(input_filename)));
		device_paths.push_back("evdev");
		main.autoconfigure(device_paths, false /* no display */, true /* has mouse */, true /* has keyboard */, true /* has NumLock */, true /* has LEDs */);
	}

	main.raise_acquire_signal();

	main.loop();

	throw EXIT_SUCCESS;
}

#endif
