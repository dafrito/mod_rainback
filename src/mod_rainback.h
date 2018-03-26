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
};
typedef struct mod_rainback mod_rainback;
mod_rainback* mod_rainback_new(marla_Server* server);
void mod_rainback_destroy(mod_rainback* rb);

struct rainback_Page {
char* cacheKey;
unsigned char* data;
size_t contentLength;
size_t headLength;
size_t capacity;
int refs;
int writeStage;
struct timespec expiry;
};
typedef struct rainback_Page rainback_Page;

rainback_Page* rainback_Page_new(const char* cacheKey);
int rainback_Page_write(rainback_Page* page, void* buf, size_t len);
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

struct rainback_ContactHandlerData {
mod_rainback* rb;
};
typedef struct rainback_ContactHandlerData rainback_ContactHandlerData;
rainback_ContactHandlerData* rainback_ContactHandlerData_new(marla_Request* req, mod_rainback* rb);

struct rainback_ImportHandlerData {
mod_rainback* rb;
};
typedef struct rainback_ImportHandlerData rainback_ImportHandlerData;
rainback_ImportHandlerData* rainback_ImportHandlerData_new(marla_Request* req, mod_rainback* rb);

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

struct rainback_SearchHandlerData {
mod_rainback* rb;
};
typedef struct rainback_SearchHandlerData rainback_SearchHandlerData;
rainback_SearchHandlerData* rainback_SearchHandlerData_new(marla_Request* req, mod_rainback* rb);

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

struct rainback_SubscribeHandlerData {
mod_rainback* rb;
};
typedef struct rainback_SubscribeHandlerData rainback_SubscribeHandlerData;
rainback_SubscribeHandlerData* rainback_SubscribeHandlerData_new(marla_Request* req, mod_rainback* rb);
rainback_Page* rainback_getKilledPage(mod_rainback* rb, const char* reason, const char* url);

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

#endif // mod_rainback_INCLUDED
