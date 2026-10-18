/* PictoCard for 3DS: the live card preview on the top screen, the form on the
 * touch screen. Cards go out through PictoCard's JSON API, by email or as a
 * letter to a Wii Message Board from PictoCard's own Wii Number. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <time.h>
#include <sys/stat.h>
#include <3ds.h>
#include "app.h"
#include "api.h"
#include "autotest.h"
#include "camera.h"
#include "cJSON.h"
#include "doodle.h"
#include "gfx.h"
#include "image.h"
#include "input.h"
#include "settings.h"
#include "theme.h"
#include "util.h"
#include "version.h"
#include "wiino.h"
#include "logo_png.h"

#define MII_BYTES 96 /* 3DS Mii data (CFSD) with its checksum */
#define TEXT_BYTES 2048

typedef struct {
    int method; /* 0 email, 1 wii */
    char recipient_email[256];
    char sender_name[128];
    char sender_email[256];
    char recipient_wii[20];
    char message[TEXT_BYTES];
    char card_text[TEXT_BYTES];
    int font;       /* index into meta.fonts */
    int color;      /* index into the text colour palette, -1 follows the background */
    int dark;       /* card background */
    int expression; /* index into meta.expressions */
    int attach;     /* wii: 0 none, 1 picture, 2 PictoCard */
    int doodle;     /* 1: the card is a doodle drawn on the 3DS, 0: a full PictoCard */
    int letterhead; /* index into meta.letterheads, -1 default */
    int has_mii;
    u8 mii[MII_BYTES];
    char mii_name[48];
    char picture_name[128];
    unsigned char *picture_jpeg; /* full size, for sending */
    size_t picture_len;
    unsigned char *preview_jpeg; /* smaller, for live previews */
    size_t preview_len;
} draft_t;

typedef struct {
    const char *name;
    u32 rgb;
} swatch_t;

static const swatch_t PALETTE[] = {
    { "Black", 0x111111 }, { "White", 0xf5f5f7 }, { "Red", 0xd62d20 },  { "Orange", 0xf08a24 },
    { "Gold", 0xc9a227 },  { "Green", 0x2e9e4f }, { "Teal", 0x14a3a3 }, { "Blue", 0x1f6fd1 },
    { "Sky", 0x4fb4ea },   { "Purple", 0x7d4cc4 }, { "Pink", 0xe0569b }, { "Brown", 0x7a4b2a },
};
#define PALETTE_COUNT ((int)(sizeof PALETTE / sizeof *PALETTE))

static draft_t draft;

enum { SCR_CONNECT, SCR_COMPOSE, SCR_REVIEW, SCR_VERIFY, SCR_DONE };

enum {
    F_NONE, F_METHOD, F_REC_EMAIL, F_SENDER_NAME, F_SENDER_EMAIL, F_REC_WII, F_FRIEND, F_ATTACH, F_LETTERHEAD,
    F_STYLE, F_PICTURE, F_MII, F_EXPRESSION, F_BACKGROUND, F_FONT, F_COLOR, F_MESSAGE, F_CARD_TEXT, F_REVIEW,
    F_SEND, F_EDIT, F_CODE, F_VERIFY_SEND, F_VERIFY_BACK, F_ANOTHER, F_EXIT, F_RETRY, F_THEME
};

/* ── Rows: every screen on the touch screen is a list of these ─────────── */

enum { R_HEADER, R_INFO, R_FIELD, R_BUTTON, R_ITEM };

typedef struct {
    int id;      /* field id, or the picker value */
    int kind;
    int primary; /* drawn as the main button; for fields, the value is a placeholder */
    int has_swatch;
    u32 swatch;
    float h;     /* height, measured once */
    char label[64];
    char value[200];
} row_t;

typedef struct {
    float scroll, target;
    int focus;     /* row index */
    int dragging;  /* stylus moved far enough to scroll instead of tap */
    float drag_scroll;
} list_t;

#define LIST_Y 26
#define LIST_H (SCR_H - LIST_Y)
#define MAX_ROWS 640

static row_t rows[MAX_ROWS];
static int row_count;
static list_t form_list;

static int screen = SCR_CONNECT;
static int focus_id = F_METHOD;
static int frame;
static int quit;

static char alert_text[256];
static int alert_error, alert_frames;

static int version, preview_version = -1, requested_version = -1, changed_frame;
static image_t *preview_card, *preview_letter[MAX_LETTER_PAGES];
static int preview_letters, preview_needs_image;
static char preview_error[256];
static texture_t tex_preview, tex_logo;
static int tex_for = -2, tex_for_version = -1; /* what tex_preview holds: -1 card, n letter page */
static int letter_page, show_card; /* top screen in Wii mode: letter page or the attached card */
static int pending_review;

static char verify_ticket[64], verify_email[128], verify_code[16];
static int busy_modal;
static int done_wii;
static char working_text[96]; /* a slow step on this thread (encoding a picture): drawn before it runs */
static void (*working_fn)(void);

/* ── Pickers and the file browser ───────────────────────────────────────── */

typedef void (*pick_cb)(int value);

static struct {
    int active;
    int files; /* browsing the SD card */
    char title[64];
    row_t *items;
    int count;
    list_t list;
    pick_cb cb;
    /* the file to show on the top screen for the highlighted entry, or NULL */
    const char *(*preview_path)(int value);
} picker;

#define MAX_FILES 600
static char browse_dir[512];
static char (*file_names)[128];
static int *file_is_dir;

static void show_alert(const char *msg, int error)
{
    snprintf(alert_text, sizeof alert_text, "%s", msg);
    alert_error = error;
    alert_frames = 60 * 8;
}

static void draft_changed(void)
{
    version++;
    changed_frame = frame;
}

static u32 draft_text_color(void)
{
    if (draft.color >= 0 && draft.color < PALETTE_COUNT) return PALETTE[draft.color].rgb;
    return draft.dark ? 0xf5f5f7 : 0x111111;
}

static int makes_card(void) { return draft.method == 0 || draft.attach == 2; }
/* a doodle replaces the whole PictoCard layout: no Mii, text, font or background on the picture */
static int is_doodle(void) { return draft.doodle && makes_card(); }
static int styled_card(void) { return makes_card() && !is_doodle(); }
static int has_picture_field(void) { return draft.method == 0 || draft.attach != 0; }

static int count_chars(const char *s)
{
    int n = 0;
    for (; *s; s++) n += (*s & 0xC0) != 0x80;
    return n;
}

/* ── Draft → JSON ────────────────────────────────────────────────────────── */

static void add_b64(cJSON *obj, const char *key, const unsigned char *data, size_t len)
{
    char *b = base64_encode(data, len);
    if (b) {
        cJSON_AddStringToObject(obj, key, b);
        free(b);
    }
}

static char *draft_json(int for_send)
{
    cJSON *o = cJSON_CreateObject();
    int wii = draft.method == 1;
    cJSON_AddStringToObject(o, "method", wii ? "wii" : "email");
    cJSON_AddStringToObject(o, "senderName", draft.sender_name);
    cJSON_AddStringToObject(o, "cardText", draft.message);
    cJSON_AddStringToObject(o, "wiiCardText", draft.card_text);
    if (meta.font_count) cJSON_AddStringToObject(o, "font", meta.fonts[draft.font].id);
    char hex[8];
    snprintf(hex, sizeof hex, "#%06x", (unsigned)draft_text_color());
    cJSON_AddStringToObject(o, "textColor", hex);
    cJSON_AddStringToObject(o, "background", draft.dark ? "dark" : "light");
    if (meta.expression_count) cJSON_AddStringToObject(o, "miiExpression", meta.expressions[draft.expression].id);
    if (is_doodle()) cJSON_AddStringToObject(o, "cardType", "doodle");
    cJSON_AddStringToObject(o, "wiiAttach", draft.attach == 1 ? "image" : draft.attach == 2 ? "card" : "none");
    cJSON_AddStringToObject(o, "letterhead", draft.letterhead >= 0 ? meta.letterheads[draft.letterhead].id : "");
    if (has_picture_field()) {
        if (for_send && draft.picture_jpeg) add_b64(o, "image", draft.picture_jpeg, draft.picture_len);
        else if (draft.preview_jpeg) add_b64(o, "image", draft.preview_jpeg, draft.preview_len);
    }
    /* A 3DS Mii can be on the card; the letter's Mii face must be a Wii Mii, so letters go without one */
    if (draft.has_mii && styled_card()) add_b64(o, "mii", draft.mii, MII_BYTES);
    if (for_send) {
        if (wii) cJSON_AddStringToObject(o, "recipientWii", draft.recipient_wii);
        else {
            cJSON_AddStringToObject(o, "recipientEmail", draft.recipient_email);
            cJSON_AddStringToObject(o, "senderEmail", draft.sender_email);
        }
    }
    char *s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    return s;
}

/* ── Pictures ────────────────────────────────────────────────────────────── */

static image_t *pending_picture;
static char pending_name[128];

