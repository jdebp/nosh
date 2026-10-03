/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define _BSD_SOURCE 1
#include <map>
#include <set>
#include <vector>
#include <cstring>
#include <cerrno>
#include "packed.h"
#include <unistd.h>
#include <fcntl.h>
#include "utils.h"
#include "fdutils.h"
#include "popt.h"
#include "ProcessEnvironment.h"
#include "FileDescriptorOwner.h"
#include "VirtualTerminalRealizer.h"

using namespace VirtualTerminalRealizer;

#if defined(__LINUX__) || defined(__linux__)

/* Linux PS/2 Mice **********************************************************
// **************************************************************************
*/

namespace {

/// On Linux, ps2mouse devices are character devices that speak the PS/2 mouse protocol.
class PS2Mouse :
	public HID
{
public:
	PS2Mouse(SharedHIDResources &, FileDescriptorOwner & fd);
	~PS2Mouse();

	/// \name overrides for the abstract I/O APIs
	/// @{
	void set_LEDs(const KeyboardLEDs &) {}
	void handle_input_events();
	/// @}
protected:
	enum { MOUSE_PS2_PACKETSIZE = 3 };
	enum { MOUSE_PS2_STDBUTTONS = 3 };
	enum { MOUSE_PS2_MAXBUTTON = 3 };
	unsigned char buffer[MOUSE_PS2_PACKETSIZE * 16];
	std::size_t offset;

	uint16_t stdbuttons;
	static int TranslateToStdButton(const unsigned short button);
	static short int GetOffset9 (uint8_t one, uint8_t two);
};

}

inline
PS2Mouse::PS2Mouse(
	SharedHIDResources & r,
	FileDescriptorOwner & fd
) :
	HID(r, fd),
	offset(0U),
	stdbuttons(0U)
{
}

PS2Mouse::~PS2Mouse()
{
	restore();
}

inline
int
PS2Mouse::TranslateToStdButton(
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
) {
	const int n(read(device.get(), reinterpret_cast<char *>(buffer) + offset, sizeof buffer - offset));
	if (0 > n) return;
	for (
		offset += n;
		offset >= MOUSE_PS2_PACKETSIZE;
		std::memmove(buffer, buffer + MOUSE_PS2_PACKETSIZE, sizeof buffer - MOUSE_PS2_PACKETSIZE * sizeof *buffer),
		offset -= MOUSE_PS2_PACKETSIZE * sizeof *buffer
	) {
 		if (short int dx = GetOffset9((buffer[0] & 0x10) >> 4, buffer[1]))
			handle_mouse_relpos(AXIS_X, dx);
		if (short int dy = GetOffset9((buffer[0] & 0x20) >> 5, buffer[2]))
			// Vertical movement is negated for some undocumented reason.
			handle_mouse_relpos(AXIS_Y, -dy);
		const unsigned newstdbuttons(buffer[0] & MOUSE_PS2_STDBUTTONS);
		for (unsigned short button(0U); button < MOUSE_PS2_MAXBUTTON; ++button) {
			const unsigned long mask(1UL << button);
			if ((stdbuttons & mask) != (newstdbuttons & mask)) {
				const bool down(newstdbuttons & mask);
				handle_mouse_button(TranslateToStdButton(button), down);
			}
		}
		stdbuttons = newstdbuttons;
	}
}

void
console_ps2_mouse_realizer [[gnu::noreturn]] (
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
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{ps2device}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}

	if (args.empty()) {
		die_missing_argument(prog, envs, "PS/2 mouse device name");
	}
	const char * input_filename(args.front());
	args.erase(args.begin());
	if (!args.empty()) die_unexpected_argument(prog, args, envs);

	Main main(prog, envs, mouse_primary);

	// Now open devices.

	{
		FileDescriptorOwner fdi(open_read_at(AT_FDCWD, input_filename));
		if (0 > fdi.get()) {
		hiderror:
			die_errno(prog, envs, input_filename);
		}
		PS2Mouse * input = new PS2Mouse(main, fdi);
		if (!input) goto hiderror;
		main.add_device(input);
		input->save();
		input->set_mode();
	}

	{
		std::list<std::string> device_paths;
		device_paths.push_back("eisa.pnp0F03." + std::string(basename_of(input_filename)));
		device_paths.push_back("eisa.pnp0F03");
		main.autoconfigure(device_paths, false /* no display */, true /* has mouse */, false /* no keyboard */, false /* no NumLock */, false /* no LEDs */);
	}

	main.raise_acquire_signal();

	main.loop();

	throw EXIT_SUCCESS;
}

#endif
