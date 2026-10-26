/* WiiConnect24 send box writer. Formats, as the RevoEX NWC24 library writes
 * them (see tools/nwc24/README.md):
 *
 * wc24send.mbx is a 2 MiB VFF: a 0x20-byte header, then a little-endian FAT12
 * volume with 512-byte clusters (two 0x2000-byte FATs, a 128-entry root
 * directory holding the "mb" directory, then data). Each letter is
 * mb/sNNNNNNN.msg, the full SMTP submission (MAIL FROM / RCPT TO / DATA and the
 * message), zero-padded to 32 bytes.
 *
 * wc24send.ctl is a 0x80-byte 'WcTf' header and 127 0x80-byte entries that
 * index the letters; packed fields are (length << 20) | offset in the file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <malloc.h>
#include "platform.h"
#include "outbox.h"
#include "nand.h"
#include "font.h"
#include "util.h"
#include "wiidata.h"

#define CTL_PATH "/shared2/wc24/mbox/wc24send.ctl"
#define MBX_PATH "/shared2/wc24/mbox/wc24send.mbx"
#define CTL_SIZE 0x4000
#define CTL_ENTRIES 127
#define VFF_SIZE 0x200000
#define CLUSTER 512
#define FAT_OFF 0x20
#define FAT_SIZE (VFF_SIZE / CLUSTER * 2)
#define ROOT_OFF (FAT_OFF + 2 * FAT_SIZE)
#define DATA_OFF (ROOT_OFF + 128 * 32)
#define N_CLUSTERS ((VFF_SIZE - DATA_OFF) / CLUSTER)

#define F_WII 0x1
#define F_MSGBOARD 0x8   /* X-Wii-AppId type 2 */
#define F_SENDBOX 0x10
#define F_COMMIT_WII 0x300
#define F_ATTACHED 0x10000
#define F_INIT 0x200000

/* ── growable string ─────────────────────────────────────────────────────── */

typedef struct {
    char *p;
    size_t len, cap;
} sb_t;

static int sb_add(sb_t *b, const char *s, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        size_t cap = (b->len + n + 1) * 2;
        char *p = realloc(b->p, cap);
        if (!p) return 0;
        b->p = p;
        b->cap = cap;
    }
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = 0;
    return 1;
}

static int sb_str(sb_t *b, const char *s) { return sb_add(b, s, strlen(s)); }

/* base64 in 76-column CRLF lines */
static int sb_b64(sb_t *b, const u8 *data, size_t len)
{
    char *s = base64_encode(data, len);
    if (!s) return 0;
    size_t n = strlen(s);
    int ok = 1;
    for (size_t i = 0; i < n && ok; i += 76) {
        if (i) ok = sb_str(b, "\r\n");
        ok = ok && sb_add(b, s + i, n - i < 76 ? n - i : 76);
    }
    free(s);
    return ok;
}

/* UTF-8 to UTF-16BE, word-wrapped at 38 characters like PictoCard's letters */
static u8 *wrap_utf16(const char *text, size_t *out_len)
{
    size_t n = 0;
    u8 *out = malloc(strlen(text) * 4 + 16);
    if (!out) return NULL;
    const char *s = text;
    int col = 0;
#define NEWLINE() (out[n++] = 0, out[n++] = '\r', out[n++] = 0, out[n++] = '\n', col = 0)
    while (*s) {
        u32 cp = utf8_next(&s);
        if (cp == '\r') continue;
        if (cp == '\n') {
            NEWLINE();
            continue;
        }
        if (cp == ' ' && col) {
            /* break here if the next word doesn't fit on this line */
            const char *w = s;
            int wl = 0;
            while (*w && *w != ' ' && *w != '\n') {
                utf8_next(&w);
                wl++;
            }
            if (col + 1 + wl > 38) {
                NEWLINE();
                continue;
            }
        }
        if (cp > 0xFFFF) cp = '?';
        if (col >= 38) NEWLINE(); /* a word longer than a line */
        out[n++] = cp >> 8;
        out[n++] = cp & 0xFF;
        col++;
    }
#undef NEWLINE
    if (!n) out[n++] = 0, out[n++] = ' ';
    *out_len = n;
    return out;
}

/* ── NAND files ──────────────────────────────────────────────────────────── */

static s32 isfs_error; /* the last ISFS failure, shown in error messages */

