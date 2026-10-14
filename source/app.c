/* PictoCard for Wii: the screens, laid out like pictocard.net. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <gccore.h>
#include "app.h"
#include "api.h"
#include "font.h"
#include "gfx.h"
#include "image.h"
#include "input.h"
#include "settings.h"
#include "ui.h"
#include "util.h"
#include "version.h"
#include "wiidata.h"
#include "cJSON.h"

draft_t draft;

const swatch_t PALETTE[] = {
    { "Black", 0x111111 }, { "White", 0xf5f5f7 }, { "Red", 0xd62d20 },  { "Orange", 0xf08a24 },
    { "Gold", 0xc9a227 },  { "Green", 0x2e9e4f }, { "Teal", 0x14a3a3 }, { "Blue", 0x1f6fd1 },
    { "Sky", 0x4fb4ea },   { "Purple", 0x7d4cc4 }, { "Pink", 0xe0569b }, { "Brown", 0x7a4b2a },
};
const int PALETTE_COUNT = sizeof PALETTE / sizeof *PALETTE;

enum { SCR_CONNECT, SCR_COMPOSE, SCR_REVIEW, SCR_VERIFY, SCR_DONE };

enum {
    F_METHOD = 1, F_REC_EMAIL, F_SENDER_NAME, F_SENDER_EMAIL, F_REC_WII, F_FRIEND, F_PICTURE, F_ATTACH,
    F_LETTERHEAD, F_MII, F_EXPRESSION, F_BACKGROUND, F_MESSAGE, F_CARD_TEXT, F_FONT, F_COLOR, F_REVIEW,
    F_SEND, F_EDIT, F_CODE, F_VERIFY_SEND, F_VERIFY_BACK, F_ANOTHER, F_EXIT, F_RETRY, F_THEME
};
enum { W_TABS, W_TEXT, W_AREA, W_SELECT, W_FILE, W_RADIO, W_BUTTON };

typedef struct {
    int id, type, panel;
    const char *label, *hint;
    int x, y, w, h; /* in panel-content coordinates (left column) */
    int half;       /* 1 left half, 2 right half */
} widget_t;

#define MAX_WIDGETS 24
static widget_t widgets[MAX_WIDGETS];
static int widget_count;
static int panel_top[3], panel_h[3];
static int content_h;

static int screen = SCR_CONNECT;
static int focus = F_REC_EMAIL;
static int scroll, scroll_target;
static int frame;
static int quit;

static char alert_text[256];
static int alert_error, alert_frames;

static int version, preview_version = -1, requested_version = -1, changed_frame;
static image_t *preview_card, *preview_letter[8];
static int preview_letters, preview_needs_image;
static image_t *preview_fit_card, *preview_fit_letter; /* scaled for the live preview */
static int fit_for_w, fit_for_h, fit_letter_page = -1;
static char preview_error[256];
static int review_page, review_show_card;
static int pending_review; /* waiting for a fresh preview before reviewing */

static char verify_ticket[64], verify_email[128], verify_code[16];
static int busy_modal; /* show a progress dialog for the running job */
static int done_wii;
static pick_item_t pick_items[512];

/* confetti, as on the site after sending */
typedef struct { float x, y, vx, vy, rot, vr; u32 color; } confetti_t;
static confetti_t confetti[140];
static int confetti_frames;

static void show_alert(const char *msg, int error)
{
    snprintf(alert_text, sizeof alert_text, "%s", msg);
    alert_error = error;
    alert_frames = 60 * 8;
}

void draft_changed(void)
{
    version++;
    changed_frame = frame;
}

u32 draft_text_color(void)
{
    if (draft.color >= 0 && draft.color < PALETTE_COUNT) return PALETTE[draft.color].rgb;
    return draft.dark ? 0xf5f5f7 : 0x111111;
}

static int makes_card(void) { return draft.method == 0 || draft.attach == 2; }
static int has_picture_field(void) { return draft.method == 0 || draft.attach != 0; }

/* ── Draft → JSON ────────────────────────────────────────────────────────── */

static void add_b64(cJSON *obj, const char *key, const unsigned char *data, size_t len)
{
    char *b = base64_encode(data, len);
    if (b) {
        cJSON_AddStringToObject(obj, key, b);
        free(b);
    }
}

char *draft_json(int for_send)
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
    cJSON_AddStringToObject(o, "wiiAttach", draft.attach == 1 ? "image" : draft.attach == 2 ? "card" : "none");
    cJSON_AddStringToObject(o, "letterhead", draft.letterhead >= 0 ? meta.letterheads[draft.letterhead].id : "");
    if (has_picture_field()) {
        if (for_send && draft.picture_jpeg) add_b64(o, "image", draft.picture_jpeg, draft.picture_len);
        else if (draft.preview_jpeg) add_b64(o, "image", draft.preview_jpeg, draft.preview_len);
    }
    if (draft.mii >= 0) {
        if (makes_card()) add_b64(o, "mii", miis[draft.mii].data, MII_SIZE);
        if (wii) add_b64(o, "wiiMii", miis[draft.mii].data, MII_SIZE);
    }
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

/* ── Picture loading ─────────────────────────────────────────────────────── */

static void load_picture(const char *path)
{
    size_t len;
    unsigned char *data = read_file(path, &len, 48 * 1024 * 1024);
    if (!data) {
        show_alert("Could not read that file. It may be too large (48 MB at most).", 1);
        return;
    }
    image_t *img = image_decode(data, len, 1600);
    free(data);
    if (!img) {
        show_alert("That file is not a photo PictoCard can read. Choose a JPEG, PNG, WebP, GIF or BMP.", 1);
        return;
    }
    size_t full_len = 0, prev_len = 0;
    unsigned char *full = image_encode_jpeg(img, 88, &full_len);
    image_t *small = image_fit(img, 720, 720);
    unsigned char *prev = small ? image_encode_jpeg(small, 80, &prev_len) : NULL;
    image_t *thumb = image_fit(img, 64, 48);
    image_free(small);
    image_free(img);
    if (!full || !prev) {
        free(full);
        free(prev);
        image_free(thumb);
        show_alert("The Wii ran out of memory loading that photo.", 1);
        return;
    }
    free(draft.picture_jpeg);
    free(draft.preview_jpeg);
    image_free(draft.thumb);
    draft.picture_jpeg = full;
    draft.picture_len = full_len;
    draft.preview_jpeg = prev;
    draft.preview_len = prev_len;
    draft.thumb = thumb;
    const char *name = strrchr(path, '/');
    snprintf(draft.picture_name, sizeof draft.picture_name, "%s", name ? name + 1 : path);
    draft_changed();
}

/* ── Widget layout ───────────────────────────────────────────────────────── */

static int LX, LY, LW, LH, RX, RW; /* left column viewport, right column */

static widget_t *add(int id, int type, int panel, const char *label, const char *hint, int h, int half)
{
    widget_t *w = &widgets[widget_count++];
    memset(w, 0, sizeof *w);
    w->id = id;
    w->type = type;
    w->panel = panel;
    w->label = label;
    w->hint = hint;
    w->h = h;
    w->half = half;
    return w;
}

static const char *friend_hint(void)
{
    static char buf[200];
    if (wii_number[0]) {
        char pretty[24];
        format_wii_number(wii_number, pretty);
        snprintf(buf, sizeof buf, "This Wii is %s. To receive letters, a Wii adds PictoCard as a friend once.", pretty);
    } else {
        snprintf(buf, sizeof buf, "To receive letters, a Wii adds PictoCard as a friend once.");
    }
    return buf;
}

