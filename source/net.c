/* HTTP and HTTPS for PictoCard on the 3DS: BSD sockets from soc:U and
 * mbedTLS, checked against the roots in certs.h. The system's own httpc
 * service is not used because its TLS stack and root store are too old for
 * today's servers. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <malloc.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <3ds.h>
#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/error.h>
#include <mbedtls/platform_time.h>
#include "net.h"
#include "certs.h"
#include "version.h"

#define TIMEOUT_MS 25000
#define SOC_BUFFER_SIZE 0x100000
#define ERR_SEND_FAILED -0x004E
#define ERR_RECV_FAILED -0x004C

static int net_ready;
static int tls_ready;
static u32 *soc_buffer;
static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context drbg;
static mbedtls_x509_crt cas;
static mbedtls_ssl_config conf;
static LightLock tls_lock;
static int lock_ready;

mbedtls_ms_time_t mbedtls_ms_time(void)
{
    return (mbedtls_ms_time_t)osGetTime();
}

int net_start(char *ip, size_t ip_len)
{
    if (!net_ready) {
        u32 wifi = 0;
        if (R_SUCCEEDED(acInit())) {
            /* give a freshly woken console a few seconds to join its access point */
            for (int i = 0; i < 50; i++) {
                if (R_SUCCEEDED(ACU_GetWifiStatus(&wifi)) && wifi) break;
                svcSleepThread(100 * 1000 * 1000);
            }
            acExit();
        }
        if (!wifi) return -1;
        if (!soc_buffer) soc_buffer = memalign(0x1000, SOC_BUFFER_SIZE);
        if (!soc_buffer || R_FAILED(socInit(soc_buffer, SOC_BUFFER_SIZE))) return -1;
        net_ready = 1;
    }
    if (ip) {
        struct in_addr a = { .s_addr = gethostid() };
        snprintf(ip, ip_len, "%s", inet_ntoa(a));
    }
    return 0;
}

void net_stop(void)
{
    if (net_ready) socExit();
    net_ready = 0;
}

/* ── Entropy ─────────────────────────────────────────────────────────────── */

static int hw_entropy(void *data, unsigned char *out, size_t len, size_t *olen)
{
    (void)data;
    if (R_FAILED(PS_GenerateRandomBytes(out, len))) return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    *olen = len;
    return 0;
}

static int tls_init(char *err, size_t err_len)
{
    if (tls_ready) return 0;
    if (R_FAILED(psInit())) {
        snprintf(err, err_len, "Could not start secure connections (no random numbers).");
        return -1;
    }
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&drbg);
    mbedtls_x509_crt_init(&cas);
    mbedtls_ssl_config_init(&conf);
    mbedtls_entropy_add_source(&entropy, hw_entropy, NULL, 32, MBEDTLS_ENTROPY_SOURCE_STRONG);

    char personal[64];
    snprintf(personal, sizeof personal, "PictoCard-3DS %llu %llu", (unsigned long long)osGetTime(),
             (unsigned long long)svcGetSystemTick());
    int ret = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, (const unsigned char *)personal,
                                    strlen(personal));
    if (!ret) ret = mbedtls_x509_crt_parse(&cas, (const unsigned char *)PICTOCARD_CA_PEM, sizeof PICTOCARD_CA_PEM);
    if (ret < 0) {
        snprintf(err, err_len, "Could not start secure connections (%d).", ret);
        return -1;
    }
    mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&conf, &cas, NULL);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);
    tls_ready = 1;
    return 0;
}

/* ── Sockets with timeouts ───────────────────────────────────────────────── */

static int wait_fd(int fd, int events)
{
    struct pollfd p = { .fd = fd, .events = events, .revents = 0 };
    int ret = poll(&p, 1, TIMEOUT_MS);
    if (ret == 0) return MBEDTLS_ERR_SSL_TIMEOUT;
    return ret < 0 ? -1 : 0;
}

static int sock_send(void *ctx, const unsigned char *buf, size_t len)
{
    int fd = (int)(intptr_t)ctx;
    if (wait_fd(fd, POLLOUT) < 0) return MBEDTLS_ERR_SSL_TIMEOUT;
    int ret = send(fd, buf, len > 16384 ? 16384 : len, 0);
    if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return MBEDTLS_ERR_SSL_WANT_WRITE;
    return ret < 0 ? ERR_SEND_FAILED : ret;
}

