/*
	vfatfs_api.cpp - ESP8266 Arduino file system wrapper for FATFS
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
#include "vfatfs_api.h"

#include <time.h>

/* Convert a FAT time/date pair to a UNIX timestamp (seconds since 1970). */
void fattime2unixts(time_t &ts, uint16_t time, uint16_t date) {
	struct tm tpart{0};
	tpart.tm_year = (date >> 9) + 80;
	tpart.tm_mon = (date >> 5) & 0xf -1;
	tpart.tm_mday	= (date & 0x1f);
	tpart.tm_hour = (time >> 11);
	tpart.tm_min = (time >> 5) & 0x3f;
	tpart.tm_sec = (time & 0x1f) << 1;

	ESPFAT_DEBUGVV("[VFATFS##fattime2unixts] %d-%02d-%02d %02d:%02d:%02d\n",
		tpart.tm_year + 1900, tpart.tm_mon + 1, tpart.tm_mday,
		tpart.tm_hour, tpart.tm_min, tpart.tm_sec);
	ts = mktime(&tpart);
}

/* Convert a UNIX timestamp to a FAT time/date pair (since 1980). */
void unixts2fattime(time_t ts, uint16_t &time, uint16_t &date) {
	struct tm tpart;
	gmtime_r(&ts, &tpart);

	ESPFAT_DEBUGVV("[VFATFS##unixts2fattime] %d-%02d-%02d %02d:%02d:%02d\n",
		tpart.tm_year + 1900, tpart.tm_mon + 1, tpart.tm_mday,
		tpart.tm_hour, tpart.tm_min, tpart.tm_sec);

	time = tpart.tm_hour << 11 | tpart.tm_min << 5 | tpart.tm_sec >> 1;
	date = (tpart.tm_year - 80) << 9 | (tpart.tm_mon + 1) << 5 | tpart.tm_mday;
}

bool normalizePath(const char* in, String& out) {
	ESPFAT_DEBUGVV("[VFATFS##normalizePath] Input '%s'\n",in);

	// All path must start from root
	if (*in != '/') {
		ESPFAT_DEBUGV("[VFATFS##normalizePath] Not from root\n");
		return false;
	}

	String buf(in);
	char* toks[16] = {0};
	uint8_t idx = 0;

	char* ptr = const_cast<char*>(buf.c_str());
	while ((idx < 16) && ptr) {
		while (*++ptr == '/');
		if (!*ptr) break;
		toks[idx] = ptr;
		while (*++ptr != '/')
			if (!*ptr) break;
		ptr = *ptr ? (*ptr = 0, ptr): 0;
		if (*toks[idx] == '.') {
			if (!toks[idx][1])
				continue;
			if ((toks[idx][1] == '.') && !toks[idx][2]) {
				if (!idx) {
					ESPFAT_DEBUGV("[VFATFS##normalizePath] Token underflow\n");
					return false;
				}
				idx--;
				continue;
			}
		}
		idx++;
	}
	// Check token overflow
	if (ptr && *ptr) {
		ESPFAT_DEBUGV("[VFATFS##normalizePath] Token overflow\n");
		return false;
	}
	out = idx? "" : "/";
	for (uint8_t i=0; i<idx; i++)
		(out+= '/')+= toks[i];

	ESPFAT_DEBUGVV("[VFATFS##normalizePath] Output '%s'\n",out.c_str());
	return true;
}

#include "spiffs/spiffs.h"
#include "fatfs/diskio.h"

// these symbols should be defined in the linker script for each flash layout
#ifdef ARDUINO

#include <Arduino.h>

extern "C" uint32_t _SPIFFS_start;
extern "C" uint32_t _SPIFFS_end;

#define VFATFS_PHYS_ADDR	((uint32_t) (&_SPIFFS_start) - 0x40200000)
#define VFATFS_PHYS_SIZE	((uint32_t) (&_SPIFFS_end) - (uint32_t) (&_SPIFFS_start))

extern int32_t spiffs_hal_write(uint32_t addr, uint32_t size, uint8_t *src);
extern int32_t spiffs_hal_erase(uint32_t addr, uint32_t size);
extern int32_t spiffs_hal_read(uint32_t addr, uint32_t size, uint8_t *dst);

