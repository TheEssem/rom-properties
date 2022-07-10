/***************************************************************************
 * ROM Properties Page shell extension. (libromdata)                       *
 * PSF.hpp: PSF audio reader.                                              *
 *                                                                         *
 * Copyright (c) 2018-2022 by David Korth.                                 *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

#include "stdafx.h"
#include "config.librpbase.h"

#include "PSF.hpp"
#include "psf_structs.h"

// librpbase, librpfile
using namespace LibRpBase;
using LibRpFile::IRpFile;

// C++ STL classes
using std::string;
using std::u8string;
using std::unique_ptr;
using std::unordered_map;
using std::vector;

namespace LibRomData {

class PSFPrivate final : public RomDataPrivate
{
	public:
		PSFPrivate(PSF *q, IRpFile *file);

	private:
		typedef RomDataPrivate super;
		RP_DISABLE_COPY(PSFPrivate)

	public:
		/** RomDataInfo **/
		static const char8_t *const exts[];
		static const char *const mimeTypes[];
		static const RomDataInfo romDataInfo;

	public:
		// PSF header
		// NOTE: **NOT** byteswapped in memory.
		PSF_Header psfHeader;

		// Tags map
		typedef unordered_map<u8string, u8string> psf_tags_t;

		/**
		 * Parse the tag section.
		 * @param tag_addr Tag section starting address.
		 * @return Map containing key/value entries.
		 */
		psf_tags_t parseTags(off64_t tag_addr);

		/**
		 * Get the "ripped by" tag name for the specified PSF version.
		 * @param version PSF version.
		 * @return "Ripped by" tag name.
		 */
		const char8_t *getRippedByTagName(uint8_t version);

		/**
		 * Convert a PSF length string to milliseconds.
		 * @param str PSF length string.
		 * @return Milliseconds.
		 */
		static unsigned int lengthToMs(const char *str);
};

ROMDATA_IMPL(PSF)

/** PSFPrivate **/

/* RomDataInfo */
const char8_t *const PSFPrivate::exts[] = {
	// NOTE: The .*lib files are not listed, since they
	// contain samples, not songs.

	U8(".psf"), U8(".minipsf"),
	U8(".psf1"), U8(".minipsf1"),
	U8(".psf2"), U8(".minipsf2"),

	U8(".ssf"), U8(".minissf"),
	U8(".dsf"), U8(".minidsf"),

	U8(".usf"), U8(".miniusf"),
	U8(".gsf"), U8(".minigsf"),
	U8(".snsf"), U8(".minisnsf"),

	U8(".qsf"), U8(".miniqsf"),

	nullptr
};
const char *const PSFPrivate::mimeTypes[] = {
	// Unofficial MIME types from FreeDesktop.org.
	"audio/x-psf",
	"audio/x-minipsf",

	nullptr
};
const RomDataInfo PSFPrivate::romDataInfo = {
	"PSF", exts, mimeTypes
};

PSFPrivate::PSFPrivate(PSF *q, IRpFile *file)
	: super(q, file, &romDataInfo)
{
	// Clear the PSF header struct.
	memset(&psfHeader, 0, sizeof(psfHeader));
}

/**
 * Parse the tag section.
 * @param tag_addr Tag section starting address.
 * @return Map containing key/value entries.
 */
