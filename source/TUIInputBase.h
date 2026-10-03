/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_TUIINPUTBASE_H)
#define INCLUDE_TUIINPUTBASE_H

#include <list>
#include <iosfwd>
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
public:
	class EventHandler {
	protected:
		/// \name Event interception
		/// All of these should be implemented as call-to-parent for unhandled events.
		/// @{
		virtual bool ConsumerKey(uint_fast16_t k, uint_fast8_t m);
		virtual bool ExtendedKey(uint_fast16_t k, uint_fast8_t m);
		virtual bool FunctionKey(uint_fast16_t k, uint_fast8_t m);
		virtual bool UCS3(char32_t character);
		virtual bool Accelerator(char32_t character);
		virtual bool MouseMove(uint_fast16_t, uint_fast16_t, uint8_t);
		virtual bool MouseWheel(uint_fast8_t n, int_fast8_t v, uint_fast8_t m);
		virtual bool MouseButton(uint_fast8_t n, uint_fast8_t v, uint_fast8_t m);
		/// @}
		friend class TUIInputBase;

		/// \name Redispatching translated events
		/// @{
		void GenerateConsumerKey(uint_fast16_t k, uint_fast8_t m) { input.ConsumerKey(k, m); }
		void GenerateExtendedKey(uint_fast16_t k, uint_fast8_t m) { input.ExtendedKey(k, m); }
		void GenerateFunctionKey(uint_fast16_t k, uint_fast8_t m) { input.FunctionKey(k, m); }
		void GenerateUCS3(char32_t character) { input.UCS3(character); }
		void GenerateAccelerator(char32_t character) { input.Accelerator(character); }
		void GenerateMouseMove(uint_fast16_t n, uint_fast16_t v, uint8_t m) { input.MouseMove(n, v, m); }
		void GenerateMouseWheel(uint_fast8_t n, int_fast8_t v, uint_fast8_t m) { input.MouseWheel(n, v, m); }
		void GenerateMouseButton(uint_fast8_t n, uint_fast8_t v, uint_fast8_t m) { input.MouseButton(n, v, m); }
		/// @}

		EventHandler(TUIInputBase &);
		virtual ~EventHandler() = 0;
		TUIInputBase & input;
	};
	friend class EventHandler;

	/// \brief Convert graphic calculator keys into their plain character equivalents.
	class CalculatorKeypadToPrintables :
		public EventHandler
	{
	public:
		CalculatorKeypadToPrintables(TUIInputBase &);
		~CalculatorKeypadToPrintables();
	protected:
		virtual bool ExtendedKey(uint_fast16_t k, uint_fast8_t m);
	};

	/// \brief Convert mouse wheel events into cursor motion keys.
	class WheelToKeyboard :
		public EventHandler
	{
	public:
		WheelToKeyboard(TUIInputBase &);
		~WheelToKeyboard();
	protected:
		virtual bool MouseWheel(uint_fast8_t n, int_fast8_t v, uint_fast8_t m);
	};

	/// \brief Convert WASD gaming-style navigation into cursor motion keys.
	class GamingToCursorKeypad :
		public EventHandler
	{
	public:
		GamingToCursorKeypad(TUIInputBase &);
		~GamingToCursorKeypad();
	protected:
		virtual bool UCS3(char32_t character);
	};

	/// \brief Convert editing keys on the calculator keypad into the equivalent editing keypad keys.
	class CalculatorToEditingKeypad :
		public EventHandler
	{
	public:
		CalculatorToEditingKeypad(TUIInputBase &);
		~CalculatorToEditingKeypad();
	protected:
		virtual bool ExtendedKey(uint_fast16_t k, uint_fast8_t m);
	};

	/// \brief Convert some consumer keys to their equivalent into extended keys.
	class ConsumerKeysToExtendedKeys :
		public EventHandler
	{
	public:
		ConsumerKeysToExtendedKeys(TUIInputBase &);
		~ConsumerKeysToExtendedKeys();
	protected:
		virtual bool ConsumerKey(uint_fast16_t k, uint_fast8_t m);
	};

	/// \brief Convert some CUA1988 and Microsoft Windows keys into extended keys.
	class CUAToExtendedKeys :
		public EventHandler
	{
	public:
		CUAToExtendedKeys(TUIInputBase &);
		~CUAToExtendedKeys();
	protected:
		virtual bool UCS3(char32_t character);
		virtual bool Accelerator(char32_t character);
		virtual bool FunctionKey(uint_fast16_t k, uint_fast8_t m);
		virtual bool ExtendedKey(uint_fast16_t k, uint_fast8_t m);
	};

	/// \brief Convert a very basic universal set of control characters into extended keys.
	class ControlCharactersToExtendedKeys :
		public EventHandler
	{
	public:
		ControlCharactersToExtendedKeys(TUIInputBase &);
		~ControlCharactersToExtendedKeys();
	protected:
		virtual bool UCS3(char32_t character);
	};

	/// \brief Convert some common VIM keys into extended keys.
	class VIMToExtendedKeys :
		public ControlCharactersToExtendedKeys
	{
	public:
		VIMToExtendedKeys(TUIInputBase &);
		~VIMToExtendedKeys();
	protected:
		virtual bool UCS3(char32_t character);
	};

	/// \brief Convert some common less/more/pg keys into extended keys.
	class LessToExtendedKeys :
		public VIMToExtendedKeys
	{
	public:
		LessToExtendedKeys(TUIInputBase &);
		~LessToExtendedKeys();
	protected:
		virtual bool UCS3(char32_t character);
	};

	/// \brief Convert some common emacs navigation keys into cursor motion keys.
	class EMACSToCursorKeypad :
		public ControlCharactersToExtendedKeys
	{
	public:
		EMACSToCursorKeypad(TUIInputBase &);
		~EMACSToCursorKeypad();
	protected:
		virtual bool UCS3(char32_t character);
	};

	/// \brief Convert some conventional line discipline special keys into extended keys.
	class LineDisciplineToExtendedKeys :
		public ControlCharactersToExtendedKeys
	{
	public:
		LineDisciplineToExtendedKeys(TUIInputBase &);
		~LineDisciplineToExtendedKeys();
	protected:
		virtual bool UCS3(char32_t character);
	};

	int QueryInputFD() const;
