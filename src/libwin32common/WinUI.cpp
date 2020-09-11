/***************************************************************************
 * ROM Properties Page shell extension. (libwin32common)                   *
 * WinUI.hpp: Windows UI common functions.                                 *
 *                                                                         *
 * Copyright (c) 2016-2019 by David Korth.                                 *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

#include "config.libwin32common.h"
#include "WinUI.hpp"
#include "AutoGetDC.hpp"
#include "MiniU82T.hpp"

#include <commctrl.h>
#include <commdlg.h>
#include <shlwapi.h>

// C++ includes.
#include <algorithm>
#include <memory>
#include <string>
#include <unordered_set>
using std::string;
using std::unique_ptr;
using std::unordered_set;
using std::tstring;
using std::vector;

namespace LibWin32Common {

/**
 * Convert UNIX line endings to DOS line endings.
 * @param tstr_unix	[in] String with UNIX line endings.
 * @param lf_count	[out,opt] Number of LF characters found.
 * @return tstring with DOS line endings.
 */
tstring unix2dos(const TCHAR *tstr_unix, int *lf_count)
{
	// TODO: Optimize this!
	tstring tstr_dos;
	tstr_dos.reserve(_tcslen(tstr_unix) + 16);
	int lf = 0;
	for (; *tstr_unix != _T('\0'); tstr_unix++) {
		if (*tstr_unix == _T('\n')) {
			tstr_dos += _T("\r\n");
			lf++;
		} else {
			tstr_dos += *tstr_unix;
		}
	}
	if (lf_count) {
		*lf_count = lf;
	}
	return tstr_dos;
}

/**
 * Measure text size using GDI.
 * @param hWnd		[in] hWnd.
 * @param hFont		[in] Font.
 * @param tstr		[in] String.
 * @param lpSize	[out] Size.
 * @return 0 on success; non-zero on error.
 */
int measureTextSize(HWND hWnd, HFONT hFont, const TCHAR *tstr, LPSIZE lpSize)
{
	assert(hWnd != nullptr);
	assert(hFont != nullptr);
	assert(tstr != nullptr);
	assert(lpSize != nullptr);
	if (!hWnd || !hFont || !tstr || !lpSize)
		return -EINVAL;

	SIZE size_total = {0, 0};
	AutoGetDC hDC(hWnd, hFont);

	// Find the next newline.
	do {
		const TCHAR *p_nl = _tcschr(tstr, L'\n');
		int len;
		if (p_nl) {
			// Found a newline.
			len = static_cast<int>(p_nl - tstr);
		} else {
			// No newline. Process the rest of the string.
			len = static_cast<int>(_tcslen(tstr));
		}
		assert(len >= 0);
		if (len < 0) {
			len = 0;
		}

		// Check if a '\r' is present before the '\n'.
		if (len > 0 && p_nl != nullptr && *(p_nl-1) == L'\r') {
			// Ignore the '\r'.
			len--;
		}

		// Measure the text size.
		SIZE size_cur;
		BOOL bRet = GetTextExtentPoint32(hDC, tstr, len, &size_cur);
		if (!bRet) {
			// Something failed...
			return -1;
		}

		if (size_cur.cx > size_total.cx) {
			size_total.cx = size_cur.cx;
		}
		size_total.cy += size_cur.cy;

		// Next newline.
		if (!p_nl)
			break;
		tstr = p_nl + 1;
	} while (*tstr != 0);

	*lpSize = size_total;
	return 0;
}

/**
 * Measure text size using GDI.
 * This version removes HTML-style tags before
 * calling the regular measureTextSize() function.
 * @param hWnd		[in] hWnd.
 * @param hFont		[in] Font.
 * @param tstr		[in] String.
 * @param lpSize	[out] Size.
 * @return 0 on success; non-zero on error.
 */
int measureTextSizeLink(HWND hWnd, HFONT hFont, const TCHAR *tstr, LPSIZE lpSize)
{
	assert(tstr != nullptr);
	if (!tstr)
		return -EINVAL;

	// Remove HTML-style tags.
	// NOTE: This is a very simplistic version.
	size_t len = _tcslen(tstr);
	unique_ptr<TCHAR[]> ntstr(new TCHAR[len+1]);
	TCHAR *p = ntstr.get();

	int lbrackets = 0;
	for (; *tstr != 0; tstr++) {
		if (*tstr == _T('<')) {
			// Starting bracket.
			lbrackets++;
			continue;
		} else if (*tstr == _T('>')) {
			// Ending bracket.
			assert(lbrackets > 0);
			lbrackets--;
			continue;
		}

		if (lbrackets == 0) {
			// Not currently in a tag.
			*p++ = *tstr;
		}
	}

	*p = 0;
	return measureTextSize(hWnd, hFont, ntstr.get(), lpSize);
}

