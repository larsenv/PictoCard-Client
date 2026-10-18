#pragma once
#include "platform.h"
#include "gfx.h"

#define TEXT_BYTES 2048

typedef struct {
    int method; /* 0 email, 1 wii */
    char recipient_email[256];
    char sender_name[128];
    char sender_email[256];
    char recipient_wii[32];
    char message[TEXT_BYTES];
    char card_text[TEXT_BYTES];
    int font;            /* index into meta.fonts */
    int color;           /* index into the text colour palette, -1 follows the background */
    int dark;            /* card background */
    int expression;      /* index into meta.expressions */
    int attach;          /* wii: 0 none, 1 picture, 2 PictoCard */
    int doodle;          /* 1: the card is a doodle drawn on the Wii, 0: a full PictoCard */
    int letterhead;      /* index into meta.letterheads, -1 default */
    int mii;             /* index into miis, -1 none */
    char picture_name[128];
    unsigned char *picture_jpeg; /* full size, for sending */
    size_t picture_len;
    unsigned char *preview_jpeg; /* smaller, for live previews */
    size_t preview_len;
    image_t *thumb;
} draft_t;

typedef struct {
    const char *name;
    u32 rgb;
} swatch_t;

extern draft_t draft;
extern const swatch_t PALETTE[];
extern const int PALETTE_COUNT;

/* app.c */
void app_run(void);
void draft_changed(void);
u32 draft_text_color(void);
char *draft_json(int for_send);

/* keyboard.c — on-screen keyboard */
void osk_open(const char *label, char *buf, int buf_size, int max_chars, int multiline, int numeric, int email);
int osk_active(void);
int osk_hover(void); /* the highlighted key, for the hover tick */
void osk_insert_text(const char *s);
void osk_update(void *input);
void osk_draw(int frame);

/* doodle.c — doodle pad */
typedef void (*doodle_cb)(const image_t *drawing);
void doodle_open(doodle_cb cb);
int doodle_active(void);
void doodle_update(void *input);
void doodle_draw(int frame);

/* camera.c — take a photo with the GamePad camera */
typedef void (*camera_cb)(const image_t *photo);
void camera_open(camera_cb cb, void (*on_error)(const char *message));
int camera_active(void);
void camera_update(void *input);
void camera_draw(int frame);
void camera_shutdown(void);

/* mb.c — pictures from the Wii Message Board (NAND cdb.vff and SD card messages) */
typedef void (*mb_cb)(const image_t *picture);
void mb_open(mb_cb cb, void (*on_error)(const char *message));

/* picker.c — list picker and file browser */
typedef struct {
    char label[96];
    u32 color;
    int has_color;
    int header; /* group heading, not selectable */
    image_t *icon; /* optional small picture (a Mii face), drawn in place of the colour swatch */
    int value;
} pick_item_t;

typedef void (*pick_cb)(int value);
void picker_open(const char *title, pick_item_t *items, int count, int selected_value, pick_cb cb);
/* Same, with a small preview of the highlighted entry (preview(value) returns an image the picker
 * owns, or NULL) and a callback for when the list closes. */
typedef image_t *(*pick_preview_cb)(int value);
void picker_open_preview(const char *title, pick_item_t *items, int count, int selected_value, pick_cb cb,
                         pick_preview_cb preview, void (*closed)(void));
int picker_active(void);
int picker_hover(void);
void picker_update(void *input);
void picker_draw(int frame);

typedef void (*file_cb)(const char *path);
void files_open(file_cb cb);
int files_active(void);
int files_hover(void);
void files_update(void *input);
void files_draw(int frame);
