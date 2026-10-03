/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define _BSD_SOURCE 1
#include <map>
#include <set>
#include <stack>
#include <deque>
#include <vector>
#include <cstring>
#include <iostream>
#include <cerrno>
#if !defined(__LINUX__) && !defined(__linux__)
#	include <sys/uio.h>
#	include <dev/usb/usb.h>
#	include <dev/usb/usbhid.h>
#	if !defined(__OpenBSD__) && !defined(__NetBSD__)
#	include <dev/usb/usbdi.h>
#	include <dev/usb/usb_ioctl.h>
#	include <dev/usb/usb_endian.h>
#	endif
#endif
#include "packed.h"
#include <unistd.h>
#include <inttypes.h>
#include <fcntl.h>
#include "utils.h"
#include "fdutils.h"
#include "pack.h"
#include "popt.h"
#include "ProcessEnvironment.h"
#include "FileDescriptorOwner.h"
#include "kbdmap_utils.h"
#include "VirtualTerminalRealizer.h"

using namespace VirtualTerminalRealizer;

/* Realizing a virtual terminal onto a set of physical devices **************
// **************************************************************************
*/

namespace {

enum {
	HUG_LAST_SYSTEM_KEY		= 0x00BF,

	HUK_NUMLOCK			= 0x0053,

	HUC_CONSUMER_CONTROL		= 0x0001,
	HUC_APPLICATION_LAUNCH		= 0x0180,	// FIXME: unused
	HUC_GENERIC_GUI_CONTROLS	= 0x0200,	// FIXME: unused
#if !defined(HUC_AC_ZOOM)
	HUC_AC_ZOOM			= 0x022F,
#endif
#if !defined(HUC_AC_SCROLL)
	HUC_AC_SCROLL			= 0x0235,
#endif
#if !defined(HUC_AC_PAN)
	HUC_AC_PAN			= 0x0238,
#endif

#if !defined(HUP_LEDS) && defined(HUP_LED)
	HUP_LEDS			= HUP_LED,
#endif
};

}

#if !defined(__LINUX__) && !defined(__linux__)

namespace {

#if !defined(USB_DEVICE_DIR)
#define USB_DEVICE_DIR "usb"
#endif
#if !defined(USB_GENERIC_NAME)
#define USB_GENERIC_NAME "ugen"
#endif

const std::string device_path_prefix(std::string(USB_DEVICE_DIR) + ".");
const std::string device_path_ugen_prefix((device_path_prefix + USB_GENERIC_NAME) + ".");
const std::string device_path_uhid_prefix((device_path_prefix + "uhid") + ".");

class USBHIDCommon :
	public HID
{
public:
	virtual ~USBHIDCommon() {}
	void set_LEDs(const KeyboardLEDs &);
	void handle_input_events();

	/// \name information parsed from the report description
	/// @{
	bool has_mouse() const;
	bool has_keyboard() const;
	bool has_numlock_key() const;
	bool has_LEDs() const;
	/// @}

protected:
	typedef uint32_t UsageID;
	static uint16_t HID_USAGE(UsageID d) { return d & 0xFFFF; }
#if defined(__OpenBSD__)
	enum {
		USB_SHORT_XFER_OK	= USBD_SHORT_XFER_OK,
	};
#endif
	enum {
#if !defined(__OpenBSD__)
		HUL_NUM_LOCK	= 0x0001,
		HUL_CAPS_LOCK	= 0x0002,
		HUL_SCROLL_LOCK	= 0x0003,
		HUL_COMPOSE	= 0x0004,
		HUL_KANA	= 0x0005,
#endif
		HUL_SHIFT	= 0x0007,
	};

	struct ParserContext {
		struct Item {
			Item(uint32_t pd, std::size_t ps): d(pd), s(ps) {}
			Item() : d(), s() {}
			uint32_t d;
			std::size_t s;
		};
		Item globals[16], locals[16];
		bool has_min, has_max;
		ParserContext() : has_min(false), has_max(false) { wipe_globals(); wipe_locals(); }
		void clear_locals() { wipe_locals(); idents.clear(); has_min = has_max = false; }
		const Item & logical_minimum() const { return globals[1]; }
		const Item & logical_maximum() const { return globals[2]; }
		const Item & report_size() const { return globals[7]; }
		const Item & report_id() const { return globals[8]; }
		const Item & report_count() const { return globals[9]; }
		UsageID ident() const { return ident(usage(), page()); }
		UsageID ident_minimum() const { return ident(usage_minimum(), page()); }
		UsageID ident_maximum() const { return ident(usage_maximum(), page()); }
		void add_ident() { idents.push_back(ident()); }
		UsageID take_variable_ident() {
			if (!idents.empty()) {
				const UsageID v(idents.front());
				idents.pop_front();
				return v;
			} else
			if (has_min) {
				const UsageID v(ident_minimum());
				if (has_max && v < ident_maximum()) {
					// Reset to full size because pages are not min/max boundaries and we don't want page wraparound.
					locals[1].d = v + 1;
					locals[1].s = 4;
				}
				return v;
			} else
				return ident();
		}
	protected:
		std::deque<UsageID> idents;
		static UsageID ident(const Item & u, const Item & p) { return u.s < 3 ? HID_USAGE2(p.d, u.d) : u.d; }
		const Item & page() const { return globals[0]; }
		const Item & usage() const { return locals[0]; }
		const Item & usage_minimum() const { return locals[1]; }
		const Item & usage_maximum() const { return locals[2]; }
		void wipe_locals() { for (std::size_t i(0); i < sizeof locals/sizeof *locals; ++i) locals[i].d = locals[i].s = 0U; }
		void wipe_globals() { for (std::size_t i(0); i < sizeof globals/sizeof *globals; ++i) globals[i].d = globals[i].s = 0U; }
	};
	struct ReportField {
		static int64_t mm(const ParserContext::Item & control, const ParserContext::Item & value)
		{
			const uint32_t control_signbit(1U << (control.s * 8U - 1U));
			const uint32_t value_signbit(1U << (value.s * 8U - 1U));
			if ((control.d & control_signbit) && (value.d & value_signbit)) {
				const uint32_t v(value.d | ~(value_signbit - 1U));
				return static_cast<int32_t>(v);
			} else
				return static_cast<int64_t>(value.d);
		}
		ReportField(std::size_t p, std::size_t l): pos(p), len(l) {}
		std::size_t pos, len;
	};
	struct InputVariable : public ReportField {
		InputVariable(UsageID i, std::size_t p, std::size_t l, bool r, const ParserContext::Item & mi, const ParserContext::Item & ma): ReportField(p, l), relative(r), ident(i), min(mm(mi, mi)), max(mm(mi, ma)) {}
		bool relative;
		UsageID ident;
		int64_t min, max;
	};
	struct InputIndex : public ReportField {
		InputIndex(std::size_t p, std::size_t l, UsageID umi, UsageID uma, const ParserContext::Item & imi, const ParserContext::Item & ima): ReportField(p, l), min_ident(umi), max_ident(uma), min_index(mm(imi, imi)), max_index(mm(imi, ima)) {}
		UsageID min_ident, max_ident;
		int64_t min_index, max_index;
	};
	struct InputReportDescription {
		InputReportDescription() : bits(0U), bytes(0U), has_mouse(false), has_keyboard(false), has_numlock_key(false) {}
		std::size_t bits, bytes;
		bool has_mouse, has_keyboard, has_numlock_key;
		typedef std::list<InputVariable> Variables;
		Variables variables;
		typedef std::list<InputIndex> Indices;
		Indices array_indices;
	};
	struct OutputVariable : public ReportField {
		OutputVariable(UsageID i, std::size_t p, std::size_t l): ReportField(p, l), ident(i) {}
		UsageID ident;
	};
	struct OutputIndex : public ReportField {
		OutputIndex(std::size_t p, std::size_t l, UsageID umi, UsageID uma): ReportField(p, l), min_ident(umi), max_ident(uma) {}
		UsageID min_ident, max_ident;
	};
	struct OutputReportDescription {
		OutputReportDescription() : bits(0U), bytes(0U), has_LEDs(false), has_compose_LED(false) {}
		std::size_t bits, bytes;
		bool has_LEDs;
		bool has_compose_LED;
		typedef std::list<OutputVariable> Variables;
		Variables variables;
		typedef std::list<OutputIndex> Indices;
		Indices array_indices;
	};
	typedef std::map<uint8_t, InputReportDescription> InputReports;
	typedef std::map<uint8_t, OutputReportDescription> OutputReports;
	typedef std::set<UsageID> KeysPressed;

	USBHIDCommon(SharedHIDResources &, FileDescriptorOwner & fd);

	char output_buffer[4096];
	char input_buffer[4096];
	std::size_t input_offset;
	bool has_report_ids;
	InputReports input_reports;
	OutputReports output_reports;
	bool has_compose_LED;

	bool read_report_description(unsigned char * const b, const std::size_t maxlen, const std::size_t actlen);
	bool IsInRange(const InputVariable & f, uint32_t);
	uint32_t GetUnsignedField(const ReportField & f, std::size_t);
	bool IsInRange(const InputVariable & f, int32_t);
	int32_t GetSignedField(const ReportField & f, std::size_t);

	virtual void write_output_report(const uint8_t, std::size_t) = 0;
	void SetUnsignedField(const ReportField & f, std::size_t, uint32_t);

	static unsigned short TranslateToStdButton(const unsigned short button);
	void handle_mouse_movement(const InputReportDescription & report, const InputVariable & f, const MouseAxis axis);
	using HID::handle_mouse_button;
	void handle_mouse_button(UsageID ident, bool down);

	KeysPressed keys;
	static void handle_key(bool & seen_keyboard, KeysPressed & keys, UsageID ident, bool down);
};

}

