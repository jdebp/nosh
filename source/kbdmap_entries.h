/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_KBDMAP_ENTRIES_H)
#define INCLUDE_KBDMAP_ENTRIES_H

#include "kbdmap.h"

/* Keyboard map entry shorthands ********************************************
// **************************************************************************
*/

#define SIMPLE(x) { 'p', { (x),(x),(x),(x),(x),(x),(x),(x),(x),(x),(x),(x),(x),(x),(x),(x) } }
#define SHIFTABLE(m,n,o,p,q,r,s,t) { 's', { (m),(n),NOOP(0),NOOP(0),(o),(p),NOOP(0),NOOP(0),(q),(r),NOOP(0),NOOP(0),(s),(t),NOOP(0),NOOP(0) } }
#define SHIFTABLE_CONTROL(m,n,o,p,c,q,r,s,t) { 's', { (m),(n),(c),(c),(o),(p),(c),(c),(q),(r),(c),(c),(s),(t),(c),(c) } }
#define CAPSABLE_CONTROL(m,n,o,p,c,q,r,s,t) { 'c', { (m),(n),(c),(c),(o),(p),(c),(c),(q),(r),(c),(c),(s),(t),(c),(c) } }
#define NUMABLE(m,n) { 'n', { (m),(n),(m),(n),NOOP(0),NOOP(0),NOOP(0),NOOP(0),(m),(n),(m),(n),NOOP(0),NOOP(0),NOOP(0),NOOP(0) } }
#define NUMABLE_CONTROL(m,n,o) { 'n', { (m),(n),(m),(o),NOOP(0),NOOP(0),NOOP(0),NOOP(0),(m),(n),(m),(o),NOOP(0),NOOP(0),NOOP(0),NOOP(0) } }
#if !defined(__LINUX__) && !defined(__linux__)
#define FUNCABLE1(m) { 'f', { FUNC(m),FUNC(m),FUNC(m),FUNC(m),SCRN((m)-1U),SCRN((m)-1U),SCRN((m)-1U),SCRN((m)-1U),FUNC(m),FUNC(m),FUNC(m),FUNC(m),SCRN((m)-1U),SCRN((m)-1U),SCRN((m)-1U),SCRN((m)-1U) } }
#define FUNCABLE2(m,e) { 'f', { EXTE(e),FUNC(m),FUNC(m),FUNC(m),SCRN((m)-1U),SCRN((m)-1U),SCRN((m)-1U),SCRN((m)-1U),EXTE(e),FUNC(m),FUNC(m),FUNC(m),SCRN((m)-1U),SCRN((m)-1U),SCRN((m)-1U),SCRN((m)-1U) } }
#endif
#if defined(__LINUX__) || defined(__linux__)
#define FUNKABLE1(m) { 'f', { FUNK(m),FUNK((m)+12U),FUNK((m)+24U),FUNK((m)+36U),SCRN((m)-1U),SCRN((m)+12U-1U),SCRN((m)+24U-1U),SCRN((m)+36U-1U),FUNK(m),FUNK((m)+12U),FUNK((m)+24U),FUNK((m)+36U),SCRN((m)-1U),SCRN((m)+12U-1U),SCRN((m)+24U-1U),SCRN((m)+36U-1U) } }
#define FUNKABLE2(m,e) { 'f', { EXTE(e),FUNK((m)+12U),FUNK((m)+24U),FUNK((m)+36U),SCRN((m)-1U),SCRN((m)+12U-1U),SCRN((m)+24U-1U),SCRN((m)+36U-1U),EXTE(e),FUNK((m)+12U),FUNK((m)+24U),FUNK((m)+36U),SCRN((m)-1U),SCRN((m)+12U-1U),SCRN((m)+24U-1U),SCRN((m)+36U-1U) } }
#endif

#define	NOOP(x)	 (((x) & 0x00FFFFFF) << 0U)
#define UCSA(x) ((((x) & 0x00FFFFFF) << 0U) | KBDMAP_ACTION_UCS3)
#define MMNT(x) ((((x) & 0x0000FFFF) << 8U) | KBDMAP_ACTION_MODIFIER | KBDMAP_MODIFIER_CMD_MOMENTARY)
#define LTCH(x) ((((x) & 0x0000FFFF) << 8U) | KBDMAP_ACTION_MODIFIER | KBDMAP_MODIFIER_CMD_LATCH)
#define LOCK(x) ((((x) & 0x0000FFFF) << 8U) | KBDMAP_ACTION_MODIFIER | KBDMAP_MODIFIER_CMD_LOCK)
#define SCRN(x) ((((x) & 0x0000FFFF) << 8U) | KBDMAP_ACTION_SCREEN)
#define SYST(x) ((((x) & 0x0000FFFF) << 8U) | KBDMAP_ACTION_SYSTEM)
#define CONS(x) ((((x) & 0x0000FFFF) << 8U) | KBDMAP_ACTION_CONSUMER)
#define EXTE(x) ((((x) & 0x0000FFFF) << 8U) | KBDMAP_ACTION_EXTENDED)
#define EXTN(x) ((((x) & 0x0000FFFF) << 8U) | KBDMAP_ACTION_EXTENDED1)
#if !defined(__LINUX__) && !defined(__linux__)
#define FUNC(x) ((((x) & 0x0000FFFF) << 8U) | KBDMAP_ACTION_FUNCTION)
#endif
#define FUNK(x) ((((x) & 0x0000FFFF) << 8U) | KBDMAP_ACTION_FUNCTION1)

#endif
