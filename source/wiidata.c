#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <gccore.h>
#include "wiidata.h"
#include "font.h"

mii_t miis[MAX_MIIS];
int mii_count;
char wii_number[24];

static const u32 MII_COLORS[12] = {
    0xC1292E, 0xF5821F, 0xFCD20F, 0x7DC144, 0x15813A, 0x1D58AA,
    0x3DB2E0, 0xF26BA8, 0x8A47A5, 0x7D5133, 0xF2F2F2, 0x262626
};

u32 mii_color(int index)
{
    return MII_COLORS[index >= 0 && index < 12 ? index : 0];
}

static void utf16be_to_utf8(const u8 *src, int chars, char *out, int out_len)
{
    int n = 0;
    for (int i = 0; i < chars; i++) {
        u32 cp = src[i * 2] << 8 | src[i * 2 + 1];
        if (!cp) break;
        char tmp[4];
        int len = utf8_encode(cp, tmp);
        if (n + len >= out_len) break;
        memcpy(out + n, tmp, len);
        n += len;
    }
    out[n] = 0;
}

static void *read_nand(const char *path, u32 *size)
{
    s32 fd = ISFS_Open(path, ISFS_OPEN_READ);
    if (fd < 0) return NULL;
    static fstats st ATTRIBUTE_ALIGN(32);
    void *buf = NULL;
    if (ISFS_GetFileStats(fd, &st) >= 0 && st.file_length > 0 && st.file_length < 2 * 1024 * 1024) {
        buf = memalign(32, (st.file_length + 31) & ~31);
        if (buf && ISFS_Read(fd, buf, st.file_length) != (s32)st.file_length) {
            free(buf);
            buf = NULL;
        }
        *size = st.file_length;
    }
    ISFS_Close(fd);
    return buf;
}

void format_wii_number(const char *digits, char *out)
{
    int n = 0;
    for (int i = 0; digits[i] && i < 16; i++) {
        if (i && i % 4 == 0) out[n++] = ' ';
        out[n++] = digits[i];
    }
    out[n] = 0;
}

void wiidata_load(void)
{
    if (ISFS_Initialize() < 0) return;
    u32 size = 0;

    /* WiiConnect24 mail config: the Wii Number is a u64 at offset 8 */
    u8 *cfg = read_nand("/shared2/wc24/nwc24msg.cfg", &size);
    if (cfg && size >= 16 && !memcmp(cfg, "WcCf", 4)) {
        u64 id = 0;
        memcpy(&id, cfg + 8, 8);
        if (id) snprintf(wii_number, sizeof wii_number, "%016llu", (unsigned long long)id);
    }
    free(cfg);

    /* Mii Channel database: "RNOD", then 100 RFLCharData entries */
    u8 *db = read_nand("/shared2/menu/FaceLib/RFL_DB.dat", &size);
    if (db && size >= 4 + MAX_MIIS * MII_SIZE && !memcmp(db, "RNOD", 4)) {
        for (int i = 0; i < MAX_MIIS; i++) {
            const u8 *m = db + 4 + i * MII_SIZE;
            if (!m[2] && !m[3]) continue; /* no name: empty slot */
            mii_t *out = &miis[mii_count++];
            memcpy(out->data, m, MII_SIZE);
            utf16be_to_utf8(m + 2, 10, out->name, sizeof out->name);
            utf16be_to_utf8(m + 0x36, 10, out->creator, sizeof out->creator);
            out->color = ((m[0] << 8 | m[1]) >> 1) & 0xF;
        }
    }
    free(db);
    ISFS_Deinitialize();
}