inline
USBHIDCommon::USBHIDCommon(
	SharedHIDResources & r,
	FileDescriptorOwner & fd
) :
	HID(r, fd),
	input_offset(0U),
	has_report_ids(false),
	has_compose_LED(false)
{
}

inline
uint16_t
USBHIDCommon::TranslateToStdButton(
	const uint16_t button
) {
	switch (button) {
		case 1U:	return 2U;
		case 2U:	return 1U;
		default:	return button;
	}
}

inline
uint32_t
USBHIDCommon::GetUnsignedField(
	const ReportField & f,
	std::size_t report_size
) {
	if (0U == f.len) return 0U;
	// Byte-aligned fields can be optimized.
	if (0U == (f.pos & 7U)) {
		const std::size_t bytepos(f.pos >> 3U);
		if (bytepos >= report_size) return 0U;
		if (f.len <= 8U) {
			uint8_t v(*reinterpret_cast<const uint8_t *>(input_buffer + bytepos));
			if (f.len < 8U) v &= 0xFF >> (8U - f.len);
			return v;
		}
		if (f.len <= 16U) {
			uint16_t v(le16toh(*reinterpret_cast<const uint16_t *>(input_buffer + bytepos)));
			if (f.len < 16U) v &= 0xFFFF >> (16U - f.len);
			return v;
		}
		if (f.len <= 32U) {
			uint32_t v(le32toh(*reinterpret_cast<const uint32_t *>(input_buffer + bytepos)));
			if (f.len < 32U) v &= 0xFFFFFFFF >> (32U - f.len);
			return v;
		}
	}
	// 1-bit fields can be optimized.
	if (1U == f.len) {
		const std::size_t bytepos(f.pos >> 3U);
		if (bytepos >= report_size) return 0U;
		const uint8_t v(*reinterpret_cast<const uint8_t *>(input_buffer + bytepos));
		return (v >> (f.pos & 7U)) & 1U;
	}
	/// \bug FIXME: This doesn't handle unaligned >1-bit fields.
	std::clog << "DEBUG: Unaligned >1 bit unsigned field at " << f.pos << " length " << f.len << " in USB input report\n";
	return 0U;
}

inline
bool
USBHIDCommon::IsInRange(
	const InputVariable & f,
	uint32_t v
) {
	return v <= f.max && v >= f.min;
}

inline
int32_t
USBHIDCommon::GetSignedField(
	const ReportField & f,
	std::size_t report_size
) {
	if (0U == f.len) return 0U;
	// Byte-aligned fields can be optimized.
	if (0U == (f.pos & 7U)) {
		const std::size_t bytepos(f.pos >> 3U);
		if (bytepos >= report_size) return 0U;
		if (f.len <= 8U) {
			uint8_t v(*reinterpret_cast<const uint8_t *>(input_buffer + bytepos));
			if (f.len < 8U) v &= 0xFF >> (8U - f.len);
			const uint8_t signbit(1U << (f.len - 1U));
			if (v & signbit) v |= ~(signbit - 1U);
			return static_cast<int8_t>(v);
		}
		if (f.len <= 16U) {
			uint16_t v(le16toh(*reinterpret_cast<const uint16_t *>(input_buffer + bytepos)));
			if (f.len < 16U) v &= 0xFFFF >> (16U - f.len);
			const uint16_t signbit(1U << (f.len - 1U));
			if (v & signbit) v |= ~(signbit - 1U);
			return static_cast<int16_t>(v);
		}
		if (f.len <= 32U) {
			uint32_t v(le32toh(*reinterpret_cast<const uint32_t *>(input_buffer + bytepos)));
			if (f.len < 32U) v &= 0xFFFFFFFF >> (32U - f.len);
			const uint32_t signbit(1U << (f.len - 1U));
			if (v & signbit) v |= ~(signbit - 1U);
			return static_cast<int32_t>(v);
		}
	}
	// 1-bit fields can be optimized.
	if (1U == f.len) {
		const std::size_t bytepos(f.pos >> 3U);
		if (bytepos >= report_size) return 0U;
		const uint8_t v(*reinterpret_cast<const uint8_t *>(input_buffer + bytepos));
		// The 1 bit is the sign bit, so sign extension gets us this.
		return (v >> (f.pos & 7U)) & 1U ? -1 : 0;
	}
	/// \bug FIXME: This doesn't handle unaligned >1-bit fields.
	std::clog << "DEBUG: Unaligned >1 bit signed field at " << f.pos << " length " << f.len << " in USB input report\n";
	return 0U;
}

