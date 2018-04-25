#ifndef mod_rainback_INCLUDED
#define mod_rainback_INCLUDED

#include "marla.h"
#include <parsegraph_Session.h>
#include <parsegraph_user.h>
#include <parsegraph_List.h>
#include <parsegraph_environment.h>
#include <apr_pools.h>
#include <time.h>
#include <string.h>
#include <dlfcn.h>
#include <apr_dso.h>

struct mod_rainback {
parsegraph_Session* session;
parsegraph_Session* worldSession;
apr_hash_t* cache;
apr_hash_t* templates;
};
typedef struct mod_rainback mod_rainback;
mod_rainback* mod_rainback_new(marla_Server* server);
void mod_rainback_destroy(mod_rainback* rb);

struct rainback_Page {
char* cacheKey;
unsigned char* data;
size_t length;
size_t headBoundary;
size_t capacity;
int refs;
int writeStage;
struct timespec expiry;
};
typedef struct rainback_Page rainback_Page;

rainback_Page* rainback_Page_new(const char* cacheKey);
int rainback_Page_write(rainback_Page* page, const void* buf, size_t len);
int rainback_Page_append(rainback_Page* page, const void* buf, size_t len);
int rainback_Page_prepend(rainback_Page* page, const void* buf, size_t len);
void rainback_Page_ref(rainback_Page* page);
void rainback_Page_unref(rainback_Page* page);
void rainback_Page_endHead(rainback_Page* page);
void mod_rainback_eachPage(mod_rainback* rb, void(*visitor)(mod_rainback*, const char*, rainback_Page*, void*), void* visitorData);

void mod_rainback_route(struct marla_Request* req, void* hookData);
void mod_rainback_init(struct marla_Server* server, enum marla_ServerModuleEvent e);

void rainback_pageHandler(struct marla_Request* req, enum marla_ClientEvent ev, void* data, int dataLen);
void rainback_generatePage(rainback_Page* page, mod_rainback* rb, const char* urlState, const char* url, parsegraph_user_login* login);
rainback_Page* rainback_getPage(mod_rainback* rb, const char* urlState, const char* url, parsegraph_user_login* login);
rainback_Page* rainback_getPageByKey(mod_rainback* rb, const char* cacheKey);
void rainback_removePageFromCache(mod_rainback* rb, const char* cacheKey);

// Template and context
enum rainback_VariableType {
rainback_VariableType_STRING,
rainback_VariableType_ENUMERATOR,
rainback_VariableType_HASH,
rainback_VariableType_BLANK
};

struct rainback_TemplateStep;
typedef struct rainback_TemplateStep rainback_TemplateStep;
struct rainback_Template;
typedef struct rainback_Template rainback_Template;

struct rainback_Context {
apr_hash_t* vars;
apr_pool_t* pool;
struct rainback_Context* parent;
};
typedef struct rainback_Context rainback_Context;

struct rainback_Variable {
enum rainback_VariableType type;
const char* name;
union {
const char* s;
rainback_Context* h;
struct {
void*(*func)(rainback_Template*, rainback_Context*, void**, void*);
void* data;
} e;
} data;
};
typedef struct rainback_Variable rainback_Variable;
rainback_Variable* rainback_Variable_new(rainback_Context* ctx, const char* name);
void rainback_Variable_clear(rainback_Variable* var);

struct rainback_TemplateStep {
void(*render)(mod_rainback*, rainback_Page*, rainback_Context*, void*);
void* renderData;
rainback_TemplateStep* nextStep;
};

struct rainback_TemplateProcessor {
void(*templateCommand)(rainback_Template* template, const char* command, void* procData);
void(*addStep)(rainback_Template* template, rainback_TemplateStep*, void* procData);
void* procData;
struct rainback_TemplateProcessor* prevProcessor;
};
typedef struct rainback_TemplateProcessor rainback_TemplateProcessor;

struct rainback_Template {
apr_pool_t* pool;
const char* path;
mod_rainback* rb;
rainback_TemplateProcessor* processor;
rainback_TemplateStep* firstStep;
rainback_TemplateStep* lastStep;
marla_FileEntry* fe;
rainback_Page* renderedPage;
};

enum TemplateParseStage {
TemplateParseStage_STATIC,
TemplateParseStage_COMMAND
};

