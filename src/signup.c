#include "mod_rainback.h"
#include <string.h>
#include <ctype.h>
#include <apr_escape.h>

#define BUFSIZE 4096

struct rainback_SignupResponse {
mod_rainback* rb;
marla_Ring* input;
apr_pool_t* pool;
parsegraph_user_login login;
};

rainback_SignupResponse* rainback_SignupResponse_new(marla_Request* req, mod_rainback* rb)
{
    rainback_SignupResponse* resp = malloc(sizeof(*resp));
    memset(&resp->login, 0, sizeof(resp->login));
    resp->rb = rb;
    if(apr_pool_create(&resp->pool, resp->rb->session->pool) != APR_SUCCESS) {
        marla_killRequest(req, 500, "Failed to create request handler memory pool.");
    }
    resp->input = marla_Ring_new(BUFSIZE);
    return resp;
}

void rainback_SignupResponse_destroy(rainback_SignupResponse* resp)
{
    apr_pool_destroy(resp->pool);
    marla_Ring_free(resp->input);
    free(resp);
}

void rainback_generateSignupPage(rainback_Page* page, mod_rainback* rb)
{
    apr_pool_t* pool;
    if(apr_pool_create(&pool, rb->session->pool) != APR_SUCCESS) {
        marla_die(rb->session->server, "Failed to generate request pool.");
    }
    rainback_Context* context = rainback_Context_new(pool);
    rainback_renderTemplate(rb, "signup.html", context, page);

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

void rainback_generatedSignupPasswordDoesntMatchPage(rainback_Page* page, mod_rainback* rb, const char* username)
{
    char encodedUsername[parsegraph_USERNAME_MAX_LENGTH * 3 + 1];
    memset(encodedUsername, 0, sizeof(encodedUsername));
    apr_escape_entity(encodedUsername, username, APR_ESCAPE_STRING, 1, 0);

    apr_pool_t* pool;
    if(apr_pool_create(&pool, rb->session->pool) != APR_SUCCESS) {
        marla_die(rb->session->server, "Failed to generate request pool.");
    }
    rainback_Context* context = rainback_Context_new(pool);
    rainback_Context_setString(context, "username", encodedUsername);
    rainback_renderTemplate(rb, "signup.html", context, page);

    char buf[8192];
    int len = snprintf(buf, sizeof buf,
        "HTTP/1.1 403 Bad Request\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %d\r\n"
        "\r\n",
        page->length
    );
    rainback_Page_prepend(page, buf, len);
    page->headBoundary = len;

    apr_pool_destroy(pool);
}

void rainback_generateSignupFailedPage(rainback_Page* page, mod_rainback* rb, parsegraph_UserStatus rv, const char* username)
{
    char encodedUsername[parsegraph_USERNAME_MAX_LENGTH * 3 + 1];
    memset(encodedUsername, 0, sizeof(encodedUsername));
    apr_escape_entity(encodedUsername, username, APR_ESCAPE_STRING, 1, 0);

    apr_pool_t* pool;
    if(apr_pool_create(&pool, rb->session->pool) != APR_SUCCESS) {
        marla_die(rb->session->server, "Failed to generate request pool.");
    }
    rainback_Context* context = rainback_Context_new(pool);
    rainback_Context_setString(context, "status", parsegraph_nameUserStatus(rv));
    rainback_Context_setString(context, "username", encodedUsername);
    rainback_renderTemplate(rb, "signup_failed.html", context, page);

    char buf[8192];
    int len = snprintf(buf, sizeof buf,
        "HTTP/1.1 403 Bad Request\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %d\r\n"
        "\r\n",
        page->length
    );
    rainback_Page_prepend(page, buf, len);
    page->headBoundary = len;
    apr_pool_destroy(pool);
}

static marla_WriteResult readSignupForm(mod_rainback* rb, marla_Request* req, char* buf, size_t buflen, parsegraph_user_login* login)
{
    if(!login) {
        fprintf(stderr, "A non-null login struct must be given.\n");
        abort();
    }
    char* pairPtr;
    char* sepPtr;
    char username[3*parsegraph_USERNAME_MAX_LENGTH + 1];
    memset(username, 0, sizeof username);
    char password[3*parsegraph_PASSWORD_MAX_LENGTH + 1];
    memset(password, 0, sizeof password);
    char passwordAgain[3*parsegraph_PASSWORD_MAX_LENGTH + 1];
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
                marla_killRequest(req, 400, "Login pair must have at least one =.");
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
            else if(!strcmp(subtoken, "password_again")) {
                subtoken = strtok_r(0, "=", &sepPtr);
                if(subtoken != 0) {
                    switch(apr_unescape_url(passwordAgain, subtoken, APR_ESCAPE_STRING, 0, 0, 1, 0)) {
                    case APR_SUCCESS:
                    case APR_NOTFOUND:
                        break;
                    default:
                        marla_killRequest(req, 400, "Failed to unescape repeated password.");
                        return marla_WriteResult_KILLED;
                    }
                }
            }
            for(; subtoken;) {
                subtoken = strtok_r(0, "=", &sepPtr);
            }
            break;
        }
    }

    rainback_Page* page = rainback_Page_new("");

    if(strcmp(password, passwordAgain)) {
        rainback_generatedSignupPasswordDoesntMatchPage(page, rb, username);
    }
    else {
        parsegraph_UserStatus rv = parsegraph_createNewUser(rb->session, username, password);
        if(rv != parsegraph_OK) {
            rainback_generateSignupFailedPage(page, rb, rv, username);
        }
        else {
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
        }
    }
    rainback_SignupResponse* resp = req->handlerData;
    rainback_SignupResponse_destroy(resp);
    req->handler = rainback_pageHandler;
    req->handlerData = page;
    return marla_WriteResult_CONTINUE;
}

static marla_WriteResult readRequestBody(marla_Request* req, marla_WriteEvent* we)
{
    rainback_SignupResponse* resp = req->handlerData;
    if(we->length == 0) {
        if(!strcmp(req->method, "POST")) {
            unsigned char buf[4096];
            int len = marla_Ring_read(resp->input, buf, sizeof buf);
            buf[len] = 0;
            for(int i = 0; i < len; ++i) {
                char c = buf[i];
                if(c == 0) {
                    marla_killRequest(req, 400, "Unexpected null given in %s request.", req->method);
                    return marla_WriteResult_KILLED;
                }
            }
            marla_WriteResult wr = readSignupForm(resp->rb, req, buf, len, &resp->login);
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
    rainback_SignupResponse* resp = req->handlerData;
    if(!strcmp(req->method, "GET") || !strcmp(req->method, "HEAD")) {
        if(resp->login.username) {
            rainback_Page* page = rainback_Page_new(0);
            rainback_generateAlreadyLoggedInPage(page, resp->rb, &resp->login);
            req->handler = rainback_pageHandler;
            req->handlerData = page;
            rainback_SignupResponse_destroy(resp);
            return 1;
        }

        req->handler = rainback_pageHandler;
        req->handlerData = rainback_getPage(resp->rb, "", req->uri, &resp->login);
        rainback_SignupResponse_destroy(resp);
        return 1;
    }
    if(!strcmp(req->method, "POST")) {
        return 1;
    }

    return 0;
}

void rainback_signupHandler(struct marla_Request* req, enum marla_ClientEvent ev, void* data, int dataLen)
{
    rainback_SignupResponse* resp = req->handlerData;

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
        marla_killRequest(req, 500, "SignupHandler must not process write events.");
        break;
    case marla_EVENT_DESTROYING:
        req->handlerData = 0;
        rainback_SignupResponse_destroy(resp);
        break;
    }
}