static u8 *read_all(const char *path, u32 *size)
{
    s32 fd = ISFS_Open(path, ISFS_OPEN_READ);
    if (fd < 0) {
        isfs_error = fd;
        return NULL;
    }
    static fstats st ATTRIBUTE_ALIGN(32);
    u8 *buf = NULL;
    if (ISFS_GetFileStats(fd, &st) >= 0) {
        *size = st.file_length;
        buf = memalign(32, (st.file_length + 31) & ~31);
        if (buf && st.file_length && ISFS_Read(fd, buf, st.file_length) != (s32)st.file_length) {
            free(buf);
            buf = NULL;
        }
    }
    ISFS_Close(fd);
    return buf;
}

static int write_all(const char *path, const u8 *buf, u32 size, u32 old_size)
{
    s32 r;
    if (old_size != size) {
        /* a stub file (as in emulators): truncated in place, keeping the vWii's
         * owner and permissions, rather than deleted and made again */
        if ((r = ISFS_CreateFile(path, 0, 3, 3, 3)) < 0) return isfs_error = r, 0;
    }
    s32 fd = ISFS_Open(path, ISFS_OPEN_WRITE);
    if (fd < 0) return isfs_error = fd, 0;
    r = ISFS_Write(fd, buf, size);
    ISFS_Close(fd);
    if (r != (s32)size) return isfs_error = r, 0;
    return 1;
}

/* ── FAT12 inside the VFF ────────────────────────────────────────────────── */

static int fat_get(const u8 *v, int n)
{
    const u8 *p = v + FAT_OFF + n * 3 / 2;
    int x = p[0] | p[1] << 8;
    return n & 1 ? x >> 4 : x & 0xFFF;
}

static void fat_set(u8 *v, int n, int x)
{
    for (int f = 0; f < 2; f++) {
        u8 *p = v + FAT_OFF + f * FAT_SIZE + n * 3 / 2;
        if (n & 1) {
            p[0] = (p[0] & 0x0F) | (x << 4 & 0xF0);
            p[1] = x >> 4;
        } else {
            p[0] = x & 0xFF;
            p[1] = (p[1] & 0xF0) | (x >> 8 & 0x0F);
        }
    }
}

static u8 *cluster(u8 *v, int n) { return v + DATA_OFF + (n - 2) * CLUSTER; }

static void put16(u8 *p, int x) { p[0] = x, p[1] = x >> 8; }
static void put32le(u8 *p, u32 x) { p[0] = x, p[1] = x >> 8, p[2] = x >> 16, p[3] = x >> 24; }
static void put32(u8 *p, u32 x) { p[0] = x >> 24, p[1] = x >> 16, p[2] = x >> 8, p[3] = x; }

static void dir_entries(u8 *out, const char *long_name, const char *short_name, int attr, int first, u32 size,
                        const struct tm *t)
{
    u8 name[26];
    memset(name, 0xFF, sizeof name);
    int i = 0;
    for (; long_name[i]; i++) name[i * 2] = long_name[i], name[i * 2 + 1] = 0;
    name[i * 2] = name[i * 2 + 1] = 0;
    u8 sum = 0;
    for (int k = 0; k < 11; k++) sum = (u8)(((sum & 1) << 7) + (sum >> 1) + (u8)short_name[k]);
    u8 *l = out;
    memset(l, 0, 32);
    l[0] = 0x41;
    memcpy(l + 1, name, 10);
    l[11] = 0x0F;
    l[13] = sum;
    memcpy(l + 14, name + 10, 12);
    memcpy(l + 28, name + 22, 4);
    u8 *s = out + 32;
    memset(s, 0, 32);
    memcpy(s, short_name, 11);
    s[11] = attr;
    s[13] = 1;
    int tm_ = t->tm_hour << 11 | t->tm_min << 5 | t->tm_sec / 2;
    int dt = (t->tm_year - 80) << 9 | (t->tm_mon + 1) << 5 | t->tm_mday;
    put16(s + 14, tm_), put16(s + 16, dt), put16(s + 18, dt), put16(s + 22, tm_), put16(s + 24, dt);
    put16(s + 26, first);
    put32le(s + 28, size);
}

