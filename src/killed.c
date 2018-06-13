#include "mod_rainback.h"
#include <string.h>
#include <apr_escape.h>

static void rainback_generateKilledPage(rainback_Page* page, mod_rainback* rb, int statusCode, const char* reason)
{
    char encodedReason[1024 * 3 + 1];
    memset(encodedReason, 0, sizeof(encodedReason));
    apr_escape_entity(encodedReason, reason, APR_ESCAPE_STRING, 1, 0);

    char encodedStatusCode[1024 * 3 + 1];
    memset(encodedStatusCode, 0, sizeof(encodedStatusCode));
    apr_escape_entity(encodedStatusCode, marla_getDefaultStatusLine(statusCode), APR_ESCAPE_STRING, 1, 0);

    apr_pool_t* pool;
    if(apr_pool_create(&pool, rb->session->pool) != APR_SUCCESS) {
        marla_die(rb->session->server, "Failed to generate request pool.");
    }
    rainback_Context* context = rainback_Context_new(pool);
    rainback_Context_setString(context, "status_code", encodedStatusCode);
    rainback_Context_setString(context, "reason", encodedReason);
    rainback_renderTemplate(rb, "killed.html", context, page);

    char buf[8192];
    int len = snprintf(buf, sizeof buf,
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %d\r\n"
        "\r\n",
        statusCode, marla_getDefaultStatusLine(statusCode),
        page->length
    );
    rainback_Page_prepend(page, buf, len);
    page->headBoundary = len;
    apr_pool_destroy(pool);
}

rainback_Page* rainback_getKilledPage(mod_rainback* rb, int statusCode, const char* reason)
{
    // Generate the cache key.
    char buf[1024];
    memset(buf, 0, sizeof buf);
    int len = snprintf(buf, sizeof buf, "killed$%d$%s$", statusCode, reason);
    char* cacheKey = buf;

    rainback_Page* page = rainback_getPageByKey(rb, cacheKey);
    if(!page) {
        page = rainback_Page_new(cacheKey);
        rainback_generateKilledPage(page, rb, statusCode, reason);
        struct timespec now;
        if(0 != clock_gettime(CLOCK_MONOTONIC, &now)) {
            fprintf(stderr, "Failed to retrieve current server time.\n");
            abort();
        }
        page->expiry.tv_sec = now.tv_sec + 60;
        page->expiry.tv_nsec = now.tv_nsec;
        apr_hash_set(rb->cache, page->cacheKey, APR_HASH_KEY_STRING, page);
    }
    return page;
}
