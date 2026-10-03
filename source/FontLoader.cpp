/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#define _BSD_SOURCE 1
#include <cerrno>
#include <cstdio>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <stdint.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/mman.h>
#if defined(__LINUX__) || defined(__linux__)
#include <endian.h>
#else
#include <sys/endian.h>
#endif
#include "utils.h"
#include "fdutils.h"
#include "FileDescriptorOwner.h"
#include "vtfont.h"
#include "CompositeFont.h"

/* Loadable fonts ***********************************************************
// **************************************************************************
*/

struct ProcessEnvironment;

namespace FontLoader {

inline
ssize_t
pread(int fd, bsd_vtfont_header & header, off_t o)
{
	const ssize_t rc(pread(fd, &header, sizeof header, o));
	if (0 <= rc) {
		header.glyphs = be32toh(header.glyphs);
		for (unsigned i(0U); i < sizeof header.map_lengths/sizeof *header.map_lengths; ++i) header.map_lengths[i] = be32toh(header.map_lengths[i]);
	}
	return rc;
}

inline
ssize_t
pread(int fd, bsd_vtfont_map_entry & me, off_t o)
{
	const ssize_t rc(pread(fd, &me, sizeof me, o));
	if (0 <= rc) {
		me.character = be32toh(me.character);
		me.glyph = be16toh(me.glyph);
		me.count = be16toh(me.count);
	}
	return rc;
}

inline
bool
good ( const bsd_vtfont_header & header )
{
	return 0 == std::memcmp(header.magic, "VFNT0002", sizeof header.magic);
}

inline
std::size_t
glyphs_size ( const bsd_vtfont_header & header )
{
	return header.glyphs * header.height * ((header.width + 7U) / 8U);
}

inline
off_t
map_start ( const bsd_vtfont_header & header )
{
	return sizeof header + glyphs_size(header);
}

void
LoadRawFont (
	const char * prog,
	const ProcessEnvironment & envs,
	CombinedFont & font,
	CombinedFont::Font::Weight weight,
	CombinedFont::Font::Slant slant,
	const char * name
) {
	FileDescriptorOwner font_fd(open_read_at(AT_FDCWD, name));
	if (0 > font_fd.get()) {
bad_file:
		die_errno(prog, envs, name);
	}
	struct stat t;
	if (0 > fstat(font_fd.get(), &t))
		goto bad_file;

	void * const base(mmap(nullptr, t.st_size, PROT_READ, MAP_SHARED, font_fd.get(), 0UL));
	if (MAP_FAILED == base) goto bad_file;

	if (CombinedFont::RawFileFont * f = font.AddRawFileFont(weight, slant, 8U, 8U, base, t.st_size, 0U))
		f->AddMapping(0x00000000, 0U, t.st_size / 8U);
}

void
LoadVTFont (
	const char * prog,
	const ProcessEnvironment & envs,
	CombinedFont & font,
	bool faint,
	CombinedFont::Font::Slant slant,
	const char * name
) {
	FileDescriptorOwner font_fd(open_read_at(AT_FDCWD, name));
	if (0 > font_fd.get()) {
bad_file:
		die_errno(prog, envs, name);
	}
	struct stat t;
	if (0 > fstat(font_fd.get(), &t))
		goto bad_file;

	bsd_vtfont_header header;
	if (t.st_size < sizeof header) {
invalid_file:
		die_invalid(prog, envs, name, "Not a valid VT4 font");
	}
	if (0 > pread(font_fd.get(), header, 0U)) goto bad_file;
	if (!good(header)) goto invalid_file;

	const void * const real_base(mmap(nullptr /* no address hint */, t.st_size - sizeof header, PROT_READ, MAP_SHARED, font_fd.get(), 0UL));
	if (MAP_FAILED == real_base) goto bad_file;
	const void * const base(reinterpret_cast<const unsigned char *>(real_base) + sizeof header);
	const std::size_t size(glyphs_size(header));
	if (false) {
bad_glyph_map:
		die_invalid(prog, envs, name, "VT4 font has a corrupt glyph map.");
	}

	if (header.map_lengths[1U] > 0U || header.map_lengths[3U] > 0U) {
		if (CombinedFont::LeftRightVTFileFont * f = font.AddLeftRightVTFileFont(faint, slant, header.height, header.width, base, real_base, size, 0U)) {
			bsd_vtfont_map_entry me;
			off_t pos(map_start(header));
			for (unsigned vtfont_index(0U); vtfont_index < 4U; ++vtfont_index) {
				for (unsigned c(0U); c < header.map_lengths[vtfont_index]; ++c) {
					if (0 > pread(font_fd.get(), me, pos)) goto bad_file;
					if (me.glyph + me.count + 1U > header.glyphs) goto bad_glyph_map;
					f->AddMapping(vtfont_index, me.character, me.glyph, me.count + 1U);
					pos += sizeof me;
				}
			}
		}
	} else
	{
		if (CombinedFont::LeftVTFileFont * f = font.AddLeftVTFileFont(faint, slant, header.height, header.width, base, real_base, size, 0U)) {
			bsd_vtfont_map_entry me;
			off_t pos(map_start(header));
			for (unsigned vtfont_index(0U); vtfont_index < 4U; vtfont_index += 2U) {
				for (unsigned c(0U); c < header.map_lengths[vtfont_index]; ++c) {
					if (0 > pread(font_fd.get(), me, pos)) goto bad_file;
					if (me.glyph + me.count + 1U > header.glyphs) goto bad_glyph_map;
					f->AddMapping(vtfont_index, me.character, me.glyph, me.count + 1U);
					pos += sizeof me;
				}
			}
		}
	}
}

}
