#include <sqlite3.h>
#include <unistd.h>
#include "mod_rainback.h"
#include "marla.h"

struct parsegraph_live_session {
    mod_rainback* rb;
    char error[256];
    size_t errorBufSize;
    int closed;
    apr_pool_t* pool;
    int processHead;
    parsegraph_user_login login;
    parsegraph_GUID env;
    size_t envReceived;
    struct printing_item* initialData;
};

int openWorldStreams = 0;

static int acquireWorldStream();
static int releaseWorldStream(int commit);

static void
callback_parsegraph_environment(struct marla_Request* req, enum marla_ClientEvent reason, void *in, int len)
{
    marla_Server* server = req->cxn->server;
    parsegraph_live_session* resp = req->handlerData;

    int neededEnvLength = 36;
    int m, rv;
    static unsigned char buf[MAX_INIT_LENGTH];
    switch(reason) {
    case marla_EVENT_WEBSOCKET_CLOSE_REASON:
        marla_logMessagef(server, "environment_ws CLOSE_REASON");
        if(len == 0 && resp->initialData != 0) {
            if(resp->initialData->stage != 3 && !resp->initialData->error) {
                // World streaming interrupted, decrement usage counter.
                resp->initialData->error = 1;
                if(releaseWorldStream(1) != 0) {
                    return;
                }
            }

            free(resp->initialData);
            resp->initialData = 0;
        }
        return;

    case marla_EVENT_WEBSOCKET_MUST_READ:
        marla_logMessagef(server, "environment_ws MUST_READ");
        if(resp->envReceived < neededEnvLength) {
            if(resp->envReceived + len < neededEnvLength) {
                // Copy the whole thing.
                memcpy(resp->env.value + resp->envReceived, in, len);
                resp->envReceived += len;
                // Await more data.
                marla_logMessagef(server, "Awaiting rest of environment");
                return;
            }
            else {
                // Otherwise, just copy the needed portion
                memcpy(resp->env.value + resp->envReceived, in, neededEnvLength - resp->envReceived);
                in += (neededEnvLength - resp->envReceived);
                len -= (neededEnvLength - resp->envReceived);
                resp->envReceived = neededEnvLength;
                marla_logMessagef(server, "Got the whole environment");
            }
            if(len == 0) {
                // Only the environment received.
                return;
            }
        }

        if(len > 0) {
            // TODO Handle storage event commands
            // TODO Handle environment event commands
            // TODO Handle chat event commands

            // Extraneous data should cause an error.
            strcpy(resp->error, "Received extraneous data.");
        }
        return;

    case marla_EVENT_HEADER:
        if(!strcmp("Cookie", in)) {
            char* cookie = in + len;
            int cookie_len = strlen(cookie);
            int cookieType = 0;
            char* cookie_saveptr;
            char* tok = strtok_r(cookie, ";", &cookie_saveptr);

            while(tok) {
                // Only one cookie
                char* partSavePtr;
                char* partTok = strtok_r(tok, "=", &partSavePtr);
                if(!partTok) {
                    return;
                }
                if(!strcmp(partTok, "session")) {
                    cookieType = 1;
                }
                else {
                    cookieType = 0;
                }

                partTok = strtok_r(0, ";", &partSavePtr);
                if(!partTok) {
                    return;
                }

                if(cookieType == 1) {
                    // This cookie value is the session identifier; authenticate.
                    marla_logMessagef(req->cxn->server, "Found session cookie: %s", partTok);
                    char* sessionValue = partTok;
                    resp->login.username = 0;
                    resp->login.userId = -1;
                    if(sessionValue && 0 == parsegraph_deconstructSessionString(resp->rb->session, sessionValue, &resp->login.session_selector, &resp->login.session_token)) {
                        parsegraph_UserStatus rv = parsegraph_refreshUserLogin(resp->rb->session, &resp->login);
                        if(rv != parsegraph_OK) {
                            marla_logMessagef(req->cxn->server, "Failed to refresh session's login: %s", parsegraph_nameUserStatus(rv));
                            strcpy(resp->error, parsegraph_nameUserStatus(rv));
                            return;
                        }

                        parsegraph_UserStatus idRV = parsegraph_getIdForUsername(resp->rb->session, resp->login.username, &(resp->login.userId));
                        if(parsegraph_isSeriousUserError(idRV)) {
                            marla_logMessagef(req->cxn->server, "Failed to retrieve ID for authenticated login.");
                            strcpy(resp->error, "Failed to retrieve ID for authenticated login.\n");
                            return;
                        }
                    }
                    if(!resp->login.username) {
                        marla_logMessagef(req->cxn->server, "Session does not match any user.");
                        strcpy(resp->error, "Session does not match any user.");
                        return;
                    }
                    marla_logMessagef(req->cxn->server, "Session matched user %s", resp->login.username);
                    break;
                }
                else {
                    // Do nothing.
                }

                tok = strtok_r(0, ";", &cookie_saveptr);
            }
        }
        break;

    case marla_EVENT_ACCEPTING_REQUEST:
        marla_logMessagef(server, "environment_ws ACCEPTING_REQUEST.");
        *((int*)in) = 1;
        break;

    case marla_EVENT_WEBSOCKET_MUST_WRITE:
        marla_logMessagef(server, "environment_ws MUST_WRITE");
        if(strlen(resp->error) > 0) {
            if(!resp->closed) {
                resp->closed = 1;
            }
            marla_logMessagef(req->cxn->server, "Closing connection. %s", resp->error);
            marla_closeWebSocketRequest(req, 1000, resp->error, strlen(resp->error));
            return;
        }
        if(resp->envReceived < neededEnvLength) {
            marla_logMessagef(req->cxn->server, "environment_ws still needs to receive the requested environment ID");
            goto choked;
        }
        if(!resp->login.username) {
            strcpy(resp->error, "Session is not logged in.");
            goto choked;
        }
        if(!resp->initialData) {
            if(0 != acquireWorldStream()) {
                strcpy(resp->error, "Failed to begin transaction to initialize user.");
                goto choked;
            }

            resp->initialData = malloc(sizeof(struct printing_item));
            resp->initialData->error = 0;
            resp->initialData->stage = 0;
            resp->initialData->values = 0;
            resp->initialData->nvalues = 0;
            resp->initialData->index = 0;
            resp->initialData->nextLevel = 0;
            resp->initialData->parentLevel = 0;
            if(0 != parsegraph_getEnvironmentRoot(resp->rb->session, &resp->env, &resp->initialData->listId)) {
                strcpy(resp->error, "Error retrieving environment root.");
                resp->initialData->error = 1;
                goto choked;
            }
        }
        if(resp->initialData->error) {
            goto choked;
        }
        if(resp->initialData->listId == 0) {
            if(0 != parsegraph_prepareEnvironment(resp)) {
                goto choked;
            }
        }
        while(resp->initialData->stage != 3) {
            switch(parsegraph_printItem(req, resp, resp->initialData)) {
            case 0:
                // Done!
                if(releaseWorldStream(1) != 0) {
                    releaseWorldStream(0);
                    resp->initialData->error = 1;
                    strcpy(resp->error, "Failed to commit prepared environment.");
                    goto choked;
                }
                marla_logMessage(server, "Finished writing initial environment.");
                break;
            case -1:
                // Choked.
                {
                    int nflushed;
                    marla_Connection_flush(req->cxn, &nflushed);
                    if(nflushed == 0) {
                        goto choked;
                    }
                    continue;
                }
            case -2:
            default:
                // Died.
                releaseWorldStream(0);
                resp->initialData->error = 1;
                strcpy(resp->error, "Failed to prepare environment.");
                goto choked;
            }
        }

        // TODO Get storage event log since last transmit.
            // For each event, interpret and send to user.
            // Update event counter.
            // Stop if choked.

        // TODO Get environment event log since last transmit.
            // For each event, interpret and send to user.
            // Update event counter.
            // Stop if choked.

        // TODO Get all chat logs for user since last transmit.
            // For each event, interpret and send to user.
            // Update event counter.
            // Stop if choked.
        break;
    default:
        break;
    }

    return;
choked:
    marla_logMessagef(req->cxn->server, "environment_ws choked. %s", resp->error);
    (*(int*)in) = -1;
    if(strlen(resp->error) > 0 && !resp->closed) {
        marla_logMessagef(req->cxn->server, "Closing connection. %s", resp->error);
        marla_closeWebSocketRequest(req, 1000, resp->error, strlen(resp->error));
        resp->closed = 1;
    }
}

