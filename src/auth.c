#include "mod_rainback.h"
#include <parsegraph_user.h>
#include <apr_pools.h>

int rainback_processCookie(marla_Request* req, apr_pool_t* pool, ap_dbd_t* dbd, parsegraph_user_login* login, char* cookie)
{
    int cookie_len = strlen(cookie);
    int cookieType = 0;
    char* cookie_saveptr;
    char* tok = strtok_r(cookie, ";", &cookie_saveptr);

    while(tok) {
        // Only one cookie
        char* partSavePtr;
        char* partTok = strtok_r(tok, "=", &partSavePtr);
        if(!partTok) {
            return 1;
        }
        if(!strcmp(partTok, "session")) {
            cookieType = 1;
        }
        else {
            cookieType = 0;
        }

        partTok = strtok_r(0, ";", &partSavePtr);
        if(!partTok) {
            return 1;
        }

        if(cookieType == 1) {
            // This cookie value is the session identifier; authenticate.
            marla_logMessagef(req->cxn->server, "Found session cookie: %s", partTok);
            char* sessionValue = partTok;
            login->username = 0;
            login->userId = -1;
            if(sessionValue && 0 == parsegraph_deconstructSessionString(pool, sessionValue, &login->session_selector, &login->session_token)) {
                parsegraph_UserStatus rv = parsegraph_refreshUserLogin(pool, dbd, login);
                if(rv != parsegraph_OK) {
                    marla_killRequest(req, "Failed to refresh session's login: %s", parsegraph_nameUserStatus(rv));
                    return 1;
                }

                parsegraph_UserStatus idRV = parsegraph_getIdForUsername(pool, dbd, login->username, &(login->userId));
                if(parsegraph_isSeriousUserError(idRV)) {
                    marla_killRequest(req, "Failed to retrieve ID for authenticated login.");
                    return 1;
                }
            }
            if(!login->username) {
                marla_killRequest(req, "Session does not match any user.");
                return 1;
            }
            marla_logMessagef(req->cxn->server, "Session matched user %s", login->username);
            return 0;
        }
        else {
            // Do nothing.
        }

        tok = strtok_r(0, ";", &cookie_saveptr);
    }

    return 1;
}