inline
bool
USBHIDCommon::IsInRange(
	const InputVariable & f,
	int32_t v
) {
	return v <= f.max && v >= f.min;
}

inline
void
USBHIDCommon::SetUnsignedField(
	const ReportField & f,
	std::size_t report_size,
	uint32_t value
) {
	if (0U == f.len) return;
	// Byte-aligned fields can be optimized.
	if (0U == (f.pos & 7U)) {
		const std::size_t bytepos(f.pos >> 3U);
		if (bytepos >= report_size) return;
		if (f.len <= 8U) {
			uint8_t v(*reinterpret_cast<const uint8_t *>(output_buffer + bytepos));
			v &= 0xFF << f.len;
			v |= value & (0xFF >> (8U - f.len));
			*reinterpret_cast<uint8_t *>(output_buffer + bytepos) = v;
		}
		if (f.len <= 16U) {
			uint16_t v(le16toh(*reinterpret_cast<const uint16_t *>(output_buffer + bytepos)));
			v &= 0xFFFF << f.len;
			v |= value & (0xFFFF >> (16U - f.len));
			*reinterpret_cast<uint16_t *>(output_buffer + bytepos) = htole16(v);
		}
		if (f.len <= 32U) {
			uint32_t v(le32toh(*reinterpret_cast<const uint32_t *>(output_buffer + bytepos)));
			v &= 0xFFFFFFFF << f.len;
			v |= value & (0xFFFFFFFF >> (32U - f.len));
			*reinterpret_cast<uint32_t *>(output_buffer + bytepos) = htole32(v);
		}
	}
	// 1-bit fields can be optimized.
	if (1U == f.len) {
		const std::size_t bytepos(f.pos >> 3U);
		if (bytepos >= report_size) return;
		uint8_t & v(*reinterpret_cast<uint8_t *>(output_buffer + bytepos));
		const uint8_t mask(1U << (f.pos & 7U));
		if (value)
			v |= mask;
		else
			v &= ~mask;
	}
	/// \bug FIXME: This doesn't handle unaligned >1-bit fields.
}