rainback_Template* rainback_Template_new(mod_rainback* rb);
void rainback_Template_parseString(rainback_Template* te, unsigned char* str);
void rainback_Template_parseFile(rainback_Template* te, const char* path, const char* watchpath);
void rainback_Template_destroy(rainback_Template* te);
void rainback_Template_stringContent(rainback_Template* te, const char* content);
void rainback_Template_mappedContent(rainback_Template* te, const char* key);
void rainback_renderTemplate(mod_rainback* rb, const char* name, rainback_Context* context, rainback_Page* page);
void rainback_Template_render(rainback_Template* te, rainback_Context* context, rainback_Page* page);

rainback_Context* rainback_Context_new(apr_pool_t* pool);
rainback_Variable* rainback_Context_getVariable(rainback_Context* ctx, const char* name);
void rainback_Context_setVariable(rainback_Context* ctx, rainback_Variable* v);
const char* rainback_Context_getString(rainback_Context* ctx, const char* name);
void rainback_Context_setString(rainback_Context* ctx, const char* name, const char* value);
void rainback_Context_setHash(rainback_Context* ctx, const char* name, rainback_Context* hash);
void rainback_Context_setEnumerator(rainback_Context* ctx, const char* name, void*(*enumerator)(rainback_Template*, rainback_Context*, void**, void*), void* extra);
rainback_Context* rainback_Context_getHash(rainback_Context* ctx, const char* name);
void rainback_Context_setParent(rainback_Context* ctx, rainback_Context* par);
void rainback_Context_remove(rainback_Context* context, const char* name);

typedef void*(*rainback_Enumerator)(rainback_Template*, rainback_Context*, void**, void*);
rainback_Enumerator rainback_Context_getEnumerator(rainback_Context* ctx, const char* name, void** savePtr);
void rainback_Context_blank(rainback_Context* ctx, const char* name);

// Login
struct rainback_LoginResponse;
typedef struct rainback_LoginResponse rainback_LoginResponse;
void rainback_loginHandler(struct marla_Request* req, enum marla_ClientEvent ev, void* data, int dataLen);
void rainback_generateLoginPage(rainback_Page* page, mod_rainback* rb, const char* pageState, parsegraph_user_login* login);
void rainback_LoginResponse_destroy(rainback_LoginResponse* resp);
rainback_LoginResponse* rainback_LoginResponse_new(marla_Request* req, mod_rainback* rb);
void rainback_generateAlreadyLoggedInPage(rainback_Page* page, mod_rainback* rb, parsegraph_user_login* login);
void rainback_generateLoginSucceededPage(rainback_Page* page, mod_rainback* rb, parsegraph_user_login* login);
void rainback_generateLoginFailedPage(rainback_Page* page, mod_rainback* rb, parsegraph_user_login* login, char* username);
void rainback_generateBadUserOrPasswordPage(rainback_Page* page, mod_rainback* rb, parsegraph_user_login* login, char* username);

// Account
struct rainback_AccountResponse;
typedef struct rainback_AccountResponse rainback_AccountResponse;
void rainback_accountHandler(struct marla_Request* req, enum marla_ClientEvent ev, void* data, int dataLen);
void rainback_AccountResponse_destroy(rainback_AccountResponse* resp);
rainback_AccountResponse* rainback_AccountResponse_new(marla_Request* req, mod_rainback* rb);
void rainback_generateAccountPage(rainback_Page* page, mod_rainback* rb, parsegraph_user_login* login);

// Logout
struct rainback_LogoutResponse;
typedef struct rainback_LogoutResponse rainback_LogoutResponse;
void rainback_logoutHandler(struct marla_Request* req, enum marla_ClientEvent ev, void* data, int dataLen);
void rainback_generateLogoutPage(rainback_Page* page, mod_rainback* rb, const char* pageState, parsegraph_user_login* login);
void rainback_LogoutResponse_destroy(rainback_LogoutResponse* resp);
rainback_LogoutResponse* rainback_LogoutResponse_new(marla_Request* req, mod_rainback* rb);
void rainback_generateNotLoggedInPage(rainback_Page* page, mod_rainback* rb);
void rainback_generateLogoutSucceededPage(rainback_Page* page, mod_rainback* rb);
void rainback_generateLogoutFailedPage(rainback_Page* page, mod_rainback* rb);

