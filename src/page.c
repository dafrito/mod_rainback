#include "mod_rainback.h"
#include <string.h>

#define BUFSIZE 1024

rainback_Page* rainback_Page_new(const char* cacheKey)
{
    rainback_Page* page = malloc(sizeof(*page));
    page->data = malloc(BUFSIZE);
    page->contentLength = 0;
    page->headLength = 0;
    page->capacity = BUFSIZE;
    page->refs = 1;
    page->writeStage = 0;
    page->expiry.tv_sec = 0;
    page->expiry.tv_nsec = 0;

    page->cacheKey = 0;
    if(cacheKey) {
        size_t size = strlen(cacheKey);
        page->cacheKey = malloc(1 + size);
        memset(page->cacheKey, 0, 1 + size);
        strcpy(page->cacheKey, cacheKey);
    }
    return page;
}

void rainback_Page_endHead(rainback_Page* page)
{
    page->writeStage = 1;
}

int rainback_Page_write(rainback_Page* page, void* buf, size_t len)
{
    while(page->capacity < page->headLength + page->contentLength + len) {
        page->data = realloc(page->data, page->capacity * 2);
        page->capacity *= 2;
        if(!page->data) {
            return -1;
        }
    }
    memcpy(page->data + page->headLength + page->contentLength, buf, len);
    if(page->writeStage == 0) {
        page->headLength += len;
    }
    else if(page->writeStage == 1) {
        page->contentLength += len;
    }
    return len;
}

void rainback_Page_ref(rainback_Page* page)
{
    ++page->refs;
}

void rainback_Page_unref(rainback_Page* page)
{
    if(page->refs < 1) {
        fprintf(stderr, "rainback_Page unref'd at refs = %d\n", page->refs);
        abort();
    }
    if(--page->refs > 0) {
        return;
    }
    free(page->cacheKey);
    free(page->data);
    free(page);
}

static marla_WriteResult writeResponse(marla_Request* req, marla_WriteEvent* we)
{
    rainback_Page* page = req->handlerData;

    const char* header = page->data;
    int needed = page->headLength + (!strcmp(req->method, "HEAD") ? 0 : page->contentLength) - we->index;
    //fprintf(stderr, "Head=%d, Content=%d, index=%d, needed is %d\n", page->headLength, page->contentLength, we->index, needed);
    int nwritten = marla_Connection_write(req->cxn, page->data + we->index, needed);
    if(nwritten > 0) {
        we->index += nwritten;
    }
    if(nwritten < needed) {
        //fprintf(stderr, "DOWNSTREAM CHOKED after %d\n", nwritten);
        return marla_WriteResult_DOWNSTREAM_CHOKED;
    }
    if(!strcmp(req->method, "HEAD") && we->index >= page->headLength) {
        req->writeStage = marla_CLIENT_REQUEST_AFTER_RESPONSE;
    }
    else if(we->index >= page->headLength + page->contentLength) {
        req->writeStage = marla_CLIENT_REQUEST_AFTER_RESPONSE;
    }
    return marla_WriteResult_CONTINUE;
}

void rainback_pageHandler(struct marla_Request* req, enum marla_ClientEvent ev, void* data, int dataLen)
{
    rainback_Page* page = req->handlerData;
    marla_WriteEvent* we;
    switch(ev) {
    case marla_EVENT_REQUEST_BODY:
        we = data;
        if(we->length != 0) {
            marla_killRequest(req, 500, "rainback_pageHandler must not process any request body.");
        }
        else {
            req->readStage = marla_CLIENT_REQUEST_DONE_READING;
        }
        break;
    case marla_EVENT_HEADER:
        marla_killRequest(req, 500, "rainback_pageHandler must not process headers.");
        break;
    case marla_EVENT_ACCEPTING_REQUEST:
        (*(int*)data) = 1;
        //marla_killRequest(req, 500, "rainback_pageHandler must not accept requests.");
        break;
    case marla_EVENT_MUST_WRITE:
        we = data;
        we->status = writeResponse(req, we);
        break;
    case marla_EVENT_DESTROYING:
        req->handlerData = 0;
        rainback_Page_unref(page);
        break;
    }
}