inline
bool
USBHIDCommon::read_report_description (
	unsigned char * const b,
	const std::size_t maxlen,
	const std::size_t actlen
) {
	input_reports.clear();
	output_reports.clear();
	has_report_ids = false;

	enum { MAIN = 0, GLOBAL, LOCAL, LONG };
	enum /*local tags*/ { USAGE = 0, MIN, MAX };
	enum /*global tags*/ { PAGE = 0, REPORT_ID = 8, PUSH = 10, POP = 11 };
	enum /*main tags*/ { INPUT = 8, OUTPUT = 9, COLLECT = 10, END = 12};
	enum /*collection types*/ { APPLICATION = 1 };

	std::stack<ParserContext> context_stack;
	context_stack.push(ParserContext());
	unsigned collection_level(0U);
	bool enable_fields(false);

	for ( std::size_t i(0U); i < actlen; ) {
		if (i >= maxlen) break;
		unsigned size((b[i] & 3U) + ((b[i] & 3U) > 2U));
		unsigned type((b[i] >> 2) & 3U);
		unsigned tag((b[i] >> 4) & 15U);
		++i;
		uint32_t datum(0U);
		switch (size) {
			case 4:
				if (i + 4 > actlen) break;
				datum = b[i+0] | (uint32_t(b[i+1]) << 8) | (uint32_t(b[i+2]) << 16) | (uint32_t(b[i+3]) << 24);
				i += 4;
				break;
			case 2:
				if (i + 2 > actlen) break;
				datum = b[i+0] | (uint32_t(b[i+1]) << 8);
				i += 2;
				break;
			case 1:
				if (i + 1 > actlen) break;
				datum = b[i++];
				break;
			case 0:
				break;
		}
		ParserContext & context(context_stack.top());
		switch (type) {
			case LONG:
				size = datum & 0xFF;
				tag = (datum >> 8) & 0xFF;
				if (i + size > actlen) break;
				i += size;
				break;
			case LOCAL:
				context.locals[tag] = ParserContext::Item(datum, size);
				if (USAGE == tag) context.add_ident();
				if (MIN == tag) context.has_min = true;
				if (MAX == tag) context.has_max = true;
				break;
			case GLOBAL:
				if (PUSH == tag)
					context_stack.push(context);
				else
				if (POP == tag) {
					if (context_stack.size() > 1)
						context_stack.pop();
				} else
				if (PUSH > tag) {
					context.globals[tag] = ParserContext::Item(datum, size);
					if (REPORT_ID == tag) has_report_ids = true;
				}
				break;
			case MAIN:
				switch (tag) {
					case INPUT:
					{
						if (!enable_fields) break;
						InputReportDescription & r(input_reports[context.report_id().d]);
						if (datum & HIO_CONST) {
							r.bits += context.report_size().d * context.report_count().d;
						} else
						if (datum & HIO_VARIABLE) {
							for ( std::size_t count(0U); count < context.report_count().d; ++count ) {
								const UsageID ident(context.take_variable_ident());
								const bool relative(HIO_RELATIVE & datum);
								r.variables.push_back(InputVariable(ident, r.bits, context.report_size().d, relative, context.logical_minimum(), context.logical_maximum()));
								r.bits += context.report_size().d;
							}
						} else
						{
							for ( std::size_t count(0U); count < context.report_count().d; ++count ) {
								r.array_indices.push_back(InputIndex(r.bits, context.report_size().d, context.ident_minimum(), context.ident_maximum(), context.logical_minimum(), context.logical_maximum()));
								r.bits += context.report_size().d;
							}
						}
						r.bytes = (r.bits + 7U) >> 3U;
						break;
					}
					case OUTPUT:
					{
						if (!enable_fields) break;
						OutputReportDescription & r(output_reports[context.report_id().d]);
						if (datum & HIO_CONST) {
							r.bits += context.report_size().d * context.report_count().d;
						} else
						if (datum & HIO_VARIABLE) {
							for ( std::size_t count(0U); count < context.report_count().d; ++count ) {
								const UsageID ident(context.take_variable_ident());
								r.variables.push_back(OutputVariable(ident, r.bits, context.report_size().d));
								r.bits += context.report_size().d;
							}
						} else
						{
							for ( std::size_t count(0U); count < context.report_count().d; ++count ) {
								r.array_indices.push_back(OutputIndex(r.bits, context.report_size().d, context.ident_minimum(), context.ident_maximum()));
								r.bits += context.report_size().d;
							}
						}
						r.bytes = (r.bits + 7U) >> 3U;
						break;
					}
					case COLLECT:
						if (0 == collection_level && APPLICATION == datum) {
							enable_fields =
								HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_MOUSE) == context.ident() ||
								HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_KEYBOARD) == context.ident() ||
								HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_KEYPAD) == context.ident() ||
								HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_SYSTEM_CONTROL) == context.ident() ||
								HID_USAGE2(HUP_CONSUMER, HUC_CONSUMER_CONTROL) == context.ident()
								;
						}
						++collection_level;
						break;
					case END:
						if (collection_level > 0) {
							--collection_level;
							if (0 == collection_level)
								enable_fields = false;
						}
						break;
				}
				context.clear_locals();
				break;
		}
	}

	for (InputReports::iterator rp(input_reports.begin()); input_reports.end() != rp; ++rp) {
		InputReportDescription & report(rp->second);
		report.has_keyboard = report.has_mouse = report.has_numlock_key = false;
		for (InputReportDescription::Variables::const_iterator i(report.variables.begin()); i != report.variables.end(); ++i) {
			const InputVariable & f(*i);
			if (HID_USAGE2(HUP_BUTTON, 0) <= f.ident && HID_USAGE2(HUP_BUTTON, 0xFFFF) >= f.ident) {
				report.has_mouse = true;
			} else
			if ((HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_SYSTEM_CONTROL) <= f.ident && HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_LAST_SYSTEM_KEY) >= f.ident)
			||  (HID_USAGE2(HUP_KEYBOARD, 0) <= f.ident && HID_USAGE2(HUP_KEYBOARD, 0xFFFF) >= f.ident)
			||  (HID_USAGE2(HUP_CONSUMER, 0) <= f.ident && HID_USAGE2(HUP_CONSUMER, 0xFFFF) >= f.ident)
			) {
				report.has_keyboard = true;
				if (HID_USAGE2(HUP_KEYBOARD, HUK_NUMLOCK) == f.ident)
					report.has_numlock_key = true;
			} else
			switch (f.ident) {
				case HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_X):
				case HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_Y):
				case HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_WHEEL):
				case HID_USAGE2(HUP_CONSUMER, HUC_AC_ZOOM):
				case HID_USAGE2(HUP_CONSUMER, HUC_AC_SCROLL):
				case HID_USAGE2(HUP_CONSUMER, HUC_AC_PAN):
					report.has_mouse = true;
					break;
				default:
					break;
			}
		}
		for (InputReportDescription::Indices::const_iterator i(report.array_indices.begin()); i != report.array_indices.end(); ++i) {
			const InputIndex & f(*i);
			if (HID_USAGE2(HUP_BUTTON, 0) <= f.max_ident && HID_USAGE2(HUP_BUTTON, 0xFFFF) >= f.min_ident) {
				report.has_mouse = true;
			}
			if ((HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_SYSTEM_CONTROL) <= f.max_ident && HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_LAST_SYSTEM_KEY) >= f.min_ident)
			||  (HID_USAGE2(HUP_KEYBOARD, 0) <= f.max_ident && HID_USAGE2(HUP_KEYBOARD, 0xFFFF) >= f.min_ident)
			||  (HID_USAGE2(HUP_CONSUMER, 0) <= f.max_ident && HID_USAGE2(HUP_CONSUMER, 0xFFFF) >= f.min_ident)
			// Officially, this is UCS-2, not even UTF-16.
			// But because it has 24 bits, and because we don't officially have to cope with UTF-16, let us just make it UTF-32 by fiat.
			||  (HID_USAGE2(HUP_UNICODE, 0) <= f.max_ident && HID_USAGE2(HUP_UNICODE, 0xFFFFFF) >= f.min_ident)
			) {
				report.has_keyboard = true;
			}
			if (HID_USAGE2(HUP_KEYBOARD, HUK_NUMLOCK) <= f.max_ident && HID_USAGE2(HUP_KEYBOARD, HUK_NUMLOCK) >= f.min_ident) {
				report.has_numlock_key = true;
			}
		}
	}
	has_compose_LED = false;
	for (OutputReports::iterator rp(output_reports.begin()); output_reports.end() != rp; ++rp) {
		OutputReportDescription & report(rp->second);

		report.has_LEDs = false;
		report.has_compose_LED = false;
		for (OutputReportDescription::Variables::const_iterator i(report.variables.begin()); i != report.variables.end(); ++i) {
			const OutputVariable & f(*i);
			if (HID_USAGE2(HUP_LEDS, 0) <= f.ident && HID_USAGE2(HUP_LEDS, 0xFFFF) >= f.ident) {
				report.has_LEDs = true;
				if (HID_USAGE2(HUP_LEDS, HUL_COMPOSE) == f.ident) report.has_compose_LED = true;
				break;
			}
		}
		has_compose_LED |= report.has_compose_LED;
	}
	return true;
}

inline
bool
USBHIDCommon::has_mouse() const
{
	for (InputReports::const_iterator rp(input_reports.begin()); input_reports.end() != rp; ++rp) {
		const InputReportDescription & report(rp->second);
		if (report.has_mouse) return true;
	}
	return false;
}

inline
bool
USBHIDCommon::has_keyboard() const
{
	for (InputReports::const_iterator rp(input_reports.begin()); input_reports.end() != rp; ++rp) {
		const InputReportDescription & report(rp->second);
		if (report.has_keyboard) return true;
	}
	return false;
}

inline
bool
USBHIDCommon::has_LEDs() const
{
	for (OutputReports::const_iterator rp(output_reports.begin()); output_reports.end() != rp; ++rp) {
		const OutputReportDescription & report(rp->second);
		if (report.has_LEDs) return true;
	}
	return false;
}

inline
bool
USBHIDCommon::has_numlock_key() const
{
	for (InputReports::const_iterator rp(input_reports.begin()); input_reports.end() != rp; ++rp) {
		const InputReportDescription & report(rp->second);
		if (report.has_numlock_key) return true;
	}
	return false;
}

inline
void
USBHIDCommon::handle_mouse_movement(
	const InputReportDescription & report,
	const InputVariable & f,
	const MouseAxis axis
) {
	if (f.relative) {
		const int32_t off(GetSignedField(f, report.bytes));
		if (IsInRange(f, off))
			handle_mouse_relpos(axis, off);
	} else {
		const uint32_t pos(GetUnsignedField(f, report.bytes));
		if (IsInRange(f, pos))
			handle_mouse_abspos(axis, pos, f.max - f.min + 1U);
	}
}

