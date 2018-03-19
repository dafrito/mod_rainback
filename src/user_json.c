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

struct handler_list {
    apr_hash_t* commands;
    int (**handlers)(request_rec*);
};

static int parsegraph_handler_list_make(
    apr_pool_t* pool,
    const char** commandNames,
    int (**handlers)(request_rec*),
    size_t numCommands,
    struct handler_list** commands
)
{
    *commands = apr_pcalloc(pool, sizeof(struct handler_list));
    (*commands)->commands = apr_hash_make(pool);
    (*commands)->handlers = handlers;

    for(size_t i = 0; i < numCommands; ++i) {
        // Make commands one-based, to allow for NULL to be interpreted as empty.
        apr_hash_set((*commands)->commands, commandNames[i], APR_HASH_KEY_STRING, (void*)i);
    }

    return 0;
}

static int parsegraph_handler_list_select(
    struct handler_list* commands,
    const char* command,
    size_t command_size,
    int(**handler)(request_rec*)
)
{
    void* res = apr_hash_get(commands->commands, command, command_size);
    if(res == NULL) {
        return 500;
    }
    *handler = commands->handlers[(size_t)res];
    return 0;
}

static int parsegraph_createNewUser_handler(request_rec* r)
{
    ap_dbd_t* dbd = ap_dbd_acquire(r);
    if(!dbd) {
        ap_log_perror(
            APLOG_MARK, APLOG_ERR, 0, r->pool, "DBD must not be null."
        );
        return 500;
    }

    const char* username = 0;
    const char* password = 0;
    apr_table_t* form_data = apr_table_make(r->pool, 2);
    int dbrv = parsegraph_flatten_form_data(r, form_data);
    if(dbrv != 0) {
        return dbrv;
    }
    username = apr_table_get(form_data, "username");
    password = apr_table_get(form_data, "password");

    parsegraph_UserStatus rv = parsegraph_createNewUser(
        r->pool,
        dbd,
        username,
        password
    );
    if(rv != parsegraph_OK) {
        goto fail;
    }

    struct parsegraph_user_login* user_login;
    rv = parsegraph_beginUserLogin(
        r->pool, dbd, username, password, &user_login
    );
    if(rv != parsegraph_OK) {
        goto fail;
    }

    if(!r->header_only) {
        ap_rputs("{\"result\":\"User created.", r);
        ap_rprintf(r, "\",\"username\":\"%s\",", user_login->username);
        ap_rputs("\"session_selector\":\"", r);
        ap_rputs(user_login->session_selector, r);
        ap_rputs("\",\"session_token\":\"", r);
        ap_rputs(user_login->session_token, r);
        ap_rputs("\"}", r);
    }
    return OK;
fail:
    if(!r->header_only) {
        ap_rprintf(r, "{\"result\":\"%s\"}", parsegraph_nameUserStatus(rv));
    }
    return parsegraph_userStatusToHttp(rv);
}

static int parsegraph_beginUserLogin_handler(request_rec* r)
{
    ap_dbd_t* dbd = ap_dbd_acquire(r);
    if(!dbd) {
        ap_log_perror(
            APLOG_MARK, APLOG_ERR, 0, r->pool, "DBD must not be null."
        );
        return 500;
    }

    const char* username = 0;
    const char* password = 0;
    const char* remember = 0;
    apr_table_t* form_data = apr_table_make(r->pool, 2);
    int dbrv = parsegraph_flatten_form_data(r, form_data);
    if(dbrv != 0) {
        return dbrv;
    }
    username = apr_table_get(form_data, "username");
    password = apr_table_get(form_data, "password");
    remember = apr_table_get(form_data, "remember");
    if(!remember) {
        remember = "0";
    }

    struct parsegraph_user_login* user_login;
    parsegraph_UserStatus rv = parsegraph_beginUserLogin(
        r->pool, dbd, username, password, &user_login
    );
    if(rv != parsegraph_OK) {
        goto fail;
    }

    char buf[255];
    if(!strncmp("1", remember, 1)) {
        snprintf(buf, 255, "HttpOnly;Max-Age=315360000;Version=1");
    }
    else {
        snprintf(buf, 255, "HttpOnly;Version=1");
    }

    if(APR_SUCCESS != ap_cookie_write(r, "session", parsegraph_constructSessionString(r->pool,
        user_login->session_selector,
        user_login->session_token
    ), buf, 0, r->headers_out, NULL)) {
        ap_log_perror(
            APLOG_MARK, APLOG_ERR, 0, r->pool, "Failed to set session cookie."
        );
        return 500;
    }

    if(!r->header_only) {
        ap_rputs("{\"result\":\"Began new user login.\"", r);
        ap_rputs(", \"username\":\"", r);
        ap_rputs(user_login->username, r);
        ap_rputs("\"}", r);
    }

    return OK;
fail:
    if(!r->header_only) {
        ap_rprintf(r, "{\"result\":\"%s\"}", parsegraph_nameUserStatus(rv));
    }
    return parsegraph_userStatusToHttp(rv);
}

