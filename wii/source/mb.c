/* Pictures from the Wii Message Board. Messages live in the system menu's
 * cdb.vff on the NAND (a small FAT image) and, for messages kept on an SD
 * card, as encrypted files under sd:/private/wii/title/HAEA. The layout and
 * the picture formats come from cdbackup (larsenv/cdbackup): attachments are
 * either Nintendo's "AJPG" (ODH) pictures or plain JPEGs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <malloc.h>
#include <dirent.h>
#include <sys/stat.h>
#include <gccore.h>
#include <ogc/isfs.h>
#include "app.h"
#include "aes.h"
#include "gfx.h"
#include "image.h"
#include "libpatcher.h"
#include "odh.h"

#define NAND_VFF "/title/00000001/00000002/data/cdb.vff"
#define SD_DIR "sd:/private/wii/title/HAEA"
#define MAX_VFF_BYTES (24 * 1024 * 1024)
#define MAX_ENTRIES 240
#define SECONDS_TO_2000 946684800ULL
#define ATTR_SIZE 0x400    /* CDBAttrHeader, then the message itself (CDBFILE) */
#define HEADER_READ (ATTR_SIZE + 0x144)

/* ── VFF (FAT12/16 image with a 0x20 byte header) ────────────────────────── */

/* The 20 MB image stays on the NAND: sectors are read as needed through ISFS. */
static s32 vff_fd = -1;
static u32 vff_len;
static u8 *fat;     /* both FAT copies are not needed, just the first */
static u8 *io;      /* 32-byte aligned bounce buffer for ISFS */
static int fat16, root_off, data_off, fat_entries;
#define IO_BYTES 1024