void rainback_live_environment_install(mod_rainback* rb, marla_Request* req)
{
    struct parsegraph_live_session* hd = malloc(sizeof *hd);
    req->handlerData = hd;
    req->handler = callback_parsegraph_environment;
    // Initialize the session structure.
    marla_logMessagef(req->cxn->server, "Routing /environment/live websocket connection");
    if(0 != initialize_parsegraph_live_session(hd, rb)) {
        //strcpy(resp->error, "Error initializing session.");
        //return;
    }
}

static int destroy_module(struct marla_Server* server)
{
    // Close the world streaming DBD connection.
    int rv = apr_dbd_close(worldStreamDBD->driver, worldStreamDBD->handle);
    if(rv != APR_SUCCESS) {
        fprintf(stderr, "Failed closing world streaming database connection, APR status of %d.\n", rv);
        return -1;
    }

    // Close the control DBD connection.
    rv = apr_dbd_close(controlDBD->driver, controlDBD->handle);
    if(rv != APR_SUCCESS) {
        fprintf(stderr, "Failed closing control database connection, APR status of %d.\n", rv);
        return -1;
    }

    apr_pool_destroy(modpool);
    modpool = NULL;

    return 0;
}

static int acquireWorldStream()
{
    if(openWorldStreams++ == 0) {
        int nrows;
        int dbrv = apr_dbd_query(worldStreamDBD->driver, worldStreamDBD->handle, &nrows, "BEGIN IMMEDIATE");
        if(dbrv != 0) {
            openWorldStreams = 0;
            return -1;
        }
    }
    return 0;
}