static void build_widgets(void)
{
    widget_count = 0;
    int wii = draft.method == 1;
    add(F_METHOD, W_TABS, 0, NULL, NULL, 36, 0);
    if (!wii) {
        add(F_REC_EMAIL, W_TEXT, 0, "Recipient Email Address", NULL, 52, 0);
        add(F_SENDER_NAME, W_TEXT, 0, "Your Name", NULL, 52, 1);
        add(F_SENDER_EMAIL, W_TEXT, 0, "Your Email", NULL, 52, 2);
    } else {
        add(F_REC_WII, W_TEXT, 0, "Recipient Wii Number", NULL, 52, 0);
        add(F_SENDER_NAME, W_TEXT, 0, "Your Name", "Shown on the letter. Optional.", 52, 0);
        add(F_FRIEND, W_BUTTON, 0, NULL, friend_hint(), 34, 0);
    }
    if (wii) add(F_ATTACH, W_SELECT, 1, "Attachment", NULL, 52, 0);
    if (has_picture_field())
        add(F_PICTURE, W_FILE, 1, wii && draft.attach == 1 ? "Picture to attach" : "Card picture",
            "Any photo on your SD card or USB drive.", 62, 0);
    if (wii) add(F_LETTERHEAD, W_SELECT, 1, "Letterhead", NULL, 52, 0);
    add(F_MII, W_SELECT, 1, wii ? "Your Mii" : "Mii on your card", NULL, 52, 0);
    if (draft.mii >= 0 && makes_card()) add(F_EXPRESSION, W_SELECT, 1, "Mii expression", NULL, 52, 0);
    if (makes_card()) add(F_BACKGROUND, W_RADIO, 1, "Card background", NULL, 46, 0);
    add(F_MESSAGE, W_AREA, 2, "Message", NULL, 18 + 96, 0);
    if (wii && draft.attach == 2)
        add(F_CARD_TEXT, W_AREA, 2, "Card text", "Leave empty to use the letter's message.", 18 + 74, 0);
    if (makes_card()) {
        add(F_FONT, W_SELECT, 2, "Font", NULL, 52, 1);
        add(F_COLOR, W_SELECT, 2, "Text Color", NULL, 52, 2);
    }

    /* stack the panels, place widgets inside them */
    int y = 0, pad = 14, gap = 12;
    for (int p = 0; p < 3; p++) {
        panel_top[p] = y;
        int cy = y + 42 + pad;
        for (int i = 0; i < widget_count; i++) {
            widget_t *w = &widgets[i];
            if (w->panel != p) continue;
            int inner = LW - 2 * pad;
            w->x = pad;
            w->w = inner;
            if (w->half == 1) w->w = (inner - 12) / 2;
            if (w->half == 2) {
                w->w = (inner - 12) / 2;
                w->x = pad + w->w + 12;
                w->y = widgets[i - 1].y;
                continue;
            }
            w->y = cy;
            int hint_h = 0;
            if (w->hint) hint_h = 4 + ui_wrapped_height(FONT_REGULAR, 12, w->w, w->hint, 3);
            int hh = w->h + hint_h;
            if (i + 1 < widget_count && widgets[i + 1].half == 2 && widgets[i + 1].panel == p && widgets[i + 1].hint)
                hh = w->h + 4 + ui_wrapped_height(FONT_REGULAR, 12, (inner - 12) / 2, widgets[i + 1].hint, 3);
            cy += hh + gap;
        }
        if (p == 0 && draft.method == 0) cy += 0;
        panel_h[p] = cy - gap + pad - y;
        y += panel_h[p] + 16;
    }
    content_h = y;
}

static widget_t *find(int id)
{
    for (int i = 0; i < widget_count; i++)
        if (widgets[i].id == id) return &widgets[i];
    return NULL;
}

/* ── Values shown in selects ─────────────────────────────────────────────── */

static const char *attach_name(int a) { return a == 1 ? "A picture" : a == 2 ? "A PictoCard" : "No attachment"; }

static const char *select_value(int id, u32 *swatch, int *has_swatch)
{
    static char buf[128];
    *has_swatch = 0;
    switch (id) {
    case F_ATTACH: return attach_name(draft.attach);
    case F_LETTERHEAD:
        if (draft.letterhead < 0) return "Default (plain letter)";
        snprintf(buf, sizeof buf, "%s", meta.letterheads[draft.letterhead].name);
        return buf;
    case F_MII:
        if (!mii_count) return "No Miis found on this Wii";
        if (draft.mii < 0) return "No Mii";
        *swatch = mii_color(miis[draft.mii].color);
        *has_swatch = 1;
        return miis[draft.mii].name;
    case F_EXPRESSION: return meta.expression_count ? meta.expressions[draft.expression].name : "Normal";
    case F_FONT: return meta.font_count ? meta.fonts[draft.font].name : "Rodin NTLG";
    case F_COLOR:
        *swatch = draft_text_color();
        *has_swatch = 1;
        if (draft.color < 0) snprintf(buf, sizeof buf, "#%06x", (unsigned)*swatch);
        else snprintf(buf, sizeof buf, "%s  #%06x", PALETTE[draft.color].name, (unsigned)*swatch);
        return buf;
    }
    return "";
}

static void cycle(int id, int dir)
{
    switch (id) {
    case F_METHOD:
        draft.method = !draft.method;
        if (focus != F_METHOD) focus = F_METHOD;
        break;
    case F_ATTACH: draft.attach = (draft.attach + 3 + dir) % 3; break;
    case F_LETTERHEAD:
        if (!meta.letterhead_count) return;
        draft.letterhead += dir;
        if (draft.letterhead < -1) draft.letterhead = meta.letterhead_count - 1;
        if (draft.letterhead >= meta.letterhead_count) draft.letterhead = -1;
        break;
    case F_MII:
        if (!mii_count) return;
        draft.mii += dir;
        if (draft.mii < -1) draft.mii = mii_count - 1;
        if (draft.mii >= mii_count) draft.mii = -1;
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
        if (draft.color < 0) draft.color = draft.dark ? 1 : 0;
        draft.color = (draft.color + PALETTE_COUNT + dir) % PALETTE_COUNT;
        break;
    case F_BACKGROUND:
        draft.dark = !draft.dark;
        break;
    default: return;
    }
    draft_changed();
}

/* ── Pickers ─────────────────────────────────────────────────────────────── */

static int pick_field;
static void picked(int value)
{
    switch (pick_field) {
    case F_ATTACH: draft.attach = value; break;
    case F_LETTERHEAD: draft.letterhead = value; break;
    case F_MII: draft.mii = value; break;
    case F_EXPRESSION: draft.expression = value; break;
    case F_FONT: draft.font = value; break;
    case F_COLOR: draft.color = value; break;
    }
    draft_changed();
}

