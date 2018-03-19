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
    if(apr_pool_create(&resp->pool, resp->rb->pool) != APR_SUCCESS) {
        marla_killRequest(req, "Failed to create request handler memory pool.");
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
    char body[8192];
    int len = snprintf(body, sizeof body,
"<!DOCTYPE html>"
"<html>"
"<head>"
    "<title>Logout from Rainback</title>"
    "<link rel=\"stylesheet\" type=\"text/css\" href=\"rainback.css\">"
    "<link rel=\"icon\" type=\"image/png\" href=\"favicon.png\" sizes=\"16x16\">"
    "<style>"
    "#login {\n"
    "width: 50%;\n"
    "margin: auto;\n"
    "box-sizing: border-box;\n"
    "}\n"
    "\n"
"@media only screen and (max-width: 600px) {\n"
"#login {\n"
"width: 100%;\n"
"box-sizing: border-box;\n"
"}\n"
"}\n"
"</style>"
"</head>"
"<body>"
    "<p style=\"text-align: center\">"
    "<a href=/><img id=logo src=\"nav-side-logo.png\"></img></a>"
    "</p>"
    "<div class=block id=login>"
        "<h1>Logout from Rainback</h1>"
        "<form method=post style='text-align: center'>"
        "<input type=submit value='Log out'/>"
        "</form>"
    "</div>"
"<div style=\"clear: both\"></div>"
"<div style=\"display: block; text-align: center; margin: 1em 0\">"
    "<div class=slot style=\"display: inline-block;\">"
        "&copy; 2018 <a href='https://rainback.com'>Rainback, Inc.</a> All rights reserved.</a>"
    "</div>"
"</div>"
"</body>"
"</html>"
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

void rainback_generateNotLoggedInPage(rainback_Page* page, mod_rainback* rb)
{
    char body[8192];
    int len = snprintf(body, sizeof body,
"<!DOCTYPE html>"
"<html>"
"<head>"
    "<title>Not logged in</title>"
    "<link rel=\"stylesheet\" type=\"text/css\" href=\"rainback.css\">"
    "<link rel=\"icon\" type=\"image/png\" href=\"favicon.png\" sizes=\"16x16\">"
    "<style>"
    "#login {\n"
    "width: 50%;\n"
    "margin: auto;\n"
    "box-sizing: border-box;\n"
    "}\n"
    "\n"
"@media only screen and (max-width: 600px) {\n"
"#login {\n"
"width: 100%;\n"
"box-sizing: border-box;\n"
"}\n"
"}\n"
"</style>"
"</head>"
"<body>"
    "<p style=\"text-align: center\">"
    "<a href=/><img id=logo src=\"nav-side-logo.png\"></img></a>"
    "</p>"
    "<div class=block id=login>"
        "<h1>Not logged in</h1>"
        "<p>You are not logged in.</p>"
    "</div>"
"<div style=\"clear: both\"></div>"
"<div style=\"display: block; text-align: center; margin: 1em 0\">"
    "<div class=slot style=\"display: inline-block;\">"
        "&copy; 2018 <a href='https://rainback.com'>Rainback, Inc.</a> All rights reserved.</a>"
    "</div>"
"</div>"
"</body>"
"</html>"
    );
    size_t bodylen = strlen(body);

    char buf[8192];
    len = snprintf(buf, sizeof buf,
        "HTTP/1.1 303 See other\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %d\r\n"
        "Location: /\r\n"
        "\r\n",
        bodylen,
        rb->server->using_ssl ? "Secure;" : ""
    );
    rainback_Page_write(page, buf, len);
    rainback_Page_endHead(page);
    rainback_Page_write(page, body, bodylen);
}

void rainback_generateLogoutSucceededPage(rainback_Page* page, mod_rainback* rb)
{
    char body[8192];
    int len = snprintf(body, sizeof body,
"<!DOCTYPE html>"
"<html>"
"<head>"
    "<title>Logged out</title>"
    "<link rel=\"stylesheet\" type=\"text/css\" href=\"rainback.css\">"
    "<link rel=\"icon\" type=\"image/png\" href=\"favicon.png\" sizes=\"16x16\">"
    "<style>"
    "#login {\n"
    "width: 50%;\n"
    "margin: auto;\n"
    "box-sizing: border-box;\n"
    "}\n"
    "\n"
"@media only screen and (max-width: 600px) {\n"
"#login {\n"
"width: 100%;\n"
"box-sizing: border-box;\n"
"}\n"
"}\n"
"</style>"
"</head>"
"<body>"
    "<p style=\"text-align: center\">"
    "<a href=/><img id=logo src=\"nav-side-logo.png\"></img></a>"
    "</p>"
    "<div class=block id=login>"
        "<h1>Logged out</h1>"
        "<p>You have been logged out and are being redirected to the homepage.</p>"
    "</div>"
"<div style=\"clear: both\"></div>"
"<div style=\"display: block; text-align: center; margin: 1em 0\">"
    "<div class=slot style=\"display: inline-block;\">"
        "&copy; 2018 <a href='https://rainback.com'>Rainback, Inc.</a> All rights reserved.</a>"
    "</div>"
"</div>"
"</body>"
"</html>"
    );
    size_t bodylen = strlen(body);

    char buf[8192];
    len = snprintf(buf, sizeof buf,
        "HTTP/1.1 303 See other\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %d\r\n"
        "Set-Cookie: session=;HttpOnly;%sMax-Age=0;Version=1\r\n"
        "Location: /\r\n"
        "\r\n",
        bodylen,
        rb->server->using_ssl ? "Secure;" : ""
    );
    rainback_Page_write(page, buf, len);
    rainback_Page_endHead(page);
    rainback_Page_write(page, body, bodylen);
}

void rainback_generateLogoutFailedPage(rainback_Page* page, mod_rainback* rb)
{
    char body[8192];
    int len = snprintf(body, sizeof body,
"<!DOCTYPE html>"
"<html>"
"<head>"
    "<title>Logout failed</title>"
    "<link rel=\"stylesheet\" type=\"text/css\" href=\"rainback.css\">"
    "<link rel=\"icon\" type=\"image/png\" href=\"favicon.png\" sizes=\"16x16\">"
    "<style>"
    "#login {\n"
    "width: 50%;\n"
    "margin: auto;\n"
    "box-sizing: border-box;\n"
    "}\n"
    "\n"
"@media only screen and (max-width: 600px) {\n"
"#login {\n"
"width: 100%;\n"
"box-sizing: border-box;\n"
"}\n"
"}\n"
"</style>"
"</head>"
"<body>"
    "<p style=\"text-align: center\">"
    "<a href=/><img id=logo src=\"nav-side-logo.png\"></img></a>"
    "</p>"
    "<div class=block id=login>"
        "<h1>Logout failed</h1>"
        "<p>The server failed to log you out.</p>"
        "<form method=post style='text-align: center'>"
        "<input type=submit value=\"Log out\"><br/>"
        "</form>"
    "</div>"
"<div style=\"clear: both\"></div>"
"<div style=\"display: block; text-align: center; margin: 1em 0\">"
    "<div class=slot style=\"display: inline-block;\">"
        "&copy; 2018 <a href='https://rainback.com'>Rainback, Inc.</a> All rights reserved.</a>"
    "</div>"
"</div>"
"</body>"
"</html>"
    );
    size_t bodylen = strlen(body);

    char buf[8192];
    len = snprintf(buf, sizeof buf,
        "HTTP/1.1 500 Server error\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %d\r\n"
        "\r\n",
        bodylen
    );
    rainback_Page_write(page, buf, len);
    rainback_Page_endHead(page);
    rainback_Page_write(page, body, bodylen);
}

static marla_WriteResult readRequestBody(marla_Request* req, marla_WriteEvent* we)
{
    rainback_LogoutResponse* resp = req->handlerData;
    if(we->length == 0) {
        if(!strcmp(req->method, "POST")) {
            rainback_Page* page = rainback_Page_new("");
            int logins_ended = 0;
            switch(parsegraph_endUserLogin(resp->rb->pool, resp->rb->dbd, resp->login.username,
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
    marla_killRequest(req, "Unexpected input given in %s request.", req->method);
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
        rainback_authenticateByCookie(req, resp->pool, resp->rb->dbd, &resp->login, data + dataLen);
        break;
    case marla_EVENT_ACCEPTING_REQUEST:
        *((int*)data) = acceptRequest(req);
        break;
    case marla_EVENT_REQUEST_BODY:
        we = data;
        we->status = readRequestBody(req, we);
        break;
    case marla_EVENT_MUST_WRITE:
        marla_killRequest(req, "LogoutHandler must not process write events.");
        break;
    case marla_EVENT_DESTROYING:
        req->handlerData = 0;
        rainback_LogoutResponse_destroy(resp);
        break;
    }
}
