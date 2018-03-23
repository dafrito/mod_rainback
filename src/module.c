#include "mod_rainback.h"
#include <dlfcn.h>
#include <parsegraph_List.h>
#include <parsegraph_environment.h>

static parsegraph_Session* session;

int mod_rainback_preinit(apr_pool_t* modpool)
{
    struct timeval time;
    gettimeofday(&time,NULL);

    // microsecond has 1 000 000
    // Assuming you did not need quite that accuracy
    // Also do not assume the system clock has that accuracy.
    srand((time.tv_sec * 1000) + (time.tv_usec / 1000));

    // Load APR and DBD.
    dlopen(NULL, RTLD_NOW|RTLD_GLOBAL);
    dlopen("/usr/lib64/libaprutil-1.so", RTLD_NOW|RTLD_GLOBAL);
    char rverr[255];
    apr_dso_handle_t* res_handle;
    apr_status_t rv = apr_dso_load(&res_handle, "/usr/lib64/apr-util-1/apr_dbd_sqlite3-1.so", modpool);
    if(rv != APR_SUCCESS) {
        apr_dso_error(res_handle, rverr, 255);
        fprintf(stderr, "Failed loading DSO: %s", rverr);
        return -1;
    }

    // Initialize DBD.
    rv = apr_dbd_init(modpool);
    if(rv != APR_SUCCESS) {
        fprintf(stderr, "Failed initializing DBD, APR status of %d.\n", rv);
        return -1;
    }

    return 0;
}

static int mod_rainback_prepareDBD(parsegraph_Session* session)
{
    apr_pool_t* modpool = session->pool;
    ap_dbd_t* dbd = session->dbd;
    int rv = apr_dbd_get_driver(modpool, "sqlite3", &dbd->driver);
    if(rv != APR_SUCCESS) {
        fprintf(stderr, "Failed creating DBD driver, APR status of %d.\n", rv);
        return -1;
    }
    rv = apr_dbd_open(dbd->driver, modpool, session->server->db_path, &dbd->handle);
    if(rv != APR_SUCCESS) {
        fprintf(stderr, "Failed connecting to database at %s, APR status of %d.\n", session->server->db_path, rv);
        return -1;
    }
    dbd->prepared = apr_hash_make(modpool);

    // Prepare the database connection.
    rv = parsegraph_prepareLoginStatements(session);
    if(rv != 0) {
        fprintf(stderr, "Failed preparing user SQL statements, status of %d.\n", rv);
        return -1;
    }

    rv = parsegraph_List_prepareStatements(session);
    if(rv != 0) {
        fprintf(stderr, "Failed preparing list SQL statements, status of %d: %s\n", rv, apr_dbd_error(dbd->driver, dbd->handle, rv));
        return -1;
    }

    parsegraph_EnvironmentStatus erv;
    erv = parsegraph_prepareEnvironmentStatements(session);
    if(erv != parsegraph_Environment_OK) {
        fprintf(stderr, "Failed preparing environment SQL statements, status of %d.\n", rv);
        return -1;
    }

    return 0;
}

void mod_rainback_eachPage(mod_rainback* rb, void(*visitor)(mod_rainback*, const char*, rainback_Page*, void*), void* visitorData)
{
    for(apr_hash_index_t* hi = apr_hash_first(rb->session->pool, rb->cache); hi; hi = apr_hash_next(hi)) {
        const char* url = apr_hash_this_key(hi);
        rainback_Page* page = apr_hash_this_val(hi);
        visitor(rb, url, page, visitorData);
    }
}

static void destroyPage(mod_rainback* rb, const char* url, rainback_Page* page, void* visitorData)
{
    rainback_Page_unref(page);
}

void mod_rainback_destroy(mod_rainback* rb)
{
    mod_rainback_eachPage(rb, destroyPage, 0);
    apr_hash_clear(rb->cache);

    // Disconnect database.
    apr_dbd_close(rb->session->dbd->driver, rb->session->dbd->handle);
    free(rb->session->dbd);

    apr_pool_destroy(rb->session->pool);
    free(rb);
}

static void undertake(marla_Request* req)
{
    if(req->handler) {
        // Destroy any existing handler data.
        req->handler(req, marla_EVENT_DESTROYING, 0, 0);
    }

    mod_rainback* rb = req->cxn->server->undertakerData;
    req->handler = rainback_pageHandler;
    req->handlerData = rainback_getKilledPage(rb, req->error, req->uri);
}

mod_rainback* mod_rainback_new(marla_Server* server)
{
    mod_rainback* rb = malloc(sizeof(*rb));

    apr_pool_t* pool;
    apr_status_t rv;
    if(APR_SUCCESS != apr_pool_create(&pool, 0)) {
        fprintf(stderr, "Failed to create initial pool.\n");
        abort();
    }
    rv = apr_dbd_init(pool);
    if(rv != APR_SUCCESS) {
        fprintf(stderr, "Failed initializing DBD, APR status of %d.\n", rv);
        abort();
    }

    // Initialize DBD.
    ap_dbd_t* dbd = (ap_dbd_t*)apr_palloc(pool, sizeof(ap_dbd_t));
    if(dbd == NULL) {
        fprintf(stderr, "Failed initializing DBD memory");
        abort();
    }
    rb->session = parsegraph_Session_new(pool, dbd);
    rb->session->server = server;
    mod_rainback_prepareDBD(rb->session);

    // Initialize world DBD.
    dbd = (ap_dbd_t*)apr_palloc(pool, sizeof(ap_dbd_t));
    if(dbd == NULL) {
        fprintf(stderr, "Failed initializing DBD memory");
        abort();
    }
    rb->worldSession = parsegraph_Session_new(pool, dbd);
    rb->worldSession->server = server;
    mod_rainback_prepareDBD(rb->worldSession);
    rb->cache = apr_hash_make(rb->session->pool);

    return rb;
}

void mod_rainback_init(struct marla_Server* server, enum marla_ServerModuleEvent e)
{
    mod_rainback* rb;
    switch(e) {
    case marla_EVENT_SERVER_MODULE_START:
        rb = mod_rainback_new(server);
        server->undertaker = undertake;
        server->undertakerData = rb;
        marla_Server_addHook(server, marla_ServerHook_ROUTE, mod_rainback_route, rb);
        //printf("mod_rainback loaded.\n");
        break;
    case marla_EVENT_SERVER_MODULE_STOP:
        mod_rainback_destroy(rb);
        break;
    }
}
