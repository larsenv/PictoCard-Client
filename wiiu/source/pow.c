/* Proof of work for PictoCard's anti-spam challenges: find a nonce so that
 * SHA-256(challenge ":" nonce) starts with `bits` zero bits.
 *
 * The fixed prefix is hashed once (the "midstate"). A few filler characters
 * are added to the nonce when needed so the part that changes always starts
 * a new 64-byte block; every attempt is then a single compression. */
#include <string.h>
#include <stdio.h>
#include "pow.h"

static const u32 K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void compress(u32 *state, const u8 *block)
{
    u32 w[64];
    for (int i = 0; i < 16; i++) /* the Wii U is big-endian: words load directly */
        w[i] = (u32)block[i * 4] << 24 | block[i * 4 + 1] << 16 | block[i * 4 + 2] << 8 | block[i * 4 + 3];
    for (int i = 16; i < 64; i++) {
        u32 s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        u32 s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    u32 a = state[0], b = state[1], c = state[2], d = state[3];
    u32 e = state[4], f = state[5], g = state[6], h = state[7];
    for (int i = 0; i < 64; i++) {
        u32 t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        u32 t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

static int zero_bits(const u32 *state)
{
    int bits = 0;
    for (int i = 0; i < 8; i++) {
        if (!state[i]) {
            bits += 32;
            continue;
        }
        return bits + __builtin_clz(state[i]);
    }
    return bits;
}

int pow_solve(const char *challenge, int bits, char *nonce_out, volatile int *progress)
{
    static const u32 IV[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                               0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    char prefix[512];
    int plen = snprintf(prefix, sizeof prefix, "%s:", challenge);
    if (plen <= 0 || plen >= (int)sizeof prefix - 64) return -1;

    /* filler so the counter part fits in one final block with its padding */
    char filler[64] = "";
    int rem = plen % 64, flen = 0;
    if (rem > 64 - 9 - 7) flen = 64 - rem;
    for (int i = 0; i < flen; i++) filler[i] = 'p';
    memcpy(prefix + plen, filler, flen);
    int full = plen + flen; /* multiple of 64 when flen > 0 */

    u32 mid[8];
    memcpy(mid, IV, sizeof mid);
    int done = 0;
    for (; done + 64 <= full; done += 64) compress(mid, (const u8 *)prefix + done);
    int tail = full - done; /* bytes of prefix left for the last block (0..48) */

    double expected = 1;
    for (int i = 0; i < bits; i++) expected *= 2;
    u8 block[64];
    for (u32 n = 0; n < 0x7FFFFFFF; n++) {
        char digits[8];
        int len = 0;
        u32 v = n;
        do {
            digits[len++] = "0123456789abcdefghijklmnopqrstuvwxyz"[v % 36];
            v /= 36;
        } while (v);
        memset(block, 0, sizeof block);
        memcpy(block, prefix + done, tail);
        for (int i = 0; i < len; i++) block[tail + i] = digits[len - 1 - i];
        int msg = tail + len;
        block[msg] = 0x80;
        u64 total_bits = (u64)(full + len) * 8;
        for (int i = 0; i < 8; i++) block[63 - i] = (u8)(total_bits >> (i * 8));
        u32 st[8];
        memcpy(st, mid, sizeof st);
        compress(st, block);
        if (zero_bits(st) >= bits) {
            memcpy(nonce_out, filler, flen);
            for (int i = 0; i < len; i++) nonce_out[flen + i] = digits[len - 1 - i];
            nonce_out[flen + len] = 0;
            if (progress) *progress = 100;
            return 0;
        }
        if (progress && (n & 4095) == 0) {
            int pct = (int)(n / expected * 63);
            *progress = pct > 99 ? 99 : pct;
        }
    }
    return -1;
}
