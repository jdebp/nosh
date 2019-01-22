/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_TUIINPUTBASE_H)
#define INCLUDE_TUIINPUTBASE_H

#include <termios.h>
#include <stdint.h>
class TerminalCapabilities;
#include "UTF8Decoder.h"
#include "ECMA48Decoder.h"

/// \brief Realize a UTF-8 and an ECMA-48 decoder onto a serial input device and translate to abstract input actions.
///
/// The actual reading from the input is done elsewhere; this being a sink for the data buffers that are read.
/// Input data become a sequence of calls to virtual functions representing various classes of input message.
class TUIInputBase :
	public UTF8Decoder::UCS32CharacterSink,
	public ECMA48Decoder::ECMA48ControlSequenceSink
{
protected:
	TUIInputBase(const TerminalCapabilities &, FILE *);
	virtual ~TUIInputBase() = 0;

	void HandleInput(const char * b, std::size_t l);
	void BreakInput() { ecma48_decoder.AbortSequence(); }

	// Sink API that users of this class must implement.
	virtual void ExtendedKey(uint_fast16_t k, uint_fast8_t m) = 0;
	virtual void FunctionKey(uint_fast16_t k, uint_fast8_t m) = 0;
	virtual void UCS3(uint_fast32_t character) = 0;
	virtual void Accelerator(uint_fast32_t character) = 0;
	virtual void MouseMove(uint_fast16_t, uint_fast16_t, uint8_t) = 0;
	virtual void MouseWheel(uint_fast8_t n, int_fast8_t v, uint_fast8_t m) = 0;
	virtual void MouseButton(uint_fast8_t n, uint_fast8_t v, uint_fast8_t m) = 0;
private:
	const TerminalCapabilities & caps;
	UTF8Decoder utf8_decoder;
	ECMA48Decoder ecma48_decoder;
	FILE * const in;

	// Our implementation of UTF8Decoder::UCS32CharacterSink
	virtual void ProcessDecodedUTF8(uint32_t character, bool decoder_error, bool overlong);

	// Our implementation of ECMA48Decoder::ECMA48ControlSequenceSink
	virtual void PrintableCharacter(bool, unsigned short, uint_fast32_t);
	virtual void ControlCharacter(uint_fast32_t);
	virtual void EscapeSequence(uint_fast32_t, char);
	virtual void ControlSequence(uint_fast32_t, char, char);
	virtual void ControlString(uint_fast32_t);

	void ExtendedKeyFromControlSequenceArgs(uint_fast16_t k, uint_fast8_t default_modifier = 0U);
	void FunctionKeyFromDECFNKArgs(uint_fast8_t default_modifier);
	void MouseFromXTerm1006Report(bool);
	void MouseFromDECLocatorReport();

	termios original_attr;
};

#endif
