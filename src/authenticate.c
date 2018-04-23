#include "mod_rainback.h"
#include <string.h>
#include <ctype.h>
#include <apr_escape.h>

#define BUFSIZE 4096

struct rainback_AuthenticateResponse {
mod_rainback* rb;
marla_Ring* input;
apr_pool_t* pool;
parsegraph_user_login login;
};

rainback_AuthenticateResponse* rainback_AuthenticateResponse_new(marla_Request* req, mod_rainback* rb)
{
    rainback_AuthenticateResponse* resp = malloc(sizeof(*resp));
    memset(&resp->login, 0, sizeof(resp->login));
    resp->rb = rb;
    if(apr_pool_create(&resp->pool, resp->rb->session->pool) != APR_SUCCESS) {
        marla_killRequest(req, 500, "Failed to create request handler memory pool.");
    }
    resp->input = marla_Ring_new(BUFSIZE);
    return resp;
}

void rainback_AuthenticateResponse_destroy(rainback_AuthenticateResponse* resp)
{
    apr_pool_destroy(resp->pool);
    marla_Ring_free(resp->input);
    free(resp);
}

static marla_WriteResult writeResponse(marla_Request* req, marla_WriteEvent* we, rainback_AuthenticateResponse* resp)
{
    char bodybuf[8192];
    int needed;
    if(resp->login.username) {
        needed = snprintf(bodybuf, sizeof bodybuf,
            "{\"username\":\"%s\", \"result\":\"Successfully authenticated.\"}", resp->login.username
        );
    }
    else {
        needed = snprintf(bodybuf, sizeof bodybuf,
            "{\"username\":\"\", \"result\":\"Failed to authenticate.\"}"
        );
    }

    char buf[8192];
    int fullneeded = snprintf(buf, sizeof buf,
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\n\r\n%s", needed, bodybuf
    );
    int nwritten = marla_Connection_write(req->cxn, buf, fullneeded);
    if(nwritten < fullneeded) {
        if(nwritten > 0) {
            marla_Connection_putbackWrite(req->cxn, nwritten);
        }
        return marla_WriteResult_DOWNSTREAM_CHOKED;
    }
    req->writeStage = marla_CLIENT_REQUEST_AFTER_RESPONSE;
    return marla_WriteResult_CONTINUE;
}

void rainback_authenticateHandler(struct marla_Request* req, enum marla_ClientEvent ev, void* data, int dataLen)
{
    rainback_AuthenticateResponse* resp = req->handlerData;

    marla_WriteEvent* we;
    switch(ev) {
    case marla_EVENT_HEADER:
        if(strcmp("Cookie", data)) {
            break;
        }
        rainback_authenticateByCookie(req, resp->rb, &resp->login, data + dataLen);
        break;
    case marla_EVENT_ACCEPTING_REQUEST:
        *((int*)data) = 1;
        break;
    case marla_EVENT_REQUEST_BODY:
        we = data;
        if(we->length > 0) {
            we->index = we->length;
        }
        else {
            req->readStage = marla_CLIENT_REQUEST_DONE_READING;
        }
        break;
    case marla_EVENT_MUST_WRITE:
        we = data;
        we->status = writeResponse(req, we, resp);
        break;
    case marla_EVENT_DESTROYING:
        req->handlerData = 0;
        rainback_AuthenticateResponse_destroy(resp);
        break;
    }
}
