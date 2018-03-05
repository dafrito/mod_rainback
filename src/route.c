#include "mod_rainback.h"

static int handleSearch(marla_Request* req)
{
    if(strncmp(req->uri, "/search", strlen("/search"))) {
        return 1;
    }

    char searchquery[1024];
    memset(searchquery, 0, sizeof searchquery);
    if(req->uri[strlen("/search")] != '?') {
        if(req->uri[strlen("/search")] != 0) {
            return 1;
        }
        // Accessing the search page directly.
        strcpy(req->uri, "/search?q=");
    }
    else {
        if(req->uri[strlen("/search") + 1] != 'q') {
            return 1;
        }
        if(req->uri[strlen("/search") + 2] != '=') {
            return 1;
        }
        for(int i = 0;; ++i) {
            searchquery[i] = req->uri[strlen("/search") + 3 + i];
            if(searchquery[i] == 0) {
                break;
            }
        }
    }

    struct marla_ChunkedPageRequest* cpr;
    cpr = marla_ChunkedPageRequest_new(marla_BUFSIZE, req);
    cpr->handler = makeSearchPage;
    req->handler = marla_chunkedRequestHandler;
    req->handlerData = cpr;
    return 0;
}

void routeHook(struct marla_Request* req, void* hookData)
{
    struct marla_ChunkedPageRequest* cpr;
    if(handleSearch(req) == 0) {
        return;
    }
    if(!strcmp(req->uri, "/contact")) {
        cpr = marla_ChunkedPageRequest_new(marla_BUFSIZE, req);
        cpr->handler = makeContactPage;
        req->handler = marla_chunkedRequestHandler;
        req->handlerData = cpr;
        return;
    }
    if(!strcmp(req->uri, "/profile")) {
        req->handler = rainback_profileHandler;
        return;
    }
    if(!strcmp(req->uri, "/import")) {
        req->handler = rainback_importHandler;
        return;
    }
    if(!strncmp(req->uri, "/user", 5)) {
        // Check for suitable termination
        if(req->uri[5] != 0 && req->uri[5] != '/' && req->uri[5] != '?') {
            // Not really handled.
            return;
        }
        // Install backend handler.
        req->handler = marla_backendClientHandler;
        return;
    }
    int len = strlen("/environment");
    if(!strncmp(req->uri, "/environment", len)) {
        // Check for suitable termination
        if(req->uri[len] != 0 && req->uri[len] != '/' && req->uri[len] != '?' && req->uri[len] != '.' ) {
            // Not really handled.
            return;
        }
        // Install backend handler.
        req->handler = marla_backendClientHandler;
        return;
    }

    const char* bufs[] = {
        "/parsegraph-1.2.js",
        "/parsegraph-widgets-1.2.js",
        "/parsegraph-1.0.js",
        "/parsegraph-widgets-1.0.js",
        "/sga.css",
        "/UnicodeData.txt",
        "/chat.html",
        "/float.html",
        "/woodwork.html",
        "/GLWidget.js",
        "/bible.html",
        "/primes.html",
        "/corporate.html",
        "/alpha.html",
        "/weetcubes.html",
        "/week.html",
        "/calendar.html",
        "/ulam.html",
        "/piers.html",
        "/lisp.html",
        "/anthonylispjs-1.0.js",
        "/chess.html",
        "/ip.html",
        "/todo.html",
        "/start.html",
        "/multislot.html",
        "/esprima.js",
        "/finish.html",
        "/surface.lisp",
        "/terminal.html",
        "/initial.html",
        "/javascript.html",
        "/builder.html",
        "/htmlgraph.html",
        "/audio.html",
        "/favicon.ico",
        "/list-bullet.png",
        "/logo.png",
        0
    };
    for(int i = 0; ; ++i) {
        const char* path = bufs[i];
        if(!path) {
            break;
        }
        if(!strncmp(req->uri, path, strlen(path))) {
            // Install backend handler.
            req->handler = marla_backendClientHandler;
            return;
        }
    }

    // Default handler.
    cpr = marla_ChunkedPageRequest_new(marla_BUFSIZE, req);
    cpr->handler = makeCounterPage;
    req->handler = marla_chunkedRequestHandler;
    req->handlerData = cpr;
}
