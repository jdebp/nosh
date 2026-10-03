/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_ECMA48DECODER_H)
#define INCLUDE_ECMA48DECODER_H

#include <cstddef>
#include <stdint.h>
#include <vector>
#include <limits>

/// \brief Decode ECMA-48 character sequences from Unicode UTF-32.
///
/// This is not directly connected to a UTF-8 decoder, but is designed to be fed from something like one.
/// Characters are sent to the Process() function and the decoder outputs the decoded results to a "sink".
class ECMA48Decoder
{
public:
	/// \brief The abstract base class for sinking decoded ECMA-48 character sequences.
	class ECMA48ControlSequenceSink
	{
	public:
		ECMA48ControlSequenceSink();

		/// \name Decoder Helpers
		/// argument and control string storage API called by the decoder
		/// @{
		void ResetControlSequenceArgs();
		void ResetControlString();
		void AppendArgDigit(unsigned digit);
		void Colon();
		void SemiColon();
		void AppendControlString(char32_t character);
		void MinimumOneArg();
		/// @}

		/// \name Abstract API
		/// sink API called by the decoder, to be implemented by a derived class
		/// @{
		virtual void PrintableCharacter(bool error, unsigned short shift_level, char32_t character) = 0;
		virtual void ControlCharacter(char32_t character) = 0;
		virtual void EscapeSequence(char32_t character, char32_t first_intermediate) = 0;
		virtual void ControlSequence(char32_t character, char32_t last_intermediate, char32_t first_private_parameter) = 0;
		virtual void ControlString(char32_t string_char, char32_t term_char) = 0;
		/// @}

		typedef unsigned int argument_type;

		void SetZeroDefaultMode(bool v) { zero_replacement = (v ? 1U : 0U); }

	protected:
		/// \name Sink Helpers
		/// argument and control string retrieval API available to a derived class
		/// @{
		void CollapseArgsToSubArgs(std::size_t sub);
		bool HasNoSubArgsFrom(std::size_t sub);
		argument_type GetArgZeroIfEmpty(std::size_t sub, std::size_t index = 0U) const { return GetArgThisIfEmpty(sub, index, 0U); }
		argument_type GetArgOneIfEmpty(std::size_t sub, std::size_t index = 0U) const { return GetArgThisIfEmpty(sub, index, 1U); }
		argument_type GetArgOneIfZeroOrEmpty(std::size_t sub, std::size_t index = 0U) const { return GetArgThisIfZeroThisIfEmpty(sub, index, 1U, 1U); }
		argument_type GetArgThisIfZeroOrEmpty(std::size_t sub, argument_type d) const { return GetArgThisIfZeroThisIfEmpty(sub, 0U, d, d); }
		argument_type GetArgOneIfZeroThisIfEmpty(std::size_t sub, argument_type d) const { return GetArgThisIfZeroThisIfEmpty(sub, 0U, 1U, d); }
		argument_type GetArgZDIfZeroOneIfEmpty(std::size_t sub) const { return GetArgThisIfZeroThisIfEmpty(sub, 0U, zero_replacement, 1U); }
#if 0 // not used
		argument_type GetArgThisIfZeroOrEmpty(std::size_t sub, std::size_t index, argument_type d) const { return GetArgThisIfZeroThisIfEmpty(sub, index, d, d); }
#endif
		argument_type GetArgOneIfZeroThisIfEmpty(std::size_t sub, std::size_t index, argument_type d) const { return GetArgThisIfZeroThisIfEmpty(sub, index, 1U, d); }
		std::size_t QueryArgCount() const;
		std::size_t QuerySubArgCount(std::size_t sub) const;
		std::size_t QueryControlStringLength() const { return slen; }
		bool QueryArgNull(std::size_t sub, std::size_t index) const;
		char32_t QueryControlStringCharacter(std::size_t index) const { return index < slen ? str[index] : '\0'; }
		/// @}

	private:
		template <typename T>
		struct Nullable {
			Nullable() : isnull(true), value() {}
			bool isnull;
			T value;
			static T max() { return std::numeric_limits<T>().max(); }
		};
		typedef Nullable<argument_type> Number;
		typedef std::vector<Number> Sublist;
		typedef std::vector<Sublist> Arguments;

		Arguments args;
		std::size_t slen;
		char32_t str[2096];

		/// \brief This represents the ECMA-48 Zero Default Mode.
		///
		/// ZDM was superseded in ECMA-48:1986 and deprecated in ECMA-48:1991; and an explicit zero should actually be zero in a control sequence.
		unsigned zero_replacement;

		/// \name Sink Helper implementations
		/// @{
		argument_type GetArgThisIfEmpty(std::size_t sub, std::size_t index, argument_type d) const;
		argument_type GetArgThisIfZeroThisIfEmpty(std::size_t sub, std::size_t index, argument_type dz, argument_type de) const;
		/// @}
	};
	/// \brief Options for an ECMA48Decoder
	struct Options {
		Options(bool cs, bool can, bool b7, bool is, bool rfn, bool lfn) : control_strings(cs), allow_cancel(can), allow_7bit_extension(b7), interix_shift(is), rxvt_function_keys(rfn), linux_function_keys(lfn) {}
		Options() : control_strings(false), allow_cancel(false), allow_7bit_extension(false), interix_shift(false), rxvt_function_keys(false), linux_function_keys(false) {}
		bool control_strings, allow_cancel, allow_7bit_extension, interix_shift, rxvt_function_keys, linux_function_keys;
	};
	ECMA48Decoder(ECMA48ControlSequenceSink &, const Options & o);
	void Process(char32_t character, bool decoder_error, bool overlong);
	void AbortSequence();
protected:
	ECMA48ControlSequenceSink & sink;
	enum { NORMAL, ESCAPE, ESCAPE_NF, CONTROL1, CONTROL2, SHIFT2, SHIFT3, SHIFTA, SHIFTL, CONTROLSTRING, CONTROLSTRINGESCAPE } state;
	const Options options;
	uint_fast32_t first_private_parameter, saved_intermediate, string_char;

	void TerminateSequence(char32_t term_char);
	void ResetControlSeqAndStr();
	void Escape(char32_t character);
	void Escape_nF(char32_t character);
	void ControlCharacter(char32_t character);
	void ControlSequence(char32_t character);
	void ControlString(char32_t character);
	void ControlStringEscape(char32_t character);

};

#endif
