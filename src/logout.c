#include "mod_rainback.h"
#include <string.h>
#include <ctype.h>
#include <apr_escape.h>

#define BUFSIZE 4096

struct rainback_LogoutResponse {
mod_rainback* rb;
marla_Ring* input;
apr_pool_t* pool;
parsegraph_user_login login;
};

rainback_LogoutResponse* rainback_LogoutResponse_new(marla_Request* req, mod_rainback* rb)
{
    rainback_LogoutResponse* resp = malloc(sizeof(*resp));
    memset(&resp->login, 0, sizeof(resp->login));
    resp->rb = rb;
    if(apr_pool_create(&resp->pool, resp->rb->session->pool) != APR_SUCCESS) {
        marla_killRequest(req, 500, "Failed to create request handler memory pool.");
    }
    resp->input = marla_Ring_new(BUFSIZE);
    return resp;
}

void rainback_LogoutResponse_destroy(rainback_LogoutResponse* resp)
{
    apr_pool_destroy(resp->pool);
    marla_Ring_free(resp->input);
    free(resp);
}

void rainback_generateLogoutPage(rainback_Page* page, mod_rainback* rb, const char* pageState, parsegraph_user_login* login)
{
    apr_pool_t* pool;
    if(apr_pool_create(&pool, rb->session->pool) != APR_SUCCESS) {
        marla_die(rb->session->server, "Failed to generate request pool.");
    }

    rainback_Context* context = rainback_Context_new(pool);
    char encodedUsername[parsegraph_USERNAME_MAX_LENGTH * 3 + 1];
    if(login && login->username) {
        memset(encodedUsername, 0, sizeof(encodedUsername));
        apr_escape_entity(encodedUsername, login->username, APR_ESCAPE_STRING, 1, 0);
        rainback_Context_setString(context, "username", encodedUsername);
    }
    rainback_renderTemplate(rb, "logout.html", context, page);

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

void rainback_generateNotLoggedInPage(rainback_Page* page, mod_rainback* rb)
{
    apr_pool_t* pool;
    if(apr_pool_create(&pool, rb->session->pool) != APR_SUCCESS) {
        marla_die(rb->session->server, "Failed to generate request pool.");
    }

    rainback_Context* context = rainback_Context_new(pool);
    rainback_renderTemplate(rb, "not_logged_in.html", context, page);

    char buf[8192];
    int len = snprintf(buf, sizeof buf,
        "HTTP/1.1 303 See other\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %d\r\n"
        "Location: /\r\n"
        "\r\n",
        page->length
    );
    rainback_Page_prepend(page, buf, len);
    page->headBoundary = len;
    apr_pool_destroy(pool);
}

void rainback_generateLogoutSucceededPage(rainback_Page* page, mod_rainback* rb)
{
    apr_pool_t* pool;
    if(apr_pool_create(&pool, rb->session->pool) != APR_SUCCESS) {
        marla_die(rb->session->server, "Failed to generate request pool.");
    }

    rainback_Context* context = rainback_Context_new(pool);
    rainback_renderTemplate(rb, "logout_succeeded.html", context, page);

    char buf[8192];
    int len = snprintf(buf, sizeof buf,
        "HTTP/1.1 303 See other\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %d\r\n"
        "Set-Cookie: session=;HttpOnly;%sMax-Age=0;Version=1\r\n"
        "Location: /\r\n"
        "\r\n",
        page->length,
        rb->session->server->using_ssl ? "Secure;" : ""
    );
    rainback_Page_prepend(page, buf, len);
    page->headBoundary = len;
    apr_pool_destroy(pool);
}

void rainback_generateLogoutFailedPage(rainback_Page* page, mod_rainback* rb)
{
    apr_pool_t* pool;
    if(apr_pool_create(&pool, rb->session->pool) != APR_SUCCESS) {
        marla_die(rb->session->server, "Failed to generate request pool.");
    }

    rainback_Context* context = rainback_Context_new(pool);
    rainback_renderTemplate(rb, "logout_failed.html", context, page);

    char buf[8192];
    int len = snprintf(buf, sizeof buf,
        "HTTP/1.1 500 Server error\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %d\r\n"
        "\r\n",
        page->length
    );
    rainback_Page_prepend(page, buf, len);
    page->headBoundary = len;
    apr_pool_destroy(pool);
}

static marla_WriteResult readRequestBody(marla_Request* req, marla_WriteEvent* we)
{
    rainback_LogoutResponse* resp = req->handlerData;
    if(we->length == 0) {
        if(!strcmp(req->method, "POST")) {
            rainback_Page* page = rainback_Page_new("");
            int logins_ended = 0;
            switch(parsegraph_endUserLogin(resp->rb->session, resp->login.username,
                &logins_ended)) {
            case parsegraph_OK:
                rainback_generateLogoutSucceededPage(page, resp->rb);
                break;
            default:
                rainback_generateLogoutFailedPage(page, resp->rb);
                break;
            }
            rainback_LogoutResponse_destroy(resp);
            req->handler = rainback_pageHandler;
            req->handlerData = page;
        }
        req->readStage = marla_CLIENT_REQUEST_DONE_READING;
        return marla_WriteResult_CONTINUE;
    }
    marla_killRequest(req, 400, "Unexpected input given in %s request.", req->method);
    return marla_WriteResult_KILLED;
}

static int acceptRequest(marla_Request* req)
{
    rainback_LogoutResponse* resp = req->handlerData;
    if(!strcmp(req->method, "GET") || !strcmp(req->method, "HEAD")) {
        if(!resp->login.username) {
            rainback_Page* page = rainback_Page_new(0);
            rainback_generateNotLoggedInPage(page, resp->rb);
            req->handler = rainback_pageHandler;
            req->handlerData = page;
            rainback_LogoutResponse_destroy(resp);
            return 1;
        }

        req->handler = rainback_pageHandler;
        req->handlerData = rainback_getPage(resp->rb, "", req->uri, &resp->login);
        rainback_LogoutResponse_destroy(resp);
        return 1;
    }
    if(!strcmp(req->method, "POST")) {
        return 1;
    }

    return 0;
}

void rainback_logoutHandler(struct marla_Request* req, enum marla_ClientEvent ev, void* data, int dataLen)
{
    rainback_LogoutResponse* resp = req->handlerData;

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
        we->status = readRequestBody(req, we);
        break;
    case marla_EVENT_MUST_WRITE:
        marla_killRequest(req, 500, "LogoutHandler must not process write events.");
        break;
    case marla_EVENT_DESTROYING:
        req->handlerData = 0;
        rainback_LogoutResponse_destroy(resp);
        break;
    }
}
