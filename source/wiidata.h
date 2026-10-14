#pragma once
#include <gctypes.h>

/* Data read from the console's NAND: its Wii Number and the Miis in the
 * Mii Channel. Both are optional; the app works without them. */
#define MII_SIZE 74
#define MAX_MIIS 100

typedef struct {
    char name[32];      /* UTF-8 */
    char creator[32];
    u8 color;           /* favourite colour 0..11 */
    u8 data[MII_SIZE];  /* RFLCharData, as sent in X-WiiFace */
    struct image *face; /* rendered face for the Mii picker, filled in from the server */
} mii_t;

extern mii_t miis[MAX_MIIS];
extern int mii_count;
extern char wii_number[24]; /* 16 digits, or "" */

void wiidata_load(void);
u32 mii_color(int index);
void format_wii_number(const char *digits, char *out); /* "1234 5678 9012 3456" */
