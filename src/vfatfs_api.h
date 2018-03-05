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
//#include <limits>
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

#define VFATFS_PHYS_BLOCK		 	4096
#define VFATFS_SECT_PER_PHYS	1			// HAL layer does not handle partial erase
#define VFATFS_SECTOR_SIZE		(VFATFS_PHYS_BLOCK/VFATFS_SECT_PER_PHYS)

using namespace fs;

class VFATFSFileImpl;
class VFATFSDirImpl;

class VFATFSImpl : public FSImpl {
public:
	VFATFSImpl() :_fatfs({0}), _mounted(false) {}

	FileImplPtr open(const char* path, OpenMode openMode, AccessMode accessMode) override;
	DirImplPtr openDir(const char* path, bool create) override;
	bool rename(const char* pathFrom, const char* pathTo) override;
	bool info(FSInfo& info) override;
	bool remove(const char* path) override;
	bool exists(const char* path) override;
	bool isDir(const char* path) override;
	time_t mtime(const char* path) override;

	bool begin() override;
	void end() override;
	bool format() override;

protected:
	friend class VFATFSFileImpl;
	friend class VFATFSDirImpl;

	bool mount();
	bool unmount();

	FATFS _fatfs;
	bool _mounted;
};

class VFATFSFileImpl : public FileImpl {
public:
	VFATFSFileImpl(VFATFSImpl& fs, FIL fd, const char* pathname)
	: _fs(fs), _fd(fd), _pathname(pathname) {}

	~VFATFSFileImpl() override {
		close();
	}

	size_t write(const uint8_t *buf, size_t size) override;
	size_t read(uint8_t* buf, size_t size) override;
	void flush() override;
	bool seek(uint32_t pos, SeekMode mode) override;

	size_t position() const override {
		MUSTNOTCLOSE();
		return f_tell(&_fd);
	}

	size_t size() const override {
		MUSTNOTCLOSE();
		return f_size(&_fd);
	}

	const char* name() const override {
		return _pathname.c_str();
	}

	time_t mtime() const override;
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
	VFATFSDirImpl(VFATFSImpl& fs, DIR fd, const char* pathname)
	: _fs(fs), _fd(fd), _pathname(pathname), entryStats({0}) {}

	~VFATFSDirImpl() override {
		close();
	}

	FileImplPtr openFile(OpenMode openMode, AccessMode accessMode) override;
	DirImplPtr openDir() override;
	FileImplPtr openFile(const char *name, OpenMode openMode, AccessMode accessMode) override;
	DirImplPtr openDir(const char *name, bool create) override;
	bool remove() override;
	bool remove(const char *name) override;

	const char* entryName() const override {
		return entryStats.fname[0]? entryStats.fname : NULL;
	}

	size_t entrySize() const override {
		return entryStats.fname[0]? entryStats.fsize : 0;
	}

	time_t entryMtime() const override;
	bool isEntryDir() const override;
	bool isDir(const char* path) const override;

	const char* name() const override {
		return _pathname.c_str();
	}

	time_t mtime() const override;

	bool next(bool reset) override;

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