static void vff_format(u8 *v, const struct tm *t)
{
    memset(v, 0, VFF_SIZE);
    memcpy(v, "VFF ", 4);
    v[4] = 0xFE, v[5] = 0xFF, v[6] = 0x01, v[7] = 0x00;
    put32(v + 8, VFF_SIZE);
    v[12] = 0, v[13] = CLUSTER / 16;
    fat_set(v, 0, 0xFF0), fat_set(v, 1, 0xFFF), fat_set(v, 2, 0xFFF);
    dir_entries(v + ROOT_OFF, "mb", "MB         ", 0x10, 2, 0, t);
    u8 tmp[64];
    dir_entries(tmp, "", ".          ", 0x10, 2, 0, t);
    memcpy(cluster(v, 2), tmp + 32, 32);
    dir_entries(tmp, "", "..         ", 0x10, 0, 0, t);
    memcpy(cluster(v, 2) + 32, tmp + 32, 32);
}

static int mb_cluster(const u8 *v)
{
    for (int o = ROOT_OFF; o < DATA_OFF; o += 32)
        if (!memcmp(v + o, "MB         ", 11)) return v[o + 26] | v[o + 27] << 8;
    return -1;
}

static int used_clusters(const u8 *v)
{
    int n = 0;
    for (int c = 2; c < N_CLUSTERS + 2; c++) n += fat_get(v, c) != 0;
    return n;
}

/* Allocate count clusters as one chain; returns the first or -1 */
static int alloc_chain(u8 *v, int count)
{
    int first = -1, prev = -1;
    for (int c = 2; c < N_CLUSTERS + 2 && count; c++) {
        if (fat_get(v, c)) continue;
        if (prev >= 0) fat_set(v, prev, c);
        else first = c;
        fat_set(v, c, 0xFFF);
        prev = c;
        count--;
    }
    if (count) {
        for (int c = first; c >= 2 && c < 0xFF0;) { /* undo */
            int next = fat_get(v, c);
            fat_set(v, c, 0);
            c = next;
        }
        return -1;
    }
    return first;
}

static int vff_add(u8 *v, const char *long_name, const char *short_name, const u8 *data, u32 len,
                   const struct tm *t)
{
    int dir = mb_cluster(v);
    if (dir < 2) return 0;
    int n = (len + CLUSTER - 1) / CLUSTER;
    int first = alloc_chain(v, n ? n : 1);
    if (first < 0) return 0;
    u32 done = 0;
    for (int c = first; c >= 2 && c < 0xFF0; c = fat_get(v, c)) {
        u32 k = len - done < CLUSTER ? len - done : CLUSTER;
        memset(cluster(v, c), 0, CLUSTER);
        memcpy(cluster(v, c), data + done, k);
        done += k;
    }
    u8 ent[64];
    dir_entries(ent, long_name, short_name, 0x20, first, len, t);
    /* KD reuses IDs of deleted letters: drop a leftover file of the same name */
    for (int c = dir; c >= 2 && c < 0xFF0; c = fat_get(v, c)) {
        u8 *d = cluster(v, c);
        for (int o = 0; o < CLUSTER; o += 32) {
            if (d[o] == 0 || d[o] == 0xE5 || memcmp(d + o, short_name, 11)) continue;
            for (int k = d[o + 26] | d[o + 27] << 8; k >= 2 && k < 0xFF0 && k != first;) {
                int next = fat_get(v, k);
                fat_set(v, k, 0);
                k = next;
            }
            d[o] = 0xE5;
            if (o >= 32 && d[o - 32 + 11] == 0x0F) d[o - 32] = 0xE5;
        }
    }
    /* two adjacent free slots in the mb directory, growing it if needed */
    int last = dir;
    for (int c = dir; c >= 2 && c < 0xFF0; c = fat_get(v, c)) {
        u8 *d = cluster(v, c);
        for (int o = 0; o + 64 <= CLUSTER; o += 32)
            if ((d[o] == 0 || d[o] == 0xE5) && (d[o + 32] == 0 || d[o + 32] == 0xE5)) {
                memcpy(d + o, ent, 64);
                return 1;
            }
        last = c;
    }
    int grow = alloc_chain(v, 1);
    if (grow < 0) return 0;
    fat_set(v, last, grow);
    memset(cluster(v, grow), 0, CLUSTER);
    memcpy(cluster(v, grow), ent, 64);
    return 1;
}

/* ── the letter ──────────────────────────────────────────────────────────── */