static u32 le16(const u8 *p) { return p[0] | p[1] << 8; }
static u32 le32(const u8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }
static u32 be32(const u8 *p) { return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

static void vff_free(void)
{
    if (vff_fd >= 0) {
        ISFS_Close(vff_fd);
        ISFS_Deinitialize();
    }
    vff_fd = -1;
    vff_len = 0;
    free(fat);
    free(io);
    fat = io = NULL;
}

/* read len (<= 512) bytes at a byte offset of the image */
static int vff_pread(u32 off, u8 *dst, u32 len)
{
    if (vff_fd < 0 || len > 512 || (u64)off + len > vff_len) return 0;
    u32 base = off & ~31u, span = ((off + len - base) + 31) & ~31u;
    if (ISFS_Seek(vff_fd, base, 0) < 0) return 0;
    if (ISFS_Read(vff_fd, io, span) != (s32)span) return 0;
    memcpy(dst, io + (off - base), len);
    return 1;
}

static int vff_parse(void)
{
    u8 h[32];
    if (!vff_pread(0, h, 32) || memcmp(h, "VFF ", 4) || h[4] != 0xFE || h[5] != 0xFF) return 0;
    u32 file_size = be32(h + 8);
    u32 cluster_bytes = (h[12] << 8 | h[13]) * 16;
    if (file_size != vff_len || cluster_bytes != 512) return 0;
    u32 clusters = file_size / cluster_bytes;
    u32 table;
    if (clusters < 0xFF5) {
        fat16 = 0;
        table = ((clusters + 1) / 2) * 3;
    } else if (clusters < 0xFFF5) {
        fat16 = 1;
        table = clusters * 2;
    } else {
        return 0;
    }
    u32 fat_sectors = (table + 511) / 512;
    u32 sysect = 1 + fat_sectors * 2 + 0x80 / 16;
    if (sysect >= clusters) return 0;
    fat_entries = clusters - sysect + 2;
    root_off = 0x20 + fat_sectors * 2 * 512;
    data_off = 0x20 + (sysect - 1) * 512; /* cluster 2 starts here */
    fat = calloc(fat_sectors + 1, 512);
    if (!fat) return 0;
    for (u32 i = 0; i < fat_sectors; i++)
        if (!vff_pread(0x20 + i * 512, fat + i * 512, 512)) return 0;
    return 1;
}

static u32 fat_next(u32 c)
{
    if (c >= (u32)fat_entries) return 0;
    if (fat16) {
        u32 v = le16(fat + c * 2);
        return v >= 0xFFF8 ? 0 : v;
    }
    u32 v = le16(fat + c + c / 2);
    v = (c & 1) ? v >> 4 : v & 0xFFF;
    return v >= 0xFF8 ? 0 : v;
}

static int read_cluster(u32 c, u8 *dst, u32 in, u32 len)
{
    if (c < 2 || (int)c >= fat_entries) return 0;
    return vff_pread(data_off + (c - 2) * 512 + in, dst, len);
}

/* read up to len bytes of a file starting at byte `off` */
static u32 vff_file_read(u32 first, u32 size, u32 off, u8 *dst, u32 len)
{
    if (off >= size) return 0;
    if (len > size - off) len = size - off;
    u32 c = first, skip = off / 512, got = 0, guard = 0;
    while (skip-- && c) c = fat_next(c);
    u32 in = off % 512;
    while (c && got < len && guard++ < (u32)fat_entries) {
        u32 n = 512 - in;
        if (n > len - got) n = len - got;
        if (!read_cluster(c, dst + got, in, n)) break;
        got += n;
        in = 0;
        c = fat_next(c);
    }
    return got;
}

/* ── Entries ─────────────────────────────────────────────────────────────── */

typedef struct {
    int sd;           /* 1: encrypted file on the SD card */
    u32 first, size;  /* VFF file */
    char path[160];   /* SD file */
    u32 when;         /* seconds since 2000 */
} entry_t;

static entry_t entries[MAX_ENTRIES];
static int entry_count;
static pick_item_t items[MAX_ENTRIES];
static char error_text[160];
static mb_cb deliver;
static void (*report_error)(const char *);

static int is_picture(const u8 *data)
{
    return !memcmp(data, "AJPG", 4) || (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF);
}

/* The picture inside a message buffer (header at 0), or NULL. The CDBFILE
 * attachment fields are tried first; messages that keep their picture
 * elsewhere are searched for an AJPG or JPEG start after the header. */
static const u8 *attachment(const u8 *msg, size_t len, u32 *size)
{
    if (len < HEADER_READ) return NULL;
    const u8 *file = msg + ATTR_SIZE;
    u32 off = be32(file + 0x12C), asz = be32(file + 0x130);
    if (off && asz >= 16 && (size_t)ATTR_SIZE + off + asz <= len && is_picture(file + off)) {
        *size = asz;
        return file + off;
    }
    for (size_t i = HEADER_READ; i + 16 <= len; i++) {
        const u8 *p = msg + i;
        if ((p[0] == 'A' && !memcmp(p, "AJPG", 4)) || (p[0] == 0xFF && p[1] == 0xD8 && p[2] == 0xFF && (p[3] & 0xF0) == 0xE0)) {
            *size = (u32)(len - i);
            return p;
        }
    }
    return NULL;
}

/* Message Board photos have no names of their own, so entries are listed by date and time. */
static void describe(entry_t *e)
{
    time_t t = (time_t)(SECONDS_TO_2000 + e->when);
    struct tm tm;
    gmtime_r(&t, &tm);
    int i = (int)(e - entries);
    snprintf(items[i].label, sizeof items[i].label, "%04d-%02d-%02d  %02d:%02d  Photo%s", tm.tm_year + 1900,
             tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, e->sd ? "  (SD card)" : "");
}

static int add_entry(const entry_t *e, const u8 *msg)
{
    if (entry_count >= MAX_ENTRIES) return 0;
    entries[entry_count] = *e;
    memset(&items[entry_count], 0, sizeof items[entry_count]);
    items[entry_count].value = entry_count;
    (void)msg;
    describe(&entries[entry_count]);
    entry_count++;
    return 1;
}

static int parse_name(const u8 *d, u32 *when)
{
    if (memcmp(d + 8, "000", 3)) return 0;
    u32 v = 0;
    for (int i = 0; i < 8; i++) {
        int c = d[i];
        int h = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (h < 0) return 0;
        v = v << 4 | h;
    }
    *when = v;
    return 1;
}

static void scan_dir_entries(const u8 *base, int count, int depth);

static void scan_dir_chain(u32 cluster, int depth)
{
    u32 guard = 0;
    u8 buf[512];
    while (cluster && guard++ < (u32)fat_entries) {
        if (!read_cluster(cluster, buf, 0, 512)) return;
        scan_dir_entries(buf, 16, depth);
        cluster = fat_next(cluster);
    }
}

static void scan_dir_entries(const u8 *base, int count, int depth)
{
    for (int i = 0; i < count; i++) {
        const u8 *d = base + i * 32;
        if (!d[0]) return;
        if (d[0] == 0xE5 || d[0] == '.' || d[11] == 0x0F || (d[11] & 0x08)) continue;
        u32 first = le16(d + 26), size = le32(d + 28);
        if (d[11] & 0x10) {
            if (depth < 6) scan_dir_chain(first, depth + 1);
            continue;
        }
        entry_t e;
        memset(&e, 0, sizeof e);
        if (!parse_name(d, &e.when) || size < HEADER_READ) continue;
        e.first = first;
        e.size = size;
        u8 head[HEADER_READ];
        if (vff_file_read(first, size, 0, head, HEADER_READ) != HEADER_READ) continue;
        /* the picture lies beyond the header: look at its first bytes only */
        u32 off = be32(head + ATTR_SIZE + 0x12C), asz = be32(head + ATTR_SIZE + 0x130);
        u8 magic[4];
        if (off && asz >= 16 && (u64)ATTR_SIZE + off + asz <= size &&
            vff_file_read(first, size, ATTR_SIZE + off, magic, 4) == 4 && is_picture(magic)) {
            add_entry(&e, head);
        } else if (size >= 4096 && size <= 8 * 1024 * 1024) {
            /* no usable attachment fields: search the whole message (pictures make it large) */
            u8 *msg = malloc(size);
            u32 asz2;
            if (msg && vff_file_read(first, size, 0, msg, size) == size && attachment(msg, size, &asz2))
                add_entry(&e, head);
            free(msg);
        }
    }
}

static void scan_nand(void)
{
    static int patched;
    if (!patched) {
        /* the Message Board data is readable only with the NAND permissions opened up */
        if (!ahbprot_disabled()) {
            snprintf(error_text, sizeof error_text,
                     "Reading the Message Board needs AHB access. Start PictoCard from the Homebrew Channel's list of apps.");
            return;
        }
        patched = 1;
        patch_isfs_permissions_in_place();
    }
    if (ISFS_Initialize() < 0) {
        snprintf(error_text, sizeof error_text, "Could not open the Wii's NAND.");
        return;
    }
    vff_fd = ISFS_Open(NAND_VFF, ISFS_OPEN_READ);
    if (vff_fd < 0) {
        snprintf(error_text, sizeof error_text, "Could not open the Message Board data (error %d).", (int)vff_fd);
        ISFS_Deinitialize();
        vff_fd = -1;
        return;
    }
    fstats *st = memalign(32, sizeof(fstats));
    io = memalign(32, IO_BYTES);
    if (st && io && ISFS_GetFileStats(vff_fd, st) >= 0) vff_len = st->file_length;
    free(st);
    if (!io || !vff_len || !vff_parse()) {
        vff_free();
        snprintf(error_text, sizeof error_text, "The Message Board data is not in a format PictoCard can read.");
        return;
    }
    u8 *root = malloc(0x80 * 32);
    if (!root) return;
    int ok = 1;
    for (int i = 0; i < 8 && ok; i++) ok = vff_pread(root_off + i * 512, root + i * 512, 512);
    if (ok) scan_dir_entries(root, 0x80, 0);
    free(root);
}

/* ── SD card messages ────────────────────────────────────────────────────── */

static u8 *read_sd_message(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < HEADER_READ || size > 8 * 1024 * 1024) {
        fclose(f);
        return NULL;
    }
    u8 *buf = malloc(size);
    if (buf && fread(buf, 1, size, f) != (size_t)size) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    if (!buf) return NULL;
    /* messages kept on the SD card are AES-CBC encrypted with an all-zero key */
    u8 key[16] = { 0 };
    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, key, buf + 0xA0);
    AES_CBC_decrypt_buffer(&ctx, buf + ATTR_SIZE, (size - ATTR_SIZE) & ~15L);
    *len = size;
    return buf;
}