// Signup
struct rainback_SignupResponse;
typedef struct rainback_SignupResponse rainback_SignupResponse;
void rainback_signupHandler(struct marla_Request* req, enum marla_ClientEvent ev, void* data, int dataLen);
void rainback_generateSignupPage(rainback_Page* page, mod_rainback* rb);
void rainback_SignupResponse_destroy(rainback_SignupResponse* resp);
rainback_SignupResponse* rainback_SignupResponse_new(marla_Request* req, mod_rainback* rb);

// Profile
struct rainback_ProfileResponse;
typedef struct rainback_ProfileResponse rainback_ProfileResponse;
void rainback_profileHandler(struct marla_Request* req, enum marla_ClientEvent ev, void* data, int dataLen);
void rainback_ProfileResponse_destroy(rainback_ProfileResponse* resp);
rainback_ProfileResponse* rainback_ProfileResponse_new(mod_rainback* rb);
void rainback_generateProfilePage(rainback_Page* page, mod_rainback* rb, const char* pageState, parsegraph_user_login* login);

// Website sections
void rainback_contactHandler(struct marla_Request* req, enum marla_ClientEvent ev, void* data, int datalen);

void rainback_userHandler(struct marla_Request* req, enum marla_ClientEvent ev, void* data, int dataLen);
void rainback_importHandler(struct marla_Request* req, enum marla_ClientEvent ev, void* data, int datalen);
void rainback_searchHandler(struct marla_Request* req, enum marla_ClientEvent ev, void* data, int datalen);
void rainback_environmentHandler(struct marla_Request* req, enum marla_ClientEvent ev, void* data, int datalen);
void rainback_paymentHandler(struct marla_Request* req, enum marla_ClientEvent ev, void* data, int datalen);
void rainback_subscribeHandler(struct marla_Request* req, enum marla_ClientEvent ev, void* data, int datalen);

int rainback_authenticateByCookie(marla_Request* req, mod_rainback* rb, parsegraph_user_login* login, char* cookie);

struct rainback_HomepageResponse;
typedef struct rainback_HomepageResponse rainback_HomepageResponse;
rainback_HomepageResponse* rainback_HomepageResponse_new(marla_Request* req, mod_rainback* rb);
void rainback_HomepageResponse_destroy(rainback_HomepageResponse* resp);
void rainback_homepageHandler(struct marla_Request* req, enum marla_ClientEvent ev, void* data, int dataLen);
void rainback_generateHomepage(rainback_Page* page, mod_rainback* rb, const char* pageState, parsegraph_user_login* login);

struct rainback_UserHandlerData {
mod_rainback* rb;
};
typedef struct rainback_UserHandlerData rainback_UserHandlerData;
rainback_UserHandlerData* rainback_UserHandlerData_new(marla_Request* req, mod_rainback* rb);

struct rainback_PaymentHandlerData {
mod_rainback* rb;
};
typedef struct rainback_PaymentHandlerData rainback_PaymentHandlerData;
rainback_PaymentHandlerData* rainback_PaymentHandlerData_new(marla_Request* req, mod_rainback* rb);

struct rainback_EnvironmentHandlerData {
mod_rainback* rb;
};
typedef struct rainback_EnvironmentHandlerData rainback_EnvironmentHandlerData;
rainback_EnvironmentHandlerData* rainback_EnvironmentHandlerData_new(marla_Request* req, mod_rainback* rb);

struct rainback_SubscribeResponse;
typedef struct rainback_SubscribeResponse rainback_SubscribeResponse;
rainback_SubscribeResponse* rainback_SubscribeResponse_new(marla_Request* req, mod_rainback* rb);
void rainback_SubscribeResponse_destroy(rainback_SubscribeResponse* resp);
void rainback_generateSubscribePage(rainback_Page* page, mod_rainback* rb, parsegraph_user_login* login);

rainback_Page* rainback_getKilledPage(mod_rainback* rb, int statusCode, const char* reason);

#include "marla.h"

#define MAX_MESSAGE_QUEUE 512
#define MAX_INIT_LENGTH 4096

