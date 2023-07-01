/***************************************************************************
 * ROM Properties Page shell extension. (KDE)                              *
 * AboutTab.hpp: About tab for rp-config.                                  *
 *                                                                         *
 * Copyright (c) 2016-2023 by David Korth.                                 *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

#include "stdafx.h"
#include "config.librpbase.h"

#include "AboutTab.hpp"
#include "UpdateChecker.hpp"

// Other rom-properties libraries
#include "librpbase/config/AboutTabText.hpp"
using namespace LibRpBase;
using namespace LibRpText;

// C++ STL classes
using std::string;

// Qt includes
#include <QtCore/QThread>

#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)
// KIO version.
// NOTE: Only available as a compile-time constant.
#  include <kio_version.h>
#else
// kdelibs version.
#  include <kdeversion.h>
#endif

// Other libraries.
#ifdef HAVE_ZLIB
#  include <zlib.h>
#endif /* HAVE_ZLIB */
#ifdef HAVE_PNG
#  include <png.h>	// PNG_LIBPNG_VER_STRING
#  include "librpbase/img/RpPng.hpp"
#endif /* HAVE_PNG */
// TODO: JPEG
#if defined(ENABLE_DECRYPTION) && defined(HAVE_NETTLE)
#  include "librpbase/crypto/AesNettle.hpp"
#endif /* ENABLE_DECRYPTION && HAVE_NETTLE */
#ifdef ENABLE_XML
#  include <tinyxml2.h>
#endif /* ENABLE_XML */

#include "ui_AboutTab.h"
class AboutTabPrivate
{
	public:
		explicit AboutTabPrivate(AboutTab *q);
		~AboutTabPrivate();

	private:
		AboutTab *const q_ptr;
		Q_DECLARE_PUBLIC(AboutTab)
		Q_DISABLE_COPY(AboutTabPrivate)

	public:
		Ui::AboutTab ui;

	protected:
		/**
		 * Initialize the program title text.
		 */
		void initProgramTitleText(void);

		/**
		 * Initialize the "Credits" tab.
		 */
		void initCreditsTab(void);

		/**
		 * Initialize the "Libraries" tab.
		 */
		void initLibrariesTab(void);

		/**
		 * Initialize the "Support" tab.
		 */
		void initSupportTab(void);

	public:
		/**
		 * Initialize the dialog.
		 */
		void init(void);

		/**
		 * Check for updates.
		 */
		void checkForUpdates(void);

	public:
		// Update checker object and thread.
		QThread *thrUpdate;
		UpdateChecker *updChecker;

		// Checked for updates yet?
		bool checkedForUpdates;
};

/** AboutTabPrivate **/

// Useful strings.
#define BR	"<br/>\n"
#define B_START	"<b>"
#define B_END	"</b>"
#define INDENT	"&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;"
#define BULLET	"\xE2\x80\xA2"	/* U+2022: BULLET */

AboutTabPrivate::AboutTabPrivate(AboutTab *q)
	: q_ptr(q)
	, thrUpdate(nullptr)
	, updChecker(nullptr)
	, checkedForUpdates(false)
{ }

AboutTabPrivate::~AboutTabPrivate()
{
	if (thrUpdate && thrUpdate->isRunning()) {
		// Make sure the thread is stopped.
		thrUpdate->quit();
		bool ok = thrUpdate->wait(5000);
		if (!ok) {
			// Thread is hung. Terminate it.
			thrUpdate->terminate();
		}
	}

	delete updChecker;
	delete thrUpdate;
}

/**
 * Initialize the program title text.
 */
void AboutTabPrivate::initProgramTitleText(void)
{
	// lblTitle is RichText.

	// Program icon.
	// TODO: Make a custom icon instead of reusing the system icon.
	// TODO: Fallback for older Qt?
#if QT_VERSION >= QT_VERSION_CHECK(4,6,0)
	const QIcon icon = QIcon::fromTheme(QLatin1String("media-flash"));
	if (!icon.isNull()) {
		// Get the 128x128 icon.
		// TODO: Determine the best size.
		ui.lblLogo->setPixmap(icon.pixmap(128, 128));
	} else {
		// No icon...
		ui.lblLogo->hide();
	}
#endif /* QT_VERSION >= QT_VERSION_CHECK(4,6,0) */

	const char *const programVersion =
		AboutTabText::getProgramInfoString(AboutTabText::ProgramInfoStringID::ProgramVersion);
	const char *const gitVersion =
		AboutTabText::getProgramInfoString(AboutTabText::ProgramInfoStringID::GitVersion);

	assert(programVersion != nullptr);

	string sPrgTitle;
	sPrgTitle.reserve(1024);
	// tr: Uses Qt's HTML subset for formatting.
	sPrgTitle += C_("AboutTab", "<b>ROM Properties Page</b><br>Shell Extension");
	sPrgTitle += BR BR;
	sPrgTitle += rp_sprintf(C_("AboutTab", "Version %s"), programVersion);
	if (gitVersion) {
		sPrgTitle += BR;
		sPrgTitle += gitVersion;
		const char *const gitDescription =
			AboutTabText::getProgramInfoString(AboutTabText::ProgramInfoStringID::GitDescription);
		if (gitDescription) {
			sPrgTitle += BR;
			sPrgTitle += gitDescription;
		}
	}

	ui.lblTitle->setText(U82Q(sPrgTitle));
}