static int releaseWorldStream(int commit)
{
    if(openWorldStreams == 0) {
        // Invalid.
        return -1;
    }
    if(--openWorldStreams > 0) {
        return 0;
    }

    int nrows;
    int dbrv = apr_dbd_query(worldStreamDBD->driver, worldStreamDBD->handle, &nrows, commit ? "COMMIT" : "ROLLBACK");
    if(dbrv != 0) {
        return -1;
    }
    return 0;
}

char data_version_storage[255];
int cb_get_data_version(void* userdata, int numCols, char** values, char** columnName)
{
    strncpy(data_version_storage, values[0], 254);
    fprintf(stderr, values[0]);
    return 0;
}

void run_conn_sql(const char* sql)
{
    sqlite3_exec(apr_dbd_native_handle(controlDBD->driver, controlDBD->handle), sql, cb_get_data_version, 0, 0);
}

int initialize_parsegraph_live_session(parsegraph_live_session* session, mod_rainback* rb)
{
    parsegraph_guid_init(&session->env);
    session->rb = rb;
    session->envReceived = 0;
    session->errorBufSize = 255;
    memset(session->error, 0, session->errorBufSize + 1);
    session->closed = 0;
    session->initialData = 0;
    int rv = apr_pool_create(&session->pool, 0);
    if(0 != rv) {
        return -1;
    }
    return 0;
}

int parsegraph_prepareEnvironment(parsegraph_live_session* resp)
{
    parsegraph_Session* session = resp->rb->worldSession;
    if(parsegraph_OK != parsegraph_beginTransaction(session, resp->env.value)) {
        strcpy(resp->error, "Failed to begin transaction for preparing environment.");
        return -1;
    }

    // Prepare the environment.
    if(parsegraph_List_OK != parsegraph_List_new(session, resp->env.value, &resp->initialData->listId)) {
        resp->initialData->listId = 0;
        strcpy(resp->error, "Failed to prepare environment.");
        resp->initialData->error = 1;
        parsegraph_rollbackTransaction(session, resp->env.value);
        return -1;
    }

    if(parsegraph_Environment_OK != parsegraph_setEnvironmentRoot(session, &resp->env, resp->initialData->listId)) {
        resp->initialData->listId = 0;
        resp->initialData->error = 1;
        strcpy(resp->error, "Failed to prepare environment.");
        parsegraph_rollbackTransaction(session, resp->env.value);
        return -1;
    }

    int metaList;
    if(parsegraph_List_OK != parsegraph_List_appendItem(session, resp->initialData->listId, parsegraph_BlockType_MetaList, "", &metaList)) {
        resp->initialData->error = 1;
        strcpy(resp->error, "Failed to prepare environment.");
        parsegraph_rollbackTransaction(session, resp->env.value);
        return -1;
    }

    int worldList;
    if(parsegraph_List_OK != parsegraph_List_appendItem(session, resp->initialData->listId, parsegraph_BlockType_WorldList, "", &worldList)) {
        resp->initialData->error = 1;
        strcpy(resp->error, "Failed to prepare environment.");
        parsegraph_rollbackTransaction(session, resp->env.value);
        return -1;
    }

    char buf[1024];
    char nbuf[1024];
    int numMultislots = 4 + rand() % 64;
    int child;
    for(int i = 0; i < numMultislots; ++i) {
        int rowSize = 1 + rand() % 32;
        int columnSize = 1 + rand() % 24;
        snprintf(buf, sizeof(buf), "[%d, %d, %d, %d, %d, %d]", rand() % 4, rowSize, columnSize, 55 + rand() % 200, 55 + rand() % 200, 55 + rand() % 200);
        if(parsegraph_List_OK != parsegraph_List_appendItem(session, worldList, parsegraph_BlockType_Multislot, buf, &child)) {
            resp->initialData->error = 1;
            strcpy(resp->error, "Failed to prepare environment.");
            parsegraph_rollbackTransaction(session, resp->env.value);
            return -1;
        }
    }

    if(parsegraph_OK != parsegraph_commitTransaction(session, resp->env.value)) {
        parsegraph_rollbackTransaction(session, resp->env.value);
        resp->initialData->error = 1;
        strcpy(resp->error, "Failed to prepare environment.");
        return -1;
    }
    return 0;
}

