#pragma once
#include <3ds/types.h>
#include "image.h"

/* PictoCard's JSON API (/api/v1), called from a background thread so the
 * screens keep animating. Only one job runs at a time. */

typedef struct {
    char id[48];
    char name[64];
    char group[64];
} named_t;

typedef struct {
    int loaded;
    int wii_enabled;
    char pictocard_wii[20];  /* the Wii Number letters come from; recipients add it as a friend */
    char min_client[16];
    int max_text;
    named_t fonts[32];
    int font_count;
    named_t letterheads[128];
    int letterhead_count;
    named_t expressions[64];
    int expression_count;
} meta_t;

enum { JOB_NONE, JOB_CONNECT, JOB_PREVIEW, JOB_SEND, JOB_VERIFY, JOB_FRIEND };
enum { STAGE_IDLE, STAGE_NETWORK, STAGE_CHALLENGE, STAGE_SOLVING, STAGE_UPLOADING, STAGE_WAITING, STAGE_DONE };

#define MAX_LETTER_PAGES 8

typedef struct {
    volatile int busy;
    volatile int job;
    volatile int stage;
    volatile int progress;     /* 0..100 */
    volatile int finished;     /* set when a job completes; the UI clears it */
    int ok;
    int status;                /* HTTP status */
    char error[256];
    char result[24];           /* "sent", "verify", "already" ... */
    char ticket[64];
    char verify_email[128];
    image_t *card;             /* preview results, owned by the UI once finished */
    image_t *letter[MAX_LETTER_PAGES];
    int letter_count;
    int needs_image;
    char ip[20];
} api_state_t;

extern meta_t meta;
extern api_state_t api;

void api_init(void);
void api_exit(void);
/* body is a JSON string built by the UI; the API takes ownership */
int api_connect(void);
int api_preview(char *body);
int api_send(char *body);
int api_verify(const char *ticket, const char *code);
/* PictoCard sends a friend request to this Wii, so its letters are accepted */
int api_friend(const char *wii_number);
