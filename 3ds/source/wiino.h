#pragma once
#include <3ds/types.h>

/* Wii Number (NWC24 user ID) validation, after RiiConnect24/wiino. A real
 * number is a scrambled 53-bit value carrying a 10-bit CRC, so a mistyped or
 * made-up number fails the check. Returns 1 when `digits` is a plausible Wii. */
int wiino_valid(const char *digits);
