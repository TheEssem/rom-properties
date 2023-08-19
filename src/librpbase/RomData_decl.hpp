/***************************************************************************
 * ROM Properties Page shell extension. (librpbase)                        *
 * RomData_decl.hpp: ROM data base class. (Subclass macros)                *
 *                                                                         *
 * Copyright (c) 2016-2023 by David Korth.                                 *
 * Copyright (c) 2016-2018 by Egor.                                        *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

#pragma once

#ifndef __ROMPROPERTIES_LIBRPBASE_ROMDATA_HPP__
#  error RomData_decl.hpp should only be included by RomData.hpp
#endif

namespace LibRpBase {
	struct RomDataInfo;
}

// for loadInternalImage() implementation macros
#include <cerrno>

// C++ includes
#include <memory>

/** Macros for RomData subclasses. **/

/**
 * Initial declaration for a RomData subclass.
 * Declares functions common to all RomData subclasses.
 * @param klass Class name
 */
#define ROMDATA_DECL_BEGIN(klass) \
class klass##Private; \
class klass final : public LibRpBase::RomData { \
public: \
	explicit klass(const std::shared_ptr<LibRpFile::IRpFile> &file); \
protected: \
	RP_LIBROMDATA_LOCAL \
	~klass() final = default; \
private: \
	typedef RomData super; \
	friend class klass##Private; \
	RP_DISABLE_COPY(klass); \
\
public: \
	/** \
	 * Is a ROM image supported by this class? \
	 * @param info DetectInfo containing ROM detection information. \
	 * @return Class-specific system ID (>= 0) if supported; -1 if not. \
	 */ \
	static int isRomSupported_static(const DetectInfo *info); \
\
	/** \
	 * Is a ROM image supported by this object? \
	 * @param info DetectInfo containing ROM detection information. \
	 * @return Class-specific system ID (>= 0) if supported; -1 if not. \
	 */ \
	int isRomSupported(const DetectInfo *info) const final; \
\
	/** \
	 * Get the name of the system the loaded ROM is designed for. \
	 * @param type System name type. (See the SystemName enum.) \
	 * @return System name, or nullptr if type is invalid. \
	 */ \
	const char *systemName(unsigned int type) const final; \
\
	/** \
	 * Get the static RomDataInfo for this class. \
	 * @return Static RomDataInfo \
	 */ \
	static const LibRpBase::RomDataInfo *romDataInfo(void); \
\
protected: \
	/** \
	 * Load field data. \
	 * Called by RomData::fields() if the field data hasn't been loaded yet. \
	 * @return 0 on success; negative POSIX error code on error. \
	 */ \
	RP_LIBROMDATA_LOCAL \
	int loadFieldData(void) final;

/**
 * RomData subclass function declaration for loading metadata properties.
 */
#define ROMDATA_DECL_METADATA() \
protected: \
	/** \
	 * Load metadata properties. \
	 * Called by RomData::metaData() if the field data hasn't been loaded yet. \
	 * @return Number of metadata properties read on success; negative POSIX error code on error. \
	 */ \
	RP_LIBROMDATA_LOCAL \
	int loadMetaData(void) final;

/**
 * RomData subclass function declarations for image handling.
 */
#define ROMDATA_DECL_IMGSUPPORT() \
public: \
	/** \
	 * Get a bitfield of image types this class can retrieve. \
	 * @return Bitfield of supported image types. (ImageTypesBF) \
	 */ \
	static uint32_t supportedImageTypes_static(void); \
\
	/** \
	 * Get a bitfield of image types this object can retrieve. \
	 * @return Bitfield of supported image types. (ImageTypesBF) \
	 */ \
	uint32_t supportedImageTypes(void) const final; \
\
	/** \
	 * Get a list of all available image sizes for the specified image type. \
	 * \
	 * The first item in the returned vector is the "default" size. \
	 * If the width/height is 0, then an image exists, but the size is unknown. \
	 * \
	 * @param imageType Image type. \
	 * @return Vector of available image sizes, or empty vector if no images are available. \
	 */ \
	static std::vector<RomData::ImageSizeDef> supportedImageSizes_static(ImageType imageType); \
\
	/** \
	 * Get a list of all available image sizes for the specified image type. \
	 * \
	 * The first item in the returned vector is the "default" size. \
	 * If the width/height is 0, then an image exists, but the size is unknown. \
	 * \
	 * @param imageType Image type. \
	 * @return Vector of available image sizes, or empty vector if no images are available. \
	 */ \
	std::vector<RomData::ImageSizeDef> supportedImageSizes(ImageType imageType) const final;

/**
 * RomData subclass function declaration for image processing flags.
 */
#define ROMDATA_DECL_IMGPF() \
public: \
	/** \
	 * Get image processing flags. \
	 * \
	 * These specify post-processing operations for images, \
	 * e.g. applying transparency masks. \
	 * \
	 * @param imageType Image type. \
	 * @return Bitfield of ImageProcessingBF operations to perform. \
	 */ \
	uint32_t imgpf(ImageType imageType) const final;

/**
 * RomData subclass function declaration for loading internal images.
 *
 * NOTE: This function needs to be public because it might be
 * called by RomData subclasses that own other RomData subclasses.
 *
 */
#define ROMDATA_DECL_IMGINT() \
public: \
	/** \
	 * Load an internal image. \
	 * Called by RomData::image(). \
	 * @param imageType	[in] Image type to load. \
	 * @param pImage	[out] Reference to shared_ptr<const rp_image> to store the image in. \
	 * @return 0 on success; negative POSIX error code on error. \
	 */ \
	int loadInternalImage(ImageType imageType, std::shared_ptr<const LibRpTexture::rp_image> &pImage) final; \

/**
 * RomData subclass function declaration for obtaining URLs for external images.
 */
#define ROMDATA_DECL_IMGEXT() \
public: \
	/** \
	 * Get a list of URLs for an external image type. \
	 * \
	 * A thumbnail size may be requested from the shell. \
	 * If the subclass supports multiple sizes, it should \
	 * try to get the size that most closely matches the \
	 * requested size. \
	 * \
	 * @param imageType     [in]     Image type. \
	 * @param pExtURLs      [out]    Output vector. \
	 * @param size          [in,opt] Requested image size. This may be a requested \
	 *                               thumbnail size in pixels, or an ImageSizeType \
	 *                               enum value. \
	 * @return 0 on success; negative POSIX error code on error. \
	 */ \
	int extURLs(ImageType imageType, std::vector<ExtURL> *pExtURLs, int size = IMAGE_SIZE_DEFAULT) const final;

/**
 * RomData subclass function declaration for loading the animated icon.
 */
#define ROMDATA_DECL_ICONANIM() \
public: \
	/** \
	 * Get the animated icon data. \
	 * \
	 * Check imgpf for IMGPF_ICON_ANIMATED first to see if this \
	 * object has an animated icon. \
	 * \
	 * The retrieved IconAnimData must be ref()'d by the caller if the \
	 * caller stores it instead of using it immediately. \
	 * \
	 * @return Animated icon data, or nullptr if no animated icon is present. \
	 */ \
	const LibRpBase::IconAnimData *iconAnimData(void) const final;

/**
 * RomData subclass function declaration for indicating "dangerous" permissions.
 */
#define ROMDATA_DECL_DANGEROUS() \
public: \
	/** \
	 * Does this ROM image have "dangerous" permissions? \
	 * \
	 * @return True if the ROM image has "dangerous" permissions; false if not. \
	 */ \
	bool hasDangerousPermissions(void) const final;

/**
 * RomData subclass function declaration for indicating ROM operations are possible.
 */
#define ROMDATA_DECL_ROMOPS() \
protected: \
	/** \
	 * Get the list of operations that can be performed on this ROM. \
	 * Internal function; called by RomData::romOps(). \
	 * @return List of operations. \
	 */ \
	RP_LIBROMDATA_LOCAL \
	std::vector<RomOp> romOps_int(void) const final; \
\
	/** \
	 * Perform a ROM operation. \
	 * Internal function; called by RomData::doRomOp(). \
	 * @param id		[in] Operation index. \
	 * @param pParams	[in/out] Parameters and results. (for e.g. UI updates) \
	 * @return 0 on success; negative POSIX error code on error. \
	 */ \
	RP_LIBROMDATA_LOCAL \
	int doRomOp_int(int id, RomOpParams *pParams) final;

/**
 * RomData subclass function declaration for "viewed" achievements.
 */
#define ROMDATA_DECL_VIEWED_ACHIEVEMENTS() \
public: \
	/** \
	 * Check for "viewed" achievements. \
	 * \
	 * @return Number of achievements unlocked. \
	 */ \
	int checkViewedAchievements(void) const final;

/**
 * RomData subclass function declaration for closing the internal file handle.
 * Only needed if extra handling is needed, e.g. if multiple files are opened.
 */
#define ROMDATA_DECL_CLOSE() \
public: \
	/** \
	 * Close the opened file. \
	 */ \
	void close(void) final;

/**
 * End of RomData subclass declaration.
 */
#define ROMDATA_DECL_END() };

/** RomData subclass function implementations for static function wrappers. **/

/**
 * Common static function wrappers.
 */
#define ROMDATA_IMPL(klass) \
/** \
 * Is a ROM image supported by this object? \
 * @param info DetectInfo containing ROM detection information. \
 * @return Class-specific system ID (>= 0) if supported; -1 if not. \
 */ \
int klass::isRomSupported(const DetectInfo *info) const \
{ \
	return klass::isRomSupported_static(info); \
} \
\
/** \
 * Get the static RomDataInfo for this class. \
 * @return Static RomDataInfo \
 */ \
const LibRpBase::RomDataInfo *klass::romDataInfo(void) \
{ \
	return &klass##Private::romDataInfo; \
}

/**
 * Static function wrappers for subclasses that have images.
 */
#define ROMDATA_IMPL_IMG_TYPES(klass) \
/** \
 * Get a bitfield of image types this class can retrieve. \
 * @return Bitfield of supported image types. (ImageTypesBF) \
 */ \
uint32_t klass::supportedImageTypes(void) const \
{ \
	return klass::supportedImageTypes_static(); \
}

#define ROMDATA_IMPL_IMG_SIZES(klass) \
/** \
 * Get a list of all available image sizes for the specified image type. \
 * \
 * The first item in the returned vector is the "default" size. \
 * If the width/height is 0, then an image exists, but the size is unknown. \
 * \
 * @param imageType Image type. \
 * @return Vector of available image sizes, or empty vector if no images are available. \
 */ \
std::vector<RomData::ImageSizeDef> klass::supportedImageSizes(ImageType imageType) const \
{ \
	return klass::supportedImageSizes_static(imageType); \
}

#define ROMDATA_IMPL_IMG(klass) \
	ROMDATA_IMPL_IMG_TYPES(klass) \
	ROMDATA_IMPL_IMG_SIZES(klass)

/** Assertion macros. **/

#define ASSERT_supportedImageSizes(imageType) do { \
	assert((imageType) >= IMG_INT_MIN && (imageType) <= IMG_EXT_MAX); \
	if ((imageType) < IMG_INT_MIN || (imageType) > IMG_EXT_MAX) { \
		/* ImageType is out of range. */ \
		return vector<ImageSizeDef>(); \
	} \
} while (0)