static void open_picker(int id)
{
    int n = 0;
    const char *title = "";
    pick_field = id;
    int sel = 0;
    switch (id) {
    case F_ATTACH:
        title = "Attachment";
        for (int i = 0; i < 3; i++) {
            snprintf(pick_items[n].label, 96, "%s", attach_name(i));
            pick_items[n].value = i;
            pick_items[n].header = pick_items[n].has_color = 0;
            n++;
        }
        sel = draft.attach;
        break;
    case F_LETTERHEAD: {
        title = "Letterhead";
        snprintf(pick_items[n].label, 96, "Default (plain letter)");
        pick_items[n].value = -1;
        pick_items[n].header = pick_items[n].has_color = 0;
        n++;
        const char *group = "";
        for (int i = 0; i < meta.letterhead_count && n < 500; i++) {
            if (strcmp(group, meta.letterheads[i].group)) {
                group = meta.letterheads[i].group;
                snprintf(pick_items[n].label, 96, "%s", group);
                pick_items[n].header = 1;
                pick_items[n].has_color = 0;
                n++;
            }
            snprintf(pick_items[n].label, 96, "%s", meta.letterheads[i].name);
            pick_items[n].value = i;
            pick_items[n].header = pick_items[n].has_color = 0;
            n++;
        }
        sel = draft.letterhead;
        break;
    }
    case F_MII:
        title = "Choose a Mii from this Wii";
        snprintf(pick_items[n].label, 96, "No Mii");
        pick_items[n].value = -1;
        pick_items[n].header = pick_items[n].has_color = 0;
        n++;
        for (int i = 0; i < mii_count; i++) {
            if (miis[i].creator[0]) snprintf(pick_items[n].label, 96, "%s  (by %s)", miis[i].name, miis[i].creator);
            else snprintf(pick_items[n].label, 96, "%s", miis[i].name);
            pick_items[n].value = i;
            pick_items[n].color = mii_color(miis[i].color);
            pick_items[n].has_color = 1;
            pick_items[n].header = 0;
            n++;
        }
        sel = draft.mii;
        break;
    case F_EXPRESSION:
    case F_FONT: {
        title = id == F_FONT ? "Font" : "Mii expression";
        named_t *list = id == F_FONT ? meta.fonts : meta.expressions;
        int count = id == F_FONT ? meta.font_count : meta.expression_count;
        for (int i = 0; i < count; i++) {
            snprintf(pick_items[n].label, 96, "%s", list[i].name);
            pick_items[n].value = i;
            pick_items[n].header = pick_items[n].has_color = 0;
            n++;
        }
        sel = id == F_FONT ? draft.font : draft.expression;
        break;
    }
    case F_COLOR:
        title = "Text Color";
        for (int i = 0; i < PALETTE_COUNT; i++) {
            snprintf(pick_items[n].label, 96, "%s  #%06x", PALETTE[i].name, (unsigned)PALETTE[i].rgb);
            pick_items[n].value = i;
            pick_items[n].color = PALETTE[i].rgb;
            pick_items[n].has_color = 1;
            pick_items[n].header = 0;
            n++;
        }
        sel = draft.color >= 0 ? draft.color : draft.dark;
        break;
    default: return;
    }
    picker_open(title, pick_items, n, sel, picked);
}

/* ── Actions ─────────────────────────────────────────────────────────────── */

static int count_chars(const char *s)
{
    int n = 0;
    while (*s) {
        utf8_next(&s);
        n++;
    }
    return n;
}

static void edit_text(int id)
{
    switch (id) {
    case F_REC_EMAIL: osk_open("Recipient Email Address", draft.recipient_email, sizeof draft.recipient_email, 254, 0, 0, 1); break;
    case F_SENDER_NAME: osk_open("Your Name", draft.sender_name, sizeof draft.sender_name, 60, 0, 0, 0); break;
    case F_SENDER_EMAIL: osk_open("Your Email", draft.sender_email, sizeof draft.sender_email, 254, 0, 0, 1); break;
    case F_REC_WII: osk_open("Recipient Wii Number", draft.recipient_wii, sizeof draft.recipient_wii, 16, 0, 1, 0); break;
    case F_MESSAGE: osk_open("Message", draft.message, sizeof draft.message, meta.max_text ? meta.max_text : 500, 1, 0, 0); break;
    case F_CARD_TEXT: osk_open("Card text", draft.card_text, sizeof draft.card_text, meta.max_text ? meta.max_text : 500, 1, 0, 0); break;
    case F_CODE: osk_open("Verification code", verify_code, sizeof verify_code, 6, 0, 1, 0); break;
    }
}

static void request_preview(void)
{
    if (api.busy || !meta.loaded) return;
    char *body = draft_json(0);
    if (body && api_preview(body)) requested_version = version;
}

static const char *validate(void)
{
    static char msg[160];
    if (draft.method == 0) {
        if (!strchr(draft.recipient_email, '@')) return "Please enter a valid recipient email address.";
        if (!draft.sender_name[0]) return "Your name is required.";
        if (!strchr(draft.sender_email, '@')) return "A valid sender email is required for verification.";
        if (!draft.picture_jpeg) return "Please choose a picture for your card.";
    } else {
        if (!meta.wii_enabled) return "Wii delivery isn't set up yet. Please check back soon.";
        if (strlen(draft.recipient_wii) != 16) return "Enter the 16-digit Wii Number.";
        if (!draft.message[0]) return "A message is required for Wii mail.";
        if (draft.attach && !draft.picture_jpeg) return "Please choose a picture to attach.";
    }
    int max = meta.max_text ? meta.max_text : 500;
    if (count_chars(draft.message) > max || count_chars(draft.card_text) > max) {
        snprintf(msg, sizeof msg, "Messages can contain up to %d characters. Please shorten yours.", max);
        return msg;
    }
    return NULL;
}

static void go_review(void)
{
    const char *err = validate();
    if (err) {
        show_alert(err, 1);
        return;
    }
    snprintf(settings.sender_name, sizeof settings.sender_name, "%s", draft.sender_name);
    snprintf(settings.sender_email, sizeof settings.sender_email, "%s", draft.sender_email);
    settings_save();
    if (preview_version != version) {
        pending_review = 1;
        busy_modal = 1;
        if (!api.busy) request_preview();
        return;
    }
    review_page = 0;
    review_show_card = 0;
    screen = SCR_REVIEW;
    focus = F_SEND;
}

static void start_send(void)
{
    char *body = draft_json(1);
    if (!body) {
        show_alert("The Wii ran out of memory.", 1);
        return;
    }
    if (api_send(body)) busy_modal = 1;
}

static void activate(int id)
{
    widget_t *w = find(id);
    switch (id) {
    case F_METHOD: break;
    case F_REC_EMAIL: case F_SENDER_NAME: case F_SENDER_EMAIL: case F_REC_WII: case F_MESSAGE: case F_CARD_TEXT: case F_CODE:
        edit_text(id);
        break;
    case F_PICTURE: files_open(load_picture); break;
    case F_BACKGROUND: cycle(id, 1); break;
    case F_FRIEND:
        if (!wii_number[0]) show_alert("This Wii's number could not be read. Add PictoCard from the Message Board's address book instead.", 1);
        else if (api_friend(wii_number)) busy_modal = 1;
        break;
    case F_REVIEW: go_review(); break;
    case F_SEND: start_send(); break;
    case F_EDIT: screen = SCR_COMPOSE; focus = F_REVIEW; break;
    case F_VERIFY_SEND:
        if (strlen(verify_code) != 6) show_alert("Enter the 6-digit code from the email.", 1);
        else if (api_verify(verify_ticket, verify_code)) busy_modal = 1;
        break;
    case F_VERIFY_BACK: screen = SCR_REVIEW; focus = F_SEND; break;
    case F_ANOTHER:
        draft.message[0] = draft.card_text[0] = 0;
        screen = SCR_COMPOSE;
        focus = F_MESSAGE;
        draft_changed();
        break;
    case F_EXIT: quit = 1; break;
    case F_RETRY: api_connect(); break;
    case F_THEME:
        settings.light = !settings.light;
        ui_set_theme(settings.light);
        settings_save();
        break;
    default:
        if (w && w->type == W_SELECT) open_picker(id);
    }
}

