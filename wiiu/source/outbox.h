#pragma once
#include <stddef.h>
#include "platform.h"

/* Queue a Message Board letter in Wii mode's WiiConnect24 send box
 * (/shared2/wc24/mbox/wc24send.ctl + .mbx on the vWii NAND), the way the NWC24
 * library's NWC24CommitMsg does. KD (WiiConnect24) only runs in Wii mode, so the
 * letter goes out the next time the Wii Menu checks for mail. It comes from Wii
 * mode's own number, so the recipient must have it registered as a friend.
 * jpeg, a letterhead .arc and mii (74 bytes) are optional. Returns 0, or -1 with err filled in. */
int outbox_send(const char *to_number, const char *sender_name, const char *text, const u8 *jpeg, size_t jpeg_len,
                const u8 *arc, size_t arc_len, const u8 *mii, char *err, int err_len);
