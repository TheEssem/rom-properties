/***************************************************************************
 * ROM Properties Page shell extension. (libromdata)                       *
 * IResourceReader.cpp: Interface for Windows resource readers.            *
 *                                                                         *
 * Copyright (c) 2016-2019 by David Korth.                                 *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

#include "stdafx.h"
#include "IResourceReader.hpp"

// librpbase
using LibRpBase::IRpFile;

namespace LibRomData {

/**
 * DWORD alignment function.
 * @param file	[in] File to DWORD align.
 * @return 0 on success; non-zero on error.
 */
int IResourceReader::alignFileDWORD(IRpFile *file)
{
	int ret = 0;
	int64_t pos = file->tell();
	if (pos % 4 != 0) {
		pos = ALIGN_BYTES(4, pos);
		ret = file->seek(pos);
	}
	return ret;
}

}