inline
void
USBHIDCommon::handle_mouse_button(
	UsageID ident,
	bool down
) {
	if (HID_USAGE2(HUP_BUTTON, 0U) < ident && HID_USAGE2(HUP_BUTTON, 0xFFFF) >= ident) {
		const uint16_t button(ident - HID_USAGE2(HUP_BUTTON, 1U));
		handle_mouse_button(TranslateToStdButton(button), down);
	} else
	{
		/* Ignore out of range button. */
	}
}

inline
void
USBHIDCommon::handle_key(
	bool & seen_keyboard,
	KeysPressed & newkeys,
	UsageID ident,
	bool down
) {
	if ((HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_SYSTEM_CONTROL) <= ident && HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_LAST_SYSTEM_KEY) >= ident)
	||  (HID_USAGE2(HUP_KEYBOARD, 0) <= ident && HID_USAGE2(HUP_KEYBOARD, 0xFFFF) >= ident)
	||  (HID_USAGE2(HUP_CONSUMER, 0) <= ident && HID_USAGE2(HUP_CONSUMER, 0xFFFF) >= ident)
	) {
		seen_keyboard = true;
		if (down)
			newkeys.insert(ident);
		else
			newkeys.erase(ident);
	} else
	{
		/* Ignore out of range key. */
	}
}

inline
void
USBHIDCommon::handle_input_events(
) {
	const int n(read(device.get(), reinterpret_cast<char *>(input_buffer) + input_offset, sizeof input_buffer - input_offset));
	if (0 > n) return;

	KeysPressed newkeys;
	bool seen_keyboard(false);

	for (
		input_offset += n;
		input_offset > 0;
	    ) {
		uint8_t report_id(0);
		if (has_report_ids) {
			report_id = input_buffer[0];
			std::memmove(input_buffer, input_buffer + 1, sizeof input_buffer - 1);
			--input_offset;
		}
		InputReports::const_iterator rp(input_reports.find(report_id));
		if (input_reports.end() == rp) break; // Bad report number; assume desynchronized from report ID and swallow one character.
		const InputReportDescription & report(rp->second);
		if (input_offset < report.bytes) break;

		for (InputReportDescription::Variables::const_iterator i(report.variables.begin()); i != report.variables.end(); ++i) {
			const InputVariable & f(*i);
			if (HID_USAGE2(HUP_BUTTON, 0) <= f.ident && HID_USAGE2(HUP_BUTTON, 0xFFFF) >= f.ident) {
				const uint32_t down(GetUnsignedField(f, report.bytes));
				if (IsInRange(f, down))
					handle_mouse_button(f.ident, down);
			} else
			if ((HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_SYSTEM_CONTROL) <= f.ident && HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_LAST_SYSTEM_KEY) >= f.ident)
			||  (HID_USAGE2(HUP_KEYBOARD, 0) <= f.ident && HID_USAGE2(HUP_KEYBOARD, 0xFFFF) >= f.ident)
			||  (HID_USAGE2(HUP_CONSUMER, 0) <= f.ident && HID_USAGE2(HUP_CONSUMER, 0xFFFF) >= f.ident)
			) {
				const uint32_t down(GetUnsignedField(f, report.bytes));
				if (IsInRange(f, down))
					handle_key(seen_keyboard, newkeys, f.ident, down);
			} else
			switch (f.ident) {
				case HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_X):
					handle_mouse_movement(report, f, AXIS_X);
					break;
				case HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_Y):
					handle_mouse_movement(report, f, AXIS_Y);
					break;
				case HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_WHEEL):
					handle_mouse_movement(report, f, V_SCROLL);
					break;
				case HID_USAGE2(HUP_CONSUMER, HUC_AC_ZOOM):
					handle_mouse_movement(report, f, AXIS_Z);
					break;
				case HID_USAGE2(HUP_CONSUMER, HUC_AC_SCROLL):
					handle_mouse_movement(report, f, AXIS_W);
					break;
				case HID_USAGE2(HUP_CONSUMER, HUC_AC_PAN):
					handle_mouse_movement(report, f, H_SCROLL);
					break;
			}
		}
		for (InputReportDescription::Indices::const_iterator i(report.array_indices.begin()); i != report.array_indices.end(); ++i) {
			const InputIndex & f(*i);
			const uint32_t v(GetUnsignedField(f, report.bytes));
			if (v < f.min_index || v > f.max_index) continue;
			uint32_t ident(v + f.min_ident - f.min_index);
			if (ident > f.max_ident) continue;

			const bool down(true);	// Implied by the existence of the index value.
			if (HID_USAGE2(HUP_BUTTON, 0) < ident && HID_USAGE2(HUP_BUTTON, 0xFFFF) >= ident) {
				handle_mouse_button(ident, down);
			} else
			if ((HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_SYSTEM_CONTROL) <= ident && HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_LAST_SYSTEM_KEY) >= ident)
			||  (HID_USAGE2(HUP_KEYBOARD, 0) <= ident && HID_USAGE2(HUP_KEYBOARD, 0xFFFF) >= ident)
			||  (HID_USAGE2(HUP_CONSUMER, 0) <= ident && HID_USAGE2(HUP_CONSUMER, 0xFFFF) >= ident)
			) {
				handle_key(seen_keyboard, newkeys, ident, down);
			} else
			{
				/* Ignore out of range button/key. */
			}
		}

		std::memmove(input_buffer, input_buffer + report.bytes, sizeof input_buffer - report.bytes);
		input_offset -= report.bytes;
	}

	if (seen_keyboard) {
		for (KeysPressed::iterator was(keys.begin()); was != keys.end(); ) {
			const uint32_t ident(*was);
			KeysPressed::iterator now(newkeys.find(ident));
			const uint16_t index(usb_ident_to_keymap_index(ident));
			if (0xFFFF == index) continue;
			if (newkeys.end() == now) {
				shared.handle_keyboard(index, 0 /*release*/);
				was = keys.erase(was);
			} else {
				shared.handle_keyboard(index, 2 /*auto*/);
				newkeys.erase(now);
				++was;
			}
		}
		for (KeysPressed::iterator now(newkeys.begin()); now != newkeys.end(); now = newkeys.erase(now)) {
			const uint32_t ident(*now);
			// Officially, this is UCS-2, not even UTF-16.
			// But because it has 24 bits, and because we don't officially have to cope with UTF-16, let us just make it UTF-32 by fiat.
			if ((HID_USAGE2(HUP_UNICODE, 0) <= ident && HID_USAGE2(HUP_UNICODE, 0xFFFFFF) >= ident)) {
				const uint_fast32_t c(ident - HID_USAGE2(HUP_UNICODE, 0));
				shared.handle_unicode(c);
			} else {
				const uint16_t index(usb_ident_to_keymap_index(ident));
				if (0xFFFF == index) continue;
				shared.handle_keyboard(index, 1 /*press*/);
				keys.insert(ident);
			}
		}
	}
}

