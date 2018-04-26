#include "mod_rainback.h"
#include <string.h>
#include <apr_strings.h>

#define BUFSIZE 4096

struct rainback_EnvironmentResponse {
mod_rainback* rb;
marla_Ring* input;
apr_pool_t* pool;
parsegraph_user_login login;
};

rainback_EnvironmentResponse* rainback_EnvironmentResponse_new(marla_Request* req, mod_rainback* rb)
{
    rainback_EnvironmentResponse* resp = malloc(sizeof(*resp));
    memset(&resp->login, 0, sizeof(resp->login));
    resp->rb = rb;
    if(apr_pool_create(&resp->pool, resp->rb->session->pool) != APR_SUCCESS) {
        marla_killRequest(req, 500, "Failed to create request handler memory pool.");
    }
    resp->input = marla_Ring_new(BUFSIZE);
    return resp;
}

void rainback_EnvironmentResponse_destroy(rainback_EnvironmentResponse* resp)
{
    apr_pool_destroy(resp->pool);
    marla_Ring_free(resp->input);
    free(resp);
}

void rainback_generateEnvironmentPage(rainback_Page* page, mod_rainback* rb)
{
    apr_pool_t* pool;
    if(apr_pool_create(&pool, rb->session->pool) != APR_SUCCESS) {
        marla_die(rb->session->server, "Failed to generate request pool.");
    }

    // Render the response body from the template.
    rainback_Context* context = rainback_Context_new(pool);
    rainback_renderTemplate(rb, "environment.html", context, page);

    char buf[8192];
    int len = snprintf(buf, sizeof buf,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %d\r\n"
        "\r\n",
        page->length
    );
    rainback_Page_prepend(page, buf, len);
    page->headBoundary = len;
    apr_pool_destroy(pool);
}

static int acceptRequest(marla_Request* req)
{
    rainback_EnvironmentResponse* resp = req->handlerData;
    if(!strcmp(req->method, "GET") || !strcmp(req->method, "HEAD")) {
        req->handler = rainback_pageHandler;
        req->handlerData = rainback_getPage(resp->rb, "", req->uri, &resp->login);
        rainback_EnvironmentResponse_destroy(resp);
        return 1;
    }

    return 0;
}

void rainback_environmentHandler(struct marla_Request* req, enum marla_ClientEvent ev, void* data, int dataLen)
{
    rainback_EnvironmentResponse* resp = req->handlerData;

    marla_WriteEvent* we;
    switch(ev) {
    case marla_EVENT_HEADER:
        if(strcmp("Cookie", data)) {
            break;
        }
        rainback_authenticateByCookie(req, resp->rb, &resp->login, data + dataLen);
        break;
    case marla_EVENT_ACCEPTING_REQUEST:
        *((int*)data) = acceptRequest(req);
        break;
    case marla_EVENT_REQUEST_BODY:
        we = data;
        marla_killRequest(req, 500, "Environment handler must not process request body.");
        break;
    case marla_EVENT_MUST_WRITE:
        marla_killRequest(req, 500, "Environment handler must not process write events.");
        break;
    case marla_EVENT_DESTROYING:
        req->handlerData = 0;
        rainback_EnvironmentResponse_destroy(resp);
        break;
    }
}

