/***************************************************************************
 * ROM Properties Page shell extension. (libromdata)                       *
 * WiiUPackage.cpp: Wii U NUS Package reader.                              *
 *                                                                         *
 * Copyright (c) 2016-2024 by David Korth.                                 *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

#include "stdafx.h"
#include "config.librpbase.h"
#include "WiiUPackage.hpp"

// RomData subclasses
#include "WiiTicket.hpp"
#include "WiiTMD.hpp"

// Wii U FST
#include "disc/WiiUFst.hpp"

// librpbase
#ifdef ENABLE_DECRYPTION
#  include "librpbase/disc/CBCReader.hpp"
#endif /* ENABLE_DECRYPTION */

// Other rom-properties libraries
using namespace LibRpBase;
using namespace LibRpFile;
using namespace LibRpText;

// C++ STL classes
using std::string;
using std::unique_ptr;
using std::vector;

namespace LibRomData {

class WiiUPackagePrivate final : public RomDataPrivate
{
public:
	WiiUPackagePrivate(const char *path);
	~WiiUPackagePrivate();

private:
	typedef RomDataPrivate super;
	RP_DISABLE_COPY(WiiUPackagePrivate)

public:
	/** RomDataInfo **/
	static const char *const exts[];
	static const char *const mimeTypes[];
	static const RomDataInfo romDataInfo;

public:
	// Directory path (strdup()'d)
	char *path;

	// Ticket, TMD, and FST
	WiiTicket *ticket;
	WiiTMD *tmd;
	WiiUFst *fst;

#ifdef ENABLE_DECRYPTION
	// Title key
	uint8_t title_key[16];
#endif /* ENABLE_DECRYPTION */

	// Contents table
	rp::uvector<WUP_Content_Entry> contentsTable;

	// Contents readers (index is the TMD index)
	vector<IDiscReaderPtr> contentsReaders;

public:
	/**
	 * Clear everything.
	 */
	void reset(void);

