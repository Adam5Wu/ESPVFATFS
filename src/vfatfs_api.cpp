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

#define VFATFS_DIR_MAXNEST 8

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

time_t fattime2unixts(uint16_t time, uint16_t date) {
	time_t ret;
	fattime2unixts(ret, time, date);
	return ret;
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

bool normalizePath(const char* in, uint8_t partno, String& out) {
	ESPFAT_DEBUGVV("[VFATFS##normalizePath] Input '%s'\n", in);

	// All path must start from root
	if (*in != '/') {
		ESPFAT_DEBUGV("[VFATFS##normalizePath] Not from root\n");
		return false;
	}

	String buf(in);
	char* toks[VFATFS_DIR_MAXNEST] = {0};
	uint8_t idx = 0;

	char* ptr = const_cast<char*>(buf.c_str());
	while ((idx < VFATFS_DIR_MAXNEST) && ptr) {
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
	out = String(partno)+':';
	uint8_t i = 0;
	do {
		out.concat('/');
		if (i >= idx) break;
		out.concat(toks[i++]);
	} while (i < idx);

	ESPFAT_DEBUGVV("[VFATFS##normalizePath] Output '%s'\n",out.c_str());
	return true;
}

#include "fatfs/diskio.h"

#ifdef ESP8266

#include <spi_flash.h>

// these symbols should be defined in the linker script for each flash layout
extern "C" uint32_t _SPIFFS_start;
extern "C" uint32_t _SPIFFS_end;

#define VFATFS_PHYS_ADDR	\
	((uint32_t) (&_SPIFFS_start) - 0x40200000)
#define VFATFS_PHYS_SIZE	\
	((uint32_t) (&_SPIFFS_end) - (uint32_t) (&_SPIFFS_start))

/*-----------------------------------------------------------------------*/
/* Initialize a Drive																										*/
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (
	BYTE pdrv					/* Physical drive nmuber (0..) */
) {
	if (pdrv != 0)
		return STA_NODISK;
	return 0;
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
	int32_t ret = spi_flash_read(addr, (uint32*)buff, size);
	return ret == SPI_FLASH_RESULT_OK? RES_OK : RES_ERROR;
}

/*-----------------------------------------------------------------------*/
/* Write Sector(s)																											*/
/*-----------------------------------------------------------------------*/

DRESULT disk_write (
	BYTE pdrv,			/* Physical drive nmuber (0..) */
	const BYTE *buff, 	/* Data to be written */
	DWORD sector,		/* Sector address (LBA) */
	UINT count			/* Number of sectors to write (1..128) */
) {
	if (pdrv != 0)
		return RES_PARERR;

	uint32_t addr = VFATFS_PHYS_ADDR + sector * VFATFS_SECTOR_SIZE;
	uint32_t size = count * VFATFS_SECTOR_SIZE;

	ESPFAT_DEBUGVV("[VFATFS##disk_write] E 0x%lX 0x%X\n", addr, size);
	uint32_t erase_base = VFATFS_PHYS_ADDR/VFATFS_SECTOR_SIZE + sector;
	while (count--) {
		int32_t ret = spi_flash_erase_sector(erase_base+count);
		if (ret != SPI_FLASH_RESULT_OK) return RES_ERROR;
	}

	ESPFAT_DEBUGVV("[VFATFS##disk_write] W 0x%lX 0x%X\n", addr, size);
	int32_t ret = spi_flash_write(addr, (uint32*)buff, size);
	return ret == SPI_FLASH_RESULT_OK? RES_OK : RES_ERROR;
}

/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions																							*/
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl (
	BYTE pdrv,				/* Physical drive nmuber (0..) */
	BYTE cmd,				/* Control code */
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
			// erase block size in units of sector size
			*((DWORD*)buff) = VFATFS_SECT_PER_PHYS;
			return RES_OK;

		case CTRL_TRIM:
			DWORD *range = (DWORD*)buff;
			uint32_t erase_base = VFATFS_PHYS_ADDR/VFATFS_SECTOR_SIZE + range[0];
			uint16_t count = range[1]-range[0];
			ESPFAT_DEBUGV("[VFATF::disk_ioctl] Trim #%d of %d sectors!\n",
				erase_base, count);
			while (count--) {
				int32_t ret = spi_flash_erase_sector(erase_base+count);
				if (ret != SPI_FLASH_RESULT_OK) return RES_ERROR;
			}
			return RES_OK;
	}

	ESPFAT_DEBUGV("[VFATFS##disk_ioctl] Unhandled %d\n", cmd);
	return RES_PARERR;
}

/*-----------------------------------------------------------------------*/
/* Heap Memory Functions																							*/
/*-----------------------------------------------------------------------*/

void* ff_memalloc (UINT msize)		/* Allocate memory block */
{ return malloc(msize); }

void ff_memfree (void* mblock)		/* Free memory block */
{ return free(mblock); }

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

#include <utility>

using namespace fs;

// Partitions

PARTITION VolToPart[FF_VOLUMES] = {
	{0, 1},     /* "0:" ==> Physical drive 0, 1st partition */
	{0, 2},     /* "1:" ==> Physical drive 0, 2nd partition */
	{0, 3},     /* "2:" ==> Physical drive 0, 3rd partition */
	{0, 4}      /* "3:" ==> Physical drive 0, 4th partition */
};

DWORD VFATFSPartitions::_size[4] = { 100, 0, 0, 0 };
uint8_t VFATFSPartitions::_opencnt = 0;

bool VFATFSPartitions::create() {
	if (_opencnt) {
		ESPFAT_DEBUGV("[VFATFSPartitions::create] There are %d mounted partitions!\n",
			_opencnt);
		return false;
	}

	ESPFAT_DEBUGVV("[VFATFSPartitions::create] In progress...\n");
	FRESULT res = f_fdisk(0, _size, nullptr);
	if (res != FR_OK) {
		ESPFAT_DEBUGV("[VFATFSPartitions::create] Error %d\n", res);
		return false;
	}
	ESPFAT_DEBUGVV("[VFATFSPartitions::create] Done\n");

	return true;
}

bool VFATFSPartitions::config(uint8_t A, uint8_t B, uint8_t C, uint8_t D) {
	if (_opencnt) {
		ESPFAT_DEBUGV("[VFATFSPartitions::config] There are %d mounted partitions!\n",
			_opencnt);
		return false;
	}

	if (A + B + C + D > 100) {
		ESPFAT_DEBUGV("[VFATFSPartitions::config] Invalid partition sizes "
			"(%d%% + %d%% + %d%% + %d%% > 100%%)\n", A, B, C, D);
		return false;
	}
	if (A + B + C + D < 100) {
		ESPFAT_DEBUG("[VFATFSPartitions::config] Partitions do not fill disk "
		"(%d%% + %d%% + %d%% + %d%% < 100%%)\n", A, B, C, D);
	}

	_size[0] = A; _size[1] = B; _size[2] = C; _size[3] = D;
	return true;
}

// FS

#define CSTR_NODRV(s) s.c_str()+2

bool VFATFSImpl::mount() {
	String DrvRoot(_partno);
	DrvRoot.concat(":/",2);
	ESPFAT_DEBUGVV("[VFATFSImpl::mount] Mount '%s' in progress...\n",
		DrvRoot.c_str());
	FRESULT res = f_mount(&_fatfs, DrvRoot.c_str(), 1);
	if (res != FR_OK) {
		ESPFAT_DEBUGV("[VFATFSImpl::mount] Error %d\n", res);
		return false;
	}
	_mounted = true;
	uint8_t mountCnt = ++VFATFSPartitions::_opencnt;
	ESPFAT_DEBUGVV("[VFATFSImpl::mount] Mounted %s (#%d)\n",
		DrvRoot.c_str(), mountCnt);
	return true;
}

bool VFATFSImpl::unmount() {
	String DrvRoot(_partno);
	DrvRoot.concat(":/",2);
	ESPFAT_DEBUGVV("[VFATFSImpl::unmount] Unmount '%s' in progress...\n",
		DrvRoot.c_str());
	FRESULT res = f_mount(NULL, DrvRoot.c_str(), 0);
	if (res != FR_OK) {
		ESPFAT_DEBUGV("[VFATFSImpl::unmount] Error %d\n", res);
		return false;
	}
	_mounted = false;
	uint8_t mountCnt = --VFATFSPartitions::_opencnt;
	ESPFAT_DEBUGVV("[VFATFSImpl::unmount] Unmounted %s (#%d)\n",
		DrvRoot.c_str(), mountCnt);
	return true;
}

bool VFATFSImpl::begin() {
	if (_mounted) return true;

	if (!VFATFSPartitions::_size[_partno]) {
		ESPFAT_DEBUGV("[VFATFSImpl::begin] Partition #%d not enabled\n",
			_partno);
		return false;
	}

	if (mount()) return true;
	if (format()) return mount();

	if (VFATFSPartitions::create()) {
		if (mount()) return true;
		if (format()) return mount();
	}
	return false;
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

	String DrvRoot(_partno);
	DrvRoot.concat(":/",2);
	ESPFAT_DEBUGVV("[VFATFSImpl::format] Format '%s' in progress...\n",
		DrvRoot.c_str());
	FRESULT res = f_mkfs(DrvRoot.c_str(), FM_FAT, 0, _fatfs.win, FF_MAX_SS);
	if (res != FR_OK) {
		ESPFAT_DEBUGV("[VFATFSImpl::format] Error %d\n", res);
		return false;
	}
	ESPFAT_DEBUGVV("[VFATFSImpl::format] Done %s\n", DrvRoot.c_str());

	if (wasMounted) {
		mount();
	}
	return true;
}

bool VFATFSImpl::info(FSInfo& info) const {
	info.maxOpenFiles = 10; // Give a reasonable number
	info.maxPathLength = 260; // MAX_PATH

	uint32_t totalBytes, usedBytes;
	FATFS *fatfs;
	DWORD nclst;
	String DrvRoot(_partno);
	DrvRoot.concat(":/",2);
	FRESULT res = f_getfree(DrvRoot.c_str(), &nclst, &fatfs);
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

bool VFATFSImpl::exists(const char* path) const {
	String normPath;
	if (!normalizePath(path, _partno, normPath)) {
		ESPFAT_DEBUGV("[VFATFSImpl::exists] Invalid path\n");
		return false;
	}

	ESPFAT_DEBUGVV("[VFATFSImpl::exists] Normalized path '%s'\n",
		normPath.c_str());
	if (normPath.length() > 3) {
		FRESULT res = f_stat(normPath.c_str(), NULL);
		ESPFAT_DEBUGVV("[VFATFSImpl::exists] result %d\n", res);
		return res == FR_OK;
	}
	return true;
}

bool VFATFSImpl::isDir(const char* path) const {
	FILINFO stats;
	FRESULT res = f_stat(path, &stats);
	if (res != FR_OK) {
		ESPFAT_DEBUGV("[VFATFSImpl::isDir] Error %d\n", res);
		return false;
	}
	return (stats.fattrib & AM_DIR);
}

size_t VFATFSImpl::size(const char* path) const {
	FILINFO stats;
	FRESULT res = f_stat(path, &stats);
	if (res != FR_OK) {
		ESPFAT_DEBUGV("[VFATFSImpl::size] Error %d\n", res);
		return 0;
	}

	return stats.fsize;
}

time_t VFATFSImpl::mtime(const char* path) const {
	FILINFO stats;
	FRESULT res = f_stat(path, &stats);
	if (res != FR_OK) {
		ESPFAT_DEBUGV("[VFATFSImpl::mtime] Error %d\n", res);
		return 0;
	}

	return fattime2unixts(stats.ftime, stats.fdate);
}

FileImplPtr VFATFSImpl::openFile(const char* path, OpenMode openMode,
	AccessMode accessMode) {
	String normPath;
	if (!normalizePath(path, _partno,  normPath)) {
		ESPFAT_DEBUGV("[VFATFSImpl::openFile] Invalid path\n");
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

	FIL fd{0}; // Note: enable FS_TINY, or have >5K free stack space!
	FRESULT res = f_open(&fd, normPath.c_str(), open_mode);
	if (res != FR_OK) {
		ESPFAT_DEBUGV("[VFATFSImpl::openFile] Error %d\n", res);
		return FileImplPtr();
	}
	return std::make_shared<VFATFSFileImpl>(*this, fd, std::move(normPath));
}

DirImplPtr VFATFSImpl::openDir(const char* path, bool create) {
	String normPath;
	if (!normalizePath(path, _partno, normPath)) {
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
	return std::make_shared<VFATFSDirImpl>(*this, fd, std::move(normPath));
}

bool VFATFSImpl::remove(const char* path) {
	String normPath;
	if (!normalizePath(path, _partno, normPath)) {
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

bool VFATFSImpl::rename(const char* pathFrom, const char* pathTo) {
	String normPathFrom;
	if (!normalizePath(pathFrom, _partno, normPathFrom)) {
		ESPFAT_DEBUGV("[VFATFSImpl::rename] Invalid path from\n");
		return false;
	}
	String normPathTo;
	if (!normalizePath(pathTo, _partno, normPathTo)) {
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

// File

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

bool VFATFSFileImpl::truncate() {
	MUSTNOTCLOSE();

	FRESULT res = f_truncate(&_fd);
	if (res != FR_OK) {
		ESPFAT_DEBUGV("[VFATFSFileImpl::truncate] Error %d\n", res);
		return false;
	}
	return true;
}

time_t VFATFSFileImpl::mtime() const {
	return _fs.mtime(CSTR_NODRV(_pathname));
}

bool VFATFSFileImpl::remove() {
	MUSTNOTCLOSE();
	close();

	return _fs.remove(CSTR_NODRV(_pathname));
}

bool VFATFSFileImpl::rename(const char *nameTo) {
	MUSTNOTCLOSE();
	close();

	String targetPath = pathGetParent(_pathname);
	pathAppend(targetPath, nameTo);
	if (_fs.rename(CSTR_NODRV(_pathname), CSTR_NODRV(targetPath))) {
		_pathname = std::move(targetPath);
		return true;
	}
	return false;
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

// Dir

FileImplPtr VFATFSDirImpl::openFile(const char *name, OpenMode openMode, AccessMode accessMode) {
	String entrypath = pathJoin(_pathname, name);
	return _fs.openFile(CSTR_NODRV(entrypath), openMode, accessMode);
}

DirImplPtr VFATFSDirImpl::openDir(const char *name, bool create) {
	String entrypath = pathJoin(_pathname, name);
	return _fs.openDir(CSTR_NODRV(entrypath), create);
}

bool VFATFSDirImpl::exists(const char *name) const {
	String entrypath = pathJoin(_pathname, name);
	return _fs.exists(CSTR_NODRV(entrypath));
}

bool VFATFSDirImpl::isDir(const char* name) const {
	String entrypath = pathJoin(_pathname, name);
	return _fs.isDir(CSTR_NODRV(entrypath));
}

size_t VFATFSDirImpl::size(const char* name) const {
	String entrypath = pathJoin(_pathname, name);
	return _fs.size(CSTR_NODRV(entrypath));
}

time_t VFATFSDirImpl::mtime(const char* name) const {
	String entrypath = pathJoin(_pathname, name);
	return _fs.mtime(CSTR_NODRV(entrypath));
}

bool VFATFSDirImpl::remove(const char *name) {
	String entrypath = pathJoin(_pathname, name);
	return _fs.remove(CSTR_NODRV(entrypath));
}

bool VFATFSDirImpl::rename(const char* nameFrom, const char* nameTo) {
	String entrypathFrom = pathJoin(_pathname, nameFrom);
	String entrypathTo = pathJoin(_pathname, nameTo);
	return _fs.rename(CSTR_NODRV(entrypathFrom), CSTR_NODRV(entrypathTo));
}

time_t VFATFSDirImpl::entryMtime() const {
	if (entryStats.fname[0]) {
		return fattime2unixts(entryStats.ftime, entryStats.fdate);
	}
	return 0;
}

bool VFATFSDirImpl::isEntryDir() const {
	if (entryStats.fname[0]) {
		return (entryStats.fattrib & AM_DIR);
	}
	return false;
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

FileImplPtr VFATFSDirImpl::openEntryFile(OpenMode openMode, AccessMode accessMode) {
	const char* name = entryName();
	return name ? openFile(name, openMode, accessMode) : FileImplPtr();
}

DirImplPtr VFATFSDirImpl::openEntryDir() {
	const char* name = entryName();
	return name ? openDir(name, false) : DirImplPtr();
}

bool VFATFSDirImpl::removeEntry() {
	const char* name = entryName();
	return name ? remove(name) : false;
}

bool VFATFSDirImpl::renameEntry(const char* nameTo) {
	const char* name = entryName();
	return name ? rename(name, nameTo) : false;
}

time_t VFATFSDirImpl::mtime() const {
	return _fs.mtime(CSTR_NODRV(_pathname));
}

void VFATFSDirImpl::close() {
	FRESULT res = f_closedir(&_fd);
	if (res != FR_OK) {
		ESPFAT_DEBUGV("[VFATFSDirImpl::close] Error %d\n", res);
	}
}
