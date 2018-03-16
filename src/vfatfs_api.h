#ifndef vfatfs_api_h
#define vfatfs_api_h

/*
	vfatfs_api.h - ESP8266 Arduino file system wrapper for FATFS
	Copyright (c) 2017 Zhenyu Wu. All rights reserved.

	This code was influenced by VFATFS Arduino wrapper, written by Ivan Grokhotkov,
	and MicroPython vfs_fat wrapper, written by Damien P. George and Paul Sokolovsky.

	This library is free software; you can redistribute it and/or
	modify it under the terms of the GNU Lesser General Public
	License as published by the Free Software Foundation; either
	version 2.1 of the License, or (at your option) any later version.

	This library is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the GNU
	Lesser General Public License for more details.

	You should have received a copy of the GNU Lesser General Public
	License along with this library; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "FS.h"

#include "FSImpl.h"
#include "Misc.h"

#ifndef ESPFAT_DEBUG_LEVEL
	#define ESPFAT_DEBUG_LEVEL ESPZW_DEBUG_LEVEL
#endif

#ifndef ESPFAT_LOG
	#define ESPFAT_LOG(...) ESPZW_LOG(__VA_ARGS__)
#endif

#if ESPFAT_DEBUG_LEVEL < 1
	#define ESPFAT_DEBUGDO(...)
	#define ESPFAT_DEBUG(...)
	#else
	#define ESPFAT_DEBUGDO(...) __VA_ARGS__
	#define ESPFAT_DEBUG(...) ESPFAT_LOG(__VA_ARGS__)
#endif

#if ESPFAT_DEBUG_LEVEL < 2
	#define ESPFAT_DEBUGVDO(...)
	#define ESPFAT_DEBUGV(...)
	#else
	#define ESPFAT_DEBUGVDO(...) __VA_ARGS__
	#define ESPFAT_DEBUGV(...) ESPFAT_LOG(__VA_ARGS__)
#endif

#if ESPFAT_DEBUG_LEVEL < 3
	#define ESPFAT_DEBUGVVDO(...)
	#define ESPFAT_DEBUGVV(...)
	#else
	#define ESPFAT_DEBUGVVDO(...) __VA_ARGS__
	#define ESPFAT_DEBUGVV(...) ESPFAT_LOG(__VA_ARGS__)
#endif

#include "fatfs/ff.h"

#include <flash_utils.h>

#define VFATFS_PHYS_BLOCK		FLASH_SECTOR_SIZE
#define VFATFS_SECT_PER_PHYS	1	// HAL layer does not handle partial erase
#define VFATFS_SECTOR_SIZE		(VFATFS_PHYS_BLOCK/VFATFS_SECT_PER_PHYS)

// Enable in-memory cache of trimmed sectors
// Required to actually enable trim support
// Consumes heap space, but benefits read/write performance and flash longevity
#define VFATFS_TRIMCACHE

#ifdef VFATFS_TRIMCACHE

	// Flash conserve levels:
	//  0 - Not conservative (fastest, incurs unnecessary flash wears after reboot)
	//  1 - Avoid redundant trim (slower read/write after reboot + some stack for sector probing)
	//  2 - Avoid all '1' write mark sector dirty write (even slower write)
	//  3 - Avoid unnecessary erase/write (extra VFATFS_SECTOR_SIZE heap space)
	//  * Level 3 not implemented yet!
	#define VFATFS_CONSERVE_LEVEL	1

	// Heap consumption:
	// Level 0: ~64 bytes per 1MB (~1KB for 16MB)
	// Level 1,2: ~128 bytes per 1MB (~2KB for 16MB)
	// Level 3: (Level 1,2) + ~4KB

	#if VFATFS_CONSERVE_LEVEL >= 1

		// How much stack can be use for sector probing
		// Must be multiple of 4
		//  and integer divisor of VFATFS_SECTOR_SIZE
		//  and small enough to NOT overflow stack
		//  and large enough to have good efficiency
		#define VFATFS_PROBE_UNIT (VFATFS_SECTOR_SIZE/16)

		// Non-zero interval enables background trimming
		// Each time check/trim up to 16 sectors
		// Hint: significantly improves trim request performance
		#define VFATFS_BGTRIM_INTERVAL 100

	#endif

	#if !VFATFS_BGTRIM_INTERVAL

		// None-zero enables "lazy" trimming
		// For each trim request, erase up to given number of sectors
		//  and the rest is ignored.
		// This improves response time at very large trim request
		#define VFATFS_LAZY_TRIM 16

	#endif

#endif

using namespace fs;

class VFATFSImpl;

class VFATPartitions {
friend class VFATFSImpl;
protected:
	static DWORD _size[4];
	static uint8_t _opencnt;
	static bool create();
public:
	static bool config(uint8_t A, uint8_t B = 0, uint8_t C = 0, uint8_t D = 0);
};

class VFATFSFileImpl;
class VFATFSDirImpl;

class VFATFSImpl : public FSImpl {
public:
	VFATFSImpl(uint8_t partno = 0)
		: _fatfs({0}), _mounted(false), _partno(partno) {}

	bool begin() override;
	void end() override;
	bool format() override;
	bool info(FSInfo& info) const override;

	bool getLabel(char *label) const;
	bool setLabel(const char *label);

	bool exists(const char* path) const override;
	bool isDir(const char* path) const override;
	size_t size(const char* path) const override;
	time_t mtime(const char* path) const override;

	FileImplPtr openFile(const char* path, OpenMode openMode,
		AccessMode accessMode) override;
	DirImplPtr openDir(const char* path, bool create) override;
	bool remove(const char* path) override;
	bool rename(const char* pathFrom, const char* pathTo) override;

protected:
	friend class VFATFSFileImpl;
	friend class VFATFSDirImpl;

	bool mount();
	bool unmount();

	FATFS _fatfs;
	bool _mounted;
	uint8_t _partno;
};

class VFATFSFileImpl : public FileImpl {
public:
	VFATFSFileImpl(VFATFSImpl& fs, FIL fd, String && pathname)
	: _fs(fs), _fd(fd), _pathname(std::move(pathname)) {}

	~VFATFSFileImpl() override {
		close();
	}

	size_t write(const uint8_t *buf, size_t size) override;
	size_t read(uint8_t* buf, size_t size) override;
	void flush() override;
	bool seek(uint32_t pos, SeekMode mode) override;
	bool truncate() override;

	size_t position() const override {
		MUSTNOTCLOSE();
		return f_tell(&_fd);
	}

	size_t size() const override {
		MUSTNOTCLOSE();
		return f_size(&_fd);
	}

	const char* name() const override {
		return _pathname.c_str()+2;
	}

	time_t mtime() const override;
	bool remove() override;
	bool rename(const char *nameTo) override;

	void close() override;

protected:
	inline void MUSTNOTCLOSE() const {
		while (!_fd.obj.fs) { panic(); }
	}

	VFATFSImpl& _fs;
	FIL _fd;
	String _pathname;
};

class VFATFSDirImpl : public DirImpl {
	public:
	VFATFSDirImpl(VFATFSImpl& fs, DIR fd, String && pathname)
	: _fs(fs), _fd(fd), _pathname(std::move(pathname)), entryStats({0}) {}

	~VFATFSDirImpl() override {
		close();
	}

	FileImplPtr openFile(const char *name, OpenMode openMode,
		AccessMode accessMode) override;
	DirImplPtr openDir(const char *name, bool create) override;
	bool exists(const char *name) const override;
	bool isDir(const char* name) const override;
	size_t size(const char* name) const override;
	time_t mtime(const char* name) const override;
	bool remove(const char *name) override;
	bool rename(const char* nameFrom, const char* nameTo) override;

	const char* entryName() const override {
		return entryStats.fname[0]? entryStats.fname : NULL;
	}

	size_t entrySize() const override {
		return entryStats.fname[0]? entryStats.fsize : 0;
	}

	time_t entryMtime() const override;
	bool isEntryDir() const override;
	bool next(bool reset) override;

	FileImplPtr openEntryFile(OpenMode openMode,
		AccessMode accessMode) override;
	DirImplPtr openEntryDir() override;
	bool removeEntry() override;
	bool renameEntry(const char* nameTo) override;

	time_t mtime() const override;

	const char* name() const override {
		return _pathname.c_str()+2;
	}

protected:
	VFATFSImpl& _fs;
	DIR _fd;
	String _pathname;
	FILINFO entryStats;

	void close();
};

#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_VFATFS)
extern fs::FS VFATFS;
#endif

#endif//vfatfs_api_h
