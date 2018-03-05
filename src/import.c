#include "mod_rainback.h"

void rainback_importHandler(struct marla_Request* req, enum marla_ClientEvent ev, void* data, int datalen)
{
    marla_WriteEvent* we;
    switch(ev) {
    case marla_EVENT_HEADER:
        break;
    case marla_EVENT_ACCEPTING_REQUEST:
        //if(req->contentType
        *((int*)data) = 1;
        break;
    case marla_EVENT_REQUEST_BODY:
        we = data;
        if(we->length == 0) {
            req->readStage = marla_CLIENT_REQUEST_DONE_READING;
            return;
        }
        break;
    case marla_EVENT_MUST_WRITE:
        req->writeStage = marla_CLIENT_REQUEST_AFTER_RESPONSE;
        break;
    case marla_EVENT_DESTROYING:
        break;
    }
}

marla_WriteResult makeImportPage(struct marla_ChunkedPageRequest* cpr)
{
    char buf[1024];
    int len;

    marla_Server* server = cpr->req->cxn->server;
    char websocket_url[256];
    memset(websocket_url, 0, sizeof websocket_url);
    int off = sprintf(websocket_url, server->using_ssl ? "wss://" : "ws://");
    if(index(server->serverport, ':') != 0) {
        off += sprintf(websocket_url + off, server->serverport);
    }
    else {
        off += sprintf(websocket_url + off, "localhost:%s", server->serverport);
    }
    off += sprintf(websocket_url + off, "/environment/live");

    // Generate the page.
    switch(cpr->handleStage) {
    case 0:
        len = snprintf(buf, sizeof buf, "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">");
        break;
    case 1:
        len = snprintf(buf, sizeof buf,
            "<script>"
            "function run() {"
                "WS=new WebSocket(\"%s\"); "
                "WS.onopen = function() { WS.send('123456789012345678901234567890123456'); };"
                "WS.onclose = function(c, r) { console.log(c, r); };"
            "};"
            "</script>"
            "</head>"
            "<body onload='run()'>"
            "Hello, <b>world.</b>"
            "<p>"
            "This is request %d from servermod"
            "<p>"
            "<a href='/contact'>Contact us!</a>"
            "</body></html>",
            websocket_url,
            cpr->req->id
        );
        break;
    default:
        return marla_WriteResult_CONTINUE;
    }

    // Write the generated page.
    int nwritten = marla_Ring_write(cpr->input, buf + cpr->index, len - cpr->index);
    if(nwritten == 0) {
        return marla_WriteResult_DOWNSTREAM_CHOKED;
    }
    if(nwritten + cpr->index < len) {
        if(nwritten > 0) {
            cpr->index += nwritten;
        }
    }
    else {
        // Move to the next stage.
        ++cpr->handleStage;
        cpr->index = 0;
    }

    return marla_WriteResult_CONTINUE;
}

