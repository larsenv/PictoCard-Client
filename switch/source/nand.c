#include <stdio.h>
#include "nand.h"

/* The Switch has no Wii mode: the NAND the Wii and Wii U versions read (the Wii
 * Number, Miis, friends, the Message Board and the send box) isn't there, and
 * the shared code just sees an empty one. */

void nand_init(void) {}
void nand_shutdown(void) {}

int nand_available(void) { return 0; }
const char *nand_error(void) { return "A Switch has no Wii mode, so there is no Wii Number or Message Board to read."; }

u8 *mlc_read(const char *path, u32 *size, u32 max)
{
    (void)path;
    (void)max;
    *size = 0;
    return NULL;
}

s32 ISFS_Initialize(void) { return ISFS_EINVAL; }
s32 ISFS_Deinitialize(void) { return 0; }
s32 ISFS_Open(const char *path, u8 mode) { (void)path; (void)mode; return ISFS_ENOENT; }
s32 ISFS_Close(s32 fd) { (void)fd; return ISFS_EINVAL; }
s32 ISFS_Read(s32 fd, void *buf, u32 len) { (void)fd; (void)buf; (void)len; return ISFS_EINVAL; }
s32 ISFS_Write(s32 fd, const void *buf, u32 len) { (void)fd; (void)buf; (void)len; return ISFS_EINVAL; }
s32 ISFS_Seek(s32 fd, s32 where, s32 whence) { (void)fd; (void)where; (void)whence; return ISFS_EINVAL; }
s32 ISFS_GetFileStats(s32 fd, fstats *st) { (void)fd; (void)st; return ISFS_EINVAL; }
s32 ISFS_CreateFile(const char *path, u8 attr, u8 owner, u8 group, u8 other)
{
    (void)path; (void)attr; (void)owner; (void)group; (void)other;
    return ISFS_EINVAL;
}
s32 ISFS_Delete(const char *path) { (void)path; return ISFS_EINVAL; }