PSFPrivate::psf_tags_t PSFPrivate::parseTags(off64_t tag_addr)
{
	psf_tags_t kv;

	// Read the tag magic first.
	char tag_magic[sizeof(PSF_TAG_MAGIC)-1];
	size_t size = file->seekAndRead(tag_addr, tag_magic, sizeof(tag_magic));
	if (size != sizeof(tag_magic)) {
		// Seek and/or read error.
		return kv;
	}

#ifdef HAVE_UNORDERED_MAP_RESERVE
	kv.reserve(11);
#endif /* HAVE_UNORDERED_MAP_RESERVE */

	// Read the rest of the file.
	// NOTE: Maximum of 16 KB.
	off64_t data_len = file->size() - tag_addr - sizeof(tag_magic);
	if (data_len <= 0) {
		// Not enough data...
		return kv;
	}

	// NOTE: Values may be encoded as either cp1252/sjis or UTF-8.
	// Since we won't be able to determine this until we're finished
	// decoding variables, we'll have to do character conversion
	// *after* kv is populated.
	const size_t data_len_sz = static_cast<size_t>(data_len);
	unique_ptr<char8_t[]> tag_data(new char8_t[data_len_sz]);
	size = file->read(tag_data.get(), data_len_sz);
	if (size != data_len_sz) {
		// Read error.
		return kv;
	}

	bool isUtf8 = false;
	const char8_t *start = tag_data.get();
	const char8_t *const endptr = start + data_len;
	for (const char8_t *p = start; p < endptr; p++) {
		// Find the next newline.
		const char8_t *nl = static_cast<const char8_t*>(memchr(p, '\n', endptr-p));
		if (!nl) {
			// No newline. Assume this is the end of the tag section,
			// and read up to the end.
			nl = endptr;
		}
		if (p == nl) {
			// Empty line.
			continue;
		}

		// Find the equals sign.
		const char8_t *eq = static_cast<const char8_t*>(memchr(p, '=', nl-p));
		if (eq) {
			// Found the equals sign.
			int k_len = (int)(eq - p);
			int v_len = (int)(nl - eq - 1);
			if (k_len > 0 && v_len > 0) {
				// Key and value are valid.
				// NOTE: Key is case-insensitive, so convert to lowercase.
				// NOTE: Key *must* be ASCII.
				u8string key(p, k_len);
				std::transform(key.begin(), key.end(), key.begin(),
					[](char8_t c) { return std::tolower(c); });
				kv.emplace(key, u8string(reinterpret_cast<const char8_t*>(eq)+1, v_len));

				// Check for UTF-8.
				// NOTE: v_len check is redundant...
				if (key == U8("utf8") && v_len > 0) {
					// "utf8" key with non-empty value.
					// This is UTF-8.
					isUtf8 = true;
				}
			}
		}

		// Next line.
		p = nl;
	}

	// If we're not using UTF-8, convert the values.
	if (!isUtf8) {
		for (auto &p : kv) {
			p.second = cp1252_sjis_to_utf8(reinterpret_cast<const char*>(p.second.c_str()));
		}
	}

	return kv;
}

/**
 * Get the "ripped by" tag name for the specified PSF version.
 * @param version PSF version.
 * @return "Ripped by" tag name.
 */
const char8_t *PSFPrivate::getRippedByTagName(uint8_t version)
{
	static const struct {
		uint8_t version;
		char8_t tag_name[7];
	} psfby_lkup_tbl[] = {
		{PSF_VERSION_PLAYSTATION,	U8("psfby")},
		{PSF_VERSION_PLAYSTATION_2,	U8("psfby")},
		{PSF_VERSION_SATURN,		U8("ssfby")},
		{PSF_VERSION_DREAMCAST,		U8("dsfby")},
		{PSF_VERSION_MEGA_DRIVE,	U8("msfby")}, // FIXME: May be incorrect.
		{PSF_VERSION_N64,		U8("usfby")},
		{PSF_VERSION_GBA,		U8("gsfby")},
		{PSF_VERSION_SNES,		U8("snsfby")},
		{PSF_VERSION_QSOUND,		U8("qsfby")},
	};

	for (const auto &p : psfby_lkup_tbl) {
		if (p.version == version) {
			// Found a match.
			return p.tag_name;
		}
	}

	// No match. Assume it's PSF.
	return psfby_lkup_tbl[0].tag_name;
}

/**
 * Convert a PSF length string to milliseconds.
 * @param str PSF length string.
 * @return Milliseconds.
 */