/**
 * Initialize the "Credits" tab.
 */
void AboutTabPrivate::initCreditsTab(void)
{
	// License name, with HTML formatting.
	const string sPrgLicense = rp_sprintf("<a href='https://www.gnu.org/licenses/gpl-2.0.html'>%s</a>",
		C_("AboutTabl|Credits", "GNU GPL v2"));

	// lblCredits is RichText.
	string sCredits;
	sCredits.reserve(4096);
	// NOTE: Copyright is NOT localized.
	sCredits += AboutTabText::getProgramInfoString(AboutTabText::ProgramInfoStringID::Copyright);
	sCredits += BR;
	sCredits += rp_sprintf(
		// tr: %s is the name of the license.
		C_("AboutTab|Credits", "This program is licensed under the %s or later."),
			sPrgLicense.c_str());

	AboutTabText::CreditType lastCreditType = AboutTabText::CreditType::Continue;
	for (const AboutTabText::CreditsData_t *creditsData = AboutTabText::getCreditsData();
	     creditsData->type < AboutTabText::CreditType::Max; creditsData++)
	{
		if (creditsData->type != AboutTabText::CreditType::Continue &&
		    creditsData->type != lastCreditType)
		{
			// New credit type.
			sCredits += BR BR B_START;

			switch (creditsData->type) {
				case AboutTabText::CreditType::Developer:
					sCredits += C_("AboutTab|Credits", "Developers:");
					break;
				case AboutTabText::CreditType::Contributor:
					sCredits += C_("AboutTab|Credits", "Contributors:");
					break;
				case AboutTabText::CreditType::Translator:
					sCredits += C_("AboutTab|Credits", "Translators:");
					break;

				case AboutTabText::CreditType::Continue:
				case AboutTabText::CreditType::Max:
				default:
					assert(!"Invalid credit type.");
					break;
			}

			sCredits += B_END;
		}

		// Append the contributor's name.
		sCredits += BR INDENT BULLET " ";
		sCredits += creditsData->name;
		if (creditsData->url) {
			sCredits += " &lt;<a href='";
			sCredits += creditsData->url;
			sCredits += "'>";
			if (creditsData->linkText) {
				sCredits += creditsData->linkText;
			} else {
				sCredits += creditsData->url;
			}
			sCredits += "</a>&gt;";
		}
		if (creditsData->sub) {
			// tr: Sub-credit.
			sCredits += rp_sprintf(C_("AboutTab|Credits", " (%s)"),
				creditsData->sub);
		}

		lastCreditType = creditsData->type;
	}

	// We're done building the string.
	ui.lblCredits->setText(U82Q(sCredits));
}

/**
 * Initialize the "Libraries" tab.
 */