inline
void
USBHIDCommon::set_LEDs(
	const KeyboardLEDs & leds
) {
	for (OutputReports::const_iterator rp(output_reports.begin()); output_reports.end() != rp; ++rp) {
		const uint8_t report_id(rp->first);
		const OutputReportDescription & report(rp->second);
		if (!report.has_LEDs) continue;
		if (sizeof output_buffer < report.bytes) continue;

		std::fill(output_buffer, output_buffer + report.bytes, '\0');

		bool seen_LED(false);
		for (OutputReportDescription::Variables::const_iterator i(report.variables.begin()); i != report.variables.end(); ++i) {
			const OutputVariable & f(*i);
			if (HID_USAGE2(HUP_LEDS, 0) <= f.ident && HID_USAGE2(HUP_LEDS, 0xFFFF) >= f.ident) {
				switch (HID_USAGE(f.ident)) {
					case HUL_NUM_LOCK:
						SetUnsignedField(f, report.bytes, leds.num_lock());
						break;
					case HUL_CAPS_LOCK:
						SetUnsignedField(f, report.bytes, leds.caps_lock()||leds.shift2_lock());
						break;
					case HUL_SCROLL_LOCK:
						SetUnsignedField(f, report.bytes, has_compose_LED ? false : leds.group2());
						break;
					case HUL_COMPOSE:
						SetUnsignedField(f, report.bytes, has_compose_LED ? leds.group2() : false);
						break;
					case HUL_KANA:
						SetUnsignedField(f, report.bytes, leds.alt());
						break;
					case HUL_SHIFT:
						SetUnsignedField(f, report.bytes, leds.shift2()||leds.shift3());
						break;
					default:
						break;
				}
				seen_LED = true;
			}
		}

		if (seen_LED)
			write_output_report(report_id, report.bytes);
	}
}

/* BSD uhid HIDs ************************************************************
// **************************************************************************
*/

namespace {

class USBHID :
	public USBHIDCommon
{
public:
	USBHID(SharedHIDResources &, FileDescriptorOwner & fd);
	~USBHID() {}

	bool read_report_description();
protected:
	void write_output_report(const uint8_t, std::size_t);
	using USBHIDCommon::read_report_description;
};

inline const char * ToYesNo(bool v) { return v? "yes" : "no"; }

}

inline
USBHID::USBHID(
	SharedHIDResources & r,
	FileDescriptorOwner & fd
) :
	USBHIDCommon(r, fd)
{
}

#if defined(__FreeBSD__) || defined (__DragonFly__)
bool
USBHID::read_report_description()
{
	usb_gen_descriptor d = {};

	d.ugd_maxlen = 65535U;
	if (0 > ioctl(device.get(), USB_GET_REPORT_DESC, &d)) return false;
	std::vector<unsigned char> b(d.ugd_actlen);
	const std::size_t maxlen(d.ugd_actlen);
	d.ugd_data = b.data();
	d.ugd_maxlen = maxlen;
	if (0 > ioctl(device.get(), USB_GET_REPORT_DESC, &d)) return false;

	return read_report_description(b.data(), maxlen, d.ugd_actlen);
}
#endif

#if defined(__OpenBSD__) || defined(__NetBSD__)
bool
USBHID::read_report_description()
{
	usb_ctl_report_desc d = {};
	const std::size_t maxlen(sizeof d.ucrd_data);

	d.ucrd_size = maxlen;
	if (0 > ioctl(device.get(), USB_GET_REPORT_DESC, &d)) return false;

	return read_report_description(d.ucrd_data, maxlen, d.ucrd_size);
}
#endif

void
USBHID::write_output_report(
	const uint8_t report_id,
	std::size_t report_size
) {
	struct iovec v[] = {
		{ const_cast<uint8_t *>(&report_id), has_report_ids ? 1U : 0U },
		{ output_buffer, report_size },
	};
	writev(device.get(), v, sizeof v/sizeof *v);
}

void
console_uhid_realizer [[gnu::noreturn]] (
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
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{uhiddevice}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}

	if (args.empty()) {
		die_missing_argument(prog, envs, "uhid device name");
	}
	const char * input_filename(args.front());
	args.erase(args.begin());
	if (!args.empty()) die_unexpected_argument(prog, args, envs);

	Main main(prog, envs, mouse_primary);

	// Now open devices.

	{
		FileDescriptorOwner fd1(open_readwriteexisting_at(AT_FDCWD, input_filename));
		if (0 > fd1.get()) {
		hiderror:
			die_errno(prog, envs, input_filename);
		}
		USBHID * input = new USBHID(main, fd1);
		if (!input) goto hiderror;
		main.add_device(input);
		input->save();
		input->set_mode();
		if (!input->read_report_description()) {
			die_errno(prog, envs, input_filename);
		}
		std::fprintf(stderr, "%s: INFO: %s: mouse %s, keyboard %s, NumLock %s, LEDs %s.\n", prog, input_filename, ToYesNo(input->has_mouse()), ToYesNo(input->has_keyboard()), ToYesNo(input->has_numlock_key()), ToYesNo(input->has_LEDs()));
		if (!input->has_mouse() && !input->has_keyboard() && !input->has_LEDs())
			die_invalid(prog, envs, input_filename, "Not a keyboard/mouse human input device.");
		std::list<std::string> device_paths;
		device_paths.push_back(device_path_uhid_prefix + basename_of(input_filename));
		device_paths.push_back(device_path_prefix + "uhid");
		main.autoconfigure(device_paths, false /* no display */, input->has_mouse(), input->has_keyboard(), input->has_numlock_key(), input->has_LEDs());
	}

	main.raise_acquire_signal();

	main.loop();

	throw EXIT_SUCCESS;
}

/* BSD ugen HIDs ************************************************************
// **************************************************************************
*/

namespace {

class GenericUSB :
	public USBHIDCommon
{
public:
	class ControlDevice
	{
	public:
		ControlDevice(FileDescriptorOwner & fd);
		~ControlDevice() {}

		int do_request(const usb_ctl_request & d) const;
#if defined(__FreeBSD__) || defined(__DragonFly__)
		bool is_kernel_driver_attached(int index) const;
		bool detach_kernel_driver(int index) const;
#endif
		bool query_device_interfaces(unsigned & interfaces) const;
		bool query_device_IDs(uint16_t & deviceclass, uint16_t & subclass, uint16_t & vendor, uint16_t & product) const;
	protected:
		FileDescriptorOwner device;
	};

	GenericUSB(SharedHIDResources &, const ControlDevice & c, FileDescriptorOwner & fd, int);
	~GenericUSB() {}

	bool set_short_xfer(bool);
	bool set_timeout(int);
	bool read_report_description();
	bool query_interface_IDs(uint16_t & interfaceclass, uint16_t & subclass, uint16_t & protocol) const;
#if defined(__FreeBSD__) || defined(__DragonFly__)
	bool is_kernel_driver_attached() const { return control.is_kernel_driver_attached(index); }
	bool detach_kernel_driver() const { return control.detach_kernel_driver(index); }
#endif
protected:
	using USBHIDCommon::read_report_description;
	const ControlDevice & control;
	int index;

#if defined(__OpenBSD__)
	enum {
		USB_SET_RX_SHORT_XFER = USB_SET_SHORT_XFER
		USB_SET_RX_TIMEOUT = USB_SET_TIMEOUT
	};
#endif

	void write_output_report(const uint8_t, std::size_t);
};

}