/* ── Compose screen ──────────────────────────────────────────────────────── */

static void layout_columns(void)
{
    LX = 24;
    LY = 132;
    LH = SH - 46 - LY;
    LW = (SW - 48 - 16) * 57 / 100;
    RX = LX + LW + 16;
    RW = SW - 24 - RX;
}

static void hero(const char *title, const char *sub)
{
    text_draw(FONT_BOLD, 24, 24, 72, T.text, title);
    text_draw_fit(FONT_REGULAR, 13, 24, 104, SW - 48, T.muted, sub);
}

static void draw_widget(widget_t *w, int ox, int oy)
{
    int x = ox + w->x, y = oy + w->y, f = focus == w->id;
    int box_y = y + (w->label ? 20 : 0);
    if (w->label) text_draw(FONT_BOLD, 13, x, y, T.muted, w->label);
    int box_h = w->h - (w->label ? 20 : 0);
    switch (w->type) {
    case W_TABS: {
        int tw = (w->w - 8) / 2;
        ui_tab(x, y, tw, w->h, "Email", draft.method == 0, f && draft.method == 0);
        ui_tab(x + tw + 8, y, tw, w->h, "Wii", draft.method == 1, f && draft.method == 1);
        break;
    }
    case W_TEXT: {
        const char *val = "", *ph = "";
        char pretty[32];
        switch (w->id) {
        case F_REC_EMAIL: val = draft.recipient_email; ph = "friend@example.com"; break;
        case F_SENDER_NAME: val = draft.sender_name; ph = "Your name"; break;
        case F_SENDER_EMAIL: val = draft.sender_email; ph = "you@example.com"; break;
        case F_REC_WII: format_wii_number(draft.recipient_wii, pretty); val = pretty; ph = "1234 5678 9012 3456"; break;
        case F_CODE: val = verify_code; ph = "123456"; break;
        }
        ui_input(x, box_y, w->w, box_h, val, ph, f, 0, 0);
        break;
    }
    case W_AREA: {
        const char *val = w->id == F_MESSAGE ? draft.message : draft.card_text;
        const char *ph = w->id == F_MESSAGE ? "Write something nice..." : "Text on the PictoCard";
        ui_input(x, box_y, w->w, box_h, val, ph, f, 1, 0);
        char count[24];
        snprintf(count, sizeof count, "%d / %d", count_chars(val), meta.max_text ? meta.max_text : 500);
        text_draw(FONT_REGULAR, 12, x + w->w - text_width(FONT_REGULAR, 12, count), y + 1, T.muted, count);
        break;
    }
    case W_SELECT: {
        u32 sw = 0;
        int has = 0;
        const char *v = select_value(w->id, &sw, &has);
        ui_select(x, box_y, w->w, box_h, v, sw, has, f, w->id == F_MII && !mii_count);
        break;
    }
    case W_FILE: {
        if (f) ui_focus_ring(x, box_y, w->w, box_h);
        gfx_frame(x, box_y, w->w, box_h, RADIUS, T.elev, f ? T.accent : T.input_border, 1);
        /* dashed look like the site's drop zone */
        for (int i = x + 6; i < x + w->w - 6; i += 8) {
            gfx_fill(i, box_y + 2, 4, 1, T.input_border);
            gfx_fill(i, box_y + box_h - 3, 4, 1, T.input_border);
        }
        int tx = x + 12;
        if (draft.thumb) {
            gfx_fill(x + 9, box_y + (box_h - draft.thumb->h) / 2 - 1, draft.thumb->w + 2, draft.thumb->h + 2, 0xFFFFFF);
            gfx_blit(draft.thumb, x + 10, box_y + (box_h - draft.thumb->h) / 2);
            tx += 64 + 6;
        }
        int bw = 86;
        text_draw_fit(FONT_BOLD, 14, tx, box_y + 6, w->w - (tx - x) - bw - 16, T.text,
                      draft.picture_name[0] ? draft.picture_name : "Choose a picture");
        text_draw_fit(FONT_REGULAR, 12, tx, box_y + 22, w->w - (tx - x) - bw - 16, T.muted,
                      draft.picture_name[0] ? "Press A to choose another" : "From your SD card or USB drive");
        ui_button(x + w->w - bw - 8, box_y + (box_h - 28) / 2, bw, 28, "Browse", 0, 0, 0);
        break;
    }
    case W_RADIO:
        ui_radio(x, box_y + 2, "Light", !draft.dark, f && !draft.dark);
        ui_radio(x + 100, box_y + 2, "Dark", draft.dark, f && draft.dark);
        break;
    case W_BUTTON:
        if (w->id == F_FRIEND) ui_button(x, y, w->w, w->h, "Add PictoCard to this Wii", 0, f, 0);
        break;
    }
    if (w->hint) ui_wrapped(FONT_REGULAR, 12, x, y + w->h + 4, w->w, T.muted, w->hint, 3);
}

static const char *STEPS[3] = { "01", "02", "03" };
static const char *TITLES[3] = { "Who is it for?", "Your picture", "Your message" };

static void draw_preview_panel(int x, int y, int w, int h)
{
    int top = ui_panel(x, y, w, h, NULL, "Live preview");
    int sx = x + 1, sy = top, sw = w - 2, sh = h - (top - y) - 64;
    gfx_fill(sx, sy, sw, sh, T.bg2);
    gfx_grid(sx, sy, sw, sh, 14, T.grid);
    image_t *src = draft.method == 1 && preview_letters ? preview_letter[0] : preview_card;
    if (src) {
        if (!preview_fit_card || fit_for_w != sw - 24 || fit_for_h != sh - 24 || fit_letter_page != (src == preview_card ? -2 : 0)) {
            image_free(preview_fit_card);
            preview_fit_card = image_fit(src, sw - 24, sh - 24);
            fit_for_w = sw - 24;
            fit_for_h = sh - 24;
            fit_letter_page = src == preview_card ? -2 : 0;
        }
        image_t *im = preview_fit_card;
        if (im) {
            int ix = sx + (sw - im->w) / 2, iy = sy + (sh - im->h) / 2;
            gfx_fill(ix - 3, iy - 3, im->w + 6, im->h + 6, 0xFFFFFF);
            gfx_blit(im, ix, iy);
        }
    } else {
        text_draw_center(FONT_REGULAR, 13, sx, sy + sh / 2 - 10, sw, T.muted,
                         api.busy && api.job == JOB_PREVIEW ? "Drawing your card..." : "Your card will appear here");
    }
    if (api.busy && api.job == JOB_PREVIEW) {
        gfx_round(sx + sw - 40, sy + 8, 32, 32, 16, T.bg2);
        ui_spinner(sx + sw - 24, sy + 24, 10, T.accent, frame);
    }
    gfx_fill(sx, sy + sh, sw, 1, T.input_border);
    const char *cap = preview_error[0] ? preview_error
        : draft.method == 1 ? "This is the letter they'll see on the Wii Message Board. You'll review everything before sending."
        : "This is the card image they'll receive. You'll review everything before sending.";
    ui_wrapped(FONT_REGULAR, 12, x + 14, sy + sh + 8, w - 28, preview_error[0] ? T.danger : T.muted, cap, 3);
}