#define ASSERT_imgpf(imageType) do { \
	assert((imageType) >= IMG_INT_MIN && (imageType) <= IMG_EXT_MAX); \
	if ((imageType) < IMG_INT_MIN || (imageType) > IMG_EXT_MAX) { \
		/* ImageType is out of range. */ \
		return 0; \
	} \
} while (0)

#define ASSERT_loadInternalImage(imageType, pImage) do { \
	assert((imageType) >= IMG_INT_MIN && (imageType) <= IMG_INT_MAX); \
	if ((imageType) < IMG_INT_MIN || (imageType) > IMG_INT_MAX) { \
		/* ImageType is out of range. */ \
		(pImage).reset(); \
		return -ERANGE; \
	} \
} while (0)

#define ASSERT_extURLs(imageType, pExtURLs) do { \
	assert((imageType) >= IMG_EXT_MIN && (imageType) <= IMG_EXT_MAX); \
	if ((imageType) < IMG_EXT_MIN || (imageType) > IMG_EXT_MAX) { \
		/* ImageType is out of range. */ \
		return -ERANGE; \
	} \
	assert((pExtURLs) != nullptr); \
	if (!(pExtURLs)) { \
		/* No vector. */ \
		return -EINVAL; \
	} \
} while (0)

/**
 * loadInternalImage() implementation for RomData subclasses
 * with only a single type of internal image.
 *
 * @param ourImageType	Internal image type.
 * @param file		IRpFile pointer to check.
 * @param isValid	isValid value to check. (must be true)
 * @param romType	RomType value to check. (must be >= 0; specify 0 if N/A)
 * @param imgCache	Cached image pointer to check. (Specify nullptr if N/A)
 * @param func		Function to load the image.
 */
#define ROMDATA_loadInternalImage_single(ourImageType, file, isValid, romType, imgCache, func) do { \
	if ((imageType) != (ourImageType)) { \
		(pImage).reset(); \
		return -ENOENT; \
	} else if ((imgCache) != nullptr) { \
		(pImage) = (imgCache); \
		return 0; \
	} else if (!(file)) { \
		(pImage).reset(); \
		return -EBADF; \
	} else if (!(isValid) || ((int)(romType)) < 0) { \
		(pImage).reset(); \
		return -EIO; \
	} \
	\
	(pImage) = (func)(); \
	return ((bool)pImage ? 0 : -EIO); \
} while (0)