typedef struct {
    u32 from, to, charset, encoding; /* packed (length << 20) | offset */
    u32 header_length, text_offset, text_encoded, text_decoded;
    u32 att_offset, att_size, att_decoded;
} layout_t;

static u32 pack(size_t off, size_t len) { return (u32)(len << 20 | off); }

static int build_letter(sb_t *b, layout_t *L, const char *from, const char *to, const char *name, const char *text,
                        const u8 *jpeg, size_t jpeg_len, const u8 *arc, size_t arc_len, const u8 *mii, time_t now)
{
    static const char *const MON[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    struct tm t;
    gmtime_r(&now, &t);
    char line[256];
    int ok = 1;
    memset(L, 0, sizeof *L);
    snprintf(line, sizeof line, "MAIL FROM: %s\r\nRCPT TO: %s\r\nDATA\r\n", from, to);
    ok &= sb_str(b, line);
    snprintf(line, sizeof line, "Date: %d %s %d %02d:%02d:%02d -0000\r\nFrom: ", t.tm_mday, MON[t.tm_mon],
             t.tm_year + 1900, t.tm_hour, t.tm_min, t.tm_sec);
    ok &= sb_str(b, line);
    L->from = pack(b->len, strlen(from));
    ok &= sb_str(b, from) && sb_str(b, "\r\nTo: ");
    L->to = pack(b->len, strlen(to));
    ok &= sb_str(b, to);
    snprintf(line, sizeof line, "\r\nMessage-Id: <%08X%08X%08X@%s>\r\n", (unsigned)now, (unsigned)rand(),
             (unsigned)gettick(), strchr(from, '@') + 1);
    ok &= sb_str(b, line);
    ok &= sb_str(b, "X-Wii-AppId: 2-48414541-0001\r\nX-Wii-Cmd: 00044001\r\n");
    if (name && name[0]) {
        size_t ul = 0;
        u8 *u = wrap_utf16(name, &ul);
        char *b64 = u ? base64_encode(u, ul) : NULL;
        ok &= b64 && sb_str(b, "X-Wii-AltName: ") && sb_str(b, b64) && sb_str(b, "\r\n");
        free(u);
        free(b64);
    }
    if (mii) {
        char *face = base64_encode(mii, MII_SIZE);
        if (!face) return 0;
        ok &= sb_str(b, "X-WiiFace: ") && sb_add(b, face, 56) && sb_str(b, "\r\n ") && sb_str(b, face + 56) &&
              sb_str(b, "\r\n");
        free(face);
    }
    ok &= sb_str(b, "MIME-Version: 1.0\r\n");

    size_t ul = 0;
    u8 *u = wrap_utf16(text ? text : "", &ul);
    if (!u) return 0;
    char boundary[64];
    snprintf(boundary, sizeof boundary, "Boundary-NWC24-%08X%04X", (unsigned)now, (unsigned)(gettick() & 0xFFFF));
    int multipart = jpeg || arc;
    if (multipart) {
        snprintf(line, sizeof line, "Content-Type: multipart/mixed;\r\n boundary=\"%s\"\r\n\r\n--%s\r\n", boundary,
                 boundary);
        ok &= sb_str(b, line);
        L->header_length = 0; /* set below */
    }
    size_t hdr_end_mark = b->len;
    ok &= sb_str(b, "Content-Type: text/plain; charset=");
    L->charset = pack(b->len, strlen("utf-16be\r"));
    ok &= sb_str(b, "utf-16be\r\nContent-Transfer-Encoding: ");
    L->encoding = pack(b->len, strlen("base64\r"));
    ok &= sb_str(b, "base64\r\n\r\n");
    L->text_offset = b->len;
    ok &= sb_b64(b, u, ul);
    L->text_encoded = b->len - L->text_offset;
    L->text_decoded = ul;
    free(u);
    if (!multipart) {
        L->header_length = L->text_offset;
        ok &= sb_str(b, "\r\n\r\n\r\n");
    } else {
        /* the top-level header ends with the blank line before the first boundary */
        L->header_length = hdr_end_mark - strlen(boundary) - 4;
        if (jpeg) {
            snprintf(line, sizeof line, "\r\n\r\n--%s\r\nContent-Type: image/jpeg;\r\n name=image.jpg\r\n"
                     "Content-Transfer-Encoding: base64\r\nContent-Disposition: attachment;\r\n filename=image.jpg\r\n\r\n",
                     boundary);
            ok &= sb_str(b, line);
            L->att_offset = b->len;
            ok &= sb_b64(b, jpeg, jpeg_len);
            L->att_size = b->len - L->att_offset;
            L->att_decoded = jpeg_len;
        }
        if (arc) {
            /* the letterhead part, as the server sends it (WiiLink Mail-Admin layout) */
            snprintf(line, sizeof line, "\r\n\r\n--%s\r\nContent-Type: application/x-wii-msgboard;\r\n name=letterhead.arc\r\n"
                     "Content-Transfer-Encoding: base64\r\nContent-Disposition: attachment;\r\n filename=letterhead.arc\r\n\r\n",
                     boundary);
            ok &= sb_str(b, line) && sb_b64(b, arc, arc_len);
        }
        snprintf(line, sizeof line, "\r\n\r\n--%s--\r\n", boundary);
        ok &= sb_str(b, line);
    }
    return ok;
}

static void ctl_entry(u8 *e, u32 id, u32 size, const layout_t *L, u64 from_id, u32 minutes, int attached)
{
    memset(e, 0, 0x80);
    put32(e + 0x00, id);
    put32(e + 0x04, F_INIT | F_COMMIT_WII | F_SENDBOX | F_MSGBOARD | F_WII | (attached ? F_ATTACHED : 0));
    put32(e + 0x08, size);
    put32(e + 0x0C, 0x48414541); /* HAEA, the Wii Message Board */
    put32(e + 0x10, L->header_length);
    put32(e + 0x18, 0x00044001); /* X-Wii-Cmd */
    put32(e + 0x20, from_id >> 32);
    put32(e + 0x24, (u32)from_id);
    put32(e + 0x28, minutes);
    e[0x30] = 1;               /* recipients */
    e[0x31] = attached ? 1 : 0;
    e[0x32] = 0, e[0x33] = 1;  /* app group */
    put32(e + 0x34, L->from);
    put32(e + 0x38, L->to);
    put32(e + 0x40, L->charset);
    put32(e + 0x44, L->encoding);
    put32(e + 0x48, L->text_offset);
    put32(e + 0x4C, L->text_encoded);
    if (attached) {
        put32(e + 0x50, L->att_offset);
        put32(e + 0x54, L->att_size);
        put32(e + 0x60, L->att_decoded);
        put32(e + 0x68, 0x20000); /* image/jpeg */
    } else {
        put32(e + 0x68, 0x10000);
    }
    put32(e + 0x6C, 0x10000);
    put32(e + 0x70, L->text_decoded);
    put32(e + 0x78, 0x80000000);
}

/* On the Wii the NWC24 library suspends KD (WiiConnect24 in IOS) around every
 * mailbox change and then asks it to send. On the Wii U, KD only runs in Wii
 * mode, so nothing is running behind these writes and the letter waits until
 * the Wii Menu's next mail check. */

int outbox_send(const char *to_number, const char *sender_name, const char *text, const u8 *jpeg, size_t jpeg_len,
                const u8 *arc, size_t arc_len, const u8 *mii, char *err, int err_len)
{
#define FAIL(...) do { snprintf(err, err_len, __VA_ARGS__); goto out; } while (0)
    int rc = -1;
    u8 *ctl = NULL, *vff = NULL, *cfg = NULL;
    sb_t mail = { 0 };
    isfs_error = 0;
    if (ISFS_Initialize() < 0) {
        snprintf(err, err_len, "%s", nand_error());
        return -1;
    }
    u32 cfg_size = 0, ctl_size = 0, vff_size = 0;
    cfg = read_all("/shared2/wc24/nwc24msg.cfg", &cfg_size);
    if (!cfg || cfg_size < 0x400 || memcmp(cfg, "WcCf", 4)) FAIL("WiiConnect24 isn't set up in Wii mode.");
    u64 own = be64(cfg + 8);
    if (!own) FAIL("Wii mode has no Wii Number yet. Set up WiiConnect24 in the Wii Menu first.");
    char domain[64];
    snprintf(domain, sizeof domain, "%.63s", cfg + 0x18); /* "@wii.com" or the mail server's */
    if (domain[0] != '@') snprintf(domain, sizeof domain, "@wii.com");

    ctl = read_all(CTL_PATH, &ctl_size);
    if (!ctl) FAIL("Could not open the WiiConnect24 send box (error %d).", (int)isfs_error);
    if (ctl_size != CTL_SIZE || memcmp(ctl, "WcTf", 4)) FAIL("The WiiConnect24 send box is damaged.");
    vff = memalign(32, VFF_SIZE);
    if (!vff) FAIL("The Switch ran out of memory.");
    u8 *old = read_all(MBX_PATH, &vff_size);
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    if (old && vff_size == VFF_SIZE && !memcmp(old, "VFF ", 4)) memcpy(vff, old, VFF_SIZE);
    else vff_format(vff, &lt);
    free(old);

    char from[96], to[96];
    snprintf(from, sizeof from, "w%016llu%s", (unsigned long long)own, domain);
    snprintf(to, sizeof to, "w%s%s", to_number, domain);
    layout_t L;
    if (!build_letter(&mail, &L, from, to, sender_name, text, jpeg, jpeg_len, arc, arc_len, mii, now)) FAIL("The Switch ran out of memory.");
    if (mail.len > 1578040) FAIL("The letter is too large for WiiConnect24.");

    u32 count = ctl[8] << 24 | ctl[9] << 16 | ctl[10] << 8 | ctl[11];
    u32 total = ctl[0x10] << 24 | ctl[0x11] << 16 | ctl[0x12] << 8 | ctl[0x13];
    u32 next_id = ctl[0x18] << 24 | ctl[0x19] << 16 | ctl[0x1A] << 8 | ctl[0x1B];
    u32 next_off = ctl[0x1C] << 24 | ctl[0x1D] << 16 | ctl[0x1E] << 8 | ctl[0x1F];
    if (!next_id) next_id = 1;
    int slot = -1;
    if (next_off >= 0x80 && next_off < 0x80 + CTL_ENTRIES * 0x80 && !(next_off & 0x7F) &&
        !memcmp(ctl + next_off, "\0\0\0\0", 4))
        slot = next_off;
    for (int o = 0x80; slot < 0 && o < 0x80 + CTL_ENTRIES * 0x80; o += 0x80)
        if (!memcmp(ctl + o, "\0\0\0\0", 4)) slot = o;
    if (slot < 0) FAIL("The send box is full. Open the Wii Menu so it sends its mail, then try again.");

    char lname[16], sname[12];
    snprintf(lname, sizeof lname, "s%07u.msg", (unsigned)next_id);
    snprintf(sname, sizeof sname, "S%07uMSG", (unsigned)next_id);
    u32 real_len = (u32)mail.len;
    size_t padded = (mail.len + 31) & ~31;
    if (!sb_add(&mail, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", padded - mail.len))
        FAIL("The Switch ran out of memory.");
    if (!vff_add(vff, lname, sname, (const u8 *)mail.p, (u32)padded, &lt)) FAIL("The send box is full.");

    u32 minutes = (u32)(((u64)now + 2208988800ULL) / 60);
    ctl_entry(ctl + slot, next_id, real_len, &L, own, minutes, jpeg != NULL);
    int free_slot = 0x80;
    for (int o = 0x80; o < 0x80 + CTL_ENTRIES * 0x80; o += 0x80)
        if (!memcmp(ctl + o, "\0\0\0\0", 4)) {
            free_slot = o;
            break;
        }
    put32(ctl + 0x08, count + 1);
    put32(ctl + 0x10, total + real_len);
    put32(ctl + 0x18, next_id + 1);
    put32(ctl + 0x1C, free_slot);
    if (!count) put32(ctl + 0x20, next_id);
    put32(ctl + 0x24, (N_CLUSTERS - 2 - used_clusters(vff)) * CLUSTER);

    /* the letter first, then the index that points at it */
    if (!write_all(MBX_PATH, vff, VFF_SIZE, vff_size)) FAIL("Could not write the send box (error %d).", (int)isfs_error);
    if (!write_all(CTL_PATH, ctl, CTL_SIZE, CTL_SIZE)) FAIL("Could not write the send box index (error %d).", (int)isfs_error);
    rc = 0;
out:
    free(mail.p);
    free(ctl);
    free(vff);
    free(cfg);
    ISFS_Deinitialize();
    return rc;
#undef FAIL
}
