/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define _BSD_SOURCE 1
#include <map>
#include <set>
#include <stack>
#include <vector>
#include <iostream>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <sys/mman.h>
#if defined(__FreeBSD__) || defined (__DragonFly__)
#	include <sys/mouse.h>
#	include <sys/kbio.h>
#endif
#include "packed.h"
#include <unistd.h>
#include <fcntl.h>
#include "utils.h"
#include "fdutils.h"
#include "popt.h"
#include "ProcessEnvironment.h"
#include "FileDescriptorOwner.h"
#include "kbdmap_utils.h"
#include "VirtualTerminalRealizer.h"

using namespace VirtualTerminalRealizer;

#if defined(__FreeBSD__) || defined (__DragonFly__)

/* FreeBSD sysmouse HIDs ****************************************************
// **************************************************************************
*/

namespace {

/// On the BSDs, sysmouse devices are character devices with a terminal line discipline that speak the sysmouse protocol.
class SysMouse :
	public HIDWithLineDiscipline
{
public:
	SysMouse(SharedHIDResources &, FileDescriptorOwner & fd);
	~SysMouse();

	/// \name overrides for the abstract I/O APIs
	/// @{
	virtual void set_LEDs(const KeyboardLEDs &) {}
	virtual void handle_input_events();
	virtual void save();
	virtual void set_mode();
	/// @}
protected:
	unsigned char buffer[MOUSE_SYS_PACKETSIZE * 16];
	std::size_t offset;

	int level;
	void restore();
	virtual termios make_raw ( const termios & );

	uint16_t stdbuttons, extbuttons;
	static int TranslateToStdButton(const unsigned short button);

private:
	static bool IsSync (char b);
	static uint8_t Extend7to8 (uint8_t v);
	static short int GetOffset8 (uint8_t one, uint8_t two);
	static short int GetOffset7 (uint8_t one, uint8_t two);
};

}