static void draw_compose(void)
{
    hero("Send a PictoCard!", "Create a personalized greeting card and send it by email. Or send a special note to a Wii Message Board with WiiLink!");
    int oy = LY - scroll;
    gfx_clip(LX - 6, LY - 4, LW + 12, LH + 8);
    for (int p = 0; p < 3; p++) {
        ui_panel(LX, oy + panel_top[p], LW, panel_h[p], STEPS[p], TITLES[p]);
        gfx_clip(LX + 1, oy + panel_top[p] + 42, LW - 2, panel_h[p] - 43);
        for (int i = 0; i < widget_count; i++)
            if (widgets[i].panel == p) draw_widget(&widgets[i], LX, oy);
        gfx_noclip();
    }
    gfx_noclip();
    /* fade the edges of the scrolling column */
    if (scroll > 0) for (int i = 0; i < 10; i++) gfx_fill_alpha(LX - 6, LY - 4 + i, LW + 12, 1, T.bg, 230 - i * 22);
    if (scroll < content_h - LH) for (int i = 0; i < 10; i++) gfx_fill_alpha(LX - 6, LY + LH + 3 - i, LW + 12, 1, T.bg, 230 - i * 22);

    int ph = LH - 76;
    draw_preview_panel(RX, LY, RW, ph);
    ui_button(RX, LY + ph + 14, RW, 40, "Review your card ->", 1, focus == F_REVIEW, 0);
    text_draw_center(FONT_REGULAR, 12, RX, LY + ph + 58, RW, T.muted, "Nothing is sent until you confirm.");

    static const char *const hints[] = { "A", "Select", "<>", "Change", "+", "Review", "1 2", "Steps", "HOME", "Exit" };
    ui_footer_hints(hints, 5);
}

static void ensure_visible(void)
{
    widget_t *w = find(focus);
    if (!w) return;
    int top = w->y - 50, bottom = w->y + w->h + 40;
    if (w == &widgets[0] || w->y < panel_top[w->panel] + 80) top = panel_top[w->panel];
    if (top < scroll_target) scroll_target = top;
    if (bottom > scroll_target + LH) scroll_target = bottom - LH;
    if (scroll_target > content_h - LH) scroll_target = content_h - LH;
    if (scroll_target < 0) scroll_target = 0;
}

/* Focus order: every field in turn, then the review button */
static void move_focus(int dir)
{
    if (focus == F_REVIEW) {
        if (dir < 0) focus = widgets[widget_count - 1].id;
        return;
    }
    for (int i = 0; i < widget_count; i++)
        if (widgets[i].id == focus) {
            int n = i + dir;
            if (n >= widget_count) focus = F_REVIEW;
            else if (n >= 0) focus = widgets[n].id;
            return;
        }
    focus = widgets[0].id;
}

static void jump_panel(int dir)
{
    widget_t *cur = find(focus);
    int p = cur ? cur->panel : 3;
    p += dir;
    if (p < 0) p = 0;
    if (p > 2) {
        focus = F_REVIEW;
        return;
    }
    for (int i = 0; i < widget_count; i++)
        if (widgets[i].panel == p) {
            focus = widgets[i].id;
            return;
        }
}

static int widget_at(int px, int py, widget_t **out)
{
    if (px >= RX && px < RX + RW && py >= LY + LH - 62 && py < LY + LH - 22) {
        *out = NULL;
        return F_REVIEW;
    }
    if (px < LX || px >= LX + LW || py < LY || py >= LY + LH) return 0;
    for (int i = 0; i < widget_count; i++) {
        widget_t *w = &widgets[i];
        int x = LX + w->x, y = LY - scroll + w->y;
        if (px >= x && px < x + w->w && py >= y && py < y + w->h) {
            *out = w;
            return w->id;
        }
    }
    return 0;
}

static void compose_update(input_t *in)
{
    key_event ev;
    while (input_key(&ev)) {
        widget_t *w = find(focus);
        if (ev.ch && w && (w->type == W_TEXT || w->type == W_AREA)) {
            /* typing on a text field opens the keyboard with that letter */
            edit_text(focus);
            if (osk_active()) {
                char s[2] = { ev.ch, 0 };
                osk_insert_text(s);
            }
            return; /* the keyboard takes the rest of the keys */
        }
        switch (ev.code) {
        case KEY_UP: in->down |= BTN_UP; break;
        case KEY_DOWN: case KEY_TAB: in->down |= BTN_DOWN; break;
        case KEY_LEFT: in->down |= BTN_LEFT; break;
        case KEY_RIGHT: in->down |= BTN_RIGHT; break;
        case KEY_ENTER: case KEY_KP_ENTER: in->down |= ev.ctrl ? BTN_PLUS : BTN_A; break;
        case KEY_PAGEDOWN: in->down |= BTN_2; break;
        case KEY_PAGEUP: in->down |= BTN_1; break;
        }
    }

    widget_t *hw = NULL;
    int hover = in->pointer ? widget_at(in->px, in->py, &hw) : 0;
    if (in->moved && hover) focus = hover;

    if (in->down & BTN_UP) move_focus(-1);
    if (in->down & BTN_DOWN) move_focus(1);
    widget_t *w = find(focus);
    if (in->down & (BTN_LEFT | BTN_RIGHT)) {
        int dir = in->down & BTN_LEFT ? -1 : 1;
        if (w && (w->type == W_SELECT || w->type == W_TABS || w->type == W_RADIO)) cycle(focus, dir);
        else if (w && w->half == 1 && dir > 0) focus = (w + 1)->id;
        else if (w && w->half == 2 && dir < 0) focus = (w - 1)->id;
    }
    if (in->down & BTN_1) jump_panel(-1);
    if (in->down & BTN_2) jump_panel(1);
    if (in->down & BTN_PLUS) go_review();
    if (in->down & BTN_A) {
        if (in->pointer && hover && hw) {
            /* clicks on a specific tab or radio option pick it directly */
            int lx = in->px - (LX + hw->x);
            if (hw->type == W_TABS) {
                int m = lx > hw->w / 2;
                if (m != draft.method) cycle(F_METHOD, 1);
            } else if (hw->type == W_RADIO) {
                int dark = lx > 90;
                if (dark != draft.dark) cycle(F_BACKGROUND, 1);
            } else activate(hover);
        } else if (in->pointer && in->py < 52 && in->px > SW - 120) {
            activate(F_THEME);
        } else if (w && w->type == W_TABS) {
            cycle(F_METHOD, 1);
        } else {
            activate(focus);
        }
    }
    if (in->down & BTN_MINUS) activate(F_THEME);

    /* pointing near the column's top or bottom edge scrolls it */
    if (in->pointer && in->px >= LX && in->px < LX + LW) {
        if (in->py >= LY - 20 && in->py < LY + 18) scroll_target -= 8;
        if (in->py > LY + LH - 18 && in->py < LY + LH + 30) scroll_target += 8;
        if (scroll_target < 0) scroll_target = 0;
        if (scroll_target > content_h - LH) scroll_target = content_h - LH > 0 ? content_h - LH : 0;
    }
    if (in->down & (BTN_UP | BTN_DOWN | BTN_1 | BTN_2)) ensure_visible();
}

/* ── Review screen ───────────────────────────────────────────────────────── */

