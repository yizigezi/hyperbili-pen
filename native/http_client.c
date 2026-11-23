#define _POSIX_C_SOURCE 200809L
#include "http_client.h"

#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WORKER_COUNT 2
#define QUEUE_LIMIT 8
#define URL_LIMIT 8192
#define HEADER_LINE_LIMIT 16384

typedef struct hb_job {
    uint64_t id;
    char *url, *method;
    char **headers;
    size_t header_count;
    unsigned char *body;
    size_t body_len;
    long timeout_ms;
    hb_http_response *response;
    int done;
    struct hb_job *jobs_next;
    struct hb_job *queue_next;
    struct curl_slist *curl_headers;
} hb_job;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_ready = PTHREAD_COND_INITIALIZER;
static pthread_t g_workers[WORKER_COUNT];
static hb_job *g_jobs, *g_queue_head, *g_queue_tail;
static size_t g_queue_size, g_outstanding;
static uint64_t g_next_id = 1;
static int g_initialized, g_stopping;

static void free_job(hb_job *j) {
    size_t i;
    if (!j) return;
    free(j->url);
    free(j->method);
    for (i = 0; i < j->header_count; ++i) free(j->headers[i]);
    free(j->headers);
    if (j->body) { memset(j->body, 0, j->body_len); free(j->body); }
    curl_slist_free_all(j->curl_headers);
    hb_http_response_free(j->response);
    free(j);
}

void hb_http_response_free(hb_http_response *r) {
    if (!r) return;
    free(r->headers);
    if (r->body) { memset(r->body, 0, r->body_len); free(r->body); }
    free(r);
}

static int append_bytes(char **buf, size_t *len, size_t *cap, const void *data, size_t n, size_t limit) {
    char *next;
    size_t wanted;
    if (n > limit || *len > limit - n) return 0;
    wanted = *len + n;
    if (wanted + 1 > *cap) {
        size_t new_cap = *cap ? *cap : 1024;
        while (new_cap < wanted + 1) {
            if (new_cap > limit / 2) { new_cap = limit + 1; break; }
            new_cap *= 2;
        }
        next = (char *)realloc(*buf, new_cap);
        if (!next) return 0;
        *buf = next; *cap = new_cap;
    }
    memcpy(*buf + *len, data, n);
    *len += n;
    (*buf)[*len] = '\0';
    return 1;
}

typedef struct { char *data; size_t len, cap; int failed; } hb_buffer;

static size_t on_header(char *ptr, size_t size, size_t nmemb, void *userdata) {
    hb_buffer *b = (hb_buffer *)userdata;
    size_t n = size * nmemb;
    if (!append_bytes(&b->data, &b->len, &b->cap, ptr, n, 256u * 1024u)) {
        b->failed = 1; return 0;
    }
    return n;
}

static size_t on_body(char *ptr, size_t size, size_t nmemb, void *userdata) {
    hb_buffer *b = (hb_buffer *)userdata;
    size_t n = size * nmemb;
    if (!append_bytes(&b->data, &b->len, &b->cap, ptr, n, HB_HTTP_MAX_BODY)) {
        b->failed = 1; return 0;
    }
    return n;
}

static const char *find_ca_bundle(void) {
    static const char *const paths[] = {
        "/etc/ssl/certs/ca-certificates.crt",
        "/etc/ssl/cert.pem",
        "/etc/pki/tls/certs/ca-bundle.crt",
        "/etc/ssl/ca-bundle.pem",
        "/usr/share/ca-certificates/ca-certificates.crt",
        NULL
    };
    size_t i;
    for (i = 0; paths[i]; ++i) {
        if (access(paths[i], R_OK) == 0) return paths[i];
    }
    return NULL;
}

static const char *find_ca_directory(void) {
    static const char *const paths[] = {
        "/etc/ssl/certs",
        "/etc/pki/tls/certs",
        NULL
    };
    size_t i;
    for (i = 0; paths[i]; ++i) {
        if (access(paths[i], R_OK | X_OK) == 0) return paths[i];
    }
    return NULL;
}