void AboutTabPrivate::initLibrariesTab(void)
{
	// lblLibraries is RichText.
	char sVerBuf[64];

	// NOTE: These strings can NOT be static.
	// Otherwise, they won't be retranslated if the UI language
	// is changed at runtime.

	// tr: Using an internal copy of a library.
	const char *const sIntCopyOf = C_("AboutTab|Libraries", "Internal copy of %s.");
	// tr: Compiled with a specific version of an external library.
	const char *const sCompiledWith = C_("AboutTab|Libraries", "Compiled with %s.");
	// tr: Using an external library, e.g. libpcre.so
	const char *const sUsingDll = C_("AboutTab|Libraries", "Using %s.");
	// tr: License: (libraries with only a single license)
	const char *const sLicense = C_("AboutTab|Libraries", "License: %s");
	// tr: Licenses: (libraries with multiple licenses)
	const char *const sLicenses = C_("AboutTab|Libraries", "Licenses: %s");

	// Suppress "unused variable" warnings.
	// sIntCopyOf isn't used if no internal copies of libraries are needed.
	RP_UNUSED(sIntCopyOf);
	RP_UNUSED(sCompiledWith);
	RP_UNUSED(sUsingDll);

	// Included libraries string.
	string sLibraries;
	sLibraries.reserve(8192);

	/** Qt **/
	string qtVersion = "Qt ";
	qtVersion += qVersion();
#ifdef QT_IS_STATIC
	sLibraries += rp_sprintf(sIntCopyOf, qtVersion.c_str());
#else
	sLibraries += rp_sprintf(sCompiledWith, "Qt " QT_VERSION_STR);
	sLibraries += BR;
	sLibraries += rp_sprintf(sUsingDll, qtVersion.c_str());
#endif /* QT_IS_STATIC */
	sLibraries += BR
		"Copyright (C) 1995-2022 The Qt Company Ltd. and/or its subsidiaries." BR
		"<a href='https://www.qt.io/'>https://www.qt.io/</a>" BR;
	// TODO: Check QT_VERSION at runtime?
#if QT_VERSION >= QT_VERSION_CHECK(4,5,0)
	sLibraries += rp_sprintf(sLicenses, "GNU LGPL v2.1+, GNU GPL v2+");
#else
	sLibraries += rp_sprintf(sLicense, "GNU GPL v2+");
#endif /* QT_VERSION */

	/** KDE **/
	sLibraries += BR BR;
#if QT_VERSION >= QT_VERSION_CHECK(5,0,0)
	// NOTE: Can't obtain the runtime version for KF5 easily...
	sLibraries += rp_sprintf(sCompiledWith, "KDE Frameworks " KIO_VERSION_STRING);
	sLibraries += BR
		"Copyright (C) 1996-2022 KDE contributors." BR
		"<a href='https://www.kde.org/'>https://www.kde.org/</a>" BR;
	sLibraries += rp_sprintf(sLicense, "GNU LGPL v2.1+");
#else /* QT_VERSION < QT_VERSION_CHECK(5,0,0) */
	string kdeVersion = "KDE Libraries ";
	kdeVersion += KDE::versionString();
	sLibraries += rp_sprintf(sCompiledWith, "KDE Libraries " KDE_VERSION_STRING);
	sLibraries += BR;
	sLibraries += rp_sprintf(sUsingDll, kdeVersion.c_str());
	sLibraries += BR
		"Copyright (C) 1996-2017 KDE contributors." BR;
	sLibraries += rp_sprintf(sLicense, "GNU LGPL v2.1+");
#endif /* QT_VERSION >= QT_VERSION_CHECK(5,0,0) */

	/** zlib **/
#ifdef HAVE_ZLIB
	sLibraries += BR BR;
	const bool zlib_is_ng = RpPng::zlib_is_ng();
	string sZlibVersion = (zlib_is_ng ? "zlib-ng " : "zlib ");
	sZlibVersion += RpPng::zlib_version_string();

#if defined(USE_INTERNAL_ZLIB) && !defined(USE_INTERNAL_ZLIB_DLL)
	sLibraries += rp_sprintf(sIntCopyOf, sZlibVersion.c_str());
#else
#  ifdef ZLIBNG_VERSION
	sLibraries += rp_sprintf(sCompiledWith, "zlib-ng " ZLIBNG_VERSION);
#  else /* !ZLIBNG_VERSION */
	sLibraries += rp_sprintf(sCompiledWith, "zlib " ZLIB_VERSION);
#  endif /* ZLIBNG_VERSION */
	sLibraries += BR;
	sLibraries += rp_sprintf(sUsingDll, sZlibVersion.c_str());
#endif
	sLibraries += BR
		"Copyright (C) 1995-2022 Jean-loup Gailly and Mark Adler." BR
		"<a href='https://zlib.net/'>https://zlib.net/</a>" BR;
	if (zlib_is_ng) {
		sLibraries += "<a href='https://github.com/zlib-ng/zlib-ng'>https://github.com/zlib-ng/zlib-ng</a>" BR;
	}
	sLibraries += rp_sprintf(sLicense, "zlib license");
#endif /* HAVE_ZLIB */

	/** libpng **/
#ifdef HAVE_PNG
	const bool APNG_is_supported = RpPng::libpng_has_APNG();
	const uint32_t png_version_number = RpPng::libpng_version_number();
	char pngVersion[48];
	snprintf(pngVersion, sizeof(pngVersion), "libpng %u.%u.%u%s",
		png_version_number / 10000,
		(png_version_number / 100) % 100,
		png_version_number % 100,
		(APNG_is_supported ? " + APNG" : " (No APNG support)"));

	sLibraries += BR BR;
#if defined(USE_INTERNAL_PNG) && !defined(USE_INTERNAL_ZLIB_DLL)
	sLibraries += rp_sprintf(sIntCopyOf, pngVersion);
#else
	// NOTE: Gentoo's libpng has "+apng" at the end of
	// PNG_LIBPNG_VER_STRING if APNG is enabled.
	// We have our own "+ APNG", so remove Gentoo's.
	string pngVersionCompiled = "libpng " PNG_LIBPNG_VER_STRING;
	for (size_t i = pngVersionCompiled.size()-1; i > 6; i--) {
		const char chr = pngVersionCompiled[i];
		if (ISDIGIT(chr))
			break;
		pngVersionCompiled.resize(i);
	}

	string fullPngVersionCompiled;
	if (APNG_is_supported) {
		// PNG version, with APNG support.
		fullPngVersionCompiled = rp_sprintf("%s + APNG", pngVersionCompiled.c_str());
	} else {
		// PNG version, without APNG support.
		fullPngVersionCompiled = rp_sprintf("%s (No APNG support)", pngVersionCompiled.c_str());
	}

	sLibraries += rp_sprintf(sCompiledWith, fullPngVersionCompiled.c_str());
	sLibraries += BR;
	sLibraries += rp_sprintf(sUsingDll, pngVersion);
#endif

	// Convert newlines to "<br/>\n".
	const char *const s_png_tmp = RpPng::libpng_copyright_string();
	for (const char *p = s_png_tmp; *p != '\0'; p++) {
		const char chr = *p;
		if (unlikely(chr == '\n')) {
			sLibraries += BR;
		} else {
			sLibraries += chr;
		}
	}

	sLibraries += "<a href='http://www.libpng.org/pub/png/libpng.html'>http://www.libpng.org/pub/png/libpng.html</a>" BR
		"<a href='https://github.com/glennrp/libpng'>https://github.com/glennrp/libpng</a>" BR;
	if (APNG_is_supported) {
		sLibraries += C_("AboutTab|Libraries", "APNG patch:");
		sLibraries += " <a href='https://sourceforge.net/projects/libpng-apng/'>https://sourceforge.net/projects/libpng-apng/</a>" BR;
	}
	sLibraries += rp_sprintf(sLicense, "libpng license");
#endif /* HAVE_PNG */

	/** nettle **/
#if defined(ENABLE_DECRYPTION) && defined(HAVE_NETTLE)
	sLibraries += BR BR;
	int nettle_major, nettle_minor;
	int ret = AesNettle::get_nettle_compile_time_version(&nettle_major, &nettle_minor);
	if (ret == 0) {
		if (nettle_major >= 3) {
			snprintf(sVerBuf, sizeof(sVerBuf), "GNU Nettle %d.%d",
				nettle_major, nettle_minor);
			sLibraries += rp_sprintf(sCompiledWith, sVerBuf);
		} else {
			sLibraries += rp_sprintf(sCompiledWith, "GNU Nettle 2.x");
		}
		sLibraries += BR;
	}

	ret = AesNettle::get_nettle_runtime_version(&nettle_major, &nettle_minor);
	if (ret == 0) {
		snprintf(sVerBuf, sizeof(sVerBuf), "GNU Nettle %d.%d",
			nettle_major, nettle_minor);
		sLibraries += rp_sprintf(sUsingDll, sVerBuf);
		sLibraries += BR;
	}

	if (nettle_major >= 3) {
		if (nettle_minor >= 1) {
			sLibraries += "Copyright (C) 2001-2022 Niels Möller." BR
				"<a href='https://www.lysator.liu.se/~nisse/nettle/'>https://www.lysator.liu.se/~nisse/nettle/</a>" BR;
		} else {
			sLibraries += "Copyright (C) 2001-2014 Niels Möller." BR
				"<a href='https://www.lysator.liu.se/~nisse/nettle/'>https://www.lysator.liu.se/~nisse/nettle/</a>" BR;
		}
		sLibraries += rp_sprintf(sLicenses, "GNU LGPL v3+, GNU GPL v2+");
	} else {
		sLibraries +=
			"Copyright (C) 2001-2013 Niels Möller." BR
			"<a href='https://www.lysator.liu.se/~nisse/nettle/'>https://www.lysator.liu.se/~nisse/nettle/</a>" BR;
		sLibraries += rp_sprintf(sLicense, "GNU LGPL v2.1+");
	}
#endif /* ENABLE_DECRYPTION && HAVE_NETTLE */

	/** TinyXML2 **/
#ifdef ENABLE_XML
	sLibraries += BR BR;
	snprintf(sVerBuf, sizeof(sVerBuf), "TinyXML2 %u.%u.%u",
		static_cast<unsigned int>(TIXML2_MAJOR_VERSION),
		static_cast<unsigned int>(TIXML2_MINOR_VERSION),
		static_cast<unsigned int>(TIXML2_PATCH_VERSION));

#  if defined(USE_INTERNAL_XML) && !defined(USE_INTERNAL_XML_DLL)
	sLibraries += rp_sprintf(sIntCopyOf, sVerBuf);
#  else
	// FIXME: Runtime version?
	sLibraries += rp_sprintf(sCompiledWith, sVerBuf);
#  endif
	sLibraries += BR
		"Copyright (C) 2000-2021 Lee Thomason" BR
		"<a href='http://www.grinninglizard.com/'>http://www.grinninglizard.com/</a>" BR;
	sLibraries += rp_sprintf(sLicense, "zlib license");
#endif /* ENABLE_XML */

	/** GNU gettext **/
	// NOTE: glibc's libintl.h doesn't have the version information,
	// so we're only printing this if we're using GNU gettext's version.
#if defined(HAVE_GETTEXT) && defined(LIBINTL_VERSION)
	if (LIBINTL_VERSION & 0xFF) {
		snprintf(sVerBuf, sizeof(sVerBuf), "GNU gettext %u.%u.%u",
			static_cast<unsigned int>( LIBINTL_VERSION >> 16),
			static_cast<unsigned int>((LIBINTL_VERSION >>  8) & 0xFF),
			static_cast<unsigned int>( LIBINTL_VERSION        & 0xFF));
	} else {
		snprintf(sVerBuf, sizeof(sVerBuf), "GNU gettext %u.%u",
			static_cast<unsigned int>( LIBINTL_VERSION >> 16),
			static_cast<unsigned int>((LIBINTL_VERSION >>  8) & 0xFF));
	}
#  ifdef _WIN32
	sLibraries += rp_sprintf(sIntCopyOf, sVerBuf);
#  else /* _WIN32 */
	// FIXME: Runtime version?
	sLibraries += rp_sprintf(sCompiledWith, sVerBuf);
#  endif /* _WIN32 */
	sLibraries += BR
		"Copyright (C) 1995-1997, 2000-2016, 2018-2020 Free Software Foundation, Inc." BR
		"<a href='https://www.gnu.org/software/gettext/'>https://www.gnu.org/software/gettext/</a>" BR;
	sLibraries += rp_sprintf(sLicense, "GNU LGPL v2.1+");
#endif /* HAVE_GETTEXT && LIBINTL_VERSION */

	// We're done building the string.
	ui.lblLibraries->setText(U82Q(sLibraries));
}

