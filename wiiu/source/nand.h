#pragma once
#include "platform.h"

/* The vWii's NAND (Wii mode's system memory), mounted through the Mocha payload
 * that Aroma runs. It holds what the Wii version reads from its own NAND: the
 * Wii Number and mail settings, the Miis, the address book, the Message Board
 * and the WiiConnect24 send box. The Wii U's own storage (for its Miis) is
 * mounted the same way.
 *
 * The shared code talks to it with the libogc ISFS calls it uses on the Wii;
 * here they are thin wrappers over stdio on the mounted volume. */

void nand_init(void);
void nand_shutdown(void);
int nand_available(void); /* the vWii NAND is mounted */
const char *nand_error(void); /* why it isn't, for messages */

/* A whole file from the Wii U's own system storage (MLC), e.g.
 * "/usr/save/00050010/1004a100/user/common/db/FFL_ODB.dat"; malloc'd, or NULL */
u8 *mlc_read(const char *path, u32 *size, u32 max);

#define ISFS_OPEN_READ 1
#define ISFS_OPEN_WRITE 2
#define ISFS_OPEN_RW 3
#define ISFS_EINVAL -4
#define ISFS_ENOENT -106

typedef struct {
    u32 file_length;
    u32 file_pos;
} fstats;

s32 ISFS_Initialize(void);
s32 ISFS_Deinitialize(void);
s32 ISFS_Open(const char *path, u8 mode);
s32 ISFS_Close(s32 fd);
s32 ISFS_Read(s32 fd, void *buf, u32 len);
s32 ISFS_Write(s32 fd, const void *buf, u32 len);
s32 ISFS_Seek(s32 fd, s32 where, s32 whence);
s32 ISFS_GetFileStats(s32 fd, fstats *st);
s32 ISFS_CreateFile(const char *path, u8 attr, u8 owner, u8 group, u8 other);
s32 ISFS_Delete(const char *path);