unsigned int PSFPrivate::lengthToMs(const char *str)
{
	/**
	 * Possible formats:
	 * - seconds.decimal
	 * - minutes:seconds.decimal
	 * - hours:minutes:seconds.decimal
	 *
	 * Decimal may be omitted.
	 * Commas are also accepted.
	 */

	// TODO: Verify 'frac' length.
	// All fractional portions observed thus far are
	// three digits (milliseconds).
	unsigned int hour, min, sec, frac;

	// Check the 'frac' length.
	unsigned int frac_adj = 0;
	const char *dp = strchr(str, '.');
	if (!dp) {
		dp = strchr(str, ',');
	}
	if (dp) {
		// Found the decimal point.
		// Count how many digits are after it.
		unsigned int digit_count = 0;
		dp++;
		for (; *dp != '\0'; dp++) {
			if (ISDIGIT(*dp)) {
				// Found a digit.
				digit_count++;
			} else {
				// Not a digit.
				break;
			}
		}
		switch (digit_count) {
			case 0:
				// No digits.
				frac_adj = 0;
				break;
			case 1:
				// One digit. (tenths)
				frac_adj = 100;
				break;
			case 2:
				// Two digits. (hundredths)
				frac_adj = 10;
				break;
			case 3:
				// Three digits. (thousandths)
				frac_adj = 1;
				break;
			default:
				// Too many digits...
				// TODO: Mask these digits somehow.
				frac_adj = 1;
				break;
		}
	}

	// hours:minutes:seconds.decimal
	int s = sscanf(str, "%u:%u:%u.%u", &hour, &min, &sec, &frac);
	if (s != 4) {
		s = sscanf(str, "%u:%u:%u,%u", &hour, &min, &sec, &frac);
	}
	if (s == 4) {
		// Format matched.
		return (hour * 60 * 60 * 1000) +
		       (min * 60 * 1000) +
		       (sec * 1000) +
		       (frac * frac_adj);
	}

	// hours:minutes:seconds
	s = sscanf(str, "%u:%u:%u", &hour, &min, &sec);
	if (s == 3) {
		// Format matched.
		return (hour * 60 * 60 * 1000) +
		       (min * 60 * 1000) +
		       (sec * 1000);
	}

	// minutes:seconds.decimal
	s = sscanf(str, "%u:%u.%u", &min, &sec, &frac);
	if (s != 3) {
		s = sscanf(str, "%u:%u,%u", &min, &sec, &frac);
	}
	if (s == 3) {
		// Format matched.
		return (min * 60 * 1000) +
		       (sec * 1000) +
		       (frac * frac_adj);
	}

	// minutes:seconds
	s = sscanf(str, "%u:%u", &min, &sec);
	if (s == 2) {
		// Format matched.
		return (min * 60 * 1000) +
		       (sec * 1000);
	}

	// seconds.decimal
	s = sscanf(str, "%u.%u", &sec, &frac);
	if (s != 2) {
		s = sscanf(str, "%u,%u", &sec, &frac);
	}
	if (s == 2) {
		// Format matched.
		return (min * 60 * 1000) +
		       (sec * 1000) +
		       (frac * frac_adj);
	}

	// seconds
	s = sscanf(str, "%u", &sec);
	if (s == 1) {
		// Format matched.
		return sec;
	}

	// No matches.
	return 0;
}

/** PSF **/

/**
 * Read an PSF audio file.
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
PSF::PSF(IRpFile *file)
	: super(new PSFPrivate(this, file))
{
	RP_D(PSF);
	d->mimeType = "audio/x-psf";	// unofficial (TODO: x-minipsf?)
	d->fileType = FileType::AudioFile;

	if (!d->file) {
		// Could not ref() the file handle.
		return;
	}

	// Read the PSF header.
	d->file->rewind();
	size_t size = d->file->read(&d->psfHeader, sizeof(d->psfHeader));
	if (size != sizeof(d->psfHeader)) {
		UNREF_AND_NULL_NOCHK(d->file);
		return;
	}

	// Check if this file is supported.
	const DetectInfo info = {
		{0, sizeof(d->psfHeader), reinterpret_cast<const uint8_t*>(&d->psfHeader)},
		nullptr,	// ext (not needed for PSF)
		0		// szFile (not needed for PSF)
	};
	d->isValid = (isRomSupported_static(&info) >= 0);

	if (!d->isValid) {
		UNREF_AND_NULL_NOCHK(d->file);
	}
}

/**
 * Is a ROM image supported by this class?
 * @param info DetectInfo containing ROM detection information.
 * @return Class-specific system ID (>= 0) if supported; -1 if not.
 */