/* Encode a decoded picture (or doodle) as the draft's full-size and preview JPEGs. */
static void store_pending_picture(void)
{
    image_t *img = pending_picture;
    pending_picture = NULL;
    if (!img) return;
    size_t full_len = 0, prev_len = 0;
    image_t *big = image_fit(img, 1280, 1280);
    unsigned char *full = big ? image_encode_jpeg(big, 88, &full_len) : NULL;
    image_free(big);
    image_t *small = image_fit(img, 720, 720);
    unsigned char *prev = small ? image_encode_jpeg(small, 80, &prev_len) : NULL;
    image_free(small);
    image_free(img);
    if (!full || !prev) {
        free(full);
        free(prev);
        show_alert("The 3DS ran out of memory loading that picture.", 1);
        return;
    }
    free(draft.picture_jpeg);
    free(draft.preview_jpeg);
    draft.picture_jpeg = full;
    draft.picture_len = full_len;
    draft.preview_jpeg = prev;
    draft.preview_len = prev_len;
    snprintf(draft.picture_name, sizeof draft.picture_name, "%s", pending_name);
    draft_changed();
}

static void queue_picture(image_t *img, const char *name)
{
    image_free(pending_picture);
    pending_picture = img;
    snprintf(pending_name, sizeof pending_name, "%s", name);
    snprintf(working_text, sizeof working_text, "Getting the picture ready...");
    working_fn = store_pending_picture;
}

static char pending_path[640];

static void load_pending_file(void)
{
    size_t len;
    unsigned char *data = read_file(pending_path, &len, 24 * 1024 * 1024);
    if (!data) {
        show_alert("Could not read that file. It may be too large (24 MB at most).", 1);
        return;
    }
    image_t *img = image_decode(data, len, 1600);
    free(data);
    if (!img) {
        show_alert(image_error(), 1);
        return;
    }
    const char *name = strrchr(pending_path, '/');
    queue_picture(img, name ? name + 1 : pending_path); /* encoded next frame */
}

static void store_doodle(const image_t *drawing)
{
    image_t *copy = image_fit(drawing, drawing->w, drawing->h);
    if (copy) queue_picture(copy, "Your doodle");
}

static void clear_picture(void)
{
    free(draft.picture_jpeg);
    free(draft.preview_jpeg);
    draft.picture_jpeg = draft.preview_jpeg = NULL;
    draft.picture_len = draft.preview_len = 0;
    draft.picture_name[0] = 0;
}

/* A photo is not a doodle, so switching the card style starts the picture over. */
static void set_style(int doodle)
{
    if (draft.doodle != doodle) clear_picture();
    draft.doodle = doodle;
}

/* Attach choices on the form: none, picture, PictoCard, doodle (a PictoCard drawn here) */
#define ATTACH_CHOICES 4
static int attach_choice(void) { return draft.attach == 2 && draft.doodle ? 3 : draft.attach; }

static void set_attach_choice(int choice)
{
    if (choice == 3) {
        draft.attach = 2;
        set_style(1);
    } else {
        if (choice == 2 || choice == 1) set_style(0);
        draft.attach = choice;
    }
}

static const char *attach_name(int a)
{
    return a == 1 ? "A picture" : a == 2 ? "A PictoCard" : a == 3 ? "A doodle" : "No attachment";
}

/* ── Text entry with the system keyboard ────────────────────────────────── */

enum { KB_TEXT, KB_EMAIL, KB_NUMBER, KB_MULTILINE };

static void keyboard(const char *hint, char *buf, size_t size, int max_chars, int kind)
{
    SwkbdState kb;
    swkbdInit(&kb, kind == KB_NUMBER ? SWKBD_TYPE_NUMPAD : SWKBD_TYPE_NORMAL, 2, max_chars);
    swkbdSetHintText(&kb, hint);
    swkbdSetInitialText(&kb, buf);
    swkbdSetButton(&kb, SWKBD_BUTTON_LEFT, "Cancel", false);
    swkbdSetButton(&kb, SWKBD_BUTTON_RIGHT, "OK", true);
    u32 features = SWKBD_PREDICTIVE_INPUT;
    if (kind == KB_MULTILINE) features |= SWKBD_MULTILINE;
    if (kind == KB_EMAIL || kind == KB_NUMBER) features = 0;
    swkbdSetFeatures(&kb, features);
    char *out = malloc(size);
    if (!out) return;
#ifdef AUTOTEST
    const char *scripted = autotest_keyboard();
    SwkbdButton b = SWKBD_BUTTON_RIGHT;
    if (scripted) snprintf(out, size, "%s", scripted);
    else b = swkbdInputText(&kb, out, size);
#else
    SwkbdButton b = swkbdInputText(&kb, out, size);
#endif
    if (b == SWKBD_BUTTON_RIGHT) {
        if (kind == KB_NUMBER) {
            /* keep the digits, so "1234 5678 ..." works too */
            int n = 0;
            for (char *p = out; *p && n < (int)size - 1; p++)
                if (*p >= '0' && *p <= '9') buf[n++] = *p;
            buf[n] = 0;
        } else {
            snprintf(buf, size, "%s", out);
        }
        draft_changed();
    }
    free(out);
}

/* ── Mii ─────────────────────────────────────────────────────────────────── */

static void choose_mii(void)
{
    MiiSelectorConf conf;
    MiiSelectorReturn ret;
    miiSelectorInit(&conf);
    miiSelectorSetTitle(&conf, "Choose a Mii for your PictoCard");
    miiSelectorSetOptions(&conf, MIISELECTOR_CANCEL | MIISELECTOR_GUESTS);
    miiSelectorLaunch(&conf, &ret);
    if (ret.no_mii_selected) return;
    if (!miiSelectorChecksumIsValid(&ret)) {
        show_alert("That Mii's data didn't come through correctly. Please try again.", 1);
        return;
    }
    _Static_assert(sizeof(MiiData) + 4 == MII_BYTES, "Mii data with its checksum");
    memcpy(draft.mii, &ret.mii, MII_BYTES); /* MiiData, padding, then the big-endian CRC */
    miiSelectorReturnGetName(&ret, draft.mii_name, sizeof draft.mii_name);
    draft.has_mii = 1;
    draft_changed();
}

/* ── List drawing and input ─────────────────────────────────────────────── */

static float row_height(const row_t *r)
{
    switch (r->kind) {
    case R_HEADER: return 22;
    case R_INFO:
        if (!r->h) ((row_t *)r)->h = text_height(12, BOT_W - 24, r->value) + 8;
        return r->h;
    case R_FIELD: return 36;
    case R_BUTTON: return 36;
    default: return 26;
    }
}

static int selectable(const row_t *r) { return r->kind == R_FIELD || r->kind == R_BUTTON || r->kind == R_ITEM; }

static float list_total(const row_t *items, int n)
{
    float h = 4;
    for (int i = 0; i < n; i++) h += row_height(&items[i]);
    return h + 4;
}

static float row_top(const row_t *items, int index)
{
    float y = 4;
    for (int i = 0; i < index; i++) y += row_height(&items[i]);
    return y;
}

static void list_clamp(list_t *l, const row_t *items, int n)
{
    float max = list_total(items, n) - LIST_H;
    if (max < 0) max = 0;
    if (l->target < 0) l->target = 0;
    if (l->target > max) l->target = max;
    if (l->scroll < 0) l->scroll = 0;
    if (l->scroll > max) l->scroll = max;
}

static void list_reveal(list_t *l, const row_t *items, int n)
{
    if (l->focus < 0 || l->focus >= n) return;
    float y = row_top(items, l->focus), h = row_height(&items[l->focus]);
    if (y - 4 < l->target) l->target = y - 4;
    if (y + h + 4 > l->target + LIST_H) l->target = y + h + 4 - LIST_H;
    list_clamp(l, items, n);
}

static void list_move(list_t *l, const row_t *items, int n, int dir)
{
    int i = l->focus;
    for (int step = 0; step < n; step++) {
        i += dir;
        if (i < 0 || i >= n) return;
        if (selectable(&items[i])) {
            l->focus = i;
            list_reveal(l, items, n);
            return;
        }
    }
}

static void list_fix_focus(list_t *l, const row_t *items, int n)
{
    if (l->focus >= 0 && l->focus < n && selectable(&items[l->focus])) return;
    for (int i = 0; i < n; i++)
        if (selectable(&items[i])) {
            l->focus = i;
            return;
        }
    l->focus = -1;
}

