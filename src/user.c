#include "httpd.h"
#include "http_config.h"
#include "apr_strings.h"
#include "apr_escape.h"
#include "http_protocol.h"
#include "ap_config.h"
#include "util_cookies.h"
#include "util_script.h"
#include "parsegraph_user.h"
#include "parsegraph_user_httpd.h"
#include <http_log.h>

static int parsegraph_flatten_form_data(request_rec* r, apr_table_t* form_data)
{
    apr_array_header_t *pairs;
    int rv = ap_parse_form_data(r, NULL, &pairs, -1, HUGE_STRING_LEN);
    if(rv != OK) {
        return rv;
    }

    apr_off_t len;
    apr_size_t size;
    char *buffer;
    while(pairs && !apr_is_empty_array(pairs)) {
        ap_form_pair_t *pair = (ap_form_pair_t *) apr_array_pop(pairs);
        apr_brigade_length(pair->value, 1, &len);
        size = (apr_size_t)len;
        buffer = apr_palloc(r->pool, size + 1);
        apr_brigade_flatten(pair->value, buffer, &size);
        buffer[len] = 0;
        apr_table_set(form_data, pair->name, buffer);
    }

    return 0;
}

static int parsegraph_setUserProfile_handler(request_rec* r)
{
    // Retrieve and validate the session token and selector.
    struct parsegraph_user_login* userLogin = 0;
    parsegraph_UserStatus rv = parsegraph_authenticate(r, &userLogin);
    if(parsegraph_isSeriousUserError(rv)) {
        ap_log_perror(
            APLOG_MARK, APLOG_ERR, 0, r->pool, "Serious error while authenticating user: %s", parsegraph_nameUserStatus(rv)
        );
        return 500;
    }

    // Disallow anonymous use.
    if(!r->user) {
        return HTTP_UNAUTHORIZED;
    }

    apr_table_t* form_data = apr_table_make(r->pool, 1);
    rv = parsegraph_flatten_form_data(r, form_data);
    if(rv != 0) {
        return rv;
    }
    const char* profile = apr_table_get(form_data, "profile");
    if(!profile) {
        return 500;
    }
    profile = apr_punescape_url(r->pool, profile, 0, 0, 0);
    if(!profile) {
        return 500;
    }

    ap_dbd_t* dbd = ap_dbd_acquire(r);
    if(!dbd) {
        ap_log_perror(
            APLOG_MARK, APLOG_ERR, 0, r->pool, "DBD must not be null."
        );
        return 500;
    }

    if(0 != parsegraph_setUserProfile(
        r->pool, dbd, r->user, profile
    )) {
        ap_log_perror(
            APLOG_MARK, APLOG_ERR, 0, r->pool, "Failed to set user profile."
        );
        return 500;
    }

    return OK;
}