static int parsegraph_endUserLogin_handler(request_rec* r)
{
    struct parsegraph_user_login* userLogin = 0;
    parsegraph_UserStatus rv = parsegraph_authenticate(r, &userLogin);
    if(rv != parsegraph_OK) {
        goto fail;
    }

    apr_table_t* queryArgs;
    ap_args_to_table(r, &queryArgs);
    const char* username = apr_table_get(queryArgs, "username");

    // Default to removing the current user.
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
    rv = parsegraph_endUserLogin(
        r->pool, dbd, username, &logins_ended
    );
    if(rv != parsegraph_OK) {
        goto fail;
    }

    rv = parsegraph_removeSession(r);
    if(0 != rv) {
        return rv;
    }

    if(!r->header_only) {
        ap_rputs("{\"result\":\"Ended user logins.\"", r);
        ap_rputs(", \"username\":\"", r);
        ap_rputs(username, r);
        ap_rputs("\", \"logins_ended\":", r);
        ap_rprintf(r, "%u", (unsigned int)logins_ended);
        ap_rputs("}", r);
    }

    return OK;
fail:
    if(!r->header_only) {
        ap_rprintf(r, "{\"result\":\"%s\"}", parsegraph_nameUserStatus(rv));
    }
    return parsegraph_userStatusToHttp(rv);
}

static int parsegraph_removeUser_handler(request_rec* r)
{
    ap_dbd_t* dbd = ap_dbd_acquire(r);
    if(!dbd) {
        ap_log_perror(
            APLOG_MARK, APLOG_ERR, 0, r->pool, "DBD must not be null."
        );
        return 500;
    }

    struct parsegraph_user_login* userLogin = 0;
    parsegraph_UserStatus rv = parsegraph_authenticate(r, &userLogin);
    if(rv != parsegraph_OK) {
        goto fail;
    }

    apr_table_t* queryArgs;
    ap_args_to_table(r, &queryArgs);
    const char* username = apr_table_get(queryArgs, "username");
    // Default to the current user.
    if(!username) {
        username = r->user;
    }

    // Disallow ending other user's logins.
    if(strcmp(r->user, username)) {
        if(!r->header_only) {
            ap_rputs("{\"result\":\"User cannot end other user's logins.\"}", r);
        }
        return HTTP_BAD_REQUEST;
    }

    rv = parsegraph_removeUser(
        r->pool, dbd, username
    );
    if(rv != parsegraph_OK) {
        goto fail;
    }

    rv = parsegraph_removeSession(r);
    if(0 != rv) {
        return rv;
    }

    if(!r->header_only) {
        ap_rputs("{\"result\":\"User was removed.\"}", r);
    }
    return OK;
fail:
    if(!r->header_only) {
        ap_rprintf(r, "{\"result\":\"%s\"}", parsegraph_nameUserStatus(rv));
    }
    return parsegraph_userStatusToHttp(rv);
}