protected:
	TUIInputBase(const TerminalCapabilities &, std::istream &, int);
	virtual ~TUIInputBase() = 0;

	/// \name Input
	/// @{
	void HandleInput(const char * b, std::size_t l);
	void BreakInput() { ecma48_decoder.AbortSequence(); }
	/// @}
private:
	const TerminalCapabilities & caps;
	UTF8Decoder utf8_decoder;
	ECMA48Decoder ecma48_decoder;
	std::istream & in;
	int in_fd;
	std::list<EventHandler*> handlers;

	/// \name Concrete UTF-8 Sink
	/// Our implementation of UTF8Decoder::UCS32CharacterSink
	/// @{
	virtual void ProcessDecodedUTF8(char32_t character, bool decoder_error, bool overlong);
	/// @}

	/// \name Concrete ECMA-48 Sink
	/// Our implementation of ECMA48Decoder::ECMA48ControlSequenceSink
	/// @{
	virtual void PrintableCharacter(bool, unsigned short, char32_t);
	virtual void ControlCharacter(char32_t);
	virtual void EscapeSequence(char32_t, char32_t);
	virtual void ControlSequence(char32_t, char32_t, char32_t);
	virtual void ControlString(char32_t, char32_t);
	/// @}

	/// \name Handler dispatchers
	/// These dispatch to the registered event handlers.
	/// @{
	void ConsumerKey(uint_fast16_t k, uint_fast8_t m);
	void ExtendedKey(uint_fast16_t k, uint_fast8_t m);
	void FunctionKey(uint_fast16_t k, uint_fast8_t m);
	void UCS3(char32_t character);
	void Accelerator(char32_t character);
	void MouseMove(uint_fast16_t, uint_fast16_t, uint8_t);
	void MouseWheel(uint_fast8_t n, int_fast8_t v, uint_fast8_t m);
	void MouseButton(uint_fast8_t n, uint_fast8_t v, uint_fast8_t m);
	/// @}

	void ExtendedKeyFromControlSequenceArgs(uint_fast16_t k, uint_fast8_t default_modifier = 0U);
	void ExtendedKeyFromFNKArgs();
	void ConsumerKeyFromFNKArgs();
	void XTermModifiedKey(unsigned k, uint_fast8_t modifier);
	void FunctionKeyFromFNKArgs();
	void FunctionKeyFromDECFNKNumber(unsigned k, uint_fast8_t modifier);
	void FunctionKeyFromDECFNKArgs();
	void FunctionKeyFromURxvtArgs(uint_fast8_t modifier);
	void FunctionKeyFromSCOFNK(unsigned k);
	void MouseFromXTerm1006Report(bool);
	void MouseFromDECLocatorReport();

	termios original_attr;
};

#endif