static int sock_recv(void *ctx, unsigned char *buf, size_t len)
{
    int fd = (int)(intptr_t)ctx;
    if (wait_fd(fd, POLLIN) < 0) return MBEDTLS_ERR_SSL_TIMEOUT;
    int ret = recv(fd, buf, len, 0);
    if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return MBEDTLS_ERR_SSL_WANT_READ;
    return ret < 0 ? ERR_RECV_FAILED : ret;
}

typedef struct {
    int fd;
    int tls;
    mbedtls_ssl_context ssl;
} conn_t;

static int conn_write(conn_t *c, const void *data, size_t len)
{
    const unsigned char *p = data;
    while (len) {
        int ret = c->tls ? mbedtls_ssl_write(&c->ssl, p, len) : sock_send((void *)(intptr_t)c->fd, p, len);
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (ret <= 0) return -1;
        p += ret;
        len -= ret;
    }
    return 0;
}

static int conn_read(conn_t *c, void *buf, size_t len)
{
    for (;;) {
        int ret = c->tls ? mbedtls_ssl_read(&c->ssl, buf, len) : sock_recv((void *)(intptr_t)c->fd, buf, len);
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || ret == 0) return 0;
        return ret;
    }
}

/* ── HTTP ────────────────────────────────────────────────────────────────── */

static int parse_url(const char *url, int *tls, char *host, size_t host_len, int *port, const char **path)
{
    const char *p;
    if (!strncmp(url, "https://", 8)) { *tls = 1; *port = 443; p = url + 8; }
    else if (!strncmp(url, "http://", 7)) { *tls = 0; *port = 80; p = url + 7; }
    else return -1;
    const char *end = p + strcspn(p, ":/");
    if ((size_t)(end - p) >= host_len || end == p) return -1;
    memcpy(host, p, end - p);
    host[end - p] = 0;
    if (*end == ':') {
        *port = atoi(end + 1);
        end += strcspn(end, "/");
    }
    *path = *end ? end : "/";
    return 0;
}

static void fail(http_response *res, const char *msg, int code)
{
    if (code) snprintf(res->error, sizeof res->error, "%s (%d)", msg, code);
    else snprintf(res->error, sizeof res->error, "%s", msg);
}

static int dechunk(char *body, size_t len, size_t *out_len)
{
    size_t r = 0, w = 0;
    while (r < len) {
        char *line_end = strstr(body + r, "\r\n");
        if (!line_end) return -1;
        size_t size = strtoul(body + r, NULL, 16);
        r = line_end - body + 2;
        if (size == 0) break;
        if (r + size > len) return -1;
        memmove(body + w, body + r, size);
        w += size;
        r += size + 2;
    }
    body[w] = 0;
    *out_len = w;
    return 0;
}