static int parsegraph_user_html_handler(request_rec *r)
{
    if(strcmp(r->handler, "parsegraph_user")) {
        return DECLINED;
    }

    const char* acceptHeader = apr_table_get(r->headers_in, "Accept");
    /*if(acceptHeader) {
        ap_log_perror(
            APLOG_MARK, APLOG_ERR, 0, r->pool, "%s", acceptHeader
        );
    }
    else {
        ap_log_perror(
            APLOG_MARK, APLOG_ERR, 0, r->pool, "No accept header"
        );
    }*/

    if(!acceptHeader || ((NULL == strstr(acceptHeader, "*/*")) && (NULL == strstr(acceptHeader, "text/html")))) {
        return DECLINED;
    }

    // Indicate HTML as the returned data.
    r->content_type = "text/html";

    ap_log_perror(
        APLOG_MARK, APLOG_INFO, 0, r->pool, "parsegraph_user_html handling request"
    );
    r->status_line = 0;

    // Retrieve and validate the session token and selector.
    struct parsegraph_user_login* userLogin = 0;
    parsegraph_UserStatus rv = parsegraph_authenticate(r, &userLogin);
    if(parsegraph_isSeriousUserError(rv)) {
        ap_log_perror(
            APLOG_MARK, APLOG_ERR, 0, r->pool, "Serious error while authenticating user: %s", parsegraph_nameUserStatus(rv)
        );
        return 500;
    }

    ap_dbd_t* dbd = ap_dbd_acquire(r);
    if(!dbd) {
        ap_log_perror(
            APLOG_MARK, APLOG_ERR, 0, r->pool, "DBD must not be null."
        );
        return 500;
    }

    apr_table_t* queryArgs;
    ap_args_to_table(r, &queryArgs);
    struct parsegraph_user_login* user_login;

    if(r->method_number == M_POST) {
        const char* command = apr_table_get(queryArgs, "command");

        if(!strcmp("parsegraph_beginUserLogin", command)) {
            const char* username = 0;
            const char* password = 0;
            apr_table_t* form_data = apr_table_make(r->pool, 2);
            int irv = parsegraph_flatten_form_data(r, form_data);
            if(irv != 0) {
                return irv;
            }
            username = apr_table_get(form_data, "username");
            password = apr_table_get(form_data, "password");

            // Attempt the user login
            parsegraph_UserStatus rv = parsegraph_beginUserLogin(
                r->pool, dbd, username, password, &user_login
            );
            if(rv != parsegraph_OK) {
                ap_log_perror(
                    APLOG_MARK, APLOG_ERR, 0, r->pool, "Failed to begin user login: %s", parsegraph_nameUserStatus(rv)
                );
                r->status_line = "Failed to create initial user login.";
                return 500;
            }

            rv = parsegraph_setSession(r, user_login);
            if(rv != parsegraph_OK) {
                ap_log_perror(
                    APLOG_MARK, APLOG_ERR, 0, r->pool, "Failed to set user session. %s", parsegraph_nameUserStatus(rv)
                );
                return 500;
            }
        }
        else if(!strcmp("parsegraph_createNewUser", command)) {
            const char* username = 0;
            const char* password = 0;
            apr_table_t* form_data = apr_table_make(r->pool, 2);
            int rv = parsegraph_flatten_form_data(r, form_data);
            if(rv != 0) {
                return rv;
            }
            username = apr_table_get(form_data, "username");
            password = apr_table_get(form_data, "password");

            // Create the user.
            struct parsegraph_user_login* user_login;
            rv = parsegraph_createNewUser(
                r->pool, dbd, username, password
            );
            if(0 != rv) {
                ap_log_perror(
                    APLOG_MARK, APLOG_ERR, 0, r->pool, "Failed to create new user. %s", parsegraph_nameUserStatus(rv)
                );
                return 500;
            }

            // Begin the user login.
            rv = parsegraph_beginUserLogin(
                r->pool, dbd, username, password, &user_login
            );
            if(0 != rv) {
                if(rv != HTTP_UNAUTHORIZED) {
                    ap_log_perror(
                        APLOG_MARK, APLOG_ERR, 0, r->pool, "Failed to begin session for new user. %s", parsegraph_nameUserStatus(rv)
                    );
                    return 500;
                }
                r->status_line = "Failed to log in with given credentials.";
            }
            else if(0 != parsegraph_setSession(r, user_login)) {
                ap_log_perror(
                    APLOG_MARK, APLOG_ERR, 0, r->pool, "Failed to set user session."
                );
                return 500;
            }
        }
        else if(!strcmp("parsegraph_setUserProfile", command)) {
            int rv = parsegraph_setUserProfile_handler(r);
            if(rv != OK) {
                ap_log_perror(
                    APLOG_MARK, APLOG_ERR, 0, r->pool, "Failed to set user profile."
                );
                return 500;
            }
        }
        else if(!strcmp("parsegraph_endUserLogin", command)) {
            // Disallow anonymous use.
            if(!r->user) {
                ap_log_perror(
                    APLOG_MARK, APLOG_ERR, 0, r->pool, "Refusing endUserLogin service for anonymous user."
                );
                return HTTP_UNAUTHORIZED;
            }

            // Default to removing the current user.
            const char* username = apr_table_get(queryArgs, "username");
            if(!username) {
                username = r->user;
            }

            // Disallow ending other user's logins.
            if(strcmp(r->user, username)) {
                return HTTP_BAD_REQUEST;
            }

            ap_dbd_t* dbd = ap_dbd_acquire(r);
            if(!dbd) {
                ap_log_perror(
                    APLOG_MARK, APLOG_ERR, 0, r->pool, "DBD must not be null."
                );
                return 500;
            }

            int logins_ended = 0;
            if(0 != parsegraph_endUserLogin(
                r->pool, dbd, username, &logins_ended
            )) {
                ap_log_perror(
                    APLOG_MARK, APLOG_ERR, 0, r->pool, "Failed to end user logins."
                );
                return 500;
            }

            rv = parsegraph_removeSession(r);
            if(0 != rv) {
                return rv;
            }

            // See other.
            r->status = 303;
            apr_table_set(r->headers_out, "Location", "/");
            return OK;
        }

        // See other.
        r->status = 303;
        apr_table_set(r->headers_out, "Location", "/user");
        return OK;
    }

    if(!r->header_only) {
        ap_rputs("<!DOCTYPE html><html><head><title>", r);
        ap_rputs("Parsegraph", r);
        ap_rputs("</title>", r);
        ap_rputs("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">", r);
        ap_rputs("<script src=\"parsegraph-1.2.js\"></script>", r);
        ap_rputs("</head>", r);
        ap_rputs("<body>", r);
        if(r->status_line) {
            ap_rputs(r->status_line, r);
        }
        ap_rputs("<h1>Parsegraph.com!</h1>", r);
        ap_rputs("<a href=/>Return to homepage.</a><p>", r);
        if(r->user) {
            const char* profile;
            parsegraph_getUserProfile(r->pool, dbd, r->user, &profile);
            ap_rprintf(r, "You are logged in as %s", r->user);
            ap_rputs("<form method='post' action='?command=parsegraph_setUserProfile'>", r);
            ap_rputs("<textarea name='profile' style='width:400px;height:400px'>", r);
            if(profile) {
                ap_rputs(profile, r);
            }
            ap_rputs("</textarea><br/>", r);
            ap_rputs("<input type=submit value='Set user profile'>", r);
            ap_rputs("</form>", r);
            if(profile) {
                ap_rputs(profile, r);
            }
            ap_rputs("<form method='post' action='?command=parsegraph_endUserLogin'>", r);
            ap_rputs("<input type=submit value='Log out'>", r);
            ap_rputs("</form>", r);
        }
        else {
            ap_rputs("You are not logged in.", r);
            ap_rputs("<form method=post action='?command=parsegraph_beginUserLogin'>", r);
            ap_rputs("<label for=username>Username:</label><input type=text name=username><br>", r);
            ap_rputs("<label for=password>Password:</label><input type=password name=password><br>", r);
            ap_rputs("<input type=submit value='Log in'>", r);
            ap_rputs("</form>", r);
            ap_rputs("<br/>", r);
            ap_rputs("<form method=post action='?command=parsegraph_createNewUser'>", r);
            ap_rputs("<label for=username>Username:</label><input type=text name=username><br>", r);
            ap_rputs("<label for=password>Password:</label><input type=password name=password><br>", r);
            ap_rputs("<input type=submit value='Create new user'>", r);
            ap_rputs("</form>", r);
        }
        ap_rputs("</body></html>", r);
    }

    return OK;
}

static void parsegraph_user_html_register_hooks(apr_pool_t *pool)
{
    ap_hook_handler(parsegraph_user_html_handler, NULL, NULL, APR_HOOK_MIDDLE);
}

/* Dispatch list for API hooks */
module AP_MODULE_DECLARE_DATA parsegraph_user_html_module = {
    STANDARD20_MODULE_STUFF,
    NULL,                  /* create per-dir    config structures */
    NULL,                  /* merge  per-dir    config structures */
    NULL,                  /* create per-server config structures */
    NULL,                  /* merge  per-server config structures */
    NULL,                  /* table of config file commands       */
    parsegraph_user_html_register_hooks  /* register hooks       */
};