typedef struct a_message {
    void *payload;
    size_t len;
} a_message;

struct list_item_progress {
    int listId;

};

struct printing_item {
int stage;
int error;
parsegraph_List_item** values;
size_t nvalues;
int listId;
struct printing_item* parentLevel;
struct printing_item* nextLevel;
size_t index;
};

struct parsegraph_live_session;
typedef struct parsegraph_live_session parsegraph_live_session;

typedef struct parsegraph_live_server {
    apr_pool_t* pool;
    a_message messages[MAX_MESSAGE_QUEUE];
    int receiveHead;
} parsegraph_live_server;

int initialize_parsegraph_live_session(parsegraph_live_session* session, mod_rainback* rb);
int parsegraph_printItem(marla_Request* req, parsegraph_live_session* session, struct printing_item* level);
int parsegraph_prepareEnvironment(parsegraph_live_session* session);
void rainback_live_environment_install(mod_rainback* rb, marla_Request* req);

int mod_rainback_acquireWorldStream(mod_rainback* rb);
int mod_rainback_releaseWorldStream(mod_rainback* rb, int commit);

struct rainback_EnvironmentResponse;
typedef struct rainback_EnvironmentResponse rainback_EnvironmentResponse;

rainback_EnvironmentResponse* rainback_EnvironmentResponse_new(marla_Request* req, mod_rainback* rb);
void rainback_EnvironmentResponse_destroy(rainback_EnvironmentResponse* resp);
void rainback_generateEnvironmentPage(rainback_Page* page, mod_rainback* rb);

struct rainback_AuthenticateResponse;
typedef struct rainback_AuthenticateResponse rainback_AuthenticateResponse;

rainback_AuthenticateResponse* rainback_AuthenticateResponse_new(marla_Request* req, mod_rainback* rb);
void rainback_AuthenticateResponse_destroy(rainback_AuthenticateResponse* resp);
void rainback_authenticateHandler(struct marla_Request* req, enum marla_ClientEvent ev, void* data, int dataLen);

struct rainback_SearchResponse;
typedef struct rainback_SearchResponse rainback_SearchResponse;
rainback_SearchResponse* rainback_SearchResponse_new(marla_Request* req, mod_rainback* rb);
void rainback_SearchResponse_destroy(rainback_SearchResponse* resp);
void rainback_generateSearchPage(rainback_Page* page, mod_rainback* rb, const char* uri);
void rainback_SearchHandler(struct marla_Request* req, enum marla_ClientEvent ev, void* data, int dataLen);

// Forgot password
struct rainback_ForgotPasswordResponse;
typedef struct rainback_ForgotPasswordResponse rainback_ForgotPasswordResponse;
rainback_ForgotPasswordResponse* rainback_ForgotPasswordResponse_new(mod_rainback* rb);
void rainback_ForgotPasswordResponse_destroy(rainback_ForgotPasswordResponse* resp);
void rainback_generateForgotPasswordPage(rainback_Page* page, mod_rainback* rb, const char* pageState, parsegraph_user_login* login);
void rainback_ForgotPasswordHandler(struct marla_Request* req, enum marla_ClientEvent ev, void* data, int dataLen);

// Import
struct rainback_ImportResponse;
typedef struct rainback_ImportResponse rainback_ImportResponse;
rainback_ImportResponse* rainback_ImportResponse_new(marla_Request* req, mod_rainback* rb);
void rainback_ImportResponse_destroy(rainback_ImportResponse* resp);
void rainback_generateImportPage(rainback_Page* page, mod_rainback* rb, parsegraph_user_login* login);
void rainback_ImportHandler(struct marla_Request* req, enum marla_ClientEvent ev, void* data, int dataLen);

// Contact
struct rainback_ContactResponse;
typedef struct rainback_ContactResponse rainback_ContactResponse;
rainback_ContactResponse* rainback_ContactResponse_new(marla_Request* req, mod_rainback* rb);
void rainback_ContactResponse_destroy(rainback_ContactResponse* resp);
void rainback_generateContactPage(rainback_Page* page, mod_rainback* rb, parsegraph_user_login* login);
void rainback_ContactHandler(struct marla_Request* req, enum marla_ClientEvent ev, void* data, int dataLen);

#endif // mod_rainback_INCLUDED
