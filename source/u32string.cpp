/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include "u32string.h"
#include "UTF8Decoder.h"
#include "UTF8Encoder.h"

namespace {

struct UTF8DecoderHelper1 :
	public UTF8Decoder::UCS32CharacterSink
{
	UTF8DecoderHelper1(u32string & t) : s(t) {}
	virtual void ProcessDecodedUTF8(char32_t character, bool decoder_error, bool overlong);
protected:
	u32string & s;
};

void
UTF8DecoderHelper1::ProcessDecodedUTF8(char32_t character, bool /*decoder_error*/, bool /*overlong*/)
{
	s.append(&character, 1U);
}

struct UTF8DecoderHelper2 :
	public UTF8Decoder::UCS32CharacterSink
{
	UTF8DecoderHelper2(std::string::size_type & l) : length(l) {}
	virtual void ProcessDecodedUTF8(char32_t character, bool decoder_error, bool overlong);
protected:
	std::string::size_type & length;
};

void
UTF8DecoderHelper2::ProcessDecodedUTF8(char32_t /*character*/, bool /*decoder_error*/, bool /*overlong*/)
{
	++length;
}

struct UTF8EncoderHelper1 :
	public UTF8Encoder::UTF8CharacterSink
{
	UTF8EncoderHelper1(std::string::size_type & z) : size(z) {}
	virtual void ProcessEncodedUTF8(std::size_t l, const char * p);
protected:
	std::string::size_type & size;
};

void
UTF8EncoderHelper1::ProcessEncodedUTF8(std::size_t l, const char *)
{
	size += l;
}

struct UTF8EncoderHelper2 :
	public UTF8Encoder::UTF8CharacterSink
{
	UTF8EncoderHelper2(std::string & t) : s(t) {}
	virtual void ProcessEncodedUTF8(std::size_t l, const char * p);
protected:
	std::string & s;
};

void
UTF8EncoderHelper2::ProcessEncodedUTF8(std::size_t l, const char * p)
{
	s.append(p, l);
}

}

u32string
ConvertFromUTF8 (
	const std::string & s
) {
	u32string r;
	UTF8DecoderHelper1 helper(r);
	UTF8Decoder decoder(helper);
	for (std::string::const_iterator p(s.begin()), e(s.end()); p != e; ++p)
		decoder.Process(*p);
	return r;
}

void
ConvertToUTF8(
	std::string & d,
	const u32string & s
) {
	UTF8EncoderHelper2 helper(d);
	UTF8Encoder encoder(helper);
	for (u32string::const_iterator p(s.begin()), e(s.end()); p != e; ++p)
		encoder.Process(*p);
}

void
ConvertToUTF8(
	std::string & d,
	const std::string & s
) {
	UTF8EncoderHelper2 helper(d);
	UTF8Encoder encoder(helper);
	for (std::string::const_iterator p(s.begin()), e(s.end()); p != e; ++p)
		encoder.Process(static_cast<unsigned char>(*p));
}

std::string::size_type
LengthInUTF8 (
	const u32string & value
) {
	std::string::size_type length(0U);
	UTF8EncoderHelper1 helper(length);
	UTF8Encoder encoder(helper);
	for (u32string::const_iterator p(value.begin()), e(value.end()); p != e; ++p)
		encoder.Process(*p);
	return length;
}

std::string::size_type
LengthAsUTF8 (
	const std::string & value
) {
	std::string::size_type length(0U);
	UTF8DecoderHelper2 helper(length);
	UTF8Decoder decoder(helper);
	for (std::string::const_iterator p(value.begin()), e(value.end()); p != e; ++p)
		decoder.Process(*p);
	return length;
}
