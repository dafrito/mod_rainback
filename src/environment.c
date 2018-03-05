#include "httpd.h"
#include "http_config.h"
#include "apr_strings.h"
#include <apr_buckets.h>
#include "http_protocol.h"
#include "ap_config.h"
#include "util_cookies.h"
#include "util_script.h"
#include "parsegraph_environment.h"
#include <parsegraph_user.h>
#include <parsegraph_List.h>
#include <parsegraph_user_httpd.h>
#include <http_log.h>
#include <ctype.h>

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

static int printEnvironmentForGUID(request_rec *r, struct parsegraph_user_login* userLogin, parsegraph_GUID* env)
{
    // Get DBD.
    ap_dbd_t* dbd = ap_dbd_acquire(r);
    if(!dbd) {
        ap_log_perror(
            APLOG_MARK, APLOG_ERR, 0, r->pool, "Failed to retrieve DBD."
        );
        return 500;
    }

    // Get the title.
    const char* envTitle = 0;
    parsegraph_EnvironmentStatus erv = parsegraph_Environment_OK;
    erv = parsegraph_getEnvironmentTitleForGUID(r->pool, dbd, env, &envTitle);
    if(parsegraph_isSeriousEnvironmentError(erv)) {
        ap_log_perror(
            APLOG_MARK, APLOG_ERR, 0, r->pool, "Failed to retrieve environment title for %s.",
            envTitle
        );
        return parsegraph_environmentStatusToHttp(erv);
    }

    if(!envTitle) {
        envTitle = env->value;
    }

    ap_rprintf(r, "<a href=\"%s\">%s</a>\n", apr_pstrcat(r->pool, "/environment/", env->value, NULL), envTitle);
    return 200;
}

static int printEnvironmentForId(request_rec *r, struct parsegraph_user_login* userLogin, int envId)
{
    parsegraph_GUID envGUID;
    parsegraph_EnvironmentStatus erv;
    erv = parsegraph_getEnvironmentGUIDForId(r->pool, ap_dbd_acquire(r), envId, &envGUID);
    switch(erv) {
    case parsegraph_Environment_OK:
        break;
    case parsegraph_Environment_NOT_FOUND:
        return 404;
    default:
        return parsegraph_environmentStatusToHttp(erv);
    }

    return printEnvironmentForGUID(r, userLogin, &envGUID);
}

static int printEnvironmentList(request_rec *r, ap_dbd_t* dbd, struct parsegraph_user_login* userLogin, apr_dbd_results_t* savedEnvGUIDs)
{
    apr_dbd_row_t* envRow = 0;
    parsegraph_GUID guid;
    guid.value[36] = 0;
    ap_rputs("<ul>", r);
    while(0 == apr_dbd_get_row(dbd->driver, r->pool, savedEnvGUIDs, &envRow, -1)) {
        const char* savedEnvGUID = apr_dbd_get_entry(dbd->driver, envRow, 0);
        if(!savedEnvGUID) {
            // No GUID for this record.
            continue;
        }
        strncpy(guid.value, savedEnvGUID, 36);
        ap_rputs("<li>", r);
        printEnvironmentForGUID(r, userLogin, &guid);
    }
    ap_rputs("</ul>", r);
    return 0;
}

static int printSavedEnvironments(request_rec *r, struct parsegraph_user_login* userLogin)
{
    apr_dbd_results_t* savedEnvGUIDs = 0;
    ap_dbd_t* dbd = ap_dbd_acquire(r);
    parsegraph_EnvironmentStatus erv;
    erv = parsegraph_getSavedEnvironmentGUIDs(r->pool, dbd, userLogin->userId, &savedEnvGUIDs);
    ap_rprintf(r, "<h2>%s's saved environments</h2>", userLogin->username);
    if(parsegraph_isSeriousEnvironmentError(erv)) {
        return parsegraph_environmentStatusToHttp(erv);
    }
    return printEnvironmentList(r, dbd, userLogin, savedEnvGUIDs);
}