static int parsegraph_getIDForUsername_handler(request_rec* r)
{
    ap_dbd_t* dbd = ap_dbd_acquire(r);
    if(!dbd) {
        ap_log_perror(
            APLOG_MARK, APLOG_ERR, 0, r->pool, "DBD must not be null."
        );
        return 500;
    }

    struct parsegraph_user_login* userLogin = 0;
    parsegraph_UserStatus rv = parsegraph_authenticate(r, &userLogin);
    if(parsegraph_OK != rv) {
        goto fail;
    }

    apr_table_t* queryArgs;
    ap_args_to_table(r, &queryArgs);
    const char* username = apr_table_get(queryArgs, "username");
    // Default to the current user.
    if(!username) {
        username = r->user;
    }

    // Disallow getting ID for other users.
    if(strcmp(r->user, username)) {
        if(!r->header_only) {
            ap_rputs("{\"result\":\"User cannot retrieve ID for other users.\"}", r);
        }
        return HTTP_BAD_REQUEST;
    }

    int user_id;
    rv = parsegraph_getIdForUsername(
        r->pool, dbd,
        username, &user_id
    );
    if(rv != parsegraph_OK) {
        goto fail;
    }

    if(!r->header_only) {
        ap_rprintf(r, "{\"result\":%d}", user_id);
    }
    return OK;
fail:
    if(!r->header_only) {
        ap_rprintf(r, "{\"result\":\"%s\"}", parsegraph_nameUserStatus(rv));
    }
    return parsegraph_userStatusToHttp(rv);
}

static int parsegraph_authenticate_handler(request_rec* r)
{
    ap_dbd_t* dbd = ap_dbd_acquire(r);
    if(!dbd) {
        ap_log_perror(
            APLOG_MARK, APLOG_ERR, 0, r->pool, "DBD must not be null."
        );
        return 500;
    }

    struct parsegraph_user_login* userLogin = 0;
    parsegraph_UserStatus rv = parsegraph_authenticate(r, &userLogin);
    if(parsegraph_OK != rv) {
        goto fail;
    }

    if(!r->header_only) {
        ap_rprintf(r, "{\"username\":\"%s\"}", r->user);
    }
    return OK;
fail:
    if(!r->header_only) {
        ap_rprintf(r, "{\"result\":\"%s\"}", parsegraph_nameUserStatus(rv));
    }
    return parsegraph_userStatusToHttp(rv);
}

static int parsegraph_getUserProfile_handler(request_rec* r)
{
    ap_dbd_t* dbd = ap_dbd_acquire(r);
    if(!dbd) {
        ap_log_perror(
            APLOG_MARK, APLOG_ERR, 0, r->pool, "DBD must not be null."
        );
        return 500;
    }


    struct parsegraph_user_login* userLogin = 0;
    parsegraph_UserStatus rv = parsegraph_authenticate(r, &userLogin);
    if(parsegraph_OK != rv) {
        goto fail;
    }

    apr_table_t* queryArgs;
    ap_args_to_table(r, &queryArgs);
    const char* username = apr_table_get(queryArgs, "username");
    // Default to the current user.
    if(!username) {
        username = r->user;
    }

    // Disallow getting other user's profiles.
    if(strcmp(r->user, username)) {
        if(!r->header_only) {
            ap_rputs("{\"result\":\"User cannot retrieve profile for other users.\"}", r);
        }
        return HTTP_BAD_REQUEST;
    }

    const char* profile;
    rv = parsegraph_getUserProfile(
        r->pool, dbd,
        username, &profile
    );
    if(rv != parsegraph_OK) {
        goto fail;
    }

    if(!r->header_only) {
        ap_rprintf(r, "{\"status\":0, \"profile\":\"%s\"}", apr_pescape_urlencoded(r->pool, profile));
    }
    return OK;
fail:
    if(!r->header_only) {
        ap_rprintf(r, "{\"result\":\"%s\"}", parsegraph_nameUserStatus(rv));
    }
    return parsegraph_userStatusToHttp(rv);
}

