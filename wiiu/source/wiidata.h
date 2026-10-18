#pragma once
#include "platform.h"

/* Data read from Wii mode's (the vWii's) NAND: its Wii Number and the Miis in
 * the Mii Channel, plus the Wii U's own Miis from Mii Maker. All optional; the
 * app works without them. */
#define MII_SIZE 74       /* a Wii Mii (RFLCharData) */
#define MII_WIIU_SIZE 92  /* a Wii U Mii (FFLiMiiDataOfficial, in the 3DS byte order the server reads) */
#define MAX_WII_MIIS 100
#define MAX_WIIU_MIIS 300
#define MAX_MIIS (MAX_WII_MIIS + MAX_WIIU_MIIS)

typedef struct {
    char name[32];      /* UTF-8 */
    char creator[32];
    u8 color;           /* favourite colour 0..11 */
    u8 wiiu;            /* from the Wii U's Mii Maker rather than Wii mode's Mii Channel */
    u8 size;            /* MII_SIZE or MII_WIIU_SIZE */
    u8 data[MII_WIIU_SIZE]; /* Wii: RFLCharData, as sent in X-WiiFace. Wii U: Ver3 Mii data */
    struct image *face; /* rendered face for the Mii picker, filled in from the server */
} mii_t;

extern mii_t miis[MAX_MIIS];
extern int mii_count;      /* Wii Miis first, then Wii U Miis */
extern int wii_mii_count; /* how many are Wii Miis */
extern char wii_number[24]; /* 16 digits, or "" */

/* Wii friends from the Message Board address book (/shared2/wc24/nwc24fl.bin) */
#define MAX_FRIENDS 100
typedef struct {
    char name[48];     /* UTF-8 nickname */
    char number[17];   /* 16 digits */
    int established;   /* the friend has added this Wii back */
} friend_t;

extern friend_t friends[MAX_FRIENDS];
extern int friend_count;
const char *friend_name(const char *number); /* nickname for a Wii Number, or NULL */

void wiidata_load(void);
u32 mii_color(int index);
void format_wii_number(const char *digits, char *out); /* "1234 5678 9012 3456" */
