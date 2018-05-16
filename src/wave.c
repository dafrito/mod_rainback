#include "mod_rainback.h"
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <apr_escape.h>

#define BUFSIZE 4096
#define NUM_CHANNELS 2

rainback_WaveResponse* rainback_WaveResponse_new(marla_Request* req, mod_rainback* rb)
{
    rainback_WaveResponse* resp = malloc(sizeof(*resp));
    resp->printed_header = 0;
    resp->printed_wav_header = 0;
    memset(&resp->login, 0, sizeof(resp->login));
    resp->rb = rb;
    if(apr_pool_create(&resp->pool, resp->rb->session->pool) != APR_SUCCESS) {
        marla_killRequest(req, 500, "Failed to create request handler memory pool.");
    }
    resp->input = marla_Ring_new(BUFSIZE);

    resp->sampleRate = 44100;
    resp->bitsPerSample = 16;
    resp->lengthSec = 5;
    resp->numSamples = resp->lengthSec * resp->sampleRate;
    resp->index = 0;
    resp->baseFreq = 0.0f;
    return resp;
}

void rainback_WaveResponse_destroy(rainback_WaveResponse* resp)
{
    apr_pool_destroy(resp->pool);
    marla_Ring_free(resp->input);
    free(resp);
}

static marla_WriteResult readRequestBody(marla_Request* req, marla_WriteEvent* we)
{
    rainback_WaveResponse* resp = req->handlerData;
    if(we->length == 0) {
        req->readStage = marla_CLIENT_REQUEST_DONE_READING;
        return marla_WriteResult_CONTINUE;
    }
    marla_killRequest(req, 400, "Unexpected input given in %s request.", req->method);
    return marla_WriteResult_KILLED;
}

static int acceptRequest(marla_Request* req)
{
    rainback_WaveResponse* resp = req->handlerData;
    return 1;
}

static marla_WriteResult writeRequest(marla_Request* req, marla_WriteEvent* we)
{
    if(req->writeStage < marla_CLIENT_REQUEST_WRITING_RESPONSE) {
        return marla_WriteResult_UPSTREAM_CHOKED;
    }

    rainback_WaveResponse* resp = req->handlerData;
    if(!resp->printed_header) {
        char buf[1024];
        int len = snprintf(buf, sizeof(buf), "HTTP/1.1 200 OK\r\nContent-Length: %d\r\nContent-Type: audio/wav\r\n\r\n",
            44 + (resp->bitsPerSample/8) * resp->numSamples * NUM_CHANNELS
        );
        int nwritten = marla_Connection_write(req->cxn, buf, len);
        if(nwritten < len) {
            if(nwritten > 0) {
                marla_Connection_putbackWrite(req->cxn, nwritten);
            }
            return marla_WriteResult_DOWNSTREAM_CHOKED;
        }
        resp->printed_header = 1;
    }

    if(!resp->printed_wav_header) {
        char buf[1024];

        int bitsPerSample = 16;

        // ChunkID
        strncpy(buf, "RIFF", 4);
        // ChunkSize
        unsigned int dataSize = (resp->bitsPerSample/8) * resp->numSamples * NUM_CHANNELS;
        *((uint32_t*)(buf + 4)) = htole32(dataSize + 36);
        strncpy(buf + 8, "WAVE", 4);

        // fmt subchunk
        strncpy(buf + 12, "fmt ", 4);
        *((uint32_t*)(buf + 16)) = htole32(16);
        // Audio Format (PCM=1)
        *((uint16_t*)(buf + 20)) = htole16(1);
        // NumChannels
        *((uint16_t*)(buf + 22)) = htole16(NUM_CHANNELS);
        // SampleRate
        *((uint32_t*)(buf + 24)) = htole32(resp->sampleRate);
        // ByteRate
        *((uint32_t*)(buf + 28)) = htole32(NUM_CHANNELS * (bitsPerSample/8) * resp->sampleRate);
        // BlockAlign
        *((uint16_t*)(buf + 32)) = htole16(NUM_CHANNELS * (bitsPerSample/8));
        // BitsPerSample
        *((uint16_t*)(buf + 34)) = htole16(bitsPerSample);

        strncpy(buf + 36, "data", 4);
        *((uint32_t*)(buf + 40)) = htole32(dataSize);

        int nwritten = marla_Connection_write(req->cxn, buf, 44);
        if(nwritten < 44) {
            if(nwritten > 0) {
                marla_Connection_putbackWrite(req->cxn, nwritten);
            }
            return marla_WriteResult_DOWNSTREAM_CHOKED;
        }

        resp->printed_wav_header = 1;
    }

    if(resp->baseFreq == 0.0f) {
        resp->baseFreq = 120.0f + ((float)rand()/(float)RAND_MAX)*700;
    }
    for(; resp->index < resp->numSamples; ++resp->index) {
        float freq = resp->baseFreq;
        float timePos = (float)resp->index / (float)resp->sampleRate;
        float baseLoudness = 0.1f;
        float loudness = baseLoudness;
        if(timePos < resp->lengthSec*.66) {
            loudness *= timePos / (resp->lengthSec*.66);
        }
        freq = freq + freq * (timePos/resp->lengthSec);
        //if(freq > 440 + 440 * (8/12)) {
            //freq = 440 + 440 * (8/12);
        //}
        loudness = loudness + .4 * baseLoudness * sinf(2 * 3.14159 * 8 * timePos);
        if(loudness > 1.0f) {
            loudness = 1.0f;
        }
        if(resp->index > resp->numSamples - (resp->sampleRate * .25)) {
            loudness = loudness * (float)(resp->numSamples - resp->index) /(float)(resp->sampleRate * .5);
        }
        int16_t l = loudness * sinf(2 * 3.14159 * freq * timePos) * (65536 - 0x8000);
        int16_t r = l;
        //int16_t r = rand() % 65536 - 0x8000;
        char buf[sizeof(int16_t) * NUM_CHANNELS];
        *((int16_t*)buf) = l;
        ((int16_t*)buf)[1] = r;
        int len = sizeof(int16_t) * NUM_CHANNELS;
        int nwritten = marla_Connection_write(req->cxn, buf, len);
        if(nwritten < len) {
            if(nwritten > 0) {
                marla_Connection_putbackWrite(req->cxn, nwritten);
            }
            return marla_WriteResult_DOWNSTREAM_CHOKED;
        }
    }

    req->writeStage = marla_CLIENT_REQUEST_AFTER_RESPONSE;

    return marla_WriteResult_CONTINUE;
}

void rainback_waveHandler(struct marla_Request* req, enum marla_ClientEvent ev, void* data, int dataLen)
{
    rainback_WaveResponse* resp = req->handlerData;

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
        rainback_WaveResponse_destroy(resp);
        break;
    }
}