static void perform_job(hb_job *j) {
    CURL *curl = curl_easy_init();
    CURLcode code;
    hb_buffer headers = {0}, body = {0};
    long status = 0, ssl_verify_result = 0, os_errno = 0;
    char error_buffer[CURL_ERROR_SIZE] = {0};
    const char *ca_bundle = find_ca_bundle();
    const char *ca_directory = find_ca_directory();
    size_t i;
    j->response = (hb_http_response *)calloc(1, sizeof(*j->response));
    if (!j->response) return;
    if (!curl) { snprintf(j->response->error, sizeof(j->response->error), "curl_easy_init failed"); return; }

    for (i = 0; i < j->header_count; ++i) {
        struct curl_slist *next = curl_slist_append(j->curl_headers, j->headers[i]);
        if (!next) {
            snprintf(j->response->error, sizeof(j->response->error), "header allocation failed");
            curl_easy_cleanup(curl); return;
        }
        j->curl_headers = next;
    }
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);
    curl_easy_setopt(curl, CURLOPT_URL, j->url);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, j->method);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, j->curl_headers);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, on_header);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_body);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, j->timeout_ms);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, j->timeout_ms);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    /* Prefer a readable firmware CA store; otherwise retain libcurl's compiled defaults. */
    if (ca_bundle) curl_easy_setopt(curl, CURLOPT_CAINFO, ca_bundle);
    if (ca_directory) curl_easy_setopt(curl, CURLOPT_CAPATH, ca_directory);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "HyperBilibili-Pen/0.1");
    if (j->body && j->body_len) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, j->body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)j->body_len);
    }
    code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_getinfo(curl, CURLINFO_SSL_VERIFYRESULT, &ssl_verify_result);
    curl_easy_getinfo(curl, CURLINFO_OS_ERRNO, &os_errno);
    j->response->status = status;
    j->response->headers = headers.data;
    j->response->headers_len = headers.len;
    j->response->body = (unsigned char *)body.data;
    j->response->body_len = body.len;
    headers.data = NULL; body.data = NULL;
    if (code != CURLE_OK) {
        const char *detail = error_buffer[0] ? error_buffer : curl_easy_strerror(code);
        snprintf(j->response->error, sizeof(j->response->error),
            "%s (curl=%d, ssl_verify=%ld, os_errno=%ld, ca=%s, capath=%s)",
            detail, (int)code, ssl_verify_result, os_errno,
            ca_bundle ? ca_bundle : "default",
            ca_directory ? ca_directory : "default");
    }
    free(headers.data);
    free(body.data);
    curl_easy_cleanup(curl);
}

static void *worker_main(void *unused) {
    (void)unused;
    for (;;) {
        hb_job *j;
        pthread_mutex_lock(&g_lock);
        while (!g_queue_head && !g_stopping) pthread_cond_wait(&g_ready, &g_lock);
        if (!g_queue_head && g_stopping) { pthread_mutex_unlock(&g_lock); break; }
        j = g_queue_head;
        g_queue_head = j->queue_next;
        if (!g_queue_head) g_queue_tail = NULL;
        j->queue_next = NULL;
        if (g_queue_size) --g_queue_size;
        pthread_mutex_unlock(&g_lock);

        perform_job(j);

        pthread_mutex_lock(&g_lock);
        j->done = 1;
        pthread_mutex_unlock(&g_lock);
    }
    return NULL;
}

int hb_http_init(void) {
    int i;
    pthread_mutex_lock(&g_lock);
    if (g_initialized) { pthread_mutex_unlock(&g_lock); return 0; }
    if (g_stopping) { pthread_mutex_unlock(&g_lock); return -1; }
    g_stopping = 0;
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        pthread_mutex_unlock(&g_lock); return -1;
    }
    for (i = 0; i < WORKER_COUNT; ++i) {
        if (pthread_create(&g_workers[i], NULL, worker_main, NULL) != 0) {
            g_stopping = 1; pthread_cond_broadcast(&g_ready);
            pthread_mutex_unlock(&g_lock);
            while (i-- > 0) pthread_join(g_workers[i], NULL);
            curl_global_cleanup();
            pthread_mutex_lock(&g_lock); g_stopping = 0;
            pthread_mutex_unlock(&g_lock); return -2;
        }
    }
    g_initialized = 1;
    pthread_mutex_unlock(&g_lock);
    return 0;
}