inline
GenericUSB::ControlDevice::ControlDevice(
	FileDescriptorOwner & fd
) :
	device(fd.release())
{
}

inline
int
GenericUSB::ControlDevice::do_request(
	const usb_ctl_request & d
) const {
	return ioctl(device.get(), USB_DO_REQUEST, &d);
}

#if defined(__FreeBSD__) || defined(__DragonFly__)
inline
bool
GenericUSB::ControlDevice::is_kernel_driver_attached(
	int i
) const {
	return 0 <= ioctl(device.get(), USB_IFACE_DRIVER_ACTIVE, &i);
}

inline
bool
GenericUSB::ControlDevice::detach_kernel_driver(
	int i
) const {
	return 0 <= ioctl(device.get(), USB_IFACE_DRIVER_DETACH, &i);
}
#endif

inline
bool
GenericUSB::ControlDevice::query_device_interfaces(
	unsigned & interfaces
) const {
	int current_config = 0;
	if (0 > ioctl(device.get(), USB_GET_CONFIG, &current_config)) return false;
#if defined(__OpenBSD__) || defined(__NetBSD__)
	usb_config_desc d;
	d.ucd_config_index = current_config;
#else
	usb_config_descriptor d;
#endif
	if (0 > ioctl(device.get(), USB_GET_CONFIG_DESC, &d)) return false;
#if defined(__OpenBSD__) || defined(__NetBSD__)
	interfaces = d.ucd_desc.bNumInterface;
#else
	interfaces = d.bNumInterface;
#endif
	return true;
}

inline
bool
GenericUSB::ControlDevice::query_device_IDs(
	uint16_t & deviceclass,
	uint16_t & subclass,
	uint16_t & vendor,
	uint16_t & product
) const {
	usb_device_descriptor_t d;
	if (0 > ioctl(device.get(), USB_GET_DEVICE_DESC, &d)) return false;
	deviceclass = d.bDeviceClass;
	subclass = d.bDeviceSubClass;
	vendor = UGETW(d.idVendor);
	product = UGETW(d.idProduct);
	return true;
}

inline
GenericUSB::GenericUSB(
	SharedHIDResources & r,
	const ControlDevice & c,
	FileDescriptorOwner & fd,
	int i
) :
	USBHIDCommon(r, fd),
	control(c),
	index(i)
{
}

bool
GenericUSB::read_report_description(
) {
	std::vector<unsigned char> b(0x8000);
	usb_ctl_request d;
	d.ucr_addr = 0;
	d.ucr_actlen = 0;
	d.ucr_data = b.data();
#if defined(USBD_SHORT_XFER_OK)
	d.ucr_flags = USBD_SHORT_XFER_OK;
#else
	d.ucr_flags = USB_SHORT_XFER_OK;
#endif
	d.ucr_request.bmRequestType = 0x80 | 0x01;			// device to host, interface
	d.ucr_request.bRequest = 0x06;					// GET_DESCRIPTOR
	pack_littleendian(d.ucr_request.wValue, 0x2200, 2U);		// UHID report descriptor
	pack_littleendian(d.ucr_request.wIndex, index, 2U);		// interface index
	pack_littleendian(d.ucr_request.wLength, b.size(), 2U);

	if (0 > control.do_request(d)) return false;

	return read_report_description(b.data(), b.size(), d.ucr_actlen);
}

bool
GenericUSB::set_short_xfer(
	bool on
) {
#if defined(USB_SET_RX_SHORT_XFER)
	int b = on ? 1 : 0;
	return 0 <= ioctl(device.get(), USB_SET_RX_SHORT_XFER, &b);
#elif defined(USB_SET_SHORT_XFER)
	int b = on ? 1 : 0;
	return 0 <= ioctl(device.get(), USB_SET_SHORT_XFER, &b);
#else
	static_cast<void>(on);	// Silences a compiler warning.
	return true;
#endif
}

bool
GenericUSB::set_timeout(
	int t
) {
#if defined(USB_SET_RX_TIMEOUT)
	return 0 <= ioctl(device.get(), USB_SET_RX_TIMEOUT, &t);
#elif defined(USB_SET_TIMEOUT)
	return 0 <= ioctl(device.get(), USB_SET_TIMEOUT, &t);
#else
	static_cast<void>(t);	// Silences a compiler warning.
	return true;
#endif
}

void
GenericUSB::write_output_report(
	const uint8_t report_id,
	std::size_t report_size
) {
	usb_ctl_request d;
	d.ucr_addr = 0;
	d.ucr_actlen = 0;
	d.ucr_data = output_buffer;
#if defined(USBD_SHORT_XFER_OK)
	d.ucr_flags = USBD_SHORT_XFER_OK;
#else
	d.ucr_flags = USB_SHORT_XFER_OK;
#endif
	d.ucr_request.bmRequestType = 0x00 | 0x20 | 0x01;			// host to device, class, interface
	d.ucr_request.bRequest = 0x09;						// SET_REPORT
	pack_littleendian(d.ucr_request.wValue, 0x0200 | report_id, 2U);	// output report
	pack_littleendian(d.ucr_request.wIndex, index, 2U);			// interface zero
	pack_littleendian(d.ucr_request.wLength, report_size, 2U);

	control.do_request(d);
}

inline
bool
GenericUSB::query_interface_IDs(
	uint16_t & interfaceclass,
	uint16_t & subclass,
	uint16_t & protocol
) const {
#if defined(__OpenBSD__) || defined(__NetBSD__)
	usb_interface_desc d;
	d.uid_config_index = USB_CURRENT_CONFIG_INDEX;
	d.uid_interface_index = USB_CURRENT_ALT_INDEX;
	d.uid_alt_index = USB_CURRENT_ALT_INDEX;
	if (0 > ioctl(device.get(), USB_GET_INTERFACE_DESC, &d)) return false;
	interfaceclass = d.uid_desc.bInterfaceClass;
	subclass = d.uid_desc.bInterfaceSubClass;
	protocol = d.uid_desc.bInterfaceProtocol;
#else
	usb_interface_descriptor d;
	if (0 > ioctl(device.get(), USB_GET_RX_INTERFACE_DESC, &d)) return false;
	interfaceclass = d.bInterfaceClass;
	subclass = d.bInterfaceSubClass;
	protocol = d.bInterfaceProtocol;
#endif
	return true;
}

