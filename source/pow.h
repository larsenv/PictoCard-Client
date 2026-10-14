#pragma once
#include <gctypes.h>

/* Returns 0 and writes the nonce (at most 32 characters) on success */
int pow_solve(const char *challenge, int bits, char *nonce_out, volatile int *progress);