/* Scrolling, D-Pad focus and taps. Returns the row tapped or chosen with A, or -1. */
static int list_input(list_t *l, const row_t *items, int n, const input_t *in)
{
    int chosen = -1;
    if (in->repeat & KEY_DUP) list_move(l, items, n, -1);
    if (in->repeat & KEY_DDOWN) list_move(l, items, n, 1);
    if (in->repeat & (KEY_ZL | KEY_L)) {
        l->target -= LIST_H - 30;
        list_clamp(l, items, n);
    }
    if (in->repeat & (KEY_ZR | KEY_R)) {
        l->target += LIST_H - 30;
        list_clamp(l, items, n);
    }
    if (abs(in->cpad.dy) > 20) {
        l->target -= in->cpad.dy / 12.0f;
        list_clamp(l, items, n);
    }
    if (in->down & KEY_A && l->focus >= 0) chosen = l->focus;

    if (in->pressed) {
        l->dragging = 0;
        l->drag_scroll = l->target;
    }
    if (in->touching && in->start_y >= LIST_Y) {
        int dy = in->y - in->start_y;
        if (abs(dy) > 6) l->dragging = 1;
        if (l->dragging) {
            l->target = l->scroll = l->drag_scroll - dy;
            list_clamp(l, items, n);
        }
    }
    if (in->released && !l->dragging && in->y >= LIST_Y) {
        float y = in->y - LIST_Y + l->scroll, top = 4;
        for (int i = 0; i < n; i++) {
            float h = row_height(&items[i]);
            if (y >= top && y < top + h) {
                if (selectable(&items[i])) {
                    l->focus = i;
                    chosen = i;
                }
                break;
            }
            top += h;
        }
    }
    l->scroll += (l->target - l->scroll) * 0.35f;
    if (abs((int)(l->target - l->scroll)) < 1) l->scroll = l->target;
    return chosen;
}

static void draw_title(const char *title, const char *right)
{
    rect(0, 0, BOT_W, LIST_Y - 2, rgb(T->panel));
    rect(0, LIST_Y - 2, BOT_W, 2, rgb(T->accent));
    text_fit(8, 4, 15, rgb(T->text), C2D_AlignLeft, right ? 200 : BOT_W - 16, title);
    if (right) text_fit(BOT_W - 8, 6, 12, rgb(T->muted), C2D_AlignRight, 100, right);
}

static void list_draw(const list_t *l, const row_t *items, int n)
{
    float y = LIST_Y + 4 - l->scroll;
    for (int i = 0; i < n; i++) {
        const row_t *r = &items[i];
        float h = row_height(r);
        if (y + h < LIST_Y || y > SCR_H) {
            y += h;
            continue;
        }
        int focused = i == l->focus;
        switch (r->kind) {
        case R_HEADER:
            text(10, y + 6, 12, rgb(T->accent), C2D_AlignLeft, 0, r->label);
            break;
        case R_INFO:
            text(12, y + 3, 12, rgb(r->primary ? T->danger : T->muted), C2D_AlignLeft, BOT_W - 24, r->value);
            break;
        case R_FIELD: {
            rect(6, y + 1, BOT_W - 12, h - 2, rgb(focused ? T->select : T->elev));
            if (focused) frame_rect(6, y + 1, BOT_W - 12, h - 2, 1, rgb(T->accent));
            text(14, y + 3, 11, rgb(T->muted), C2D_AlignLeft, 0, r->label);
            float vx = 14;
            if (r->has_swatch) {
                rect(14, y + 18, 12, 12, rgb(r->swatch));
                frame_rect(14, y + 18, 12, 12, 1, rgb(T->line));
                vx = 32;
            }
            text_fit(vx, y + 17, 14, rgb(r->primary ? T->muted : T->text), C2D_AlignLeft, BOT_W - vx - 20, r->value);
            break;
        }
        case R_BUTTON: {
            u32 bg = r->primary ? T->accent : T->elev;
            rect(6, y + 2, BOT_W - 12, h - 4, rgb(bg));
            if (focused) frame_rect(5, y + 1, BOT_W - 10, h - 2, 2, rgb(r->primary ? T->text : T->accent));
            text_fit(BOT_W / 2, y + 10, 15, rgb(r->primary ? T->on_accent : T->text), C2D_AlignCenter, BOT_W - 24,
                     r->label);
            break;
        }
        default: {
            if (focused) rect(4, y, BOT_W - 8, h, rgb(T->select));
            float x = 12;
            if (r->has_swatch) {
                rect(12, y + 6, 14, 14, rgb(r->swatch));
                frame_rect(12, y + 6, 14, 14, 1, rgb(T->line));
                x = 32;
            }
            text_fit(x, y + 5, 14, rgb(focused ? T->text : T->text), C2D_AlignLeft, BOT_W - x - 12, r->label);
        }
        }
        y += h;
    }
    /* scroll bar */
    float total = list_total(items, n);
    if (total > LIST_H) {
        float bh = LIST_H * LIST_H / total, by = LIST_Y + l->scroll / (total - LIST_H) * (LIST_H - bh);
        rect(BOT_W - 4, by, 3, bh, rgba(T->muted, 160));
    }
}

static row_t *add_row(int kind, int id, const char *label, const char *value)
{
    if (row_count >= MAX_ROWS) return &rows[MAX_ROWS - 1];
    row_t *r = &rows[row_count++];
    memset(r, 0, sizeof *r);
    r->kind = kind;
    r->id = id;
    snprintf(r->label, sizeof r->label, "%s", label ? label : "");
    snprintf(r->value, sizeof r->value, "%s", value ? value : "");
    return r;
}

/* ── Picker ──────────────────────────────────────────────────────────────── */

static row_t pick_rows[MAX_FILES + 4];

static void picker_begin(const char *title, pick_cb cb)
{
    memset(&picker.list, 0, sizeof picker.list);
    snprintf(picker.title, sizeof picker.title, "%s", title);
    picker.items = pick_rows;
    picker.count = 0;
    picker.cb = cb;
    picker.files = 0;
    picker.preview_path = NULL;
}

static row_t *pick_add(int value, const char *label, int header)
{
    if (picker.count >= (int)(sizeof pick_rows / sizeof *pick_rows)) return &pick_rows[picker.count - 1];
    row_t *r = &pick_rows[picker.count++];
    memset(r, 0, sizeof *r);
    r->kind = header ? R_HEADER : R_ITEM;
    r->id = value;
    snprintf(r->label, sizeof r->label, "%s", label);
    return r;
}

static void picker_show(int selected_value)
{
    picker.active = 1;
    picker.list.focus = -1;
    for (int i = 0; i < picker.count; i++)
        if (picker.items[i].kind == R_ITEM && picker.items[i].id == selected_value) picker.list.focus = i;
    list_fix_focus(&picker.list, picker.items, picker.count);
    list_reveal(&picker.list, picker.items, picker.count);
    picker.list.scroll = picker.list.target;
}

/* File browser: folders first, then pictures, A-Z */

static int is_picture_name(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot) return 0;
    return !strcasecmp(dot, ".jpg") || !strcasecmp(dot, ".jpeg") || !strcasecmp(dot, ".png") ||
           !strcasecmp(dot, ".bmp") || !strcasecmp(dot, ".gif") || !strcasecmp(dot, ".mpo");
}

static int *sort_order;

static int by_name(const void *a, const void *b)
{
    int i = *(const int *)a, j = *(const int *)b;
    if (file_is_dir[i] != file_is_dir[j]) return file_is_dir[j] - file_is_dir[i];
    return strcasecmp(file_names[i], file_names[j]);
}

static void file_chosen(int value);
static const char *file_preview(int value);

static void browse(const char *dir)
{
    if (!file_names) {
        file_names = malloc(sizeof *file_names * MAX_FILES);
        file_is_dir = malloc(sizeof *file_is_dir * MAX_FILES);
        sort_order = malloc(sizeof *sort_order * MAX_FILES);
        if (!file_names || !file_is_dir || !sort_order) return;
    }
    snprintf(browse_dir, sizeof browse_dir, "%s", dir);
    int n = 0;
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) && n < MAX_FILES) {
            if (e->d_name[0] == '.') continue;
            int is_dir = e->d_type == DT_DIR;
            if (!is_dir && !is_picture_name(e->d_name)) continue;
            snprintf(file_names[n], sizeof file_names[n], "%s", e->d_name);
            file_is_dir[n] = is_dir;
            sort_order[n] = n;
            n++;
        }
        closedir(d);
    }
    qsort(sort_order, n, sizeof *sort_order, by_name);

    const char *shown = strncmp(dir, "sdmc:", 5) ? dir : dir + 5;
    picker_begin(*shown ? shown : "/", file_chosen);
    picker.files = 1;
    picker.preview_path = file_preview;
    if (strcmp(dir, "sdmc:/")) pick_add(-1, ".. (up one folder)", 0);
    for (int k = 0; k < n; k++) {
        int i = sort_order[k];
        char label[160];
        snprintf(label, sizeof label, "%s%s", file_names[i], file_is_dir[i] ? "/" : "");
        pick_add(i, label, 0);
    }
    if (!n) pick_add(-2, "No pictures here. Choose another folder.", 1)->kind = R_INFO;
    picker_show(-1);
}

