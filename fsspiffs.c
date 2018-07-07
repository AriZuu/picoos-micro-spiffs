/*
 * Copyright (c) 2016, Ari Suutari <ari@stonepile.fi>.
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote
 *     products derived from this software without specific prior written
 *     permission. 
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT,  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <picoos.h>
#include <picoos-u.h>
#include <picoos-u-spiffs.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

#if !defined(UOSCFG_SPIFFS) || UOSCFG_SPIFFS == 0
#error UOSCFG_SPIFFS must be > 0
#endif

#if !defined(UOSCFG_MAX_OPEN_FILES) || UOSCFG_MAX_OPEN_FILES == 0
#error UOSCFG_MAX_OPEN_FILES must be > 0
#endif

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

/*
 * SPIFFS fs. Wraps actual implementation
 * from https://github.com/pellepl/spiffs inside
 * picoos-micro FS api.
 */

#include "spiffs.h"

typedef struct {

  UosFS base;
  spiffs fs;
  spiffs_config cfg;
  spiflash_t* flash;
  POSMUTEX_t lock;
  u8_t* workBuf;
  u8_t* cacheBuf;
  u8_t* fdBuf;
} SpiFS;

UOS_BITTAB_TABLE(SpiFS, UOSCFG_MAX_MOUNT);

static SpiFSBittab        mountedFs;

static int spiffsInit(const UosFS*);
static int spiffsOpen(const UosFS* mount, UosFile* file, const char *name, int flags, int mode);
static int spiffsClose(UosFile* file);
static int spiffsRead(UosFile* file, char* buf, int max);
static int spiffsWrite(UosFile* file, const char* buf, int max);
static int spiffsUnlink(const UosFS* mount, const char* name);
static int spiffsSync(UosFile* file);
static int spiffsStat(const UosFS* fs, const char* fn, UosFileInfo* st);
static int spiffsFStat(UosFile* file, UosFileInfo* st);
static int spiffsSeek(UosFile* file, int offset, int whence);

static const UosFSConf fsConf = {

  .init   = spiffsInit,
  .open   = spiffsOpen,
  .unlink = spiffsUnlink,
  .stat   = spiffsStat,
};

static const UosFileConf fileConf = {

  .close  = spiffsClose,
  .read   = spiffsRead,
  .write  = spiffsWrite,
  .sync   = spiffsSync,
  .fstat  = spiffsFStat,
  .lseek  = spiffsSeek
};

/*
 * SPIFFS locking.
 */
void uosSpiffsMutexLock(spiffs* fsImpl)
{
  SpiFS* fs = (SpiFS*)fsImpl->user_data;

  nosMutexLock(fs->lock);
}

void uosSpiffsMutexUnlock(spiffs* fsImpl)
{
  SpiFS* fs = (SpiFS*)fsImpl->user_data;

  nosMutexUnlock(fs->lock);
}

/* 
 * Interface to flash driver.
 */
static s32_t spiFlashRead(spiffs* fsImpl, u32_t addr, u32_t size, u8_t * dst)
{
  SpiFS* fs = (SpiFS*)fsImpl->user_data;
  spiflash_t* spif = fs->flash;

  SPIFLASH_read(spif, addr, size, dst);
  return SPIFFS_OK;
}

static s32_t spiFlashWrite(spiffs* fsImpl, u32_t addr, u32_t size, u8_t * src)
{
  SpiFS* fs = (SpiFS*)fsImpl->user_data;
  spiflash_t* spif = fs->flash;

  SPIFLASH_write(spif, addr, size, src);
  return SPIFFS_OK;
}

static s32_t spiFlashErase(spiffs* fsImpl, u32_t addr, u32_t size)
{
  SpiFS* fs = (SpiFS*)fsImpl->user_data;
  spiflash_t* spif = fs->flash;

  SPIFLASH_erase(spif, addr, size);
  return SPIFFS_OK;
}

/*
 * Mount and format flash fs if necessary.
 */

