#include "mod_rainback.h"
#include <string.h>
#include <apr_strings.h>

#define BUFSIZE 4096

struct rainback_HomepageResponse {
mod_rainback* rb;
marla_Ring* input;
apr_pool_t* pool;
parsegraph_user_login login;
};

rainback_HomepageResponse* rainback_HomepageResponse_new(marla_Request* req, mod_rainback* rb)
{
    rainback_HomepageResponse* resp = malloc(sizeof(*resp));
    memset(&resp->login, 0, sizeof(resp->login));
    resp->rb = rb;
    if(apr_pool_create(&resp->pool, resp->rb->session->pool) != APR_SUCCESS) {
        marla_killRequest(req, 500, "Failed to create request handler memory pool.");
    }
    resp->input = marla_Ring_new(BUFSIZE);
    return resp;
}

void rainback_HomepageResponse_destroy(rainback_HomepageResponse* resp)
{
    apr_pool_destroy(resp->pool);
    marla_Ring_free(resp->input);
    free(resp);
}

static int acceptRequest(marla_Request* req)
{
    rainback_HomepageResponse* resp = req->handlerData;
    if(!strcmp(req->method, "GET") || !strcmp(req->method, "HEAD")) {
        req->handler = rainback_pageHandler;
        req->handlerData = rainback_getPage(resp->rb, "", req->uri, &resp->login);
        rainback_HomepageResponse_destroy(resp);
        return 1;
    }
    else if(!strcmp(req->method, "POST")) {
        return 1;
    }

    return 0;
}

static marla_WriteResult readRequestBody(marla_Request* req, marla_WriteEvent* we)
{
    rainback_HomepageResponse* resp = req->handlerData;

    if(!strcmp(req->method, "POST")) {
        char* buf = we->buf + we->index;
        int len = we->length - we->index;
        if(!strcmp(buf, "action=parsegraph_createEnvironment")) {
            parsegraph_GUID createdEnv;
            parsegraph_EnvironmentStatus erv = parsegraph_createEnvironment(
                resp->rb->session, resp->login.userId, 0, 0, &createdEnv);
            if(erv != parsegraph_Environment_OK) {
                return parsegraph_environmentStatusToHttp(erv);
            }
            char buf[4096];
            int needed = snprintf(buf, sizeof buf,
                "HTTP/1.1 303 See Other\r\nLocation: /environment/%s\r\n\r\n", createdEnv.value
            );
            int nwritten = marla_Connection_write(req->cxn, buf, needed);
            if(nwritten < needed) {
                if(nwritten > 0) {
                    marla_Connection_putbackWrite(req->cxn, nwritten);
                }
                return marla_WriteResult_DOWNSTREAM_CHOKED;
            }

            while(marla_Ring_size(req->cxn->output) > 0) {
                int nflushed;
                marla_WriteResult wr = marla_Connection_flush(req->cxn, &nflushed);
                if(wr != marla_WriteResult_CONTINUE) {
                    return wr;
                }
            }

            req->writeStage = marla_CLIENT_REQUEST_AFTER_RESPONSE;
            return marla_WriteResult_CONTINUE;
        }
        else {
            marla_killRequest(req, 400, "Unexpected input given in %s request.", req->method);
            return marla_WriteResult_KILLED;
        }
    }
    else if(we->length == 0) {
        req->writeStage = marla_CLIENT_REQUEST_DONE_READING;
        return marla_WriteResult_CONTINUE;
    }
    marla_killRequest(req, 400, "Unexpected input given in %s request.", req->method);
    return marla_WriteResult_KILLED;
}

void rainback_homepageHandler(struct marla_Request* req, enum marla_ClientEvent ev, void* data, int dataLen)
{
    rainback_HomepageResponse* resp = req->handlerData;

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
        marla_killRequest(req, 500, "HomepageHandler must not process write events.");
        break;
    case marla_EVENT_DESTROYING:
        req->handlerData = 0;
        rainback_HomepageResponse_destroy(resp);
        break;
    }
}