/**
 * Initialize the "Support" tab.
 */
void AboutTabPrivate::initSupportTab(void)
{
	// lblSupport is RichText.
	string sSupport;
	sSupport.reserve(4096);
	sSupport = C_("AboutTab|Support",
		"For technical support, you can visit the following websites:");
	sSupport += BR;

	for (const AboutTabText::SupportSite_t *supportSite = AboutTabText::getSupportSites();
	     supportSite->name != nullptr; supportSite++)
	{
		sSupport += INDENT BULLET " ";
		sSupport += supportSite->name;
		sSupport += " &lt;<a href='";
		sSupport += supportSite->url;
		sSupport += "'>";
		sSupport += supportSite->url;
		sSupport += "</a>&gt;" BR;
	}

	// Email the author.
	sSupport += BR;
	sSupport += C_("AboutTab|Support", "You can also email the developer directly:");
	sSupport += BR INDENT BULLET " "
		"David Korth "
		"&lt;<a href=\"mailto:gerbilsoft@gerbilsoft.com\">"
		"gerbilsoft@gerbilsoft.com</a>&gt;";

	// We're done building the string.
	ui.lblSupport->setText(U82Q(sSupport));
}

/**
 * Initialize the dialog.
 */
void AboutTabPrivate::init(void)
{
	initProgramTitleText();
	initCreditsTab();
	initLibrariesTab();
	initSupportTab();
}