/*-----------------------------------------------------------------------*/
/* Initialize a Drive																										*/
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (
	BYTE pdrv					/* Physical drive nmuber (0..) */
) {
	if (pdrv != 0)
		return STA_NODISK;
	return 0;
	// return STA_PROTECT; // Read-Only Disk
}

/*-----------------------------------------------------------------------*/
/* Get Disk Status																											*/
/*-----------------------------------------------------------------------*/

DSTATUS disk_status (
	BYTE pdrv					/* Physical drive nmuber (0..) */
) {
	if (pdrv != 0)
		return STA_NODISK;
	return 0;
	// return STA_PROTECT; // Read-Only Disk
}

/*-----------------------------------------------------------------------*/
/* Read Sector(s)																												*/
/*-----------------------------------------------------------------------*/

DRESULT disk_read (
	BYTE pdrv,				/* Physical drive nmuber (0..) */
	BYTE *buff,				/* Data buffer to store read data */
	DWORD sector,			/* Sector address (LBA) */
	UINT count				/* Number of sectors to read (1..128) */
) {
	if (pdrv != 0)
		return RES_PARERR;

	uint32_t addr = VFATFS_PHYS_ADDR + sector * VFATFS_SECTOR_SIZE;
	uint32_t size = count * VFATFS_SECTOR_SIZE;

	ESPFAT_DEBUGVV("[VFATF##disk_read] R 0x%lX 0x%X\n", addr, size);
	int32_t ret = spiffs_hal_read(addr, size, buff);
	return ret == SPIFFS_OK? RES_OK : RES_ERROR;
}

/*-----------------------------------------------------------------------*/
/* Write Sector(s)																											*/
/*-----------------------------------------------------------------------*/

DRESULT disk_write (
	BYTE pdrv,				/* Physical drive nmuber (0..) */
	const BYTE *buff, /* Data to be written */
	DWORD sector,			/* Sector address (LBA) */
	UINT count				/* Number of sectors to write (1..128) */
) {
	if (pdrv != 0)
		return RES_PARERR;

	uint32_t addr = VFATFS_PHYS_ADDR + sector * VFATFS_SECTOR_SIZE;
	uint32_t size = count * VFATFS_SECTOR_SIZE;

	ESPFAT_DEBUGVV("[VFATFS##disk_write] E 0x%lX 0x%X\n", addr, size);
	int32_t ret = spiffs_hal_erase(addr, size);
	if (ret != SPIFFS_OK)
		return RES_ERROR;
	ESPFAT_DEBUGVV("[VFATFS##disk_write] W 0x%lX 0x%X\n", addr, size);
	ret = spiffs_hal_write(addr, size, const_cast<uint8_t*>(buff));
	return ret == SPIFFS_OK? RES_OK : RES_ERROR;
}


/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions																							*/
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl (
	BYTE pdrv,				/* Physical drive nmuber (0..) */
	BYTE cmd,					/* Control code */
	void *buff				/* Buffer to send/receive control data */
) {
	if (pdrv != 0)
		return RES_PARERR;

	switch (cmd) {
		case CTRL_SYNC:
			// NOP
		return RES_OK;

		case GET_SECTOR_COUNT:
			*((DWORD*)buff) = VFATFS_PHYS_SIZE / VFATFS_SECTOR_SIZE;
			return RES_OK;

		case GET_SECTOR_SIZE:
			*((DWORD*)buff) = VFATFS_SECTOR_SIZE;
			return RES_OK;

		case GET_BLOCK_SIZE:
			*((DWORD*)buff) = VFATFS_SECT_PER_PHYS; // erase block size in units of sector size
			return RES_OK;
	}

	ESPFAT_DEBUGV("[VFATFS##disk_ioctl] Unhandled %d\n", cmd);
	return RES_PARERR;
}

#include <time.h>

DWORD get_fattime() {
	uint16_t fattime, fatdate;
	unixts2fattime(time(NULL), fattime, fatdate);
	return (DWORD)fatdate << 16 | fattime;
}

#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_VFATFS)
FS VFATFS = FS(FSImplPtr(new VFATFSImpl()));
#endif