static image_t *review_fit;
static image_t *review_fit_src;

static void summary_row(int x, int *y, int w, const char *label, const char *value)
{
    text_draw(FONT_BOLD, 12, x, *y, T.muted, label);
    text_draw_fit(FONT_REGULAR, 14, x, *y + 17, w, T.text, value);
    *y += 46;
}

static void draw_review(void)
{
    hero("Review your card", "Take a last look. Nothing is sent until you confirm.");
    int stage_w = (SW - 48 - 16) * 60 / 100, stage_h = LH;
    int top = ui_panel(LX, LY, stage_w, stage_h, NULL, draft.method == 1 ? "Wii Message Board letter" : "Card preview");
    int sx = LX + 1, sy = top, sw = stage_w - 2, sh = stage_h - (top - LY) - 1;
    gfx_fill(sx, sy, sw, sh, T.bg2);
    gfx_grid(sx, sy, sw, sh, 14, T.grid);
    int wii = draft.method == 1;
    image_t *src = wii && !review_show_card && preview_letters ? preview_letter[review_page % preview_letters] : preview_card;
    int has_tabs = wii && preview_letters && preview_card;
    int area_y = sy + (has_tabs ? 40 : 0);
    int area_h = sh - (has_tabs ? 40 : 0) - (wii && preview_letters > 1 && !review_show_card ? 22 : 0);
    if (has_tabs) {
        int tw = (sw - 36) / 2;
        ui_tab(sx + 12, sy + 8, tw, 28, "Letter", !review_show_card, 0);
        ui_tab(sx + 24 + tw, sy + 8, tw, 28, "Attachment", review_show_card, 0);
    }
    if (src) {
        if (review_fit_src != src) {
            image_free(review_fit);
            review_fit = image_fit(src, sw - 24, area_h - 24);
            review_fit_src = src;
        }
        if (review_fit) {
            int ix = sx + (sw - review_fit->w) / 2, iy = area_y + (area_h - review_fit->h) / 2;
            gfx_fill(ix - 3, iy - 3, review_fit->w + 6, review_fit->h + 6, 0xFFFFFF);
            gfx_blit(review_fit, ix, iy);
        }
    }
    if (wii && preview_letters > 1 && !review_show_card) {
        char page[32];
        snprintf(page, sizeof page, "Page %d of %d", review_page % preview_letters + 1, preview_letters);
        text_draw_center(FONT_REGULAR, 12, sx, sy + sh - 22, sw, T.muted, page);
    }

    int px = LX + stage_w + 16, pw = SW - 24 - px;
    int ptop = ui_panel(px, LY, pw, LH, NULL, "Summary");
    int y = ptop + 12, x = px + 14, w = pw - 28;
    char buf[300];
    if (wii) {
        char pretty[24];
        format_wii_number(draft.recipient_wii, pretty);
        summary_row(x, &y, w, "To Wii Number", pretty);
        summary_row(x, &y, w, "From", draft.sender_name[0] ? draft.sender_name : "Someone");
        snprintf(buf, sizeof buf, "%s, %s", attach_name(draft.attach),
                 draft.letterhead >= 0 ? meta.letterheads[draft.letterhead].name : "plain letter");
        summary_row(x, &y, w, "Letter", buf);
    } else {
        summary_row(x, &y, w, "To", draft.recipient_email);
        snprintf(buf, sizeof buf, "%s <%s>", draft.sender_name, draft.sender_email);
        summary_row(x, &y, w, "From", buf);
        snprintf(buf, sizeof buf, "We'll email a 6-digit code to %s to make sure it's you.", draft.sender_email);
        ui_wrapped(FONT_REGULAR, 12, x, y, w, T.muted, buf, 2);
    }
    int by = LY + LH - 100;
    ui_button(x, by, w, 40, "Send it! ->", 1, focus == F_SEND, api.busy);
    ui_button(x, by + 50, w, 34, "Edit card", 0, focus == F_EDIT, 0);
    static const char *const hints[] = { "A", "Select", "B", "Edit", "<>", "Page", "1", "Letter / Attachment" };
    static const char *const ehints[] = { "A", "Select", "B", "Edit", "HOME", "Exit" };
    if (wii) ui_footer_hints(hints, 4);
    else ui_footer_hints(ehints, 3);
}

static void review_update(input_t *in)
{
    key_event ev;
    while (input_key(&ev)) {
        if (ev.code == KEY_UP) in->down |= BTN_UP;
        else if (ev.code == KEY_DOWN) in->down |= BTN_DOWN;
        else if (ev.code == KEY_LEFT) in->down |= BTN_LEFT;
        else if (ev.code == KEY_RIGHT) in->down |= BTN_RIGHT;
        else if (ev.code == KEY_ENTER || ev.code == KEY_KP_ENTER) in->down |= BTN_A;
        else if (ev.code == KEY_ESC || ev.code == KEY_BACKSPACE) in->down |= BTN_B;
    }
    int stage_w = (SW - 48 - 16) * 60 / 100;
    int px = LX + stage_w + 16, pw = SW - 24 - px, by = LY + LH - 100;
    if (in->moved) {
        if (in->px >= px + 14 && in->px < px + pw - 14 && in->py >= by && in->py < by + 40) focus = F_SEND;
        if (in->px >= px + 14 && in->px < px + pw - 14 && in->py >= by + 50 && in->py < by + 84) focus = F_EDIT;
    }
    if (in->down & (BTN_UP | BTN_DOWN)) focus = focus == F_SEND ? F_EDIT : F_SEND;
    if (in->down & BTN_LEFT) review_page = review_page > 0 ? review_page - 1 : (preview_letters ? preview_letters - 1 : 0);
    if (in->down & BTN_RIGHT) review_page++;
    if ((in->down & BTN_1) && preview_card && preview_letters) review_show_card = !review_show_card;
    if (in->down & BTN_A) {
        if (in->pointer && in->px < px && in->py > LY + 42 && in->py < LY + 82 && draft.method == 1 && preview_card && preview_letters)
            review_show_card = in->px > LX + stage_w / 2;
        else activate(focus);
    }
    if (in->down & BTN_B) activate(F_EDIT);
}

/* ── Verify screen ───────────────────────────────────────────────────────── */

static void draw_verify(void)
{
    hero("Check your email", "Enter the code we emailed you to send your card.");
    int w = SW > 700 ? 520 : 440, x = (SW - w) / 2;
    int top = ui_panel(x, LY, w, 250, NULL, "Verify it's you");
    char buf[256];
    snprintf(buf, sizeof buf, "We sent a 6-digit code to %s. It expires in 10 minutes.", verify_email);
    ui_wrapped(FONT_REGULAR, 14, x + 16, top + 12, w - 32, T.text, buf, 3);
    text_draw(FONT_BOLD, 13, x + 16, top + 64, T.muted, "Verification code");
    ui_input(x + 16, top + 84, w - 32, 38, verify_code, "123456", focus == F_CODE, 0, 0);
    int bw = (w - 44) / 2;
    ui_button(x + 16, top + 138, bw, 40, "Send my card ->", 1, focus == F_VERIFY_SEND, api.busy);
    ui_button(x + 28 + bw, top + 141, bw, 34, "Back", 0, focus == F_VERIFY_BACK, 0);
    static const char *const hints[] = { "A", "Select", "B", "Back", "HOME", "Exit" };
    ui_footer_hints(hints, 3);
}