/**
 * Check for updates.
 */
void AboutTabPrivate::checkForUpdates(void)
{
	// Create the QThread and UpdateChecker if necessary.
	Q_Q(AboutTab);
	if (!thrUpdate) {
		thrUpdate = new QThread(q);
		thrUpdate->setObjectName(QLatin1String("thrUpdate"));
	}
	if (!updChecker) {
		updChecker = new UpdateChecker(nullptr);
		updChecker->setObjectName(QLatin1String("updChecker"));
		updChecker->moveToThread(thrUpdate);

		// Status slots
		QObject::connect(updChecker, SIGNAL(error(QString)),
				 q, SLOT(updChecker_error(QString)));
		QObject::connect(updChecker, SIGNAL(retrieved(quint64)),
				 q, SLOT(updChecker_retrieved(quint64)));

		// Thread signals
		QObject::connect(thrUpdate, SIGNAL(started()),
				 updChecker, SLOT(run()));
		QObject::connect(updChecker, SIGNAL(finished()),
				 thrUpdate, SLOT(quit()));
	}

	// Run the update check thread.
	ui.lblUpdateCheck->setText(U82Q(C_("AboutTab", "Checking for updates...")));
	thrUpdate->start();
}

/** AboutTab **/

AboutTab::AboutTab(QWidget *parent)
	: super(parent)
	, d_ptr(new AboutTabPrivate(this))
{
	Q_D(AboutTab);
	d->ui.setupUi(this);

	// Initialize the dialog.
	d->init();
}