static void file_chosen(int value)
{
    char next[640];
    if (value == -1) {
        snprintf(next, sizeof next, "%s", browse_dir);
        size_t len = strlen(next);
        if (len > 6 && next[len - 1] == '/') next[--len] = 0;
        char *slash = strrchr(next, '/');
        if (slash) slash[1] = 0;
        browse(next);
        return;
    }
    if (value < 0) return;
    snprintf(next, sizeof next, "%s%s%s", browse_dir, browse_dir[strlen(browse_dir) - 1] == '/' ? "" : "/",
             file_names[value]);
    if (file_is_dir[value]) {
        strncat(next, "/", sizeof next - strlen(next) - 1);
        browse(next);
        return;
    }
    picker.active = 0;
    snprintf(browse_dir, sizeof browse_dir, "%s", next);
    char *slash = strrchr(browse_dir, '/');
    if (slash) slash[1] = 0;
    snprintf(pending_path, sizeof pending_path, "%s", next);
    snprintf(working_text, sizeof working_text, "Opening the picture...");
    working_fn = load_pending_file;
}

static void open_files(void)
{
    static int started;
    if (!started) {
        started = 1;
        struct stat st;
        /* the Nintendo 3DS Camera keeps its photos in DCIM */
        snprintf(browse_dir, sizeof browse_dir, "%s", !stat("sdmc:/DCIM", &st) ? "sdmc:/DCIM/" : "sdmc:/");
    }
    browse(browse_dir);
}


/* ── The Nintendo 3DS Camera's photos ───────────────────────────────────── */

/* The Camera app saves to sdmc:/DCIM/<nnn>NIN03/HNI_<nnnn>.JPG (and .MPO for 3D
 * photos), so sorting the paths backwards lists the newest first. */
#define MAX_PHOTOS 600
static char (*photo_paths)[96];
static int photo_count;

static int by_path_desc(const void *a, const void *b) { return strcmp((const char *)b, (const char *)a); }

static const char *photo_preview(int value) { return value >= 0 && value < photo_count ? photo_paths[value] : NULL; }

static void photo_chosen(int value)
{
    if (value < 0 || value >= photo_count) return;
    snprintf(pending_path, sizeof pending_path, "%s", photo_paths[value]);
    snprintf(working_text, sizeof working_text, "Opening the picture...");
    working_fn = load_pending_file;
}

static void open_camera_photos(void)
{
    if (!photo_paths) photo_paths = malloc(sizeof *photo_paths * MAX_PHOTOS);
    if (!photo_paths) return;
    photo_count = 0;
    DIR *top = opendir("sdmc:/DCIM");
    if (top) {
        struct dirent *d;
        while ((d = readdir(top)) && photo_count < MAX_PHOTOS) {
            if (d->d_name[0] == '.' || d->d_type != DT_DIR) continue;
            char dir[64];
            snprintf(dir, sizeof dir, "sdmc:/DCIM/%.40s", d->d_name);
            DIR *sub = opendir(dir);
            if (!sub) continue;
            struct dirent *e;
            while ((e = readdir(sub)) && photo_count < MAX_PHOTOS) {
                if (e->d_type == DT_DIR || !is_picture_name(e->d_name)) continue;
                snprintf(photo_paths[photo_count++], sizeof *photo_paths, "%s/%.40s", dir, e->d_name);
            }
            closedir(sub);
        }
        closedir(top);
    }
    qsort(photo_paths, photo_count, sizeof *photo_paths, by_path_desc);

    picker_begin("3DS Camera photos", photo_chosen);
    picker.preview_path = photo_preview;
    for (int i = 0; i < photo_count; i++) {
        const char *name = strrchr(photo_paths[i], '/') + 1;
        char label[96];
        const char *dot = strrchr(name, '.');
        int mpo = dot && !strcasecmp(dot, ".mpo");
        /* the SD card's own timestamp; stat() on sdmc: leaves it out */
        u64 mtime = 0;
        struct stat st;
        if (R_FAILED(archive_getmtime(photo_paths[i], &mtime)) || !mtime) mtime = !stat(photo_paths[i], &st) ? st.st_mtime : 0;
        time_t t = (time_t)mtime;
        struct tm tm;
        gmtime_r(&t, &tm);
        if (mtime > 0 && tm.tm_year > 100) { /* a clock that was never set reads 2000-01-01 */
            snprintf(label, sizeof label, "%04d-%02d-%02d %02d:%02d  %.*s%s", tm.tm_year + 1900, tm.tm_mon + 1,
                     tm.tm_mday, tm.tm_hour, tm.tm_min, dot ? (int)(dot - name) : 40, name, mpo ? " (3D)" : "");
        } else {
            snprintf(label, sizeof label, "%.*s%s", dot ? (int)(dot - name) : 40, name, mpo ? " (3D)" : "");
        }
        pick_add(i, label, 0);
    }
    if (!photo_count)
        pick_add(-2, "No photos from the Nintendo 3DS Camera on this SD card. Take one with the camera, or browse the SD card.",
                 1)->kind = R_INFO;
    picker_show(0);
}

static void store_photo(image_t *photo) { queue_picture(photo, "Camera photo"); }

static void open_camera(void)
{
    const char *err = NULL;
    if (!camera_open(store_photo, &err)) show_alert(err, 1);
}

/* Thumbnails of the highlighted file, shortly after the highlight stops moving */
static texture_t tex_pick;
static char pick_tex_path[640];
static int pick_tex_failed, pick_focus_frame, pick_last_focus = -2;

static const char *file_preview(int value)
{
    static char path[640];
    if (value < 0 || file_is_dir[value]) return NULL;
    snprintf(path, sizeof path, "%s%s%s", browse_dir, browse_dir[strlen(browse_dir) - 1] == '/' ? "" : "/",
             file_names[value]);
    return path;
}

static const char *highlighted_path(void)
{
    if (!picker.active || !picker.preview_path || picker.list.focus < 0) return NULL;
    return picker.preview_path(picker.items[picker.list.focus].id);
}

static void update_pick_preview(void)
{
    if (picker.list.focus != pick_last_focus) {
        pick_last_focus = picker.list.focus;
        pick_focus_frame = frame;
    }
    const char *path = highlighted_path();
    if (!path || !strcmp(path, pick_tex_path) || frame - pick_focus_frame < 10) return;
    snprintf(pick_tex_path, sizeof pick_tex_path, "%s", path);
    pick_tex_failed = 1;
    size_t len;
    unsigned char *data = read_file(path, &len, 12 * 1024 * 1024);
    image_t *img = data ? image_decode(data, len, 0) : NULL;
    free(data);
    image_t *fit = img ? image_fit(img, TOP_W - 16, SCR_H - 24 - 20 - 8) : NULL;
    image_free(img);
    if (fit && tex_set(&tex_pick, fit)) pick_tex_failed = 0;
    image_free(fit);
}

static void draw_pick_preview(void)
{
    const char *path = highlighted_path();
    float area_y = 28, area_h = SCR_H - 24 - 20 - 8;
    if (path && !strcmp(path, pick_tex_path) && !pick_tex_failed) {
        float x = (TOP_W - tex_pick.w) / 2.0f, y = area_y + (area_h - tex_pick.h) / 2.0f;
        tex_draw(&tex_pick, x, y, 1, 1);
    } else if (path) {
        text(TOP_W / 2, area_y + area_h / 2 - 8, 14, rgb(T->muted), C2D_AlignCenter, 0,
             !strcmp(path, pick_tex_path) ? "This picture can't be shown." : "Loading the picture...");
    }
}

static void picker_update(const input_t *in)
{
    int chosen = list_input(&picker.list, picker.items, picker.count, in);
    if (in->down & KEY_B) {
        if (picker.files && strcmp(browse_dir, "sdmc:/")) {
            file_chosen(-1);
            return;
        }
        picker.active = 0;
        return;
    }
    if (in->down & KEY_START) {
        picker.active = 0;
        return;
    }
    if (chosen >= 0) {
        int value = picker.items[chosen].id;
        if (!picker.files) picker.active = 0;
        if (picker.cb) picker.cb(value);
        draft_changed();
    }
}

static void picker_draw(void)
{
    list_draw(&picker.list, picker.items, picker.count);
    draw_title(picker.title, picker.files ? "B: back" : "B: cancel");
}

/* ── Field values ────────────────────────────────────────────────────────── */

static const char *select_value(int id, u32 *swatch, int *has_swatch)
{
    static char buf[200];
    *has_swatch = 0;
    switch (id) {
    case F_METHOD: return draft.method ? "A Wii (Message Board letter)" : "Email";
    case F_ATTACH: return attach_name(attach_choice());
    case F_STYLE: return draft.doodle ? "Doodle (your drawing is the card)" : "PictoCard (picture and message)";
    case F_LETTERHEAD:
        if (draft.letterhead < 0) return "Default (plain letter)";
        return meta.letterheads[draft.letterhead].name;
    case F_PICTURE:
        if (draft.picture_jpeg) return draft.picture_name;
        return is_doodle() ? "Tap to draw" : "Tap to take or choose a picture";
    case F_MII:
        if (!draft.has_mii) return "No Mii";
        return draft.mii_name[0] ? draft.mii_name : "Your Mii";
    case F_EXPRESSION: return meta.expression_count ? meta.expressions[draft.expression].name : "Normal";
    case F_BACKGROUND: return draft.dark ? "Dark" : "Light";
    case F_FONT: return meta.font_count ? meta.fonts[draft.font].name : "Rodin NTLG";
    case F_COLOR:
        *has_swatch = 1;
        *swatch = draft_text_color();
        if (draft.color < 0) return "Automatic";
        return PALETTE[draft.color].name;
    }
    (void)buf;
    return "";
}