static void verify_update(input_t *in)
{
    key_event ev;
    while (input_key(&ev)) {
        if (ev.ch >= '0' && ev.ch <= '9' && focus == F_CODE) {
            size_t n = strlen(verify_code);
            if (n < 6) {
                verify_code[n] = ev.ch;
                verify_code[n + 1] = 0;
            }
        } else if (ev.code == KEY_BACKSPACE && focus == F_CODE) {
            size_t n = strlen(verify_code);
            if (n) verify_code[n - 1] = 0;
        } else if (ev.code == KEY_UP) in->down |= BTN_UP;
        else if (ev.code == KEY_DOWN || ev.code == KEY_TAB) in->down |= BTN_DOWN;
        else if (ev.code == KEY_LEFT) in->down |= BTN_LEFT;
        else if (ev.code == KEY_RIGHT) in->down |= BTN_RIGHT;
        else if (ev.code == KEY_ENTER || ev.code == KEY_KP_ENTER) {
            if (focus == F_CODE) focus = F_VERIFY_SEND;
            in->down |= BTN_A;
        } else if (ev.code == KEY_ESC) in->down |= BTN_B;
    }
    int w = SW > 700 ? 520 : 440, x = (SW - w) / 2, top = LY + 42, bw = (w - 44) / 2;
    if (in->moved) {
        if (in->py >= top + 84 && in->py < top + 122 && in->px > x && in->px < x + w) focus = F_CODE;
        if (in->py >= top + 138 && in->py < top + 178) focus = in->px < x + 16 + bw + 6 ? F_VERIFY_SEND : F_VERIFY_BACK;
    }
    if (in->down & BTN_UP) focus = F_CODE;
    if (in->down & BTN_DOWN && focus == F_CODE) focus = F_VERIFY_SEND;
    if (in->down & (BTN_LEFT | BTN_RIGHT) && focus != F_CODE) focus = focus == F_VERIFY_SEND ? F_VERIFY_BACK : F_VERIFY_SEND;
    if (in->down & BTN_A) activate(focus);
    if (in->down & BTN_B) activate(F_VERIFY_BACK);
}

/* ── Done screen ─────────────────────────────────────────────────────────── */

static void start_confetti(void)
{
    static const u32 colors[] = { 0x84dbfc, 0x26ccff, 0xa25afd, 0xff5e7e, 0x88ff5a, 0xfcff42, 0xffa62d, 0xff36ff };
    for (int i = 0; i < 140; i++) {
        confetti_t *c = &confetti[i];
        int side = i % 2;
        c->x = side ? SW - 40 : 40;
        c->y = SH * 0.7f;
        float ang = (side ? 120 : 60) * (float)M_PI / 180 + ((rand() % 100) - 50) / 100.0f;
        float sp = 9 + (rand() % 100) / 12.0f;
        c->vx = cosf(ang) * sp;
        c->vy = -sinf(ang) * sp;
        c->rot = rand() % 360;
        c->vr = (rand() % 20) - 10;
        c->color = colors[rand() % 8];
    }
    confetti_frames = 200;
}

static void draw_confetti(void)
{
    if (confetti_frames <= 0) return;
    confetti_frames--;
    int alpha = confetti_frames > 60 ? 255 : confetti_frames * 4;
    for (int i = 0; i < 140; i++) {
        confetti_t *c = &confetti[i];
        c->x += c->vx;
        c->y += c->vy;
        c->vx *= 0.97f;
        c->vy = c->vy * 0.97f + 0.35f;
        c->rot += c->vr;
        int w = 4 + (int)(3 * fabsf(cosf(c->rot * (float)M_PI / 180)));
        gfx_fill_alpha((int)c->x, (int)c->y, w, 7, c->color, alpha);
    }
}

static void draw_done(void)
{
    hero("Your card is on its way!", "Thanks for using PictoCard.");
    int w = SW > 700 ? 560 : 480, x = (SW - w) / 2, y = LY;
    const char *msg = "Your card was sent successfully!";
    ui_alert(x, y, w, msg, 0);
    y += ui_alert_height(w, msg) + 16;
    const char *detail = done_wii
        ? "It will appear on the Wii Message Board after that Wii's next WiiConnect24 check."
        : "We've also emailed you a copy for your records.";
    ui_wrapped(FONT_REGULAR, 14, x, y, w, T.text, detail, 3);
    y += 60;
    int bw = (w - 12) / 2;
    ui_button(x, y, bw, 40, "Send another card", 1, focus == F_ANOTHER, 0);
    ui_button(x + bw + 12, y + 3, bw, 34, "Exit", 0, focus == F_EXIT, 0);
    draw_confetti();
    static const char *const hints[] = { "A", "Select", "HOME", "Exit" };
    ui_footer_hints(hints, 2);
}

static void done_update(input_t *in)
{
    key_event ev;
    while (input_key(&ev)) {
        if (ev.code == KEY_LEFT || ev.code == KEY_RIGHT || ev.code == KEY_TAB) in->down |= BTN_RIGHT;
        else if (ev.code == KEY_ENTER || ev.code == KEY_KP_ENTER) in->down |= BTN_A;
    }
    int w = SW > 700 ? 560 : 480, x = (SW - w) / 2, bw = (w - 12) / 2;
    if (in->moved && in->py > LY + 40 && in->py < LY + 200) focus = in->px < x + bw + 6 ? F_ANOTHER : F_EXIT;
    if (in->down & (BTN_LEFT | BTN_RIGHT | BTN_UP | BTN_DOWN)) focus = focus == F_ANOTHER ? F_EXIT : F_ANOTHER;
    if (in->down & BTN_A) activate(focus);
}

/* ── Connect screen ──────────────────────────────────────────────────────── */

static void draw_connect(void)
{
    hero("Send a PictoCard!", "Greeting cards by email, and letters to a Wii Message Board with WiiLink.");
    int w = SW > 700 ? 520 : 440, x = (SW - w) / 2;
    int top = ui_panel(x, LY, w, 200, NULL, "Getting ready");
    if (api.busy || (!api.finished && !api.error[0])) {
        ui_spinner(x + 40, top + 50, 14, T.accent, frame);
        text_draw(FONT_BOLD, 15, x + 70, top + 28, T.text,
                  api.stage == STAGE_NETWORK ? "Connecting to the Internet..." : "Loading PictoCard...");
        text_draw_fit(FONT_REGULAR, 13, x + 70, top + 52, w - 90, T.muted, settings.server);
    } else {
        ui_wrapped(FONT_BOLD, 14, x + 16, top + 14, w - 32, T.danger, api.error, 4);
        ui_button(x + 16, top + 110, w - 32, 40, "Try again", 1, 1, 0);
    }
    char ver[64];
    snprintf(ver, sizeof ver, "PictoCard for Wii %s", APP_VERSION);
    text_draw_center(FONT_REGULAR, 12, 0, LY + 216, SW, T.muted, ver);
    static const char *const hints[] = { "A", "Retry", "HOME", "Exit" };
    ui_footer_hints(hints, 2);
}

/* ── Busy dialog ─────────────────────────────────────────────────────────── */

