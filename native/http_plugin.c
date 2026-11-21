#include "quickjs.h"
#include "http_client.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef JSModuleDef* (*LoadCModuleFunction)(JSContext*, const char*);
extern void registerCModuleLoader(const char*, LoadCModuleFunction);

static JSValue js_submit(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *url = NULL, *method = "GET", *body = NULL;
    size_t body_len = 0, header_count = 0, i;
    const char *headers[HB_HTTP_MAX_HEADERS];
    char **owned_headers = NULL;
    hb_http_request req;
    uint64_t id = 0;
    int rc;
    (void)this_val;
    if (argc < 1 || JS_IsString(argv[0]) == 0)
        return JS_ThrowTypeError(ctx, "hyperHttp.submit(url, method?, headers?, body?, timeoutMs?)");
    url = JS_ToCString(ctx, argv[0]);
    if (!url) return JS_EXCEPTION;
    if (argc > 1 && JS_IsString(argv[1])) {
        method = JS_ToCString(ctx, argv[1]);
        if (!method) { JS_FreeCString(ctx, url); return JS_EXCEPTION; }
    }
    if (argc > 2 && JS_IsArray(ctx, argv[2])) {
        JSValue lenv = JS_GetPropertyStr(ctx, argv[2], "length");
        uint32_t n = 0;
        JS_ToUint32(ctx, &n, lenv);
        JS_FreeValue(ctx, lenv);
        if (n > HB_HTTP_MAX_HEADERS) {
            JS_FreeCString(ctx, url);
            if (method != (const char*)"GET") JS_FreeCString(ctx, method);
            return JS_ThrowRangeError(ctx, "too many HTTP headers");
        }
        header_count = n;
        owned_headers = (char**)calloc(header_count ? header_count : 1, sizeof(char*));
        if (!owned_headers) {
            JS_FreeCString(ctx, url);
            if (method != (const char*)"GET") JS_FreeCString(ctx, method);
            return JS_ThrowOutOfMemory(ctx);
        }
        for (i = 0; i < header_count; ++i) {
            JSValue item = JS_GetPropertyUint32(ctx, argv[2], (uint32_t)i);
            owned_headers[i] = (char*)JS_ToCString(ctx, item);
            JS_FreeValue(ctx, item);
            if (!owned_headers[i]) break;
            headers[i] = owned_headers[i];
        }
        if (i != header_count) {
            while (i > 0) JS_FreeCString(ctx, owned_headers[--i]);
            free(owned_headers);
            JS_FreeCString(ctx, url);
            if (method != (const char*)"GET") JS_FreeCString(ctx, method);
            return JS_EXCEPTION;
        }
    }
    if (argc > 3 && !JS_IsNull(argv[3]) && !JS_IsUndefined(argv[3])) {
        body = JS_ToCStringLen(ctx, &body_len, argv[3]);
        if (!body) {
            for (i = 0; i < header_count; ++i) JS_FreeCString(ctx, owned_headers[i]);
            free(owned_headers); JS_FreeCString(ctx, url);
            if (method != (const char*)"GET") JS_FreeCString(ctx, method);
            return JS_EXCEPTION;
        }
    }
    memset(&req, 0, sizeof(req));
    req.url = url; req.method = method;
    req.headers = header_count ? headers : NULL; req.header_count = header_count;
    req.body = body; req.body_len = body_len; req.timeout_ms = 15000;
    if (argc > 4) {
        int64_t timeout = 0;
        if (JS_ToInt64(ctx, &timeout, argv[4]) == 0 && timeout > 0) req.timeout_ms = (long)timeout;
    }
    rc = hb_http_submit(&req, &id);
    if (body) JS_FreeCString(ctx, body);
    for (i = 0; i < header_count; ++i) JS_FreeCString(ctx, owned_headers[i]);
    free(owned_headers);
    JS_FreeCString(ctx, url);
    if (method != (const char*)"GET") JS_FreeCString(ctx, method);
    if (rc != 0) return JS_ThrowInternalError(ctx, "native HTTP submit failed (%d)", rc);
    return JS_NewInt64(ctx, (int64_t)id);
}

static JSValue js_poll(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    int64_t raw_id = 0;
    hb_http_response *response = NULL;
    int rc;
    JSValue obj;
    (void)this_val;
    if (argc < 1 || JS_ToInt64(ctx, &raw_id, argv[0]) < 0 || raw_id <= 0)
        return JS_ThrowTypeError(ctx, "hyperHttp.poll(id)");
    rc = hb_http_poll((uint64_t)raw_id, &response);
    if (rc == 0) return JS_NULL;
    if (rc < 0) return JS_ThrowInternalError(ctx, "native HTTP job not found (%d)", rc);
    if (!response) return JS_ThrowInternalError(ctx, "native HTTP response allocation failed");
    obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "status", JS_NewInt32(ctx, (int32_t)response->status));
    JS_SetPropertyStr(ctx, obj, "headersRaw",
        JS_NewStringLen(ctx, response->headers ? response->headers : "", response->headers_len));
    JS_SetPropertyStr(ctx, obj, "bodyText",
        JS_NewStringLen(ctx, response->body ? (const char*)response->body : "", response->body_len));
    JS_SetPropertyStr(ctx, obj, "error", JS_NewString(ctx, response->error));
    hb_http_response_free(response);
    return obj;
}

static JSValue js_shutdown(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)ctx; (void)this_val; (void)argc; (void)argv;
    hb_http_shutdown();
    return JS_UNDEFINED;
}

static int hyper_http_module_init(JSContext *ctx, JSModuleDef *m) {
    JSValue api = JS_NewObject(ctx);
    if (JS_IsException(api)) return -1;
    JS_SetPropertyStr(ctx, api, "submit", JS_NewCFunction(ctx, js_submit, "submit", 5));
    JS_SetPropertyStr(ctx, api, "poll", JS_NewCFunction(ctx, js_poll, "poll", 1));
    JS_SetPropertyStr(ctx, api, "shutdown", JS_NewCFunction(ctx, js_shutdown, "shutdown", 0));
    JS_SetModuleExport(ctx, m, "default", api);
    return 0;
}

static JSModuleDef *hyper_http_module_load(JSContext *ctx, const char *name) {
    JSModuleDef *m = JS_NewCModule(ctx, name, hyper_http_module_init);
    if (!m) return NULL;
    if (JS_AddModuleExport(ctx, m, "default") < 0) return NULL;
    return m;
}

__attribute__((visibility("default")))
void custom_init_jsapis(void) {
    registerCModuleLoader("hyperHttp", hyper_http_module_load);
}


/* The host unloads libjsapi_*.so with the native runtime. Never leave detached
 * workers behind; all workers are joined before this library is unloaded. */
__attribute__((destructor))
static void hyper_http_library_unload(void) {
    hb_http_shutdown();
}