void
console_ugen_hid_realizer [[gnu::noreturn]] (
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
		popt::top_table_definition main_option(sizeof top_table/sizeof *top_table, top_table, "Main options", "{ugendevice}");

		std::vector<const char *> new_args;
		popt::arg_processor<const char **> p(args.data() + 1, args.data() + args.size(), prog, envs, main_option, new_args);
		p.process(true /* strictly options before arguments */);
		args = new_args;
		if (p.stopped()) throw EXIT_SUCCESS;
	} catch (const popt::error & e) {
		die(prog, envs, e);
	}

	if (args.empty()) {
		die_missing_argument(prog, envs, "ugen device name");
	}
	const char * control_filename(args.front());
	args.erase(args.begin());
	if (!args.empty()) die_unexpected_argument(prog, args, envs);

	Main main(prog, envs, mouse_primary);

	// Now open devices.
	const char * ugen(basename_of(control_filename));
	unsigned bus, address;
	if (2 > std::sscanf(ugen, USB_GENERIC_NAME "%u.%u", &bus, &address))
		die_invalid(prog, envs, control_filename, "Cannot parse bus and address from device name.");

	bool has_mouse(false), has_keyboard(false), has_numlock_key(false), has_LEDs(false);
	std::list<std::string> device_paths;

	// The control device is for control requests.
	FileDescriptorOwner fdc(open_readwriteexisting_at(AT_FDCWD, control_filename));
	if (0 > fdc.get()) {
	hiderror:
		die_errno(prog, envs, control_filename);
	}
	GenericUSB::ControlDevice control(fdc);

	bool found(false);
	{
		uint16_t deviceclass, subclass, vendor, product;
		if (!control.query_device_IDs(deviceclass, subclass, vendor, product)) goto hiderror;
		std::fprintf(stderr, "%s: INFO: %s: class %04" PRIx16 " subclass %04" PRIx16 " product ID %04" PRIx16 " vendor ID %04" PRIx16 ".\n", prog, control_filename, deviceclass, subclass, product, vendor);
		unsigned interfaces;
		if (!control.query_device_interfaces(interfaces)) goto hiderror;
		std::fprintf(stderr, "%s: INFO: %s: %u interface(s).\n", prog, control_filename, interfaces);
		const std::string prefix(control_filename, ugen);
		for (unsigned index(0U); index < interfaces; ++index) {
			char buf[64];
			std::snprintf(buf, sizeof buf, USB_DEVICE_DIR "/%u.%u.%u", bus, address, index + 1);
			const std::string input_filename(prefix + buf);
			// The other devices are for input or output/feature reports.
			FileDescriptorOwner fdi(open_read_at(AT_FDCWD, input_filename.c_str()));
			if (0 > fdi.get()) {
			interface_error:
				die_errno(prog, envs, input_filename.c_str());
			}
			GenericUSB * input = new GenericUSB(main, control, fdi, index);
			if (!input) goto interface_error;
			input->set_short_xfer(true);
			input->set_timeout(0);
			uint16_t interfaceclass, interfacesubclass, interfaceprotocol;
			if (input->query_interface_IDs(interfaceclass, interfacesubclass, interfaceprotocol)) {
				std::fprintf(stderr, "%s: INFO: %s: class %02x, subclass %02x, protocol %02x.\n", prog, input_filename.c_str(), interfaceclass, interfacesubclass, interfaceprotocol);
				if (UICLASS_HID != interfaceclass) {
					std::fprintf(stderr, "%s: WARNING: %s: Interface is not a Human Input Device.\n", prog, input_filename.c_str());
					delete input; input = nullptr;
					continue;
				}
				if (UISUBCLASS_BOOT == interfacesubclass && UIPROTO_BOOT_KEYBOARD == interfaceprotocol) {
					// \todo FIXME: What to do with this knowledge?
				}
			}
			if (!input->read_report_description()) {
				message_warning(prog, envs, input_filename.c_str(), "Interface has no report descriptor.");
				delete input; input = nullptr;
				continue;
			}
			std::fprintf(stderr, "%s: INFO: %s: Has mouse %s, keyboard %s, NumLock %s, LEDs %s.\n", prog, input_filename.c_str(), ToYesNo(input->has_mouse()), ToYesNo(input->has_keyboard()), ToYesNo(input->has_numlock_key()), ToYesNo(input->has_LEDs()));
			if (!input->has_mouse() && !input->has_keyboard() && !input->has_LEDs()) {
				message_error(prog, envs, input_filename.c_str(), "Not a keyboard/mouse human input device.");
				delete input; input = nullptr;
				continue;
			}
			has_mouse |= input->has_mouse();
			has_keyboard |= input->has_keyboard();
			has_numlock_key |= input->has_numlock_key();
			has_LEDs |= input->has_LEDs();
			main.add_device(input);
#if defined(__FreeBSD__) || defined(__DragonFly__)
			if (input->is_kernel_driver_attached()) {
				std::fprintf(stderr, "%s: INFO: %s: %s\n", prog, input_filename.c_str(), "Detaching kernel device driver from ugen device.");
				if (!input->detach_kernel_driver()) goto interface_error;
			}
#endif
			input->save();
			input->set_mode();
			found = true;
		}
		{
			char buf[64];
			snprintf(buf, sizeof buf, "B%04x.A%04x.V%04" PRIx16 ".P%04" PRIx16, bus, address, vendor, product);
			device_paths.push_back(device_path_ugen_prefix + buf);
			snprintf(buf, sizeof buf, "B%04x.A%04x.V%04" PRIx16, bus, address, vendor);
			device_paths.push_back(device_path_ugen_prefix + buf);
			snprintf(buf, sizeof buf, "V%04" PRIx16 ".P%04" PRIx16, vendor, product);
			device_paths.push_back(device_path_ugen_prefix + buf);
			snprintf(buf, sizeof buf, "V%04" PRIx16, vendor);
			device_paths.push_back(device_path_ugen_prefix + buf);
			snprintf(buf, sizeof buf, "C%04" PRIx16 ".S%04" PRIx16 ".V%04" PRIx16, deviceclass, subclass, vendor);
			device_paths.push_back(device_path_ugen_prefix + buf);
			snprintf(buf, sizeof buf, "C%04" PRIx16 ".S%04" PRIx16, deviceclass, subclass);
			device_paths.push_back(device_path_ugen_prefix + buf);
			snprintf(buf, sizeof buf, "C%04" PRIx16, deviceclass);
			device_paths.push_back(device_path_ugen_prefix + buf);
			snprintf(buf, sizeof buf, "B%04x.A%04x", bus, address);
			device_paths.push_back(device_path_ugen_prefix + buf);
		}
	}

	if (!found )
		die_invalid(prog, envs, control_filename, "No input devices found.");

	drop_privileges(prog, envs);

	{
		device_paths.push_back(device_path_prefix + USB_GENERIC_NAME);
		main.autoconfigure(device_paths, false /* no display */, has_mouse, has_keyboard, has_numlock_key, has_LEDs);
	}

	main.raise_acquire_signal();

	main.loop();

	throw EXIT_SUCCESS;
}

#endif
