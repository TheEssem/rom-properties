/***************************************************************************
 * ROM Properties Page shell extension. (libromdata)                       *
 * N64.cpp: Nintendo 64 ROM image reader.                                  *
 *                                                                         *
 * Copyright (c) 2016-2019 by David Korth.                                 *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

#include "N64.hpp"
#include "librpbase/RomData_p.hpp"

#include "n64_structs.h"

// librpbase
#include "librpbase/common.h"
#include "librpbase/byteswap.h"
#include "librpbase/TextFuncs.hpp"
#include "librpbase/file/IRpFile.hpp"
using namespace LibRpBase;

// libi18n
#include "libi18n/i18n.h"

// C includes. (C++ namespace)
#include "librpbase/ctypex.h"
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>

// C++ includes.
#include <string>
#include <vector>
using std::string;
using std::vector;

namespace LibRomData {

ROMDATA_IMPL(N64)

class N64Private : public RomDataPrivate
{
	public:
		N64Private(N64 *q, IRpFile *file);

	private:
		typedef RomDataPrivate super;
		RP_DISABLE_COPY(N64Private)

	public:
		// ROM image type.
		enum RomType {
			ROM_TYPE_UNKNOWN = -1,	// Unknown ROM type.

			ROM_TYPE_Z64 = 0,	// Z64 format
			ROM_TYPE_V64 = 1,	// V64 format
			ROM_TYPE_SWAP2 = 2,	// swap2 format
			ROM_TYPE_LE32 = 3,	// LE32 format
		};
		int romType;

	public:
		// ROM header.
		// NOTE: Fields have been byteswapped in the constructor.
		N64_RomHeader romHeader;
};

/** N64Private **/

N64Private::N64Private(N64 *q, IRpFile *file)
	: super(q, file)
	, romType(ROM_TYPE_UNKNOWN)
{
	// Clear the ROM header struct.
	memset(&romHeader, 0, sizeof(romHeader));
}

/** N64 **/

/**
 * Read a Nintendo 64 ROM image.
 *
 * A ROM file must be opened by the caller. The file handle
 * will be ref()'d and must be kept open in order to load
 * data from the ROM.
 *
 * To close the file, either delete this object or call close().
 *
 * NOTE: Check isValid() to determine if this is a valid ROM.
 *
 * @param file Open ROM image.
 */
N64::N64(IRpFile *file)
	: super(new N64Private(this, file))
{
	RP_D(N64);
	d->className = "N64";

	if (!d->file) {
		// Could not ref() the file handle.
		return;
	}

	// Read the ROM image header.
	d->file->rewind();
	size_t size = d->file->read(&d->romHeader, sizeof(d->romHeader));
	if (size != sizeof(d->romHeader)) {
		d->file->unref();
		d->file = nullptr;
		return;
	}

	// Check if this ROM image is supported.
	DetectInfo info;
	info.header.addr = 0;
	info.header.size = sizeof(d->romHeader);
	info.header.pData = reinterpret_cast<const uint8_t*>(&d->romHeader);
	info.ext = nullptr;	// Not needed for N64.
	info.szFile = 0;	// Not needed for N64.
	d->romType = isRomSupported_static(&info);

	switch (d->romType) {
		case N64Private::ROM_TYPE_Z64:
			// Z64 format. Byteswapping will be done afterwards.
			break;

		case N64Private::ROM_TYPE_V64:
			// V64 format. (16-bit byteswapped)
			// Convert the header to Z64 first.
			__byte_swap_16_array(d->romHeader.u16, sizeof(d->romHeader.u16));
			break;

		case N64Private::ROM_TYPE_SWAP2:
			// swap2 format. (wordswapped)
			// Convert the header to Z64 first.
			#define UNSWAP2(x) (uint32_t)(((x) >> 16) | ((x) << 16))
			for (int i = 0; i < ARRAY_SIZE(d->romHeader.u32); i++) {
				d->romHeader.u32[i] = UNSWAP2(d->romHeader.u32[i]);
			}
			break;

		case N64Private::ROM_TYPE_LE32:
			// LE32 format. (32-bit byteswapped)
			// Convert the header to Z64 first.
			// TODO: Optimize by not converting the non-text fields
			// if the host system is little-endian?
			// FIXME: Untested - ucon64 doesn't support it.
			__byte_swap_32_array(d->romHeader.u32, sizeof(d->romHeader.u32));
			break;

		default:
			// Unknown ROM type.
			d->romType = N64Private::ROM_TYPE_UNKNOWN;
			d->file->unref();
			d->file = nullptr;
			return;
	}

	d->isValid = true;

	// Byteswap the header from Z64 format.
	d->romHeader.init_pi	= be32_to_cpu(d->romHeader.init_pi);
	d->romHeader.clockrate	= be32_to_cpu(d->romHeader.clockrate);
	d->romHeader.entrypoint	= be32_to_cpu(d->romHeader.entrypoint);
	d->romHeader.crc[0]     = be32_to_cpu(d->romHeader.crc[0]);
	d->romHeader.crc[1]     = be32_to_cpu(d->romHeader.crc[1]);
}