static void cycle(int id, int dir)
{
    switch (id) {
    case F_METHOD: draft.method = !draft.method; break;
    case F_ATTACH: set_attach_choice((attach_choice() + ATTACH_CHOICES + dir) % ATTACH_CHOICES); break;
    case F_STYLE: set_style(!draft.doodle); break;
    case F_LETTERHEAD:
        if (!meta.letterhead_count) return;
        draft.letterhead += dir;
        if (draft.letterhead < -1) draft.letterhead = meta.letterhead_count - 1;
        if (draft.letterhead >= meta.letterhead_count) draft.letterhead = -1;
        break;
    case F_EXPRESSION:
        if (!meta.expression_count) return;
        draft.expression = (draft.expression + meta.expression_count + dir) % meta.expression_count;
        break;
    case F_FONT:
        if (!meta.font_count) return;
        draft.font = (draft.font + meta.font_count + dir) % meta.font_count;
        break;
    case F_COLOR:
        draft.color += dir;
        if (draft.color < -1) draft.color = PALETTE_COUNT - 1;
        if (draft.color >= PALETTE_COUNT) draft.color = -1;
        break;
    case F_BACKGROUND: draft.dark = !draft.dark; break;
    default: return;
    }
    draft_changed();
}

static int picking_field;

static void picked(int value)
{
    switch (picking_field) {
    case F_METHOD: draft.method = value; break;
    case F_ATTACH: set_attach_choice(value); break;
    case F_STYLE: set_style(value); break;
    case F_LETTERHEAD: draft.letterhead = value; break;
    case F_EXPRESSION: draft.expression = value; break;
    case F_FONT: draft.font = value; break;
    case F_COLOR: draft.color = value; break;
    case F_BACKGROUND: draft.dark = value; break;
    case F_MII:
        if (value == 1) choose_mii();
        else if (value == 0) draft.has_mii = 0;
        break;
    case F_PICTURE:
        if (value == 1) open_files();
        else if (value == 2) doodle_open(store_doodle);
        else if (value == 3) open_camera();
        else if (value == 4) open_camera_photos();
        else if (value == 0) clear_picture();
        break;
    }
}

static void open_select(int id)
{
    picking_field = id;
    int sel = -1;
    switch (id) {
    case F_METHOD:
        picker_begin("Send to", picked);
        pick_add(0, "Email", 0);
        pick_add(1, "A Wii (Message Board letter)", 0);
        sel = draft.method;
        break;
    case F_ATTACH:
        picker_begin("Attach to the letter", picked);
        for (int i = 0; i < ATTACH_CHOICES; i++) pick_add(i, attach_name(i), 0);
        sel = attach_choice();
        break;
    case F_STYLE:
        picker_begin("Card style", picked);
        pick_add(0, "PictoCard (picture and message)", 0);
        pick_add(1, "Doodle (your drawing is the card)", 0);
        sel = draft.doodle;
        break;
    case F_BACKGROUND:
        picker_begin("Card background", picked);
        pick_add(0, "Light", 0);
        pick_add(1, "Dark", 0);
        sel = draft.dark;
        break;
    case F_LETTERHEAD: {
        picker_begin("Letterhead", picked);
        pick_add(-1, "Default (plain letter)", 0);
        const char *group = "";
        for (int i = 0; i < meta.letterhead_count; i++) {
            if (strcmp(group, meta.letterheads[i].group)) {
                group = meta.letterheads[i].group;
                int single = i + 1 >= meta.letterhead_count || strcmp(meta.letterheads[i + 1].group, group);
                if (*group && !(single && !strcmp(meta.letterheads[i].name, group))) pick_add(-100, group, 1);
            }
            pick_add(i, meta.letterheads[i].name, 0);
        }
        sel = draft.letterhead;
        break;
    }
    case F_EXPRESSION:
        picker_begin("Mii expression", picked);
        for (int i = 0; i < meta.expression_count; i++) pick_add(i, meta.expressions[i].name, 0);
        sel = draft.expression;
        break;
    case F_FONT:
        picker_begin("Font", picked);
        for (int i = 0; i < meta.font_count; i++) pick_add(i, meta.fonts[i].name, 0);
        sel = draft.font;
        break;
    case F_COLOR: {
        picker_begin("Text color", picked);
        row_t *r = pick_add(-1, "Automatic (follows the background)", 0);
        r->has_swatch = 1;
        r->swatch = draft.dark ? 0xf5f5f7 : 0x111111;
        for (int i = 0; i < PALETTE_COUNT; i++) {
            r = pick_add(i, PALETTE[i].name, 0);
            r->has_swatch = 1;
            r->swatch = PALETTE[i].rgb;
        }
        sel = draft.color;
        break;
    }
    case F_MII:
        if (!draft.has_mii) {
            choose_mii();
            return;
        }
        picker_begin("Mii", picked);
        pick_add(1, "Choose another Mii...", 0);
        pick_add(0, "No Mii", 0);
        sel = 1;
        break;
    case F_PICTURE:
        if (is_doodle()) {
            doodle_open(store_doodle);
            return;
        }
        picker_begin("Picture", picked);
        pick_add(3, "Take a picture with the camera", 0);
        pick_add(4, "3DS Camera photos", 0);
        pick_add(1, "Browse the SD card", 0);
        if (draft.picture_jpeg) pick_add(0, "Remove the picture", 0);
        sel = 3;
        break;
    default: return;
    }
    picker_show(sel);
}

/* ── Validation, review and sending ─────────────────────────────────────── */

static const char *validate(void)
{
    static char msg[160];
    if (draft.method == 0) {
        if (!strchr(draft.recipient_email, '@')) return "Please enter a valid recipient email address.";
        if (!draft.sender_name[0]) return "Your name is required.";
        if (!strchr(draft.sender_email, '@')) return "A valid sender email is required for verification.";
        if (!draft.picture_jpeg) return draft.doodle ? "Please draw a doodle for your card." : "Please choose a picture for your card.";
    } else {
        if (!meta.wii_enabled) return "Wii delivery isn't set up yet. Please check back soon.";
        if (strlen(draft.recipient_wii) != 16) return "Enter the 16-digit Wii Number.";
        if (!wiino_valid(draft.recipient_wii)) return "That Wii Number doesn't look right. Please check it for typos.";
        if (!draft.message[0] && !(draft.attach == 1 && draft.picture_jpeg)) return "A message is required for Wii mail.";
        if (draft.attach && !draft.picture_jpeg)
            return draft.attach == 2 && draft.doodle ? "Please draw a doodle for your card." : "Please choose a picture to attach.";
    }
    int max = meta.max_text ? meta.max_text : 500;
    if (count_chars(draft.message) > max || count_chars(draft.card_text) > max) {
        snprintf(msg, sizeof msg, "Messages can contain up to %d characters. Please shorten yours.", max);
        return msg;
    }
    return NULL;
}

static void request_preview(void)
{
    char *body = draft_json(0);
    if (body && api_preview(body)) requested_version = version;
}

static void remember_sender(void)
{
    snprintf(settings.sender_name, sizeof settings.sender_name, "%s", draft.sender_name);
    snprintf(settings.sender_email, sizeof settings.sender_email, "%s", draft.sender_email);
    if (draft.method == 1 && wiino_valid(draft.recipient_wii))
        snprintf(settings.wii_number, sizeof settings.wii_number, "%s", draft.recipient_wii);
    settings_save();
}

static void go_review(void)
{
    const char *err = validate();
    if (err) {
        show_alert(err, 1);
        return;
    }
    remember_sender();
    if (preview_version != version) {
        pending_review = 1;
        busy_modal = 1;
        if (!api.busy) request_preview();
        return;
    }
    letter_page = 0;
    show_card = 0;
    screen = SCR_REVIEW;
    focus_id = F_SEND;
}

static void start_send(void)
{
    const char *err = validate();
    if (err) {
        show_alert(err, 1);
        return;
    }
    char *body = draft_json(1);
    if (!body) {
        show_alert("The 3DS ran out of memory.", 1);
        return;
    }
    if (api_send(body)) busy_modal = 1;
}

static void send_friend_request(void)
{
    if (!meta.wii_enabled) {
        show_alert("Wii delivery isn't set up yet. Please check back soon.", 1);
        return;
    }
    if (strlen(draft.recipient_wii) != 16 || !wiino_valid(draft.recipient_wii)) {
        show_alert("Enter the recipient's 16-digit Wii Number first.", 1);
        return;
    }
    remember_sender();
    if (api_friend(draft.recipient_wii)) busy_modal = 1;
}

