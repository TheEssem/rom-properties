/***************************************************************************
 * ROM Properties Page shell extension. (librpfile)                        *
 * FileSystem_posix.cpp: File system functions. (POSIX implementation)     *
 *                                                                         *
 * Copyright (c) 2016-2022 by David Korth.                                 *
 * SPDX-License-Identifier: GPL-2.0-or-later                               *
 ***************************************************************************/

#include "stdafx.h"
#include "config.librpfile.h"
#include "FileSystem.hpp"

// C includes
#include <fcntl.h>	// AT_FDCWD
#include <sys/stat.h>	// stat(), statx()
#include <utime.h>
#include <unistd.h>

// DT_* enumeration
#include "d_type.h"

#ifdef __linux__
// TODO: Remove once /proc/mounts parsing is implemented.
#  include <sys/vfs.h>
#  include <linux/magic.h>
// from `man 2 fstatfs`, but not present in linux/magic.h on 4.14-r1
#  ifndef CGROUP2_SUPER_MAGIC
#    define CGROUP2_SUPER_MAGIC 0x63677270
#  endif
#  ifndef CIFS_MAGIC_NUMBER
#    define CIFS_MAGIC_NUMBER 0xff534d42
#  endif
#  ifndef COH_SUPER_MAGIC
#    define COH_SUPER_MAGIC 0x012ff7b7
#  endif
#  ifndef FUSE_SUPER_MAGIC
#    define FUSE_SUPER_MAGIC 0x65735546
#  endif
#  ifndef MQUEUE_MAGIC
#    define MQUEUE_MAGIC 0x19800202
#  endif
#  ifndef NSFS_MAGIC
#    define NSFS_MAGIC 0x6e736673
#  endif
#  ifndef OCFS2_SUPER_MAGIC
#    define OCFS2_SUPER_MAGIC 0x7461636f
#  endif
#  ifndef TRACEFS_MAGIC
#    define TRACEFS_MAGIC 0x74726163
#  endif
#  ifndef SYSV2_SUPER_MAGIC
#    define SYSV2_SUPER_MAGIC 0x012ff7b6
#  endif
#  ifndef SYSV4_SUPER_MAGIC
#    define SYSV4_SUPER_MAGIC 0x012ff7b5
#  endif
#endif /* __linux__ */

// C++ STL classes
using std::string;

