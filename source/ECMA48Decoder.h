/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_ECMA48DECODER_H)
#define INCLUDE_ECMA48DECODER_H

#include <cstddef>
#include <stdint.h>

class ECMA48Decoder 
{
public:
	class ECMA48ControlSequenceSink
	{
	public:
		std::size_t argc;
		unsigned int args[16];
		bool seen_arg_digit;
		std::size_t slen;
		char str[2096];

		ECMA48ControlSequenceSink();
		void ResetControlSeqOrStr();
		void FinishArg(unsigned int d);
		virtual void PrintableCharacter(bool error, unsigned short shift_level, uint_fast32_t character) = 0;
		virtual void ControlCharacter(uint_fast32_t character) = 0;
		virtual void EscapeSequence(uint_fast32_t character, char last_intermediate) = 0;
		virtual void ControlSequence(uint_fast32_t character, char last_intermediate, char first_private_parameter) = 0;
		virtual void ControlString(uint_fast32_t character) = 0;
	};
	ECMA48Decoder(ECMA48ControlSequenceSink &, bool, bool, bool, bool, bool);
	void Process(uint_fast32_t character, bool decoder_error, bool overlong);
	void AbortSequence();
protected:
	ECMA48ControlSequenceSink & sink;
	enum { NORMAL, ESCAPE1, ESCAPE2, CONTROL1, CONTROL2, SHIFT2, SHIFT3, SHIFTA, DSTRING, OSTRING, PSTRING, ASTRING, SSTRING } state;
	const bool control_strings, allow_cancel, allow_7bit_extension, interix_shift, rxvt_function_keys;
	char first_private_parameter, last_intermediate;

	void ResetControlSeqOrStr();
	bool IsControl(uint_fast32_t);
	bool IsInStringControl(uint_fast32_t);
	bool IsIntermediate(uint_fast32_t);
	bool IsParameter(uint_fast32_t);
	void Escape1(uint_fast32_t character);
	void Escape2(uint_fast32_t character);
	void ControlCharacter(uint_fast32_t character);
	void ControlSequence(uint_fast32_t character);
	void ControlString(uint_fast32_t character);

};

#endif
