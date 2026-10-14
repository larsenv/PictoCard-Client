/* On-screen keyboard. Works with every controller (D-Pad or pointer + A),
 * and a USB keyboard types straight into it. */
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "input.h"
#include "font.h"
#include "ui.h"

#define COLS 12

enum { K_CHAR, K_SHIFT, K_SYMBOLS, K_SPACE, K_ENTER, K_LEFT, K_RIGHT, K_BACK, K_OK };

typedef struct {
    int kind;
    int span;
    const char *text; /* for K_CHAR: the character (UTF-8) */
} osk_key_t;

static const char *LOWER[4][COLS] = {
    { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "=" },
    { "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "[", "]" },
    { "a", "s", "d", "f", "g", "h", "j", "k", "l", ";", "'", "@" },
    { NULL, "z", "x", "c", "v", "b", "n", "m", ",", ".", "/", "_" },
};
static const char *UPPER[4][COLS] = {
    { "!", "@", "#", "$", "%", "^", "&", "*", "(", ")", "_", "+" },
    { "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "{", "}" },
    { "A", "S", "D", "F", "G", "H", "J", "K", "L", ":", "\"", "@" },
    { NULL, "Z", "X", "C", "V", "B", "N", "M", "<", ">", "?", "~" },
};
static const char *SYM_LOWER[4][COLS] = {
    { "!", "?", "#", "$", "%", "&", "*", "(", ")", "+", "=", "~" },
    { "à", "á", "â", "ä", "ã", "å", "æ", "ç", "è", "é", "ê", "ë" },
    { "ì", "í", "î", "ï", "ñ", "ò", "ó", "ô", "ö", "õ", "ø", "ù" },
    { NULL, "ú", "û", "ü", "ý", "ÿ", "ß", "ð", "¿", "¡", "¢", "£" },
};
static const char *SYM_UPPER[4][COLS] = {
    { "<", ">", "{", "}", "[", "]", "|", "\\", "^", "`", "°", "·" },
    { "À", "Á", "Â", "Ä", "Ã", "Å", "Æ", "Ç", "È", "É", "Ê", "Ë" },
    { "Ì", "Í", "Î", "Ï", "Ñ", "Ò", "Ó", "Ô", "Ö", "Õ", "Ø", "Ù" },
    { NULL, "Ú", "Û", "Ü", "Ý", "¥", "©", "Ð", "«", "»", "®", "§" },
};
static const char *NUMPAD[4][3] = { { "1", "2", "3" }, { "4", "5", "6" }, { "7", "8", "9" }, { NULL, "0", NULL } };

static struct {
    int active;
    const char *label;
    char *target;
    int target_size;
    char buf[TEXT_BYTES];
    int caret;
    int max_chars;
    int multiline, numeric, email;
    int shift, caps, symbols;
    int row, col;
    int blink;
} k;

int osk_active(void) { return k.active; }

void osk_open(const char *label, char *buf, int buf_size, int max_chars, int multiline, int numeric, int email)
{
    memset(&k, 0, sizeof k);
    k.active = 1;
    k.label = label;
    k.target = buf;
    k.target_size = buf_size < TEXT_BYTES ? buf_size : TEXT_BYTES;
    snprintf(k.buf, sizeof k.buf, "%s", buf);
    k.caret = strlen(k.buf);
    k.max_chars = max_chars;
    k.multiline = multiline;
    k.numeric = numeric;
    k.email = email;
    k.row = numeric ? 0 : 1;
}

static int char_count(const char *s)
{
    int n = 0;
    while (*s) {
        utf8_next(&s);
        n++;
    }
    return n;
}

static void insert(const char *s)
{
    int len = strlen(s), cur = strlen(k.buf);
    if (cur + len >= k.target_size || char_count(k.buf) + char_count(s) > k.max_chars) return;
    if (k.numeric && (s[0] < '0' || s[0] > '9')) return;
    memmove(k.buf + k.caret + len, k.buf + k.caret, cur - k.caret + 1);
    memcpy(k.buf + k.caret, s, len);
    k.caret += len;
    k.blink = 0;
    if (k.shift && !k.caps) k.shift = 0;
}

void osk_insert_text(const char *s) { insert(s); }

static void backspace(void)
{
    if (!k.caret) return;
    int prev = utf8_prev(k.buf, k.caret);
    memmove(k.buf + prev, k.buf + k.caret, strlen(k.buf) - k.caret + 1);
    k.caret = prev;
    k.blink = 0;
}

static void delete_forward(void)
{
    if (!k.buf[k.caret]) return;
    const char *p = k.buf + k.caret;
    utf8_next(&p);
    memmove(k.buf + k.caret, p, strlen(p) + 1);
}

static void move_caret(int dir)
{
    if (dir < 0) k.caret = utf8_prev(k.buf, k.caret);
    else if (k.buf[k.caret]) {
        const char *p = k.buf + k.caret;
        utf8_next(&p);
        k.caret = p - k.buf;
    }
    k.blink = 0;
}

static void finish(void)
{
    snprintf(k.target, k.target_size, "%s", k.buf);
    k.active = 0;
    draft_changed();
}

/* ── Layout ──────────────────────────────────────────────────────────────── */

static int rows(void) { return 5; }

/* Fill keys for a row; returns count. Units always total COLS (or 3 for the numpad). */
static int row_keys(int row, osk_key_t *out)
{
    if (k.numeric) {
        if (row == 4) {
            out[0] = (osk_key_t){ K_BACK, 1, "Del" };
            out[1] = (osk_key_t){ K_OK, 2, "OK" };
            return 2;
        }
        int n = 0;
        for (int c = 0; c < 3; c++) {
            const char *t = NUMPAD[row][c];
            if (t) out[n++] = (osk_key_t){ K_CHAR, 1, t };
            else if (row == 3 && c == 0) out[n++] = (osk_key_t){ K_LEFT, 1, "<" };
            else out[n++] = (osk_key_t){ K_RIGHT, 1, ">" };
        }
        return n;
    }
    if (row == 4) {
        int n = 0;
        out[n++] = (osk_key_t){ K_SYMBOLS, 2, k.symbols ? "ABC" : "àé?" };
        out[n++] = (osk_key_t){ K_SPACE, k.multiline ? 3 : 4, "Space" };
        if (k.multiline) out[n++] = (osk_key_t){ K_ENTER, 1, "Enter" };
        out[n++] = (osk_key_t){ K_LEFT, 1, "<" };
        out[n++] = (osk_key_t){ K_RIGHT, 1, ">" };
        out[n++] = (osk_key_t){ K_BACK, 1, "Del" };
        out[n++] = (osk_key_t){ K_OK, 2, "OK" };
        return n;
    }
    const char *(*layout)[COLS] = k.symbols ? (k.shift ? SYM_UPPER : SYM_LOWER) : (k.shift ? UPPER : LOWER);
    int n = 0;
    for (int c = 0; c < COLS; c++) {
        const char *t = layout[row][c];
        if (!t) out[n++] = (osk_key_t){ K_SHIFT, 1, "Shift" };
        else if (k.email && row == 2 && c == 11) out[n++] = (osk_key_t){ K_CHAR, 1, k.shift ? "+" : "@" };
        else out[n++] = (osk_key_t){ K_CHAR, 1, t };
    }
    return n;
}

typedef struct { int x, y, w, h; } rect_t;

static int units(void) { return k.numeric ? 3 : COLS; }

static void geometry(int *px, int *py, int *pw, int *ph, int *kx, int *ky, int *kw, int *kh)
{
    int w = k.numeric ? 340 : (SW - 48 > 640 ? 640 : SW - 48);
    int text_h = k.multiline ? 120 : 40;
    int key_h = 36;
    int h = 42 + 34 + text_h + 14 + rows() * (key_h + 6) + 14;
    *pw = w;
    *ph = h;
    *px = (SW - w) / 2;
    *py = SH - 40 - h;
    if (*py < 70) *py = 70;
    *kx = *px + 14;
    *ky = *py + 42 + 34 + text_h + 14;
    *kw = (w - 28) / units();
    *kh = key_h;
}

static rect_t key_rect(int row, int index, osk_key_t *keys)
{
    int px, py, pw, ph, kx, ky, kw, kh;
    geometry(&px, &py, &pw, &ph, &kx, &ky, &kw, &kh);
    int u = 0;
    for (int i = 0; i < index; i++) u += keys[i].span;
    return (rect_t){ kx + u * kw, ky + row * (kh + 6), keys[index].span * kw - 6, kh };
}

static void press(const osk_key_t *key)
{
    switch (key->kind) {
    case K_CHAR: insert(key->text); break;
    case K_SHIFT:
        if (k.shift && !k.caps) k.caps = 1;
        else if (k.caps) k.shift = k.caps = 0;
        else k.shift = 1;
        break;
    case K_SYMBOLS: k.symbols = !k.symbols; break;
    case K_SPACE: insert(" "); break;
    case K_ENTER: insert("\n"); break;
    case K_LEFT: move_caret(-1); break;
    case K_RIGHT: move_caret(1); break;
    case K_BACK: backspace(); break;
    case K_OK: finish(); break;
    }
}

/* Move the highlight vertically to the key whose centre is closest */
static void move_row(int dir)
{
    osk_key_t cur[COLS], next[COLS];
    int n = row_keys(k.row, cur);
    if (k.col >= n) k.col = n - 1;
    rect_t r = key_rect(k.row, k.col, cur);
    int nr = (k.row + dir + rows()) % rows();
    int m = row_keys(nr, next);
    int best = 0, bestd = 1 << 30;
    for (int i = 0; i < m; i++) {
        rect_t q = key_rect(nr, i, next);
        int d = (q.x + q.w / 2) - (r.x + r.w / 2);
        if (d < 0) d = -d;
        if (d < bestd) {
            bestd = d;
            best = i;
        }
    }
    k.row = nr;
    k.col = best;
}

void osk_update(void *vin)
{
    input_t *in = vin;
    osk_key_t keys[COLS];
    int n = row_keys(k.row, keys);
    if (k.col >= n) k.col = n - 1;
    k.blink++;

    /* USB keyboard */
    key_event ev;
    while (k.active && input_key(&ev)) {
        if (ev.ch) insert((char[]){ ev.ch, 0 });
        else if (ev.code == KEY_BACKSPACE) backspace();
        else if (ev.code == KEY_DELETE) delete_forward();
        else if (ev.code == KEY_LEFT) move_caret(-1);
        else if (ev.code == KEY_RIGHT) move_caret(1);
        else if (ev.code == KEY_HOME) k.caret = 0;
        else if (ev.code == KEY_END) k.caret = strlen(k.buf);
        else if (ev.code == KEY_ENTER || ev.code == KEY_KP_ENTER) {
            if (k.multiline && !ev.ctrl) insert("\n");
            else finish();
        } else if (ev.code == KEY_ESC || ev.code == KEY_TAB) finish();
    }
    if (!k.active) return;

    if (in->pointer && in->moved) {
        for (int r = 0; r < rows(); r++) {
            osk_key_t rk[COLS];
            int m = row_keys(r, rk);
            for (int i = 0; i < m; i++) {
                rect_t q = key_rect(r, i, rk);
                if (in->px >= q.x && in->px < q.x + q.w + 6 && in->py >= q.y && in->py < q.y + q.h + 6) {
                    k.row = r;
                    k.col = i;
                }
            }
        }
        n = row_keys(k.row, keys);
    }
    if (in->down & BTN_LEFT) k.col = (k.col + n - 1) % n;
    if (in->down & BTN_RIGHT) k.col = (k.col + 1) % n;
    if (in->down & BTN_UP) move_row(-1);
    if (in->down & BTN_DOWN) move_row(1);
    n = row_keys(k.row, keys);
    if (k.col >= n) k.col = n - 1;
    if (in->down & BTN_A) press(&keys[k.col]);
    else if (in->down & BTN_B) backspace();
    else if (in->down & BTN_PLUS) finish();
    else if (in->down & BTN_MINUS) {
        osk_key_t s = { K_SHIFT, 1, NULL };
        if (!k.numeric) press(&s);
    } else if (in->down & BTN_1) {
        if (!k.numeric) k.symbols = !k.symbols;
    } else if (in->down & BTN_2) insert(k.multiline || !k.numeric ? " " : "");
}

void osk_draw(int frame)
{
    (void)frame;
    int px, py, pw, ph, kx, ky, kw, kh;
    geometry(&px, &py, &pw, &ph, &kx, &ky, &kw, &kh);
    ui_dim();
    int top = ui_panel(px, py, pw, ph, NULL, k.label);
    char count[32];
    snprintf(count, sizeof count, "%d / %d", char_count(k.buf), k.max_chars);
    text_draw(FONT_REGULAR, 12, px + pw - 14 - text_width(FONT_REGULAR, 12, count), py + 14, T.muted, count);

    /* text box */
    int bx = px + 14, by = top + 12, bw = pw - 28, bh = k.multiline ? 120 : 40;
    gfx_frame(bx, by, bw, bh, RADIUS, T.input_bg, T.accent, 2);
    int show_caret = (k.blink / 30) % 2 == 0;
    if (k.multiline) {
        int starts[64], lens[64];
        int nl = text_wrap(FONT_REGULAR, 14, k.buf, bw - 24, starts, lens, 64);
        int caret_line = 0;
        for (int i = 0; i < nl; i++)
            if (k.caret >= starts[i]) caret_line = i;
        int visible = (bh - 12) / 22;
        int first = caret_line >= visible ? caret_line - visible + 1 : 0;
        gfx_clip(bx + 2, by + 2, bw - 4, bh - 4);
        for (int i = first; i < nl && i < first + visible; i++) {
            int y = by + 6 + (i - first) * 22;
            text_draw_n(FONT_REGULAR, 14, bx + 12, y, T.text, k.buf + starts[i], lens[i]);
            if (i == caret_line && show_caret) {
                int cx = bx + 12 + text_width_n(FONT_REGULAR, 14, k.buf + starts[i], k.caret - starts[i]);
                gfx_fill(cx, y + 3, 2, 17, T.accent);
            }
        }
        gfx_noclip();
    } else {
        gfx_clip(bx + 2, by, bw - 4, bh);
        int before = text_width_n(FONT_REGULAR, 14, k.buf, k.caret);
        int tx = bx + 12;
        if (before > bw - 30) tx -= before - (bw - 30);
        text_draw(FONT_REGULAR, 14, tx, by + 9, T.text, k.buf);
        if (show_caret) gfx_fill(tx + before, by + 11, 2, 18, T.accent);
        gfx_noclip();
    }

    /* keys */
    for (int r = 0; r < rows(); r++) {
        osk_key_t keys[COLS];
        int n = row_keys(r, keys);
        for (int i = 0; i < n; i++) {
            rect_t q = key_rect(r, i, keys);
            int sel = r == k.row && i == k.col;
            int on = (keys[i].kind == K_SHIFT && k.shift) || (keys[i].kind == K_SYMBOLS && k.symbols);
            int special = keys[i].kind != K_CHAR;
            u32 fill = sel ? T.tab_bg : on ? T.hover : special ? T.elev : T.input_bg;
            if (keys[i].kind == K_OK) {
                ui_button(q.x, q.y, q.w, q.h, "OK", 1, sel, 0);
                continue;
            }
            if (sel) ui_focus_ring(q.x, q.y, q.w, q.h);
            gfx_round(q.x, q.y + 2, q.w, q.h, RADIUS, T.shadow);
            gfx_frame(q.x, q.y, q.w, q.h, RADIUS, fill, sel ? T.accent : T.input_border, 1);
            const char *label = keys[i].text;
            if (keys[i].kind == K_SHIFT && k.caps) label = "CAPS";
            int font = special ? FONT_BOLD : FONT_REGULAR;
            int size = special ? 13 : 16;
            text_draw_center(font, size, q.x, q.y + (q.h - size * 155 / 100) / 2, q.w, sel ? T.accent : T.text, label);
        }
    }
    static const char *const hints[] = { "A", "Type", "B", "Delete", "+", "Done", "-", "Shift", "1", "Symbols" };
    static const char *const num_hints[] = { "A", "Type", "B", "Delete", "+", "Done" };
    if (k.numeric) ui_footer_hints(num_hints, 3);
    else ui_footer_hints(hints, 5);
}