int PSF::isRomSupported_static(const DetectInfo *info)
{
	assert(info != nullptr);
	assert(info->header.pData != nullptr);
	assert(info->header.addr == 0);
	if (!info || !info->header.pData ||
	    info->header.addr != 0 ||
	    info->header.size < sizeof(PSF_Header))
	{
		// Either no detection information was specified,
		// or the header is too small.
		return -1;
	}

	const PSF_Header *const psfHeader =
		reinterpret_cast<const PSF_Header*>(info->header.pData);

	// Check the PSF magic number.
	if (!memcmp(psfHeader->magic, PSF_MAGIC, sizeof(psfHeader->magic))) {
		// Found the PSF magic number.
		return 0;
	}

	// Not supported.
	return -1;
}

/**
 * Get the name of the system the loaded ROM is designed for.
 * @param type System name type. (See the SystemName enum.)
 * @return System name, or nullptr if type is invalid.
 */
const char *PSF::systemName(unsigned int type) const
{
	RP_D(const PSF);
	if (!d->isValid || !isSystemNameTypeValid(type))
		return nullptr;

	// PSF has the same name worldwide, so we can
	// ignore the region selection.
	static_assert(SYSNAME_TYPE_MASK == 3,
		"PSF::systemName() array index optimization needs to be updated.");

	// Bits 0-1: Type. (long, short, abbreviation)
	static const char *const sysNames[4] = {
		"Portable Sound Format", "PSF", "PSF", nullptr
	};

	return sysNames[type & SYSNAME_TYPE_MASK];
}

/**
 * Load field data.
 * Called by RomData::fields() if the field data hasn't been loaded yet.
 * @return Number of fields read on success; negative POSIX error code on error.
 */
