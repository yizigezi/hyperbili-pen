#ifndef HYPERBILI_HTTP_CLIENT_H
#define HYPERBILI_HTTP_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HB_HTTP_MAX_HEADERS 32
#define HB_HTTP_MAX_BODY (8u * 1024u * 1024u)

typedef struct {
    const char *url;
    const char *method;
    const char *const *headers; /* each item: "Name: value" */
    size_t header_count;
    const void *body;
    size_t body_len;
    long timeout_ms; /* 1..30000; 0 uses 15000 */
} hb_http_request;

typedef struct {
    long status;
    char *headers; /* raw response header lines; repeated Set-Cookie lines are preserved */
    size_t headers_len;
    unsigned char *body;
    size_t body_len;
    char error[192];
} hb_http_response;

/* Start a bounded two-worker queue. Safe to call once per native runtime. */
int hb_http_init(void);
/* Copies all request data. Returns 0 and writes an id, or a negative error. */
int hb_http_submit(const hb_http_request *request, uint64_t *out_id);
/* 0=pending, 1=completed (response ownership transfers to caller), negative=unknown/error. */
int hb_http_poll(uint64_t id, hb_http_response **out_response);
void hb_http_response_free(hb_http_response *response);
/* Stop accepting work, drain queued work, join workers, and release queue state. */
void hb_http_shutdown(void);

#ifdef __cplusplus
}
#endif
#endif