static int printEnvironmentForGUID(mod_rainback* rb, rainback_Page* page, struct parsegraph_user_login* userLogin, parsegraph_GUID* env)
{
    // Get the title.
    apr_pool_t* pool = rb->session->server->pool;
    const char* envTitle = 0;
    parsegraph_EnvironmentStatus erv = parsegraph_Environment_OK;
    erv = parsegraph_getEnvironmentTitleForGUID(rb->session, env, &envTitle);
    if(parsegraph_isSeriousEnvironmentError(erv)) {
        marla_logMessagef(rb->session->server, "Failed to retrieve environment title for %s.", envTitle);
        return parsegraph_environmentStatusToHttp(erv);
    }

    if(!envTitle) {
        envTitle = env->value;
    }

    char buf[1024];
    int len = snprintf(buf, sizeof buf, "<a href=\"%s\">%s</a>\n", apr_pstrcat(pool, "/environment/", env->value, NULL), envTitle);
    rainback_Page_append(page, buf, len);
    return 200;
}

static int printEnvironmentForId(mod_rainback* rb, rainback_Page* page, struct parsegraph_user_login* userLogin, int envId)
{
    parsegraph_GUID envGUID;
    parsegraph_EnvironmentStatus erv;
    erv = parsegraph_getEnvironmentGUIDForId(rb->session, envId, &envGUID);
    switch(erv) {
    case parsegraph_Environment_OK:
        break;
    case parsegraph_Environment_NOT_FOUND:
        return 404;
    default:
        return parsegraph_environmentStatusToHttp(erv);
    }

    return printEnvironmentForGUID(rb, page, userLogin, &envGUID);
}

static int printEnvironmentList(mod_rainback* rb, rainback_Page* page, struct parsegraph_user_login* userLogin, apr_dbd_results_t* savedEnvGUIDs)
{
    apr_dbd_row_t* envRow = 0;
    parsegraph_GUID guid;
    guid.value[36] = 0;
    rainback_Page_append(page, "<ul>", 4);
    const apr_dbd_driver_t* driver = rb->session->dbd->driver;
    apr_pool_t* pool = rb->session->server->pool;
    while(0 == apr_dbd_get_row(driver, pool, savedEnvGUIDs, &envRow, -1)) {
        const char* savedEnvGUID = apr_dbd_get_entry(driver, envRow, 0);
        if(!savedEnvGUID) {
            // No GUID for this record.
            continue;
        }
        strncpy(guid.value, savedEnvGUID, 36);
        rainback_Page_append(page, "<li>", 4);
        printEnvironmentForGUID(rb, page, userLogin, &guid);
    }
    rainback_Page_append(page, "</ul>", 5);
    return 0;
}

// Saved - All environments saved by the authenticated user
static int printSavedEnvironments(mod_rainback* rb, rainback_Page* page, struct parsegraph_user_login* userLogin)
{
    apr_dbd_results_t* savedEnvGUIDs = 0;
    parsegraph_EnvironmentStatus erv;
    erv = parsegraph_getSavedEnvironmentGUIDs(rb->session, userLogin->userId, &savedEnvGUIDs);

    char buf[1024];
    int len = snprintf(buf, sizeof buf, "<h2>%s's saved environments</h2>", userLogin->username);
    rainback_Page_append(page, buf, len);

    if(parsegraph_isSeriousEnvironmentError(erv)) {
        return parsegraph_environmentStatusToHttp(erv);
    }
    return printEnvironmentList(rb, page, userLogin, savedEnvGUIDs);
}

// Owned - All environments owned by the user if there are any environments owned by the authenticated user.
static int printOwnedEnvironments(mod_rainback* rb, rainback_Page* page, struct parsegraph_user_login* userLogin)
{
    apr_dbd_results_t* envs = 0;
    parsegraph_EnvironmentStatus erv;
    erv = parsegraph_getOwnedEnvironmentGUIDs(rb->session, userLogin->userId, &envs);
    char buf[1024];
    int len = snprintf(buf, len, "<h2>%s's owned environments</h2>", userLogin->username);
    rainback_Page_append(page, buf, len);
    if(parsegraph_isSeriousEnvironmentError(erv)) {
        return parsegraph_environmentStatusToHttp(erv);
    }
    return printEnvironmentList(rb, page, userLogin, envs);
}

