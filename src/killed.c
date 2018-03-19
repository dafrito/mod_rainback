#include "mod_rainback.h"
#include <string.h>
#include <apr_escape.h>

static void rainback_generateKilledPage(rainback_Page* page, mod_rainback* rb, const char* reason, const char* url)
{
    char encodedReason[1024 * 3 + 1];
    memset(encodedReason, 0, sizeof(encodedReason));
    apr_escape_entity(encodedReason, reason, APR_ESCAPE_STRING, 1, 0);

    char body[8192];
    int len = snprintf(body, sizeof body,
"<!DOCTYPE html>"
"<html>"
"<head>"
    "<title>Server error</title>"
    "<link rel=\"stylesheet\" type=\"text/css\" href=\"rainback.css\">"
    "<link rel=\"icon\" type=\"image/png\" href=\"favicon.png\" sizes=\"16x16\">"
"</head>"
"<body>"
"<nav>"
    "<p style=\"text-align: center\">"
    "<a href=/><img id=logo src=\"nav-side-logo.png\"></img></a>"
    "</p>"
    "<div style=\"text-align: center\">"
    "</div>"
    "<p style=\"text-align: center\">"
    "</p>"
"</nav>"
"<main>"
    "<div class=links>"
        "<form id=search action=\"/search\">"
        "<input name=q></input> <input type=submit value=Search></input>"
        "</form>"
    "</div>"
    "<div class=block style=\"clear:both\">"
        "<h1>Rainback Server Error</h1>"
        "<p>%s</p>"
        "<p>Return to the <a href=\"/\">homepage.</a></p>"
        "</div>"
"</main>"
"<div style=\"clear: both\"></div>"
"<div style=\"display: block; text-align: center; margin: 1em 0\">"
    "<div class=slot style=\"display: inline-block;\">"
        "&copy; 2018 <a href='https://rainback.com'>Rainback, Inc.</a> All rights reserved. <a href=/contact><span class=\"bud\">Contact Us</span></a>"
    "</div>"
"</div>"
"</body>"
"</html>",
    encodedReason
    );
    size_t bodylen = strlen(body);

    char buf[8192];
    len = snprintf(buf, sizeof buf,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %d\r\n"
        "\r\n",
        bodylen
    );
    rainback_Page_write(page, buf, len);
    rainback_Page_endHead(page);
    rainback_Page_write(page, body, bodylen);
}

rainback_Page* rainback_getKilledPage(mod_rainback* rb, const char* reason, const char* url)
{
    char buf[1024];
    memset(buf, 0, sizeof buf);

    char* cacheKey = buf;
    cacheKey = strcat(cacheKey, "killed$");
    cacheKey = strcat(cacheKey, "$");
    cacheKey = strcat(cacheKey, url);
    cacheKey = strcat(cacheKey, "$");

    rainback_Page* page = rainback_getPageByKey(rb, cacheKey);
    if(!page) {
        page = rainback_Page_new(cacheKey);
        rainback_generateKilledPage(page, rb, reason, url);
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

