#include "mod_rainback.h"
#include <string.h>
#include <apr_escape.h>

struct rainback_ForgotPasswordResponse {
mod_rainback* rb;
marla_Ring* input;
parsegraph_user_login login;
};
typedef struct rainback_ForgotPasswordResponse rainback_ForgotPasswordResponse;

rainback_ForgotPasswordResponse* rainback_ForgotPasswordResponse_new(mod_rainback* rb)
{
    rainback_ForgotPasswordResponse* resp = malloc(sizeof(*resp));
    resp->rb = rb;
    return resp;
}

void rainback_ForgotPasswordResponse_destroy(rainback_ForgotPasswordResponse* resp)
{
    free(resp);
}

void rainback_generateForgotPasswordPage(rainback_Page* page, mod_rainback* rb, const char* pageState, parsegraph_user_login* login)
{
    apr_pool_t* pool;
    if(apr_pool_create(&pool, rb->session->pool) != APR_SUCCESS) {
        marla_die(rb->session->server, "Failed to generate request pool.");
    }

    // Render the response body from the template.
    rainback_Context* context = rainback_Context_new(pool);
    rainback_Context_setString(context, "title",
        (login && login->username) ? login->username : "Rainback");
    rainback_Context_setString(context, "username",
        (login && login->username) ? login->username : "Anonymous");
    rainback_renderTemplate(rb, "import.html", context, page);
    rainback_renderTemplate(rb, "forgot_password.html", context, page);

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

static marla_WriteResult readForgotPasswordForm(mod_rainback* rb, marla_Request* req, char* buf, size_t buflen, parsegraph_user_login* login)
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
                marla_killRequest(req, 400, "Pair must have at least one =.");
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
                        marla_killRequest(req, 400, "Failed to unescape username.");
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
                        marla_killRequest(req, 400, "Failed to unescape password.");
                        return marla_WriteResult_KILLED;
                    }
                }
            }
            subtoken = strtok_r(0, "=", &sepPtr);
            if(subtoken != NULL) {
                marla_killRequest(req, 400, "Pair is malformed.");
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
    rainback_ForgotPasswordResponse* resp = req->handlerData;
    rainback_ForgotPasswordResponse_destroy(resp);
    req->handler = rainback_pageHandler;
    req->handlerData = page;
    return marla_WriteResult_CONTINUE;
}

static marla_WriteResult readRequestBody(marla_Request* req, marla_WriteEvent* we)
{
    rainback_ForgotPasswordResponse* resp = req->handlerData;
    if(we->length == 0) {
        if(!strcmp(req->method, "POST")) {
            unsigned char buf[4096];
            int len = marla_Ring_read(resp->input, buf, sizeof buf);
            buf[len] = 0;
            marla_WriteResult wr = readForgotPasswordForm(resp->rb, req, buf, len, &resp->login);
            if(wr != marla_WriteResult_CONTINUE) {
                return wr;
            }
        }
        req->readStage = marla_CLIENT_REQUEST_DONE_READING;
        return marla_WriteResult_CONTINUE;
    }
    if(strcmp(req->method, "POST")) {
        marla_killRequest(req, 400, "Unexpected input given in %s request.", req->method);
        return marla_WriteResult_KILLED;
    }

    int nwrit = marla_Ring_write(resp->input, we->buf + we->index, we->length - we->index);
    we->index += nwrit;
    if(nwrit < we->length - we->index) {
        marla_killRequest(req, 400, "Too much input given to login request.\n");
        return marla_WriteResult_KILLED;
    }

    return marla_WriteResult_CONTINUE;
}

static int acceptRequest(marla_Request* req)
{
    rainback_ForgotPasswordResponse* resp = req->handlerData;
    if(!strcmp(req->method, "GET") || !strcmp(req->method, "HEAD")) {
        if(!resp->login.username) {
            rainback_Page* page = rainback_Page_new(0);
            rainback_generateNotLoggedInPage(page, resp->rb);
            req->handler = rainback_pageHandler;
            req->handlerData = page;
            rainback_ForgotPasswordResponse_destroy(resp);
            return 1;
        }

        req->handler = rainback_pageHandler;
        req->handlerData = rainback_getPage(resp->rb, "", req->uri, &resp->login);
        rainback_ForgotPasswordResponse_destroy(resp);
        return 1;
    }
    if(!strcmp(req->method, "POST")) {
        return 1;
    }

    return 0;
}

void rainback_ForgotPasswordHandler(struct marla_Request* req, enum marla_ClientEvent ev, void* data, int dataLen)
{
    rainback_ForgotPasswordResponse* resp = req->handlerData;

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
        marla_killRequest(req, 500, "ForgotPasswordHandler must not process write events.");
        break;
    case marla_EVENT_DESTROYING:
        req->handlerData = 0;
        rainback_ForgotPasswordResponse_destroy(resp);
        break;
    }
}