int PSF::loadFieldData(void)
{
	RP_D(PSF);
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

	// PSF header.
	const PSF_Header *const psfHeader = &d->psfHeader;

	// PSF fields:
	// - 1 regular field.
	// - 11 fields in the "[TAG]" section.
	d->fields->reserve(1+11);

	// System.
	static const struct {
		uint8_t version;
		const char8_t *sysname;
	} sysname_lkup_tbl[] = {
		{PSF_VERSION_PLAYSTATION,	NOP_C_("PSF|System", "Sony PlayStation")},
		{PSF_VERSION_PLAYSTATION_2,	NOP_C_("PSF|System", "Sony PlayStation 2")},
		{PSF_VERSION_SATURN,		NOP_C_("PSF|System", "Sega Saturn")},
		{PSF_VERSION_DREAMCAST,		NOP_C_("PSF|System", "Sega Dreamcast")},
		{PSF_VERSION_MEGA_DRIVE,	NOP_C_("PSF|System", "Sega Mega Drive")},
		{PSF_VERSION_N64,		NOP_C_("PSF|System", "Nintendo 64")},
		{PSF_VERSION_GBA,		NOP_C_("PSF|System", "Game Boy Advance")},
		{PSF_VERSION_SNES,		NOP_C_("PSF|System", "Super NES")},
		{PSF_VERSION_QSOUND,		NOP_C_("PSF|System", "Capcom QSound")},
	};

	const uint8_t psf_version = psfHeader->version;
	const char8_t *sysname = nullptr;
	for (const auto &p : sysname_lkup_tbl) {
		if (p.version == psf_version) {
			// Found a match.
			sysname = p.sysname;
			break;
		}
	}

	// FIXME: U8STRFIX - dpgettext_expr(), rp_sprintf()
	const char8_t *const system_title = C_("PSF", "System");
	if (sysname) {
		d->fields->addField_string(system_title,
			dpgettext_expr(RP_I18N_DOMAIN, "PSF|System", reinterpret_cast<const char*>(sysname)));
	} else {
		d->fields->addField_string(system_title,
			rp_sprintf(reinterpret_cast<const char*>(C_("RomData", "Unknown (0x%02X)")), psf_version));
	}

	// Parse the tags.
	const off64_t tag_addr = (off64_t)sizeof(*psfHeader) +
		le32_to_cpu(psfHeader->reserved_size) +
		le32_to_cpu(psfHeader->compressed_prg_length);
	const PSFPrivate::psf_tags_t tags = d->parseTags(tag_addr);

	if (!tags.empty()) {
		// Title
		auto iter = tags.find(U8("title"));
		if (iter != tags.end()) {
			d->fields->addField_string(C_("RomData|Audio", "Title"), iter->second);
		}

		// Artist
		iter = tags.find(U8("artist"));
		if (iter != tags.end()) {
			d->fields->addField_string(C_("RomData|Audio", "Artist"), iter->second);
		}

		// Game
		iter = tags.find(U8("game"));
		if (iter != tags.end()) {
			d->fields->addField_string(C_("PSF", "Game"), iter->second);
		}

		// Release Date
		// NOTE: The tag is "year", but it may be YYYY-MM-DD.
		iter = tags.find(U8("year"));
		if (iter != tags.end()) {
			d->fields->addField_string(C_("RomData", "Release Date"), iter->second);
		}

		// Genre
		iter = tags.find(U8("genre"));
		if (iter != tags.end()) {
			d->fields->addField_string(C_("RomData|Audio", "Genre"), iter->second);
		}

		// Copyright
		iter = tags.find(U8("copyright"));
		if (iter != tags.end()) {
			d->fields->addField_string(C_("RomData|Audio", "Copyright"), iter->second);
		}

		// Ripped By
		// NOTE: The tag varies based on PSF version.
		const char8_t *const ripped_by_tag = d->getRippedByTagName(psfHeader->version);
		const char8_t *const ripped_by_title = C_("PSF", "Ripped By");
		iter = tags.find(ripped_by_tag);
		if (iter != tags.end()) {
			d->fields->addField_string(ripped_by_title, iter->second);
		} else {
			// Try "psfby" if the system-specific one isn't there.
			iter = tags.find(U8("psfby"));
			if (iter != tags.end()) {
				d->fields->addField_string(ripped_by_title, iter->second);
			}
		}

		// Volume (floating-point number)
		iter = tags.find(U8("volume"));
		if (iter != tags.end()) {
			d->fields->addField_string(C_("PSF", "Volume"), iter->second);
		}

		// Duration
		//
		// Possible formats:
		// - seconds.decimal
		// - minutes:seconds.decimal
		// - hours:minutes:seconds.decimal
		//
		// Decimal may be omitted.
		// Commas are also accepted.
		iter = tags.find(U8("length"));
		if (iter != tags.end()) {
			d->fields->addField_string(C_("RomData|Audio", "Duration"), iter->second);
		}

		// Fadeout duration
		// Same format as duration.
		iter = tags.find(U8("fade"));
		if (iter != tags.end()) {
			d->fields->addField_string(C_("PSF", "Fadeout Duration"), iter->second);
		}

		// Comment
		iter = tags.find(U8("comment"));
		if (iter != tags.end()) {
			d->fields->addField_string(C_("RomData|Audio", "Comment"), iter->second);
		}
	}

	// Finished reading the field data.
	return static_cast<int>(d->fields->count());
}

/**
 * Load metadata properties.
 * Called by RomData::metaData() if the field data hasn't been loaded yet.
 * @return Number of metadata properties read on success; negative POSIX error code on error.
 */
