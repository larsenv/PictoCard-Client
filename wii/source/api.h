#pragma once
#include <gctypes.h>
#include "gfx.h"

/* PictoCard's JSON API (/api/v1), called from a background thread so the
 * screen keeps animating. Only one job runs at a time. */

#define LETTERHEAD_MAX 128
typedef struct {
    char id[48];
    char name[64];
    char group[64];
} named_t;

typedef struct {
    int loaded;
    int wii_enabled;
    char pictocard_wii[20];
    char min_client[16];
    int max_text;
    named_t fonts[32];
    int font_count;
    named_t letterheads[LETTERHEAD_MAX];
    int letterhead_count;
    named_t expressions[64];
    int expression_count;
} meta_t;

enum { JOB_NONE, JOB_CONNECT, JOB_PREVIEW, JOB_SEND, JOB_VERIFY, JOB_FRIEND, JOB_FACES, JOB_LETTERHEADS, JOB_ARC };
enum { STAGE_IDLE, STAGE_NETWORK, STAGE_CHALLENGE, STAGE_SOLVING, STAGE_UPLOADING, STAGE_WAITING, STAGE_DONE };

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
    image_t *letter[8];
    int letter_count;
    int needs_image;
    image_t *faces[16];        /* Mii faces, owned by the UI once finished */
    int face_count;
    int face_first;            /* index of the first Mii in the request */
    char ip[20];
    image_t *sheet;            /* letterhead thumbnails in one grid, owned by the UI once finished */
    int sheet_w, sheet_h, sheet_cols, sheet_count;
    unsigned char *arc;        /* a letterhead's .arc, owned by the UI once finished */
    size_t arc_len;
} api_state_t;

extern meta_t meta;
extern api_state_t api;

void api_init(void);
/* body is a JSON string built by the UI; the API takes ownership */
int api_connect(void);
int api_preview(char *body);
int api_send(char *body);
int api_verify(const char *ticket, const char *code);
int api_friend(const char *wii_number);
/* body: {"miis":[base64...]}; the faces for Miis first.. come back in api.faces */
int api_faces(char *body, int first);
/* all letterhead thumbnails (w x h each) as one grid in api.sheet, in meta.letterheads order */
int api_letterheads(int w, int h, int cols);
/* one letterhead's .arc (for letters sent from this Wii's outbox) into api.arc */
int api_letterhead_arc(const char *id);
