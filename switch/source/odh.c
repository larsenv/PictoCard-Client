/* The Wii Message Board's "AJPG" (ODH) picture decoder reads the big-endian
 * words of a Wii's own data, and the Switch has no Message Board to read them
 * from, so there is nothing to decode here. */
#include "odh.h"

image_t *odh_decode(const unsigned char *src, size_t size)
{
    (void)src;
    (void)size;
    return NULL;
}