namespace LibRpFile { namespace FileSystem {

/**
 * Recursively mkdir() subdirectories.
 *
 * The last element in the path will be ignored, so if
 * the entire pathname is a directory, a trailing slash
 * must be included.
 *
 * NOTE: Only native separators ('\\' on Windows, '/' on everything else)
 * are supported by this function.
 *
 * @param path Path to recursively mkdir. (last component is ignored)
 * @return 0 on success; negative POSIX error code on error.
 */
int rmkdir(const string &path)
{
	// Linux (and most other systems) use UTF-8 natively.
	string path8 = path;

	// Find all slashes and ensure the directory component exists.
	size_t slash_pos = path8.find(DIR_SEP_CHR, 0);
	if (slash_pos == 0) {
		// Root is always present.
		slash_pos = path8.find(DIR_SEP_CHR, 1);
	}

	while (slash_pos != string::npos) {
		// Temporarily NULL out this slash.
		path8[slash_pos] = 0;

		// Attempt to create this directory.
		if (::mkdir(path8.c_str(), 0777) != 0) {
			// Could not create the directory.
			// If it exists already, that's fine.
			// Otherwise, something went wrong.
			if (errno != EEXIST) {
				// Something went wrong.
				return -errno;
			}
		}

		// Put the slash back in.
		path8[slash_pos] = DIR_SEP_CHR;
		slash_pos++;

		// Find the next slash.
		slash_pos = path8.find(DIR_SEP_CHR, slash_pos);
	}

	// rmkdir() succeeded.
	return 0;
}

/**
 * Does a file exist?
 * @param pathname Pathname.
 * @param mode Mode.
 * @return 0 if the file exists with the specified mode; non-zero if not.
 */
int access(const char *pathname, int mode)
{
	return ::access(pathname, mode);
}

/**
 * Get a file's size.
 * @param filename Filename.
 * @return Size on success; -1 on error.
 */
off64_t filesize(const char *filename)
{
	assert(filename != nullptr);
	assert(filename[0] != '\0');
	if (unlikely(!filename || filename[0] == '\0')) {
		return -1;
	}

#ifdef HAVE_STATX
	struct statx sbx;
	int ret = statx(AT_FDCWD, filename, 0, STATX_SIZE, &sbx);
	if (ret != 0 || !(sbx.stx_mask & STATX_SIZE)) {
		// statx() failed and/or did not return the file size.
		int ret = -errno;
		return (ret != 0 ? ret : -EIO);
	}
	return sbx.stx_size;
#else /* !HAVE_STATX */
	struct stat sb;
	if (stat(filename, &sb) != 0) {
		// stat() failed.
		int ret = -errno;
		return (ret != 0 ? ret : -EIO);
	}
	return sb.st_size;
#endif /* HAVE_STATX */
}

/**
 * Set the modification timestamp of a file.
 * @param filename	[in] Filename (UTF-16)
 * @param mtime		[in] Modification time (UNIX timestamp)
 * @return 0 on success; negative POSIX error code on error.
 */
int set_mtime(const char *filename, time_t mtime)
{
	assert(filename && filename[0] != '\0');
	if (!filename || filename[0] == '\0')
		return -EINVAL;

	// FIXME: time_t is 32-bit on 32-bit Linux.
	// TODO: Add a static_warning() macro?
	// - http://stackoverflow.com/questions/8936063/does-there-exist-a-static-warning
	struct utimbuf utbuf;
	utbuf.actime = time(nullptr);
	utbuf.modtime = mtime;
	int ret = utime(filename, &utbuf);

	return (ret == 0 ? 0 : -errno);
}

/**
 * Get the modification timestamp of a file.
 * @param filename	[in] Filename (UTF-8)
 * @param pMtime	[out] Buffer for the modification time (UNIX timestamp)
 * @return 0 on success; negative POSIX error code on error.
 */
int get_mtime(const char *filename, time_t *pMtime)
{
	assert(filename && filename[0] != '\0');
	assert(pMtime != nullptr);
	if (!filename || filename[0] == '\0' || !pMtime)
		return -EINVAL;

	// FIXME: time_t is 32-bit on 32-bit Linux.
	// TODO: Add a static_warning() macro?
	// - http://stackoverflow.com/questions/8936063/does-there-exist-a-static-warning

#ifdef HAVE_STATX
	struct statx sbx;
	int ret = statx(AT_FDCWD, filename, 0, STATX_MTIME, &sbx);
	if (ret != 0 || !(sbx.stx_mask & STATX_MTIME)) {
		// statx() failed and/or did not return the modification time.
		int ret = -errno;
		return (ret != 0 ? ret : -EIO);
	}
	*pMtime = sbx.stx_mtime.tv_sec;
#else /* !HAVE_STATX */
	struct stat sb;
	if (stat(filename, &sb) != 0) {
		// stat() failed.
		int ret = -errno;
		return (ret != 0 ? ret : -EIO);
	}
	*pMtime = sb.st_mtime;
#endif /* HAVE_STATX */

	return 0;
}

/**
 * Delete a file.
 * @param filename Filename.
 * @return 0 on success; negative POSIX error code on error.
 */
int delete_file(const char *filename)
{
	assert(filename != nullptr);
	assert(filename[0] != '\0');
	if (unlikely(!filename || filename[0] == '\0')) {
		return -EINVAL;
	}

	int ret = unlink(filename);
	if (ret != 0) {
		// Error deleting the file.
		ret = -errno;
	}

	return ret;
}

/**
 * Check if the specified file is a symbolic link.
 *
 * Symbolic links are NOT resolved; otherwise wouldn't check
 * if the specified file was a symlink itself.
 *
 * @param filename Filename (UTF-8)
 * @return True if the file is a symbolic link; false if not.
 */
bool is_symlink(const char *filename)
{
	assert(filename != nullptr);
	assert(filename[0] != '\0');
	if (unlikely(!filename || filename[0] == '\0')) {
		return false;
	}

#ifdef HAVE_STATX
	struct statx sbx;
	int ret = statx(AT_FDCWD, filename, AT_SYMLINK_NOFOLLOW, STATX_TYPE, &sbx);
	if (ret != 0 || !(sbx.stx_mask & STATX_TYPE)) {
		// statx() failed and/or did not return the file type.
		// Assume this is not a symlink.
		return false;
	}
	return !!S_ISLNK(sbx.stx_mode);
#else /* !HAVE_STATX */
	struct stat sb;
	if (lstat(filename, &sb) != 0) {
		// lstat() failed.
		// Assume this is not a symlink.
		return false;
	}
	return !!S_ISLNK(sb.st_mode);
#endif /* HAVE_STATX */
}

/**
 * Resolve a symbolic link.
 *
 * If the specified filename is not a symbolic link,
 * the filename will be returned as-is.
 *
 * @param filename Filename of symbolic link (UTF-8)
 * @return Resolved symbolic link, or empty string on error.
 */
string resolve_symlink(const char *filename)
{
	assert(filename != nullptr);
	assert(filename[0] != '\0');
	if (unlikely(!filename || filename[0] == '\0')) {
		return string();
	}

	// NOTE: realpath() might not be available on some systems...
	string ret;
	char *const resolved_path = realpath(filename, nullptr);
	if (resolved_path != nullptr) {
		ret = resolved_path;
		free(resolved_path);
	}
	return ret;
}

/**
 * Check if the specified file is a directory.
 *
 * Symbolic links are resolved as per usual directory traversal.
 *
 * @param filename Filename to check (UTF-8)
 * @return True if the file is a directory; false if not.
 */
bool is_directory(const char *filename)
{
	assert(filename != nullptr);
	assert(filename[0] != '\0');
	if (unlikely(!filename || filename[0] == '\0')) {
		return false;
	}

#ifdef HAVE_STATX
	struct statx sbx;
	int ret = statx(AT_FDCWD, filename, 0, STATX_TYPE, &sbx);
	if (ret != 0 || !(sbx.stx_mask & STATX_TYPE)) {
		// statx() failed and/or did not return the file type.
		// Assume this is not a directory.
		return false;
	}
	return !!S_ISDIR(sbx.stx_mode);
#else /* !HAVE_STATX */
	struct stat sb;
	if (stat(filename, &sb) != 0) {
		// stat() failed.
		// Assume this is not a directory.
		return false;
	}
	return !!S_ISDIR(sb.st_mode);
#endif /* HAVE_STATX */
}

/**
 * Is a file located on a "bad" file system?
 *
 * We don't want to check files on e.g. procfs,
 * or on network file systems if the option is disabled.
 *
 * @param filename Filename (UTF-8)
 * @param allowNetFS If true, allow network file systems.
 *
 * @return True if this file is on a "bad" file system; false if not.
 */
bool isOnBadFS(const char *filename, bool allowNetFS)
{
	bool bRet = false;

#ifdef __linux__
	// TODO: Get the mount point, then look it up in /proc/mounts.

	struct statfs sfbuf;
	if (statfs(filename, &sfbuf) != 0) {
		// statfs() failed.
		// Assume this isn't a network file system.
		return false;
	}

	// TODO: Convert to a lookup table? Check the generated assembler code.
	switch (static_cast<uint32_t>(sfbuf.f_type)) {
		case ANON_INODE_FS_MAGIC:
		case BDEVFS_MAGIC:
		case BPF_FS_MAGIC:
		case CGROUP_SUPER_MAGIC:
		case CGROUP2_SUPER_MAGIC:
		case DEBUGFS_MAGIC:
		case DEVPTS_SUPER_MAGIC:
		case EFIVARFS_MAGIC:
		case FUTEXFS_SUPER_MAGIC:
		case MQUEUE_MAGIC:
		case NSFS_MAGIC:
		case OPENPROM_SUPER_MAGIC:
		case PIPEFS_MAGIC:
		case PROC_SUPER_MAGIC:
		case PSTOREFS_MAGIC:
		case SECURITYFS_MAGIC:
		case SMACK_MAGIC:
		case SOCKFS_MAGIC:
		case SYSFS_MAGIC:
		case SYSV2_SUPER_MAGIC:
		case SYSV4_SUPER_MAGIC:
		case TRACEFS_MAGIC:
		case USBDEVICE_SUPER_MAGIC:
			// Bad file systems.
			bRet = true;
			break;

		case AFS_SUPER_MAGIC:
		case CIFS_MAGIC_NUMBER:
		case CODA_SUPER_MAGIC:
		case COH_SUPER_MAGIC:
		case NCP_SUPER_MAGIC:
		case NFS_SUPER_MAGIC:
		case OCFS2_SUPER_MAGIC:
		case SMB_SUPER_MAGIC:
		case V9FS_MAGIC:
			// Network file system.
			// Allow it if we're allowing network file systems.
			bRet = !allowNetFS;
			break;

		case FUSE_SUPER_MAGIC:	// TODO: Check the actual fs type.
			// Other file system.
			// FIXME: `fuse` is used for various local file systems
			// as well as sshfs. Local is more common, so let's assume
			// it's in use for a local file system.
			break;

		default:
			break;
	}
#else
#  warning TODO: Implement "badfs" support for non-Linux systems.
	RP_UNUSED(filename);
	RP_UNUSED(allowNetFS);
#endif

	return bRet;
}

/**
 * Get a file's size and time.
 * @param filename	[in] Filename (UTF-8)
 * @param pFileSize	[out] File size
 * @param pMtime	[out] Modification time (UNIX timestamp)
 * @return 0 on success; negative POSIX error code on error.
 */
int get_file_size_and_mtime(const char *filename, off64_t *pFileSize, time_t *pMtime)
{
	assert(filename && filename[0] != '\0');
	assert(pFileSize != nullptr);
	assert(pMtime != nullptr);
	if (unlikely(!filename || filename[0] == '\0' || !pFileSize || !pMtime)) {
		return -EINVAL;
	}

	// FIXME: time_t is 32-bit on 32-bit Linux.
	// TODO: Add a static_warning() macro?
	// - http://stackoverflow.com/questions/8936063/does-there-exist-a-static-warning

#ifdef HAVE_STATX
	struct statx sbx;
	int ret = statx(AT_FDCWD, filename, 0, STATX_TYPE | STATX_MTIME | STATX_SIZE, &sbx);
	if (ret != 0 || (sbx.stx_mask & (STATX_MTIME | STATX_SIZE)) != (STATX_MTIME | STATX_SIZE)) {
		// statx() failed and/or did not return the modification time or file size.
		int ret = -errno;
		return (ret != 0 ? ret : -EIO);
	}

	// Make sure this is not a directory.
	if ((sbx.stx_mask & STATX_TYPE) && S_ISDIR(sbx.stx_mode)) {
		// It's a directory.
		return -EISDIR;
	}

	// Return the file size and mtime.
	*pFileSize = sbx.stx_size;
	*pMtime = sbx.stx_mtime.tv_sec;
#else /* !HAVE_STATX */
	struct stat sb;
	if (stat(filename, &sb) != 0) {
		// stat() failed.
		int ret = -errno;
		return (ret != 0 ? ret : -EIO);
	}

	// Make sure this is not a directory.
	if (S_ISDIR(sb.st_mode)) {
		// It's a directory.
		return -EISDIR;
	}

	// Return the file size and mtime.
	*pFileSize = sb.st_size;
	*pMtime = sb.st_mtime;
#endif /* HAVE_STATX */

	return 0;
}

/**
 * Get a file's d_type.
 * @param filename Filename
 * @param deref If true, dereference symbolic links (lstat)
 * @return File d_type
 */
uint8_t get_file_d_type(const char *filename, bool deref)
{
	assert(filename != nullptr);
	assert(filename[0] != '\0');
	if (unlikely(!filename || filename[0] == '\0')) {
		return DT_UNKNOWN;
	}

	unsigned int mode;

#ifdef HAVE_STATX
	struct statx sbx;
	const int dirfd = (unlikely(deref))
		?  AT_FDCWD				// dereference symlinks
		: (AT_FDCWD | AT_SYMLINK_NOFOLLOW);	// don't dereference symlinks
	int ret = statx(dirfd, filename, 0, STATX_TYPE, &sbx);
	if (ret != 0 || !(sbx.stx_mask & STATX_TYPE)) {
		// statx() failed and/or did not return the file type.
		return DT_UNKNOWN;
	}
	mode = sbx.stx_mode;
#else /* !HAVE_STATX */
	struct stat sb;
	int ret = (unlikely(deref) ? stat(filename, &sb) : lstat(filename, &sb));
	if (ret != 0) {
		// stat() failed.
		return DT_UNKNOWN;
	}
	mode = sb.st_mode;
#endif /* HAVE_STATX */

	// The type bits in struct stat's mode match the
	// DT_* enumeration values.
	return IFTODT(mode);
}

} }
