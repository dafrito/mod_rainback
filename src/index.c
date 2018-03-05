#include "httpd.h"
#include "http_config.h"
#include "http_protocol.h"
#include "ap_config.h"
#include "util_cookies.h"
#include "util_script.h"
#include "parsegraph_user.h"
#include "parsegraph_user_httpd.h"
#include <http_log.h>

static int parsegraph_index_html_handler(request_rec *r)
{
    if(strcmp(r->handler, "parsegraph_index_html")) {
        return DECLINED;
    }

//    ap_log_perror(
//        APLOG_MARK, APLOG_INFO, 0, r->pool, "parsegraph_index_html handling request"
//    );
    r->status_line = 0;

    // Indicate HTML as the returned data.
    r->content_type = "text/html";

    // Retrieve and validate the session token and selector.
    struct parsegraph_user_login* userLogin = 0;
    parsegraph_UserStatus rv = parsegraph_authenticate(r, &userLogin);
    if(parsegraph_isSeriousUserError(rv)) {
        ap_log_perror(
            APLOG_MARK, APLOG_ERR, 0, r->pool, "Serious error while authenticating user: %s", parsegraph_nameUserStatus(rv)
        );
        return 500;
    }

    if(!r->header_only) {
        ap_rputs("<!DOCTYPE html><html><head><title>", r);
        ap_rputs("Parsegraph", r);
        ap_rputs("</title>", r);
        ap_rputs("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">", r);
        ap_rputs("<script src=\"parsegraph-1.0.js\"></script>", r);
        ap_rputs("<script src=\"parsegraph-widgets-1.0.js\"></script>", r);
        ap_rputs("<link rel=stylesheet href='sga.css'/>", r);
        ap_rputs("</head>", r);
        ap_rputs("<body>", r);
        if(r->status_line) {
            ap_rputs(r->status_line, r);
        }
        ap_rputs("<h1>Parsegraph.com!</h1>", r);
        ap_rputs("<ul><li>Parsegraph <a href='/environment'>Environments</a>", r);
        ap_rputs("<li>Parsegraph <a href='/doc'>Documentation</a></ul>", r);
        if(r->user) {
            ap_rprintf(r, "You are logged in as <a href=/user>%s</a>. ", r->user);
            ap_rputs("<form method=post action='?command=parsegraph_changeUserPassword'>", r);
            ap_rputs("<label for=username>Username:</label><input type=text name=username><br>", r);
            ap_rputs("<label for=password>Password:</label><input type=password name=password><br>", r);
            ap_rputs("<input type=submit value='Change password'>", r);
            ap_rputs("</form>", r);
        }
        else {
            ap_rputs("You are not logged in. ", r);
            ap_rputs("<a href=/user>Log in</a> or ", r);
            ap_rputs("<a href=/user>Create a new user</a>", r);
        }
        ap_rputs("</body></html>", r);
    }

    return OK;
}

static void parsegraph_index_html_register_hooks(apr_pool_t *pool)
{
    ap_hook_handler(parsegraph_index_html_handler, NULL, NULL, APR_HOOK_MIDDLE);
}

/* Dispatch list for API hooks */
module AP_MODULE_DECLARE_DATA parsegraph_index_html_module = {
    STANDARD20_MODULE_STUFF,
    NULL,                  /* create per-dir    config structures */
    NULL,                  /* merge  per-dir    config structures */
    NULL,                  /* create per-server config structures */
    NULL,                  /* merge  per-server config structures */
    NULL,                  /* table of config file commands       */
    parsegraph_index_html_register_hooks  /* register hooks       */
};
