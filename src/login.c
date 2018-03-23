#include "mod_rainback.h"
#include <string.h>
#include <ctype.h>
#include <apr_escape.h>

#define BUFSIZE 4096

struct rainback_LoginResponse {
mod_rainback* rb;
marla_Ring* input;
apr_pool_t* pool;
parsegraph_user_login login;
};

rainback_LoginResponse* rainback_LoginResponse_new(marla_Request* req, mod_rainback* rb)
{
    rainback_LoginResponse* resp = malloc(sizeof(*resp));
    memset(&resp->login, 0, sizeof(resp->login));
    resp->rb = rb;
    if(apr_pool_create(&resp->pool, resp->rb->session->pool) != APR_SUCCESS) {
        marla_killRequest(req, "Failed to create request handler memory pool.");
    }
    resp->input = marla_Ring_new(BUFSIZE);
    return resp;
}

void rainback_LoginResponse_destroy(rainback_LoginResponse* resp)
{
    apr_pool_destroy(resp->pool);
    marla_Ring_free(resp->input);
    free(resp);
}

void rainback_generateLoginPage(rainback_Page* page, mod_rainback* rb, const char* pageState, parsegraph_user_login* login)
{
    char body[8192];
    int len = snprintf(body, sizeof body,
"<!DOCTYPE html>"
"<html>"
"<head>"
    "<title>Login to Rainback</title>"
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
        "<h1>Login to Rainback</h1>"
        "<form method=post>"
        "<div><label for=username>Username:</label><input type=text name=username></div>"
        "<div><label for=password>Password:</label><input type=password name=password></div>"
        "<div><label for=login></label><input type=submit value=\"Log in\" style=\"background: greenyellow\"> <a href=\"/signup\"><span class=bud style=\"background: gold\">Sign up</span></a></div>"
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

void rainback_generateAlreadyLoggedInPage(rainback_Page* page, mod_rainback* rb, parsegraph_user_login* login)
{
    char encodedUsername[parsegraph_USERNAME_MAX_LENGTH * 3 + 1];
    memset(encodedUsername, 0, sizeof(encodedUsername));
    apr_escape_entity(encodedUsername, login->username, APR_ESCAPE_STRING, 1, 0);

    char body[8192];
    int len = snprintf(body, sizeof body,
"<!DOCTYPE html>"
"<html>"
"<head>"
    "<title>Already logged in</title>"
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
        "<h1>Already logged in</h1>"
        "<p>You are already logged in as %s.</p>"
    "</div>"
"<div style=\"clear: both\"></div>"
"<div style=\"display: block; text-align: center; margin: 1em 0\">"
    "<div class=slot style=\"display: inline-block;\">"
        "&copy; 2018 <a href='https://rainback.com'>Rainback, Inc.</a> All rights reserved.</a>"
    "</div>"
"</div>"
"</body>"
"</html>",
    encodedUsername
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
        parsegraph_constructSessionString(rb->session,
            login->session_selector,
            login->session_token
        ),
        rb->session->server->using_ssl ? "Secure;" : ""
    );
    rainback_Page_write(page, buf, len);
    rainback_Page_endHead(page);
    rainback_Page_write(page, body, bodylen);
}

void rainback_generateLoginSucceededPage(rainback_Page* page, mod_rainback* rb, parsegraph_user_login* login)
{
    char body[8192];
    int len = snprintf(body, sizeof body,
"<!DOCTYPE html>"
"<html>"
"<head>"
    "<title>Login succeeded</title>"
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
    "<h1>Login succeeded</h1>"
    "<p>You are being redirected to your userpage.</p>"
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
        "Set-Cookie: session=%s;HttpOnly;%sMax-Age=315360000;Version=1\r\n"
        "Location: /\r\n"
        "\r\n",
        bodylen,
        parsegraph_constructSessionString(rb->session,
            login->session_selector,
            login->session_token
        ),
        rb->session->server->using_ssl ? "Secure;" : ""
    );
    rainback_Page_write(page, buf, len);
    rainback_Page_endHead(page);
    rainback_Page_write(page, body, bodylen);
}

void rainback_generateLoginFailedPage(rainback_Page* page, mod_rainback* rb, parsegraph_user_login* login, char* username)
{
    char encodedUsername[parsegraph_USERNAME_MAX_LENGTH * 3 + 1];
    memset(encodedUsername, 0, sizeof(encodedUsername));
    apr_escape_entity(encodedUsername, username, APR_ESCAPE_STRING, 1, 0);

    char body[8192];
    int len = snprintf(body, sizeof body,
"<!DOCTYPE html>"
"<html>"
"<head>"
    "<title>Login failed</title>"
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
        "<h1>Login failed</h1>"
        "<p>A server error occurred while logging in.</p>"
        "<form method=post>"
        "<div><label for=username>Username:</label><input type=text name=username></div>"
        "<div><label for=password>Password:</label><input type=password name=password></div>"
        "<div><label for=login></label><input type=submit value=\"Log in\" style=\"background: greenyellow\"> <a href=\"/signup\"><span class=bud style=\"background: gold\">Sign up</span></a></div>"
        "</form>"
    "</div>"
    "</div>"
"<div style=\"clear: both\"></div>"
"<div style=\"display: block; text-align: center; margin: 1em 0\">"
    "<div class=slot style=\"display: inline-block;\">"
        "&copy; 2018 <a href='https://rainback.com'>Rainback, Inc.</a> All rights reserved.</a>"
    "</div>"
"</div>"
"</body>"
"</html>",
    encodedUsername
    );
    size_t bodylen = strlen(body);

    char buf[8192];
    len = snprintf(buf, sizeof buf,
        "HTTP/1.1 403 Forbidden\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %d\r\n"
        "\r\n",
        bodylen
    );
    rainback_Page_write(page, buf, len);
    rainback_Page_endHead(page);
    rainback_Page_write(page, body, bodylen);
}

void rainback_generateBadUserOrPasswordPage(rainback_Page* page, mod_rainback* rb, parsegraph_user_login* login, char* username)
{
    char encodedUsername[parsegraph_USERNAME_MAX_LENGTH * 3 + 1];
    memset(encodedUsername, 0, sizeof(encodedUsername));
    apr_escape_entity(encodedUsername, username, APR_ESCAPE_STRING, 1, 0);

    char body[8192];
    int len = snprintf(body, sizeof body,
"<!DOCTYPE html>"
"<html>"
"<head>"
    "<title>Login to Rainback</title>"
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
        "<h1>Login to Rainback</h1>"
        "<p>The username or password was not recognized.</p>"
        "<form method=post>"
        "<div><label for=username>Username:</label><input type=text name=username value=\"%s\"></div>"
        "<div><label for=password>Password:</label><input type=password name=password></div>"
        "<div><label for=login></label><input type=submit value=\"Log in\" style=\"background: greenyellow\"> <a href=\"/signup\"><span class=bud style=\"background: gold\">Sign up</span></a></div>"
        "</form>"
    "</div>"
    "</div>"
"<div style=\"clear: both\"></div>"
"<div style=\"display: block; text-align: center; margin: 1em 0\">"
    "<div class=slot style=\"display: inline-block;\">"
        "&copy; 2018 <a href='https://rainback.com'>Rainback, Inc.</a> All rights reserved.</a>"
    "</div>"
"</div>"
"</body>"
"</html>",
    encodedUsername,
    encodedUsername
    );
    size_t bodylen = strlen(body);

    char buf[8192];
    len = snprintf(buf, sizeof buf,
        "HTTP/1.1 403 Forbidden\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %d\r\n"
        "\r\n",
        bodylen
    );
    rainback_Page_write(page, buf, len);
    rainback_Page_endHead(page);
    rainback_Page_write(page, body, bodylen);
}

static marla_WriteResult readLoginForm(mod_rainback* rb, marla_Request* req, char* buf, size_t buflen, parsegraph_user_login* login)
{
    if(!login) {
        fprintf(stderr, "A non-null login struct must be given.\n");
        abort();
    }
    char* pairPtr;
    char* sepPtr;
    char username[parsegraph_USERNAME_MAX_LENGTH + 1];
    int usernameIndex = 0;
    memset(username, 0, sizeof username);
    char password[parsegraph_PASSWORD_MAX_LENGTH + 1];
    int passwordIndex = 0;
    memset(password, 0, sizeof password);

    char* str1 = buf;
    char* str2;
    char* token, *subtoken;
    int j;
    token = strtok_r(buf, "&", &pairPtr);
    for(j = 1, str1 = buf; token; j++, str1 = NULL, token = strtok_r(0, "&", &pairPtr)) {
        for(str2 = token; ;) {
            subtoken = strtok_r(str2, "=", &sepPtr);
            if(subtoken == 0) {
                marla_killRequest(req, "Login pair must have at least one =.");
                return marla_WriteResult_KILLED;
            }
            if(!strcmp(subtoken, "username")) {
                subtoken = strtok_r(0, "=", &sepPtr);
                if(subtoken != 0) {
                    switch(apr_unescape_url(username, subtoken, APR_ESCAPE_STRING, 0, 0, 1, 0)) {
                    case APR_SUCCESS:
                    case APR_NOTFOUND:
                        break;
                    default:
                        marla_killRequest(req, "Failed to unescape username.");
                        return marla_WriteResult_KILLED;
                    }
                }
            }
            else if(!strcmp(subtoken, "password")) {
                subtoken = strtok_r(0, "=", &sepPtr);
                if(subtoken != 0) {
                    switch(apr_unescape_url(password, subtoken, APR_ESCAPE_STRING, 0, 0, 1, 0)) {
                    case APR_SUCCESS:
                    case APR_NOTFOUND:
                        break;
                    default:
                        marla_killRequest(req, "Failed to unescape password.");
                        return marla_WriteResult_KILLED;
                    }
                }
            }
            subtoken = strtok_r(0, "=", &sepPtr);
            if(subtoken != NULL) {
                marla_killRequest(req, "Login pair is malformed.");
                return marla_WriteResult_KILLED;
            }
            break;
        }
    }

    rainback_Page* page = rainback_Page_new("");

    switch(parsegraph_beginUserLogin(rb->session,
        username, password,
        &login
    )) {
    case parsegraph_OK:
        rainback_generateLoginSucceededPage(page, rb, login);
        break;
    case parsegraph_UNDEFINED_PREPARED_STATEMENT:
    case parsegraph_ERROR:
        rainback_generateLoginFailedPage(page, rb, login, username);
        break;
    default:
    case parsegraph_INVALID_PASSWORD:
    case parsegraph_USER_DOES_NOT_EXIST:
        rainback_generateBadUserOrPasswordPage(page, rb, login, username);
        break;
    }
    rainback_LoginResponse* resp = req->handlerData;
    rainback_LoginResponse_destroy(resp);
    req->handler = rainback_pageHandler;
    req->handlerData = page;
    return marla_WriteResult_CONTINUE;
}

static marla_WriteResult readRequestBody(marla_Request* req, marla_WriteEvent* we)
{
    rainback_LoginResponse* resp = req->handlerData;
    if(we->length == 0) {
        if(!strcmp(req->method, "POST")) {
            unsigned char buf[4096];
            int len = marla_Ring_read(resp->input, buf, sizeof buf);
            buf[len] = 0;
            marla_WriteResult wr = readLoginForm(resp->rb, req, buf, len, &resp->login);
            if(wr != marla_WriteResult_CONTINUE) {
                return wr;
            }
        }
        req->readStage = marla_CLIENT_REQUEST_DONE_READING;
        return marla_WriteResult_CONTINUE;
    }
    if(strcmp(req->method, "POST")) {
        marla_killRequest(req, "Unexpected input given in %s request.", req->method);
        return marla_WriteResult_KILLED;
    }

    int nwrit = marla_Ring_write(resp->input, we->buf + we->index, we->length - we->index);
    we->index += nwrit;
    if(nwrit < we->length - we->index) {
        marla_killRequest(req, "Too much input given to login request.\n");
        return marla_WriteResult_KILLED;
    }

    return marla_WriteResult_CONTINUE;
}

static int acceptRequest(marla_Request* req)
{
    rainback_LoginResponse* resp = req->handlerData;
    if(!strcmp(req->method, "GET") || !strcmp(req->method, "HEAD")) {
        if(resp->login.username) {
            rainback_Page* page = rainback_Page_new(0);
            rainback_generateAlreadyLoggedInPage(page, resp->rb, &resp->login);
            req->handler = rainback_pageHandler;
            req->handlerData = page;
            rainback_LoginResponse_destroy(resp);
            return 1;
        }

        req->handler = rainback_pageHandler;
        req->handlerData = rainback_getPage(resp->rb, "", req->uri, &resp->login);
        rainback_LoginResponse_destroy(resp);
        return 1;
    }
    if(!strcmp(req->method, "POST")) {
        return 1;
    }

    return 0;
}

void rainback_loginHandler(struct marla_Request* req, enum marla_ClientEvent ev, void* data, int dataLen)
{
    rainback_LoginResponse* resp = req->handlerData;

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
        marla_killRequest(req, "LoginHandler must not process write events.");
        break;
    case marla_EVENT_DESTROYING:
        req->handlerData = 0;
        rainback_LoginResponse_destroy(resp);
        break;
    }
}