static void scan_sd(const char *dir, int depth)
{
    DIR *dp = opendir(dir);
    if (!dp) return;
    struct dirent *de;
    while ((de = readdir(dp)) && entry_count < MAX_ENTRIES) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        char path[160];
        snprintf(path, sizeof path, "%s/%s", dir, de->d_name);
        struct stat sb;
        if (stat(path, &sb) < 0) continue;
        if (S_ISDIR(sb.st_mode)) {
            if (depth < 5) scan_sd(path, depth + 1);
        } else if (strlen(de->d_name) == 12) {
            entry_t e;
            memset(&e, 0, sizeof e);
            u8 name[12];
            memcpy(name, de->d_name, 12);
            for (int i = 0; i < 8; i++)
                if (name[i] >= 'a' && name[i] <= 'f') name[i] -= 32;
            if (!parse_name(name, &e.when)) continue;
            e.sd = 1;
            snprintf(e.path, sizeof e.path, "%s", path);
            size_t len;
            u8 *msg = read_sd_message(path, &len);
            if (!msg) continue;
            u32 asz;
            const u8 *att = attachment(msg, len, &asz);
            if (att && is_picture(att)) add_entry(&e, msg);
            free(msg);
        }
    }
    closedir(dp);
}

/* ── Loading a picture ───────────────────────────────────────────────────── */