static int spiffsInit(const UosFS* fs)
{
  SpiFS* m = (SpiFS*) fs;
  uint32_t fdBufSize;
  uint32_t cacheBufSize;

/*
 * Allocate fs buffers.
 */

//XXX: log_page_size is used inside SPIFFS_buffer_bytes_for_cache
  m->fs.cfg.log_page_size = m->cfg.log_page_size;
  cacheBufSize = SPIFFS_buffer_bytes_for_cache(&m->fs, 4);
  fdBufSize = SPIFFS_buffer_bytes_for_filedescs(&m->fs, UOSCFG_SPIFFS);

  m->workBuf  = malloc(m->cfg.log_page_size * 2);
  m->cacheBuf = malloc(cacheBufSize);
  m->fdBuf = malloc(fdBufSize);

  m->lock = nosMutexCreate(0, "spiffs*");

  m->fs.user_data = (void*)fs;

  m->cfg.hal_read_f  = spiFlashRead;
  m->cfg.hal_write_f = spiFlashWrite;
  m->cfg.hal_erase_f = spiFlashErase;

  int res = SPIFFS_mount(&m->fs,
                         &m->cfg,
                         m->workBuf,
                         m->fdBuf,
                         fdBufSize,
                         m->cacheBuf,
                         cacheBufSize,
                         0);

  if (res == SPIFFS_ERR_NOT_A_FS) {

    printf("spiffs: formatting required\n");

    res = SPIFFS_format(&m->fs);
    printf("format res: %d err %" PRId32 "\n", res, m->fs.err_code);

    res = SPIFFS_mount(&m->fs,
                       &m->cfg,
                       m->workBuf,
                       m->fdBuf,
                       fdBufSize,
                       m->cacheBuf,
                       cacheBufSize,
                       0);

  }

  if (res < 0) {

    printf("Spiffs mount error: %" PRId32 "\n", m->fs.err_code);
    errno = EIO;
    return -1;
  }

  uint32_t total, used;

  res = SPIFFS_info(&m->fs, &total, &used);
  if (res >= 0)
    printf("Spiffs mounted. Total size %" PRIu32 ", used %" PRIu32 "\n", total, used);
  else
    printf("SPIFFS_info failure %" PRId32 "\n", res);

  return 0;
}

int uosMountSpiffs(const char* mountPoint, UosFlashDev* dev, spiffs_config* cfg)
{
  int slot = UOS_BITTAB_ALLOC(mountedFs);
  if (slot == -1) {

#if NOSCFG_FEATURE_PRINTF
    nosPrintf("spiffs: mount table full\n");
#endif
    errno = ENOSPC;
    return -1;
  }

  SpiFS* m = UOS_BITTAB_ELEM(mountedFs, slot);

  memcpy(&m->cfg, cfg, sizeof(m->cfg));
  m->flash = &dev->spif;
  m->base.mountPoint = mountPoint;
  m->base.cf = &fsConf;

  return uosMount(&m->base);
}

static int spiffsOpen(const UosFS* mount, UosFile* file, const char *name, int flags, int mode)
{
  P_ASSERT("spiffsOpen", file->fs->cf == &fsConf);

  SpiFS* m = (SpiFS*) file->fs;

/*
 * SPIFFS does not have directories.
 */
  if (strchr(name, '/') != NULL) {
 
    errno = ENOENT;
    return -1;
  }

  file->cf = &fileConf;

  spiffs_flags spiflags = 0;

  switch (flags & O_ACCMODE) {
  case O_RDONLY:
    spiflags |= SPIFFS_RDONLY;
    break;

  case O_WRONLY:
    spiflags |= SPIFFS_WRONLY;
    break;

  case O_RDWR:
    spiflags |= SPIFFS_RDWR;
    break;
  }

  if (flags & O_CREAT)
    spiflags |= SPIFFS_CREAT;
  else if (flags & O_TRUNC)
    spiflags |= SPIFFS_TRUNC;

  if (flags & O_APPEND)
    spiflags |= SPIFFS_APPEND;

  spiffs_file fd;

  fd = SPIFFS_open(&m->fs, name, spiflags, 0);
  if (fd < 0) {

    s32_t err = SPIFFS_errno(&m->fs);
    if (err == SPIFFS_ERR_NOT_FOUND)
      errno = ENOENT;
    else
      errno = EIO;

    return -1;
  }

  file->fsPrivFd = fd;
  return 0;
}

