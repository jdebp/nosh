/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#include <ctime>
#include <stdint.h>
#include <cstring>
#include "utils.h"
#if defined(__LINUX__) || defined(__linux__)
#include "ProcessEnvironment.h"
#endif

static const
struct leapsec {
	uint64_t start;
	unsigned offset;
	bool leap;
} leap_seconds_table[] = {
	{ 0x3fffffffe96da18aULL,  0,  true },	// 1958-12-31 00:00:00 -- start of International Atomic Time
	{ 0x3ffffffffffffff6ULL,  1,  true },	// 1969-12-31 23:59:50 -- fake leap second
	{ 0x3ffffffffffffff7ULL,  2,  true },	// 1969-12-31 23:59:50 -- fake leap second
	{ 0x3ffffffffffffff8ULL,  3,  true },	// 1969-12-31 23:59:50 -- fake leap second
	{ 0x3ffffffffffffff9ULL,  4,  true },	// 1969-12-31 23:59:50 -- fake leap second
	{ 0x3ffffffffffffffaULL,  5,  true },	// 1969-12-31 23:59:50 -- fake leap second
	{ 0x3ffffffffffffffbULL,  6,  true },	// 1969-12-31 23:59:50 -- fake leap second
	{ 0x3ffffffffffffffcULL,  7,  true },	// 1969-12-31 23:59:50 -- fake leap second
	{ 0x3ffffffffffffffdULL,  8,  true },	// 1969-12-31 23:59:50 -- fake leap second
	{ 0x3ffffffffffffffeULL,  9,  true },	// 1969-12-31 23:59:50 -- fake leap second
	{ 0x3fffffffffffffffULL, 10,  true },	// 1969-12-31 23:59:50 -- fake leap second
	{ 0x4000000000000000ULL, 10, false },	// 1969-12-31 23:59:50 -- TAI64N defined epoch
	{ 0x4000000003c2670aULL, 10, false },	// 1971-12-31 00:00:00 -- UTC becomes SI
	{ 0x4000000004b2580aULL, 11,  true },	// 1972-06-30 23:59:60 -- 1st leap second
	{ 0x4000000005a4ec0bULL, 12,  true },	// 1972-12-31 23:59:60
	{ 0x4000000007861f8cULL, 13,  true },	// 1973-12-31 23:59:60
	{ 0x400000000967530dULL, 14,  true },	// 1974-12-31 23:59:60
	{ 0x400000000b48868eULL, 15,  true },	// 1975-12-31 23:59:60
	{ 0x400000000d2b0b8fULL, 16,  true },	// 1976-12-31 23:59:60
	{ 0x400000000f0c3f10ULL, 17,  true },	// 1977-12-31 23:59:60
	{ 0x4000000010ed7291ULL, 18,  true },	// 1978-12-31 23:59:60
	{ 0x4000000012cea612ULL, 19,  true },	// 1979-12-31 23:59:60
	{ 0x40000000159fca93ULL, 20,  true },	// 1981-06-30 23:59:60
	{ 0x400000001780fe14ULL, 21,  true },	// 1982-06-30 23:59:60
	{ 0x4000000019623195ULL, 22,  true },	// 1983-06-30 23:59:60
	{ 0x400000001d25ea16ULL, 23,  true },	// 1985-06-30 23:59:60
	{ 0x4000000021dae517ULL, 24,  true },	// 1987-12-31 23:59:60
	{ 0x40000000259e9d98ULL, 25,  true },	// 1989-12-31 23:59:60
	{ 0x40000000277fd119ULL, 26,  true },	// 1990-12-31 23:59:60
	{ 0x400000002a50f59aULL, 27,  true },	// 1992-06-30 23:59:60
	{ 0x400000002c32291bULL, 28,  true },	// 1993-06-30 23:59:60
	{ 0x400000002e135c9cULL, 29,  true },	// 1994-06-30 23:59:60
	{ 0x4000000030e7241dULL, 30,  true },	// 1995-12-31 23:59:60
	{ 0x4000000033b8489eULL, 31,  true },	// 1997-06-30 23:59:60
	{ 0x40000000368c101fULL, 32,  true },	// 1998-12-31 23:59:60
	{ 0x4000000043b71ba0ULL, 33,  true },	// 2005-12-31 23:59:60
	{ 0x40000000495c07a1ULL, 34,  true },	// 2008-12-31 23:59:60
	{ 0x400000004fef9322ULL, 35,  true },	// 2012-06-30 23:59:60
	{ 0x4000000055932da3ULL, 36,  true },	// 2015-06-30 23:59:60
	{ 0x40000000586846a4ULL, 37,  true },	// 2016-12-31 23:59:60
};

#if defined(__LINUX__) || defined(__linux__)

static int checked(-1);

static inline
bool
begins_with (
	const char * s,
	const char * p
) {
	const std::size_t sl(std::strlen(s));
	const std::size_t pl(std::strlen(p));
	return pl <= sl && 0 == memcmp(s, p, pl);
}

static inline
bool
ends_with (
	const char * s,
	const char * p
) {
	const std::size_t sl(std::strlen(s));
	const std::size_t pl(std::strlen(p));
	return pl <= sl && 0 == memcmp(s + sl - pl, p, pl);
}

static inline
bool
time_t_is_tai(const ProcessEnvironment & envs)
{
	if (0 > checked) {
		if (const char * tz = envs.query("TZ")) {
			if (begins_with(tz, "right/")) checked = 1;
			if (begins_with(tz, "posix/")) checked = 0;
		}
	}
	if (0 > checked) {
		if (const char * tzdir = envs.query("TZDIR")) {
			if (ends_with(tzdir, "/right")) checked = 1;
			if (ends_with(tzdir, "/posix")) checked = 0;
		}
	}
	return checked > 0;
}

#else

static inline
bool
time_t_is_tai(const ProcessEnvironment &)
{
	return false;
}

#endif

time_t
tai64_to_time (
	const ProcessEnvironment & envs,
	const uint64_t s,
	bool & leap
) {
	leap = false;
	if (time_t_is_tai(envs))
		return static_cast<time_t>(s - 0x4000000000000000ULL - 10UL);
	for (std::size_t i(sizeof leap_seconds_table/sizeof *leap_seconds_table); i > 0U; ) {
		const struct leapsec & l(leap_seconds_table[--i]);
		if (l.start == s) {
			leap = l.leap;
			return static_cast<time_t>(s - 0x4000000000000000ULL - l.offset);
		}
		if (l.start < s) return static_cast<time_t>(s - 0x4000000000000000ULL - l.offset);
	}
	return static_cast<time_t>(s - 0x4000000000000000ULL);
}

uint64_t
time_to_tai64 (
	const ProcessEnvironment & envs,
	const std::time_t s,
	bool leap
) {
	const uint64_t z(0x4000000000000000ULL + s);
	if (time_t_is_tai(envs))
		return z + 10UL;
	for (std::size_t i(sizeof leap_seconds_table/sizeof *leap_seconds_table); i-- > 0U; ) {
		const struct leapsec & l(leap_seconds_table[i]);
		const uint64_t tai64(z + l.offset + (leap ? 1U : 0U));
		if (l.start < tai64) return tai64;
	}
	return z;
}
