#include <string.h>
#include "wiino.h"

static const u8 table2[8] = {0x1, 0x5, 0x0, 0x4, 0x2, 0x3, 0x6, 0x7};
static const u8 table1_inv[16] = {0xD, 0x5, 0x9, 0x7, 0x0, 0xF, 0xA, 0x2,
                                  0xC, 0x3, 0xE, 0x1, 0x8, 0x6, 0xB, 0x4};

static u8 get_byte(u64 v, int i) { return (u8)(v >> (i * 8)); }
static u64 put_byte(u64 v, int i, u8 b)
{
    return (v & ~(0xFFULL << (i * 8))) | ((u64)b << (i * 8));
}

static u64 crc_reduce(u64 id)
{
    for (int i = 0; i <= 42; i++)
        if ((id >> (52 - i)) & 1) id ^= 0x635ULL << (42 - i);
    return id;
}

static u64 unscramble(u64 id)
{
    id &= 0x001FFFFFFFFFFFFFULL;
    id ^= 0x00005E5E5E5E5E5EULL;
    id &= 0x001FFFFFFFFFFFFFULL;

    u64 c = (id << 5) & 0x20;
    id |= c << 48;
    id >>= 1;

    u64 copy = id;
    for (int i = 0; i <= 5; i++) id = put_byte(id, i, get_byte(copy, table2[i]));
    for (int i = 0; i <= 5; i++) {
        u8 b = get_byte(id, i);
        id = put_byte(id, i, (u8)((table1_inv[b >> 4] << 4) | table1_inv[b & 0xF]));
    }

    u64 hi = id >> 0x20;
    u64 a = id >> 0x16 | (hi & 0x7FF) << 10;
    u64 b = id * 0x400 | (hi >> 0xB & 0x3FF);
    return (((a << 32) | b) ^ 0x0000B3B3B3B3B3B3ULL) & 0x001FFFFFFFFFFFFFULL;
}

int wiino_valid(const char *digits)
{
    if (!digits || strlen(digits) != 16) return 0;
    u64 id = 0;
    for (int i = 0; i < 16; i++) {
        if (digits[i] < '0' || digits[i] > '9') return 0;
        id = id * 10 + (u64)(digits[i] - '0');
    }
    if (!id || id > 0x001FFFFFFFFFFFFFULL) return 0;
    u64 d = unscramble(id);
    if (crc_reduce(d) != 0) return 0;
    unsigned hw = (d >> 47) & 7, area = (d >> 50) & 7;
    return hw <= 2 && area <= 6;
}