int hb_http_submit(const hb_http_request *r, uint64_t *out_id) {
    hb_job *j;
    size_t i;
    if (!r || !out_id || !r->url || !*r->url || strlen(r->url) > URL_LIMIT ||
        (r->header_count && !r->headers) || r->header_count > HB_HTTP_MAX_HEADERS ||
        (r->body_len && !r->body) || r->body_len > HB_HTTP_MAX_BODY) return -1;
    if (hb_http_init() != 0) return -2;
    j = (hb_job *)calloc(1, sizeof(*j));
    if (!j) return -3;
    j->url = strdup(r->url);
    j->method = strdup(r->method && *r->method ? r->method : "GET");
    j->timeout_ms = r->timeout_ms > 0 && r->timeout_ms <= 30000 ? r->timeout_ms : 15000;
    j->header_count = r->header_count;
    if (j->header_count) j->headers = (char **)calloc(j->header_count, sizeof(char *));
    if (r->body_len) {
        j->body = (unsigned char *)malloc(r->body_len);
        if (j->body) { memcpy(j->body, r->body, r->body_len); j->body_len = r->body_len; }
    }
    if (!j->url || !j->method || (j->header_count && !j->headers) || (r->body_len && !j->body)) {
        free_job(j); return -3;
    }
    for (i = 0; i < j->header_count; ++i) {
        if (!r->headers[i] || strlen(r->headers[i]) > HEADER_LINE_LIMIT ||
            !(j->headers[i] = strdup(r->headers[i]))) { free_job(j); return -1; }
    }

    pthread_mutex_lock(&g_lock);
    if (g_stopping || g_outstanding >= QUEUE_LIMIT) {
        pthread_mutex_unlock(&g_lock); free_job(j); return -4;
    }
    j->id = g_next_id++;
    if (!g_next_id) g_next_id = 1;
    j->jobs_next = g_jobs; g_jobs = j;
    if (g_queue_tail) g_queue_tail->queue_next = j; else g_queue_head = j;
    g_queue_tail = j; ++g_queue_size; ++g_outstanding;
    *out_id = j->id;
    pthread_cond_signal(&g_ready);
    pthread_mutex_unlock(&g_lock);
    return 0;
}

int hb_http_poll(uint64_t id, hb_http_response **out) {
    hb_job **link, *j;
    if (!out) return -1;
    *out = NULL;
    pthread_mutex_lock(&g_lock);
    for (link = &g_jobs; *link && (*link)->id != id; link = &(*link)->jobs_next) {}
    j = *link;
    if (!j) { pthread_mutex_unlock(&g_lock); return -2; }
    if (!j->done) { pthread_mutex_unlock(&g_lock); return 0; }
    *link = j->jobs_next;
    if (g_outstanding) --g_outstanding;
    *out = j->response;
    j->response = NULL;
    pthread_mutex_unlock(&g_lock);
    free_job(j);
    return 1;
}

void hb_http_shutdown(void) {
    hb_job *j, *next;
    int i;
    pthread_mutex_lock(&g_lock);
    if (!g_initialized || g_stopping) { pthread_mutex_unlock(&g_lock); return; }
    g_stopping = 1;
    pthread_cond_broadcast(&g_ready);
    pthread_mutex_unlock(&g_lock);
    for (i = 0; i < WORKER_COUNT; ++i) pthread_join(g_workers[i], NULL);
    pthread_mutex_lock(&g_lock);
    j = g_jobs; g_jobs = NULL; g_queue_head = g_queue_tail = NULL; g_queue_size = 0;
    g_initialized = 0; g_outstanding = 0;
    pthread_mutex_unlock(&g_lock);
    while (j) { next = j->jobs_next; free_job(j); j = next; }
    curl_global_cleanup();
    pthread_mutex_lock(&g_lock);
    g_stopping = 0;
    pthread_mutex_unlock(&g_lock);
}