/**
 * Get the alternate row color for ListViews.
 *
 * This function should be called on ListView creation
 * and if the system theme is changed.
 *
 * @return Alternate row color for ListViews.
 */
COLORREF getAltRowColor(void)
{
	union {
		COLORREF color;
		struct {
			uint8_t r;
			uint8_t g;
			uint8_t b;
			uint8_t a;
		};
	} rgb;
	rgb.color = GetSysColor(COLOR_WINDOW);

	// TODO: Better "convert to grayscale" and brighten/darken algorithms?
	if (((rgb.r + rgb.g + rgb.b) / 3) >= 128) {
		// Subtract 16 from each color component.
		rgb.r -= 16;
		rgb.g -= 16;
		rgb.b -= 16;
	} else {
		// Add 16 to each color component.
		rgb.r += 16;
		rgb.g += 16;
		rgb.b += 16;
	}

	return rgb.color;
}

/**
 * Are we using COMCTL32.DLL v6.10 or later?
 * @return True if it's v6.10 or later; false if not.
 */
bool isComCtl32_v610(void)
{
	// Check the COMCTL32.DLL version.
	HMODULE hComCtl32 = GetModuleHandle(_T("COMCTL32"));
	assert(hComCtl32 != nullptr);

	typedef HRESULT (CALLBACK *PFNDLLGETVERSION)(DLLVERSIONINFO *pdvi);
	PFNDLLGETVERSION pfnDllGetVersion = nullptr;
	if (!hComCtl32)
		return false;

	pfnDllGetVersion = (PFNDLLGETVERSION)GetProcAddress(hComCtl32, "DllGetVersion");
	if (!pfnDllGetVersion)
		return false;

	bool ret = false;
	DLLVERSIONINFO dvi;
	dvi.cbSize = sizeof(dvi);
	HRESULT hr = pfnDllGetVersion(&dvi);
	if (SUCCEEDED(hr)) {
		ret = dvi.dwMajorVersion > 6 ||
			(dvi.dwMajorVersion == 6 && dvi.dwMinorVersion >= 10);
	}
	return ret;
}

/** Window procedure subclasses **/

/**
 * Subclass procedure for multi-line EDIT and RICHEDIT controls.
 * This procedure does the following:
 * - ENTER and ESCAPE are forwarded to the parent window.
 * - DLGC_HASSETSEL is masked.
 *
 * @param hWnd		Control handle.
 * @param uMsg		Message.
 * @param wParam	WPARAM
 * @param lParam	LPARAM
 * @param uIdSubclass	Subclass ID. (usually the control ID)
 * @param dwRefData	HWND of parent dialog to forward WM_COMMAND messages to.
 */