// Invited - All invites for this user.
/*static int printInvitedEnvironments(request_rec *r, struct parsegraph_user_login* userLogin)
{
    apr_dbd_results_t* savedEnvGUIDs;
    ap_dbd_t* dbd = ap_dbd_acquire(r);
    parsegraph_EnvironmentStatus erv;
    erv = parsegraph_getInvitedEnvironmentGUIDs(r->pool, dbd, userLogin->userId, &savedEnvGUIDs);
    if(parsegraph_isSeriousEnvironmentError(erv)) {
        return parsegraph_environmentStatusToHttp(erv);
    }
    return printEnvironmentList(r, dbd, userLogin, savedEnvGUIDs);
}
*/

// TODO Implement these queries.
// Online - All friends who are online.
// Administration - All environments flagged 'admin' if the user is also an administrator.
// Top - Only one exists per server.
// Recent - Top X environments accessible to the user if there are any environments visited by the user.
// Watched X - Top X environments tagged with name. One group shown for each watched tag.
// Subscribed X - Top X environments owned by X. One group shown for each subscribed user.
// Featured - Top X environments flagged 'featured' if there are any featured environments.
// Starting - All environments flagged 'starting' if user is less than X time created.
// New - Top X newest environments
// Random - X random environments if there are at least 5*X environments.
// Most Popular - X most popular environments (If there are at least X environments and at least Y hits for any environment.)
// Private - All environments private to the user if there are any non-public environments with a permission set for the authenticated user.
// Published - All environments published by the user if there are any public environments owned by the authenticated user.

static void* ownedEnvironments(rainback_Template* tp, rainback_Context* context, void** savePtr)
{
    if(*savePtr) {
        // Continue.
        apr_dbd_results_t* savedEnvGUIDs = *savePtr;
    }
    else {
        // Initialize.
        apr_dbd_results_t* savedEnvGUIDs = *savePtr;
        parsegraph_EnvironmentStatus erv = parsegraph_getOwnedEnvironmentGUIDs(
            tp->rb->session, userLogin->userId, &savedEnvGUIDs);
        *savePtr = savedEnvGUIDs;

        apr_dbd_row_t* envRow = 0;
        if(0 == apr_dbd_get_row(driver, pool, savedEnvGUIDs, &envRow, -1)) {
            const char* savedEnvGUID = apr_dbd_get_entry(driver, envRow, 0);
            if(!savedEnvGUID) {
                // No GUID for this record.
                continue;
            }
            return savedEnvGUID;
        }
        else {
            return 0;
        }
    }
}

static void* savedEnvironments(rainback_Template* tp, apr_hash_t* context, void** savePtr)
{
    if(*savePtr) {
        // Continue.
    }
    else {
        // Initialize.
    }
}

static void rainback_generateUserpage(rainback_Page* page, mod_rainback* rb, const char* pageState, parsegraph_user_login* login)
{
    apr_pool_t* pool;
    if(apr_pool_create(&pool, rb->session->pool) != APR_SUCCESS) {
        marla_die(rb->session->server, "Failed to generate request pool.");
    }

    // Render the response body from the template.
    apr_hash_t* context = apr_hash_make(pool);
    apr_hash_set(context, "title", APR_HASH_KEY_STRING,
        (login && login->username) ? login->username : "Rainback");
    apr_hash_set(context, "username", APR_HASH_KEY_STRING,
        (login && login->username) ? login->username : "Anonymous");
    apr_hash_set(context, "saved_environments", APR_HASH_KEY_STRING, savedEnvironments);
    apr_hash_set(context, "owned_environments", APR_HASH_KEY_STRING, ownedEnvironments);
    rainback_renderTemplate(rb, "userpage.html", context, page);

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

void rainback_generateHomepage(rainback_Page* page, mod_rainback* rb, const char* pageState, parsegraph_user_login* login)
{
    if(login && login->username) {
        return rainback_generateUserpage(page, rb, pageState, login);
    }

    apr_pool_t* pool;
    if(apr_pool_create(&pool, rb->session->pool) != APR_SUCCESS) {
        marla_die(rb->session->server, "Failed to generate request pool.");
    }
    apr_hash_t* context = apr_hash_make(pool);
    rainback_renderTemplate(rb, "homepage.html", context, page);

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
