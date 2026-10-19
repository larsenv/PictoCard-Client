#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <gccore.h>
#include "api.h"
#include "net.h"
#include "image.h"
#include "util.h"
#include "settings.h"
#include "cJSON.h"
#include "pow.h"

meta_t meta;
api_state_t api;

typedef struct {
    int job;
    char *body;
    char arg1[96];
    char arg2[32];
} job_t;

static lwp_t worker = LWP_THREAD_NULL;
static job_t current;

void api_init(void)
{
    memset(&api, 0, sizeof api);
}

static void set_error(const char *msg)
{
    snprintf(api.error, sizeof api.error, "%s", msg);
}

static void copy_str(char *dst, size_t len, const cJSON *obj, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    snprintf(dst, len, "%s", cJSON_IsString(v) ? v->valuestring : "");
}

/* POST/GET to /api/v1/<route>; returns parsed JSON for 2xx, else NULL with api.error set */
static cJSON *call(const char *method, const char *route, const char *body, size_t len, volatile int *progress)
{
    char url[256];
    snprintf(url, sizeof url, "%s/api/v1/%s", settings.server, route);
    http_response res;
    int rc;
    /* A failed connect or TLS handshake happens before anything is sent, so it is
     * safe to try again; Wi-Fi hiccups make these fail now and then. */
    for (int attempt = 0;; attempt++) {
        rc = http_request(method, url, "application/json", body, len, &res, progress);
        if (rc >= 0 || attempt >= 2 ||
            (!strstr(res.error, "secure connection failed") && !strstr(res.error, "Could not connect")))
            break;
        http_response_free(&res);
        usleep(400 * 1000);
    }
    if (rc < 0) {
        set_error(res.error);
        api.status = 0;
        return NULL;
    }
    api.status = res.status;
    cJSON *json = res.body ? cJSON_Parse(res.body) : NULL;
    if (res.status < 200 || res.status >= 300 || !json) {
        const cJSON *err = json ? cJSON_GetObjectItemCaseSensitive(json, "error") : NULL;
        if (cJSON_IsString(err)) set_error(err->valuestring);
        else if (res.status == 429) set_error("Too many requests. Please wait a few minutes and try again.");
        else if (res.status == 404) set_error("This PictoCard server does not support the Wii app yet.");
        else snprintf(api.error, sizeof api.error, "The PictoCard server had a problem (HTTP %d). Please try again.", res.status);
        cJSON_Delete(json);
        json = NULL;
    }
    http_response_free(&res);
    return json;
}

static int load_named(const cJSON *arr, named_t *out, int max)
{
    int n = 0;
    const cJSON *item;
    cJSON_ArrayForEach(item, arr)
    {
        if (n >= max) break;
        copy_str(out[n].id, sizeof out[n].id, item, "id");
        copy_str(out[n].name, sizeof out[n].name, item, "name");
        copy_str(out[n].group, sizeof out[n].group, item, "group");
        n++;
    }
    return n;
}

static int do_connect(void)
{
    api.stage = STAGE_NETWORK;
    if (net_start(api.ip, sizeof api.ip) < 0) {
        set_error("Could not connect to the Internet. Check the Wii's Internet settings and try again.");
        return 0;
    }
    api.stage = STAGE_WAITING;
    cJSON *json = call("GET", "meta", NULL, 0, NULL);
    if (!json) return 0;
    memset(&meta, 0, sizeof meta);
    meta.wii_enabled = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json, "wiiEnabled"));
    copy_str(meta.pictocard_wii, sizeof meta.pictocard_wii, json, "pictocardWiiNumber");
    copy_str(meta.min_client, sizeof meta.min_client, json, "minClientVersion");
    const cJSON *max = cJSON_GetObjectItemCaseSensitive(json, "maxTextLength");
    meta.max_text = cJSON_IsNumber(max) ? max->valueint : 500;
    meta.font_count = load_named(cJSON_GetObjectItemCaseSensitive(json, "fonts"), meta.fonts, 32);
    meta.letterhead_count = load_named(cJSON_GetObjectItemCaseSensitive(json, "letterheads"), meta.letterheads, LETTERHEAD_MAX);
    meta.expression_count = load_named(cJSON_GetObjectItemCaseSensitive(json, "expressions"), meta.expressions, 64);
    meta.loaded = 1;
    cJSON_Delete(json);
    return 1;
}

static image_t *decode_b64_image(const cJSON *v)
{
    if (!cJSON_IsString(v) || !*v->valuestring) return NULL;
    size_t len;
    unsigned char *data = base64_decode(v->valuestring, &len);
    if (!data) return NULL;
    image_t *img = image_decode(data, len, 640);
    free(data);
    return img;
}