#endif

using namespace fs;

FileImplPtr VFATFSImpl::open(const char* path, OpenMode openMode, AccessMode accessMode) {
	String normPath;
	if (!normalizePath(path, normPath)) {
		ESPFAT_DEBUGV("[VFATFSImpl::open] Invalid path\n");
		return FileImplPtr();
	}

	BYTE open_mode = 0;
	open_mode|= (AM_READ & accessMode)? FA_READ : 0;
	open_mode|= (AM_WRITE & accessMode)? FA_WRITE : 0;
	if (OM_CREATE & openMode) {
		if (OM_TRUNCATE & openMode)
			open_mode|= FA_CREATE_ALWAYS;
		else if (OM_APPEND & openMode)
			open_mode|= FA_OPEN_APPEND;
		else
			open_mode|= FA_OPEN_ALWAYS;
	}

	FIL fd{0};
	FRESULT res = f_open(&fd, normPath.c_str(), open_mode);
	if (res != FR_OK) {
		ESPFAT_DEBUGV("[VFATFSImpl::open] Error %d\n", res);
		return FileImplPtr();
	}
	return std::make_shared<VFATFSFileImpl>(*this, fd, normPath.c_str());
}

DirImplPtr VFATFSImpl::openDir(const char* path, bool create) {
	String normPath;
	if (!normalizePath(path, normPath)) {
		ESPFAT_DEBUGV("[VFATFSImpl::openDir] Invalid path\n");
		return DirImplPtr();
	}

	DIR fd{0};
	FRESULT res = f_opendir(&fd, normPath.c_str());
	if (res != FR_OK) {
		if ((res == FR_NO_PATH) && create) {
				res = f_mkdir(normPath.c_str());
				if (res == FR_OK)
					res = f_opendir(&fd, normPath.c_str());
		}
	}
	if (res != FR_OK) {
		ESPFAT_DEBUGV("[VFATFSImpl::opendir] Error %d\n", res);
		return DirImplPtr();
	}
	return std::make_shared<VFATFSDirImpl>(*this, fd, normPath.c_str());
}

bool VFATFSImpl::rename(const char* pathFrom, const char* pathTo) {
	String normPathFrom;
	if (!normalizePath(pathFrom, normPathFrom)) {
		ESPFAT_DEBUGV("[VFATFSImpl::rename] Invalid path from\n");
		return false;
	}
	String normPathTo;
	if (!normalizePath(pathTo, normPathTo)) {
		ESPFAT_DEBUGV("[VFATFSImpl::rename] Invalid path to\n");
		return false;
	}
	if (normPathFrom == normPathTo)
		return true;

	FRESULT res = f_rename(normPathFrom.c_str(), normPathTo.c_str());
	if (res == FR_EXIST) {
		res = f_unlink(normPathTo.c_str());
		if (res != FR_OK) {
			ESPFAT_DEBUGV("[VFATFSImpl::rename] Unable to remove existing path=`%s`\n", normPathTo.c_str());
			return false;
		}
		// try to rename again
		res = f_rename(normPathFrom.c_str(), normPathTo.c_str());
	}
	if (res != FR_OK) {
		ESPFAT_DEBUGV("[VFATFSImpl::rename] Unable to rename path=`%s`\n", normPathFrom.c_str());
		return false;
	}
	return true;
}

bool VFATFSImpl::info(FSInfo& info) {
	info.maxOpenFiles = 10; // Give a reasonable number
	info.maxPathLength = 260; // MAX_PATH

	uint32_t totalBytes, usedBytes;
	FATFS *fatfs;
	DWORD nclst;
	FRESULT res = f_getfree("/", &nclst, &fatfs);
	if (FR_OK != res) {
		ESPFAT_DEBUGV("[VFATFSImpl::info] Error %d\n", res);
		return false;
	}
	info.pageSize = VFATFS_PHYS_BLOCK;
	uint32_t blocksize = fatfs->csize * VFATFS_SECTOR_SIZE;
	info.blockSize = blocksize;
	uint32_t blockcnt = (fatfs->n_fatent - 2) * fatfs->csize;
	info.totalBytes = blocksize * blockcnt;
	info.usedBytes = blocksize * (blockcnt - nclst);
	return true;
}