inline
SysMouse::SysMouse(
	SharedHIDResources & r,
	FileDescriptorOwner & fd
) :
	HIDWithLineDiscipline(r, fd),
	offset(0U),
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
SysMouse::TranslateToStdButton(
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
) {
	const int n(read(device.get(), reinterpret_cast<char *>(buffer) + offset, sizeof buffer - offset));
	if (0 > n) return;
	// Because of an unavoidable race caused by the way that the FreeBSD mouse driver works, we have to cope with both the level 0 and the level 1 protocols.
	// Fortunately, the Mouse Systems protocol (according to MS own doco) allows for extensibility, and is synchronized with an initial flag byte.
	for (
		offset += n;
		;
	) {
		while (offset > 0U && !IsSync(buffer[0])) {
			std::memmove(buffer, buffer + 1, sizeof buffer - sizeof *buffer);
			--offset;
		}
		if (offset < MOUSE_MSC_PACKETSIZE) break;
 		if (short int dx = GetOffset8(buffer[1], buffer[3]))
			handle_mouse_relpos(AXIS_X, dx);
		if (short int dy = GetOffset8(buffer[2], buffer[4]))
			// Vertical movement is negated in the original MouseSystems device protocol.
			handle_mouse_relpos(AXIS_Y, -dy);
		const bool ext(offset >= MOUSE_SYS_PACKETSIZE && !IsSync(buffer[5]) && !IsSync(buffer[6]) && !IsSync(buffer[7]));
	       	if (ext) {
			if (short int dz = GetOffset7(buffer[5], buffer[6]))
				handle_mouse_relpos(V_SCROLL, dz);
		}
		const unsigned newstdbuttons(buffer[0] & MOUSE_SYS_STDBUTTONS);
		for (unsigned short button(0U); button < MOUSE_MSC_MAXBUTTON; ++button) {
			const unsigned long mask(1UL << button);
			if ((stdbuttons & mask) != (newstdbuttons & mask)) {
				const bool up(newstdbuttons & mask);
				handle_mouse_button(TranslateToStdButton(button), !up);
			}
		}
		stdbuttons = newstdbuttons;
	       	if (ext) {
			const unsigned newextbuttons(buffer[7] & MOUSE_SYS_EXTBUTTONS);
			for (unsigned short button(0U); button < (MOUSE_SYS_MAXBUTTON - MOUSE_MSC_MAXBUTTON); ++button) {
				const unsigned long mask(1UL << button);
				if ((extbuttons & mask) != (newextbuttons & mask)) {
					const bool up(newextbuttons & mask);
					handle_mouse_button(button + MOUSE_MSC_MAXBUTTON, !up);
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

// Like the BSD cfmakeraw() and our ::make_raw(), but even harder still than both.
termios
SysMouse::make_raw (
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

void
SysMouse::save()
{
	if (-1 == device.get()) return;

	HIDWithLineDiscipline::save();
	ioctl(device.get(), MOUSE_GETLEVEL, &level);
}

void
SysMouse::set_mode()
{
	if (-1 == device.get()) return;

	HIDWithLineDiscipline::set_mode();
	/// Beneath the line discipline the mouse protocol level needs to be set to the common sysmouse protocol level.
	int one(1);
	ioctl(device.get(), MOUSE_SETLEVEL, &one);
}

void
SysMouse::restore()
{
	if (-1 == device.get()) return;

	ioctl(device.get(), MOUSE_SETLEVEL, &level);
	HIDWithLineDiscipline::restore();
}

void
console_pcat_mouse_realizer /*[[gnu::noreturn]]*/ (
	const char * & next_prog,
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
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{atmousedevice}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}

	if (args.empty()) {
		die_missing_argument(prog, envs, "AT mouse device name");
	}
	const char * input_filename(args.front());
	args.erase(args.begin());
	if (!args.empty()) die_unexpected_argument(prog, args, envs);

	Main main(prog, envs, mouse_primary);

	// Now open devices.

#if defined(__FreeBSD__)
	std::shared_ptr<HID> realdev;	// Retains ownership of a HID that we do not add to the input list.
#endif
	{
		FileDescriptorOwner fdi(open_readwriteexisting_at(AT_FDCWD, input_filename));
		if (0 > fdi.get()) {
		hiderror:
			die_errno(prog, envs, input_filename);
		}
#if defined(__FreeBSD__)
		// FreeBSD's /dev/sysmouse and /dev/psmN devices are not kevent-capable.
		// No-one seems to have noticed, alas, because moused uses select() not kevent().
		// So we have to substitute a pipe from a cat process, which is.
		int fds[2];
		if (0 > pipe_close_on_exec(fds)) {
			die_errno(prog, envs, "pipe");
		}
		pid_t child(fork());
		if (0 > child) {
			die_errno(prog, envs, "fork");
		} else
		if (0 == child) {
			// dup2() does not set O_CLOEXEC, but we need to turn off non-blocking mode which cat cannot cope with.
			dup2(fdi.get(), STDIN_FILENO);
			dup2(fds[1], STDOUT_FILENO);
			set_non_blocking(STDIN_FILENO, false);
			set_non_blocking(STDOUT_FILENO, false);
			// fd1.get() and the original pipe descriptors are all open O_CLOEXEC and will be closed when we exec cat.
			args.clear();
			args.push_back("cat");
			args.push_back("--");
			next_prog = arg0_of(args);
			return;
		} else
		{
			// Close the write end of the pipe in the parent so that EOF propagates.
			close(fds[1]);
			// The original SysMouse object retains ownership of the device file descriptor, so that ioctl()s are cleaned up at exit.
			realdev = std::shared_ptr<HID>(new SysMouse(main, fdi));
			if (!realdev) goto hiderror;
			// We use another object to represent the write end of the pipe, and that is our input device.
			FileDescriptorOwner fd0(fds[0]);
			SysMouse * input = new SysMouse(main, fd0);
			if (!input) goto hiderror;
			main.add_device(input);
			input->save();
			input->set_mode();
		}
#else
		SysMouse * input = new SysMouse(main, fdi);
		if (!input) goto hiderror;
		main.add_device(input);
		input->save();
		input->set_mode();
#endif
	}

	{
		std::list<std::string> device_paths;
		device_paths.push_back("eisa.pnp0F01." + std::string(basename_of(input_filename)));
		device_paths.push_back("eisa.pnp0F01");
		main.autoconfigure(device_paths, false /* no display */, true /* has mouse */, false /* no keyboard */, false /* no NumLock key */, false /* no LEDs */);
	}

	main.raise_acquire_signal();

	main.loop();

	throw EXIT_SUCCESS;
}

#endif

/* FreeBSD atkbd HIDs *******************************************************
// **************************************************************************
*/

namespace {

/// On the BSDs, atkbd devices are character devices with a terminal line discipline that speak the "kbio" protocol.
/// We provide this on Linux too, but it isn't particularly useful.
typedef HIDSpeakingKBIO ATKeyboard;

}

void
console_pcat_keyboard_realizer [[gnu::noreturn]] (
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
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{atkeyboarddevice}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}

	if (args.empty()) {
		die_missing_argument(prog, envs, "AT keyboard device name");
	}
	const char * input_filename(args.front());
	args.erase(args.begin());
	if (!args.empty()) die_unexpected_argument(prog, args, envs);

	Main main(prog, envs, mouse_primary);

	// Now open devices.

#if defined(__FreeBSD__)
	std::shared_ptr<HID> realdev;	// Retains ownership of a HID that we do not add to the input list.
#endif
	{
		FileDescriptorOwner fdi(open_readwriteexisting_at(AT_FDCWD, input_filename));
		if (0 > fdi.get()) {
		hiderror:
			die_errno(prog, envs, input_filename);
		}
#if defined(__FreeBSD__) && 0
		// FreeBSD's /dev/kbdmux, /dev/atkbd, and /dev/vkbdN devices are not kevent-capable.
		// No-one seems to have noticed, alas.
		// So we have to substitute a pipe from a cat process, which is.
		int fds[2];
		if (0 > pipe_close_on_exec(fds)) {
			die_errno(prog, envs, "pipe");
		}
		pid_t child(fork());
		if (0 > child) {
			die_errno(prog, envs, "fork");
		} else
		if (0 == child) {
			// dup2() does not set O_CLOEXEC, but we need to turn off non-blocking mode which cat cannot cope with.
			dup2(fdi.get(), STDIN_FILENO);
			dup2(fds[1], STDOUT_FILENO);
			set_non_blocking(STDIN_FILENO, false);
			set_non_blocking(STDOUT_FILENO, false);
			// fd1.get() and the original pipe descriptors are all open O_CLOEXEC and will be closed when we exec cat.
			args.clear();
			args.push_back("cat");
			args.push_back("--");
			next_prog = arg0_of(args);
			return;
		} else
		{
			// Close the write end of the pipe in the parent so that EOF propagates.
			close(fds[1]);
			// The original SysMouse object retains ownership of the device file descriptor, so that ioctl()s are cleaned up at exit.
			realdev = std::shared_ptr<HID>(new ATKeyboard(main, fd0));
			FileDescriptorOwner fd0(fds[0]);
			// We use another object to represent the write end of the pipe, and that is our input device.
			ATKeyboard * input = new ATKeyboard(main, fd0);
			if (!input) goto hiderror;
			main.add_device(input);
			input->save();
			input->set_mode();
		}
#else
		ATKeyboard * input = new ATKeyboard(main, fdi);
		if (!input) goto hiderror;
		main.add_device(input);
		input->save();
		input->set_mode();
#endif
	}

	{
		std::list<std::string> device_paths;
		device_paths.push_back("eisa.pnp0301." + std::string(basename_of(input_filename)));
		device_paths.push_back("eisa.pnp0301");
		main.autoconfigure(device_paths, false /* no display */, false /* no mouse */, true /* has keyboard */, true /* has NumLock key */, true /* has LEDs */);
	}

	main.raise_acquire_signal();

	main.loop();

	throw EXIT_SUCCESS;
}