static void reset_draft(void)
{
    clear_picture();
    int method = draft.method, font = draft.font;
    memset(&draft, 0, sizeof draft);
    draft.method = method;
    draft.font = font;
    draft.color = -1;
    draft.letterhead = -1;
    snprintf(draft.sender_name, sizeof draft.sender_name, "%s", settings.sender_name);
    snprintf(draft.sender_email, sizeof draft.sender_email, "%s", settings.sender_email);
    snprintf(draft.recipient_wii, sizeof draft.recipient_wii, "%s", settings.wii_number);
    draft_changed();
}

static void activate(int id)
{
    int max = meta.max_text ? meta.max_text : 500;
    switch (id) {
    case F_REC_EMAIL: keyboard("Recipient's email address", draft.recipient_email, sizeof draft.recipient_email, 254, KB_EMAIL); break;
    case F_SENDER_NAME: keyboard("Your name", draft.sender_name, sizeof draft.sender_name, 60, KB_TEXT); break;
    case F_SENDER_EMAIL: keyboard("Your email address", draft.sender_email, sizeof draft.sender_email, 254, KB_EMAIL); break;
    case F_REC_WII: keyboard("The Wii's 16-digit Wii Number", draft.recipient_wii, sizeof draft.recipient_wii, 16, KB_NUMBER); break;
    case F_MESSAGE: keyboard(draft.method ? "Letter" : "Message", draft.message, sizeof draft.message, max, KB_MULTILINE); break;
    case F_CARD_TEXT: keyboard("Text on the card", draft.card_text, sizeof draft.card_text, max, KB_MULTILINE); break;
    case F_CODE: keyboard("6-digit code", verify_code, sizeof verify_code, 6, KB_NUMBER); break;
    case F_FRIEND: send_friend_request(); break;
    case F_REVIEW: go_review(); break;
    case F_SEND: start_send(); break;
    case F_EDIT: screen = SCR_COMPOSE; focus_id = F_REVIEW; break;
    case F_VERIFY_SEND:
        if (strlen(verify_code) != 6) show_alert("Enter the 6-digit code from the email.", 1);
        else if (api_verify(verify_ticket, verify_code)) busy_modal = 1;
        break;
    case F_VERIFY_BACK: screen = SCR_REVIEW; focus_id = F_SEND; break;
    case F_ANOTHER:
        reset_draft();
        screen = SCR_COMPOSE;
        focus_id = F_METHOD;
        memset(&form_list, 0, sizeof form_list);
        break;
    case F_EXIT: quit = 1; break;
    case F_RETRY: api_connect(); break;
    case F_THEME:
        settings.light = !settings.light;
        theme_set(settings.light);
        settings_save();
        break;
    default: open_select(id);
    }
}

/* ── Rows for each screen ───────────────────────────────────────────────── */

static void field(int id, const char *label, const char *value, const char *placeholder)
{
    int empty = !value || !*value;
    add_row(R_FIELD, id, label, empty ? placeholder : value)->primary = empty;
}

static void select_field(int id, const char *label)
{
    u32 sw = 0;
    int has = 0;
    const char *v = select_value(id, &sw, &has);
    row_t *r = add_row(R_FIELD, id, label, v);
    r->has_swatch = has;
    r->swatch = sw;
}

static void text_preview(char *out, size_t len, const char *s, const char *placeholder)
{
    if (!*s) {
        snprintf(out, len, "%s", placeholder);
        return;
    }
    snprintf(out, len, "%s", s);
    for (char *p = out; *p; p++)
        if (*p == '\n') *p = ' ';
}

static void wii_number_text(char *out, size_t len, const char *digits)
{
    if (strlen(digits) != 16) {
        snprintf(out, len, "%s", digits);
        return;
    }
    snprintf(out, len, "%.4s %.4s %.4s %.4s", digits, digits + 4, digits + 8, digits + 12);
}

static void build_compose(void)
{
    char buf[200];
    add_row(R_HEADER, 0, "SEND", NULL);
    select_field(F_METHOD, "Send to");
    if (draft.method == 0) {
        field(F_REC_EMAIL, "Recipient's email", draft.recipient_email, "friend@example.com");
        field(F_SENDER_NAME, "Your name", draft.sender_name, "Your name");
        field(F_SENDER_EMAIL, "Your email (for a verification code)", draft.sender_email, "you@example.com");
    } else {
        wii_number_text(buf, sizeof buf, draft.recipient_wii);
        field(F_REC_WII, "Recipient's Wii Number", buf, "Tap to enter its 16 digits");
        if (draft.recipient_wii[0] && strlen(draft.recipient_wii) == 16 && !wiino_valid(draft.recipient_wii))
            add_row(R_INFO, 0, NULL, "That Wii Number doesn't look right. Please check it for typos.")->primary = 1;
        if (meta.wii_enabled) {
            char num[32];
            wii_number_text(num, sizeof num, meta.pictocard_wii);
            snprintf(buf, sizeof buf,
                     "Letters come from PictoCard's Wii Number %s. The recipient's Wii needs it in its address book.", num);
            add_row(R_INFO, 0, NULL, buf);
            add_row(R_BUTTON, F_FRIEND, "Ask PictoCard to add that Wii", NULL);
        } else {
            add_row(R_INFO, 0, NULL, "Wii delivery isn't set up on this PictoCard server yet.")->primary = 1;
        }
        field(F_SENDER_NAME, "From", draft.sender_name, "Your name");
        text_preview(buf, sizeof buf, draft.message, "");
        field(F_MESSAGE, "Letter", buf, "Tap to write your letter");
        select_field(F_LETTERHEAD, "Letterhead");
        select_field(F_ATTACH, "Attachment");
    }
    if (has_picture_field()) {
        add_row(R_HEADER, 0, draft.method == 0 || draft.attach == 2 ? "CARD" : "PICTURE", NULL);
        if (makes_card() && draft.method == 0) select_field(F_STYLE, "Card style");
        select_field(F_PICTURE, is_doodle() ? "Doodle" : "Picture");
        if (styled_card()) {
            if (draft.method == 0) {
                text_preview(buf, sizeof buf, draft.message, "");
                field(F_MESSAGE, "Message", buf, "Tap to write a message");
            } else {
                text_preview(buf, sizeof buf, draft.card_text, "");
                field(F_CARD_TEXT, "Text on the card", buf, "Same as the letter");
            }
            select_field(F_MII, "Mii");
            if (draft.has_mii) select_field(F_EXPRESSION, "Mii expression");
            select_field(F_BACKGROUND, "Background");
            select_field(F_FONT, "Font");
            select_field(F_COLOR, "Text color");
        }
    }
    add_row(R_HEADER, 0, "", NULL);
    add_row(R_BUTTON, F_REVIEW, "Review and send", NULL)->primary = 1;
    add_row(R_BUTTON, F_THEME, settings.light ? "Dark mode" : "Light mode", NULL);
    add_row(R_BUTTON, F_EXIT, "Exit", NULL);
    snprintf(buf, sizeof buf, "PictoCard for 3DS %s - %s", APP_VERSION, settings.server);
    add_row(R_INFO, 0, NULL, buf);
}

static void build_review(void)
{
    char buf[220], num[32];
    if (draft.method == 0) {
        add_row(R_INFO, 0, NULL, "Check your card on the top screen. PictoCard will email you a code to confirm it's you.");
        snprintf(buf, sizeof buf, "To: %s", draft.recipient_email);
        add_row(R_INFO, 0, NULL, buf);
        snprintf(buf, sizeof buf, "From: %s <%s>", draft.sender_name, draft.sender_email);
        add_row(R_INFO, 0, NULL, buf);
    } else {
        add_row(R_INFO, 0, NULL, preview_letters > 1 || draft.attach
                                      ? "Check your letter on the top screen. L / R turn the pages, Y shows the attachment."
                                      : "Check your letter on the top screen.");
        wii_number_text(num, sizeof num, draft.recipient_wii);
        snprintf(buf, sizeof buf, "To the Wii: %s", num);
        add_row(R_INFO, 0, NULL, buf);
        snprintf(buf, sizeof buf, "Attachment: %s. Letterhead: %s", attach_name(attach_choice()),
                 draft.letterhead >= 0 ? meta.letterheads[draft.letterhead].name : "plain letter");
        add_row(R_INFO, 0, NULL, buf);
    }
    add_row(R_BUTTON, F_SEND, draft.method ? "Send to the Wii" : "Send", NULL)->primary = 1;
    add_row(R_BUTTON, F_EDIT, "Edit", NULL);
}

static void build_verify(void)
{
    char buf[220];
    snprintf(buf, sizeof buf, "We sent a 6-digit code to %s. Enter it to send your card. The code expires in 10 minutes.",
             verify_email);
    add_row(R_INFO, 0, NULL, buf);
    field(F_CODE, "Verification code", verify_code, "Tap to enter the code");
    add_row(R_BUTTON, F_VERIFY_SEND, "Send my card", NULL)->primary = 1;
    add_row(R_BUTTON, F_VERIFY_BACK, "Back", NULL);
}

