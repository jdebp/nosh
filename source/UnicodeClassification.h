/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_UNICODECLASSIFICATION_H)
#define INCLUDE_UNICODECLASSIFICATION_H

#include <stdint.h>

namespace UnicodeCategorization {

extern
bool 
IsMarkNonSpacing(uint32_t character);

extern
bool 
IsMarkEnclosing(uint32_t character);

extern
bool 
IsOtherFormat(uint32_t character);

extern
bool 
IsWideOrFull(uint32_t character);

extern
unsigned int 
CombiningClass(uint32_t character);

extern
bool 
IsDrawing(uint32_t character);

extern
bool 
IsHorizontallyRepeatable(uint32_t character);

}

#endif