static int do_preview(char *body)
{
    api.stage = STAGE_UPLOADING;
    cJSON *json = call("POST", "preview", body, strlen(body), &api.progress);
    if (!json) return 0;
    api.card = decode_b64_image(cJSON_GetObjectItemCaseSensitive(json, "card"));
    const cJSON *page;
    api.letter_count = 0;
    cJSON_ArrayForEach(page, cJSON_GetObjectItemCaseSensitive(json, "letter"))
    {
        if (api.letter_count >= 8) break;
        image_t *img = decode_b64_image(page);
        if (img) api.letter[api.letter_count++] = img;
    }
    api.needs_image = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json, "needsImage"));
    cJSON_Delete(json);
    return 1;
}

static int do_faces(char *body)
{
    api.stage = STAGE_WAITING;
    cJSON *json = call("POST", "mii-faces", body, strlen(body), NULL);
    if (!json) return 0;
    api.face_count = 0;
    const cJSON *face;
    cJSON_ArrayForEach(face, cJSON_GetObjectItemCaseSensitive(json, "faces"))
    {
        if (api.face_count >= 16) break;
        api.faces[api.face_count++] = decode_b64_image(face); /* NULL when it could not be rendered */
    }
    cJSON_Delete(json);
    return 1;
}

static int do_letterheads(char *body)
{
    api.stage = STAGE_WAITING;
    cJSON *json = call("POST", "letterhead-sheet", body, strlen(body), NULL);
    if (!json) return 0;
    const cJSON *img = cJSON_GetObjectItemCaseSensitive(json, "image");
    const cJSON *ids = cJSON_GetObjectItemCaseSensitive(json, "ids");
    api.sheet_w = cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(json, "w"));
    api.sheet_h = cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(json, "h"));
    api.sheet_cols = cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(json, "cols"));
    api.sheet_count = cJSON_GetArraySize(ids);
    api.sheet = NULL;
    if (cJSON_IsString(img)) {
        size_t len;
        unsigned char *data = base64_decode(img->valuestring, &len);
        if (data) api.sheet = image_decode(data, len, 2048);
        free(data);
    }
    cJSON_Delete(json);
    return api.sheet != NULL;
}

/* ── Proof of work ───────────────────────────────────────────────────────── */

/* Fetch a challenge for `purpose` and solve it; fills pow_json with
 * {"challenge":"...","nonce":"..."} */
static int solve_challenge(const char *purpose, char *pow_json, size_t pow_len)
{
    api.stage = STAGE_CHALLENGE;
    char req[64];
    snprintf(req, sizeof req, "{\"purpose\":\"%s\"}", purpose);
    cJSON *json = call("POST", "challenge", req, strlen(req), NULL);
    if (!json) return 0;
    const cJSON *ch = cJSON_GetObjectItemCaseSensitive(json, "challenge");
    const cJSON *d = cJSON_GetObjectItemCaseSensitive(json, "difficulty");
    if (!cJSON_IsString(ch) || !cJSON_IsNumber(d) || strlen(ch->valuestring) > 400) {
        set_error("The server sent an invalid anti-spam check.");
        cJSON_Delete(json);
        return 0;
    }
    char challenge[420];
    snprintf(challenge, sizeof challenge, "%s", ch->valuestring);
    int difficulty = d->valueint;
    cJSON_Delete(json);

    api.stage = STAGE_SOLVING;
    char nonce[40];
    if (pow_solve(challenge, difficulty, nonce, &api.progress) < 0) {
        set_error("Could not solve the anti-spam check.");
        return 0;
    }
    snprintf(pow_json, pow_len, "{\"challenge\":\"%s\",\"nonce\":\"%s\"}", challenge, nonce);
    return 1;
}

/* body ends with '}'; add "pow": before it */
static char *with_pow(const char *body, const char *pow)
{
    size_t len = strlen(body);
    char *out = malloc(len + strlen(pow) + 16);
    if (!out) return NULL;
    const char *end = strrchr(body, '}');
    size_t head = end ? (size_t)(end - body) : len;
    memcpy(out, body, head);
    sprintf(out + head, "%s\"pow\":%s}", head > 1 ? "," : "", pow);
    return out;
}

static int do_send(char *body)
{
    char pow[512];
    if (!solve_challenge("send", pow, sizeof pow)) return 0;
    char *full = with_pow(body, pow);
    if (!full) {
        set_error("The Wii ran out of memory.");
        return 0;
    }
    api.stage = STAGE_UPLOADING;
    api.progress = 0;
    cJSON *json = call("POST", "send", full, strlen(full), &api.progress);
    free(full);
    if (!json) return 0;
    copy_str(api.result, sizeof api.result, json, "status");
    copy_str(api.ticket, sizeof api.ticket, json, "ticket");
    copy_str(api.verify_email, sizeof api.verify_email, json, "email");
    cJSON_Delete(json);
    return 1;
}

