/***************************************************************************
 * ROM Properties Page shell extension. (libromdata)                       *
 * BRSTM.cpp: Nintendo Wii BRSTM audio reader.                             *
 *                                                                         *
 * Copyright (c) 2019 by David Korth.                                      *
 *                                                                         *
 * This program is free software; you can redistribute it and/or modify it *
 * under the terms of the GNU General Public License as published by the   *
 * Free Software Foundation; either version 2 of the License, or (at your  *
 * option) any later version.                                              *
 *                                                                         *
 * This program is distributed in the hope that it will be useful, but     *
 * WITHOUT ANY WARRANTY; without even the implied warranty of              *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 * GNU General Public License for more details.                            *
 *                                                                         *
 * You should have received a copy of the GNU General Public License       *
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.   *
 ***************************************************************************/

#include "BRSTM.hpp"
#include "librpbase/RomData_p.hpp"

#include "brstm_structs.h"

// librpbase
#include "librpbase/common.h"
#include "librpbase/byteswap.h"
#include "librpbase/TextFuncs.hpp"
#include "librpbase/file/IRpFile.hpp"
#include "libi18n/i18n.h"
using namespace LibRpBase;

// C includes. (C++ namespace)
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>

// C++ includes.
#include <string>
#include <sstream>
using std::ostringstream;
using std::string;

namespace LibRomData {

ROMDATA_IMPL(BRSTM)

class BRSTMPrivate : public RomDataPrivate
{
	public:
		BRSTMPrivate(BRSTM *q, IRpFile *file);

	private:
		typedef RomDataPrivate super;
		RP_DISABLE_COPY(BRSTMPrivate)

	public:
		// BRSTM headers.
		// NOTE: Uses the endianness specified by the byte-order mark.
		BRSTM_Header brstmHeader;
		BRSTM_HEAD_Chunk1 headChunk1;

		// Is byteswapping needed?
		bool needsByteswap;

		/**
		 * Byteswap a uint16_t value from BRSTM to CPU.
		 * @param x Value to swap.
		 * @return Swapped value.
		 */
		inline uint16_t brstm16_to_cpu(uint16_t x)
		{
			return (needsByteswap ? __swab16(x) : x);
		}