static int spiffsClose(UosFile* file)
{
  P_ASSERT("spiffsClose", file->fs->cf == &fsConf);

  SpiFS* m = (SpiFS*) file->fs;
  spiffs_file fd = file->fsPrivFd;
  if (SPIFFS_close(&m->fs, fd) < 0) {

    errno = EIO;
    return -1;
  }

  return 0;
}

static int spiffsRead(UosFile* file, char *buf, int len)
{
  P_ASSERT("spiffsRead", file->fs->cf == &fsConf);

  SpiFS* m = (SpiFS*) file->fs;
  spiffs_file fd = file->fsPrivFd;

  int retLen;

  retLen = SPIFFS_read(&m->fs, fd, buf, len);
  if (retLen < 0 && m->fs.err_code == SPIFFS_ERR_END_OF_OBJECT)
    return 0;

  if (retLen == -1) {

    errno = EIO;
    return -1;
  }

  return retLen;
}

static int spiffsWrite(UosFile* file, const char *buf, int len)
{
  P_ASSERT("spiffsWrite", file->fs->cf == &fsConf);

  SpiFS* m = (SpiFS*) file->fs;
  spiffs_file fd = file->fsPrivFd;

  int retLen;

  retLen = SPIFFS_write(&m->fs, fd, (char*) buf, len);
  if (retLen < 0) {

    errno = EIO;
    return -1;
  }

  return retLen;
}

static int spiffsUnlink(const UosFS* fs, const char* fn)
{
  SpiFS* m = (SpiFS*) fs;

  if (SPIFFS_remove(&m->fs, fn) < 0) {

    s32_t err = SPIFFS_errno(&m->fs);
    if (err == SPIFFS_ERR_NOT_FOUND)
      errno = ENOENT;
    else
      errno = EIO;

    return -1;
  }

  return 0;
}

static int spiffsSync(UosFile* file)
{
  P_ASSERT("spiffsSync", file->fs->cf == &fsConf);

  SpiFS* m = (SpiFS*) file->fs;
  spiffs_file fd = file->fsPrivFd;

  if (SPIFFS_fflush(&m->fs, fd) < 0) {

    errno = EIO;
    return -1;
  }

  return 0;
}

static int spiffsStat(const UosFS* fs, const char* fn, UosFileInfo* st)
{
  SpiFS* m = (SpiFS*) fs;
  spiffs_stat sst;

  if (SPIFFS_stat(&m->fs, fn, &sst) < 0) {

    s32_t err = SPIFFS_errno(&m->fs);
    if (err == SPIFFS_ERR_NOT_FOUND)
      errno = ENOENT;
    else
      errno = EIO;

    return -1;
  }

  st->isDir = false;
  st->size  = sst.size;
  return 0;
}

static int spiffsFStat(UosFile* file, UosFileInfo* st)
{
  P_ASSERT("spiffsRead", file->fs->cf == &fsConf);

  SpiFS* m = (SpiFS*) file->fs;
  spiffs_file fd = file->fsPrivFd;
  spiffs_stat sst;

  if (SPIFFS_fstat(&m->fs, fd, &sst) < 0) {

    s32_t err = SPIFFS_errno(&m->fs);
    if (err == SPIFFS_ERR_NOT_FOUND)
      errno = ENOENT;
    else
      errno = EIO;

    return -1;
  }

  st->isDir = false;
  st->size  = sst.size;
  return 0;
}

static int spiffsSeek(UosFile* file, int offset, int whence)
{
  P_ASSERT("spiffsSeek", file->fs->cf == &fsConf);

  SpiFS* m = (SpiFS*) file->fs;
  spiffs_file fd = file->fsPrivFd;
  int w;

  switch (whence) {
  case SEEK_SET:
    w = SPIFFS_SEEK_SET;
    break;

  case SEEK_CUR:
    w = SPIFFS_SEEK_CUR;
    break;

  case SEEK_END:
    w = SPIFFS_SEEK_END;
    break;

  default:
    errno = EINVAL;
    return -1;
    break;
  }

  if (SPIFFS_lseek(&m->fs, fd, offset, w) < 0) {

    errno = EIO;
    return -1;
  }

  return 0;
}