AboutTab::~AboutTab()
{
	delete d_ptr;
}

/**
 * Widget state has changed.
 * @param event State change event
 */
void AboutTab::changeEvent(QEvent *event)
{
	if (event->type() == QEvent::LanguageChange) {
		// Retranslate the UI.
		Q_D(AboutTab);
		d->ui.retranslateUi(this);

		// Reinitialize the dialog.
		d->init();
	}

	// Pass the event to the base class.
	super::changeEvent(event);
}

/**
 * Widget is now visible.
 * @param event Show event
 */
void AboutTab::showEvent(QShowEvent *event)
{
	Q_UNUSED(event)

	Q_D(AboutTab);
	if (!d->checkedForUpdates) {
		d->checkedForUpdates = true;
		d->checkForUpdates();
	}
}

/** UpdateChecker slots **/

/**
 * An error occurred while trying to retrieve the update version.
 * TODO: Error code?
 * @param error Error message
 */
void AboutTab::updChecker_error(const QString &error)
{
	Q_D(AboutTab);

	// tr: Error message template. (Qt version, with formatting)
	const QString errTemplate = U82Q(C_("ConfigDialog", "<b>ERROR:</b> %1"));
	d->ui.lblUpdateCheck->setText(errTemplate.arg(error));
}

/**
 * Update version retrieved.
 * @param updateVersion Update version (64-bit format)
 */
void AboutTab::updChecker_retrieved(quint64 updateVersion)
{
	Q_D(AboutTab);

	// Our version. (ignoring the development flag)
	const uint64_t ourVersion = RP_PROGRAM_VERSION_NO_DEVEL(AboutTabText::getProgramVersion());

	// Format the latest version string.
	char sUpdVersion[32];
	const unsigned int upd[3] = {
		RP_PROGRAM_VERSION_MAJOR(updateVersion),
		RP_PROGRAM_VERSION_MINOR(updateVersion),
		RP_PROGRAM_VERSION_REVISION(updateVersion)
	};

	if (upd[2] == 0) {
		snprintf(sUpdVersion, sizeof(sUpdVersion), "%u.%u", upd[0], upd[1]);
	} else {
		snprintf(sUpdVersion, sizeof(sUpdVersion), "%u.%u.%u", upd[0], upd[1], upd[2]);
	}

	string sVersionLabel;
	sVersionLabel.reserve(512);

	sVersionLabel = rp_sprintf(C_("AboutTab", "Latest version: %s"), sUpdVersion);
	if (updateVersion > ourVersion) {
		sVersionLabel += BR BR;
		sVersionLabel += C_("AboutTab", "<b>New version available!</b>");
		sVersionLabel += BR;
		sVersionLabel += "<a href='https://github.com/GerbilSoft/rom-properties/releases'>";
		sVersionLabel += C_("AboutTab", "Download at GitHub");
		sVersionLabel += "</a>";
	}

	d->ui.lblUpdateCheck->setText(U82Q(sVersionLabel));
}