// Owned - All environments owned by the user if there are any environments owned by the authenticated user.
static int printOwnedEnvironments(request_rec *r, struct parsegraph_user_login* userLogin)
{
    apr_dbd_results_t* envs = 0;
    ap_dbd_t* dbd = ap_dbd_acquire(r);
    parsegraph_EnvironmentStatus erv;
    erv = parsegraph_getOwnedEnvironmentGUIDs(r->pool, dbd, userLogin->userId, &envs);
    ap_rprintf(r, "<h2>%s's owned environments</h2>", userLogin->username);
    if(parsegraph_isSeriousEnvironmentError(erv)) {
        return parsegraph_environmentStatusToHttp(erv);
    }
    return printEnvironmentList(r, dbd, userLogin, envs);
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


        // Online - All friends who are online.
        // Administration - All environments flagged 'admin' if the user is also an administrator.
        // Top - Only one exists per server.
        // Recent - Top X environments accessible to the user if there are any environments visited by the user.
        // Watched X - Top X environments tagged with name. One group shown for each watched tag.
        // Subscribed X - Top X environments owned by X. One group shown for each subscribed user.
        // Featured - Top X environments flagged 'featured' if there are any featured environments
        // Starting - All environments flagged 'starting' if user is less than X time created.
        // New - Top X newest environments
        // Random - X random environments if there are at least 5*X environments.
        // Most Popular - X most popular environments (If there are at least X environments and at least Y hits for any environment.)
        // Private - All environments private to the user if there are any non-public environments with a permission set for the authenticated user.
        // Published - All environments published by the user if there are any public environments owned by the authenticated user.



static int parsegraph_environment_guid_handler(request_rec *r)
{
    if(strcmp("parsegraph_environment_guid", r->handler)) {
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

    if(!acceptHeader || ((NULL == strstr(acceptHeader, "text/html")) && (NULL == strstr(acceptHeader, "*/*")))) {
        return DECLINED;
    }

    // Retrieve and validate the session token and selector.
    struct parsegraph_user_login* userLogin = 0;
    parsegraph_UserStatus rv = parsegraph_authenticate(r, &userLogin);
    if(parsegraph_isSeriousUserError(rv)) {
        ap_log_perror(
            APLOG_MARK, APLOG_ERR, 0, r->pool, "User authentication failed: %s", parsegraph_nameUserStatus(rv)
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

    // The path /environment/[guid] was requested.
    parsegraph_GUID requestedEnv;
    requestedEnv.value[36] = 0;
    strncpy(requestedEnv.value, &r->parsed_uri.path[strlen("/environment/")], 36);

    // Indicate HTML as the returned data.
    r->content_type = "text/html";

    if(r->method_number == M_POST) {
        apr_table_t* formData = apr_table_make(r->pool, 4);
        if(0 != parsegraph_flatten_form_data(r, formData)) {
            return HTTP_BAD_REQUEST;
        }
        const char* actionCommand = apr_table_get(formData, "action");
        if(!strcmp(actionCommand, "parsegraph_saveEnvironment")) {
            parsegraph_EnvironmentStatus erv;
            erv = parsegraph_saveEnvironment(r->pool, dbd, userLogin->userId, &requestedEnv, apr_table_get(formData, "state"));
            if(erv != parsegraph_Environment_OK) {
                if(parsegraph_isSeriousEnvironmentError(erv)) {
                    ap_log_perror(
                        APLOG_MARK, APLOG_ERR, 0, r->pool, "Environment saving failed: %s", parsegraph_nameEnvironmentStatus(erv)
                    );
                    return 500;
                }
            }
            r->status_line = "Environment saved.";
        }

        apr_table_set(r->headers_out, "Location", "/environment");
        return HTTP_SEE_OTHER;
    }

    if(!r->header_only) {
        // Print the header.
        ap_rputs("<!DOCTYPE html><html><head><title>", r);
        ap_rputs("Parsegraph", r);
        ap_rputs("</title>", r);
        ap_rputs("<link rel=\"stylesheet\" href=\"/sga.css\"/>", r);
        ap_rputs("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">", r);
        ap_rputs("<script src=\"/parsegraph-1.2.js\"></script>", r);
        ap_rputs("<script src=\"/parsegraph-widgets-1.2.js\"></script>", r);
        ap_rputs("</head>", r);
        ap_rputs("<body>", r);


        ap_rputs("<script>\n", r);
        ap_rputs("GUID = \"", r);
        ap_rputs(requestedEnv.value, r);
        ap_rputs("\";\n", r);

        ap_rputs("SGA = null;\n", r);
        ap_rputs("document.addEventListener(\"DOMContentLoaded\", function(event) {\n", r);
        ap_rputs("var sga = new parsegraph_SingleGraphApplication(GUID);\n", r);
        ap_rprintf(r, "sga.setCameraName(\"parsegraph-environment-%s\");\n", requestedEnv.value);
        ap_rputs("SGA = sga;\n", r);
        ap_rputs("sga.createSessionNode = function(graph, userLogin) {\n", r);
        ap_rputs("var car = new parsegraph_Caret('bu');\n", r);
        ap_rputs("car.setGlyphAtlas(graph.glyphAtlas());\n", r);
        //ap_rputs("car.label(\"Hello, \" + userLogin.username + \", from \" + GUID + \".\");", r);
        ap_rputs("return car.node();\n", r);
        ap_rputs("};\n", r);
        ap_rputs("sga.start(document.body);", r);
        ap_rputs("});\n", r);
        ap_rputs("</script>\n", r);

        // Output the footer.
        ap_rputs("</body></html>", r);
    }
    return OK;
}

enum parsegraph_json_TokenType {
    parsegraph_json_EOF,
    parsegraph_json_EOL,
    parsegraph_json_NAME,
    parsegraph_json_COMMA,
    parsegraph_json_LBRACK,
    parsegraph_json_RBRACK,
    parsegraph_json_DIVIDE,
    parsegraph_json_SINGLE_QUOTE,
    parsegraph_json_DOUBLE_QUOTE,
    parsegraph_json_BACK_QUOTE,
    parsegraph_json_DOT,
    parsegraph_json_ASSIGNMENT,
    parsegraph_json_EQUALS,
    parsegraph_json_IDENTITY,
    parsegraph_json_NOT,
    parsegraph_json_NOT_EQUALS,
    parsegraph_json_NOT_IDENTICAL,
    parsegraph_json_LPAREN,
    parsegraph_json_RPAREN,
    parsegraph_json_INTEGER,
    parsegraph_json_SEMICOLON
};

typedef struct parsegraph_json_Token {
    int type;
    const char* value;
} parsegraph_json_Token;

parsegraph_json_Token* parsegraph_json_Token_new(apr_pool_t* pool, int type, const char* value)
{
    parsegraph_json_Token* rv;
    rv = apr_palloc(pool, sizeof(*rv));
    rv->type = type;
    rv->value = value;
    return rv;
}

typedef struct parsegraph_json_error {
int errorType;
int listId;
const char* message;
struct parsegraph_json_error* next;
} parsegraph_json_error;

typedef struct parsegraph_json_Lexer {
    apr_pool_t* pool;
    ap_dbd_t* dbd;
    int listRootId;
    int listHeadId;
    const char* input;
    int index;
    size_t len;
    char c;
    int cc;

    parsegraph_json_error* firstError;
    parsegraph_json_error* errorHead;

    int(*reloader)(void*, struct parsegraph_json_Lexer*);
    void* reloaderData;
} parsegraph_json_Lexer;

// Based on 'Language Implementation Patterns' by Terence Parr
char parsegraph_json_Lexer_consume(parsegraph_json_Lexer* lexer);
parsegraph_json_Lexer* parsegraph_json_Lexer_new(apr_pool_t* pool, ap_dbd_t* dbd, const char* contentDisposition, int(*reloader)(void*, parsegraph_json_Lexer*), void* reloaderData)
{
    parsegraph_json_Lexer* lexer = apr_palloc(pool, sizeof(*lexer));
    lexer->pool = pool;
    lexer->dbd = dbd;
    int rv = parsegraph_List_new(pool, dbd, contentDisposition, &lexer->listRootId);
    switch(rv) {
        break;
    case HTTP_UNAUTHORIZED:
        // Content disposition was not a valid string.
        break;
    case -1:
        break;
    case 500:
        break;
    }
    lexer->listHeadId = -1;

    lexer->index = -1;
    lexer->input = 0;

    lexer->reloader = reloader;
    lexer->reloaderData = reloaderData;

    // Prime.
    parsegraph_json_Lexer_consume(lexer);

    return lexer;
}

const char* parsegraph_nameTokenType(enum parsegraph_json_TokenType tokenType)
{
    switch(tokenType) {
    case parsegraph_json_EOF:
        return "EOF";
    case parsegraph_json_NAME:
        return "NAME";
    case parsegraph_json_COMMA:
        return "COMMA";
    case parsegraph_json_LBRACK:
        return "LBRACK";
    case parsegraph_json_RBRACK:
        return "RBRACK";
    case parsegraph_json_LPAREN:
        return "LPAREN";
    case parsegraph_json_RPAREN:
        return "RPAREN";
    case parsegraph_json_DIVIDE:
        return "DIVIDE";
    case parsegraph_json_SINGLE_QUOTE:
        return "SINGLE_QUOTE";
    case parsegraph_json_DOUBLE_QUOTE:
        return "DOUBLE_QUOTE";
    case parsegraph_json_BACK_QUOTE:
        return "BACKQUOTE";
    case parsegraph_json_DOT:
        return "DOT";
    case parsegraph_json_ASSIGNMENT:
        return "ASSIGNMENT";
    case parsegraph_json_EQUALS:
        return "EQUALS";
    case parsegraph_json_IDENTITY:
        return "IDENTITY";
    case parsegraph_json_NOT:
        return "NOT";
    case parsegraph_json_NOT_EQUALS:
        return "NOT_EQUALS";
    case parsegraph_json_NOT_IDENTICAL:
        return "NOT_IDENTICAL";
    case parsegraph_json_INTEGER:
        return "INTEGER";
    case parsegraph_json_EOL:
        return "EOL";
    case parsegraph_json_SEMICOLON:
        return "SEMICOLON";
    }
    return 0;
}

/*parsegraph_Token.prototype.equals = function(other)
{
    if(this === other) {
        return true;
    }
    if(!other) {
        return false;
    }
    if(typeof other.type !== "function") {
        return false;
    }
    if(typeof other.text !== "function") {
        return false;
    }
    return this.type() == other.type() && this.text() == other.text();
};

parsegraph_Token.prototype.toString = function()
{
    var rv = parsegraph_nameTokenType(this.type());
    if(this.text() !== null) {
        rv += "=\"" + this.text() + "\"";
    }
    return rv;
};
*/

char parsegraph_json_Lexer_consume(parsegraph_json_Lexer* lexer)
{
    if(lexer->input && ++lexer->index < lexer->len) {
        lexer->c = lexer->input[lexer->index];
        lexer->cc = lexer->input[lexer->index];
        return lexer->c;
    }
    else if(1 == lexer->reloader(lexer->reloaderData, lexer)) {
        lexer->index = -1;
        // Reloaded.
        return parsegraph_json_Lexer_consume(lexer);
    }
    lexer->input = 0;
    lexer->len = 0;
    lexer->c = 0;
    lexer->cc = 0;
    return 0;
}

char parsegraph_json_Lexer_c(parsegraph_json_Lexer* lexer)
{
    return lexer->c;
}

int parsegraph_json_Lexer_cc(parsegraph_json_Lexer* lexer)
{
    return lexer->cc;
}

char parsegraph_json_Lexer_match(parsegraph_json_Lexer* lexer, char expected, int* matched)
{
    if(lexer->c == expected) {
        *matched = 1;
        return parsegraph_json_Lexer_consume(lexer);
    }
    *matched = 0;
    return 0;
}

parsegraph_json_error* parsegraph_json_Lexer_error(parsegraph_json_Lexer* lexer, const char* str, ...)
{
    parsegraph_json_error* err = apr_palloc(lexer->pool, sizeof(parsegraph_json_error));
    va_list ap;
    va_start(ap, str);
    err->message = apr_pvsprintf(lexer->pool, str, ap);
    va_end(ap);

    err->errorType = 0;
    err->listId = 0;
    err->next = 0;

    if(lexer->errorHead) {
        lexer->errorHead->next = err;
    }
    else {
        lexer->firstError = err;
    }
    lexer->errorHead = err;

    return err;
}

#define parsegraph_json_MAX_NAME_LENGTH 1024
#define parsegraph_json_MAX_STRING_LENGTH 4096

int parsegraph_json_Lexer_isLETTER(parsegraph_json_Lexer* lexer)
{
    if(parsegraph_json_Lexer_c(lexer) != 0 && isalpha(parsegraph_json_Lexer_c(lexer)) != 0) {
        return 1;
    }
    return 0;
}

int parsegraph_json_Lexer_isDIGIT(parsegraph_json_Lexer* lexer)
{
    if(parsegraph_json_Lexer_c(lexer) != 0 && isdigit(parsegraph_json_Lexer_c(lexer)) != 0) {
        return 1;
    }
    return 0;
}

int parsegraph_json_Lexer_isWS(parsegraph_json_Lexer* lexer)
{
    char c = parsegraph_json_Lexer_c(lexer);
    if(c != 0 && isspace(c)) {
        return 1;
    }
    return 0;
};

int parsegraph_json_Lexer_isNEWLINE(parsegraph_json_Lexer* lexer)
{
    char c = parsegraph_json_Lexer_c(lexer);
    if(c != 0 && (c == '\r' || c == '\n')) {
        return 1;
    }
    return 0;
};

parsegraph_json_Token* parsegraph_json_Lexer_NAME(parsegraph_json_Lexer* lexer, char expected, int* matched)
{
    char rv[parsegraph_json_MAX_NAME_LENGTH];
    int i = 0;
    do {
        rv[i] = parsegraph_json_Lexer_consume(lexer);
        if(rv[i] == 0 || i >= parsegraph_json_MAX_NAME_LENGTH) {
            return 0;
        }
        ++i;
    }
    while(1 == parsegraph_json_Lexer_isLETTER(lexer));

    return parsegraph_json_Token_new(lexer->pool, parsegraph_json_NAME, rv);
}

void parsegraph_json_Lexer_WS(parsegraph_json_Lexer* lexer)
{
    while(parsegraph_json_Lexer_isWS(lexer) != 0 && !parsegraph_json_Lexer_isNEWLINE(lexer)) {
        parsegraph_json_Lexer_consume(lexer);
    }
}

parsegraph_json_Token* parsegraph_json_Lexer_NUMBER(parsegraph_json_Lexer* lexer, char expected, int* matched)
{
    char rv[parsegraph_json_MAX_STRING_LENGTH];
    int i = 0;
    do {
        rv[i] = parsegraph_json_Lexer_consume(lexer);
        if(rv[i] == 0 || i >= parsegraph_json_MAX_STRING_LENGTH) {
            return 0;
        }
        ++i;
    }
    while(1 == parsegraph_json_Lexer_isDIGIT(lexer));

    return parsegraph_json_Token_new(lexer->pool, parsegraph_json_INTEGER, rv);
}

parsegraph_json_Token* parsegraph_json_Lexer_nextToken(parsegraph_json_Lexer* lexer)
{
    int matched;
    char c, quote;
    while((c = parsegraph_json_Lexer_c(lexer)) != 0) {
        switch(c) {
        case '/':
            parsegraph_json_Lexer_consume(lexer);
            if(parsegraph_json_Lexer_c(lexer) == '/') {
                // Comment.
                parsegraph_json_Lexer_consume(lexer);
                while(parsegraph_json_Lexer_c(lexer) != '\n') {
                    if(parsegraph_json_Lexer_consume(lexer) == 0) {
                        // EOF.
                        break;
                    }
                }

                continue;
            }
            else if(parsegraph_json_Lexer_c(lexer) == '*') {
                // Multi-line comment.
                parsegraph_json_Lexer_consume(lexer);
                while(1) {
                    while(parsegraph_json_Lexer_c(lexer) != '*') {
                        parsegraph_json_Lexer_consume(lexer);
                    }
                    parsegraph_json_Lexer_consume(lexer);
                    if(parsegraph_json_Lexer_c(lexer) == '/') {
                        // Comment ended.
                        break;
                    }

                    // Still in the multi-line comment.
                }

                continue;
            }
            return parsegraph_json_Token_new(lexer->pool, parsegraph_json_DIVIDE, "/");
        case '\'':
        case '"':
        case '`':
            quote = parsegraph_json_Lexer_c(lexer);
            if(!parsegraph_json_Lexer_consume(lexer)) {
                parsegraph_json_Lexer_error(lexer, "Unexpected start of string");
                return 0;
            }
            char str[parsegraph_json_MAX_STRING_LENGTH];
            int i = 0;
            while(i < parsegraph_json_MAX_STRING_LENGTH) {
                if(parsegraph_json_Lexer_c(lexer) == quote) {
                    parsegraph_json_Lexer_consume(lexer);
                    switch(quote) {
                    case '\'':
                        return parsegraph_json_Token_new(lexer->pool, parsegraph_json_SINGLE_QUOTE, str);
                    case '"':
                        return parsegraph_json_Token_new(lexer->pool, parsegraph_json_DOUBLE_QUOTE, str);
                    case '`':
                        return parsegraph_json_Token_new(lexer->pool, parsegraph_json_BACK_QUOTE, str);
                    default:
                        parsegraph_json_Lexer_error(lexer, "Unrecognized quote symbol: %c",  quote);
                        return 0;
                    }
                }

                str[i++] = parsegraph_json_Lexer_c(lexer);
                if(!parsegraph_json_Lexer_consume(lexer)) {
                    break;
                }
            }
            parsegraph_json_Lexer_error(lexer, "Unterminated string");
            return 0;
        case ' ':
        case '\t':
            {
                char c = parsegraph_json_Lexer_c(lexer);
                while(c != 0 && (c == ' ' || c == '\t')) {
                    parsegraph_json_Lexer_consume(lexer);
                }
            }
            continue;
        case '\r':
        case '\n':
            {
                char c = parsegraph_json_Lexer_c(lexer);
                while(c != 0 && (c == '\r' || c == '\n')) {
                    parsegraph_json_Lexer_consume(lexer);
                }
            }
            //parsegraph_json_Token_new(apr_pool_t* pool, int type, const char* value)
            return parsegraph_json_Token_new(lexer->pool, parsegraph_json_EOL, 0);
            continue;
        case ',':
            parsegraph_json_Lexer_consume(lexer);
            return parsegraph_json_Token_new(lexer->pool, parsegraph_json_COMMA, 0);
        case '.':
            parsegraph_json_Lexer_consume(lexer);
            return parsegraph_json_Token_new(lexer->pool, parsegraph_json_DOT, 0);
        case '(':
            parsegraph_json_Lexer_consume(lexer);
            return parsegraph_json_Token_new(lexer->pool, parsegraph_json_LPAREN, 0);
        case ')':
            parsegraph_json_Lexer_consume(lexer);
            return parsegraph_json_Token_new(lexer->pool, parsegraph_json_RPAREN, 0);
        case '!':
            parsegraph_json_Lexer_consume(lexer);
            if(parsegraph_json_Lexer_c(lexer) == '=') {
                parsegraph_json_Lexer_consume(lexer);
                if(parsegraph_json_Lexer_c(lexer) == '=') {
                    // Identity
                    return parsegraph_json_Token_new(lexer->pool, parsegraph_json_NOT_IDENTICAL, 0);
                }
                // Equality
                return parsegraph_json_Token_new(lexer->pool, parsegraph_json_NOT_EQUALS, 0);
            }
            // Assignment
            return parsegraph_json_Token_new(lexer->pool, parsegraph_json_NOT, 0);
        case '=':
            parsegraph_json_Lexer_consume(lexer);
            if(parsegraph_json_Lexer_c(lexer) == '=') {
                parsegraph_json_Lexer_consume(lexer);
                if(parsegraph_json_Lexer_c(lexer) == '=') {
                    // Identity
                    return parsegraph_json_Token_new(lexer->pool, parsegraph_json_IDENTITY, 0);
                }
                // Equality
                return parsegraph_json_Token_new(lexer->pool, parsegraph_json_EQUALS, 0);
            }
            // Assignment
            return parsegraph_json_Token_new(lexer->pool, parsegraph_json_ASSIGNMENT, 0);
        case '[':
            parsegraph_json_Lexer_consume(lexer);
            return parsegraph_json_Token_new(lexer->pool, parsegraph_json_LBRACK, 0);
        case ']':
            parsegraph_json_Lexer_consume(lexer);
            return parsegraph_json_Token_new(lexer->pool, parsegraph_json_RBRACK, 0);
        case ';':
            parsegraph_json_Lexer_consume(lexer);
            return parsegraph_json_Token_new(lexer->pool, parsegraph_json_SEMICOLON, 0);
        default:
            if(parsegraph_json_Lexer_c(lexer) == '-' || parsegraph_json_Lexer_isDIGIT(lexer)) {
                return parsegraph_json_Lexer_NUMBER(lexer, 0, &matched);
            }
            if(parsegraph_json_Lexer_isLETTER(lexer)) {
                return parsegraph_json_Lexer_NAME(lexer, 0, &matched);
            }
            parsegraph_json_Lexer_error(lexer, "Invalid character: %c", parsegraph_json_Lexer_c(lexer));
            return 0;
        }
    }

    return parsegraph_json_Token_new(lexer->pool, parsegraph_json_EOF, 0);
}

struct parsegraph_bucketReloaderData {
    apr_bucket_brigade* bb;
    ap_filter_t* input_filters;
};

int parsegraph_bucketReloader(void* reloaderData, parsegraph_json_Lexer* lexer)
{
    struct parsegraph_bucketReloaderData* d = reloaderData;

    if(APR_BRIGADE_EMPTY(d->bb)) {
        // Done reading.
        return 0;
    }

    apr_bucket* b = APR_BRIGADE_FIRST(d->bb);
    const char *data;
    apr_size_t length;
    apr_status_t rv = apr_bucket_read(b, &data, &length, APR_BLOCK_READ);
    if(rv != APR_SUCCESS) {
        char buf[1024];
        apr_strerror(rv, buf, sizeof(buf));
        ap_log_perror(
            APLOG_MARK, APLOG_ERR, 0, lexer->pool, "apr_bucket_read failed: %s ", buf
        );
        return 0;
    }

    if(length == 0) {
        // Bucket empty, delete it and recurse.
        apr_bucket_delete(b);
        return parsegraph_bucketReloader(reloaderData, lexer);
    }
    lexer->input = data;
    lexer->len = length;
    lexer->index = -1;
    return 1;
}

static int parsegraph_environment_handler(request_rec *r)
{
    if(strcmp(r->handler, "parsegraph_environment")) {
        return DECLINED;
    }

    // The path /environment was requested.

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

    if(!acceptHeader || ((NULL == strstr(acceptHeader, "text/html")) && (NULL == strstr(acceptHeader, "*/*")))) {
        return DECLINED;
    }

    ap_log_perror(
        APLOG_MARK, APLOG_INFO, 0, r->pool, "parsegraph_environment handling request"
    );
    r->status_line = 0;

    // Indicate HTML as the returned data.
    r->content_type = "text/html";

    // Retrieve and validate the session token and selector.
    struct parsegraph_user_login* userLogin = 0;
    parsegraph_UserStatus rv = parsegraph_authenticate(r, &userLogin);
    if(parsegraph_isSeriousUserError(rv)) {
        ap_log_perror(
            APLOG_MARK, APLOG_ERR, 0, r->pool, "User authentication failed: %s", parsegraph_nameUserStatus(rv)
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

    apr_table_t* formData = apr_table_make(r->pool, 4);
    if(r->method_number == M_POST) {
        if(0 != parsegraph_flatten_form_data(r, formData)) {
            return HTTP_BAD_REQUEST;
        }
        const char* actionCommand = apr_table_get(formData, "action");
        if(!actionCommand) {
            return HTTP_NOT_FOUND;
        }
        if(!strcmp(actionCommand, "parsegraph_createEnvironment")) {
            parsegraph_GUID createdEnv;
            parsegraph_EnvironmentStatus erv = parsegraph_createEnvironment(
                r->pool, dbd, userLogin->userId, 0, 0, &createdEnv);
            if(erv != parsegraph_Environment_OK) {
                return parsegraph_environmentStatusToHttp(erv);
            }
            apr_table_set(r->headers_out, "Location", apr_pstrcat(r->pool,
                "/environment/", createdEnv.value, NULL
            ));
            r->status = HTTP_SEE_OTHER;
            ap_rprintf(r, "<a href='/environment/%s'>Environment created, redirecting....</a>", createdEnv.value);
            return OK;
        }

        return HTTP_NOT_FOUND;
    }
    else if(r->method_number == M_PUT) {
        const char* givenType = apr_table_get(r->headers_in, "Content-Type");

        struct parsegraph_bucketReloaderData brd;
        brd.bb = apr_brigade_create(r->pool, apr_bucket_alloc_create(r->pool));
        apr_status_t rv = ap_get_brigade(
            r->input_filters, brd.bb, AP_MODE_READBYTES, APR_BLOCK_READ, 1024
        );
        if(rv == AP_NOBODY_READ) {
            ap_log_perror(
                APLOG_MARK, APLOG_ERR, 0, r->pool, "Failed to get brigade."
            );
            return 500;
        }
        brd.input_filters = r->input_filters;
        int parentListId = 0;

        const char* transactionName = 0;
        if(!strcmp(givenType, "application/json")) {
            transactionName = "parsegraph_put_application_json";
            if(parsegraph_OK != parsegraph_beginTransaction(r->pool, dbd, transactionName)) {
                return parsegraph_Environment_INTERNAL_ERROR;
            }

            parsegraph_json_Lexer* lexer = parsegraph_json_Lexer_new(r->pool, dbd, "json", parsegraph_bucketReloader, &brd);
            if(0 != parsegraph_List_appendItem(lexer->pool, lexer->dbd, lexer->listRootId, 0, "Line", &parentListId)) {
                parsegraph_json_Lexer_error(lexer, "Failed to create line");
                parsegraph_rollbackTransaction(lexer->pool, dbd, transactionName);
                return 0;
            }
            for(parsegraph_json_Token* t = parsegraph_json_Lexer_nextToken(lexer); t != 0 && t->type != parsegraph_json_EOF; t = parsegraph_json_Lexer_nextToken(lexer)) {
                switch(t->type) {
                case parsegraph_json_EOL:
                break;
                }
                int outItemId;
                if(0 != parsegraph_List_appendItem(lexer->pool, lexer->dbd, parentListId, t->type, t->value, &outItemId)) {
                    parsegraph_json_Lexer_error(lexer, "Failed to append item");
                    parsegraph_rollbackTransaction(lexer->pool, dbd, transactionName);
                    return 0;
                }
            }
            parentListId = lexer->listRootId;
        }
        else {
            return HTTP_NOT_ACCEPTABLE;
        }
        parsegraph_GUID createdEnv;
        parsegraph_EnvironmentStatus erv = parsegraph_createEnvironment(
            r->pool, dbd, userLogin->userId, parentListId, 0, &createdEnv);
        if(erv != parsegraph_Environment_OK) {
            parsegraph_rollbackTransaction(r->pool, dbd, transactionName);
            return parsegraph_environmentStatusToHttp(erv);
        }

        // Create a link for the user to place.
        int linkItem = 0;
        erv = parsegraph_createEnvironmentLink(r->pool, dbd, userLogin->userId, &createdEnv, &linkItem);
        if(erv != parsegraph_Environment_OK) {
            parsegraph_rollbackTransaction(r->pool, dbd, transactionName);
            return parsegraph_environmentStatusToHttp(erv);
        }
        erv = parsegraph_pushItemIntoStorage(r->pool, dbd, userLogin->userId, linkItem);
        if(erv != parsegraph_Environment_OK) {
            parsegraph_rollbackTransaction(r->pool, dbd, transactionName);
            return parsegraph_environmentStatusToHttp(erv);
        }

        if(parsegraph_OK != parsegraph_commitTransaction(r->pool, dbd, transactionName)) {
            parsegraph_rollbackTransaction(r->pool, dbd, transactionName);
            return 500;
        }

        // Redirect the user.
        apr_table_set(r->headers_out, "Location", apr_pstrcat(r->pool,
            "/environment/", createdEnv.value, NULL
        ));
        r->status = HTTP_SEE_OTHER;

        if(!r->header_only) {
            ap_rprintf(r, "<a href='/environment/%s'>Environment imported, redirecting....</a>", createdEnv.value);
            ap_rprintf(r, "<p>%s", givenType);
        }
        return OK;
    }

    // Print nothing if request method is HEAD.
    if(r->header_only) {
        return 200;
    }

    // Print the header.
    ap_rputs("<!DOCTYPE html><html><head><title>", r);
    ap_rputs("Parsegraph", r);
    ap_rputs("</title>", r);
    ap_rputs("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0,user-scalable=no\">", r);
    ap_rputs("<link rel=\"stylesheet\" href=\"/environment.css\"/>", r);
    ap_rputs("<script src=\"/parsegraph-1.2.js\"></script>", r);
    ap_rputs("</head>", r);
    ap_rputs("<body>", r);

    // Print the status as text.
    if(r->status_line) {
        ap_rputs(r->status_line, r);
    }

    if(userLogin && r->user) {
        // Show environments for this user.
        ap_rputs("<form method='post'>", r);
        ap_rputs("<input type=hidden name=action value='parsegraph_createEnvironment'>", r);
        ap_rputs("<input style='font-size: 2.5em' type=submit value='Create New Environment'>", r);
        ap_rputs("</form>", r);

        // Saved - All environments saved by the authenticated user
        printSavedEnvironments(r, userLogin);
        // Invited - All invites for this user.
        // Online - All friends who are online.
        // Administration - All environments flagged 'admin' if the user is also an administrator.
        // Top - Only one exists per server.
        // Recent - Top X environments accessible to the user if there are any environments visited by the user.
        // Watched X - Top X environments tagged with name. One group shown for each watched tag.
        // Subscribed X - Top X environments owned by X. One group shown for each subscribed user.
        // Featured - Top X environments flagged 'featured' if there are any featured environments
        // Starting - All environments flagged 'starting' if user is less than X time created.
        // New - Top X newest environments
        // Random - X random environments if there are at least 5*X environments.
        // Most Popular - X most popular environments (If there are at least X environments and at least Y hits for any environment.)
        // Private - All environments private to the user if there are any non-public environments with a permission set for the authenticated user.
        // Owned - All environments owned by the user if there are any environments owned by the authenticated user.
        printOwnedEnvironments(r, userLogin);
        // Published - All environments published by the user if there are any public environments owned by the authenticated user.
    }
    else {
        // Show public and member-accessible environments to this anonymous user.
        ap_rputs("<h1><a href='..'>Parsegraph</a> Environments</h1>", r);
        ap_rputs("Show all public and member-accessible environment lists.", r);
    }

    // Output the footer.
    ap_rputs("</body></html>", r);
    return OK;
}

static void parsegraph_environment_html_register_hooks(apr_pool_t *pool)
{
    ap_hook_handler(parsegraph_environment_guid_handler, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_handler(parsegraph_environment_handler, NULL, NULL, APR_HOOK_MIDDLE);
}

/* Dispatch list for API hooks */
module AP_MODULE_DECLARE_DATA parsegraph_environment_html_module = {
    STANDARD20_MODULE_STUFF,
    NULL,                  /* create per-dir    config structures */
    NULL,                  /* merge  per-dir    config structures */
    NULL,                  /* create per-server config structures */
    NULL,                  /* merge  per-server config structures */
    NULL,                  /* table of config file commands       */
    parsegraph_environment_html_register_hooks  /* register hooks       */
};