LRESULT CALLBACK MultiLineEditProc(
	HWND hWnd, UINT uMsg,
	WPARAM wParam, LPARAM lParam,
	UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	switch (uMsg) {
		case WM_KEYDOWN: {
			// Work around Enter/Escape issues.
			// Reference: http://blogs.msdn.com/b/oldnewthing/archive/2007/08/20/4470527.aspx
			if (!dwRefData) {
				// No parent dialog...
				break;
			}
			HWND hDlg = reinterpret_cast<HWND>(dwRefData);

			switch (wParam) {
				case VK_RETURN:
					SendMessage(hDlg, WM_COMMAND, IDOK, 0);
					return TRUE;

				case VK_ESCAPE:
					SendMessage(hDlg, WM_COMMAND, IDCANCEL, 0);
					return TRUE;

				default:
					break;
			}
			break;
		}

		case WM_GETDLGCODE: {
			// Filter out DLGC_HASSETSEL.
			// References:
			// - https://stackoverflow.com/questions/20876045/cricheditctrl-selects-all-text-when-it-gets-focus
			// - https://stackoverflow.com/a/20884852
			const LRESULT code = DefSubclassProc(hWnd, uMsg, wParam, lParam);
			return (code & ~(LRESULT)DLGC_HASSETSEL);
		}

		case WM_NCDESTROY:
			// Remove the window subclass.
			// Reference: https://blogs.msdn.microsoft.com/oldnewthing/20031111-00/?p=41883
			RemoveWindowSubclass(hWnd, MultiLineEditProc, uIdSubclass);
			break;

		default:
			break;
	}

	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

/**
 * Subclass procedure for single-line EDIT and RICHEDIT controls.
 * This procedure does the following:
 * - DLGC_HASSETSEL is masked.
 *
 * @param hWnd		Control handle.
 * @param uMsg		Message.
 * @param wParam	WPARAM
 * @param lParam	LPARAM
 * @param uIdSubclass	Subclass ID. (usually the control ID)
 * @param dwRefData	HWND of parent dialog to forward WM_COMMAND messages to.
 */
LRESULT CALLBACK SingleLineEditProc(
	HWND hWnd, UINT uMsg,
	WPARAM wParam, LPARAM lParam,
	UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	((void)dwRefData);

	switch (uMsg) {
		case WM_GETDLGCODE: {
			// Filter out DLGC_HASSETSEL.
			// References:
			// - https://stackoverflow.com/questions/20876045/cricheditctrl-selects-all-text-when-it-gets-focus
			// - https://stackoverflow.com/a/20884852
			const LRESULT code = DefSubclassProc(hWnd, uMsg, wParam, lParam);
			return (code & ~(LRESULT)DLGC_HASSETSEL);
		}

		case WM_NCDESTROY:
			// Remove the window subclass.
			// Reference: https://blogs.msdn.microsoft.com/oldnewthing/20031111-00/?p=41883
			RemoveWindowSubclass(hWnd, MultiLineEditProc, uIdSubclass);
			break;

		default:
			break;
	}

	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

/**
 * Convert an RP file dialog filter to Win32.
 *
 * RP syntax: "Sega Mega Drive ROM images|*.gen;*.bin|All Files|*.*"
 * Essentially the same as Windows, but with '|' instead of '\0'.
 * Also, no terminator sequence is needed.
 * The "(*.bin; *.srl)" part is added to the display name if needed.
 *
 * @param filter RP file dialog filter. (UTF-8, from gettext())
 * @return Win32 file dialog filter.
 */
static tstring rpFileDialogFilterToWin32(const char *filter)
{
	tstring ts_ret;
	assert(filter != nullptr && filter[0] != '\0');
	if (!filter || filter[0] == '\0')
		return ts_ret;

	// TODO: Get rid of the temporary std::string.
	// (U82T_c() overload that takes a size?)
	// (or, take a const TCHAR* filter)
	string tmpstr;

	// Tokenize manually without using strtok_r(),
	// since strtok_r() writes to the string.
	ts_ret.reserve(strlen(filter) + 32);
	const char *pLast = filter;
	do {
		// Separator 1: Between display name and pattern.
		const char *const pSep1 = strchr(pLast, '|');
		assert(pSep1 != nullptr);
		if (!pSep1) {
			// Cannot have the last token here...
			return tstring();
		}

		// Separator 2: Between pattern and next display name.
		const char *const pSep2 = strchr(pSep1 + 1, '|');

		// RP filter: "Sega Mega Drive ROM images|*.gen;*.bin|All Files|*.*"
		// Windows filter: "Sega Mega Drive ROM images (*.gen; *.bin)\0*.gen;*.bin\0All Files (*.*)\0*.*\0\0"

		// Display name.
		tmpstr.assign(pLast, pSep1 - pLast);
		ts_ret += U82T_s(tmpstr);

		// File filter portion of the display name.
		// TODO: Convert ";" to "; ".
		if (pSep2) {
			tmpstr.assign(pSep1 + 1, pSep2 - pSep1 - 1);
			pLast = pSep2 + 1;
		} else {
			tmpstr.assign(pSep1 + 1);
			pLast = nullptr;
		}
		const tstring ts_tmpstr = U82T_s(tmpstr);
		ts_ret += _T(" (");
		ts_ret += ts_tmpstr;
		ts_ret += _T(')');
		ts_ret += _T('\0');

		// File filter.
		ts_ret += ts_tmpstr;
		ts_ret += _T('\0');
	} while (pLast != nullptr && *pLast != '\0');

	ts_ret += _T('\0');
	return ts_ret;
}

/**
 * Get a filename using a File Name dialog.
 *
 * Depending on OS, this may use:
 * - Vista+: IOpenFileDialog / IFileSaveDialog
 * - XP: GetOpenFileName() / GetSaveFileName()
 *
 * @param bSave		[in] True for save; false for open.
 * @param hWnd		[in] Owner.
 * @param dlgTitle	[in] Dialog title.
 * @param filterSpec	[in] Filter specification. (RP format, UTF-8)
 * @param origFilename	[in,opt] Starting filename.
 * @return Filename, or empty string on error.
 */
static tstring getFileName_int(bool bSave, HWND hWnd, const TCHAR *dlgTitle, const char *filterSpec, const TCHAR *origFilename)
{
	assert(dlgTitle != nullptr);
	assert(filterSpec != nullptr);
	tstring ts_ret;

	if (0) {
		// TODO: Implement IFileOpenDialog and IFileSaveDialog.
		// This should support >MAX_PATH on Windows 10 v1607 and later.
		// Reference: https://msdn.microsoft.com/en-us/library/windows/desktop/bb776913%28v=vs.85%29.aspx?f=255&MSPPError=-2147217396
		// Requires the following:
		// - -DWINVER=0x0600
		// - IFileDialogEvents object
	} else {
		// GetOpenFileName() / GetSaveFileName()
		tstring ts_filterSpec = rpFileDialogFilterToWin32(filterSpec);

		TCHAR tfilename[MAX_PATH];
		tfilename[0] = 0;

		OPENFILENAME ofn;
		memset(&ofn, 0, sizeof(ofn));
		ofn.lStructSize = sizeof(ofn);
		ofn.hwndOwner = hWnd;
		ofn.lpstrFilter = ts_filterSpec.c_str();
		ofn.lpstrCustomFilter = nullptr;
		ofn.lpstrFile = tfilename;
		ofn.nMaxFile = _countof(tfilename);
		ofn.lpstrTitle = dlgTitle;

		// Check if the original filename is a directory or a file.
		if (origFilename) {
			DWORD dwAttrs = GetFileAttributes(origFilename);
			if (dwAttrs != INVALID_FILE_ATTRIBUTES && (dwAttrs & FILE_ATTRIBUTE_DIRECTORY)) {
				// It's a directory.
				ofn.lpstrInitialDir = origFilename;
			} else {
				// Not a directory, or invalid.
				// Assume it's a filename.
				ofn.lpstrInitialDir = nullptr;
				_tcscpy_s(tfilename, _countof(tfilename), origFilename);
			}
		}

		// TODO: Make OFN_DONTADDTORECENT customizable?
		BOOL bRet;
		if (!bSave) {
			ofn.Flags = OFN_DONTADDTORECENT | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
			bRet = GetOpenFileName(&ofn);
		} else {
			ofn.Flags = OFN_DONTADDTORECENT | OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;
			bRet = GetSaveFileName(&ofn);
		}

		if (bRet != 0 && tfilename[0] != _T('\0')) {
			ts_ret = tfilename;
		}
	}

	// Return the filename.
	return ts_ret;
}

/**
 * Get a filename using the Open File Name dialog.
 *
 * Depending on OS, this may use:
 * - Vista+: IFileOpenDialog
 * - XP: GetOpenFileName()
 *
 * @param hWnd		[in] Owner.
 * @param dlgTitle	[in] Dialog title.
 * @param filterSpec	[in] Filter specification. (RP format, UTF-8)
 * @param origFilename	[in,opt] Starting filename.
 * @return Filename, or empty string on error.
 */
tstring getOpenFileName(HWND hWnd, const TCHAR *dlgTitle, const char *filterSpec, const TCHAR *origFilename)
{
	return getFileName_int(false, hWnd, dlgTitle, filterSpec, origFilename);
}

/**
 * Get a filename using the Save File Name dialog.
 *
 * Depending on OS, this may use:
 * - Vista+: IFileSaveDialog
 * - XP: GetSaveFileName()
 *
 * @param hWnd		[in] Owner.
 * @param dlgTitle	[in] Dialog title.
 * @param filterSpec	[in] Filter specification. (RP format, UTF-8)
 * @param origFilename	[in,opt] Starting filename.
 * @return Filename, or empty string on error.
 */
tstring getSaveFileName(HWND hWnd, const TCHAR *dlgTitle, const char *filterSpec, const TCHAR *origFilename)
{
	return getFileName_int(true, hWnd, dlgTitle, filterSpec, origFilename);
}

}