static void build_done(void)
{
    add_row(R_INFO, 0, NULL, done_wii ? "Your letter is on its way. It will reach the Wii's Message Board after its next WiiConnect24 check."
                                      : "Your PictoCard has been sent!");
    add_row(R_BUTTON, F_ANOTHER, "Send another", NULL)->primary = 1;
    add_row(R_BUTTON, F_EXIT, "Exit", NULL);
}

static void build_connect(void)
{
    if (api.busy || (!api.finished && !api.error[0])) {
        add_row(R_INFO, 0, NULL, api.stage == STAGE_NETWORK ? "Connecting to the Internet..." : "Loading PictoCard...");
        add_row(R_INFO, 0, NULL, settings.server);
    } else {
        add_row(R_INFO, 0, NULL, api.error)->primary = 1;
        add_row(R_BUTTON, F_RETRY, "Try again", NULL)->primary = 1;
        add_row(R_BUTTON, F_EXIT, "Exit", NULL);
    }
}

static void build_rows(void)
{
    row_count = 0;
    switch (screen) {
    case SCR_CONNECT: build_connect(); break;
    case SCR_COMPOSE: build_compose(); break;
    case SCR_REVIEW: build_review(); break;
    case SCR_VERIFY: build_verify(); break;
    case SCR_DONE: build_done(); break;
    }
    /* keep the focus on the same field when rows come and go */
    form_list.focus = -1;
    for (int i = 0; i < row_count; i++)
        if (selectable(&rows[i]) && rows[i].id == focus_id) form_list.focus = i;
    list_fix_focus(&form_list, rows, row_count);
    if (form_list.focus >= 0) focus_id = rows[form_list.focus].id;
    list_clamp(&form_list, rows, row_count);
}

/* ── Job results ─────────────────────────────────────────────────────────── */

static void free_preview(void)
{
    image_free(preview_card);
    preview_card = NULL;
    for (int i = 0; i < preview_letters; i++) image_free(preview_letter[i]);
    preview_letters = 0;
    tex_for = -2;
}

static void handle_finished(void)
{
    api.finished = 0;
    int job = api.job;
    if (job == JOB_CONNECT) {
        if (api.ok) {
            screen = SCR_COMPOSE;
            focus_id = F_METHOD;
            draft.font = 0;
            for (int i = 0; i < meta.font_count; i++)
                if (!strcmp(meta.fonts[i].id, "rodinntlg")) draft.font = i;
            for (int i = 0; i < meta.expression_count; i++)
                if (!strcmp(meta.expressions[i].id, "normal")) draft.expression = i;
            if (strcmp(meta.min_client, APP_VERSION) > 0)
                show_alert("A newer PictoCard for 3DS is available. Download it from pictocard.net.", 1);
            draft_changed();
        }
        return;
    }
    if (job == JOB_PREVIEW) {
        if (api.ok) {
            free_preview();
            preview_card = api.card;
            for (int i = 0; i < api.letter_count; i++) preview_letter[i] = api.letter[i];
            preview_letters = api.letter_count;
            preview_needs_image = api.needs_image;
            preview_error[0] = 0;
            if (letter_page >= preview_letters) letter_page = 0;
        } else {
            snprintf(preview_error, sizeof preview_error, "%s", api.error);
            if (pending_review) {
                pending_review = 0;
                busy_modal = 0;
                show_alert(api.error, 1);
            }
        }
        preview_version = requested_version; /* don't retry the same draft in a loop */
        if (pending_review && preview_version == version) {
            pending_review = 0;
            busy_modal = 0;
            go_review();
        }
        return;
    }
    busy_modal = 0;
    if (!api.ok) {
        if (job == JOB_VERIFY && api.status == 410) {
            screen = SCR_REVIEW;
            focus_id = F_SEND;
        }
        show_alert(api.error, 1);
        return;
    }
    if (job == JOB_FRIEND) {
        show_alert(!strcmp(api.result, "already")
                       ? "PictoCard has already been added to that Wii. Nothing more to do!"
                       : "Friend request sent! It will appear on the Wii after its next WiiConnect24 check.", 0);
    } else if (job == JOB_SEND && !strcmp(api.result, "verify")) {
        snprintf(verify_ticket, sizeof verify_ticket, "%s", api.ticket);
        snprintf(verify_email, sizeof verify_email, "%s", api.verify_email[0] ? api.verify_email : draft.sender_email);
        verify_code[0] = 0;
        screen = SCR_VERIFY;
        focus_id = F_CODE;
        alert_frames = 0;
    } else {
        done_wii = draft.method == 1;
        screen = SCR_DONE;
        focus_id = F_ANOTHER;
        alert_frames = 0;
    }
}

/* ── Top screen ─────────────────────────────────────────────────────────── */

#define TOP_HEAD 24
#define TOP_FOOT 20

/* the picture the top screen shows now: -1 the card, n a letter page, -2 nothing */
static int top_picture(const image_t **img)
{
    *img = NULL;
    int wii = draft.method == 1;
    if (wii && !show_card && preview_letters) {
        int page = letter_page < preview_letters ? letter_page : 0;
        *img = preview_letter[page];
        return page;
    }
    if (preview_card) {
        *img = preview_card;
        return -1;
    }
    return -2;
}

static void draw_top_preview(void)
{
    const image_t *img;
    int which = top_picture(&img);
    if (img && (which != tex_for || preview_version != tex_for_version)) {
        image_t *fit = image_fit(img, TOP_W - 16, SCR_H - TOP_HEAD - TOP_FOOT - 8);
        if (fit && tex_set(&tex_preview, fit)) {
            tex_for = which;
            tex_for_version = preview_version;
        }
        image_free(fit);
    }
    float area_y = TOP_HEAD + 4, area_h = SCR_H - TOP_HEAD - TOP_FOOT - 8;
    if (img && tex_preview.ok && tex_for == which) {
        float x = (TOP_W - tex_preview.w) / 2.0f, y = area_y + (area_h - tex_preview.h) / 2.0f;
        rect(x + 3, y + 3, tex_preview.w, tex_preview.h, rgba(0x000000, 70));
        tex_draw(&tex_preview, x, y, 1, 1);
    } else {
        const char *msg = preview_error[0] ? preview_error
                        : (draft.method == 1 && !draft.attach) || has_picture_field() ? "Your preview will appear here."
                                                                                        : "";
        text(TOP_W / 2, area_y + area_h / 2 - 8, 14, rgb(preview_error[0] ? T->danger : T->muted), C2D_AlignCenter,
             TOP_W - 40, msg);
    }
    if (draft.method == 1 && preview_letters && !show_card) {
        char page[32];
        snprintf(page, sizeof page, "Page %d of %d", letter_page + 1, preview_letters);
        text(TOP_W - 8, SCR_H - TOP_FOOT + 3, 12, rgb(T->muted), C2D_AlignRight, 0, page);
    }
}

static void draw_top(void)
{
    rect(0, 0, TOP_W, SCR_H, rgb(T->bg));
    rect(0, 0, TOP_W, TOP_HEAD, rgb(T->panel));
    rect(0, TOP_HEAD, TOP_W, 1, rgb(T->line));
    text(10, 3, 18, rgb(T->accent), C2D_AlignLeft, 0, "PictoCard");
    const char *status = "";
    if (screen == SCR_CONNECT) status = "";
    else if (api.busy && api.job == JOB_PREVIEW) status = "Updating the preview...";
    else if (preview_version != version && screen == SCR_COMPOSE && meta.loaded) status = "Preview pending...";
    else if (screen == SCR_REVIEW) status = "Review";
    text_fit(TOP_W - 10, 6, 13, rgb(T->muted), C2D_AlignRight, 240, status);

    if (screen == SCR_CONNECT) {
        if (tex_logo.ok) tex_draw(&tex_logo, (TOP_W - tex_logo.w * 1.6f) / 2.0f, 114 - tex_logo.h * 1.6f, 1.6f, 1.6f);
        else text(TOP_W / 2, 80, 34, rgb(T->accent), C2D_AlignCenter, 0, "PictoCard");
        text(TOP_W / 2, 124, 15, rgb(T->text), C2D_AlignCenter, 0, "Send picture cards by email or to a Wii");
        text(TOP_W / 2, 150, 12, rgb(T->muted), C2D_AlignCenter, 0, "for Nintendo 3DS  " APP_VERSION);
    } else if (screen == SCR_DONE) {
        /* a little confetti */
        for (int i = 0; i < 60; i++) {
            u32 seed = i * 2654435761u;
            float x = (seed % 400), speed = 0.6f + (seed >> 8) % 100 / 80.0f;
            float y = ((int)(frame * speed + (seed >> 16) % 240)) % 260 - 20;
            static const u32 C[] = { 0x4f8cff, 0xff6b6b, 0x3ecf8e, 0xf0b429, 0xc77dff };
            if (y > TOP_HEAD && y < SCR_H - TOP_FOOT - 6) rect(x, y, 4, 6, rgb(C[i % 5]));
        }
        text(TOP_W / 2, 84, 30, rgb(T->ok), C2D_AlignCenter, 0, "Sent!");
        text(TOP_W / 2, 128, 14, rgb(T->text), C2D_AlignCenter, TOP_W - 40,
             done_wii ? "Your letter is on its way to the Wii." : "Your PictoCard is on its way.");
    } else if (picker.active && picker.preview_path) {
        draw_pick_preview();
    } else {
        draw_top_preview();
    }

    rect(0, SCR_H - TOP_FOOT, TOP_W, TOP_FOOT, rgb(T->panel));
    const char *hint = "";
    switch (screen) {
    case SCR_COMPOSE: hint = picker.active ? "A choose   B back" : "A edit   Left/Right change   START review"; break;
    case SCR_REVIEW: hint = draft.method ? "START send   B edit   L/R pages   Y attachment" : "START send   B edit"; break;
    case SCR_VERIFY: hint = "A enter code   START send   B back"; break;
    case SCR_DONE: hint = "A send another"; break;
    case SCR_CONNECT: hint = "A retry   START exit"; break;
    }
    text_fit(8, SCR_H - TOP_FOOT + 3, 12, rgb(T->muted), C2D_AlignLeft, 290, hint);
}