static int parsegraph_changeUserPassword_handler(request_rec* r)
{
    ap_dbd_t* dbd = ap_dbd_acquire(r);
    if(!dbd) {
        ap_log_perror(
            APLOG_MARK, APLOG_ERR, 0, r->pool, "DBD must not be null."
        );
        return 500;
    }

    struct parsegraph_user_login* userLogin = 0;
    parsegraph_UserStatus rv = parsegraph_authenticate(r, &userLogin);
    if(parsegraph_OK != rv) {
        goto fail;
    }

    const char* username = 0;
    const char* password = 0;
    apr_table_t* form_data = apr_table_make(r->pool, 2);
    int dbrv = parsegraph_flatten_form_data(r, form_data);
    if(dbrv != 0) {
        return dbrv;
    }
    username = apr_table_get(form_data, "username");
    password = apr_table_get(form_data, "password");

    // Default to the current user.
    if(!username) {
        username = r->user;
    }

    // Disallow changing other user's passwords.
    if(strcmp(r->user, username)) {
        if(!r->header_only) {
            ap_rputs("{\"result\":\"User cannot change password for other users.\"}", r);
        }
        return HTTP_BAD_REQUEST;
    }

    rv = parsegraph_changeUserPassword(
        r->pool, dbd, username, password
    );
    if(rv != parsegraph_OK) {
        goto fail;
    }
    if(!r->header_only) {
        ap_rputs("{\"result\":\"The password was changed.\"}", r);
    }
    return OK;
fail:
    if(!r->header_only) {
        ap_rprintf(r, "{\"result\":\"%s\"}", parsegraph_nameUserStatus(rv));
    }
    return parsegraph_userStatusToHttp(rv);
}

static int parsegraph_setUserProfile_handler(request_rec* r)
{
    struct parsegraph_user_login* userLogin = 0;
    parsegraph_UserStatus rv = parsegraph_authenticate(r, &userLogin);
    if(parsegraph_OK != rv) {
        return rv;
    }

    // Disallow anonymous use.
    if(!r->user) {
        return HTTP_UNAUTHORIZED;
    }

    apr_table_t* queryArgs;
    ap_args_to_table(r, &queryArgs);
    const char* username = apr_table_get(queryArgs, "username");
    // Default to the current user.
    if(!username) {
        username = r->user;
    }

    // Disallow ending other user's logins.
    if(strcmp(r->user, username)) {
        // TODO Check if the user is an admin before failing.
        return HTTP_BAD_REQUEST;
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
        r->pool, dbd, username, profile
    )) {
        ap_log_perror(
            APLOG_MARK, APLOG_ERR, 0, r->pool, "Failed to set user profile."
        );
        return 500;
    }

    if(!r->header_only) {
        ap_rputs("{\"result\":\"Profile set.\"}", r);
    }
    return OK;
}

static struct handler_list* parsegraph_user_commands;

static int parsegraph_user_json_handler(request_rec *r)
{
    if(strcmp(r->handler, "parsegraph_user")) {
        return DECLINED;
    }

    const char* acceptHeader = apr_table_get(r->headers_in, "Accept");
    if(!acceptHeader || (NULL == strstr(acceptHeader, "application/json"))) {
        return DECLINED;
    }

    ap_log_perror(
        APLOG_MARK, APLOG_INFO, 0, r->pool, "parsegraph_user_json handling request"
    );

    // Indicate JSON as the returned data.
    r->content_type = "application/json";

    // Get the requested command.
    // Process query args
    apr_table_t* queryArgs;
    ap_args_to_table(r, &queryArgs);
    const char* command = apr_table_get(queryArgs, "command");
    const char* redirect = apr_table_get(queryArgs, "redirect");

    if(!command) {
        if(r->method_number == M_GET) {
            command = "parsegraph_getUserProfile";
        }
        else if(r->method_number == M_PUT) {
            command = "parsegraph_setUserProfile";
        }
        else if(r->method_number == M_DELETE) {
            command = "parsegraph_removeUser";
        }
    }
    int parsegraph_MAX_COMMAND_LENGTH = 255;
    size_t command_size = strnlen(command, parsegraph_MAX_COMMAND_LENGTH + 1);
    if(command_size > parsegraph_MAX_COMMAND_LENGTH) {
        ap_log_perror(
            APLOG_MARK, APLOG_ERR, 0, r->pool, "Command provided was too long."
        );
        return 400;
    }
    if(command_size < 2) {
        ap_log_perror(
            APLOG_MARK, APLOG_ERR, 0, r->pool, "Command provided was too short."
        );
        return 400;
    }

    // Select the handler.
    int (*handler)(request_rec*) = NULL;
    if(0 != parsegraph_handler_list_select(
        parsegraph_user_commands,
        command, command_size,
        &handler
    ) || handler == NULL) {
        ap_log_perror(
            APLOG_MARK, APLOG_ERR, 0, r->pool, "Command '%s' not found.", command
        );
        return DECLINED;
    }

    // Defer to the handler.
    int rv = handler(r);

    if(rv == 0) {
        if(redirect) {
            apr_table_set(r->headers_out, "Location", strnlen(redirect, 2) > 1 ? redirect : "/");
            return HTTP_SEE_OTHER;
        }
    }

    return rv;
}

