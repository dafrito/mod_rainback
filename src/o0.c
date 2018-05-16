#include "mod_rainback.h"
#include <string.h>
#include <ctype.h>
#include <apr_escape.h>

#define BUFSIZE 4096

rainback_o0Response* rainback_o0Response_new(marla_Request* req, mod_rainback* rb)
{
    rainback_o0Response* resp = malloc(sizeof(*resp));
    resp->printed_header = 0;
    resp->remainingo0 = 50000;
    memset(&resp->login, 0, sizeof(resp->login));
    resp->rb = rb;
    if(apr_pool_create(&resp->pool, resp->rb->session->pool) != APR_SUCCESS) {
        marla_killRequest(req, 500, "Failed to create request handler memory pool.");
    }
    resp->input = marla_Ring_new(BUFSIZE);
    return resp;
}

void rainback_o0Response_destroy(rainback_o0Response* resp)
{
    apr_pool_destroy(resp->pool);
    marla_Ring_free(resp->input);
    free(resp);
}

static marla_WriteResult readRequestBody(marla_Request* req, marla_WriteEvent* we)
{
    rainback_o0Response* resp = req->handlerData;
    if(we->length == 0) {
        req->readStage = marla_CLIENT_REQUEST_DONE_READING;
        return marla_WriteResult_CONTINUE;
    }
    marla_killRequest(req, 400, "Unexpected input given in %s request.", req->method);
    return marla_WriteResult_KILLED;
}

static int acceptRequest(marla_Request* req)
{
    rainback_o0Response* resp = req->handlerData;
    return 1;
}

static marla_WriteResult writeRequest(marla_Request* req, marla_WriteEvent* we)
{
    if(req->writeStage < marla_CLIENT_REQUEST_WRITING_RESPONSE) {
        return marla_WriteResult_UPSTREAM_CHOKED;
    }

    const char* text = "o0";

    rainback_o0Response* resp = req->handlerData;
    if(!resp->printed_header) {
        char buf[1024];
        // Limited mode
        int len = snprintf(buf, sizeof(buf), "HTTP/1.1 200 OK\r\nContent-Length: %d\r\nContent-Type: text/plain\r\n\r\n", strlen(text)*resp->remainingo0);
        // Endless mode
        //int len = snprintf(buf, sizeof(buf), "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: text/plain\r\n\r\n");
        req->close_after_done = 1;
        int nwritten = marla_Connection_write(req->cxn, buf, len);
        if(nwritten < len) {
            if(nwritten > 0) {
                marla_Connection_putbackWrite(req->cxn, nwritten);
            }
            return marla_WriteResult_DOWNSTREAM_CHOKED;
        }
        resp->printed_header = 1;
    }

    for(; resp->remainingo0 > 0; --resp->remainingo0) {
        // Force this counter to never reach zero.
        resp->remainingo0 = 2;
        int nwritten = marla_Connection_write(req->cxn, text, 2);
        if(nwritten < 2) {
            if(nwritten > 0) {
                marla_Connection_putbackWrite(req->cxn, nwritten);
            }
            return marla_WriteResult_DOWNSTREAM_CHOKED;
        }
    }

    req->writeStage = marla_CLIENT_REQUEST_AFTER_RESPONSE;

    return marla_WriteResult_CONTINUE;
}

void rainback_o0Handler(struct marla_Request* req, enum marla_ClientEvent ev, void* data, int dataLen)
{
    rainback_o0Response* resp = req->handlerData;

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
        we = data;
        we->status = writeRequest(req, we);
        break;
    case marla_EVENT_DESTROYING:
        req->handlerData = 0;
        rainback_o0Response_destroy(resp);
        break;
    }
}