static image_t *load_picture(int index)
{
    if (index < 0 || index >= entry_count) return NULL;
    entry_t *e = &entries[index];
    u8 *msg = NULL;
    size_t len = 0;
    if (e->sd) {
        msg = read_sd_message(e->path, &len);
    } else if (vff_fd >= 0 && e->size <= 8 * 1024 * 1024) {
        msg = malloc(e->size);
        if (msg) {
            len = vff_file_read(e->first, e->size, 0, msg, e->size);
            if (len != e->size) {
                free(msg);
                msg = NULL;
            }
        }
    }
    if (!msg) return NULL;
    image_t *img = NULL;
    u32 asz;
    const u8 *att = attachment(msg, len, &asz);
    if (att) {
        if (!memcmp(att, "AJPG", 4)) img = odh_decode(att, asz);
        else img = image_decode(att, asz, 1600);
    }
    free(msg);
    return img;
}

static image_t *preview(int value)
{
    image_t *img = load_picture(value);
    if (!img) return NULL;
    image_t *thumb = image_fit(img, 150, 120);
    image_free(img);
    return thumb;
}

static void picked(int value)
{
    image_t *img = load_picture(value);
    if (!img) {
        if (report_error) report_error("That Message Board picture could not be read.");
        return;
    }
    if (deliver) deliver(img);
    image_free(img);
}

static void closed(void)
{
    vff_free();
}

static int newest_first(const void *a, const void *b)
{
    const pick_item_t *x = a, *y = b;
    u32 wa = entries[x->value].when, wb = entries[y->value].when;
    return wa < wb ? 1 : wa > wb ? -1 : 0;
}

void mb_open(mb_cb cb, void (*on_error)(const char *))
{
    deliver = cb;
    report_error = on_error;
    entry_count = 0;
    error_text[0] = 0;
    vff_free();
    scan_nand();
    scan_sd(SD_DIR, 0);
    if (!entry_count) {
        vff_free();
        if (on_error)
            on_error(error_text[0] ? error_text : "No pictures were found on the Wii Message Board.");
        return;
    }
    qsort(items, entry_count, sizeof items[0], newest_first);
    picker_open_preview("Pictures on the Wii Message Board", items, entry_count, items[0].value, picked, preview,
                        closed);
}