		/**
		 * Byteswap a uint32_t value from BRSTM to CPU.
		 * @param x Value to swap.
		 * @return Swapped value.
		 */
		inline uint32_t brstm32_to_cpu(uint32_t x)
		{
			return (needsByteswap ? __swab32(x) : x);
		}
};

/** BRSTMPrivate **/

BRSTMPrivate::BRSTMPrivate(BRSTM *q, IRpFile *file)
	: super(q, file)
	, needsByteswap(false)
{
	// Clear the BRSTM header structs.
	memset(&brstmHeader, 0, sizeof(brstmHeader));
	memset(&headChunk1, 0, sizeof(headChunk1));
}

/** BRSTM **/

/**
 * Read a Nintendo Wii BRSTM audio file.
 *
 * A ROM image must be opened by the caller. The file handle
 * will be ref()'d and must be kept open in order to load
 * data from the ROM image.
 *
 * To close the file, either delete this object or call close().
 *
 * NOTE: Check isValid() to determine if this is a valid ROM.
 *
 * @param file Open ROM image.
 */
BRSTM::BRSTM(IRpFile *file)
	: super(new BRSTMPrivate(this, file))
{
	RP_D(BRSTM);
	d->className = "BRSTM";
	d->fileType = FTYPE_AUDIO_FILE;

	if (!d->file) {
		// Could not ref() the file handle.
		return;
	}

	// Read the BRSTM header.
	d->file->rewind();
	size_t size = d->file->read(&d->brstmHeader, sizeof(d->brstmHeader));
	if (size != sizeof(d->brstmHeader)) {
		d->file->unref();
		d->file = nullptr;
		return;
	}

	// Check if this file is supported.
	DetectInfo info;
	info.header.addr = 0;
	info.header.size = sizeof(d->brstmHeader);
	info.header.pData = reinterpret_cast<const uint8_t*>(&d->brstmHeader);
	info.ext = nullptr;	// Not needed for BRSTM.
	info.szFile = 0;	// Not needed for BRSTM.
	d->isValid = (isRomSupported_static(&info) >= 0);

	if (!d->isValid) {
		d->file->unref();
		d->file = nullptr;
		return;
	}

	// Is byteswapping needed?
	d->needsByteswap = (d->brstmHeader.bom == BRSTM_BOM_SWAP);

	// Get the HEAD header.
	BRSTM_HEAD_Header headHeader;
	const uint32_t head_offset = d->brstm32_to_cpu(d->brstmHeader.head.offset);
	const uint32_t head_size = d->brstm32_to_cpu(d->brstmHeader.head.size);
	if (head_offset == 0 || head_size < sizeof(headHeader)) {
		// Invalid HEAD chunk.
		d->isValid = false;
		d->file->unref();
		d->file = nullptr;
		return;
	}
	size = d->file->seekAndRead(head_offset, &headHeader, sizeof(headHeader));
	if (size != sizeof(headHeader)) {
		// Seek and/or read error.
		d->isValid = false;
		d->file->unref();
		d->file = nullptr;
		return;
	}

	// Verify the HEAD header.
	if (headHeader.magic != cpu_to_be32(BRSTM_HEAD_MAGIC)) {
		// Incorrect magic number.
		d->isValid = false;
		d->file->unref();
		d->file = nullptr;
		return;
	}

	// Get the HEAD chunk, part 1.
	// NOTE: Offset is relative to head_offset+8.
	const uint32_t head1_offset = d->brstm32_to_cpu(headHeader.head1_offset);
	if (head1_offset < sizeof(headHeader) - 8) {
		// Invalid offset.
		d->isValid = false;
		d->file->unref();
		d->file = nullptr;
		return;
	}
	size = d->file->seekAndRead(head_offset + 8 + head1_offset, &d->headChunk1, sizeof(d->headChunk1));
	if (size != sizeof(d->headChunk1)) {
		// Seek and/or read error.
		d->isValid = false;
		d->file->unref();
		d->file = nullptr;
		return;
	}

	// TODO: Verify headChunk1, or assume it's valid?
}

/**
 * Is a ROM image supported by this class?
 * @param info DetectInfo containing ROM detection information.
 * @return Class-specific system ID (>= 0) if supported; -1 if not.
 */
int BRSTM::isRomSupported_static(const DetectInfo *info)
{
	assert(info != nullptr);
	assert(info->header.pData != nullptr);
	assert(info->header.addr == 0);
	if (!info || !info->header.pData ||
	    info->header.addr != 0 ||
	    info->header.size < sizeof(BRSTM_Header))
	{
		// Either no detection information was specified,
		// or the header is too small.
		return -1;
	}

	const BRSTM_Header *const brstmHeader =
		reinterpret_cast<const BRSTM_Header*>(info->header.pData);

	// Check the BRSTM magic number.
	if (brstmHeader->magic != cpu_to_be32(BRSTM_MAGIC)) {
		// Not the BRSTM magic number.
		return -1;
	}

	// Check the byte-order mark.
	bool needsByteswap;
	switch (brstmHeader->bom) {
		case BRSTM_BOM_HOST:
			// Host-endian.
			needsByteswap = false;
			break;
		case BRSTM_BOM_SWAP:
			// Swapped-endian.
			needsByteswap = true;
			break;
		default:
			// Invalid.
			return -1;
	}

	// TODO: Check the version number, file size, and header size?

	// Check the chunks.
	// HEAD and DATA must both be present.
	const uint16_t chunk_count = (needsByteswap
		? __swab16(brstmHeader->chunk_count)
		: brstmHeader->chunk_count);
	if (chunk_count < 2) {
		// Not enough chunks.
		return -1;
	}

	// HEAD and DATA offset and sizes must both be non-zero.
	// No byteswapping is needed here.
	if (brstmHeader->head.offset == 0 ||
	    brstmHeader->head.size == 0 ||
	    brstmHeader->data.offset == 0 ||
	    brstmHeader->data.size == 0)
	{
		// Missing a required chunk.
		return -1;
	}

	// This is a BRSTM file.
	return 0;
}

/**
 * Get the name of the system the loaded ROM is designed for.
 * @param type System name type. (See the SystemName enum.)
 * @return System name, or nullptr if type is invalid.
 */
const char *BRSTM::systemName(unsigned int type) const
{
	RP_D(const BRSTM);
	if (!d->isValid || !isSystemNameTypeValid(type))
		return nullptr;

	// BRSTM has the same name worldwide, so we can
	// ignore the region selection.
	static_assert(SYSNAME_TYPE_MASK == 3,
		"BRSTM::systemName() array index optimization needs to be updated.");

	static const char *const sysNames[4] = {
		"Nintendo Wii BRSTM",
		"BRSTM",
		"BRSTM",
		nullptr
	};

	return sysNames[type & SYSNAME_TYPE_MASK];
}

/**
 * Get a list of all supported file extensions.
 * This is to be used for file type registration;
 * subclasses don't explicitly check the extension.
 *
 * NOTE: The extensions include the leading dot,
 * e.g. ".bin" instead of "bin".
 *
 * NOTE 2: The array and the strings in the array should
 * *not* be freed by the caller.
 *
 * @return NULL-terminated array of all supported file extensions, or nullptr on error.
 */
const char *const *BRSTM::supportedFileExtensions_static(void)
{
	static const char *const exts[] = {
		".brstm",

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
const char *const *BRSTM::supportedMimeTypes_static(void)
{
	static const char *const mimeTypes[] = {
		// Unofficial MIME types.
		// TODO: Get these upstreamed on FreeDesktop.org.
		"audio/x-brstm",

		nullptr
	};
	return mimeTypes;
}

/**
 * Load field data.
 * Called by RomData::fields() if the field data hasn't been loaded yet.
 * @return Number of fields read on success; negative POSIX error code on error.
 */
int BRSTM::loadFieldData(void)
{
	RP_D(BRSTM);
	if (!d->fields->empty()) {
		// Field data *has* been loaded...
		return 0;
	} else if (!d->file) {
		// File isn't open.
		return -EBADF;
	} else if (!d->isValid) {
		// Unknown file type.
		return -EIO;
	}

	// BRSTM headers
	const BRSTM_Header *const brstmHeader = &d->brstmHeader;
	const BRSTM_HEAD_Chunk1 *const headChunk1 = &d->headChunk1;
	d->fields->reserve(8);	// Maximum of 8 fields.

	// Version
	d->fields->addField_string(C_("RomData", "Version"),
		rp_sprintf("%u.%u", brstmHeader->version_major, brstmHeader->version_minor));

	// Endianness
	d->fields->addField_string(C_("BRSTM", "Endianness"),
		(brstmHeader->bom == cpu_to_be16(BRSTM_BOM_HOST))
			? C_("BRSTM", "Big-Endian")
			: C_("BRSTM", "Little-Endian"));

	// Codec
	const char *const codec_tbl[] = {
		NOP_C_("BRSTM|Codec", "Signed 8-bit PCM"),
		NOP_C_("BRSTM|Codec", "Signed 16-bit PCM"),
		NOP_C_("BRSTM|Codec", "4-bit THP ADPCM"),
	};
	if (headChunk1->codec < ARRAY_SIZE(codec_tbl)) {
		d->fields->addField_string(C_("BRSTM", "Codec"),
			dpgettext_expr(RP_I18N_DOMAIN, "BRSTM|Codec", codec_tbl[headChunk1->codec]));
	} else {
		d->fields->addField_string(C_("BRSTM", "Codec"),
			rp_sprintf(C_("RomData", "Unknown (%u)"), headChunk1->codec));
	}

	// Number of channels
	d->fields->addField_string_numeric(C_("RomData|Audio", "Channels"), headChunk1->channel_count);

	// Sample rate and sample count
	const uint16_t sample_rate = d->brstm16_to_cpu(headChunk1->sample_rate);
	const uint32_t sample_count = d->brstm32_to_cpu(headChunk1->sample_count);

	// Sample rate
	// NOTE: Using ostringstream for localized numeric formatting.
	ostringstream oss;
	oss << sample_rate << " Hz";
	d->fields->addField_string(C_("RomData|Audio", "Sample Rate"), oss.str());

	// Length (non-looping)
	d->fields->addField_string(C_("RomData|Audio", "Length"),
		formatSampleAsTime(sample_count, sample_rate));

	// Looping
	d->fields->addField_string(C_("BRSTM", "Looping"),
		(headChunk1->loop_flag ? C_("RomData", "Yes") : C_("RomData", "No")));
	if (headChunk1->loop_flag) {
		d->fields->addField_string(C_("BRSTM", "Loop Start"),
			formatSampleAsTime(d->brstm32_to_cpu(headChunk1->loop_start), sample_rate));
	}

	// Finished reading the field data.
	return static_cast<int>(d->fields->count());
}

/**
 * Load metadata properties.
 * Called by RomData::metaData() if the field data hasn't been loaded yet.
 * @return Number of metadata properties read on success; negative POSIX error code on error.
 */
int BRSTM::loadMetaData(void)
{
	RP_D(BRSTM);
	if (d->metaData != nullptr) {
		// Metadata *has* been loaded...
		return 0;
	} else if (!d->file) {
		// File isn't open.
		return -EBADF;
	} else if (!d->isValid) {
		// Unknown file type.
		return -EIO;
	}

	// Create the metadata object.
	d->metaData = new RomMetaData();
	d->metaData->reserve(3);	// Maximum of 3 metadata properties.

	// BRSTM header chunk 1
	const BRSTM_HEAD_Chunk1 *const headChunk1 = &d->headChunk1;

	// Number of channels
	d->metaData->addMetaData_integer(Property::Channels, headChunk1->channel_count);

	// Sample rate and sample count
	const uint16_t sample_rate = d->brstm16_to_cpu(headChunk1->sample_rate);
	const uint32_t sample_count = d->brstm32_to_cpu(headChunk1->sample_count);

	// Sample rate
	d->metaData->addMetaData_integer(Property::SampleRate, sample_rate);

	// Length, in milliseconds (non-looping)
	d->metaData->addMetaData_integer(Property::Duration,
		convSampleToMs(sample_count, sample_rate));

	// Finished reading the metadata.
	return static_cast<int>(d->metaData->count());
}

}