bool VFATFSImpl::remove(const char* path) {
	String normPath;
	if (!normalizePath(path, normPath)) {
		ESPFAT_DEBUGV("[VFATFSImpl::remove] Invalid path\n");
		return false;
	}

	FRESULT res = f_unlink(normPath.c_str());
	if (res != FR_OK) {
		ESPFAT_DEBUGV("[VFATFSImpl::remove] Unable to remove path=`%s`\n", normPath.c_str());
		return false;
	}
	return true;
}

bool VFATFSImpl::exists(const char* path) {
	String normPath;
	if (!normalizePath(path, normPath)) {
		ESPFAT_DEBUGV("[VFATFSImpl::exists] Invalid path\n");
		return false;
	}

	ESPFAT_DEBUGVV("[VFATFSImpl::exists] Normalized path '%s'\n", normPath.c_str());
	if (!normPath.equals("/")) {
		FRESULT res = f_stat(normPath.c_str(), NULL);
		ESPFAT_DEBUGVV("[VFATFSImpl::exists] result %d\n", res);
		return res == FR_OK;
	}
	return true;
}

bool VFATFSImpl::isDir(const char* path) {
	FILINFO stats;
	FRESULT res = f_stat(path, &stats);
	if (res != FR_OK) {
		ESPFAT_DEBUGV("[VFATFSImpl::isDir] Error %d\n", res);
		return false;
	}
	return (stats.fattrib & AM_DIR);
}

time_t VFATFSImpl::mtime(const char* path) {
	FILINFO stats;
	FRESULT res = f_stat(path, &stats);
	if (res != FR_OK) {
		ESPFAT_DEBUGV("[VFATFSImpl::mtime] Error %d\n", res);
		return 0;
	}

	time_t mtime;
	fattime2unixts(mtime, stats.ftime, stats.fdate);
	return mtime;
}

bool VFATFSImpl::begin() {
	if (_mounted) {
		return true;
	}
	return mount()? true : (format()? mount() : false);
}

void VFATFSImpl::end() {
	if (!_mounted) {
		return;
	}
	unmount();
}

bool VFATFSImpl::format() {
	bool wasMounted = _mounted;

	if (wasMounted) {
		if (!unmount()) {
			return false;
		}
	}

	ESPFAT_DEBUGVV("[VFATFSImpl::format] In progress...\n");
	FRESULT res = f_mkfs("/", FM_FAT|FM_SFD, 0, _fatfs.win, VFATFS_SECTOR_SIZE);
	if (res != FR_OK) {
		ESPFAT_DEBUGV("[VFATFSImpl::format] Error %d\n", res);
		return false;
	}
	ESPFAT_DEBUGVV("[VFATFSImpl::format] Done\n");

	if (wasMounted) {
		mount();
	}
	return true;
}

bool VFATFSImpl::mount() {
	FRESULT res = f_mount(&_fatfs, "/", 1);
	if (res != FR_OK) {
		ESPFAT_DEBUGV("[VFATFSImpl::mount] Error %d\n", res);
		return false;
	}
	_mounted = true;
	ESPFAT_DEBUGVV("[VFATFSImpl::mount] Success\n");
	return true;
}

bool VFATFSImpl::unmount() {
	FRESULT res = f_mount(NULL, "/", 0);
	if (res != FR_OK) {
		ESPFAT_DEBUGV("[VFATFSImpl::unmount] Error %d\n", res);
		return false;
	}
	_mounted = false;
	ESPFAT_DEBUGVV("[VFATFSImpl::unmount] Success\n");
	return true;
}

size_t VFATFSFileImpl::write(const uint8_t *buf, size_t size) {
	MUSTNOTCLOSE();

	UINT sz_out;
	FRESULT res = f_write(&_fd, buf, size, &sz_out);
	if (res != FR_OK) {
		ESPFAT_DEBUGV("[VFATFSFileImpl::write] Error %d\n", res);
		return -1;
	}
	return sz_out;
}

