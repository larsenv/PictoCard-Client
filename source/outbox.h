#pragma once
#include <stddef.h>
#include <gctypes.h>

/* Queue a Message Board letter in this Wii's WiiConnect24 send box
 * (/shared2/wc24/mbox/wc24send.ctl + .mbx), the way the NWC24 library's
 * NWC24CommitMsg does, then ask KD to send it. It goes out from this Wii's own
 * number, so the recipient must have this Wii registered as a friend.
 * jpeg and mii (74 bytes) are optional. Returns 0, or -1 with err filled in. */
int outbox_send(const char *to_number, const char *sender_name, const char *text, const u8 *jpeg, size_t jpeg_len,
                const u8 *mii, char *err, int err_len);