static void draw_busy(void)
{
    ui_dim();
    int w = 420, h = 150, x = (SW - w) / 2, y = (SH - h) / 2;
    int top = ui_panel(x, y, w, h, NULL, api.job == JOB_FRIEND ? "Sending a friend request" : pending_review ? "Getting your card ready" : "Sending your card");
    const char *stage = "Working...";
    switch (api.stage) {
    case STAGE_CHALLENGE: stage = "Getting an anti-spam check..."; break;
    case STAGE_SOLVING: stage = "Solving the anti-spam check on your Wii..."; break;
    case STAGE_UPLOADING: stage = pending_review ? "Drawing your card..." : "Uploading your card..."; break;
    case STAGE_WAITING: stage = "Waiting for PictoCard..."; break;
    }
    ui_spinner(x + 34, top + 34, 12, T.accent, frame);
    text_draw_fit(FONT_BOLD, 14, x + 60, top + 22, w - 76, T.text, stage);
    if (api.stage == STAGE_SOLVING || api.stage == STAGE_UPLOADING) {
        gfx_round(x + 60, top + 56, w - 80, 10, 5, T.elev);
        int pw = (w - 80) * api.progress / 100;
        if (pw > 0) {
            gfx_clip(x + 60, top + 56, pw, 10);
            gfx_tile(img_stripe, x + 60, top + 56, w - 80, 10, 16);
            gfx_noclip();
        }
    }
}

/* ── Job results ─────────────────────────────────────────────────────────── */

static void free_preview(void)
{
    image_free(preview_card);
    preview_card = NULL;
    for (int i = 0; i < preview_letters; i++) image_free(preview_letter[i]);
    preview_letters = 0;
    image_free(preview_fit_card);
    preview_fit_card = NULL;
    image_free(review_fit);
    review_fit = NULL;
    review_fit_src = NULL;
    (void)preview_fit_letter;
}

static void handle_finished(void)
{
    api.finished = 0;
    int job = api.job;
    if (job == JOB_CONNECT) {
        if (api.ok) {
            screen = SCR_COMPOSE;
            focus = F_METHOD;
            draft.font = 0;
            for (int i = 0; i < meta.font_count; i++)
                if (!strcmp(meta.fonts[i].id, "rodinntlg")) draft.font = i;
            if (strcmp(meta.min_client, APP_VERSION) > 0)
                show_alert("A newer PictoCard for Wii is available. Download it from pictocard.net/wii-app.", 1);
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
            preview_version = requested_version;
            preview_error[0] = 0;
        } else {
            snprintf(preview_error, sizeof preview_error, "%s", api.error);
            if (pending_review) {
                pending_review = 0;
                busy_modal = 0;
                show_alert(api.error, 1);
            }
            preview_version = requested_version; /* don't retry the same draft in a loop */
        }
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
            focus = F_SEND;
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
        focus = F_CODE;
        alert_frames = 0;
    } else {
        done_wii = draft.method == 1;
        screen = SCR_DONE;
        focus = F_ANOTHER;
        alert_frames = 0;
        start_confetti();
    }
}

/* ── Main loop ───────────────────────────────────────────────────────────── */

static void reset_cb(u32 irq, void *ctx)
{
    (void)irq;
    (void)ctx;
    quit = 1;
}
static void power_cb(void) { quit = 2; }

void app_run(void)
{
    SYS_SetResetCallback(reset_cb);
    SYS_SetPowerCallback(power_cb);
    WPAD_SetPowerButtonCallback(NULL);

    layout_columns();
    draft.mii = -1;
    draft.letterhead = -1;
    draft.color = -1;
    snprintf(draft.sender_name, sizeof draft.sender_name, "%s", settings.sender_name);
    snprintf(draft.sender_email, sizeof draft.sender_email, "%s", settings.sender_email);
    if (mii_count) {
        /* pick the Mii named like the sender, else none */
        for (int i = 0; i < mii_count; i++)
            if (draft.sender_name[0] && !strcmp(miis[i].name, draft.sender_name)) draft.mii = i;
    }
    api_connect();

    input_t in;
    memset(&in, 0, sizeof in);
    while (!quit) {
        frame++;
        input_poll(&in);
        if (in.down & BTN_HOME) break;
        if (api.finished) handle_finished();

        if (osk_active()) osk_update(&in);
        else if (picker_active()) picker_update(&in);
        else if (files_active()) files_update(&in);
        else if (busy_modal) {
            key_event ev;
            while (input_key(&ev)) {}
        } else {
            switch (screen) {
            case SCR_CONNECT: {
                key_event ev;
                while (input_key(&ev))
                    if (ev.code == KEY_ENTER) in.down |= BTN_A;
                if ((in.down & BTN_A) && !api.busy && api.error[0]) activate(F_RETRY);
                if (in.down & BTN_MINUS) activate(F_THEME);
                break;
            }
            case SCR_COMPOSE: compose_update(&in); break;
            case SCR_REVIEW: review_update(&in); break;
            case SCR_VERIFY: verify_update(&in); break;
            case SCR_DONE: done_update(&in); break;
            }
        }
        if ((in.down & BTN_MINUS) && !osk_active() && !picker_active() && !files_active() && !busy_modal &&
            screen != SCR_COMPOSE && screen != SCR_CONNECT)
            activate(F_THEME);
        if (alert_frames > 0 && (in.down & ~BTN_DIRS) && !busy_modal) alert_frames = alert_frames > 60 ? 60 : alert_frames;

        /* live preview: redraw on the server shortly after the draft settles */
        if (screen == SCR_COMPOSE && meta.loaded && !api.busy && version != preview_version &&
            requested_version != version && frame - changed_frame > 50)
            request_preview();
        if (pending_review && !api.busy && requested_version != version) request_preview();

        build_widgets();
        if (!find(focus) && screen == SCR_COMPOSE && focus != F_REVIEW) focus = F_METHOD;
        scroll += (scroll_target - scroll) / 3 + (scroll_target > scroll) - (scroll_target < scroll);

        /* While the Wii solves the anti-spam check, redraw rarely so the
         * solver gets the CPU. */
        if (busy_modal && api.busy && api.stage == STAGE_SOLVING && frame % 10) {
            VIDEO_WaitVSync();
            continue;
        }

        /* draw */
        gfx_fill(0, 0, SW, SH, T.bg);
        ui_header(settings.light);
        switch (screen) {
        case SCR_CONNECT: draw_connect(); break;
        case SCR_COMPOSE: draw_compose(); break;
        case SCR_REVIEW: draw_review(); break;
        case SCR_VERIFY: draw_verify(); break;
        case SCR_DONE: draw_done(); break;
        }
        if (alert_frames > 0) {
            alert_frames--;
            int w = SW > 700 ? 600 : 520, x = (SW - w) / 2;
            int h = ui_alert_height(w, alert_text);
            int y = SH - 44 - h;
            gfx_round(x, y + 3, w, h, RADIUS, T.shadow);
            gfx_fill(x, y, w, h, T.bg);
            ui_alert(x, y, w, alert_text, alert_error);
        }
        if (osk_active()) osk_draw(frame);
        else if (picker_active()) picker_draw(frame);
        else if (files_active()) files_draw(frame);
        if (busy_modal) draw_busy();

#ifdef AUTOTEST
        {
            extern int autotest_quit;
            extern void autotest_after_draw(void);
            autotest_after_draw();
            if (autotest_quit) quit = 2; /* power off so Dolphin stops and syncs the SD folder */
        }
#endif
        VIDEO_WaitVSync();
        gfx_present(in.pointer, in.px, in.py);
    }
    settings_save();
    if (quit == 2) SYS_ResetSystem(SYS_POWEROFF, 0, 0);
}
