/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_KBDMAP_UTILS_H)
#define INCLUDE_KBDMAP_UTILS_H

#include <stdint.h>

extern
uint16_t
bsd_keycode_to_keymap_index (
	const uint16_t k
) ;
#if !defined(__LINUX__) && !defined(__linux__)
extern
uint16_t
usb_ident_to_keymap_index (
	const uint32_t ident
) ;
extern
uint16_t
linux_keycode_to_keymap_index (
	const uint16_t k
) ;
#endif

#endif