static int do_verify(const char *ticket, const char *code)
{
    api.stage = STAGE_WAITING;
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "ticket", ticket);
    cJSON_AddStringToObject(req, "code", code);
    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    cJSON *json = call("POST", "verify", body, strlen(body), NULL);
    free(body);
    if (!json) return 0;
    copy_str(api.result, sizeof api.result, json, "status");
    cJSON_Delete(json);
    return 1;
}

static int do_friend(const char *number)
{
    char pow[512];
    if (!solve_challenge("friend", pow, sizeof pow)) return 0;
    char body[640];
    snprintf(body, sizeof body, "{\"wiiNumber\":\"%s\",\"pow\":%s}", number, pow);
    api.stage = STAGE_WAITING;
    cJSON *json = call("POST", "wii-friend", body, strlen(body), NULL);
    if (!json) return 0;
    copy_str(api.result, sizeof api.result, json, "status");
    cJSON_Delete(json);
    return 1;
}

static int do_arc(const char *id)
{
    char body[128];
    snprintf(body, sizeof body, "{\"id\":\"%s\"}", id);
    api.stage = STAGE_WAITING;
    cJSON *json = call("POST", "letterhead-arc", body, strlen(body), NULL);
    if (!json) return 0;
    const cJSON *arc = cJSON_GetObjectItemCaseSensitive(json, "arc");
    api.arc = NULL;
    api.arc_len = 0;
    if (cJSON_IsString(arc)) api.arc = base64_decode(arc->valuestring, &api.arc_len);
    cJSON_Delete(json);
    if (!api.arc) set_error("Could not download the letterhead.");
    return api.arc != NULL;
}

static void *run(void *arg)
{
    (void)arg;
    int ok = 0;
    switch (current.job) {
    case JOB_CONNECT: ok = do_connect(); break;
    case JOB_PREVIEW: ok = do_preview(current.body); break;
    case JOB_SEND: ok = do_send(current.body); break;
    case JOB_VERIFY: ok = do_verify(current.arg1, current.arg2); break;
    case JOB_FRIEND: ok = do_friend(current.arg1); break;
    case JOB_FACES: ok = do_faces(current.body); break;
    case JOB_LETTERHEADS: ok = do_letterheads(current.body); break;
    case JOB_ARC: ok = do_arc(current.arg1); break;
    }
    free(current.body);
    current.body = NULL;
    api.ok = ok;
    api.stage = STAGE_DONE;
    api.finished = 1;
    api.busy = 0;
    return NULL;
}

static int start(int job, char *body, const char *a1, const char *a2)
{
    if (api.busy) {
        free(body);
        return 0;
    }
    if (worker != LWP_THREAD_NULL) {
        LWP_JoinThread(worker, NULL);
        worker = LWP_THREAD_NULL;
    }
    memset(&current, 0, sizeof current);
    current.job = job;
    current.body = body;
    if (a1) snprintf(current.arg1, sizeof current.arg1, "%s", a1);
    if (a2) snprintf(current.arg2, sizeof current.arg2, "%s", a2);
    api.error[0] = 0;
    api.result[0] = 0;
    api.progress = 0;
    api.ok = 0;
    api.status = 0;
    api.card = NULL;
    api.face_count = 0;
    api.letter_count = 0;
    api.finished = 0;
    api.job = job;
    api.busy = 1;
    if (LWP_CreateThread(&worker, run, NULL, NULL, 128 * 1024, 60) < 0) {
        api.busy = 0;
        worker = LWP_THREAD_NULL;
        free(body);
        return 0;
    }
    return 1;
}

int api_connect(void) { return start(JOB_CONNECT, NULL, NULL, NULL); }
int api_preview(char *body) { return start(JOB_PREVIEW, body, NULL, NULL); }
int api_send(char *body) { return start(JOB_SEND, body, NULL, NULL); }
int api_verify(const char *ticket, const char *code) { return start(JOB_VERIFY, NULL, ticket, code); }
int api_friend(const char *number) { return start(JOB_FRIEND, NULL, number, NULL); }
int api_letterhead_arc(const char *id) { return start(JOB_ARC, NULL, id, NULL); }

int api_letterheads(int w, int h, int cols)
{
    char *body = malloc(64);
    if (!body) return 0;
    snprintf(body, 64, "{\"width\":%d,\"height\":%d,\"cols\":%d}", w, h, cols);
    return start(JOB_LETTERHEADS, body, NULL, NULL);
}

int api_faces(char *body, int first)
{
    int ok = start(JOB_FACES, body, NULL, NULL);
    if (ok) api.face_first = first;
    return ok;
}