int parsegraph_printItem(marla_Request* req, parsegraph_live_session* resp, struct printing_item* level)
{
    //fprintf(stderr, "PRINTING item\n");
    static char buf[65536];
    if(!level || level->error) {
        return -2;
    }

    // Get to the deepest level.
    while(level->nextLevel != 0) {
        level = level->nextLevel;
    }

    while(level != 0) {
        if(level->stage == 0) {
            // Print the item.
            const char* value;
            int typeId;
            if(0 != parsegraph_List_getName(resp->rb->worldSession, level->listId, &value, &typeId)) {
                //lwsl_err("Error encountered retrieving list name.\n");
                goto die;
            }
            if(value == 0) {
                value = "";
                typeId = 0;
            }
            int written = snprintf(buf, sizeof(buf), "{\"id\":%d, \"value\":\"%s\", \"type\":%d, \"items\":[", level->listId, value, typeId);
            if(written < 0) {
                //lwsl_err("Failed to write to string buffer.\n");
                goto die;
            }
            if(written >= sizeof(buf)) {
                //lwsl_err("String buffer overflowed.\n");
                goto die;
            }

            //fprintf(stderr, "printing stage 1\n");
            int hlen = marla_writeWebSocketHeader(req, 1, written);
            if (hlen < 0) {
                goto choked;
            }
            int nwritten = marla_Connection_write(req->cxn, buf, written);
            if(nwritten < written) {
                if(nwritten > 0) {
                    marla_Connection_putbackWrite(req->cxn, hlen + nwritten);
                }
                else {
                    marla_Connection_putbackWrite(req->cxn, hlen);
                }
                goto choked;
            }
            // Item printed.

            //lwsl_err("stage 1 complete\n");
            level->stage = 1;
        }

        if(level->stage == 1) {
            // Ready to print any children.
            //lwsl_err("beginning stage 2\n");
            if(!level->values) {
                int rv = parsegraph_List_listItems(resp->rb->worldSession, level->listId, &level->values, &level->nvalues);
                if(rv != 0) {
                    //lwsl_err("Failed to list items for item.\n");
                    goto die;
                }
            }

            //fprintf(stderr, "printing stage 2\n");
            if(level->index < level->nvalues) {
                // Create the level for the child.
                struct printing_item* subLevel = malloc(sizeof(*level));
                if(!subLevel) {
                    //lwsl_err("Memory allocation error.");
                    goto die;
                }
                // Retrieve the id of the child from the list.
                subLevel->listId = level->values[level->index++]->id;
                subLevel->parentLevel = level;
                subLevel->nextLevel = 0;
                subLevel->stage = 0;
                subLevel->error = 0;
                subLevel->index = 0;
                subLevel->values = 0;
                subLevel->nvalues = 0;
                level->nextLevel = subLevel;

                // Iterate to the child.
                level = subLevel;
                continue;
            }

            // All children written.
            level->stage = 2;
        }

        if(level->stage == 2) {
            // Ready to print end of child list and object.
            //fprintf(stderr, "printing stage 3\n");
            int written;
            if(level->parentLevel && (level->parentLevel->index < level->parentLevel->nvalues)) {
                strcpy(buf, "]},");
                int hlen = marla_writeWebSocketHeader(req, 1, strlen("]},"));
                if(hlen < 0) {
                    goto choked;
                }
                written = marla_Connection_write(req->cxn, buf, strlen("]},"));
                if(written < strlen("]},")) {
                    if(written > 0) {
                        marla_Connection_putbackWrite(req->cxn, written + hlen);
                    }
                    else {
                        marla_Connection_putbackWrite(req->cxn, hlen);
                    }
                    goto choked;
                }
            }
            else {
                strcpy(buf, "]}");
                int hlen = marla_writeWebSocketHeader(req, 1, strlen("]}"));
                if(hlen < 0) {
                    goto choked;
                }
                written = marla_Connection_write(req->cxn, buf, strlen("]}"));
                if(written < strlen("]}")) {
                    if(written > 0) {
                        marla_Connection_putbackWrite(req->cxn, written + hlen);
                    }
                    else {
                        marla_Connection_putbackWrite(req->cxn, hlen);
                    }
                    goto choked;
                }
            }
            //lwsl_err("reached stage 3\n");

            // Suffix printed.
            level->stage = 3;
        }

        if(level->stage != 3) {
            goto choked;
        }

        // Move to the parent and free this level.
        struct printing_item* childLevel = level;
        level = level->parentLevel;
        if(level) {
            free(childLevel);
            level->nextLevel = 0;
        }
    }

    return 0;
choked:
    if(resp->initialData->stage == 3) {
        return 0;
    }
    return -1;
die:
    level->error = 1;
    struct printing_item* par = level->parentLevel;
    while(par != 0) {
        par->error = 1;
        par = par->parentLevel;
    }
    return -2;
}