/** ROM detection functions. **/

/**
 * Is a ROM image supported by this class?
 * @param info DetectInfo containing ROM detection information.
 * @return Class-specific system ID (>= 0) if supported; -1 if not.
 */
int N64::isRomSupported_static(const DetectInfo *info)
{
	assert(info != nullptr);
	assert(info->header.pData != nullptr);
	assert(info->header.addr == 0);
	if (!info || !info->header.pData ||
	    info->header.addr != 0 ||
	    info->header.size < sizeof(N64_RomHeader))
	{
		// Either no detection information was specified,
		// or the header is too small.
		return -1;
	}

	const N64_RomHeader *const romHeader =
		reinterpret_cast<const N64_RomHeader*>(info->header.pData);

	// Check the magic number.
	// NOTE: This technically isn't a "magic number",
	// but it appears to be the same for all N64 ROMs.
	// TODO: Check 32-bit code generation.
	int romType = N64Private::ROM_TYPE_UNKNOWN;
	if (romHeader->magic64 == cpu_to_be64(N64_Z64_MAGIC)) {
		romType = N64Private::ROM_TYPE_Z64;
	} else if (romHeader->magic64 == cpu_to_be64(N64_V64_MAGIC)) {
		romType = N64Private::ROM_TYPE_V64;
	} else if (romHeader->magic64 == cpu_to_be64(N64_SWAP2_MAGIC)) {
		romType = N64Private::ROM_TYPE_SWAP2;
	} else if (romHeader->magic64 == cpu_to_be64(N64_LE32_MAGIC)) {
		romType = N64Private::ROM_TYPE_LE32;
	}

	return romType;
}

/**
 * Get the name of the system the loaded ROM is designed for.
 * @param type System name type. (See the SystemName enum.)
 * @return System name, or nullptr if type is invalid.
 */
const char *N64::systemName(unsigned int type) const
{
	RP_D(const N64);
	if (!d->isValid || !isSystemNameTypeValid(type))
		return nullptr;

	// N64 has the same name worldwide, so we can
	// ignore the region selection.
	static_assert(SYSNAME_TYPE_MASK == 3,
		"N64::systemName() array index optimization needs to be updated.");

	// Bits 0-1: Type. (long, short, abbreviation)
	static const char *const sysNames[4] = {
		"Nintendo 64", "Nintendo 64", "N64", nullptr
	};

	return sysNames[type & SYSNAME_TYPE_MASK];
}

/**
 * Get a list of all supported file extensions.
 * This is to be used for file type registration;
 * subclasses don't explicitly check the extension.
 *
 * NOTE: The extensions do not include the leading dot,
 * e.g. "bin" instead of ".bin".
 *
 * NOTE 2: The array and the strings in the array should
 * *not* be freed by the caller.
 *
 * @return NULL-terminated array of all supported file extensions, or nullptr on error.
 */
const char *const *N64::supportedFileExtensions_static(void)
{
	static const char *const exts[] = {
		".z64", ".n64", ".v64",
		nullptr
	};
	return exts;
}

/**
 * Get a list of all supported MIME types.
 * This is to be used for metadata extractors that
 * must indicate which MIME types they support.
 *
 * NOTE: The array and the strings in the array should
 * *not* be freed by the caller.
 *
 * @return NULL-terminated array of all supported file extensions, or nullptr on error.
 */
