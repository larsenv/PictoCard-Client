#pragma once
/* The few libogc names the shared code uses, on top of libnx. */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <switch.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

#ifndef ATTRIBUTE_ALIGN
#define ATTRIBUTE_ALIGN(v) __attribute__((aligned(v)))
#endif

/* Wii data is big-endian; the Switch is little-endian, so it is read by bytes */
static inline u64 be64(const u8 *p)
{
    return (u64)p[0] << 56 | (u64)p[1] << 48 | (u64)p[2] << 40 | (u64)p[3] << 32 | (u64)p[4] << 24 | (u64)p[5] << 16 |
           (u64)p[6] << 8 | p[7];
}

static inline u64 gettime(void) { return (u64)armGetSystemTick(); }
static inline u32 gettick(void) { return (u32)armGetSystemTick(); }
#define ticks_to_millisecs(t) ((u64)armTicksToNs(t) / 1000000ULL)
#define ticks_to_microsecs(t) ((u64)armTicksToNs(t) / 1000ULL)

/* The SD card, and where the app keeps settings.txt (and test files) on it */
#define SD_ROOT "sdmc:"
#define APP_PARENT SD_ROOT "/switch"
#define APP_DIR APP_PARENT "/PictoCard"

/* Start a thread with its own stack; 0 on success */
#include <pthread.h>
static inline int thread_start(pthread_t *t, void *(*fn)(void *), size_t stack)
{
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, stack);
    int ret = pthread_create(t, &attr, fn, NULL);
    pthread_attr_destroy(&attr);
    return ret;
}