int PSF::loadMetaData(void)
{
	RP_D(PSF);
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

	// PSF header.
	const PSF_Header *const psfHeader = &d->psfHeader;

	// Attempt to parse the tags before doing anything else.
	const off64_t tag_addr = (off64_t)sizeof(*psfHeader) +
		le32_to_cpu(psfHeader->reserved_size) +
		le32_to_cpu(psfHeader->compressed_prg_length);
	const PSFPrivate::psf_tags_t tags = d->parseTags(tag_addr);

	if (tags.empty()) {
		// No tags.
		return -EIO;
	}

	// Create the metadata object.
	d->metaData = new RomMetaData();
	d->metaData->reserve(8);	// Maximum of 8 metadata properties.

	// Title
	auto iter = tags.find(U8("title"));
	if (iter != tags.end()) {
		d->metaData->addMetaData_string(Property::Title, iter->second);
	}

	// Artist
	iter = tags.find(U8("artist"));
	if (iter != tags.end()) {
		d->metaData->addMetaData_string(Property::Artist, iter->second);
	}

	// Game
	iter = tags.find(U8("game"));
	if (iter != tags.end()) {
		// NOTE: Not exactly "album"...
		d->metaData->addMetaData_string(Property::Album, iter->second);
	}

	// Release Date
	// NOTE: The tag is "year", but it may be YYYY-MM-DD.
	iter = tags.find(U8("year"));
	if (iter != tags.end()) {
		// Parse the release date.
		// NOTE: Only year is supported.
		int year;
		char chr;
		// NOTE: sscanf() doesn't support char8_t.
		int s = sscanf(reinterpret_cast<const char*>(iter->second.c_str()), "%04d%c", &year, &chr);
		if (s == 1 || (s == 2 && (chr == '-' || chr == '/'))) {
			// Year seems to be valid.
			// Make sure the number is acceptable:
			// - No negatives.
			// - Four-digit only. (lol Y10K)
			if (year >= 0 && year < 10000) {
				d->metaData->addMetaData_uint(Property::ReleaseYear, (unsigned int)year);
			}
		}
	}

	// Genre
	iter = tags.find(U8("genre"));
	if (iter != tags.end()) {
		d->metaData->addMetaData_string(Property::Genre, iter->second);
	}

	// Copyright
	iter = tags.find(U8("copyright"));
	if (iter != tags.end()) {
		d->metaData->addMetaData_string(Property::Copyright, iter->second);
	}

#if 0
	// FIXME: No property for this...
	// Ripped By
	// NOTE: The tag varies based on PSF version.
	const char8_t *const ripped_by_tag = d->getRippedByTagName(psfHeader->version);
	iter = tags.find(ripped_by_tag);
	if (iter != tags.end()) {
		// FIXME: No property for this...
		d->metaData->addMetaData_string(Property::RippedBy, iter->second);
	} else {
		// Try "psfby" if the system-specific one isn't there.
		iter = tags.find(U8("psfby"));
		if (iter != tags.end()) {
			// FIXME: No property for this...
			d->metaData->addMetaData_string(Property::RippedBy, iter->second);
		}
	}
#endif

	// Duration
	//
	// Possible formats:
	// - seconds.decimal
	// - minutes:seconds.decimal
	// - hours:minutes:seconds.decimal
	//
	// Decimal may be omitted.
	// Commas are also accepted.
	iter = tags.find(U8("length"));
	if (iter != tags.end()) {
		// Convert the length string to milliseconds.
		// FIXME: lengthToMs() can't easily be converted to use char8_t.
		const unsigned int ms = d->lengthToMs(reinterpret_cast<const char*>(iter->second.c_str()));
		d->metaData->addMetaData_integer(Property::Duration, ms);
	}

	// Comment
	iter = tags.find(U8("comment"));
	if (iter != tags.end()) {
		// TODO: Property::Comment is assumed to be user-added
		// on KDE Dolphin 18.08.1. Needs a description property.
		// Also needs verification on Windows.
		d->metaData->addMetaData_string(Property::Subject, iter->second);
	}

	// Finished reading the metadata.
	return static_cast<int>(d->metaData->count());
}

}
