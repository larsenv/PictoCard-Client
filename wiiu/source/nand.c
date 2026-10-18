#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <mocha/mocha.h>
#include "nand.h"
#include "util.h"

/* The vWii NAND is the "slccmpt01" device; Aroma's Mocha lets an app mount it */
#define MOUNT "slccmpt01"
#define MLC_MOUNT "pc_mlc01" /* the Wii U's own storage, already mounted by the system */
#define MAX_FILES 8

static int mocha_ready, mounted, mlc_mounted;
static char error[96] = "PictoCard needs Aroma with its Mocha payload to read Wii mode's data.";
static FILE *files[MAX_FILES];
static int writable[MAX_FILES];

static int mocha_init(void)
{
    if (!mocha_ready) {
        MochaUtilsStatus r = Mocha_InitLibrary();
        if (r != MOCHA_RESULT_SUCCESS) {
            snprintf(error, sizeof error, "Wii mode's data needs Aroma with its Mocha payload (error %d).", (int)r);
            return 0;
        }
        mocha_ready = 1;
    }
    return 1;
}

void nand_init(void)
{
    if (mounted || !mocha_init()) return;
    MochaUtilsStatus r = Mocha_MountFS(MOUNT, "/dev/slccmpt01", "/vol/storage_slccmpt01");
    if (r != MOCHA_RESULT_SUCCESS) r = Mocha_MountFS(MOUNT, NULL, "/vol/storage_slccmpt01"); /* already mounted */
    if (r != MOCHA_RESULT_SUCCESS) {
        snprintf(error, sizeof error, "Could not open Wii mode's system memory (error %d).", (int)r);
        return;
    }
    mounted = 1;
}

void nand_shutdown(void)
{
    for (int i = 0; i < MAX_FILES; i++) ISFS_Close(i);
    if (mounted) Mocha_UnmountFS(MOUNT);
    if (mlc_mounted) Mocha_UnmountFS(MLC_MOUNT);
    if (mocha_ready) Mocha_DeInitLibrary();
    mounted = mlc_mounted = mocha_ready = 0;
}

u8 *mlc_read(const char *path, u32 *size, u32 max)
{
    if (!mlc_mounted) {
        if (!mocha_init() || Mocha_MountFS(MLC_MOUNT, NULL, "/vol/storage_mlc01") != MOCHA_RESULT_SUCCESS) return NULL;
        mlc_mounted = 1;
    }
    char full[160];
    snprintf(full, sizeof full, MLC_MOUNT ":%s", path);
    size_t len = 0;
    u8 *data = read_file(full, &len, max);
    *size = (u32)len;
    return data;
}

int nand_available(void) { return mounted; }
const char *nand_error(void) { return error; }

/* "/shared2/wc24/nwc24msg.cfg" -> "slccmpt01:/shared2/wc24/nwc24msg.cfg" */
static void host_path(const char *path, char *out, size_t len)
{
    snprintf(out, len, MOUNT ":%s", path);
}

static FILE *file(s32 fd) { return fd >= 0 && fd < MAX_FILES ? files[fd] : NULL; }

s32 ISFS_Initialize(void) { return mounted ? 0 : ISFS_EINVAL; }
s32 ISFS_Deinitialize(void) { return 0; }

s32 ISFS_Open(const char *path, u8 mode)
{
    if (!mounted) return ISFS_EINVAL;
    int fd = 0;
    while (fd < MAX_FILES && files[fd]) fd++;
    if (fd == MAX_FILES) return ISFS_EINVAL;
    char full[128];
    host_path(path, full, sizeof full);
    /* ISFS writes over an existing file in place; it never creates or truncates */
    FILE *f = fopen(full, mode & ISFS_OPEN_WRITE ? "r+b" : "rb");
    if (!f) return errno == ENOENT ? ISFS_ENOENT : -errno;
    files[fd] = f;
    writable[fd] = (mode & ISFS_OPEN_WRITE) != 0;
    return fd;
}

s32 ISFS_Close(s32 fd)
{
    FILE *f = file(fd);
    if (!f) return ISFS_EINVAL;
    if (writable[fd]) {
        fflush(f);
        fsync(fileno(f));
    }
    fclose(f);
    files[fd] = NULL;
    return 0;
}

s32 ISFS_Read(s32 fd, void *buf, u32 len)
{
    FILE *f = file(fd);
    if (!f) return ISFS_EINVAL;
    size_t n = fread(buf, 1, len, f);
    return n || !ferror(f) ? (s32)n : -EIO;
}

s32 ISFS_Write(s32 fd, const void *buf, u32 len)
{
    FILE *f = file(fd);
    if (!f || !writable[fd]) return ISFS_EINVAL;
    size_t n = fwrite(buf, 1, len, f);
    return n == len ? (s32)n : -EIO;
}

s32 ISFS_Seek(s32 fd, s32 where, s32 whence)
{
    FILE *f = file(fd);
    if (!f) return ISFS_EINVAL;
    if (fseek(f, where, whence) < 0) return ISFS_EINVAL;
    return (s32)ftell(f);
}

s32 ISFS_GetFileStats(s32 fd, fstats *st)
{
    FILE *f = file(fd);
    if (!f) return ISFS_EINVAL;
    struct stat sb;
    if (fstat(fileno(f), &sb) < 0) return -errno;
    st->file_length = (u32)sb.st_size;
    st->file_pos = (u32)ftell(f);
    return 0;
}

s32 ISFS_CreateFile(const char *path, u8 attr, u8 owner, u8 group, u8 other)
{
    (void)attr, (void)owner, (void)group, (void)other;
    if (!mounted) return ISFS_EINVAL;
    char full[128];
    host_path(path, full, sizeof full);
    FILE *f = fopen(full, "wb");
    if (!f) return -errno;
    fclose(f);
    return 0;
}

s32 ISFS_Delete(const char *path)
{
    if (!mounted) return ISFS_EINVAL;
    char full[128];
    host_path(path, full, sizeof full);
    return remove(full) < 0 ? -errno : 0;
}