int http_request(const char *method, const char *url, const char *content_type, const char *body,
                 size_t body_len, http_response *res, volatile int *progress)
{
    memset(res, 0, sizeof *res);
    int tls, port;
    char host[128];
    const char *path;
    if (parse_url(url, &tls, host, sizeof host, &port, &path) < 0) {
        fail(res, "The server address is not valid.", 0);
        return -1;
    }
    if (!net_ready) {
        fail(res, "The 3DS is not connected to the Internet.", 0);
        return -1;
    }
    if (tls) {
        if (!lock_ready) {
            LightLock_Init(&tls_lock);
            lock_ready = 1;
        }
        LightLock_Lock(&tls_lock);
        int ret = tls_init(res->error, sizeof res->error);
        LightLock_Unlock(&tls_lock);
        if (ret < 0) return -1;
    }

    struct addrinfo hints, *ai = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char port_s[8];
    snprintf(port_s, sizeof port_s, "%d", port);
    if (getaddrinfo(host, port_s, &hints, &ai) != 0 || !ai) {
        fail(res, "Could not find the PictoCard server. Check the Internet connection.", 0);
        return -1;
    }

    conn_t c = { .fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP), .tls = tls };
    if (c.fd < 0) {
        freeaddrinfo(ai);
        fail(res, "Could not open a network connection.", errno);
        return -1;
    }
    int ret = connect(c.fd, ai->ai_addr, ai->ai_addrlen);
    freeaddrinfo(ai);
    if (ret < 0) {
        closesocket(c.fd);
        fail(res, "Could not connect to the PictoCard server.", errno);
        return -1;
    }

    int result = -1;
    char *buf = NULL;
    if (tls) {
        mbedtls_ssl_init(&c.ssl);
        if ((ret = mbedtls_ssl_setup(&c.ssl, &conf)) || (ret = mbedtls_ssl_set_hostname(&c.ssl, host))) {
            fail(res, "Could not start a secure connection.", ret);
            goto done;
        }
        mbedtls_ssl_set_bio(&c.ssl, (void *)(intptr_t)c.fd, sock_send, sock_recv, NULL);
        while ((ret = mbedtls_ssl_handshake(&c.ssl)) != 0) {
            if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
            if (ret == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED)
                fail(res, "The server's certificate could not be verified.", 0);
            else
                fail(res, "The secure connection failed.", ret);
            goto done;
        }
    }

    char head[640];
    int n = snprintf(head, sizeof head,
                     "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: PictoCard3DS/" APP_VERSION
                     "\r\nAccept: application/json\r\nConnection: close\r\n",
                     method, path, host);
    if (body) n += snprintf(head + n, sizeof head - n, "Content-Type: %s\r\nContent-Length: %u\r\n",
                            content_type ? content_type : "application/json", (unsigned)body_len);
    n += snprintf(head + n, sizeof head - n, "\r\n");
    if (conn_write(&c, head, n) < 0) {
        fail(res, "The connection was lost while sending.", 0);
        goto done;
    }
    for (size_t sent = 0; body && sent < body_len;) {
        size_t chunk = body_len - sent > 8192 ? 8192 : body_len - sent;
        if (conn_write(&c, body + sent, chunk) < 0) {
            fail(res, "The connection was lost while sending.", 0);
            goto done;
        }
        sent += chunk;
        if (progress) *progress = (int)(sent * 100 / body_len);
    }

    size_t cap = 16384, len = 0;
    buf = malloc(cap + 1);
    if (!buf) {
        fail(res, "The 3DS ran out of memory.", 0);
        goto done;
    }
    for (;;) {
        if (len + 4096 > cap) {
            if (cap >= 8 * 1024 * 1024) break;
            cap *= 2;
            char *grown = realloc(buf, cap + 1);
            if (!grown) break;
            buf = grown;
        }
        int r = conn_read(&c, buf + len, cap - len);
        if (r <= 0) {
            if (r < 0 && !len) {
                fail(res, r == MBEDTLS_ERR_SSL_TIMEOUT ? "The server took too long to answer." : "The connection was lost.", r);
                goto done;
            }
            break;
        }
        len += r;
    }
    buf[len] = 0;

    char *sep = strstr(buf, "\r\n\r\n");
    if (strncmp(buf, "HTTP/1.", 7) || !sep) {
        fail(res, "The server sent an unexpected answer.", 0);
        goto done;
    }
    res->status = atoi(buf + 9);
    *sep = 0;
    int chunked = 0;
    for (char *line = strstr(buf, "\r\n"); line; line = strstr(line + 2, "\r\n")) {
        if (!strncasecmp(line + 2, "Transfer-Encoding:", 18) && strstr(line + 20, "chunked")) chunked = 1;
    }
    size_t body_start = sep + 4 - buf;
    res->len = len - body_start;
    memmove(buf, buf + body_start, res->len + 1);
    if (chunked && dechunk(buf, res->len, &res->len) < 0) {
        fail(res, "The server's answer was cut off.", 0);
        res->status = 0;
        goto done;
    }
    res->body = buf;
    buf = NULL;
    result = 0;

done:
    if (tls) {
        mbedtls_ssl_close_notify(&c.ssl);
        mbedtls_ssl_free(&c.ssl);
    }
    closesocket(c.fd);
    free(buf);
    return result;
}

void http_response_free(http_response *res)
{
    free(res->body);
    res->body = NULL;
}