const char *const *N64::supportedMimeTypes_static(void)
{
	static const char *const mimeTypes[] = {
		// Unofficial MIME types from FreeDesktop.org.
		"application/x-n64-rom",

		nullptr
	};
	return mimeTypes;
}

/**
 * Load field data.
 * Called by RomData::fields() if the field data hasn't been loaded yet.
 * @return Number of fields read on success; negative POSIX error code on error.
 */
int N64::loadFieldData(void)
{
	RP_D(N64);
	if (!d->fields->empty()) {
		// Field data *has* been loaded...
		return 0;
	} else if (!d->file || !d->file->isOpen()) {
		// File isn't open.
		return -EBADF;
	} else if (!d->isValid || d->romType < 0) {
		// Unknown ROM image type.
		return -EIO;
	}

	// snprintf() buffer.
	char buf[32];

	// ROM file header is read and byteswapped in the constructor.
	// TODO: Indicate the byteswapping format?
	const N64_RomHeader *const romHeader = &d->romHeader;
	d->fields->reserve(6);	// Maximum of 6 fields.

	// Title.
	// TODO: Space elimination.
	d->fields->addField_string(C_("RomData", "Title"),
		cp1252_sjis_to_utf8(romHeader->title, sizeof(romHeader->title)),
		RomFields::STRF_TRIM_END);

	// Game ID.
	// Replace any non-printable characters with underscores.
	char id4[5];
	for (int i = 0; i < 4; i++) {
		id4[i] = (ISPRINT(romHeader->id4[i])
			? romHeader->id4[i]
			: '_');
	}
	id4[4] = 0;
	d->fields->addField_string(C_("N64", "Game ID"),
		latin1_to_utf8(id4, 4));

	// Revision.
	d->fields->addField_string_numeric(C_("RomData", "Revision"),
		romHeader->revision, RomFields::FB_DEC, 2);

	// Entry point.
	d->fields->addField_string_numeric(C_("N64", "Entry Point"),
		romHeader->entrypoint, RomFields::FB_HEX, 8, RomFields::STRF_MONOSPACE);

	// OS version.
	// TODO: ISALPHA(), or ISUPPER()?
	const char *const os_version_title = C_("N64", "OS Version");
	if (romHeader->os_version[0] == 0x00 &&
	    romHeader->os_version[1] == 0x00 &&
	    ISALPHA(romHeader->os_version[3]))
	{
		snprintf(buf, sizeof(buf), "OS %u%c",
			romHeader->os_version[2], romHeader->os_version[3]);
		d->fields->addField_string(os_version_title, buf);
	} else {
		// Unrecognized Release field.
		d->fields->addField_string_hexdump(os_version_title,
			romHeader->os_version, sizeof(romHeader->os_version),
			RomFields::STRF_MONOSPACE);
	}

	// CRCs.
	snprintf(buf, sizeof(buf), "0x%08X 0x%08X",
		romHeader->crc[0], romHeader->crc[1]);
	d->fields->addField_string(C_("N64", "CRCs"),
		buf, RomFields::STRF_MONOSPACE);

	// Finished reading the field data.
	return static_cast<int>(d->fields->count());
}

/**
 * Load metadata properties.
 * Called by RomData::metaData() if the field data hasn't been loaded yet.
 * @return Number of metadata properties read on success; negative POSIX error code on error.
 */
int N64::loadMetaData(void)
{
	RP_D(N64);
	if (d->metaData != nullptr) {
		// Metadata *has* been loaded...
		return 0;
	} else if (!d->file) {
		// File isn't open.
		return -EBADF;
	} else if (!d->isValid || d->romType < 0) {
		// Unknown ROM image type.
		return -EIO;
	}

	// Create the metadata object.
	d->metaData = new RomMetaData();
	d->metaData->reserve(1);	// Maximum of 1 metadata property.

	// ROM file header is read and byteswapped in the constructor.
	// TODO: Indicate the byteswapping format?
	const N64_RomHeader *const romHeader = &d->romHeader;

	// Title.
	// TODO: Space elimination.
	d->metaData->addMetaData_string(Property::Title,
		cp1252_sjis_to_utf8(romHeader->title, sizeof(romHeader->title)),
		RomMetaData::STRF_TRIM_END);

	// Finished reading the metadata.
	return static_cast<int>(d->metaData->count());
}

}
