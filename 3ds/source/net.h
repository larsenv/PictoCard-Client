#pragma once
#include <stddef.h>

typedef struct {
    int status;            /* HTTP status, or 0 when the request failed */
    char *body;            /* NUL-terminated, malloc'd */
    size_t len;
    char error[192];       /* why it failed, for the user */
} http_response;

/* Bring up the network (soc:U). Returns 0 on success; safe to call again. */
int net_start(char *ip, size_t ip_len);
void net_stop(void);
/* GET or POST to http:// or https:// URLs. *progress (0..100) tracks the upload. */
int http_request(const char *method, const char *url, const char *content_type, const char *body,
                 size_t body_len, http_response *res, volatile int *progress);
void http_response_free(http_response *res);