static const char* parsegraph_user_command_names[] = {
    "",
    //"parsegraph_USERNAME_MAX_LENGTH",
    //"parsegraph_USERNAME_MIN_LENGTH",
    //"parsegraph_PASSWORD_MIN_LENGTH",
    //"parsegraph_PASSWORD_MAX_LENGTH",
    //"parsegraph_PASSWORD_SALT_LENGTH",
    //"parsegraph_upgradeUserTables",
    "parsegraph_createNewUser", // 1
    "parsegraph_removeUser", // 2
    "parsegraph_beginUserLogin", // 3
    "parsegraph_endUserLogin", // 4
    "parsegraph_getIDForUsername", // 5
    "parsegraph_getUserProfile", // 6
    "parsegraph_setUserProfile",  // 7
    "parsegraph_authenticate", // 8
    "parsegraph_changeUserPassword" // 9
    //"parsegraph_listUsers",
    //"parsegraph_hasUser",
    //"parsegraph_validateUsername",
    //"parsegraph_validatePassword",
    //"parsegraph_createPasswordSalt",
    //"parsegraph_encryptPassword"
};

static size_t parsegraph_user_NUM_COMMANDS = 10;

static int (*parsegraph_user_handlers[])(request_rec*) = {
    NULL,
    //parsegraph_USERNAME_MAX_LENGTH_handler,
    //parsegraph_USERNAME_MIN_LENGTH_handler,
    //parsegraph_PASSWORD_MIN_LENGTH_handler,
    //parsegraph_PASSWORD_MAX_LENGTH_handler,
    //parsegraph_PASSWORD_SALT_LENGTH_handler,
    //parsegraph_upgradeUserTables_handler,
    parsegraph_createNewUser_handler, // 1
    parsegraph_removeUser_handler, // 2
    parsegraph_beginUserLogin_handler, // 3
    parsegraph_endUserLogin_handler, // 4
    parsegraph_getIDForUsername_handler, // 5
    parsegraph_getUserProfile_handler, // 6
    parsegraph_setUserProfile_handler, // 7
    parsegraph_authenticate_handler, // 8
    parsegraph_changeUserPassword_handler // 9
    //parsegraph_listUsers_handler,
    //parsegraph_hasUser_handler,
    //parsegraph_validateUsername_handler,
    //parsegraph_validatePassword_handler,
    //parsegraph_createPasswordSalt_handler,
    //parsegraph_encryptPassword_handler
};

static void parsegraph_user_json_register_hooks(apr_pool_t *pool)
{
    if(sizeof(parsegraph_user_handlers) != sizeof(parsegraph_user_command_names)) {
        ap_log_perror(
            APLOG_MARK, APLOG_ERR, 0, pool, "Size mismatch for parsegraph_user commands."
        );
        return;
    }
    parsegraph_handler_list_make(
        pool,
        parsegraph_user_command_names,
        parsegraph_user_handlers,
        parsegraph_user_NUM_COMMANDS,
        &parsegraph_user_commands
    );

    ap_hook_handler(parsegraph_user_json_handler, NULL, NULL, APR_HOOK_MIDDLE);
}

/* Dispatch list for API hooks */
module AP_MODULE_DECLARE_DATA parsegraph_user_json_module = {
    STANDARD20_MODULE_STUFF,
    NULL,                  /* create per-dir    config structures */
    NULL,                  /* merge  per-dir    config structures */
    NULL,                  /* create per-server config structures */
    NULL,                  /* merge  per-server config structures */
    NULL,                  /* table of config file commands       */
    parsegraph_user_json_register_hooks  /* register hooks       */
};
