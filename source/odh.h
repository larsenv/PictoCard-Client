#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#include "gfx.h"
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
/* Decode a Wii Message Board "AJPG" picture; NULL if it is not one or is damaged */
image_t *odh_decode(const unsigned char *src, size_t size);
#ifdef __cplusplus
}
#endif
