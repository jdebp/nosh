/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_U32STRING_H)
#define INCLUDE_U32STRING_H

#include <string>

typedef std::basic_string<char32_t> u32string;

extern u32string ConvertFromUTF8(const std::string &);
extern void ConvertToUTF8(std::string &, const u32string &);
extern void ConvertToUTF8(std::string &, const std::string &);
extern std::string::size_type LengthInUTF8(const u32string &);
extern std::string::size_type LengthAsUTF8(const std::string &);

#endif