/* ── Bottom screen overlays ─────────────────────────────────────────────── */

static void draw_busy(void)
{
    rect(0, 0, BOT_W, SCR_H, rgba(0x000000, 150));
    float x = 20, y = 70, w = BOT_W - 40, h = 100;
    rect(x, y, w, h, rgb(T->panel));
    frame_rect(x, y, w, h, 1, rgb(T->line));
    const char *title = api.job == JOB_FRIEND ? "Sending a friend request"
                      : pending_review        ? "Getting your card ready"
                      : api.job == JOB_VERIFY ? "Sending your card"
                                              : "Sending your card";
    if (working_fn) title = working_text;
    text(x + 12, y + 10, 15, rgb(T->text), C2D_AlignLeft, 0, title);
    const char *stage = "Working...";
    if (!working_fn) switch (api.stage) {
        case STAGE_NETWORK: stage = "Connecting to the Internet..."; break;
        case STAGE_CHALLENGE: stage = "Getting an anti-spam check..."; break;
        case STAGE_SOLVING: stage = "Solving the anti-spam check on your 3DS..."; break;
        case STAGE_UPLOADING: stage = pending_review ? "Drawing your card..." : "Uploading your card..."; break;
        case STAGE_WAITING: stage = "Waiting for PictoCard..."; break;
        }
    text_fit(x + 12, y + 36, 13, rgb(T->muted), C2D_AlignLeft, w - 24, stage);
    /* spinner dots */
    for (int i = 0; i < 8; i++) {
        int on = (frame / 6) % 8 == i;
        rect(x + 12 + i * 12, y + 60, 8, 8, rgb(on ? T->accent : T->elev));
    }
    if (!working_fn && (api.stage == STAGE_SOLVING || api.stage == STAGE_UPLOADING)) {
        rect(x + 12, y + 80, w - 24, 8, rgb(T->elev));
        rect(x + 12, y + 80, (w - 24) * api.progress / 100, 8, rgb(T->accent));
    }
}

static void draw_alert(void)
{
    float w = BOT_W - 24, th = text_height(13, w - 24, alert_text), h = th + 30;
    float x = 12, y = SCR_H - h - 8;
    rect(x, y, w, h, rgb(T->panel));
    rect(x, y, 4, h, rgb(alert_error ? T->danger : T->ok));
    frame_rect(x, y, w, h, 1, rgb(T->line));
    text(x + 12, y + 7, 13, rgb(T->text), C2D_AlignLeft, w - 24, alert_text);
    text(x + w - 8, y + h - 16, 10, rgb(T->muted), C2D_AlignRight, 0, "Tap or press a button to close");
}

static const char *screen_title(void)
{
    switch (screen) {
    case SCR_CONNECT: return "PictoCard";
    case SCR_COMPOSE: return draft.method ? "Send a letter to a Wii" : "Send a PictoCard";
    case SCR_REVIEW: return "Review";
    case SCR_VERIFY: return "Check your email";
    default: return "Sent!";
    }
}

/* ── Main loop ───────────────────────────────────────────────────────────── */

static void update(const input_t *in)
{
    if (api.finished) handle_finished();

    if (working_fn) return; /* runs after this frame is drawn */

    if (alert_frames > 0) {
        alert_frames--;
        if (in->down & ~(KEY_TOUCH | KEY_CPAD_UP | KEY_CPAD_DOWN | KEY_CPAD_LEFT | KEY_CPAD_RIGHT) || in->pressed) {
            alert_frames = 0;
            return;
        }
    }
    if (busy_modal) return;

    if (camera_active()) {
        camera_update(in);
        return;
    }
    if (doodle_active()) {
        doodle_update(in);
        return;
    }
    if (picker.active) {
        picker_update(in);
        return;
    }

    if ((in->held & KEY_START) && (in->held & KEY_SELECT)) {
        quit = 1;
        return;
    }
    if (in->down & KEY_SELECT) activate(F_THEME);

    build_rows();
    int chosen = list_input(&form_list, rows, row_count, in);
    if (form_list.focus >= 0) focus_id = rows[form_list.focus].id;

    switch (screen) {
    case SCR_CONNECT:
        if (in->down & KEY_START) quit = 1;
        break;
    case SCR_COMPOSE:
        if (in->down & KEY_START) {
            go_review();
            return;
        }
        if (focus_id && (in->repeat & KEY_DLEFT)) cycle(focus_id, -1);
        if (focus_id && (in->repeat & KEY_DRIGHT)) cycle(focus_id, 1);
        break;
    case SCR_REVIEW:
        if (in->down & KEY_START) {
            start_send();
            return;
        }
        if (in->down & KEY_B) {
            activate(F_EDIT);
            return;
        }
        if (draft.method == 1) {
            if ((in->down & KEY_L) && letter_page > 0) letter_page--;
            if ((in->down & KEY_R) && letter_page + 1 < preview_letters) letter_page++;
            if (in->down & KEY_Y && preview_card) show_card = !show_card;
        }
        break;
    case SCR_VERIFY:
        if (in->down & KEY_START) {
            activate(F_VERIFY_SEND);
            return;
        }
        if (in->down & KEY_B) {
            activate(F_VERIFY_BACK);
            return;
        }
        break;
    }
    if (chosen >= 0) activate(rows[chosen].id);
}

static void live_preview(void)
{
    /* redraw the card a moment after the last change, as the website does */
    if (screen != SCR_COMPOSE || !meta.loaded || api.busy || doodle_active() || camera_active()) return;
    if (preview_version == version || frame - changed_frame < 45) return;
    request_preview();
}

static void draw(void)
{
    gfx_begin(rgb(T->bg), rgb(T->bg));
    if (camera_active()) camera_draw_top();
    else if (doodle_active()) doodle_draw_top();
    else draw_top();

    gfx_bottom();
    if (camera_active()) {
        camera_draw_bottom();
    } else if (doodle_active()) {
        doodle_draw_bottom();
    } else if (picker.active) {
        picker_draw();
    } else {
        build_rows();
        list_draw(&form_list, rows, row_count);
        draw_title(screen_title(), screen == SCR_COMPOSE ? (settings.light ? "SELECT: dark" : "SELECT: light") : NULL);
    }
    if (busy_modal || working_fn) draw_busy();
    else if (alert_frames > 0) draw_alert();
    gfx_end();
}

#ifdef AUTOTEST
static int app_busy(void)
{
    int preview_due = screen == SCR_COMPOSE && !picker.active && !doodle_active() && !camera_active() && preview_version != version;
    return api.busy || working_fn || busy_modal || preview_due;
}
#endif

void app_run(void)
{
#ifdef AUTOTEST
    autotest_busy = app_busy;
#endif
    settings_load();
    theme_set(settings.light);
    api_init();
    draft.color = -1;
    draft.letterhead = -1;
    snprintf(draft.sender_name, sizeof draft.sender_name, "%s", settings.sender_name);
    snprintf(draft.sender_email, sizeof draft.sender_email, "%s", settings.sender_email);
    snprintf(draft.recipient_wii, sizeof draft.recipient_wii, "%s", settings.wii_number);
    api_connect();
    image_t *logo = image_decode(logo_png, logo_png_size, 0);
    if (logo) tex_set(&tex_logo, logo);
    image_free(logo);

    hidSetRepeatParameters(18, 5);
    input_t in;
    while (!quit && aptMainLoop()) {
        input_read(&in);
        update(&in);
        live_preview();
        if (picker.active) update_pick_preview();
        draw();
#ifdef AUTOTEST
        autotest_after_draw();
        if (autotest_quit) quit = 1;
#endif
        if (working_fn) {
            /* the busy box is on screen now; do the slow work */
            void (*fn)(void) = working_fn;
            fn();
            /* a step can queue the next one (decode, then encode), which runs after the next frame */
            if (working_fn == fn) working_fn = NULL;
        }
        frame++;
    }
    api_exit();
    free_preview();
    tex_free(&tex_preview);
    tex_free(&tex_logo);
    tex_free(&tex_pick);
    clear_picture();
}
