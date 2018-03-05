#include "mod_rainback.h"
#include <string.h>

struct rainback_ProfileResponse {
apr_pool_t* pool;
parsegraph_user_login* login;
};
typedef struct rainback_ProfileResponse rainback_ProfileResponse;

rainback_ProfileResponse* rainback_ProfileResponse_new(apr_pool_t* pool)
{
    rainback_ProfileResponse* resp = malloc(sizeof(*resp));
    resp->pool = pool;
    resp->login = calloc(1, sizeof(parsegraph_user_login));
    return resp;
}

static marla_WriteResult readRequestBody(marla_Request* req, marla_WriteEvent* we)
{
    if(we->length == 0) {
        req->writeStage = marla_CLIENT_REQUEST_DONE_READING;
        return marla_WriteResult_CONTINUE;
    }
    int true_read = marla_Ring_write(ring, we->buf + we->index, we->length - we->index);
    if(true_read < 0) {
        return marla_WriteResult_DOWNSTREAM_CHOKED;
    }
    we->index += true_read;
    return marla_WriteResult_CONTINUE;
}

static marla_WriteResult writeResponse(marla_Request* req)
{
    req->writeStage = marla_CLIENT_REQUEST_AFTER_RESPONSE;
    return marla_WriteResult_CONTINUE;
}

static int acceptRequest(marla_Request* req)
{
    return 1;
}


void rainback_profileHandler(struct marla_Request* req, enum marla_ClientEvent ev, void* data, int datalen)
{
    rainback_ProfileResponse resp = req->handlerData;
    if(!resp) {
        resp = rainback_ProfileResponse_new();
    }

    marla_WriteEvent* we;
    switch(ev) {
    case marla_EVENT_HEADER:
        if(strcmp("Cookie", data)) {
            break;
        }
        rainback_processCookie(req, resp->pool, resp->login, data + dataLen);
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
        we->status = writeResponse(req, we);
        break;
    case marla_EVENT_DESTROYING:
        break;
    }
}