size_t VFATFSFileImpl::read(uint8_t* buf, size_t size) {
	MUSTNOTCLOSE();

	UINT sz_out;
	FRESULT res = f_read(&_fd, buf, size, &sz_out);
	if (res != FR_OK) {
		ESPFAT_DEBUGV("[VFATFSFileImpl::read] Error %d\n", res);
		return -1;
	}
	return sz_out;
}

void VFATFSFileImpl::flush() {
	MUSTNOTCLOSE();

	FRESULT res = f_sync(&_fd);
	if (res != FR_OK) {
		ESPFAT_DEBUGV("[VFATFSFileImpl::flush] Error %d\n", res);
	}
}

bool VFATFSFileImpl::seek(uint32_t pos, SeekMode mode) {
	MUSTNOTCLOSE();

	switch (mode) {
			case SeekSet:
			break;
			case SeekCur:
			{
				size_t curpos = position();
				pos+= curpos;
			} break;
			case SeekEnd:
			{
				size_t endpos = size();
				pos = endpos - pos;
			}
			default:
				ESPFAT_DEBUGV("[VFATFSFileImpl::seek] Unsupported seek mode: %d\n", mode);
				return false;
	}

	FRESULT res = f_lseek(&_fd, pos);
	if (res != FR_OK) {
		ESPFAT_DEBUGV("[VFATFSFileImpl::seek] Error %d\n", res);
		return false;
	}
	return true;
}

time_t VFATFSFileImpl::mtime() const {
	return _fs.mtime(_pathname.c_str());
}

void VFATFSFileImpl::close() {
	if (_fd.obj.fs) {
		FRESULT res = f_close(&_fd);
		if (res != FR_OK) {
			ESPFAT_DEBUGV("[VFATFSFileImpl::close] Error %d\n", res);
		}
		_fd = {0};
	}
}

FileImplPtr VFATFSDirImpl::openFile(OpenMode openMode, AccessMode accessMode) {
	const char* name = entryName();
	if (name)
		return openFile(name, openMode, accessMode);
	return FileImplPtr();
}

DirImplPtr VFATFSDirImpl::openDir() {
	const char* name = entryName();
	if (name)
		return openDir(name, false);
	return DirImplPtr();
}

FileImplPtr VFATFSDirImpl::openFile(const char *name, OpenMode openMode, AccessMode accessMode) {
	String entrypath = pathAppend(_pathname, name);
	return _fs.open(entrypath.c_str(), openMode, accessMode);
}

DirImplPtr VFATFSDirImpl::openDir(const char *name, bool create) {
	String entrypath = pathAppend(_pathname, name);
	return _fs.openDir(entrypath.c_str(), create);
}

bool VFATFSDirImpl::remove() {
	const char* name = entryName();
	if (name)
		return remove(name);
	return false;
}

bool VFATFSDirImpl::remove(const char *name) {
	String entrypath = pathAppend(_pathname, name);
	return _fs.remove(entrypath.c_str());
}

time_t VFATFSDirImpl::entryMtime() const {
	if (entryStats.fname[0]) {
		time_t time;
		fattime2unixts(time, entryStats.ftime, entryStats.fdate);
		return time;
	}
	return 0;
}

bool VFATFSDirImpl::isEntryDir() const {
	if (entryStats.fname[0]) {
		return (entryStats.fattrib & AM_DIR);
	}
	return false;
}

bool VFATFSDirImpl::isDir(const char* path) const {
	String entrypath = pathAppend(_pathname, path);
	return _fs.isDir(entrypath.c_str());
}

time_t VFATFSDirImpl::mtime() const {
	return _fs.mtime(_pathname.c_str());
}

bool VFATFSDirImpl::next(bool reset) {
	if (reset) f_readdir(&_fd, NULL);
	FRESULT res = f_readdir(&_fd, &entryStats);
	if (res != FR_OK) {
		ESPFAT_DEBUGV("[VFATFSDirImpl::next] Error %d\n", res);
		return false;
	}
	return entryStats.fname[0];
}

void VFATFSDirImpl::close() {
	FRESULT res = f_closedir(&_fd);
	if (res != FR_OK) {
		ESPFAT_DEBUGV("[VFATFSDirImpl::close] Error %d\n", res);
	}
}