	/**
	 * Open a content file.
	 * @param idx Content index (TMD index)
	 * @return Content file, or nullptr on error.
	 */
	IDiscReaderPtr openContentFile(unsigned int idx);
};

ROMDATA_IMPL(WiiUPackage)

/** WiiUPackagePrivate **/

/* RomDataInfo */
const char *const WiiUPackagePrivate::exts[] = {
	// No file extensions; NUS packages are directories.
	nullptr
};
const char *const WiiUPackagePrivate::mimeTypes[] = {
	// NUS packages are directories.
	"inode/directory",

	nullptr
};
const RomDataInfo WiiUPackagePrivate::romDataInfo = {
	"WiiUPackage", exts, mimeTypes
};

WiiUPackagePrivate::WiiUPackagePrivate(const char *path)
	: super({}, &romDataInfo)
	, ticket(nullptr)
	, tmd(nullptr)
	, fst(nullptr)
{
	if (path && path[0] != '\0') {
		this->path = strdup(path);
	} else {
		this->path = nullptr;
	}

#ifdef ENABLE_DECRYPTION
	// Clear the title key.
	memset(title_key, 0, sizeof(title_key));
#endif /* ENABLE_DECRYPTION */
}

WiiUPackagePrivate::~WiiUPackagePrivate()
{
	free(path);
	delete ticket;
	delete tmd;
	delete fst;
}

/**
 * Clear everything.
 */
void WiiUPackagePrivate::reset(void)
{
	free(path);
	delete ticket;
	delete tmd;
	delete fst;

	path = nullptr;
	ticket = nullptr;
	tmd = nullptr;
	path = nullptr;
}

/**
 * Open a content file.
 * @param idx Content index (TMD index)
 * @return Content file, or nullptr on error.
 */
IDiscReaderPtr WiiUPackagePrivate::openContentFile(unsigned int idx)
{
	assert(idx < contentsReaders.size());
	if (idx >= contentsReaders.size())
		return {};

	if (contentsReaders[idx]) {
		// Content is already open.
		return contentsReaders[idx];
	}

#ifdef ENABLE_DECRYPTION
	// Attempt to open the content.
	const WUP_Content_Entry &entry = contentsTable[idx];
	string s_path(this->path);
	s_path += DIR_SEP_CHR;

	// Try with lowercase hex first.
	const uint32_t content_id = be32_to_cpu(entry.content_id);
	char fnbuf[16];
	snprintf(fnbuf, sizeof(fnbuf), "%08x.app", content_id);
	s_path += fnbuf;

	IRpFilePtr subfile = std::make_shared<RpFile>(s_path.c_str(), RpFile::FM_OPEN_READ);
	if (!subfile->isOpen()) {
		// Try with uppercase hex.
		snprintf(fnbuf, sizeof(fnbuf), "%08X.app", content_id);
		s_path.resize(s_path.size()-12);
		s_path += fnbuf;

		subfile = std::make_shared<RpFile>(s_path.c_str(), RpFile::FM_OPEN_READ);
		if (!subfile->isOpen()) {
			// Unable to open the content file.
			// TODO: Error code?
			return {};
		}
	}

	// IV is the 2-byte content index, followed by zeroes.
	uint8_t iv[16];
	memcpy(iv, &entry.index, 2);
	memset(&iv[2], 0, 14);

	// Create a disc reader.
	// TODO: H3 reader if the content is H3-hashed.
	CBCReaderPtr cbcReader = std::make_shared<CBCReader>(subfile, 0, subfile->size(), title_key, iv);
	if (!cbcReader->isOpen()) {
		// Unable to open the CBC reader.
		return {};
	}

	// Disc reader is open.
	contentsReaders[idx] = cbcReader;
	return cbcReader;
#else /* !ENABLE_DECRYPTION */
	// Unencrypted NUS packages are NOT supported right now.
	return {};
#endif /* ENABLE_DECRYPTION */
}

/** WiiUPackage **/

/**
 * Read a Wii U NUS package.
 *
 * NOTE: Wii U NUS packages are directories. This constructor
 * only accepts IRpFilePtr, so it isn't usable.
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
WiiUPackage::WiiUPackage(const IRpFilePtr &file)
	: super(new WiiUPackagePrivate(nullptr))
{
	// Not supported!
	RP_UNUSED(file);
	return;
}

/**
 * Read a Wii U NUS package.
 *
 * NOTE: Wii U NUS packages are directories. This constructor
 * takes a local directory path.
 *
 * A ROM image must be opened by the caller. The file handle
 * will be ref()'d and must be kept open in order to load
 * data from the ROM image.
 *
 * To close the file, either delete this object or call close().
 *
 * NOTE: Check isValid() to determine if this is a valid ROM.
 *
 * @param path Local directory path (UTF-8)
 */
WiiUPackage::WiiUPackage(const char *path)
	: super(new WiiUPackagePrivate(path))
{
	RP_D(WiiUPackage);
	d->fileType = FileType::ApplicationPackage;

	if (!d->path) {
		// No path specified...
		d->reset();
		return;
	}

	// Check if this path is supported.
	d->isValid = (isDirSupported_static(path) >= 0);

	if (!d->isValid) {
		d->reset();
		return;
	}

	// Open the ticket.
	WiiTicket *ticket = nullptr;

	string s_path(d->path);
	s_path += DIR_SEP_CHR;
	s_path += "title.tik";
	IRpFilePtr subfile = std::make_shared<RpFile>(s_path, RpFile::FM_OPEN_READ);
	if (subfile->isOpen()) {
		ticket = new WiiTicket(subfile);
		if (ticket->isValid()) {
			// Make sure the ticket is v1.
			if (ticket->ticketFormatVersion() != 1) {
				// Not v1.
				delete ticket;
				ticket = nullptr;
			}
		} else {
			delete ticket;
			ticket = nullptr;
		}
	}
	subfile.reset();
	if (!ticket) {
		// Unable to load the ticket.
		d->reset();
		return;
	}
	d->ticket = ticket;

	// Open the TMD.
	WiiTMD *tmd = nullptr;

	s_path.resize(s_path.size()-9);
	s_path += "title.tmd";
	subfile = std::make_shared<RpFile>(s_path, RpFile::FM_OPEN_READ);
	if (subfile->isOpen()) {
		tmd = new WiiTMD(subfile);
		if (tmd->isValid()) {
			// Make sure the TMD is v1.
			if (tmd->tmdFormatVersion() != 1) {
				// Not v1.
				delete tmd;
				tmd = nullptr;
			}
		} else {
			delete tmd;
			tmd = nullptr;
		}
	}
	subfile.reset();
	if (!tmd) {
		// Unable to load the TMD.
		d->reset();
		return;
	}
	d->tmd = tmd;

#if ENABLE_DECRYPTION
	// Decrypt the title key.
	int ret = d->ticket->decryptTitleKey(d->title_key, sizeof(d->title_key));
	if (ret != 0) {
		// Failed to decrypt the title key.
		d->reset();
		return;
	}
#endif /* ENABLE_DECRYPTION */

	// Read the contents table for group 0.
	// TODO: Multiple groups?
	d->contentsTable = tmd->contentsTableV1(0);
	if (d->contentsTable.empty()) {
		// No contents?
		d->reset();
		return;
	}

	// Initialize the contentsReaders vector based on the number of contents.
	d->contentsReaders.resize(d->contentsTable.size());

	// Find and load the FST. (It has the "bootable" flag, and is usually the first content.)
	// NOTE: tmd->bootIndex() byteswaps to host-endian. Swap it back for comparisons.
	IDiscReaderPtr fstReader;
	const uint16_t boot_index = cpu_to_be16(tmd->bootIndex());
	unsigned int i = 0;
	for (const auto &p : d->contentsTable) {
		if (p.index == boot_index) {
			// Found it!
			fstReader = d->openContentFile(i);
			break;
		}
		i++;
	}
	if (!fstReader) {
		// Could not open the FST.
		d->reset();
		return;
	}

	// Need to load the entire FST, which will be memcpy()'d by WiiUFst.
	// TODO: Eliminate a copy.
	off64_t fst_size = fstReader->size();
	if (fst_size <= 0 || fst_size > 1048576U) {
		// FST is empty and/or too big?
		d->reset();
		return;
	}
	unique_ptr<uint8_t[]> fst_buf(new uint8_t[fst_size]);
	size_t size = fstReader->read(fst_buf.get(), fst_size);
	if (size != static_cast<size_t>(fst_size)) {
		// Read error.
		d->reset();
		return;
	}

	// Create the WiiUFst.
	WiiUFst *const fst = new WiiUFst(fst_buf.get(), static_cast<uint32_t>(fst_size));
	if (!fst->isOpen()) {
		// FST is invalid?
		printf("FST PARSE ERR\n");
		d->reset();
		return;
	}

	// FST loaded.
	printf("FST OK\n");
}

/**
 * Is a ROM image supported by this class?
 * @param info DetectInfo containing ROM detection information.
 * @return Class-specific system ID (>= 0) if supported; -1 if not.
 */
int WiiUPackage::isRomSupported_static(const DetectInfo *info)
{
	// Files are not supported.
	RP_UNUSED(info);
	return -1;
}

/**
 * Is a directory supported by this class?
 * @param path Directory to check.
 * @return Class-specific system ID (>= 0) if supported; -1 if not.
 */
int WiiUPackage::isDirSupported_static(const char *path)
{
	assert(path != nullptr);
	assert(path[0] != '\0');
	if (!path || path[0] == '\0') {
		// No path specified.
		return -1;
	}

	string s_path(path);
	s_path += DIR_SEP_CHR;

	/// Check for the ticket, TMD, and certificate chain files.

	s_path += "title.tik";
	if (FileSystem::access(s_path.c_str(), R_OK) != 0) {
		// No ticket.
		return -1;
	}

	s_path.resize(s_path.size()-4);
	s_path += ".tmd";
	if (FileSystem::access(s_path.c_str(), R_OK) != 0) {
		// No TMD.
		return -1;
	}

	s_path.resize(s_path.size()-4);
	s_path += ".cert";
	if (FileSystem::access(s_path.c_str(), R_OK) != 0) {
		// No certificate chain.
		return -1;
	}

	// This appears to be a Wii U NUS package.
	return 0;
}

/**
 * Get the name of the system the loaded ROM is designed for.
 * @param type System name type. (See the SystemName enum.)
 * @return System name, or nullptr if type is invalid.
 */
const char *WiiUPackage::systemName(unsigned int type) const
{
	RP_D(const WiiUPackage);
	if (!d->isValid || !isSystemNameTypeValid(type))
		return nullptr;

	// WiiUPackage has the same name worldwide, so we can
	// ignore the region selection.
	static_assert(SYSNAME_TYPE_MASK == 3,
		"WiiUPackage::systemName() array index optimization needs to be updated.");

	// Bits 0-1: Type. (long, short, abbreviation)
	static const char *const sysNames[4] = {
		"Nintendo Wii U", "Wii U", "Wii U", nullptr
	};

	return sysNames[type & SYSNAME_TYPE_MASK];
}

/**
 * Load field data.
 * Called by RomData::fields() if the field data hasn't been loaded yet.
 * @return Number of fields read on success; negative POSIX error code on error.
 */
int WiiUPackage::loadFieldData(void)
{
	RP_D(WiiUPackage);
	if (!d->fields.empty()) {
		// Field data *has* been loaded...
		return 0;
	} else if (!d->path) {
		// No directory...
		return -EBADF;
	} else if (!d->isValid) {
		// Unknown ROM image type.
		return -EIO;
	}

	// TODO

	// Finished reading the field data.
	return static_cast<int>(d->fields.count());
}

}
